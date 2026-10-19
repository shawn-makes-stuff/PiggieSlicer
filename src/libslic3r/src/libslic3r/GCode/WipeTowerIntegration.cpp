#include "WipeTowerIntegration.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <cmath>
#include <iomanip>
#include <istream>
#include <iterator>
#include <utility>
#include <cassert>
#include <cstdlib>

#include "libslic3r/GCode.hpp"
#include "libslic3r/libslic3r.h"
#include "boost/algorithm/string/replace.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/GCode/Wipe.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"

namespace Slic3r::GCode {

static inline Point wipe_tower_point_to_object_point(GCodeGenerator &gcodegen, const Vec2f& wipe_tower_pt)
{
    return scaled(Vec2d(wipe_tower_pt.x() - gcodegen.origin()(0), wipe_tower_pt.y() - gcodegen.origin()(1)));
}

// Orca: the clearance the tower-approach router keeps around the tower
static constexpr float wipe_tower_routing_clearance = 2.f;

// Orca WipeTowerIntegration::generate_path_to_wipe_tower: from start_pos around avoid_polygon (inflated by the
// clearance) to end_pos, along the shorter side; straight to end_pos when the box leaves the bed.
static Polyline generate_path_to_wipe_tower(const Point &start_pos, const Point &end_pos, const BoundingBox &avoid_polygon, const Polygon &bed_polygon)
{
    Polyline    res;
    coord_t     alpha = scaled(wipe_tower_routing_clearance); // offset distance
    BoundingBox avoid_polygon_inner = avoid_polygon;
    avoid_polygon_inner.min -= Point(alpha, alpha);
    avoid_polygon_inner.max += Point(alpha, alpha);
    coord_t width = avoid_polygon_inner.max[0] - avoid_polygon_inner.min[0];
    Vec2f v(1, 0);                                                      // the first print direction of end_pos.
    if (std::abs(end_pos[0] - avoid_polygon_inner.min[0]) < width / 2) v = -v; // judge whether the wipe tower's infill goes to the left or right.
    // Judge whether the avoid_polygon_inner is outside the bed; if so, go directly to the end_pos.
    const Points avoid_points = {avoid_polygon_inner.min, Point(avoid_polygon_inner.max.x(), avoid_polygon_inner.min.y()),
                                 avoid_polygon_inner.max, Point(avoid_polygon_inner.min.x(), avoid_polygon_inner.max.y())};
    const bool is_bbx_in_bed = std::all_of(avoid_points.begin(), avoid_points.end(),
        [&bed_polygon](const Point &pt) { return Biz::Algorithms::Polygon::contains(bed_polygon, pt, /*border_result=*/false); });
    if (!is_bbx_in_bed) {
        res.points.push_back(end_pos);
        return res;
    }
    // Ray-Line Segment Intersection
    auto ray_intersetion_line = [](const Vec2d &a, const Vec2d &v1, const Vec2d &b, const Vec2d &c) -> std::pair<bool, Point> {
        const Vec2d v2    = c - b;
        double      denom = cross2(v1, v2);
        if (fabs(denom) < EPSILON) return {false, Point(0, 0)};
        const Vec2d v12    = (a - b);
        double      nume_a = cross2(v2, v12);
        double      nume_b = cross2(v1, v12);
        double      t1     = nume_a / denom;
        double      t2     = nume_b / denom;
        if (t1 >= 0 && t2 >= 0 && t2 <= 1.) {
            // Get the intersection point.
            Vec2d res = a + t1 * v1;
            return std::pair<bool, Point>(true, scaled(res));
        }
        return std::pair<bool, Point>(false, {0, 0});
    };
    // segment-segment intersection (Orca line_alg::intersection of two Linef)
    auto segments_intersection = [](const Vec2d &p0, const Vec2d &p1, const Vec2d &q0, const Vec2d &q1, Vec2d &out) {
        const Vec2d  r = p1 - p0, s = q1 - q0;
        const double denom = cross2(r, s);
        if (std::abs(denom) < 1e-12)
            return false;
        const double t = cross2(q0 - p0, s) / denom;
        const double u = cross2(q0 - p0, r) / denom;
        if (t < 0. || t > 1. || u < 0. || u > 1.)
            return false;
        out = p0 + t * r;
        return true;
    };
    struct Inter_info
    {
        int   inter_idx0 = -1;
        Point inter_p;
    };
    auto calc_path_len = [](const Points &points, Inter_info &beg_info, Inter_info &end_info, bool is_add) -> std::pair<std::vector<Point>, double> {
        int                beg = is_add ? (beg_info.inter_idx0 + 1) % points.size() : beg_info.inter_idx0;
        int                end = is_add ? end_info.inter_idx0 : (end_info.inter_idx0 + 1) % points.size();
        int                i   = beg;
        double             len = 0;
        std::vector<Point> path;
        path.push_back(beg_info.inter_p);
        len += (unscale(beg_info.inter_p) - unscale(points[beg])).squaredNorm();
        while (i != end) {
            int  ni = is_add ? (i + 1) % points.size() : (i - 1 + points.size()) % points.size();
            auto a  = unscale(points[i]);
            auto b  = unscale(points[ni]);
            len += (a - b).squaredNorm();
            path.push_back(points[i]);
            i = ni;
        }
        path.push_back(points[end]);
        path.push_back(end_info.inter_p);
        len += (unscale(end_info.inter_p) - unscale(points[end])).squaredNorm();
        return {path, len};
    };
    // the intersection 'p' of end_pos along v with the avoid polygon
    Inter_info inter_info;
    for (size_t i = 0; i < avoid_points.size(); i++) {
        const auto &a                  = avoid_points[i];
        const auto &b                  = avoid_points[(i + 1) % avoid_points.size()];
        auto [is_inter, inter_p] = ray_intersetion_line(unscale(end_pos), v.cast<double>(), unscale(a), unscale(b));
        if (is_inter) {
            inter_info.inter_idx0 = int(i);
            inter_info.inter_p    = inter_p;
            break;
        }
    }
    if (inter_info.inter_idx0 == -1) {
        res.points.push_back(end_pos);
        return res;
    }
    // the other intersection 'p_' of start_to_p with the avoid polygon
    Inter_info inter_info2;
    for (size_t i = 0; i < avoid_points.size(); i++) {
        if (int(i) == inter_info.inter_idx0) continue;
        Vec2d inter;
        if (segments_intersection(unscale(start_pos), unscale(inter_info.inter_p), unscale(avoid_points[i]),
                                  unscale(avoid_points[(i + 1) % avoid_points.size()]), inter)) {
            inter_info2.inter_p    = scaled(inter);
            inter_info2.inter_idx0 = int(i);
            break;
        }
    }
    // if p_ does not exist, go directly to p, else along the shorter side of the box from p_ to p
    if (inter_info2.inter_idx0 == -1) {
        res.points.push_back(inter_info.inter_p);
    } else {
        auto [path1, len1] = calc_path_len(avoid_points, inter_info2, inter_info, true);
        auto [path2, len2] = calc_path_len(avoid_points, inter_info2, inter_info, false);
        for (const Point &pt : len1 < len2 ? path1 : path2)
            res.points.push_back(pt);
    }
    res.points.push_back(end_pos);
    return res;
}

Points WipeTowerIntegration::travel_to_tower_gap(const GCodeGenerator &gcodegen, const Point &route_start, const Point &start_wipe_pos) const
{
    if (!m_use_gap_wall)
        return {};
    // tower-local points -> object coordinates, like the tcr points (a rotated tower gets an axis-aligned envelope)
    auto to_object = [&](const Vec2f &pt) { return wipe_tower_point_to_object_point(const_cast<GCodeGenerator &>(gcodegen), transform_wt_pt(pt)); };
    // Early out only when the approach already starts over the tower body itself, so a start between the
    // wall and the brim edge still gets routed in through the wall opening.
    Polygon body{to_object(Vec2f(0.f, 0.f)), to_object(Vec2f(m_right, 0.f)), to_object(Vec2f(m_right, m_depth)), to_object(Vec2f(0.f, m_depth))};
    if (Biz::Algorithms::Polygon::contains(body, route_start))
        return {};

    // the avoid box: the first layer outline (ribs, brim), else the body
    BoundingBox avoid_bbx;
    if (m_first_layer_outline.points.empty())
        avoid_bbx = get_extents(body);
    else {
        Points pts;
        for (const Point &pt : m_first_layer_outline.points)
            pts.push_back(to_object(unscale(pt).cast<float>()));
        avoid_bbx = get_extents(Polygon(pts));
    }
    Points bed_points;
    for (const Vec2d &pt : m_bed_shape)
        bed_points.push_back(wipe_tower_point_to_object_point(const_cast<GCodeGenerator &>(gcodegen), pt.cast<float>()));
    const Polygon bed(bed_points);
    if (bed.points.size() < 3)
        return {};
    // The inflated corners must stay on the bed for a route at all: clamp the box against the bed shrunk by
    // the clearance, so a tower near the bed edge is still routed along the clamped side.
    BoundingBox clamp_bbx = get_extents(bed);
    const coord_t clearance = scaled(wipe_tower_routing_clearance) + SCALED_EPSILON;
    clamp_bbx.min += Point(clearance, clearance);
    clamp_bbx.max -= Point(clearance, clearance);
    avoid_bbx.min = avoid_bbx.min.cwiseMax(clamp_bbx.min);
    avoid_bbx.max = avoid_bbx.max.cwiseMin(clamp_bbx.max);
    if (avoid_bbx.min.x() >= avoid_bbx.max.x() || avoid_bbx.min.y() >= avoid_bbx.max.y())
        return {};

    Polyline travel_polyline = generate_path_to_wipe_tower(route_start, start_wipe_pos, avoid_bbx, bed);
    // The polyline's last point is start_wipe_pos itself, travelled to by the caller.
    if (!travel_polyline.points.empty())
        travel_polyline.points.pop_back();
    return travel_polyline.points;
}

std::string WipeTowerIntegration::append_tcr(
    GCodeGenerator& gcodegen,
    const Domain::ConfigView& config,
    const WipeTower::ToolChangeResult& tcr,
    int new_extruder_id,
    double z
) const
{
    if (new_extruder_id != -1 && new_extruder_id != tcr.new_tool)
        throw Slic3r::InvalidArgument("Error: WipeTowerIntegration::append_tcr was asked to do a toolchange it didn't expect.");

    std::string gcode;


    Vec2f start_pos = tcr.start_pos;
    Vec2f end_pos = tcr.end_pos;
    if (! tcr.priming) {
        start_pos = transform_wt_pt(start_pos);
        end_pos = transform_wt_pt(end_pos);
    }

    Vec2f wipe_tower_offset = tcr.priming ? Vec2f::Zero() : m_wipe_tower_pos;
    float wipe_tower_rotation = tcr.priming ? 0.f : this->get_alpha();

    std::string tcr_rotated_gcode = post_process_wipe_tower_moves(tcr, wipe_tower_offset, wipe_tower_rotation);

    double current_z = gcodegen.writer().get_position().z();
    gcode += gcodegen.writer().travel_to_z(current_z);

    if (z == -1.) // in case no specific z was provided, print at current_z pos
        z = current_z;

    const bool needs_toolchange = gcodegen.writer().need_toolchange(new_extruder_id);
    const bool will_go_down = ! is_approx(z, current_z);
    const bool is_ramming = (config.get<bool>("single_extruder_multi_material"))
                         || (! config.get<bool>("single_extruder_multi_material") && config.get<std::vector<bool>>("filament_multitool_ramming").at(tcr.initial_tool));
    const bool should_travel_to_tower = ! tcr.priming
                                     && (tcr.force_travel        // wipe tower says so
                                         || ! needs_toolchange   // this is just finishing the tower with no toolchange
                                         || is_ramming
                                         || will_go_down);       // don't dig into the print
    if (should_travel_to_tower) {
        const Point xy_point = wipe_tower_point_to_object_point(gcodegen, start_pos);
        const Vec3crd to{to_3d(xy_point, scaled(z))};
        gcode += gcodegen.m_label_objects.maybe_stop_instance();

        const std::vector<double> retract_speed{config.get<std::vector<double>>("retract_speed")};
        const double travel_speed{config.get<double>("travel_speed")};
        const Biz::Slicing::ExtrudeConfig& extrude_config = gcodegen.extrude_config(config);

        gcode += gcodegen.retract_and_wipe(retract_speed, travel_speed);
        gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
        const std::string comment{"Travel to a Wipe Tower"};
        // Orca skip points: around the tower and in through its wall opening
        if (gcodegen.m_moved_to_first_layer_point && gcodegen.last_position)
            for (const Point &waypoint : travel_to_tower_gap(gcodegen, *gcodegen.last_position, xy_point)) {
                gcode += gcodegen.writer().travel_to_xy(gcodegen.point_to_gcode(waypoint), comment);
                gcodegen.last_position = waypoint;
            }
        if (!gcodegen.m_moved_to_first_layer_point) {
            gcode += gcodegen.travel_to_first_position(to, current_z, ExtrusionRole::Mixed, [](){return "";}, extrude_config);
        } else {
            if (gcodegen.last_position) {
                const Vec3crd from{to_3d(*gcodegen.last_position, scaled(current_z))};
                gcode += gcodegen.travel_to(
                    from, to, ExtrusionRole::Mixed, comment, [](){return "";}, extrude_config
                );
            } else {
                gcode += gcodegen.writer().travel_to_xy(gcodegen.point_to_gcode(xy_point), comment);
                gcode += gcodegen.writer().travel_to_z_force(z, comment);
            }
        }
        gcode += gcodegen.unretract();
    } else {
        // When this is multiextruder printer without any ramming, we can just change
        // the tool without travelling to the tower.
    }

    if (will_go_down) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(z, "Travel down to the last wipe tower layer.");
        gcode += gcodegen.writer().unretract();
    }

