# Orca import report: Anycubic

Status: **OK**

- Orca source version: 02.04.00.09
- printer models (families): 21
- Orca machine presets: 32
- printer configs (picker cards): 21
- printer presets (model x nozzle variants): 32
- print presets: 113
- filament presets: 504
-   of which OrcaFilamentLibrary: 330

## Orca-only settings carried as custom parameters

| Orca setting | 3.0 placeholder |
|---|---|
| `additional_cooling_fan_speed` | `custom_parameter_filament_additional_cooling_fan_speed` (custom_parameters_filament) |

## Not executable on the imported configs (kept as Orca wrote them)

Tool-change G-code only runs on a tool/filament change; the imported configs have one material slot and no feeder, so 3.0 never executes it.

- `flush_length` in 5 preset(s), e.g. Anycubic Kobra S1 Max 0.25 nozzle: toolchange_gcode
- `x_after_toolchange` in 1 preset(s), e.g. Anycubic Kobra X 0.4 nozzle: toolchange_gcode
- `y_after_toolchange` in 1 preset(s), e.g. Anycubic Kobra X 0.4 nozzle: toolchange_gcode

## Fallbacks that change the G-code

- layer_gcode: appended `G92 E0` (3.0 MissingG92E0 rule; harmless with relative E): 3 config(s): Anycubic Kobra Max 0.4 nozzle, Anycubic Kobra Plus 0.4 nozzle, Anycubic Vyper 0.4 nozzle

## Placeholder translations applied

| Orca | 3.0 | uses |
|---|---|---|
| `additional_cooling_fan_speed` | `custom_parameter_filament_additional_cooling_fan_speed` | 4 |
| `bed_temperature_initial_layer` | `first_layer_bed_temperature` | 48 |
| `bed_temperature_initial_layer_single` | `first_layer_bed_temperature[initial_extruder]` | 13 |
| `during_print_exhaust_fan_speed_num[i]` | `int(during_print_exhaust_fan_speed[i] / 100.0 * 255)` | 17 |
| `machine_max_speed_x` | `machine_max_feedrate_x` | 1 |
| `machine_max_speed_z` | `machine_max_feedrate_z` | 2 |
| `nozzle_temperature_initial_layer` | `first_layer_temperature` | 15 |
| `printable_height` | `max_print_height` | 6 |
| `retraction_length` | `retract_length` | 4 |
| `retraction_speed` | `retract_speed` | 4 |

## Notes

- process `initial_layer_infill_speed` = '35%': Orca's option type is coFloats, which Orca reads as 35; imported as that number
- process `initial_layer_infill_speed` = '50%': Orca's option type is coFloats, which Orca reads as 50; imported as that number

## Keys Orca's own loader drops (PrintConfigDef::handle_legacy)

| Orca key | presets | reason |
|---|---|---|
| `initial_layer_speed` | 21 | old-profile percentage; Orca's handle_legacy erases it (parent value kept) |
| `support_interface_speed` | 36 | old-profile percentage; Orca's handle_legacy erases it (parent value kept) |
| `support_object_xy_distance` | 36 | old-profile percentage; Orca's handle_legacy erases it (parent value kept) |

## Dropped / unmapped Orca settings

Count = number of converted presets carrying the key.

