#include <cmath>

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

#include "../ClipperUtils.hpp"
#include "../ShortestPath.hpp"
#include "FillPlanePath.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Fill/FillCornerSmoothing.hpp"

#include <numeric>
#include <type_traits>
#include <unordered_map>

#include "Slic3r/Biz/Algorithms/Line.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

class InfillPolylineClipper : public FillPlanePath::InfillPolylineOutput {
public:
    InfillPolylineClipper(const BoundingBox bbox, const double scale_out) : FillPlanePath::InfillPolylineOutput(scale_out), m_bbox(bbox) {}

    void            add_point(const Vec2d &pt);
    Points&&        result() { return std::move(m_out); }
    bool            clips() const override { return true; }

private:
    enum class Side {
        Left   = 1,
        Right  = 2,
        Top    = 4,
        Bottom = 8
    };

    int sides(const Point &p) const {
        return int(p.x() < m_bbox.min.x()) * int(Side::Left) +
               int(p.x() > m_bbox.max.x()) * int(Side::Right) +
               int(p.y() < m_bbox.min.y()) * int(Side::Bottom) +
               int(p.y() > m_bbox.max.y()) * int(Side::Top);
    };

    // Bounding box to clip the polyline with.
    BoundingBox m_bbox;

    // Classification of the two last points processed.
    int         m_sides_prev;
    int         m_sides_this;
};

void InfillPolylineClipper::add_point(const Vec2d &fpt)
{
    const Point pt{ this->scaled(fpt) };

    if (m_out.size() < 2) {
        // Collect the two first points and their status.
        (m_out.empty() ? m_sides_prev : m_sides_this) = sides(pt);
        m_out.emplace_back(pt);
    } else {
        // Classify the last inserted point, possibly remove it.
        int sides_next = sides(pt);
        if (// This point is inside. Take it.
            m_sides_this == 0 ||
            // Either this point is outside and previous or next is inside, or
            // the edge possibly cuts corner of the bounding box.
            (m_sides_prev & m_sides_this & sides_next) == 0) {
            // Keep the last point.
            m_sides_prev = m_sides_this;
        } else {
            // All the three points (this, prev, next) are outside at the same side.
            // Ignore the last point.
            m_out.pop_back();
        }
        // And save the current point.
        m_out.emplace_back(pt);
        m_sides_this = sides_next;
    }
}

// Orca restore_source_path_order: the fragments clipped out of the source polyline sorted and oriented to run in its
// order and direction.
static void restore_source_path_order(const Polyline &source, Polylines &fragments)
{
    const Points &src = source.points;
    if (src.size() < 2 || fragments.empty())
        return;

    std::unordered_map<Point, size_t, Domain::PointHash> source_index;
    source_index.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++ i)
        source_index.emplace(src[i], i);

    // the index of the source vertex where the fragment starts, then the offset of its start from that vertex, for the
    // fragments cut out of one long source segment
    std::vector<std::pair<size_t, double>> keys(fragments.size());
    for (size_t n = 0; n < fragments.size(); ++ n) {
        Polyline    &pl    = fragments[n];
        const size_t npos  = size_t(-1);
        size_t       front = npos;
        size_t       back  = npos;
        for (const Point &pt : pl.points)
            if (auto it = source_index.find(pt); it != source_index.end()) {
                front = it->second;
                break;
            }
        for (auto i = pl.points.rbegin(); i != pl.points.rend(); ++ i)
            if (auto it = source_index.find(*i); it != source_index.end()) {
                back = it->second;
                break;
            }
        Vec2crd source_dir;
        if (front == npos) {
            // all the vertices were created by the clipping: the fragment lies on a single source segment
            double best = std::numeric_limits<double>::max();
            for (size_t i = 0; i + 1 < src.size(); ++ i)
                if (const double d = Algorithms::Line::distance_to_squared(Line(src[i], src[i + 1]), pl.first_point()); d < best) {
                    best  = d;
                    front = i;
                }
            back       = front;
            source_dir = src[front + 1] - src[front];
        } else
            source_dir = src[std::min(back + 1, src.size() - 1)] - src[front > 0 ? front - 1 : 0];
        if (front > back) {
            pl.reverse();
            std::swap(front, back);
        } else if (front == back && (pl.last_point() - pl.first_point()).cast<double>().dot(source_dir.cast<double>()) < 0.)
            pl.reverse();
        const Vec2crd seg = src[std::min(front + 1, src.size() - 1)] - src[front];
        keys[n] = { front, (pl.first_point() - src[front]).cast<double>().dot(seg.cast<double>()) };
    }

    std::vector<size_t> order(fragments.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(), [&keys](size_t a, size_t b) { return keys[a] < keys[b]; });
    Polylines sorted;
    sorted.reserve(fragments.size());
    for (size_t n : order)
        sorted.emplace_back(std::move(fragments[n]));
    fragments = std::move(sorted);
}