    std::string toolchange_gcode_str;
    std::string deretraction_str;
    if (tcr.priming || (new_extruder_id >= 0 && needs_toolchange)) {
        if (is_ramming)
            gcodegen.m_wipe.reset_path(); // We don't want wiping on the ramming lines.
        gcodegen.m_toolchange_on_tower_interface = tcr.is_contact && config.get<bool>("enable_tower_interface_features");
        toolchange_gcode_str = gcodegen.set_extruder(new_extruder_id, tcr.print_z, config); // TODO: toolchange_z vs print_z
        gcodegen.m_toolchange_on_tower_interface = false;
        // Orca skip points: the tool changed in place, so the tower entry is the tcr's own positioning move,
        // a straight line across the printed wall: route it around the tower and in through the opening
        if (!should_travel_to_tower && !tcr.priming && gcodegen.last_position) {
            const Point entry = wipe_tower_point_to_object_point(gcodegen, start_pos);
            for (const Point &waypoint : travel_to_tower_gap(gcodegen, *gcodegen.last_position, entry))
                toolchange_gcode_str += gcodegen.writer().travel_to_xy(gcodegen.point_to_gcode(waypoint), "Travel to a Wipe Tower");
        }
        if (config.get<bool>("wipe_tower")) {
            deretraction_str += gcodegen.writer().travel_to_z_force(z, "restore layer Z");
            deretraction_str += gcodegen.unretract();
        }
    }
    assert(toolchange_gcode_str.empty() || toolchange_gcode_str.back() == '\n');
    assert(deretraction_str.empty() || deretraction_str.back() == '\n');

