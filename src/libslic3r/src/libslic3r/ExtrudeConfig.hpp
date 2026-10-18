#pragma once

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/GCodeFlavor.hpp"

namespace Slic3r::Biz::Slicing {
struct ExtrudeConfig
{
    explicit ExtrudeConfig(const Domain::ConfigView& config);

    std::vector<double> default_acceleration{};
    std::vector<double> first_layer_acceleration{};
    std::vector<double> first_layer_acceleration_over_raft{};
    std::vector<double> bridge_acceleration{};
    std::vector<double> top_solid_infill_acceleration{};
    std::vector<double> solid_infill_acceleration{};
    std::vector<double> infill_acceleration{};
    std::vector<double> external_perimeter_acceleration{};
    std::vector<double> perimeter_acceleration{};
    std::vector<double> perimeter_speed{};
    std::vector<double> infill_speed{};
    std::vector<Domain::FloatOrPercentage> solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> external_perimeter_speed{};
    std::vector<double> bridge_speed{};
    // Orca internal bridges: speed (a percentage of bridge_speed) and flow ratio
    std::vector<Domain::FloatOrPercentage> internal_bridge_speed{};
    double internal_bridge_flow{1.};
    // Orca brim_flow_ratio
    double brim_flow_ratio{1.};
    std::vector<double> gap_fill_speed{};
    std::vector<double> retract_speed{};
    std::vector<double> retract_lift{};
    std::vector<double> retract_lift_above{};
    std::vector<double> retract_lift_below{};
    std::vector<Domain::RetractLiftEnforce> retract_lift_enforce{};
    std::vector<double> retract_length{};
    std::vector<double> retract_before_travel{};
    std::vector<double> travel_max_lift{};
    std::vector<double> travel_slope{};
    std::vector<bool> travel_ramping_lift{};
    std::vector<bool> retract_layer_change{};
    double travel_acceleration{};
    double travel_short_distance_acceleration{};

    std::vector<double> max_volumetric_speed{};
    std::vector<double> filament_max_volumetric_speed{};
    // the patterns of the Orca small area infill flow compensation
    Domain::InfillPattern top_fill_pattern{};
    Domain::InfillPattern bottom_fill_pattern{};
    Domain::InfillPattern internal_solid_infill_pattern{};
    std::vector<bool> filament_adaptive_volumetric_speed{};
    std::vector<std::string> volumetric_speed_coefficients{};
    std::vector<double> filament_infill_max_crossing_speed{};
    std::vector<double> filament_infill_max_speed{};

    std::vector<bool> enable_dynamic_overhang_speeds{};
    std::vector<bool> slowdown_for_curled_perimeters{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_0{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_1{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_2{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_3{};

    std::vector<bool> enable_dynamic_fan_speeds{};
    std::vector<int> overhang_fan_speed_0{};
    std::vector<int> overhang_fan_speed_1{};
    std::vector<int> overhang_fan_speed_2{};
    std::vector<int> overhang_fan_speed_3{};

    Domain::GCodeFlavor gcode_flavor{};
    std::vector<double> machine_max_feedrate_x{};
    std::vector<double> machine_max_feedrate_y{};
    std::vector<double> machine_max_feedrate_z{};
    std::vector<double> machine_max_acceleration_travel{};
    std::vector<double> machine_max_acceleration_z{};
    std::vector<double> machine_max_jerk_z{};

    std::vector<Domain::Percentage> fill_density{};

    double z_offset{};
    double travel_speed{};
    double ironing_speed{};
    double support_material_speed{};
    Domain::FloatOrPercentage support_material_interface_speed{};
    Domain::FloatOrPercentage small_support_perimeter_speed{};
    double small_support_perimeter_threshold{};
    std::vector<Domain::FloatOrPercentage> over_bridge_speed{};
    std::vector<Domain::FloatOrPercentage> top_solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_perimeter_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_external_perimeter_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_support_material_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_top_solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_gap_fill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_speed_over_raft{};
    double avoid_crossing_perimeters_max_detour{};
    bool gcode_comments{};
    Domain::ArcFittingType arc_fitting{};
    Domain::ScarfSeamPlacement scarf_seam_placement{};

    bool only_retract_when_crossing_perimeters{};
    bool avoid_crossing_perimeters{};
    bool avoid_crossing_curled_overhangs{};
    double skirt_speed{}; // Orca: 0 = the layer's extrusion speed
    // Orca: the overhang / bridge fan
    std::vector<bool> enable_overhang_bridge_fan{};
    std::vector<int> overhang_fan_speed{};
    std::vector<Domain::OverhangFanThreshold> overhang_fan_threshold{};
    // Orca: the scarf joint's speed (a percentage of its wall's speed) and flow
    Domain::FloatOrPercentage scarf_joint_speed{};
    double scarf_joint_flow_ratio{1.};
    // Orca: jerk (default_jerk 0 = off) and the first layer's travel acceleration / jerk
    double default_jerk{};
    double outer_wall_jerk{};
    double inner_wall_jerk{};
    double infill_jerk{};
    double top_surface_jerk{};
    double initial_layer_jerk{};
    double travel_jerk{};
    Domain::FloatOrPercentage initial_layer_travel_jerk{};
    Domain::FloatOrPercentage initial_layer_travel_acceleration{};
    double default_junction_deviation{};
    // Orca: role fan speeds (-1 = not used): markers for the CoolingBuffer
    std::vector<int> support_material_interface_fan_speed{};
    std::vector<int> ironing_fan_speed{};
    int slow_down_layers{}; // Orca: ramp from the first layer speed over this many layers
    // Orca's flow ratios (GCode::_extrude): print, top solid infill, and the per-feature ones
    bool set_other_flow_ratios{};
    double print_flow_ratio{1.};
    double top_solid_infill_flow_ratio{1.};
    double outer_wall_flow_ratio{1.};
    double inner_wall_flow_ratio{1.};
    double overhang_flow_ratio{1.};
    double sparse_infill_flow_ratio{1.};
    double internal_solid_infill_flow_ratio{1.};
    double gap_fill_flow_ratio{1.};
    double support_flow_ratio{1.};
    double support_interface_flow_ratio{1.};
    double first_layer_flow_ratio{1.};
};

} // namespace Slic3r::Biz::Slicing
