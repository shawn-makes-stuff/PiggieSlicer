#include "StepsInvalidation.hpp"

#include <variant>

#include "Slic3r/Domain/Constants.hpp"

#include "libslic3r/Print.hpp"

using Slic3r::Domain::is_approx;

namespace Slic3r::SlicingSync {

using Domain::ConfigView;
using Domain::Percentage;
using Domain::FloatOrPercentage;
using Step = std::variant<FDMPrintStep, FDMPrintObjectStep>;

std::vector<Step> propagate(Step step)
{
    return std::visit(
        Domain::overloaded{
            [](const FDMPrintStep& step) -> std::vector<Step>
            {
                switch (step) {
                case psWipeTower:
                    return {psWipeTower, psGCodeExport};
                case psAlertWhenSupportsNeeded:
                    return {psAlertWhenSupportsNeeded, psGCodeExport};
                case psSkirtBrim:
                    return {psSkirtBrim, psGCodeExport};
                case psGCodeExport:
                    return {psGCodeExport};
                default:
                    PANIC("Unknown print step propagation!");
                }
            },
            [](const FDMPrintObjectStep& step) -> std::vector<Step>
            {
                switch (step) {
                case posSlice:
                    return {
                        posSlice,
                        posPerimeters,
                        posPrepareInfill,
                        posInfill,
                        posIroning,
                        posSupportSpotsSearch,
                        posSupportMaterial,
                        posEstimateCurledExtrusions,
                        posCalculateOverhangingPerimeters,
                        psSkirtBrim,
                        psAlertWhenSupportsNeeded,
                        psWipeTower,
                        psGCodeExport
                    };
                case posPerimeters:
                    return {
                        posPerimeters,
                        posPrepareInfill,
                        posInfill,
                        posIroning,
                        posSupportSpotsSearch,
                        posEstimateCurledExtrusions,
                        posCalculateOverhangingPerimeters,
                        psSkirtBrim,
                        psAlertWhenSupportsNeeded,
                        psWipeTower,
                        psGCodeExport
                    };
                case posPrepareInfill:
                    return {
                        posPrepareInfill,
                        posInfill,
                        posIroning,
                        posSupportSpotsSearch,
                        psAlertWhenSupportsNeeded,
                        psWipeTower,
                        psGCodeExport
                    };
                case posInfill:
                    return {
                        posInfill,
                        posIroning,
                        posSupportSpotsSearch,
                        psAlertWhenSupportsNeeded,
                        psWipeTower,
                        psGCodeExport
                    };
                case posIroning:
                    return {posIroning, psAlertWhenSupportsNeeded, psWipeTower, psGCodeExport};
                case posSupportSpotsSearch:
                    return {
                        posSupportSpotsSearch,
                        psAlertWhenSupportsNeeded,
                        psWipeTower,
                        psGCodeExport
                    };
                case posSupportMaterial:
                    return {
                        posSupportMaterial,
                        posEstimateCurledExtrusions,
                        psSkirtBrim,
                        psAlertWhenSupportsNeeded,
                        psWipeTower,
                        psGCodeExport
                    };
                case posEstimateCurledExtrusions:
                    return {posEstimateCurledExtrusions, psAlertWhenSupportsNeeded, psWipeTower, psGCodeExport};
                case posCalculateOverhangingPerimeters:
                    return {posCalculateOverhangingPerimeters, psAlertWhenSupportsNeeded, psWipeTower, psGCodeExport};
                default:
                    PANIC("Unknown object step propagation!");
                }
            }
        },
        step
    );
}

std::vector<Step> steps(const std::vector<std::vector<Step>>& steps)
{
    std::vector<Step> result;
    for (const auto& _steps : steps) {
        result.insert(result.end(), _steps.begin(), _steps.end());
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<Step> all_steps()
{
    std::set<Step> result;
    for (size_t i{}; i < psCount; ++i) {
        result.insert(static_cast<FDMPrintStep>(i));
    }
    for (size_t i{}; i < posCount; ++i) {
        result.insert(static_cast<FDMPrintObjectStep>(i));
    }
    return std::vector<Step>{result.begin(), result.end()};
}

const std::map<std::string, std::vector<Step>> invalidated_by{
    {"arc_fitting", steps({propagate(posPerimeters)})},
    {"autoemit_temperature_commands", steps({propagate(psGCodeExport)})},
    {"automatic_extrusion_widths", steps({propagate(posPerimeters)})},
    {"automatic_infill_combination", steps({propagate(posPrepareInfill)})},
    {"automatic_infill_combination_max_layer_height", steps({propagate(posPrepareInfill)})},
    {"avoid_crossing_curled_overhangs", steps({propagate(posEstimateCurledExtrusions)})},
    {"avoid_crossing_perimeters", steps({propagate(psGCodeExport)})},
    {"avoid_crossing_perimeters_max_detour", steps({propagate(psGCodeExport)})},
    {"bed_custom_model", steps({})},
    {"bed_custom_texture", steps({})},
    {"bed_shape", steps({propagate(psGCodeExport)})},
    {"bed_temperature", steps({propagate(psGCodeExport)})},
    {"bed_temperature_extruder", steps({propagate(psGCodeExport)})},
    {"before_layer_gcode", steps({propagate(psGCodeExport)})},
    {"between_objects_gcode", steps({propagate(psGCodeExport)})},
    {"binary_gcode", steps({propagate(psGCodeExport)})},
    {"bottom_fill_pattern", steps({propagate(posInfill)})},
    {"bottom_solid_layers", steps({propagate(posPrepareInfill)})},
    {"bottom_solid_min_thickness", steps({propagate(posPrepareInfill)})},
    {"bridge_acceleration", steps({propagate(psGCodeExport)})},
    {"bridge_angle", steps({propagate(posPerimeters)})}, // also counterbore hole bridging (PerimeterGenerator::process_no_bridge)
    {"bridge_fan_speed", steps({propagate(psGCodeExport)})},
    {"bridge_flow_ratio",
     steps({propagate(posPerimeters), propagate(posInfill), propagate(posSupportMaterial)})},
    {"bridge_speed", steps({propagate(psGCodeExport)})},
    {"brim_separation", steps({propagate(posSupportSpotsSearch), propagate(posSupportMaterial)})},
    {"brim_type", steps({propagate(posSupportSpotsSearch), propagate(posSupportMaterial)})},
    {"brim_ears_max_angle", steps({propagate(psSkirtBrim)})},
    {"brim_ears_detection_length", steps({propagate(psSkirtBrim)})},
    {"brim_ears_outer_only", steps({propagate(psSkirtBrim)})},
    {"brim_use_efc_outline", steps({propagate(psSkirtBrim)})},
    {"brim_flow_ratio", steps({propagate(psGCodeExport)})},
    {"combine_brims", steps({propagate(psSkirtBrim)})},
    {"brim_width", steps({propagate(posSupportSpotsSearch), propagate(posSupportMaterial)})},
    {"chamber_minimal_temperature", steps({propagate(psGCodeExport)})},
    {"chamber_temperature", steps({propagate(psGCodeExport)})},
    {"color_change_gcode", steps({propagate(psGCodeExport)})},
    {"colorprint_heights", steps({propagate(psGCodeExport)})},
    {"complete_objects", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"print_order", steps({propagate(psGCodeExport)})},
    {"cooling", steps({propagate(psGCodeExport)})},
    {"cooling_perimeter_transition_distance", steps({propagate(psGCodeExport)})},
    {"cooling_slowdown_logic", steps({propagate(psGCodeExport)})},
    {"cooling_tube_length", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"cooling_tube_retraction", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"custom_parameters_print", steps({propagate(psGCodeExport)})},
    {"custom_parameters_printer", steps({propagate(psGCodeExport)})},
    {"custom_parameters_filament", steps({propagate(psGCodeExport)})},
    {"default_acceleration", steps({propagate(psGCodeExport)})},
    {"default_material", steps({propagate(psGCodeExport)})},
    {"default_print", steps({propagate(psGCodeExport)})},
    {"default_tool_print", steps({propagate(psGCodeExport)})},
    {"deretract_speed", steps({propagate(psGCodeExport)})},
    {"disable_fan_first_layers", steps({propagate(psGCodeExport)})},
    {"dont_support_bridges", steps({propagate(posSupportMaterial)})},
    {"draft_shield", steps({propagate(psSkirtBrim)})},
    {"duplicate_distance", steps({propagate(psGCodeExport)})},
    {"elefant_foot_compensation", steps({propagate(posSlice)})},
    {"enable_dynamic_fan_speeds", all_steps()}, // TODO: probably to harsh
    {"enable_dynamic_overhang_speeds", all_steps()}, // TODO: probably to harsh
    {"enable_filament_ramming", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"enable_pressure_advance_during_ramming", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"end_filament_gcode", steps({propagate(psGCodeExport)})},
    {"end_gcode", steps({propagate(psGCodeExport)})},
    {"ensure_vertical_shell_thickness", steps({propagate(posPrepareInfill)})},
    {"external_perimeter_acceleration", steps({propagate(psGCodeExport)})},
    {"external_perimeter_extrusion_width",
     steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"external_perimeter_speed", steps({propagate(psSkirtBrim)})}, // auto brim width
    {"wall_sequence", steps({propagate(posPerimeters)})},
    {"extra_loading_move", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"extra_perimeters", steps({propagate(posPerimeters)})},
    {"extra_perimeters_on_overhangs", steps({propagate(posPerimeters)})},
    {"extruder",
     steps({propagate(posPrepareInfill), propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"extruder_clearance_height", steps({propagate(psGCodeExport)})},
    {"extruder_clearance_height_to_lid", steps({propagate(psGCodeExport)})},
    {"extruder_clearance_radius", steps({propagate(psGCodeExport)})},
    {"extruder_colour", steps({propagate(psGCodeExport)})},
    {"extruder_offset", steps({propagate(psGCodeExport)})},
    {"extrusion_axis", steps({propagate(psGCodeExport)})},
    {"extrusion_multiplier", steps({propagate(psGCodeExport)})},
    {"extrusion_width", all_steps()},
    {"fan_always_on", steps({propagate(psGCodeExport)})},
    {"fan_below_layer_time", steps({propagate(psGCodeExport)})},
    {"filament_abrasive", steps({propagate(psGCodeExport)})},
    {"filament_colour", steps({propagate(psGCodeExport)})},
    {"filament_cooling_final_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_cooling_initial_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_cooling_moves", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_cost", steps({propagate(psGCodeExport)})},
    {"filament_density", steps({propagate(psGCodeExport)})},
    {"filament_diameter", steps({propagate(psGCodeExport)})},
    {"filament_flush_speed", steps({propagate(psGCodeExport)})},
    {"filament_flush_volume", steps({propagate(psGCodeExport)})},
    {"filament_infill_max_crossing_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_infill_max_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_change_time", steps({propagate(psGCodeExport)})},
    {"filament_loading_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_loading_speed_start", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_max_volumetric_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_adaptive_volumetric_speed", steps({propagate(psGCodeExport)})},
    {"volumetric_speed_coefficients", steps({propagate(psGCodeExport)})},
    {"filament_minimal_purge_on_wipe_tower",
     steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_multitool_ramming", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_multitool_ramming_flow", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_multitool_ramming_volume", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_notes", steps({propagate(psGCodeExport)})},
    {"filament_purge_multiplier", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_ramming_parameters", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_ramming_temperature_delta", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_ramming_initial_delay", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_shrinkage_compensation_xy", steps({propagate(posSlice)})},
    {"filament_shrinkage_compensation_z", steps({propagate(posSlice)})},
    {"filament_soluble", steps({propagate(psWipeTower), propagate(posSupportMaterial)})},
    {"filament_spool_weight", steps({propagate(psGCodeExport)})},
    {"filament_stamping_distance", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_stamping_loading_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_toolchange_delay", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_type", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_unloading_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_unloading_speed_start", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"filament_vendor", steps({})},
    {"fill_angle", steps({propagate(posInfill)})},
    {"internal_bridge_angle", steps({propagate(posPrepareInfill)})},
    {"relative_bridge_angle", steps({propagate(posPerimeters)})}, // as bridge_angle
    {"bridge_density", steps({propagate(posInfill)})},
    {"internal_bridge_density", steps({propagate(posInfill)})},
    {"thick_internal_bridges", steps({propagate(posInfill)})},
    {"enable_extra_bridge_layer", steps({propagate(posPrepareInfill)})},
    {"counterbore_hole_bridging", steps({propagate(posPerimeters)})},
    {"sparse_infill_rotate_template", steps({propagate(posInfill)})},
    {"solid_infill_rotate_template", steps({propagate(posInfill)})},
    {"align_infill_direction_to_model", steps({propagate(posPerimeters)})},
    {"dont_filter_internal_bridges", steps({propagate(posPrepareInfill)})},
    {"bridge_line_width", steps({propagate(posPerimeters)})},
    {"elefant_foot_layers_density", steps({propagate(posInfill)})},
    {"internal_bridge_speed", steps({propagate(psGCodeExport)})},
    {"internal_bridge_flow", steps({propagate(psGCodeExport)})},
    {"internal_bridge_fan_speed", steps({propagate(psGCodeExport)})},
    {"solid_infill_direction", steps({propagate(posInfill)})},
    {"top_layer_direction", steps({propagate(posInfill)})},
    {"bottom_layer_direction", steps({propagate(posInfill)})},
    {"internal_solid_infill_pattern", steps({propagate(posInfill)})},
    {"top_surface_density", steps({propagate(posInfill)})},
    {"detect_narrow_internal_solid_infill", steps({propagate(posInfill)})},
    {"top_surface_fill_order", steps({propagate(posInfill)})},
    {"precise_outer_wall", steps({propagate(posPerimeters)})},
    {"precise_z_height", steps({propagate(posSlice)})},
    {"make_overhang_printable", steps({propagate(posSlice)})},
    {"make_overhang_printable_angle", steps({propagate(posSlice)})},
    {"make_overhang_printable_hole_size", steps({propagate(posSlice)})},
    {"hole_to_polyhole", steps({propagate(posSlice)})},
    {"hole_to_polyhole_threshold", steps({propagate(posSlice)})},
    {"hole_to_polyhole_twisted", steps({propagate(posSlice)})},
    {"hole_to_polyhole_max_edges", steps({propagate(posSlice)})},
    {"top_surface_expansion", steps({propagate(posPrepareInfill)})},
    {"top_surface_expansion_margin", steps({propagate(posPrepareInfill)})},
    {"top_surface_expansion_direction", steps({propagate(posPrepareInfill)})},
    {"bottom_surface_fill_order", steps({propagate(posInfill)})},
    {"center_of_surface_pattern", steps({propagate(posInfill)})},
    {"small_area_infill_flow_compensation", steps({propagate(psGCodeExport)})},
    {"small_area_infill_flow_compensation_model", steps({propagate(psGCodeExport)})},
    {"bottom_surface_density", steps({propagate(posInfill)})},
    {"fill_density", steps({propagate(posPrepareInfill)})},
    {"fill_pattern", steps({propagate(posPrepareInfill)})},
    {"fill_multiline", steps({propagate(posPrepareInfill)})},
    {"infill_complete_top", steps({propagate(posInfill)})},
    {"lateral_lattice_angle_1", steps({propagate(posInfill)})},
    {"lateral_lattice_angle_2", steps({propagate(posInfill)})},
    {"infill_overhang_angle", steps({propagate(posInfill)})},
    {"sparse_infill_smooth_factor", steps({propagate(posPrepareInfill)})},
    {"separated_infills", steps({propagate(posPrepareInfill)})},
    {"support_threshold_overlap", steps({propagate(posSupportMaterial)})},
    {"support_expansion", steps({propagate(posSupportMaterial)})},
    {"support_remove_small_overhang", steps({propagate(posSupportMaterial)})},
    {"support_object_first_layer_gap", steps({propagate(posSupportMaterial)})},
    {"support_bottom_interface_spacing", steps({propagate(posSupportMaterial)})},
    {"support_ironing", steps({propagate(posSupportMaterial)})},
    {"support_ironing_pattern", steps({propagate(posSupportMaterial)})},
    {"support_ironing_flow", steps({propagate(posSupportMaterial)})},
    {"support_ironing_spacing", steps({propagate(posSupportMaterial)})},
    {"support_interface_not_for_body", steps({propagate(posSupportMaterial)})},
    {"extra_solid_infills", steps({propagate(posPrepareInfill)})},
    {"tpms_adaptive", steps({propagate(posPrepareInfill)})},
    {"tpms_interior_density", steps({propagate(posPrepareInfill)})},
    {"tpms_adaptive_gradient", steps({propagate(posPrepareInfill)})},
    {"gyroid_optimized", steps({propagate(posPrepareInfill)})},
    {"infill_shift_step", steps({propagate(posPrepareInfill)})},
    {"symmetric_infill_y_axis", steps({propagate(posPrepareInfill)})},
    {"skin_infill_density", steps({propagate(posInfill)})},
    {"skeleton_infill_density", steps({propagate(posInfill)})},
    {"skin_infill_depth", steps({propagate(posInfill)})},
    {"infill_lock_depth", steps({propagate(posInfill)})},
    {"skin_infill_line_width", steps({propagate(posInfill)})},
    {"skeleton_infill_line_width", steps({propagate(posInfill)})},
    {"lightning_overhang_angle", steps({propagate(posPrepareInfill)})},
    {"lightning_prune_angle", steps({propagate(posPrepareInfill)})},
    {"lightning_straightening_angle", steps({propagate(posPrepareInfill)})},
    {"first_layer_acceleration", steps({propagate(psGCodeExport)})},
    {"first_layer_acceleration_over_raft", steps({propagate(psGCodeExport)})},
    {"first_layer_bed_temperature", steps({propagate(psGCodeExport)})},
    {"first_layer_extrusion_width",
     steps(
         {propagate(posSupportMaterial),
          propagate(posPerimeters),
          propagate(posInfill),
          propagate(psSkirtBrim)}
     )},
    {"first_layer_height", steps({propagate(posSlice)})},
    {"first_layer_infill_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_solid_infill_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_perimeter_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_external_perimeter_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_support_material_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_top_solid_infill_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_gap_fill_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"first_layer_speed_over_raft", steps({propagate(psGCodeExport)})},
    {"first_layer_temperature", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"full_fan_speed_layer", steps({propagate(psGCodeExport)})},
    {"fuzzy_skin", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_point_dist", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_thickness", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_first_layer", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_mode", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_noise_type", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_scale", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_octaves", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_persistence", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_ripples_per_layer", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_ripple_offset", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"fuzzy_skin_layers_between_ripple_offset", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},

    // Filtering of unprintable regions in multi-material segmentation depends on if gap-fill is enabled or not.
    // So step posSlice is invalidated when gap-fill was enabled/disabled by option "gap_fill_enabled" or by
    // changing "gap_fill_speed" to force recomputation of the multi-material segmentation.
    // For the sake of simplicity, just invalidate the slicing every time.
    {
        "gap_fill_enabled",
        steps({propagate(posPerimeters), propagate(posSlice)})
    }, // posSlice si only required for mm segmentation.
    {
        "gap_fill_speed",
        steps({propagate(posPerimeters), propagate(posSlice)})
    }, // posSlice si only required for mm segmentation.

    {"gcode_comments", steps({propagate(psGCodeExport)})},
    {"gcode_flavor", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"gcode_label_objects", steps({propagate(psGCodeExport)})},
    {"gcode_resolution",
     steps(
         {propagate(posPerimeters),
          propagate(posInfill),
          propagate(posSupportMaterial),
          propagate(psSkirtBrim)}
     )},
    {"gcode_substitutions", steps({propagate(psGCodeExport)})},
    {"high_current_on_filament_swap", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"idle_temperature", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"infill_acceleration", steps({propagate(psGCodeExport)})},
    {"infill_anchor", steps({propagate(posInfill)})},
    {"infill_anchor_max", steps({propagate(posInfill)})},
    {"infill_every_layers", steps({propagate(posPrepareInfill)})},
    {"infill_extruder", steps({propagate(posPrepareInfill)})},
    {"infill_extrusion_width", steps({propagate(posPrepareInfill)})},
    {"infill_first", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"infill_overlap", steps({propagate(posPerimeters)})},
    {"top_bottom_infill_wall_overlap", steps({propagate(posPerimeters)})},
    {"min_width_top_surface", steps({propagate(posPerimeters)})},
    {"filter_out_gap_fill", steps({propagate(posPerimeters)})},
    {"infill_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})}, // and the auto brim width
    {"inherits_cummulative", steps({})},
    {"interface_shells", steps({propagate(posPrepareInfill)})},
    {"interlocking_beam", all_steps()}, // TODO: Probably too harsh.
    {"interlocking_beam_layer_count", all_steps()}, // TODO: Probably too harsh.
    {"interlocking_beam_width", all_steps()}, // TODO: Probably too harsh.
    {"interlocking_boundary_avoidance", all_steps()}, // TODO: Probably too harsh.
    {"interlocking_depth", all_steps()}, // TODO: Probably too harsh.
    {"interlocking_orientation", all_steps()}, // TODO: Probably too harsh.
    {"ironing", all_steps()}, // TODO: Probably too harsh.
    {"ironing_flowrate", all_steps()}, // TODO: Probably too harsh.
    {"ironing_spacing", all_steps()}, // TODO: Probably too harsh.
    {"ironing_speed", all_steps()}, // TODO: Probably too harsh.
    {"ironing_type", all_steps()}, // TODO: Probably too harsh.
    {"ironing_pattern", all_steps()},
    {"spiral_mode_smooth", steps({propagate(psGCodeExport)})},
    {"spiral_mode_max_xy_smoothing", steps({propagate(psGCodeExport)})},
    {"spiral_starting_flow_ratio", steps({propagate(psGCodeExport)})},
    {"spiral_finishing_flow_ratio", steps({propagate(psGCodeExport)})},
    {"ironing_inset", all_steps()},
    {"ironing_angle", all_steps()},
    {"ironing_angle_fixed", all_steps()},
    {"layer_gcode", steps({propagate(psGCodeExport)})},
    {"layer_height", steps({propagate(posSlice)})},
    {"machine_limits_usage", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_x", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_y", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_z", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_e", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_extruding", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_retracting", steps({propagate(psGCodeExport)})},
    {"machine_max_acceleration_travel", steps({propagate(psGCodeExport)})},
    {"machine_max_feedrate_x", steps({propagate(psGCodeExport)})},
    {"machine_max_feedrate_y", steps({propagate(psGCodeExport)})},
    {"machine_max_feedrate_z", steps({propagate(psGCodeExport)})},
    {"machine_max_feedrate_e", steps({propagate(psGCodeExport)})},
    {"machine_max_jerk_x", steps({propagate(psGCodeExport)})},
    {"machine_max_jerk_y", steps({propagate(psGCodeExport)})},
    {"machine_max_jerk_z", steps({propagate(psGCodeExport)})},
    {"machine_max_jerk_e", steps({propagate(psGCodeExport)})},
    {"machine_max_junction_deviation", steps({propagate(psGCodeExport)})},
    {"machine_min_extruding_rate", steps({propagate(psGCodeExport)})},
    {"machine_min_travel_rate", steps({propagate(psGCodeExport)})},
    {"max_fan_speed", steps({propagate(psGCodeExport)})},
    {"max_layer_height",
     steps(
         {propagate(posSlice),
          propagate(posPerimeters),
          propagate(posInfill),
          propagate(posSupportMaterial),
          propagate(psSkirtBrim)}
     )},
    {"max_print_height", steps({propagate(psGCodeExport)})},
    {"max_print_speed", steps({propagate(psGCodeExport)})},
    {"max_volumetric_extrusion_rate_slope_negative", steps({propagate(psGCodeExport)})},
    {"max_volumetric_extrusion_rate_slope_positive", steps({propagate(psGCodeExport)})},
    {"max_volumetric_extrusion_rate_slope_segment_length", steps({propagate(psGCodeExport)})},
    {"extrusion_rate_smoothing_external_perimeter_only", steps({propagate(psGCodeExport)})},
    {"slowdown_for_curled_perimeters", steps({propagate(posEstimateCurledExtrusions)})},
    {"small_support_perimeter_speed", steps({propagate(psSkirtBrim)})}, // auto brim width
    {"small_support_perimeter_threshold", steps({propagate(psGCodeExport)})},
    {"max_volumetric_speed", steps({propagate(psGCodeExport)})},
    {"initial_layer_min_bead_width", steps({propagate(posPerimeters)})},
    {"min_length_factor", steps({propagate(posPerimeters)})},
    {"wall_maximum_resolution", steps({propagate(posPerimeters)})},
    {"wall_maximum_deviation", steps({propagate(posPerimeters)})},
    {"min_bead_width", steps({propagate(posSlice)})},
    {"min_fan_speed", steps({propagate(psGCodeExport)})},
    {"min_feature_size", steps({propagate(posSlice)})},
    {"min_layer_height",
     steps(
         {propagate(posSlice),
          propagate(posPerimeters),
          propagate(posInfill),
          propagate(posSupportMaterial),
          propagate(psSkirtBrim)}
     )},
    {"min_print_speed", steps({propagate(psGCodeExport)})},
    {"min_skirt_length", steps({propagate(psSkirtBrim)})},
    {"mmu_segmented_region_interlocking_depth", steps({propagate(posSlice)})},
    {"mmu_segmented_region_max_width", steps({propagate(posSlice)})},
    {"multimaterial_purging", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"notes", steps({propagate(psGCodeExport)})},
    {"nozzle_diameter", steps({propagate(posSlice)})},
    {"nozzle_high_flow", steps({propagate(psGCodeExport)})},
    {"only_one_perimeter_first_layer", steps({propagate(posPerimeters)})},
    {"only_retract_when_crossing_perimeters", steps({propagate(psGCodeExport)})},
    {"ooze_prevention", steps({propagate(psSkirtBrim)})},
    {"adaptive_pressure_advance", steps({propagate(psGCodeExport)})},
    {"adaptive_pressure_advance_overhangs", steps({propagate(psGCodeExport)})},
    {"adaptive_pressure_advance_bridges", steps({propagate(psGCodeExport)})},
    {"adaptive_pressure_advance_model", steps({propagate(psGCodeExport)})},
    {"preheat_time", steps({propagate(psGCodeExport)})},
    {"preheat_steps", steps({propagate(psGCodeExport)})},
    {"output_filename_format", steps({})},
    {"over_bridge_speed", steps({propagate(psGCodeExport)})},
    {"overhangs", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"parking_pos_retraction", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"pause_print_gcode", steps({propagate(psGCodeExport)})},
    {"perimeter_acceleration", steps({propagate(psGCodeExport)})},
    {"perimeter_extruder", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"outer_wall_filament_id", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"top_surface_filament_id", steps({propagate(posPrepareInfill)})},
    {"bottom_surface_filament_id", steps({propagate(posPrepareInfill)})},
    {"perimeter_extrusion_width", steps({propagate(posPerimeters)})},
    {"perimeter_generator", steps({propagate(posSlice)})},
    {"perimeter_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})}, // and Orca's auto brim width (Brim.cpp)
    {"perimeters", steps({propagate(posPerimeters)})},
    {"alternate_extra_wall", steps({propagate(posPerimeters)})},
    {"overhang_reverse", steps({propagate(posPerimeters)})},
    {"overhang_reverse_internal_only", steps({propagate(posPerimeters)})},
    {"overhang_reverse_threshold", steps({propagate(posPerimeters)})},
    {"unsupported_wall_last", steps({propagate(posPerimeters)})},
    {"post_process", steps({propagate(psGCodeExport)})},
    {"prefer_clockwise_movements", steps({propagate(posSlice)})},
    {"printer_model", steps({propagate(psGCodeExport)})},
    {"printer_notes", steps({propagate(psGCodeExport)})},
    {"printer_technology", all_steps()},
    {"printer_variant", steps({})},
    {"printer_vendor", steps({})},
    {"raft_contact_distance", steps({propagate(posSlice)})},
    {"raft_expansion", steps({propagate(posSupportMaterial)})},
    {"raft_first_layer_density", steps({propagate(posSupportMaterial)})},
    {"raft_first_layer_expansion", steps({propagate(posSupportMaterial)})},
    {"raft_layers", steps({propagate(posSlice)})},
    {"remaining_times", steps({propagate(psGCodeExport)})},
    {"resolution", steps({propagate(posSlice)})},
    {"retract_before_travel", steps({propagate(psGCodeExport)})},
    {"retract_before_wipe", steps({propagate(psGCodeExport)})},
    {"retract_layer_change", steps({propagate(psGCodeExport)})},
    {"retract_length", steps({propagate(psGCodeExport)})},
    {"retract_length_toolchange", steps({propagate(psGCodeExport)})},
    {"retract_lift", steps({propagate(psGCodeExport)})},
    {"retract_lift_above", steps({propagate(psGCodeExport)})},
    {"retract_lift_below", steps({propagate(psGCodeExport)})},
    {"retract_lift_enforce", steps({propagate(psGCodeExport)})},
    {"retract_restart_extra", steps({propagate(psGCodeExport)})},
    {"retract_restart_extra_toolchange", steps({propagate(psGCodeExport)})},
    {"retract_speed", steps({propagate(psGCodeExport)})},
    {"overhang_fan_speed_0", all_steps()}, // TODO: maybe to harsh
    {"overhang_fan_speed_1", all_steps()}, // TODO: maybe to harsh
    {"overhang_fan_speed_2", all_steps()}, // TODO: maybe to harsh
    {"overhang_fan_speed_3", all_steps()}, // TODO: maybe to harsh
    {"overhang_speed_0", all_steps()}, // TODO: maybe to harsh
    {"overhang_speed_1", all_steps()}, // TODO: maybe to harsh
    {"overhang_speed_2", all_steps()}, // TODO: maybe to harsh
    {"overhang_speed_3", all_steps()}, // TODO: maybe to harsh
    {"pressure_advance", steps({propagate(psGCodeExport)})},
    {"pressure_advance_value", steps({propagate(psGCodeExport)})},
    {"scarf_seam_entire_loop", steps({propagate(psGCodeExport)})},
    {"scarf_seam_length", steps({propagate(psGCodeExport)})},
    {"scarf_seam_max_segment_length", steps({propagate(psGCodeExport)})},
    {"scarf_seam_on_inner_perimeters", steps({propagate(psGCodeExport)})},
    {"scarf_seam_only_on_smooth", steps({propagate(psGCodeExport)})},
    {"scarf_angle_threshold", steps({propagate(psGCodeExport)})},
    {"scarf_overhang_threshold", steps({propagate(psGCodeExport)})},
    {"seam_slope_steps", steps({propagate(psGCodeExport)})},
    {"scarf_seam_placement", steps({propagate(psGCodeExport)})},
    {"scarf_seam_start_height", steps({propagate(psGCodeExport)})},
    {"seam_gap_distance", steps({propagate(psGCodeExport)})},
    {"seam_position", steps({propagate(psGCodeExport)})},
    {"silent_mode", steps({propagate(psGCodeExport)})},
    {"single_extruder_multi_material", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"single_extruder_multi_material_priming", steps({propagate(psGCodeExport)})},
    {"skirt_distance", steps({propagate(psSkirtBrim)})},
    {"slow_down_layers", steps({propagate(psGCodeExport)})},
    {"enable_overhang_bridge_fan", all_steps()}, // as enable_dynamic_fan_speeds: the overhangs are measured for it
    {"overhang_fan_speed", steps({propagate(psGCodeExport)})},
    {"overhang_fan_threshold", steps({propagate(psGCodeExport)})},
    {"scarf_joint_speed", steps({propagate(psGCodeExport)})},
    {"scarf_joint_flow_ratio", steps({propagate(psGCodeExport)})},
    {"time_cost", steps({propagate(psGCodeExport)})},
    {"enable_power_loss_recovery", steps({propagate(psGCodeExport)})},
    {"bed_mesh_min", steps({propagate(psGCodeExport)})},
    {"bed_mesh_max", steps({propagate(psGCodeExport)})},
    {"bed_mesh_probe_distance", steps({propagate(psGCodeExport)})},
    {"adaptive_bed_mesh_margin", steps({propagate(psGCodeExport)})},
    {"support_chamber_temp_control", steps({propagate(psGCodeExport)})},
    {"activate_chamber_temp_control", steps({propagate(psGCodeExport)})},
    {"temperature_vitrification", steps({propagate(psGCodeExport)})},
    {"nozzle_temperature_range_low", steps({propagate(psGCodeExport)})},
    {"nozzle_temperature_range_high", steps({propagate(psGCodeExport)})},
    {"filament_flush_temp", steps({propagate(psGCodeExport)})},
    {"filament_flush_volumetric_speed", steps({propagate(psGCodeExport)})},
    {"filament_is_support", steps({propagate(psGCodeExport)})},
    {"file_start_gcode", steps({propagate(psGCodeExport)})},
    {"time_lapse_gcode", steps({propagate(psGCodeExport)})},
    {"timelapse_type", steps({propagate(psGCodeExport)})},
    {"gcode_skip_config_block", steps({propagate(psGCodeExport)})},
    {"gcode_add_line_number", steps({propagate(psGCodeExport)})},
    {"manual_filament_change", steps({propagate(psGCodeExport)})},
    {"bed_temperature_formula", steps({propagate(psGCodeExport)})},
    {"default_jerk", steps({propagate(psGCodeExport)})},
    {"outer_wall_jerk", steps({propagate(psGCodeExport)})},
    {"inner_wall_jerk", steps({propagate(psGCodeExport)})},
    {"infill_jerk", steps({propagate(psGCodeExport)})},
    {"top_surface_jerk", steps({propagate(psGCodeExport)})},
    {"initial_layer_jerk", steps({propagate(psGCodeExport)})},
    {"travel_jerk", steps({propagate(psGCodeExport)})},
    {"initial_layer_travel_jerk", steps({propagate(psGCodeExport)})},
    {"initial_layer_travel_acceleration", steps({propagate(psGCodeExport)})},
    {"accel_to_decel_enable", steps({propagate(psGCodeExport)})},
    {"accel_to_decel_factor", steps({propagate(psGCodeExport)})},
    {"default_junction_deviation", steps({propagate(psGCodeExport)})},
    {"initial_layer_fan_speed", steps({propagate(psGCodeExport)})},
    {"support_material_interface_fan_speed", steps({propagate(psGCodeExport)})},
    {"ironing_fan_speed", steps({propagate(psGCodeExport)})},
    {"additional_cooling_fan_speed", steps({propagate(psGCodeExport)})},
    {"dont_slow_down_outer_wall", steps({propagate(psGCodeExport)})},
    {"auxiliary_fan", steps({propagate(psGCodeExport)})},
    {"part_cooling_fan_min_pwm", steps({propagate(psGCodeExport)})},
    {"fan_speedup_time", steps({propagate(psGCodeExport)})},
    {"fan_speedup_overhangs", steps({propagate(psGCodeExport)})},
    {"fan_kickstart", steps({propagate(psGCodeExport)})},
    {"small_perimeter_threshold", steps({propagate(psGCodeExport)})},
    {"support_air_filtration", steps({propagate(psGCodeExport)})},
    {"activate_air_filtration", steps({propagate(psGCodeExport)})},
    {"activate_air_filtration_during_print", steps({propagate(psGCodeExport)})},
    {"during_print_exhaust_fan_speed", steps({propagate(psGCodeExport)})},
    {"activate_air_filtration_on_completion", steps({propagate(psGCodeExport)})},
    {"complete_print_exhaust_fan_speed", steps({propagate(psGCodeExport)})},
    {"set_other_flow_ratios", steps({propagate(psGCodeExport)})},
    {"print_flow_ratio", steps({propagate(psGCodeExport)})},
    {"top_solid_infill_flow_ratio", steps({propagate(psGCodeExport)})},
    {"bottom_solid_infill_flow_ratio", steps({propagate(psGCodeExport)})},
    {"outer_wall_flow_ratio", steps({propagate(psGCodeExport)})},
    {"inner_wall_flow_ratio", steps({propagate(psGCodeExport)})},
    {"overhang_flow_ratio", steps({propagate(psGCodeExport)})},
    {"sparse_infill_flow_ratio", steps({propagate(psGCodeExport)})},
    {"internal_solid_infill_flow_ratio", steps({propagate(psGCodeExport)})},
    {"gap_fill_flow_ratio", steps({propagate(psGCodeExport)})},
    {"support_flow_ratio", steps({propagate(psGCodeExport)})},
    {"support_interface_flow_ratio", steps({propagate(psGCodeExport)})},
    {"first_layer_flow_ratio", steps({propagate(psGCodeExport)})},
    {"wipe_speed", steps({propagate(psGCodeExport)})},
    {"role_based_wipe_speed", steps({propagate(psGCodeExport)})},
    {"skirt_speed", steps({propagate(psGCodeExport)})},
    {"initial_layer_travel_speed", steps({propagate(psGCodeExport)})},
    {"change_extrusion_role_gcode", steps({propagate(psGCodeExport)})},
    {"process_change_extrusion_role_gcode", steps({propagate(psGCodeExport)})},
    {"filament_change_extrusion_role_gcode", steps({propagate(psGCodeExport)})},
    {"skirt_height", steps({propagate(psSkirtBrim)})},
    {"skirt_start_angle", steps({propagate(psGCodeExport)})},
    {"single_loop_draft_shield", steps({propagate(psGCodeExport)})},
    {"skirts", steps({propagate(psSkirtBrim)})},
    {"skirt_type", steps({propagate(psSkirtBrim)})},
    {"slice_closing_radius", steps({propagate(posSlice)})},
    {"slicing_mode", steps({propagate(posSlice)})},
    {"slowdown_below_layer_time", steps({propagate(psGCodeExport)})},
    {"small_perimeter_speed", steps({propagate(psSkirtBrim)})}, // auto brim width
    {"solid_infill_acceleration", steps({propagate(psGCodeExport)})},
    {"solid_infill_below_area", steps({propagate(posPrepareInfill)})},
    {"solid_infill_every_layers", steps({propagate(posPrepareInfill)})},
    {"solid_infill_extruder", steps({propagate(posPrepareInfill)})},
    {"solid_infill_extrusion_width",
     steps({propagate(posPrepareInfill), propagate(posPerimeters)})},
    {"solid_infill_speed", steps({propagate(psSkirtBrim)})}, // auto brim width
    {"spiral_vase", steps({propagate(posSlice)})},
    {"staggered_inner_seams", steps({propagate(psGCodeExport)})},
    {"standby_temperature_delta", steps({propagate(psGCodeExport)})},
    {"start_filament_gcode", steps({propagate(psGCodeExport)})},
    {"start_gcode", steps({propagate(psGCodeExport)})},
    {"stuck_filament_detection", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"support_material", steps({propagate(posSupportMaterial)})},
    {"support_material_angle", steps({propagate(posSupportMaterial)})},
    {"support_material_bottom_contact_distance", steps({propagate(posSupportMaterial)})},
    {"support_material_bottom_interface_layers", steps({propagate(posSupportMaterial)})},
    {"support_material_buildplate_only", steps({propagate(posSupportMaterial)})},
    {"support_material_closing_radius", steps({propagate(posSupportMaterial)})},
    {"support_material_contact_distance", steps({propagate(posSlice)})},
    {"support_material_enforce_layers", steps({propagate(posSupportMaterial)})},
    {"support_material_extruder", steps({propagate(posSupportMaterial)})},
    {"support_material_extrusion_width", steps({propagate(posSupportMaterial)})},
    {"support_material_first_layer_density", steps({propagate(posSupportMaterial)})},
    {"support_material_first_layer_expansion", steps({propagate(posSupportMaterial)})},
    {"support_material_interface_contact_loops", steps({propagate(posSupportMaterial)})},
    {"support_material_interface_extruder", steps({propagate(posSupportMaterial)})},
    {"support_material_interface_layers", steps({propagate(posSupportMaterial)})},
    {"support_material_interface_pattern", steps({propagate(posSupportMaterial)})},
    {"support_material_interface_spacing", steps({propagate(posSupportMaterial)})},
    {"support_material_interface_speed", steps({propagate(psGCodeExport)})},
    {"support_material_pattern", steps({propagate(posSupportMaterial)})},
    {"support_material_spacing", steps({propagate(posSupportMaterial)})},
    {"support_material_speed", steps({propagate(psSkirtBrim)})}, // auto brim width
    {"support_material_style", steps({propagate(posSupportMaterial)})},
    {"support_material_synchronize_layers", steps({propagate(posSupportMaterial)})},
    {"support_material_threshold", steps({propagate(posSupportMaterial)})},
    {"support_material_with_sheath", steps({propagate(posSupportMaterial)})},
    {"support_material_xy_spacing", steps({propagate(posSupportMaterial)})},
    {"support_tree_angle", steps({propagate(posSupportMaterial)})},
    {"support_tree_angle_slow", steps({propagate(posSupportMaterial)})},
    {"support_tree_branch_diameter", steps({propagate(posSupportMaterial)})},
    {"support_tree_branch_diameter_angle", steps({propagate(posSupportMaterial)})},
    {"support_tree_branch_diameter_double_wall", steps({propagate(posSupportMaterial)})},
    {"support_tree_branch_distance", steps({propagate(posSupportMaterial)})},
    {"support_tree_tip_diameter", steps({propagate(posSupportMaterial)})},
    {"support_tree_top_rate", steps({propagate(posSupportMaterial)})},
    {"temperature", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"template_custom_gcode", steps({propagate(psGCodeExport)})},
    {"thick_bridges", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"thin_walls", steps({propagate(posPerimeters), propagate(posSupportMaterial)})},
    {"thumbnails", steps({propagate(psGCodeExport)})},
    {"thumbnails_format", steps({propagate(psGCodeExport)})},
    {"toolchange_gcode", steps({propagate(psGCodeExport)})},
    {"toolchange_ordering", steps({propagate(psWipeTower)})},
    {"toolchange_cyclic_order", steps({propagate(psWipeTower)})},
    {"toolchange_cyclic_first_layer", steps({propagate(psWipeTower)})},
    {"top_fill_pattern", steps({propagate(posInfill)})},
    {"top_infill_extrusion_width", steps({propagate(posInfill)})},
    {"top_one_perimeter_type", steps({propagate(posPerimeters)})},
    {"top_solid_infill_acceleration", steps({propagate(psGCodeExport)})},
    {"top_solid_infill_speed", steps({propagate(psSkirtBrim)})}, // auto brim width
    {"top_solid_layers", steps({propagate(posPrepareInfill)})},
    {"top_solid_min_thickness", steps({propagate(posPrepareInfill)})},
    {"travel_acceleration", steps({propagate(psGCodeExport)})},
    {"travel_short_distance_acceleration", steps({propagate(psGCodeExport)})},
    {"travel_lift_before_obstacle", steps({propagate(psGCodeExport)})},
    {"travel_max_lift", steps({propagate(psGCodeExport)})},
    {"travel_ramping_lift", steps({propagate(psGCodeExport)})},
    {"travel_slope", steps({propagate(psGCodeExport)})},
    {"travel_speed", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"travel_speed_z", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"use_firmware_retraction", steps({propagate(psGCodeExport)})},
    {"use_relative_e_distances", steps({propagate(psGCodeExport)})},
    {"use_volumetric_e", steps({propagate(psGCodeExport)})},
    {"variable_layer_height", steps({propagate(psGCodeExport)})},
    {"wall_distribution_count", steps({propagate(posSlice)})},
    {"wall_transition_angle", steps({propagate(posSlice)})},
    {"wall_transition_filter_deviation", steps({propagate(posSlice)})},
    {"wall_transition_length", steps({propagate(posSlice)})},
    {"wipe", steps({propagate(psGCodeExport)})},
    {"wipe_on_loops", steps({propagate(psGCodeExport)})},
    {"wipe_inward", steps({propagate(psGCodeExport)})},
    {"wipe_inward_distance", steps({propagate(psGCodeExport)})},
    {"wipe_before_external_loop", steps({propagate(psGCodeExport)})},
    {"wipe_distance", steps({propagate(psGCodeExport)})},
    {"wipe_into_infill", steps({propagate(psWipeTower), propagate(psGCodeExport)})},
    {"wipe_into_objects", steps({propagate(psWipeTower), propagate(psGCodeExport)})},
    {"wipe_tower", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_acceleration", steps({propagate(psGCodeExport)})},
    {"wipe_tower_bridging", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_brim_width", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_cone_angle", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_auto_size", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_skip_points", steps({propagate(psWipeTower)})},
    {"color_stacking", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"color_stacking_max_layers", steps({propagate(psWipeTower)})},
    {"color_stacking_overhang_tolerance", steps({propagate(psWipeTower)})},
    {"color_stacking_island_order", steps({propagate(psWipeTower)})},
    {"color_stacking_surface_tolerance", steps({propagate(psWipeTower)})},
    {"color_stacking_disregard_min_layer_time", steps({propagate(psGCodeExport)})},
    {"nozzle_tip_diameter", steps({propagate(psWipeTower)})},
    {"nozzle_cone_angle", steps({propagate(psWipeTower)})},
    {"nozzle_clear_height", steps({propagate(psWipeTower)})},
    {"nozzle_contact_height", steps({propagate(psWipeTower)})},
    {"wipe_tower_sparse_layers_combination", steps({propagate(psWipeTower)})},
    {"enable_tower_interface_features", steps({propagate(psWipeTower)})},
    {"enable_tower_interface_cooldown_during_tower", steps({propagate(psWipeTower)})},
    {"wait_for_temp_on_wipe_tower", steps({propagate(psWipeTower), propagate(psGCodeExport)})},
    {"filament_tower_interface_pre_extrusion_dist", steps({propagate(psWipeTower)})},
    {"filament_tower_interface_pre_extrusion_length", steps({propagate(psWipeTower)})},
    {"filament_tower_ironing_area", steps({propagate(psWipeTower)})},
    {"filament_tower_interface_purge_volume", steps({propagate(psWipeTower)})},
    {"filament_tower_interface_print_temp", steps({propagate(psWipeTower), propagate(psGCodeExport)})},
    {"wipe_tower_wall_type", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_extra_rib_length", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_rib_width", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_fillet_wall", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_max_purge_speed", steps({propagate(psWipeTower)})},
    {"wipe_tower_extra_flow", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_extra_spacing", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_extruder", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_no_sparse_layers", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wipe_tower_width", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"purge_in_prime_tower", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"prime_volume", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wiping_volumes_matrix", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wiping_volumes_use_custom_matrix", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"wiping_volumes_by_colour", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
    {"xy_size_compensation", steps({propagate(posSlice)})},
    {"xy_hole_compensation", steps({propagate(posSlice)})},
    {"elefant_foot_compensation_layers", steps({propagate(posSlice)})},
    {"z_offset", steps({propagate(psWipeTower), propagate(psSkirtBrim)})},
};


std::set<Step> handle_special_cases(
    const ConfigView& old_config,
    const ConfigView& new_config,
    const std::vector<std::string>& diff
)
{
    std::set<Step> result;
    for (const std::string& opt_key : diff) {
        if (opt_key == "support_material") {
            if (new_config.get<double>("support_material_contact_distance") == 0.) {
                // Enabling / disabling supports while soluble support interface is enabled.
                // This changes the bridging logic (bridging enabled without supports, disabled with supports).
                // Reset everything.
                // See GH #1482 for details.
                result.insert(posSlice);
            }
        } else if (opt_key == "over_bridge_speed") {
            const auto old_speed = old_config.get<std::vector<FloatOrPercentage>>(opt_key);
            const auto new_speed = new_config.get<std::vector<FloatOrPercentage>>(opt_key);

            const auto is_zero{[](auto v) { return v.is_zero(); }};

            if (std::ranges::any_of(old_speed, is_zero) || std::ranges::any_of(new_speed, is_zero))
            {
                result.insert(posPrepareInfill);
            }
        } else if (opt_key == "fill_density") {
            // One likely wants to reslice only when switching between zero infill to simulate boolean difference (subtracting volumes),
            // normal infill and 100% (solid) infill.
            const auto old_density = old_config.get<std::vector<Percentage>>(opt_key);
            const auto new_density = new_config.get<std::vector<Percentage>>(opt_key);
            // FIXME Vojtech is not quite sure about the 100% here, maybe it is not needed.
            if (std::ranges::any_of(
                    old_density,
                    [](Percentage v) { return is_approx(v.value, 0.); }
                )
                || std::ranges::any_of(
                    old_density,
                    [](Percentage v) { return is_approx(v.value, 100.0); }
                )
                || std::ranges::any_of(
                    new_density,
                    [](Percentage v) { return is_approx(v.value, 0.); }
                )
                || std::ranges::any_of(
                    new_density,
                    [](Percentage v) { return is_approx(v.value, 100.0); }
                ))
            {
                result.insert(posPerimeters);
            }
        } else if (opt_key == "bridge_flow_ratio") {
            if (new_config.get<double>("support_material_contact_distance") > 0.) {
                // Only invalidate due to bridging if bridging is enabled.
                // If later "support_material_contact_distance" is modified, the complete PrintObject is invalidated anyway.
                result.insert({posPerimeters, posInfill, posSupportMaterial});
            }
        }
    }

    std::set<Step> propagated;
    for (const Step& step : result) {
        const std::vector<Step> dependent_steps{propagate(step)};
        propagated.insert(dependent_steps.begin(), dependent_steps.end());
    }
    return propagated;
}

PrintAndObjectSteps diff_to_invalidated_steps(
    const ConfigView& old_config,
    const ConfigView& new_config,
    const std::vector<std::string>& diff
)
{
    std::set<Step> steps;
    for (const std::string& opt_key : diff) {
        const std::vector<Step>& invalidated_steps{invalidated_by.at(opt_key)};
        steps.insert(invalidated_steps.begin(), invalidated_steps.end());
    }
    steps.merge(handle_special_cases(old_config, new_config, diff));

    std::set<FDMPrintStep> print_steps;
    std::set<FDMPrintObjectStep> print_object_steps;
    for (const Step& step : steps) {
        std::visit(
            Domain::overloaded{
                [&](const FDMPrintStep& step) { print_steps.insert(step); },
                [&](const FDMPrintObjectStep& step) { print_object_steps.insert(step); }
            },
            step
        );
    }
    return PrintAndObjectSteps{print_steps, print_object_steps};
}

template <typename Set>
AllOrSome<Set> merge(const AllOrSome<Set>& a, const AllOrSome<Set>& b)
{
    if (std::holds_alternative<AllSteps>(a) || std::holds_alternative<AllSteps>(b)) {
        return AllSteps{};
    }

    Set values_a{std::get<Set>(a)};
    Set values_b{std::get<Set>(b)};
    values_a.merge(values_b);
    return values_a;
}

template AllOrSome<FDMPrintSteps>
merge(const AllOrSome<FDMPrintSteps>& a, const AllOrSome<FDMPrintSteps>& b);
template AllOrSome<FDMPrintObjectSteps>
merge(const AllOrSome<FDMPrintObjectSteps>& a, const AllOrSome<FDMPrintObjectSteps>& b);

StepsPerPrintObject merge(const StepsPerPrintObject& a, const StepsPerPrintObject& b)
{
    StepsPerPrintObject result{a};
    StepsPerPrintObject same_key_elements{b};

    result.merge(same_key_elements);

    for (const auto& [print_object, invalidated_steps] : same_key_elements) {
        result.at(print_object) = merge(result.at(print_object), invalidated_steps);
    }

    return result;
}

InvalidatedSteps merge(const InvalidatedSteps& a, const InvalidatedSteps& b)
{
    return {merge(a.print, b.print), merge(a.object, b.object)};
}

InvalidatedSteps merge(const std::vector<InvalidatedSteps>& invalidated_steps)
{
    InvalidatedSteps result;
    for (const InvalidatedSteps& steps : invalidated_steps) {
        result = merge(result, steps);
    }
    return result;
}

PrintAndObjectSteps merge(const PrintAndObjectSteps& a, const PrintAndObjectSteps& b)
{
    PrintAndObjectSteps result;
    result.first  = merge(a.first, b.first);
    result.second = merge(a.second, b.second);
    return result;
}

PrintAndObjectSteps get_invalidated_steps(const PrintRegion& current, const PrintRegion& next)
{
    if (current.source_virtual_extruder_id() != next.source_virtual_extruder_id()) {
        return {AllSteps{}, AllSteps{}};
    }

    const std::vector<std::string> diff{
        current.config().diff_keys(next.config())
    };

    return diff_to_invalidated_steps(current.config(), next.config(), diff);
}

bool is_all_steps(const PrintAndObjectSteps& steps)
{
    return std::holds_alternative<AllSteps>(steps.first)
        && std::holds_alternative<AllSteps>(steps.second);
}

PrintAndObjectSteps get_invalidated_steps(
    const std::vector<PrintObjectRegions::VolumeRegion>& current_regions,
    const std::vector<PrintObjectRegions::VolumeRegion>& next_regions
)
{
    if (current_regions.size() != next_regions.size()) {
        return {AllSteps{}, AllSteps{}};
    }

    PrintAndObjectSteps result;
    for (std::size_t i{}; i < current_regions.size(); ++i) {
        const PrintObjectRegions::VolumeRegion& current{current_regions[i]};
        const PrintObjectRegions::VolumeRegion& next{next_regions[i]};

        if (current.region == nullptr && next.region == nullptr) {
            continue;
        }

        if (current.region == nullptr || next.region == nullptr) {
            return {AllSteps{}, AllSteps{}};
        }

        const PrintAndObjectSteps invalidated_steps{
            get_invalidated_steps(*current.region, *next.region)
        };

        result = merge(result, invalidated_steps);

        if (is_all_steps(result)) {
            return result;
        }
    }

    return result;
}

PrintAndObjectSteps get_invalidated_steps(
    const std::vector<PrintObjectRegions::PaintedRegion>& current_regions,
    const std::vector<PrintObjectRegions::PaintedRegion>& next_regions
)
{
    if (current_regions.size() != next_regions.size()) {
        return {AllSteps{}, AllSteps{}};
    }

    PrintAndObjectSteps result;
    for (std::size_t i{}; i < current_regions.size(); ++i) {
        const PrintObjectRegions::PaintedRegion& current{current_regions[i]};
        const PrintObjectRegions::PaintedRegion& next{next_regions[i]};

        if (current.parent != next.parent || current.extruder_id != next.extruder_id) {
            return {AllSteps{}, AllSteps{}};
        }

        result = merge(result, get_invalidated_steps(*current.region, *next.region));
        if (is_all_steps(result)) {
            return result;
        }
    }

    return result;
}

PrintAndObjectSteps get_invalidated_steps(
    const std::vector<PrintObjectRegions::FuzzySkinPaintedRegion>& current_regions,
    const std::vector<PrintObjectRegions::FuzzySkinPaintedRegion>& next_regions
)
{
    if (current_regions.size() != next_regions.size()) {
        return {AllSteps{}, AllSteps{}};
    }

    PrintAndObjectSteps result;
    for (std::size_t i{}; i < current_regions.size(); ++i) {
        const PrintObjectRegions::FuzzySkinPaintedRegion& current{current_regions[i]};
        const PrintObjectRegions::FuzzySkinPaintedRegion& next{next_regions[i]};

        if (current.parent != next.parent || current.parent_type != next.parent_type) {
            return {AllSteps{}, AllSteps{}};
        }

        result = merge(result, get_invalidated_steps(*current.region, *next.region));
        if (is_all_steps(result)) {
            return result;
        }
    }

    return result;
}

PrintAndObjectSteps get_invalidated_steps(
    const PrintObjectRegions& current_regions,
    const PrintObjectRegions& next_regions
)
{
    using LayerRangeRegions = PrintObjectRegions::LayerRangeRegions;

    if (current_regions.layer_ranges.size() != next_regions.layer_ranges.size()) {
        return {AllSteps{}, AllSteps{}};
    }

    PrintAndObjectSteps result;
    for (std::size_t index{}; index < current_regions.layer_ranges.size(); ++index) {
        const LayerRangeRegions& current{current_regions.layer_ranges[index]};
        const LayerRangeRegions& next{next_regions.layer_ranges[index]};

        result = merge(result, get_invalidated_steps(current.volume_regions, next.volume_regions));
        if (is_all_steps(result)) {
            return result;
        }

        result =
            merge(result, get_invalidated_steps(current.painted_regions, next.painted_regions));
        if (is_all_steps(result)) {
            return result;
        }

        result = merge(
            result,
            get_invalidated_steps(
                current.fuzzy_skin_painted_regions,
                next.fuzzy_skin_painted_regions
            )
        );
        if (is_all_steps(result)) {
            return result;
        }
    }

    return result;
}
} // namespace Slic3r::SlicingSync