    // Insert the toolchange and deretraction gcode into the generated gcode.
    boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_from_wipe_tower_generator]", toolchange_gcode_str);
    boost::replace_first(tcr_rotated_gcode, "[deretraction_from_wipe_tower_generator]", deretraction_str);
    std::string tcr_gcode;
    Biz::Algorithms::unescape_string_cstyle(tcr_rotated_gcode, tcr_gcode);

    const int acceleation_extruder{new_extruder_id > -1 ? new_extruder_id : tcr.initial_tool};
    if (config.get<std::vector<double>>("default_acceleration").at(acceleation_extruder) > 0)
        gcode += gcodegen.writer().set_print_acceleration(fast_round_up<unsigned int>(config.get<double>("wipe_tower_acceleration")));
    gcode += tcr_gcode;
    gcode += gcodegen.writer().set_print_acceleration(
        fast_round_up<unsigned int>(
            config.get<std::vector<double>>("default_acceleration").at(acceleation_extruder)
        )
    );

    // A phony move to the end position at the wipe tower.
    gcodegen.writer().travel_to_xy(end_pos.cast<double>());
    gcodegen.last_position = wipe_tower_point_to_object_point(gcodegen, end_pos);
    if (!is_approx(z, current_z)) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(current_z, "Travel back up to the topmost object layer.");
        gcode += gcodegen.writer().unretract();
    }

    else {
        // Prepare a future wipe.
        // Convert to a smooth path.
        Geometry::ArcWelder::Path path;
        path.reserve(tcr.wipe_path.size());
        std::transform(tcr.wipe_path.begin(), tcr.wipe_path.end(), std::back_inserter(path),
            [&gcodegen, this](const Vec2f &wipe_pt) {
                return Geometry::ArcWelder::Segment{ wipe_tower_point_to_object_point(gcodegen, transform_wt_pt(wipe_pt)) };
            });
        // Pass to the wipe cache.
        gcodegen.m_wipe.set_path(std::move(path));
    }

    // Let the planner know we are traveling between objects.
    gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
    return gcode;
}

