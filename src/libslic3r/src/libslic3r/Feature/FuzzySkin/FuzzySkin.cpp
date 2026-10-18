#include <memory>
#include <random>

#include <libnoise/noise.h>

#include "libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

#include "FuzzySkin.hpp"

using namespace Slic3r;

namespace Slic3r::Feature::FuzzySkin {

// Produces a random value between 0 and 1. Thread-safe.
static double random_value()
{
    thread_local std::random_device rd;
    // Hash thread ID for random number seed if no hardware rng seed is available
    thread_local std::mt19937                           gen(rd.entropy() > 0 ? rd() : std::hash<std::thread::id>()(std::this_thread::get_id()));
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

FuzzySkinConfig::FuzzySkinConfig(const PrintRegionConfigView &config, const int layer_id)
    : type(config.get<Domain::FuzzySkinType>("fuzzy_skin"))
    , thickness(scaled<coord_t>(config.get<double>("fuzzy_skin_thickness")))
    , point_distance(scaled<coord_t>(config.get<double>("fuzzy_skin_point_dist")))
    , first_layer(config.get<bool>("fuzzy_skin_first_layer"))
    , noise_type(config.get<Domain::FuzzySkinNoiseType>("fuzzy_skin_noise_type"))
    , noise_scale(config.get<double>("fuzzy_skin_scale"))
    , noise_octaves(config.get<int>("fuzzy_skin_octaves"))
    , noise_persistence(config.get<double>("fuzzy_skin_persistence"))
    , mode(config.get<Domain::FuzzySkinMode>("fuzzy_skin_mode"))
    , ripples_per_layer(config.get<int>("fuzzy_skin_ripples_per_layer"))
    , ripple_offset(config.get<Domain::Percentage>("fuzzy_skin_ripple_offset").value)
    , layers_between_ripple_offset(config.get<int>("fuzzy_skin_layers_between_ripple_offset"))
    , layer_id(layer_id)
{}

// Orca: the classic noise, uniform in [-1, 1]
class UniformNoise : public noise::module::Module
{
public:
    UniformNoise() : Module(GetSourceModuleCount()) {}
    int    GetSourceModuleCount() const override { return 0; }
    double GetValue(double, double, double) const override { return random_value() * 2 - 1; }
};

static std::unique_ptr<noise::module::Module> get_noise_module(const FuzzySkinConfig &cfg)
{
    using Domain::FuzzySkinNoiseType;
    if (cfg.noise_type == FuzzySkinNoiseType::Perlin) {
        auto perlin_noise = std::make_unique<noise::module::Perlin>();
        perlin_noise->SetFrequency(1 / cfg.noise_scale);
        perlin_noise->SetOctaveCount(cfg.noise_octaves);
        perlin_noise->SetPersistence(cfg.noise_persistence);
        return perlin_noise;
    } else if (cfg.noise_type == FuzzySkinNoiseType::Billow) {
        auto billow_noise = std::make_unique<noise::module::Billow>();
        billow_noise->SetFrequency(1 / cfg.noise_scale);
        billow_noise->SetOctaveCount(cfg.noise_octaves);
        billow_noise->SetPersistence(cfg.noise_persistence);
        return billow_noise;
    } else if (cfg.noise_type == FuzzySkinNoiseType::RidgedMulti) {
        auto ridged_multi_noise = std::make_unique<noise::module::RidgedMulti>();
        ridged_multi_noise->SetFrequency(1 / cfg.noise_scale);
        ridged_multi_noise->SetOctaveCount(cfg.noise_octaves);
        return ridged_multi_noise;
    } else if (cfg.noise_type == FuzzySkinNoiseType::Voronoi) {
        auto voronoi_noise = std::make_unique<noise::module::Voronoi>();
        voronoi_noise->SetFrequency(1 / cfg.noise_scale);
        voronoi_noise->SetDisplacement(1.0);
        return voronoi_noise;
    }
    return std::make_unique<UniformNoise>();
}

// Ripple noise: a sine wave along the path's arc length, cfg.ripples_per_layer periods per loop, anchored at the
// leftmost Y = 0 crossing of the loop (a peak there without phase shift), shifted by cfg.ripple_offset of a
// wavelength every cfg.layers_between_ripple_offset layers.
static double ripple_phase_shift_rad(const FuzzySkinConfig &cfg)
{
    if (cfg.ripple_offset == 0.0 || cfg.layers_between_ripple_offset <= 0)
        return 0.0;
    const int    effective_layer = std::max(cfg.layer_id, 0);
    const int    period_index    = effective_layer / std::max(cfg.layers_between_ripple_offset, 1);
    const double raw_shift       = period_index * (cfg.ripple_offset / 100) * (2.0 * M_PI);
    return fmod(raw_shift, 2.0 * M_PI);
}

// The arc length (mm) of the ripple anchor along the polygon: the leftmost Y = 0 crossing, else the vertex closest
// to Y = 0.
static double ripple_anchor_arc_mm(const Points &poly)
{
    const size_t np = poly.size();
    Vec2d anchor_world(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    bool  found_crossing = false;
    for (size_t i = 0; i < np; ++i) {
        const double ya = unscaled<double>(poly[i].y());
        const double yb = unscaled<double>(poly[(i + 1) % np].y());
        if ((ya <= 0.0 && yb >= 0.0) || (ya >= 0.0 && yb <= 0.0)) {
            const double t       = (std::abs(yb - ya) < 1e-9) ? 0.0 : ya / (ya - yb);
            const double x_cross = unscaled<double>(poly[i].x()) +
                std::max(0.0, std::min(1.0, t)) * (unscaled<double>(poly[(i + 1) % np].x()) - unscaled<double>(poly[i].x()));
            if (!found_crossing || x_cross < anchor_world.x()) {
                anchor_world   = Vec2d(x_cross, 0.0);
                found_crossing = true;
            }
        }
    }
    if (!found_crossing) {
        double best_abs_y = std::numeric_limits<double>::max();
        for (const Point &p : poly) {
            const double ay = std::abs(unscaled<double>(p.y()));
            if (ay < best_abs_y) {
                best_abs_y   = ay;
                anchor_world = Vec2d(unscaled<double>(p.x()), unscaled<double>(p.y()));
            }
        }
    }

    double anchor_arc_mm = 0.0;
    double best_dist_sq  = std::numeric_limits<double>::max();
    double accum_mm      = 0.0;
    for (size_t i = 0; i < np; ++i) {
        const Vec2d  pa_mm(unscaled<double>(poly[i].x()), unscaled<double>(poly[i].y()));
        const Vec2d  pb_mm(unscaled<double>(poly[(i + 1) % np].x()), unscaled<double>(poly[(i + 1) % np].y()));
        const Vec2d  seg     = pb_mm - pa_mm;
        const double seg_len = seg.norm();
        if (seg_len > 1e-9) {
            const double t       = std::max(0.0, std::min(1.0, (anchor_world - pa_mm).dot(seg) / (seg_len * seg_len)));
            const double dist_sq = (pa_mm + seg * t - anchor_world).squaredNorm();
            if (dist_sq < best_dist_sq) {
                best_dist_sq  = dist_sq;
                anchor_arc_mm = accum_mm + t * seg_len;
            }
        }
        accum_mm += seg_len;
    }
    return anchor_arc_mm;
}

// The ripple, resampling the closed polygon every cfg.point_distance.
static void fuzzy_polyline_ripple(Points &poly, const FuzzySkinConfig &cfg)
{
    const double amplitude    = unscaled<double>(cfg.thickness);
    const double N            = static_cast<double>(cfg.ripples_per_layer);
    const double fill_step_mm = unscaled<double>(cfg.point_distance);
    if (N <= 0.0 || fill_step_mm < 1e-6)
        return;

    const size_t np           = poly.size();
    double       perimeter_mm = 0.0;
    for (size_t i = 0; i < np; ++i)
        perimeter_mm += unscaled<double>((poly[(i + 1) % np] - poly[i]).cast<double>().norm());
    if (perimeter_mm < 1e-6)
        return;

    const double anchor_arc_mm   = ripple_anchor_arc_mm(poly);
    const double phase_at_anchor = M_PI * 2.0 + ripple_phase_shift_rad(cfg);
    auto arc_phase = [&](double arc_mm) { return N * (2.0 * M_PI) * (arc_mm - anchor_arc_mm) / perimeter_mm + phase_at_anchor; };

    Points out;
    out.reserve(static_cast<size_t>(perimeter_mm / fill_step_mm) + np * 2);
    double accum_mm = 0.0;
    for (size_t i = 0; i < np; ++i) {
        const Point &p0      = poly[i];
        const Point &p1      = poly[(i + 1) % np];
        const Vec2d  seg     = (p1 - p0).cast<double>();
        const double seg_len = seg.norm();
        if (seg_len < EPSILON)
            continue;
        const double seg_len_mm = unscaled<double>(seg_len);
        const Vec2d  seg_perp   = perp(Vec2d(seg / seg_len));
        const double seg_end_mm = accum_mm + seg_len_mm;
        for (double s = std::ceil(accum_mm / fill_step_mm) * fill_step_mm; s < seg_end_mm; s += fill_step_mm) {
            const double t    = (s - accum_mm) / seg_len_mm;
            const double disp = std::sin(arc_phase(s)) * amplitude;
            const Point  pt   = p0 + (seg * t).cast<coord_t>();
            out.emplace_back(pt + (seg_perp * scale_(disp)).cast<coord_t>());
        }
        accum_mm = seg_end_mm;
    }
    while (out.size() < 3)
        out.emplace_back(poly[poly.size() - 2]);
    if (out.size() >= 3)
        poly = std::move(out);
}

// The ripple on an Arachne extrusion line, keeping the junction widths.
static void fuzzy_extrusion_line_ripple(Arachne::ExtrusionJunctions &ext_lines, const FuzzySkinConfig &cfg)
{
    const double amplitude    = unscaled<double>(cfg.thickness);
    const double N            = static_cast<double>(cfg.ripples_per_layer);
    const double fill_step_mm = unscaled<double>(cfg.point_distance);
    if (N <= 0.0 || fill_step_mm < 1e-6)
        return;

    Points poly;
    poly.reserve(ext_lines.size());
    for (const auto &j : ext_lines)
        poly.push_back(j.p);
    const size_t np           = poly.size();
    double       perimeter_mm = 0.0;
    for (size_t i = 0; i + 1 < np; ++i)
        perimeter_mm += unscaled<double>((poly[i + 1] - poly[i]).cast<double>().norm());
    if (perimeter_mm < 1e-6)
        return;

    const double anchor_arc_mm   = ripple_anchor_arc_mm(poly);
    const double phase_at_anchor = M_PI * 2.0 + ripple_phase_shift_rad(cfg);
    auto arc_phase = [&](double arc_mm) { return N * (2.0 * M_PI) * (arc_mm - anchor_arc_mm) / perimeter_mm + phase_at_anchor; };

    Arachne::ExtrusionJunctions out;
    out.reserve(static_cast<size_t>(perimeter_mm / fill_step_mm) + np * 2);
    double accum_mm = 0.0;
    for (size_t i = 0; i + 1 < np; ++i) {
        const Arachne::ExtrusionJunction &j0      = ext_lines[i];
        const Arachne::ExtrusionJunction &j1      = ext_lines[i + 1];
        const Vec2d                       seg     = (j1.p - j0.p).cast<double>();
        const double                      seg_len = seg.norm();
        if (seg_len < EPSILON)
            continue;
        const double seg_len_mm = unscaled<double>(seg_len);
        const Vec2d  seg_perp   = perp(Vec2d(seg / seg_len));
        const double seg_end_mm = accum_mm + seg_len_mm;
        for (double s = std::ceil(accum_mm / fill_step_mm) * fill_step_mm; s < seg_end_mm; s += fill_step_mm) {
            const double t    = (s - accum_mm) / seg_len_mm;
            const double disp = std::sin(arc_phase(s)) * amplitude;
            const Point  pt   = j0.p + (seg * t).cast<coord_t>();
            out.emplace_back(pt + (seg_perp * scale_(disp)).cast<coord_t>(), j1.w, j1.perimeter_index);
        }
        accum_mm = seg_end_mm;
    }
    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
            break;
        --point_idx;
    }
    if (out.size() >= 3)
        ext_lines = std::move(out);
}

void fuzzy_polyline(Points &poly, bool closed, const double slice_z, const FuzzySkinConfig &cfg)
{
    if (cfg.noise_type == Domain::FuzzySkinNoiseType::Ripple) {
        if (poly.size() >= 3)
            fuzzy_polyline_ripple(poly, cfg);
        return;
    }

    const std::unique_ptr<noise::module::Module> noise = get_noise_module(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = cfg.point_distance / 2.;
    double       dist_left_over          = random_value() * (min_dist_between_points / 2.); // the distance to be traversed on the line before making the first new point

    Points out;
    out.reserve(poly.size());

    // Skip the first point for open polyline.
    Point *p0 = closed ? &poly.back() : &poly.front();
    for (auto it_pt1 = closed ? poly.begin() : std::next(poly.begin()); it_pt1 != poly.end(); ++it_pt1) {
        Point &p1 = *it_pt1;

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist) {
            const Point  pa = *p0 + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            const double r  = noise->GetValue(unscaled<double>(pa.x()), unscaled<double>(pa.y()), slice_z) * cfg.thickness;
            out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0             = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        --point_idx;
    }

    if (out.size() >= 3)
        poly = std::move(out);
}

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, const double slice_z, const double layer_height, const FuzzySkinConfig &cfg, const bool closed)
{
    if (cfg.noise_type == Domain::FuzzySkinNoiseType::Ripple) {
        if (ext_lines.size() >= 3)
            fuzzy_extrusion_line_ripple(ext_lines.junctions, cfg);
        return;
    }

    const std::unique_ptr<noise::module::Module> noise = get_noise_module(cfg);

    const double min_dist_between_points = cfg.point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = cfg.point_distance / 2.;
    // Orca: Flow::rounded_rectangle_extrusion_spacing() requires a width above height * (1 - 0.25 * PI); keep 5% above it.
    const double min_extrusion_width = scaled<double>(layer_height * (1. - 0.25 * M_PI) * 1.05);
    double       dist_left_over      = random_value() * (min_dist_between_points / 2.); // the distance to be traversed on the line before making the first new point

    Arachne::ExtrusionJunction *p0 = &ext_lines.front();
    Arachne::ExtrusionJunctions out;
    out.reserve(ext_lines.size());
    for (auto &p1 : ext_lines) {
        // Orca: copy the first point of a closed path only
        if (closed && p0->p == p1.p) {
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        double p0pa_dist = dist_left_over;
        for (; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + random_value() * range_random_point_dist) {
            const Point  pa = p0->p + (p0p1 * (p0pa_dist / p0p1_size)).cast<coord_t>();
            const double r  = noise->GetValue(unscaled<double>(pa.x()), unscaled<double>(pa.y()), slice_z) * cfg.thickness;
            switch (cfg.mode) {
            case Domain::FuzzySkinMode::Displacement:
                out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w, p1.perimeter_index);
                break;
            case Domain::FuzzySkinMode::Extrusion:
                out.emplace_back(pa, coord_t(std::max(p1.w + r + min_extrusion_width, min_extrusion_width)), p1.perimeter_index);
                break;
            case Domain::FuzzySkinMode::Combined: {
                const double width = std::max(p1.w + r + min_extrusion_width, min_extrusion_width);
                out.emplace_back(pa + (perp(p0p1).cast<double>().normalized() * ((width - p1.w) / 2)).cast<coord_t>(), coord_t(width), p1.perimeter_index);
                break;
            }
            }
        }

        dist_left_over = p0pa_dist - p0p1_size;
        p0             = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
            break;
        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p) {
        // Connect endpoints.
        out.front().p = out.back().p;
        out.front().w = out.back().w;
    }

    if (out.size() >= 3)
        ext_lines.junctions = std::move(out);
}

// Orca restrict_to_supported: fuzzify only the parts within the supported area, keeping each part's ends
static void fuzzy_polyline_supported(Points &points, const bool closed, const double slice_z, const FuzzySkinConfig &cfg, const std::optional<ExPolygons> &supported)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    if (!supported) {
        fuzzy_polyline(points, closed, slice_z, cfg);
        return;
    }
    const PolylineSegments segments = closed ? polygon_segmentation(Polygon(points), {*supported}, 1) :
                                               polyline_segmentation(Polyline(points), {*supported}, 1);
    const auto inside = [](const PolylineSegment &segment) { return segment.clip_idx == 0; };
    if (std::all_of(segments.begin(), segments.end(), inside)) {
        fuzzy_polyline(points, closed, slice_z, cfg);
        return;
    }
    if (std::none_of(segments.begin(), segments.end(), inside))
        return;

    Points     out;
    const auto push = [&out](const Point &p) {
        if (out.empty() || out.back() != p)
            out.push_back(p);
    };
    for (const PolylineSegment &segment : segments) {
        Points part = segment.polyline.points;
        if (inside(segment) && part.size() >= 2) {
            const Point front = part.front(), back = part.back();
            fuzzy_polyline(part, false, slice_z, cfg);
            push(front);
            for (const Point &p : part)
                push(p);
            push(back);
        } else
            for (const Point &p : part)
                push(p);
    }
    if (closed && out.size() > 1 && out.front() == out.back())
        out.pop_back();
    if (out.size() >= 3)
        points = std::move(out);
}

static void fuzzy_extrusion_line_supported(Arachne::ExtrusionLine &line, const bool closed, const double slice_z, const double layer_height, const FuzzySkinConfig &cfg, const std::optional<ExPolygons> &supported)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    if (!supported) {
        fuzzy_extrusion_line(line, slice_z, layer_height, cfg, closed);
        return;
    }
    ExtrusionSegments segments = extrusion_segmentation(line, {*supported}, 1);
    const auto inside = [](const ExtrusionSegment &segment) { return segment.clip_idx == 0; };
    if (std::all_of(segments.begin(), segments.end(), inside)) {
        fuzzy_extrusion_line(line, slice_z, layer_height, cfg, closed);
        return;
    }
    if (std::none_of(segments.begin(), segments.end(), inside))
        return;