void FillPlanePath::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                        expolygon,
    Polylines                       &polylines_out)
{
    expolygon.rotate(-direction.first);

    // Orca: only the sparse infill is aligned across layers, as the top / bottom surfaces may be less dense too
    const bool align = params.extrusion_role == ExtrusionRole::InternalInfill;

    BoundingBox snug_bounding_box = BB::inflated(get_extents(expolygon), SCALED_EPSILON);
    // Orca: expanded to avoid artifacts at the edges (the outer lines of a multiline path)
    snug_bounding_box = BB::inflated(snug_bounding_box, coord_t(scale_(this->spacing) * params.multiline));


    BoundingBox rotated_box;
    rotated_box = BB::merge(rotated_box, Domain::rotated(this->bounding_box.min, -direction.first));
    rotated_box = BB::merge(rotated_box, Domain::rotated(this->bounding_box.max, -direction.first));
    rotated_box = BB::merge(rotated_box, Domain::rotated(Point(this->bounding_box.min.x(), this->bounding_box.max.y()), -direction.first));
    rotated_box = BB::merge(rotated_box, Domain::rotated(Point(this->bounding_box.max.x(), this->bounding_box.min.y()), -direction.first));

    // Rotated bounding box of the area to fill in with the pattern.
    // Sparse infill needs to be aligned across layers, using the object's bounding box. The solid surfaces are centered
    // on each surface, the object or the assembly (Orca center_of_surface_pattern).
    BoundingBox bounding_box =
        align || params.center_of_surface_pattern == Domain::CenterOfSurfacePattern::EachModel ? rotated_box :
        params.center_of_surface_pattern == Domain::CenterOfSurfacePattern::EachSurface         ? snug_bounding_box :
                                                                                                  this->extended_object_bounding_box();

    Point shift = this->centered() ? 
        BB::center(bounding_box) :
        bounding_box.min;
    expolygon.translate(-shift.x(), -shift.y());
    bounding_box = BB::translated(bounding_box, Point{-shift.x(), -shift.y()});

    Polyline polyline;
    {
        auto distance_between_lines = scaled<double>(this->spacing) * params.multiline / params.density;
        auto min_x = coord_t(ceil(double(bounding_box.min.x()) / distance_between_lines));
        auto min_y = coord_t(ceil(double(bounding_box.min.y()) / distance_between_lines));
        auto max_x = coord_t(ceil(double(bounding_box.max.x()) / distance_between_lines));
        auto max_y = coord_t(ceil(double(bounding_box.max.y()) / distance_between_lines));
        auto resolution = scaled<double>(params.resolution) / distance_between_lines;
        if (align) {
            // Filling in a bounding box over the whole object, clip generated polyline against the snug bounding box.
            snug_bounding_box = BB::translated(snug_bounding_box, Point{-shift.x(), -shift.y()});
            InfillPolylineClipper output(snug_bounding_box, distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, params, output);
            polyline.points = std::move(output.result());
        } else {
            // Filling in a snug bounding box, no need to clip.
            InfillPolylineOutput output(distance_between_lines);
            this->generate(min_x, min_y, max_x, max_y, resolution, params, output);
            polyline.points = std::move(output.result());
        }
    }

    if (polyline.size() >= 2) {
        // Orca fill_multiline
        Polylines polylines { polyline };
        multiline_fill(polylines, params, spacing);
        polylines = intersection_pl(std::move(polylines), expolygon);
        Polylines chained;
        if (params.dont_connect() || params.density > 0.5 || polylines.size() <= 1) {
            if (params.fill_order != Domain::SurfaceFillOrder::Default) {
                // Orca: the fragments in the order along the generated path, which runs from the center outwards (the
                // distance from the center cannot be used: along the Octagram Spiral it oscillates more than the rings)
                restore_source_path_order(polyline, polylines);
                chained = std::move(polylines);
                if (params.fill_order == Domain::SurfaceFillOrder::Inward) {
                    std::reverse(chained.begin(), chained.end());
                    for (Polyline &pl : chained)
                        pl.reverse();
                }
            } else
                chained = chain_polylines(std::move(polylines));
        } else
            connect_infill(std::move(polylines), expolygon, chained, this->spacing, params);
        // paths must be repositioned and rotated back
        for (Polyline &pl : chained) {
            pl.translate(shift.x(), shift.y());
            pl.rotate(direction.first);
        }
        append(polylines_out, std::move(chained));
    }
}