// This function postprocesses gcode_original, rotates and moves all G1 extrusions and returns resulting gcode
// Starting position has to be supplied explicitely (otherwise it would fail in case first G1 command only contained one coordinate)
std::string WipeTowerIntegration::post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const
{
    Vec2f extruder_offset = m_extruder_offsets[tcr.initial_tool].cast<float>();

    std::istringstream gcode_str(tcr.gcode);
    std::string gcode_out;
    std::string line;
    Vec2f pos = tcr.start_pos;
    Vec2f transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;
    Vec2f old_pos(-1000.1f, -1000.1f);

    while (gcode_str) {
        std::getline(gcode_str, line);  // we read the gcode line by line

        // All G1 commands should be translated and rotated. X and Y coords are
        // only pushed to the output when they differ from last time.
        // WT generator can override this by appending the never_skip_tag
        if (boost::starts_with(line, "G1 ")) {
            bool never_skip = false;
            auto it = line.find(WipeTower::never_skip_tag());
            if (it != std::string::npos) {
                // remove the tag and remember we saw it
                never_skip = true;
                line.erase(it, it + WipeTower::never_skip_tag().size());
            }
            std::ostringstream line_out;
            std::istringstream line_str(line);
            line_str >> std::noskipws;  // don't skip whitespace
            char ch = 0;
            line_str >> ch >> ch; // read the "G1"
            while (line_str >> ch) {
                if (ch == 'X' || ch == 'Y')
                    line_str >> (ch == 'X' ? pos.x() : pos.y());
                else
                    line_out << ch;
            }

            line = line_out.str();
            boost::trim(line); // Remove leading and trailing spaces.

            transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;

            if (transformed_pos != old_pos || never_skip || ! line.empty()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3) << "G1";
                if (transformed_pos.x() != old_pos.x() || never_skip)
                    oss << " X" << transformed_pos.x() - extruder_offset.x();
                if (transformed_pos.y() != old_pos.y() || never_skip)
                    oss << " Y" << transformed_pos.y() - extruder_offset.y();
                if (! line.empty())
                    oss << " ";
                line = oss.str() + line;
                old_pos = transformed_pos;
            }
        }

        gcode_out += line + "\n";

        // If this was a toolchange command, we should change current extruder offset
        if (line == "[toolchange_gcode_from_wipe_tower_generator]") {
            extruder_offset = m_extruder_offsets[tcr.new_tool].cast<float>();

            // If the extruder offset changed, add an extra move so everything is continuous
            if (extruder_offset != m_extruder_offsets[tcr.initial_tool].cast<float>()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3)
                    << "G1 X" << transformed_pos.x() - extruder_offset.x()
                    << " Y" << transformed_pos.y() - extruder_offset.y()
                    << "\n";
                gcode_out += oss.str();
            }
        }
    }
    return gcode_out;
}