    Arachne::ExtrusionJunctions out;
    const auto push = [&out](const Arachne::ExtrusionJunction &j) {
        if (out.empty() || out.back().p != j.p)
            out.push_back(j);
    };
    for (ExtrusionSegment &part : segments) {
        if (inside(part) && part.extrusion.size() >= 2) {
            const Arachne::ExtrusionJunction front = part.extrusion.front(), back = part.extrusion.back();
            fuzzy_extrusion_line(part.extrusion, slice_z, layer_height, cfg, false);
            push(front);
            for (const Arachne::ExtrusionJunction &j : part.extrusion.junctions)
                push(j);
            push(back);
        } else
            for (const Arachne::ExtrusionJunction &j : part.extrusion.junctions)
                push(j);
    }
    // Orca: a closed loop stays closed
    if (closed && out.size() > 1 && out.front().p != out.back().p)
        out.push_back(out.front());
    if (out.size() >= 3)
        line.junctions = std::move(out);
}

bool should_fuzzify(const FuzzySkinConfig &config, const int layer_id, const size_t perimeter_idx, const bool is_contour)
{
    using Domain::FuzzySkinType;
    const FuzzySkinType type = config.type;
    if (type == FuzzySkinType::None)
        return false;
    // Orca fuzzy_skin_first_layer
    if (!config.first_layer && layer_id <= 0)
        return false;

    const bool fuzzify_contours = (perimeter_idx == 0 && type != FuzzySkinType::Hole) || type == FuzzySkinType::AllWalls;
    const bool fuzzify_holes    = (type == FuzzySkinType::Hole || type == FuzzySkinType::All || type == FuzzySkinType::AllWalls) &&
        (perimeter_idx == 0 || type == FuzzySkinType::AllWalls);
    return is_contour ? fuzzify_contours : fuzzify_holes;
}

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfigView &base_config, const PerimeterRegions &perimeter_regions, const int layer_id, const size_t perimeter_idx, const bool is_contour, const double slice_z, const std::optional<ExPolygons> &supported)
{
    using namespace Slic3r::Algorithm::LineSegmentation;

    auto apply_fuzzy_skin_on_polygon = [&](const Polygon &polygon, const PrintRegionConfigView &config) -> Polygon {
        const FuzzySkinConfig cfg{config, layer_id};
        if (should_fuzzify(cfg, layer_id, perimeter_idx, is_contour)) {
            Polygon fuzzified_polygon = polygon;
            fuzzy_polyline_supported(fuzzified_polygon.points, true, slice_z, cfg, supported);
            return fuzzified_polygon;
        } else {
            return polygon;
        }
    };

    if (perimeter_regions.empty()) {
        return apply_fuzzy_skin_on_polygon(polygon, base_config);
    }

    PolylineRegionSegments segments = polygon_segmentation(polygon, base_config, perimeter_regions);
    if (segments.size() == 1) {
        const PrintRegionConfigView &config = segments.front().config;
        return apply_fuzzy_skin_on_polygon(polygon, config);
    }

    Polygon fuzzified_polygon;
    for (PolylineRegionSegment &segment : segments) {
        const FuzzySkinConfig cfg{segment.config, layer_id};
        if (should_fuzzify(cfg, layer_id, perimeter_idx, is_contour)) {
            fuzzy_polyline_supported(segment.polyline.points, false, slice_z, cfg, supported);
        }

        assert(!segment.polyline.empty());
        if (segment.polyline.empty()) {
            continue;
        } else if (!fuzzified_polygon.empty() && fuzzified_polygon.back() == segment.polyline.front()) {
            // Remove the last point to avoid duplicate points.
            fuzzified_polygon.points.pop_back();
        }

        Slic3r::append(fuzzified_polygon.points, std::move(segment.polyline.points));
    }

    assert(!fuzzified_polygon.empty());
    if (fuzzified_polygon.front() == fuzzified_polygon.back()) {
        // Remove the last point to avoid duplicity between the first and the last point.
        fuzzified_polygon.points.pop_back();
    }

    return fuzzified_polygon;
}

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfigView &base_config, const PerimeterRegions &perimeter_regions, const int layer_id, const size_t perimeter_idx, const bool is_contour, const double slice_z, const double layer_height, const std::optional<ExPolygons> &supported)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Arachne;

    if (perimeter_regions.empty()) {
        const FuzzySkinConfig cfg{base_config, layer_id};
        if (should_fuzzify(cfg, layer_id, perimeter_idx, is_contour)) {
            ExtrusionLine fuzzified_extrusion = extrusion;
            fuzzy_extrusion_line_supported(fuzzified_extrusion, extrusion.is_closed, slice_z, layer_height, cfg, supported);
            return fuzzified_extrusion;
        } else {
            return extrusion;
        }
    }

    ExtrusionRegionSegments segments = extrusion_segmentation(extrusion, base_config, perimeter_regions);
    ExtrusionLine           fuzzified_extrusion(extrusion.inset_idx, extrusion.is_odd, extrusion.is_closed);

    for (ExtrusionRegionSegment &segment : segments) {
        const FuzzySkinConfig cfg{segment.config, layer_id};
        if (should_fuzzify(cfg, layer_id, perimeter_idx, is_contour)) {
            fuzzy_extrusion_line_supported(segment.extrusion, false, slice_z, layer_height, cfg, supported);
        }

        assert(!segment.extrusion.empty());
        if (segment.extrusion.empty()) {
            continue;
        } else if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p) {
            // Remove the last point to avoid duplicate points (We don't care if the width of both points is different.).
            fuzzified_extrusion.junctions.pop_back();
        }

        Slic3r::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
    }

    assert(!fuzzified_extrusion.empty());

    return fuzzified_extrusion;
}

} // namespace Slic3r::Feature::FuzzySkin