// Follow an Archimedean spiral, in polar coordinates: r=a+b\theta
template<typename Output>
static void generate_archimedean_chords(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, Output &output)
{
    // Radius to achieve.
    double rmax = std::sqrt(double(max_x)*double(max_x)+double(max_y)*double(max_y)) * std::sqrt(2.) + 1.5;
    // Now unwind the spiral.
    double a = 1.;
    double b = 1./(2.*M_PI);
    double theta = 0.;
    double r = 1;
    Pointfs out;
    //FIXME Vojtech: If used as a solid infill, there is a gap left at the center.
    output.add_point({ 0, 0 });
    output.add_point({ 1, 0 });
    while (r < rmax) {
        // Discretization angle to achieve a discretization error lower than resolution.
        theta += 2. * acos(1. - resolution / r);
        r = a + b * theta;
        output.add_point({ r * cos(theta), r * sin(theta) });
    }
}

void FillArchimedeanChords::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, static_cast<InfillPolylineClipper&>(output));
    else
        generate_archimedean_chords(min_x, min_y, max_x, max_y, resolution, output);
}

// Adapted from 
// http://cpansearch.perl.org/src/KRYDE/Math-PlanePath-122/lib/Math/PlanePath/HilbertCurve.pm
//
// state=0    3--2   plain
//               |
//            0--1
//
// state=4    1--2  transpose
//            |  |
//            0  3
//
// state=8
//
// state=12   3  0  rot180 + transpose
//            |  |
//            2--1
//
static inline Point hilbert_n_to_xy(const size_t n)
{
    static constexpr const int next_state[16] { 4,0,0,12, 0,4,4,8, 12,8,8,4, 8,12,12,0 };
    static constexpr const int digit_to_x[16] { 0,1,1,0, 0,0,1,1, 1,0,0,1, 1,1,0,0 };
    static constexpr const int digit_to_y[16] { 0,0,1,1, 0,1,1,0, 1,1,0,0, 1,0,0,1 };

    // Number of 2 bit digits.
    size_t ndigits = 0;
    {
        size_t nc = n;
        while(nc > 0) {
            nc >>= 2;
            ++ ndigits;
        }
    }
    int state = (ndigits & 1) ? 4 : 0;
    coord_t x = 0;
    coord_t y = 0;
    for (int i = (int)ndigits - 1; i >= 0; -- i) {
        int digit = (n >> (i * 2)) & 3;
        state += digit;
        x |= digit_to_x[state] << i;
        y |= digit_to_y[state] << i;
        state = next_state[state];
    }
    return Point(x, y);
}

template<typename Output>
static void generate_hilbert_curve(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output &output)
{
    // Minimum power of two square to fit the domain.
    size_t sz = 2;
    size_t pw = 1;
    {
        size_t sz0 = std::max(max_x + 1 - min_x, max_y + 1 - min_y);
        while (sz < sz0) {
            sz = sz << 1;
            ++ pw;
        }
    }

    size_t sz2 = sz * sz;
    output.reserve(sz2);
    for (size_t i = 0; i < sz2; ++ i) {
        Point p = hilbert_n_to_xy(i);
        output.add_point({ p.x() + min_x, p.y() + min_y });
    }
}

// Rounds the corners of the generated path on its way to the infill output.
template<typename Output>
class SmoothingPolylineOutput
{
public:
    SmoothingPolylineOutput(Output &output, const double smooth_factor, const double tolerance)
        : m_output(output), m_smoother(smooth_factor, tolerance) {}