std::string WipeTowerIntegration::prime(GCodeGenerator &gcodegen, const Domain::ConfigView& config)
{
    std::string gcode;
    for (const WipeTower::ToolChangeResult& tcr : m_priming) {
        if (! tcr.extrusions.empty())
            gcode += append_tcr(gcodegen, config, tcr, tcr.new_tool);
    }
    return gcode;
}

std::string WipeTowerIntegration::tool_change(GCodeGenerator &gcodegen, const Domain::ConfigView& config, int extruder_id, bool finish_layer)
{
    std::string gcode;
    assert(m_layer_idx >= 0);
    if (gcodegen.writer().need_toolchange(extruder_id) || finish_layer) {
        if (m_layer_idx < (int)m_tool_changes.size()) {
            if (!(size_t(m_tool_change_idx) < m_tool_changes[m_layer_idx].size()))
                throw Slic3r::RuntimeError("Wipe tower generation failed, possibly due to empty first layer.");

            // Calculate where the wipe tower layer will be printed. -1 means that print z will not change,
            // resulting in a wipe tower with sparse layers.
            double wipe_tower_z = -1;
            // Orca: folded into a later, thicker layer that prints at its own z: nothing to emit
            bool ignore_sparse = !m_tool_changes[m_layer_idx].empty() && m_tool_changes[m_layer_idx].front().combined_away;
            if (config.get<bool>("wipe_tower_no_sparse_layers")) {
                wipe_tower_z = m_last_wipe_tower_print_z;
                ignore_sparse = (m_tool_changes[m_layer_idx].size() == 1 && m_tool_changes[m_layer_idx].front().initial_tool == m_tool_changes[m_layer_idx].front().new_tool && m_layer_idx != 0);
                if (m_tool_change_idx == 0 && !ignore_sparse)
                    wipe_tower_z = m_last_wipe_tower_print_z + m_tool_changes[m_layer_idx].front().layer_height;
            }

            if (!ignore_sparse) {
                gcode += append_tcr(gcodegen, config, m_tool_changes[m_layer_idx][m_tool_change_idx++], extruder_id, wipe_tower_z);
                m_last_wipe_tower_print_z = wipe_tower_z;
            }
        }
    }
    return gcode;
}