| kind | Orca key | presets | reason |
|---|---|---|---|
| filament | `activate_chamber_temp_control` | 504 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance` | 504 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance_bridges` | 504 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance_model` | 504 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance_overhangs` | 504 | no 3.0 equivalent |
| filament | `additional_cooling_fan_speed` | 504 | no 3.0 equivalent |
| filament | `additional_fan_full_speed_layer` | 504 | no 3.0 equivalent |
| filament | `bed_type` | 174 | no 3.0 equivalent |
| filament | `close_additional_fan_first_x_layers` | 504 | no 3.0 equivalent |
| filament | `disable_fan_first_layers` | 13 | no 3.0 equivalent |
| filament | `dont_slow_down_outer_wall` | 504 | no 3.0 equivalent |
| filament | `enable_overhang_bridge_fan` | 504 | no 3.0 equivalent |
| filament | `external_perimeter_speed` | 1 | no 3.0 equivalent |
| filament | `fan_cooling_layer_time_BRASS` | 80 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `fan_cooling_layer_time_HS` | 79 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `fan_max_speed_BRASS` | 84 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `fan_max_speed_HS` | 82 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `fan_min_speed_BRASS` | 85 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `fan_min_speed_HS` | 83 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `fan_p2_after_x_layers` | 3 | no 3.0 equivalent |
| filament | `fan_p2_before_x_layers` | 3 | no 3.0 equivalent |
| filament | `fan_p2_speed_before_x_layers` | 10 | no 3.0 equivalent |
| filament | `fan_speed_after_x_layers` | 4 | no 3.0 equivalent |
| filament | `filament_adaptive_volumetric_speed` | 504 | no 3.0 equivalent |
| filament | `filament_adhesiveness_category` | 504 | no 3.0 equivalent |
| filament | `filament_change_length` | 504 | no 3.0 equivalent |
| filament | `filament_change_length_nc` | 504 | no 3.0 equivalent |
| filament | `filament_color` | 18 | no 3.0 equivalent |
| filament | `filament_cooling_before_tower` | 504 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_ams_limitations` | 504 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_heat_distortion_temperature` | 504 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_temperature` | 504 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_time` | 504 | no 3.0 equivalent |
| filament | `filament_dev_chamber_drying_bed_temperature` | 504 | no 3.0 equivalent |
| filament | `filament_dev_chamber_drying_time` | 504 | no 3.0 equivalent |
| filament | `filament_dev_drying_cooling_temperature` | 504 | no 3.0 equivalent |
| filament | `filament_dev_drying_softening_temperature` | 504 | no 3.0 equivalent |
| filament | `filament_extruder_compatibility` | 504 | no 3.0 equivalent |
| filament | `filament_extruder_variant` | 504 | no 3.0 equivalent |
| filament | `filament_flush_temp` | 504 | no 3.0 equivalent |
| filament | `filament_flush_temp_fast` | 504 | no 3.0 equivalent |
| filament | `filament_flush_volumetric_speed` | 504 | no 3.0 equivalent |
| filament | `filament_ids` | 504 | no 3.0 equivalent |
| filament | `filament_ironing_flow` | 504 | no 3.0 equivalent |
| filament | `filament_ironing_inset` | 504 | no 3.0 equivalent |
| filament | `filament_ironing_spacing` | 504 | no 3.0 equivalent |
| filament | `filament_ironing_speed` | 504 | no 3.0 equivalent |
| filament | `filament_is_support` | 504 | no 3.0 equivalent |
| filament | `filament_long_retractions_when_cut` | 488 | no 3.0 equivalent |
| filament | `filament_pre_cooling_temperature` | 504 | no 3.0 equivalent |
| filament | `filament_pre_cooling_temperature_nc` | 504 | no 3.0 equivalent |
| filament | `filament_preheat_temperature_delta` | 504 | no 3.0 equivalent |
| filament | `filament_prime_volume` | 504 | no 3.0 equivalent |
| filament | `filament_prime_volume_nc` | 504 | no 3.0 equivalent |
| filament | `filament_printable` | 504 | no 3.0 equivalent |
| filament | `filament_ramming_travel_time` | 504 | no 3.0 equivalent |
| filament | `filament_ramming_travel_time_nc` | 504 | no 3.0 equivalent |
| filament | `filament_ramming_volumetric_speed` | 504 | no 3.0 equivalent |
| filament | `filament_ramming_volumetric_speed_nc` | 504 | no 3.0 equivalent |
| filament | `filament_retract_layer_change` | 13 | no 3.0 equivalent |
| filament | `filament_retract_length_nc` | 504 | no 3.0 equivalent |
| filament | `filament_retract_lift_enforce` | 162 | 3.0 has no top/bottom-only z-hop setting |
| filament | `filament_retraction_distances_when_cut` | 488 | no 3.0 equivalent |
| filament | `filament_scarf_gap` | 330 | no 3.0 equivalent |
| filament | `filament_scarf_height` | 330 | no 3.0 equivalent |
| filament | `filament_scarf_length` | 330 | no 3.0 equivalent |
| filament | `filament_scarf_seam_type` | 330 | no 3.0 equivalent |
| filament | `filament_spool_weight` | 18 | no 3.0 equivalent |
| filament | `filament_tower_interface_pre_extrusion_dist` | 504 | no 3.0 equivalent |
| filament | `filament_tower_interface_pre_extrusion_length` | 504 | no 3.0 equivalent |
| filament | `filament_tower_interface_print_temp` | 504 | no 3.0 equivalent |
| filament | `filament_tower_interface_purge_volume` | 504 | no 3.0 equivalent |
| filament | `filament_tower_ironing_area` | 504 | no 3.0 equivalent |
| filament | `filament_wipe_distance` | 504 | 3.0 has no wipe distance |
| filament | `filament_z_hop_types` | 6 | 'Auto Lift' has no 3.0 equivalent; printed as a normal vertical lift |
| filament | `filament_z_hop_types` | 1 | 'Spiral Lift' has no 3.0 equivalent; printed as a normal vertical lift |
| filament | `first_x_layer_fan_speed` | 504 | no 3.0 equivalent |
| filament | `impact_strength_z` | 1 | no 3.0 equivalent |
| filament | `inherits` | 504 | no 3.0 equivalent |
| filament | `initial_layer_fan_speed` | 504 | no 3.0 equivalent |
| filament | `internal_bridge_fan_speed` | 504 | no 3.0 equivalent |
| filament | `ironing_fan_speed` | 504 | no 3.0 equivalent |
| filament | `long_retractions_when_ec` | 504 | no 3.0 equivalent |
| filament | `nozzle_temperature_BRASS` | 83 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `nozzle_temperature_HS` | 83 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `nozzle_temperature_initial_layer_BRASS` | 80 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `nozzle_temperature_initial_layer_HS` | 81 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `nozzle_temperature_range_high` | 504 | no 3.0 equivalent |
| filament | `nozzle_temperature_range_low` | 504 | no 3.0 equivalent |
| filament | `overhang_fan_speed` | 504 | Orca overhang fan has no 1:1 3.0 key (3.0 uses per-overlap overhang_fan_speed_N) |
| filament | `overhang_fan_threshold` | 504 | no 3.0 equivalent |
| filament | `overhang_threshold_participating_cooling` | 1 | no 3.0 equivalent |
| filament | `pellet_flow_coefficient` | 504 | no 3.0 equivalent |
| filament | `pre_start_fan_time` | 1 | no 3.0 equivalent |
| filament | `required_nozzle_HRC` | 504 | no 3.0 equivalent |
| filament | `retraction_distances_when_ec` | 504 | no 3.0 equivalent |
| filament | `slow_down_layer_time_BRASS` | 79 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `slow_down_layer_time_HS` | 75 | per-hotend variant key with no reader in Orca 2.5 source; Orca ignores it |
| filament | `support_material_interface_fan_speed` | 504 | no 3.0 equivalent |
| filament | `temperature_vitrification` | 504 | no 3.0 equivalent |
| filament | `volumetric_speed_coefficients` | 504 | no 3.0 equivalent |
| machine | `adaptive_bed_mesh_margin` | 32 | no 3.0 equivalent |
| machine | `auxiliary_fan` | 32 | no 3.0 equivalent |
| machine | `bbl_use_printhost` | 32 | physical printer setting, not a preset |
| machine | `bed_exclude_area` | 32 | no 3.0 equivalent |
| machine | `bed_mesh_max` | 22 | no 3.0 equivalent |
| machine | `bed_mesh_min` | 22 | no 3.0 equivalent |
| machine | `bed_mesh_probe_distance` | 22 | no 3.0 equivalent |
| machine | `bed_temperature_formula` | 32 | no 3.0 equivalent |
| machine | `belt_frame_tilt_angle` | 32 | no 3.0 equivalent |
| machine | `belt_frame_tilt_decouple` | 32 | no 3.0 equivalent |
| machine | `belt_printer` | 32 | no 3.0 equivalent |
| machine | `belt_printer_infinite_y` | 32 | no 3.0 equivalent |
| machine | `belt_slice_rotation` | 32 | no 3.0 equivalent |
| machine | `belt_slice_rotation_angle` | 32 | no 3.0 equivalent |
| machine | `belt_support_floor_offset` | 32 | no 3.0 equivalent |
| machine | `best_object_pos` | 22 | no 3.0 equivalent |
| machine | `build_plate_tilt_x` | 32 | no 3.0 equivalent |
| machine | `build_plate_tilt_y` | 32 | no 3.0 equivalent |
| machine | `cooling_filter_enabled` | 32 | no 3.0 equivalent |
| machine | `default_nozzle_volume_type` | 32 | no 3.0 equivalent |
| machine | `deretract_speed_extruder_change` | 32 | no 3.0 equivalent |
| machine | `emit_machine_limits_to_gcode` | 20 | 3.0 cannot emit machine limits for Klipper flavor; used for time estimate only |
| machine | `enable_belt_purge_tower` | 32 | no 3.0 equivalent |
| machine | `enable_long_retraction_when_cut` | 32 | no 3.0 equivalent |
| machine | `enable_power_loss_recovery` | 32 | no 3.0 equivalent |
| machine | `enable_pre_heating` | 32 | no 3.0 equivalent |
| machine | `extruder_clearance_dist_to_rod` | 32 | no 3.0 equivalent |
| machine | `extruder_clearance_height_to_lid` | 32 | 3.0 has a single clearance height (rod height used) |
| machine | `extruder_colour` | 32 | no 3.0 equivalent |
| machine | `extruder_max_nozzle_count` | 32 | no 3.0 equivalent |
| machine | `extruder_printable_height` | 32 | no 3.0 equivalent |
| machine | `extruder_type` | 32 | no 3.0 equivalent |
| machine | `extruder_variant_list` | 32 | no 3.0 equivalent |
| machine | `fan_direction` | 32 | no 3.0 equivalent |
| machine | `fan_kickstart` | 32 | no 3.0 equivalent |
| machine | `fan_speedup_overhangs` | 32 | no 3.0 equivalent |
| machine | `fan_speedup_time` | 32 | no 3.0 equivalent |
| machine | `farthest_point_timelapse` | 32 | no 3.0 equivalent |
| machine | `file_start_gcode` | 32 | no 3.0 equivalent |
| machine | `flashforge_serial_number` | 32 | no 3.0 equivalent |
| machine | `gcode_skip_config_block` | 32 | no 3.0 equivalent |
| machine | `grab_length` | 32 | no 3.0 equivalent |
| machine | `group_algo_with_time` | 32 | no 3.0 equivalent |
| machine | `head_wrap_detect_zone` | 32 | no 3.0 equivalent |
| machine | `host_type` | 32 | physical printer setting, not a preset |
| machine | `hotend_cooling_rate` | 32 | no 3.0 equivalent |
| machine | `hotend_heating_rate` | 32 | no 3.0 equivalent |
| machine | `imex_carriage_margin` | 32 | no 3.0 equivalent |
| machine | `imex_firmware_managed_zones` | 32 | no 3.0 equivalent |
| machine | `imex_gantry_count` | 32 | no 3.0 equivalent |
| machine | `imex_mode_active_tools` | 32 | no 3.0 equivalent |
| machine | `imex_mode_gcodes` | 32 | no 3.0 equivalent |
| machine | `imex_mode_names` | 32 | no 3.0 equivalent |
| machine | `imex_nozzle_clearance_x` | 32 | no 3.0 equivalent |
| machine | `imex_nozzle_clearance_y` | 32 | no 3.0 equivalent |
| machine | `imex_tool_layout` | 32 | no 3.0 equivalent |
| machine | `imex_tools_per_gantry` | 32 | no 3.0 equivalent |
| machine | `imex_viz_theme` | 32 | no 3.0 equivalent |
| machine | `inherits` | 32 | no 3.0 equivalent |
| machine | `input_shaping_damp_x` | 32 | no 3.0 equivalent |
| machine | `input_shaping_damp_y` | 32 | no 3.0 equivalent |
| machine | `input_shaping_emit` | 32 | no 3.0 equivalent |
| machine | `input_shaping_freq_x` | 32 | no 3.0 equivalent |
| machine | `input_shaping_freq_y` | 32 | no 3.0 equivalent |
| machine | `input_shaping_type` | 32 | no 3.0 equivalent |
| machine | `is_imex` | 32 | no 3.0 equivalent |
| machine | `long_retractions_when_cut` | 32 | no 3.0 equivalent |
| machine | `machine_bed_mass_Y` | 32 | no 3.0 equivalent |
| machine | `machine_hotend_change_time` | 32 | no 3.0 equivalent |
| machine | `machine_load_filament_time` | 32 | 3.0 filament_change_time is a different quantity |
| machine | `machine_max_force_Y` | 32 | no 3.0 equivalent |
| machine | `machine_max_printed_mass` | 32 | no 3.0 equivalent |
| machine | `machine_prepare_compensation_time` | 32 | no 3.0 equivalent |
| machine | `machine_tool_change_time` | 32 | no 3.0 equivalent |
| machine | `machine_unload_filament_time` | 32 | 3.0 filament_change_time is a different quantity |
| machine | `manual_filament_change` | 32 | no 3.0 equivalent |
| machine | `master_extruder_id` | 32 | no 3.0 equivalent |
| machine | `max_resonance_avoidance_speed` | 32 | no 3.0 equivalent |
| machine | `min_resonance_avoidance_speed` | 32 | no 3.0 equivalent |
| machine | `nozzle_flush_dataset` | 32 | no 3.0 equivalent |
| machine | `nozzle_height` | 32 | no 3.0 equivalent |
| machine | `nozzle_hrc` | 32 | no 3.0 equivalent |
| machine | `nozzle_type` | 32 | no 3.0 equivalent |
| machine | `nozzle_volume` | 32 | no 3.0 equivalent |
| machine | `parallel_printheads_bed_exclude_areas` | 32 | no 3.0 equivalent |
| machine | `parallel_printheads_count` | 32 | no 3.0 equivalent |
| machine | `part_cooling_fan_min_pwm` | 32 | no 3.0 equivalent |
| machine | `pellet_modded_printer` | 32 | no 3.0 equivalent |
| machine | `physical_extruder_map` | 32 | no 3.0 equivalent |
| machine | `preferred_orientation` | 32 | no 3.0 equivalent |
| machine | `print_host` | 32 | no 3.0 equivalent |
| machine | `print_host_webui` | 32 | no 3.0 equivalent |
| machine | `printer_agent` | 32 | no 3.0 equivalent |
| machine | `printer_extruder_id` | 32 | no 3.0 equivalent |
| machine | `printer_extruder_variant` | 32 | no 3.0 equivalent |
| machine | `printer_flush_multiplier` | 9 | no 3.0 equivalent |
| machine | `printer_structure` | 32 | informational in Orca |
| machine | `printhost_apikey` | 32 | no 3.0 equivalent |
| machine | `printhost_authorization_type` | 32 | physical printer setting, not a preset |
| machine | `printhost_cafile` | 32 | no 3.0 equivalent |
| machine | `printhost_password` | 32 | no 3.0 equivalent |
| machine | `printhost_port` | 32 | no 3.0 equivalent |
| machine | `printhost_ssl_ignore_revoke` | 32 | physical printer setting, not a preset |
| machine | `printhost_user` | 32 | no 3.0 equivalent |
| machine | `resonance_avoidance` | 32 | no 3.0 equivalent |
| machine | `retract_after_wipe` | 32 | no 3.0 equivalent |
| machine | `retract_lift_enforce` | 32 | 3.0 has no top/bottom-only z-hop setting |
| machine | `retract_on_top_layer` | 11 | no 3.0 equivalent |
| machine | `retraction_distances_when_cut` | 32 | no 3.0 equivalent |
| machine | `scan_first_layer` | 32 | no 3.0 equivalent |
| machine | `silent_mode` | 32 | no 3.0 equivalent |
| machine | `support_chamber_temp_control` | 32 | no 3.0 equivalent |
| machine | `support_cooling_filter` | 32 | no 3.0 equivalent |
| machine | `support_fast_purge_mode` | 32 | no 3.0 equivalent |
| machine | `support_object_skip_flush` | 32 | no 3.0 equivalent |
| machine | `support_parallel_printheads` | 32 | no 3.0 equivalent |
| machine | `thumbnails_internal` | 13 | no 3.0 equivalent |
| machine | `thumbnails_internal_switch` | 13 | no 3.0 equivalent |
| machine | `time_cost` | 32 | no 3.0 equivalent |
| machine | `time_lapse_gcode` | 32 | no 3.0 equivalent |
| machine | `tool_change_on_wipe_tower` | 32 | no 3.0 equivalent |
| machine | `travel_slope` | 32 | no 3.0 equivalent |
| machine | `travel_slope` | 22 | used only with Orca 'Slope Lift' |
| machine | `upward_compatible_machine` | 32 | no 3.0 equivalent |
| machine | `use_3mf` | 32 | no 3.0 equivalent |
| machine | `wait_for_temp_on_wipe_tower` | 32 | no 3.0 equivalent |
| machine | `wipe_distance` | 32 | 3.0 has no wipe distance (wipe length follows retract_before_wipe) |
| machine | `wipe_tower_type` | 32 | no 3.0 equivalent |
| machine | `wrapping_detection_gcode` | 32 | no 3.0 equivalent |
| machine | `wrapping_detection_layers` | 32 | no 3.0 equivalent |
| machine | `wrapping_exclude_area` | 32 | no 3.0 equivalent |
| machine | `z_hop_types` | 12 | 'Auto Lift' has no 3.0 equivalent; printed as a normal vertical lift |
| process | `accel_to_decel_enable` | 113 | no 3.0 equivalent |
| process | `accel_to_decel_factor` | 113 | no 3.0 equivalent |
| process | `align_infill_direction_to_model` | 113 | no 3.0 equivalent |
| process | `alternate_extra_wall` | 113 | no 3.0 equivalent |
| process | `belt_purge_tower_object` | 113 | no 3.0 equivalent |
| process | `belt_purge_tower_width` | 113 | no 3.0 equivalent |
| process | `bottom_layer_direction` | 113 | no 3.0 equivalent |
| process | `bottom_solid_infill_flow_ratio` | 113 | no 3.0 equivalent |
| process | `bottom_surface_density` | 113 | no 3.0 equivalent |
| process | `bottom_surface_filament_id` | 113 | no 3.0 equivalent |
| process | `bottom_surface_fill_order` | 113 | no 3.0 equivalent |
| process | `bridge_density` | 113 | no 3.0 equivalent |
| process | `bridge_line_width` | 113 | no 3.0 equivalent |
| process | `brim_ears_detection_length` | 113 | no 3.0 equivalent |
| process | `brim_ears_max_angle` | 113 | no 3.0 equivalent |
| process | `brim_ears_outer_only` | 113 | no 3.0 equivalent |
| process | `brim_flow_ratio` | 113 | no 3.0 equivalent |
| process | `brim_type` | 92 | Orca value 'auto_brim' has no 3.0 equivalent |
| process | `brim_use_efc_outline` | 113 | no 3.0 equivalent |
| process | `calib_flowrate_topinfill_special_order` | 113 | no 3.0 equivalent |
| process | `center_of_surface_pattern` | 113 | no 3.0 equivalent |
| process | `combine_brims` | 113 | no 3.0 equivalent |
| process | `counterbore_hole_bridging` | 113 | no 3.0 equivalent |
| process | `default_jerk` | 113 | 3.0 has no jerk settings |
| process | `default_junction_deviation` | 113 | no 3.0 equivalent |
| process | `detect_narrow_internal_solid_infill` | 113 | no 3.0 equivalent |
| process | `detect_overhang_wall` | 113 | no 3.0 equivalent |
| process | `dont_filter_internal_bridges` | 113 | no 3.0 equivalent |
| process | `elefant_foot_layers_density` | 113 | no 3.0 equivalent |
| process | `enable_extra_bridge_layer` | 113 | no 3.0 equivalent |
| process | `enable_mixed_color_sublayer` | 113 | no 3.0 equivalent |
| process | `enable_tower_interface_cooldown_during_tower` | 113 | no 3.0 equivalent |
| process | `enable_tower_interface_features` | 113 | no 3.0 equivalent |
| process | `enable_wrapping_detection` | 113 | no 3.0 equivalent |
| process | `enforce_support_layers` | 113 | no 3.0 equivalent |
| process | `ensure_vertical_shell_thickness` | 3 | Orca value 'ensure_moderate' has no 3.0 equivalent |
| process | `extra_brim_width` | 113 | no 3.0 equivalent |
| process | `extra_solid_infills` | 113 | no 3.0 equivalent |
| process | `extrusion_rate_smoothing_external_perimeter_only` | 113 | no 3.0 equivalent |
| process | `filename_format` | 48 | uses plate_number, unknown to 3.0; default file name used |
| process | `fill_multiline` | 113 | no 3.0 equivalent |
| process | `filter_out_gap_fill` | 113 | no 3.0 equivalent |
| process | `flush_into_infill` | 113 | no 3.0 equivalent |
| process | `flush_into_objects` | 113 | no 3.0 equivalent |
| process | `flush_into_support` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin` | 26 | Orca value 'disabled_fuzzy' has no 3.0 equivalent |
| process | `fuzzy_skin_first_layer` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_layers_between_ripple_offset` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_mode` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_noise_type` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_octaves` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_persistence` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_ripple_offset` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_ripples_per_layer` | 113 | no 3.0 equivalent |
| process | `fuzzy_skin_scale` | 113 | no 3.0 equivalent |
| process | `gcode_add_line_number` | 113 | no 3.0 equivalent |
| process | `gyroid_optimized` | 113 | no 3.0 equivalent |
| process | `hole_to_polyhole` | 113 | no 3.0 equivalent |
| process | `hole_to_polyhole_max_edges` | 113 | no 3.0 equivalent |
| process | `hole_to_polyhole_threshold` | 113 | no 3.0 equivalent |
| process | `hole_to_polyhole_twisted` | 113 | no 3.0 equivalent |
| process | `imex_head_filament_map` | 113 | no 3.0 equivalent |
| process | `imex_parallel_mode` | 113 | no 3.0 equivalent |
| process | `independent_support_layer_height` | 113 | no 3.0 equivalent |
| process | `infill_complete_top` | 113 | no 3.0 equivalent |
| process | `infill_jerk` | 113 | 3.0 has no jerk settings |
| process | `infill_lock_depth` | 113 | no 3.0 equivalent |
| process | `infill_overhang_angle` | 113 | no 3.0 equivalent |
| process | `infill_shift_step` | 113 | no 3.0 equivalent |
| process | `inherits` | 113 | no 3.0 equivalent |
| process | `initial_layer_jerk` | 113 | 3.0 has no jerk settings |
| process | `initial_layer_min_bead_width` | 113 | no 3.0 equivalent |
| process | `initial_layer_travel_acceleration` | 113 | no 3.0 equivalent |
| process | `initial_layer_travel_jerk` | 113 | no 3.0 equivalent |
| process | `inner_wall_filament_id` | 113 | 0 = default filament (3.0 keeps its default) |
| process | `inner_wall_jerk` | 113 | 3.0 has no jerk settings |
| process | `internal_bridge_angle` | 113 | no 3.0 equivalent |
| process | `internal_bridge_density` | 113 | no 3.0 equivalent |
| process | `internal_bridge_flow` | 113 | no 3.0 equivalent |
| process | `internal_bridge_speed` | 113 | no 3.0 equivalent |
| process | `internal_solid_filament_id` | 113 | 0 = default filament (3.0 keeps its default) |
| process | `internal_solid_infill_pattern` | 113 | no 3.0 equivalent |
| process | `ironing_angle` | 113 | no 3.0 equivalent |
| process | `ironing_angle_fixed` | 113 | no 3.0 equivalent |
| process | `ironing_expansion` | 113 | no 3.0 equivalent |
| process | `ironing_inset` | 113 | no 3.0 equivalent |
| process | `ironing_pattern` | 113 | no 3.0 equivalent |
| process | `lateral_lattice_angle_1` | 113 | no 3.0 equivalent |
| process | `lateral_lattice_angle_2` | 113 | no 3.0 equivalent |
| process | `lattice_angle_1` | 42 | no 3.0 equivalent |
| process | `lattice_angle_2` | 42 | no 3.0 equivalent |
| process | `leading_brim_length` | 113 | no 3.0 equivalent |
| process | `lightning_overhang_angle` | 113 | no 3.0 equivalent |
| process | `lightning_prune_angle` | 113 | no 3.0 equivalent |
| process | `lightning_straightening_angle` | 113 | no 3.0 equivalent |
| process | `make_overhang_printable` | 113 | no 3.0 equivalent |
| process | `make_overhang_printable_angle` | 113 | no 3.0 equivalent |
| process | `make_overhang_printable_hole_size` | 113 | no 3.0 equivalent |
| process | `max_bridge_length` | 113 | no 3.0 equivalent |
| process | `max_volumetric_extrusion_rate_slope_segment_length` | 113 | no 3.0 equivalent |
| process | `min_length_factor` | 113 | no 3.0 equivalent |
| process | `min_width_top_surface` | 113 | no 3.0 equivalent |
| process | `outer_wall_filament_id` | 113 | no 3.0 equivalent |
| process | `outer_wall_jerk` | 113 | 3.0 has no jerk settings |
| process | `overhang_reverse` | 113 | no 3.0 equivalent |
| process | `overhang_reverse_internal_only` | 113 | no 3.0 equivalent |
| process | `overhang_reverse_threshold` | 113 | no 3.0 equivalent |
| process | `plugins` | 113 | no 3.0 equivalent |
| process | `precise_outer_wall` | 113 | no 3.0 equivalent |
| process | `precise_z_height` | 113 | no 3.0 equivalent |
| process | `preheat_steps` | 113 | no 3.0 equivalent |
| process | `preheat_time` | 113 | no 3.0 equivalent |
| process | `prime_tower_enable_framework` | 113 | no 3.0 equivalent |
| process | `prime_tower_flat_ironing` | 113 | no 3.0 equivalent |
| process | `prime_tower_infill_gap` | 113 | no 3.0 equivalent |
| process | `prime_tower_max_speed` | 15 | no 3.0 equivalent |
| process | `prime_tower_skip_points` | 113 | no 3.0 equivalent |
| process | `print_extruder_id` | 113 | no 3.0 equivalent |
| process | `print_extruder_variant` | 113 | no 3.0 equivalent |
| process | `print_order` | 113 | no 3.0 equivalent |
| process | `relative_bridge_angle` | 113 | no 3.0 equivalent |
| process | `scarf_angle_threshold` | 113 | no 3.0 equivalent |
| process | `scarf_joint_flow_ratio` | 113 | no 3.0 equivalent |
| process | `scarf_joint_speed` | 113 | no 3.0 equivalent |
| process | `scarf_overhang_threshold` | 113 | no 3.0 equivalent |
| process | `seam_slope_conditional` | 113 | no 3.0 equivalent |
| process | `seam_slope_steps` | 113 | no 3.0 equivalent |
| process | `separated_infills` | 113 | no 3.0 equivalent |
| process | `single_loop_draft_shield` | 113 | no 3.0 equivalent |
| process | `skeleton_infill_density` | 113 | no 3.0 equivalent |
| process | `skeleton_infill_line_width` | 113 | no 3.0 equivalent |
| process | `skin_infill_density` | 113 | no 3.0 equivalent |
| process | `skin_infill_depth` | 113 | no 3.0 equivalent |
| process | `skin_infill_line_width` | 113 | no 3.0 equivalent |
| process | `skirt_start_angle` | 113 | no 3.0 equivalent |
| process | `skirt_type` | 113 | no 3.0 equivalent |
| process | `slicing_pipeline_plugin` | 113 | no 3.0 equivalent |
| process | `slowdown_for_curled_perimeters` | 113 | no 3.0 equivalent |
| process | `small_area_infill_flow_compensation` | 113 | no 3.0 equivalent |
| process | `small_area_infill_flow_compensation_model` | 113 | no 3.0 equivalent |
| process | `small_support_perimeter_speed` | 113 | no 3.0 equivalent |
| process | `small_support_perimeter_threshold` | 113 | no 3.0 equivalent |
| process | `smooth_speed_discontinuity_area` | 48 | no 3.0 equivalent |
| process | `solid_infill_direction` | 113 | no 3.0 equivalent |
| process | `solid_infill_rotate_template` | 113 | no 3.0 equivalent |
| process | `sparse_infill_filament_id` | 113 | 0 = default filament (3.0 keeps its default) |
| process | `sparse_infill_pattern` | 49 | Orca value 'crosshatch' has no 3.0 equivalent |
| process | `sparse_infill_rotate_template` | 113 | no 3.0 equivalent |
| process | `sparse_infill_smooth_factor` | 113 | no 3.0 equivalent |
| process | `spiral_finishing_flow_ratio` | 113 | no 3.0 equivalent |
| process | `spiral_mode_max_xy_smoothing` | 113 | no 3.0 equivalent |
| process | `spiral_mode_smooth` | 113 | no 3.0 equivalent |
| process | `spiral_starting_flow_ratio` | 113 | no 3.0 equivalent |
| process | `support_base_pattern` | 65 | Orca pattern 'default' (tree support: hollow) has no 3.0 equivalent |
| process | `support_bottom_interface_spacing` | 113 | no 3.0 equivalent |
| process | `support_critical_regions_only` | 113 | no 3.0 equivalent |
| process | `support_expansion` | 113 | no 3.0 equivalent |
| process | `support_interface_loop_pattern` | 113 | no 3.0 equivalent |
| process | `support_interface_not_for_body` | 113 | no 3.0 equivalent |
| process | `support_ironing` | 113 | no 3.0 equivalent |
| process | `support_ironing_flow` | 113 | no 3.0 equivalent |
| process | `support_ironing_pattern` | 113 | no 3.0 equivalent |
| process | `support_ironing_spacing` | 113 | no 3.0 equivalent |
| process | `support_object_first_layer_gap` | 113 | no 3.0 equivalent |
| process | `support_remove_small_overhang` | 113 | no 3.0 equivalent |
| process | `support_threshold_overlap` | 113 | no 3.0 equivalent |
| process | `symmetric_infill_y_axis` | 113 | no 3.0 equivalent |
| process | `thick_internal_bridges` | 113 | no 3.0 equivalent |
| process | `timelapse_type` | 113 | no 3.0 equivalent |
| process | `toolchange_cyclic_first_layer` | 113 | no 3.0 equivalent |
| process | `toolchange_cyclic_order` | 113 | no 3.0 equivalent |
| process | `toolchange_ordering` | 113 | no 3.0 equivalent |
| process | `top_bottom_infill_wall_overlap` | 113 | no 3.0 equivalent |
| process | `top_layer_direction` | 113 | no 3.0 equivalent |
| process | `top_surface_density` | 113 | no 3.0 equivalent |
| process | `top_surface_expansion` | 113 | no 3.0 equivalent |
| process | `top_surface_expansion_direction` | 113 | no 3.0 equivalent |
| process | `top_surface_expansion_margin` | 113 | no 3.0 equivalent |
| process | `top_surface_filament_id` | 113 | no 3.0 equivalent |
| process | `top_surface_fill_order` | 113 | no 3.0 equivalent |
| process | `top_surface_jerk` | 113 | 3.0 has no jerk settings |
| process | `tpms_adaptive` | 113 | no 3.0 equivalent |
| process | `tpms_adaptive_gradient` | 113 | no 3.0 equivalent |
| process | `tpms_interior_density` | 113 | no 3.0 equivalent |
| process | `travel_jerk` | 113 | 3.0 has no jerk settings |
| process | `tree_support_adaptive_layer_height` | 87 | no 3.0 equivalent |
| process | `tree_support_auto_brim` | 113 | no 3.0 equivalent |
| process | `tree_support_branch_angle` | 113 | no 3.0 equivalent |
| process | `tree_support_branch_diameter` | 113 | no 3.0 equivalent |
| process | `tree_support_branch_distance` | 113 | no 3.0 equivalent |
| process | `tree_support_brim_width` | 113 | no 3.0 equivalent |
| process | `tree_support_wall_count` | 113 | no 3.0 equivalent |
| process | `unsupported_wall_last` | 113 | no 3.0 equivalent |
| process | `wall_direction` | 113 | no 3.0 equivalent |
| process | `wall_maximum_deviation` | 113 | no 3.0 equivalent |
| process | `wall_maximum_resolution` | 113 | no 3.0 equivalent |
| process | `wipe_before_external_loop` | 113 | no 3.0 equivalent |
| process | `wipe_inward` | 113 | no 3.0 equivalent |
| process | `wipe_inward_distance` | 113 | no 3.0 equivalent |
| process | `wipe_on_loops` | 113 | no 3.0 equivalent |
| process | `wipe_tower_extra_rib_length` | 113 | no 3.0 equivalent |
| process | `wipe_tower_fillet_wall` | 113 | no 3.0 equivalent |
| process | `wipe_tower_max_purge_speed` | 113 | no 3.0 equivalent |
| process | `wipe_tower_rib_width` | 113 | no 3.0 equivalent |
| process | `wipe_tower_rotation_angle` | 113 | no 3.0 equivalent |
| process | `wipe_tower_sparse_layers_combination` | 113 | no 3.0 equivalent |
| process | `wipe_tower_wall_type` | 113 | no 3.0 equivalent |
| process | `wiping_volumes_extruders` | 113 | no 3.0 equivalent |
| process | `zaa_dont_alternate_fill_direction` | 113 | no 3.0 equivalent |
| process | `zaa_enabled` | 113 | no 3.0 equivalent |
| process | `zaa_min_z` | 113 | no 3.0 equivalent |
| process | `zaa_minimize_perimeter_height` | 113 | no 3.0 equivalent |
