#ifndef slic3r_GCode_WipeTowerIntegration_hpp_
#define slic3r_GCode_WipeTowerIntegration_hpp_

#include <string>
#include <vector>
#include <cmath>
#include <cstddef>
#include <optional>

#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ConfigViews.hpp"

namespace Slic3r {

class GCodeGenerator;

namespace GCode {

class WipeTowerIntegration {
public:
    WipeTowerIntegration(
        Vec2f pos,
        double rotation,
        const PrintConfigView                                           &print_config,
        const std::vector<WipeTower::ToolChangeResult>              &priming,
        const std::vector<std::vector<WipeTower::ToolChangeResult>> &tool_changes,
        const WipeTower::ToolChangeResult                           &final_purge,
        float                                                        width,
        float                                                        depth,
        const Domain::Polygon                                       &first_layer_outline) :
        m_left( 0.f),
        m_right(width),
        m_depth(depth),
        m_first_layer_outline(first_layer_outline),
        m_use_gap_wall(WipeTower::use_gap_wall(print_config)),
        m_bed_shape(print_config.get<std::vector<Vec2d>>("bed_shape")),
        m_wipe_tower_pos(pos),
        m_wipe_tower_rotation(rotation),
        m_extruder_offsets(print_config.get<std::vector<Vec2d>>("extruder_offset")),
        m_priming(priming),
        m_tool_changes(tool_changes),
        m_final_purge(final_purge),
        m_layer_idx(-1),
        m_tool_change_idx(0),
        m_last_wipe_tower_print_z(print_config.get<double>("z_offset"))
    {}

    std::string prime(GCodeGenerator &gcodegen, const Domain::ConfigView& config);
    void next_layer() { ++ m_layer_idx; m_tool_change_idx = 0; }
    std::string tool_change(GCodeGenerator &gcodegen, const Domain::ConfigView& config, int extruder_id, bool finish_layer);
    std::string finalize(GCodeGenerator &gcodegen, const Domain::ConfigView& config);
    // Colour stacking: a swap prints the next tool change of the band's first tower layer (swap_layer); the
    // band's last colour prints its layer's tower (finish_layer, -1: none). Tower layers are printed at their own z.
    std::string band_tool_change(GCodeGenerator &gcodegen, const Domain::ConfigView& config, int extruder_id, int swap_layer, int finish_from, int finish_to);
    std::vector<float> used_filament_length() const;
    std::optional<WipeTower::ToolChangeResult> get_toolchange(std::size_t index, bool ignore_sparse) const {
        if (static_cast<std::size_t>(m_layer_idx) >= m_tool_changes.size()) {
            return std::nullopt;
        }
        if(
            ignore_sparse
            && m_tool_changes.at(m_layer_idx).size() == 1
            && (
                m_tool_changes.at(m_layer_idx).front().initial_tool
                == m_tool_changes.at(m_layer_idx).front().new_tool
            )
            && m_layer_idx != 0
        ) {
            // Ignore sparse
            return std::nullopt;
        }

        return m_tool_changes.at(m_layer_idx).at(index);
    }

    Vec2f transform_wt_pt(const Vec2f& pt) const {
        Vec2f out = Eigen::Rotation2Df(this->get_alpha()) * pt;
        out += m_wipe_tower_pos;
        return out;
    };

private:
    // Toolchangeresult.gcode assumes the wipe tower corner is at the origin (except for priming lines)
    // We want to rotate and shift all extrusions (gcode postprocessing) and starting and ending position
    float get_alpha() const {
        return m_wipe_tower_rotation / 180.f * float(M_PI);
    }

    WipeTowerIntegration& operator=(const WipeTowerIntegration&);
    std::string append_tcr(GCodeGenerator &gcodegen, const Domain::ConfigView& config, const WipeTower::ToolChangeResult &tcr, int new_extruder_id, double z = -1.) const;

    // Postprocesses gcode: rotates and moves G1 extrusions and returns result
    std::string post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const;

    // Left / right edges of the wipe tower, for the planning of wipe moves.
    const float                                                  m_left;
    const float                                                  m_right;
    // Orca skip points: the tower body's depth and its first layer outline (tower-local), the bed
    const float                                                  m_depth;
    const Domain::Polygon                                        m_first_layer_outline;
    const bool                                                   m_use_gap_wall;
    const std::vector<Vec2d>                                     m_bed_shape;

    // Orca WipeTowerIntegration::travel_to_tower_gap / generate_path_to_wipe_tower: the waypoints (object
    // coordinates) of a travel routed around the tower and in through the wall opening at start_wipe_pos,
    // without start_wipe_pos itself; none when the gap wall is off or the start is over the tower body.
    Points travel_to_tower_gap(const GCodeGenerator &gcodegen, const Point &route_start, const Point &start_wipe_pos) const;
    const Vec2f                                                  m_wipe_tower_pos;
    const float                                                  m_wipe_tower_rotation;
    const std::vector<Vec2d>                                     m_extruder_offsets;

    // Reference to cached values at the Printer class.
    const std::vector<WipeTower::ToolChangeResult>              &m_priming;
    const std::vector<std::vector<WipeTower::ToolChangeResult>> &m_tool_changes;
    const WipeTower::ToolChangeResult                           &m_final_purge;
    // Current layer index.
    int                                                          m_layer_idx;
    int                                                          m_tool_change_idx;
    double                                                       m_last_wipe_tower_print_z;
    // colour stacking: per tower layer, the next tool change result to print
    std::vector<size_t>                                          m_band_next;
};

} // namespace GCode
} // namespace Slic3r

#endif // slic3r_GCode_WipeTowerIntegration_hpp_ 