std::string WipeTowerIntegration::band_tool_change(GCodeGenerator &gcodegen, const Domain::ConfigView& config, int extruder_id, int swap_layer, int finish_from, int finish_to)
{
    if (m_band_next.size() < m_tool_changes.size())
        m_band_next.resize(m_tool_changes.size(), 0);
    std::string gcode;
    const double z_offset = config.get<double>("z_offset");
    auto print_next = [&](int layer) {
        if (layer < 0 || size_t(layer) >= m_tool_changes.size() || m_band_next[layer] >= m_tool_changes[layer].size())
            return false;
        const WipeTower::ToolChangeResult &tcr = m_tool_changes[layer][m_band_next[layer]++];
        gcode += append_tcr(gcodegen, config, tcr, tcr.new_tool, tcr.print_z + z_offset);
        return true;
    };
    if (gcodegen.writer().need_toolchange(extruder_id) && !print_next(swap_layer))
        gcode += gcodegen.set_extruder(extruder_id, gcodegen.writer().get_position().z(), config);
    for (int layer = std::max(finish_from, 0); layer < finish_to; ++layer)
        while (print_next(layer)) ;
    return gcode;
}

// Print is finished. Now it remains to unload the filament safely with ramming over the wipe tower.
std::string WipeTowerIntegration::finalize(GCodeGenerator &gcodegen, const Domain::ConfigView& config)
{
    std::string gcode;
    const double purge_z{m_final_purge.print_z + config.get<double>("z_offset")};
    if (std::abs(gcodegen.writer().get_position().z() - purge_z) > EPSILON)
        gcode += gcodegen.generate_travel_gcode(
            {{gcodegen.last_position->x(), gcodegen.last_position->y(), scaled(purge_z)}},
            "move to safe place for purging", [](){return "";}, Biz::Slicing::ExtrudeConfig{config}
        );
    gcode += append_tcr(gcodegen, config, m_final_purge, -1);
    return gcode;
}

} // namespace Slic3r::GCode