    void reserve(size_t n) { m_output.reserve(n); }
    void add_point(const Vec2d &pt) { auto emit = emitter(); m_smoother.push(pt, emit); }
    // The smoother holds back the last point of the path until it knows there is no corner left to round.
    void finish() { auto emit = emitter(); m_smoother.flush(emit); }

private:
    // The curves of two adjacent corners meet at the midpoint of the segment they share, where they
    // may round to the very same output point. Drop those, they would be zero length extrusions.
    auto emitter()
    {
        return [this](const Vec2d &pt) {
            const Point snapped = m_output.scaled(pt);
            if (m_has_last_snapped && snapped == m_last_snapped)
                return;
            m_last_snapped     = snapped;
            m_has_last_snapped = true;
            m_output.add_point(pt);
        };
    }

    Output        &m_output;
    CornerSmoother m_smoother;
    Point          m_last_snapped { Point::Zero() };
    bool           m_has_last_snapped { false };
};

// Runs the path generator against the concrete output type, optionally through the corner smoother.
// The outputs do not share a virtual add_point(), so the type has to be resolved here.
template<typename GenerateFn>
static void generate_path(FillPlanePath::InfillPolylineOutput &output, const FillParams &params, const double resolution, GenerateFn generate)
{
    const double smooth_factor = sanitize_smooth_factor(params.smooth_factor);
    auto run = [smooth_factor, resolution, &generate](auto &out) {
        if (smooth_factor == 0.) {
            generate(out);
        } else {
            SmoothingPolylineOutput<std::remove_reference_t<decltype(out)>> smoothing(out, smooth_factor, resolution);
            generate(smoothing);
            smoothing.finish();
        }
    };

    if (output.clips())
        run(static_cast<InfillPolylineClipper&>(output));
    else
        run(output);
}

void FillHilbertCurve::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double /* resolution */, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_hilbert_curve(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper&>(output));
    else
        generate_hilbert_curve(min_x, min_y, max_x, max_y, output);
}

void FillHilbertCurve::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution,
    const FillParams &params, InfillPolylineOutput &output)
{
    generate_path(output, params, resolution,
        [min_x, min_y, max_x, max_y](auto &out) { generate_hilbert_curve(min_x, min_y, max_x, max_y, out); });
}

template<typename Output>
static void generate_octagram_spiral(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, Output &output)
{
    // Radius to achieve.
    double rmax = std::sqrt(double(max_x)*double(max_x)+double(max_y)*double(max_y)) * std::sqrt(2.) + 1.5;
    // Now unwind the spiral.
    double r = 0;
    double r_inc = sqrt(2.);
    output.add_point({ 0., 0. });
    while (r < rmax) {
        r += r_inc;
        double rx = r / sqrt(2.);
        double r2 = r + rx;
        output.add_point({ r,   0. });
        output.add_point({ r2,  rx });
        output.add_point({ rx,  rx });
        output.add_point({ rx,  r2 });
        output.add_point({ 0.,  r  });
        output.add_point({-rx,  r2 });
        output.add_point({-rx,  rx });
        output.add_point({-r2,  rx });
        output.add_point({- r,  0. });
        output.add_point({-r2, -rx });
        output.add_point({-rx, -rx });
        output.add_point({-rx, -r2 });
        output.add_point({ 0., -r  });
        output.add_point({ rx, -r2 });
        output.add_point({ rx, -rx });
        output.add_point({ r2+r_inc, -rx });
    }
}

void FillOctagramSpiral::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double /* resolution */, InfillPolylineOutput &output)
{
    if (output.clips())
        generate_octagram_spiral(min_x, min_y, max_x, max_y, static_cast<InfillPolylineClipper&>(output));
    else
        generate_octagram_spiral(min_x, min_y, max_x, max_y, output);
}

void FillOctagramSpiral::generate(coord_t min_x, coord_t min_y, coord_t max_x, coord_t max_y, const double resolution,
    const FillParams &params, InfillPolylineOutput &output)
{
    generate_path(output, params, resolution,
        [min_x, min_y, max_x, max_y](auto &out) { generate_octagram_spiral(min_x, min_y, max_x, max_y, out); });
}

} // namespace Slic3r
