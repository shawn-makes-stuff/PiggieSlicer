# Orca import report: Elegoo

Status: **OK**

- Orca source version: 02.04.00.11
- printer models (families): 18
- Orca machine presets: 73
- printer configs (picker cards): 19
- printer presets (model x nozzle variants): 73
- print presets: 264
- filament presets: 540
-   of which OrcaFilamentLibrary: 321

## Orca-only settings carried as custom parameters

| Orca setting | 3.0 placeholder |
|---|---|
| `nozzle_temperature_range_high` | `custom_parameter_filament_nozzle_temperature_range_high` (custom_parameters_filament) |
| `parallel_printheads_count` | `custom_parameter_printer_parallel_printheads_count` (custom_parameters_printer) |

## Not executable on the imported configs (kept as Orca wrote them)

Tool-change G-code only runs on a tool/filament change; the imported configs have one material slot and no feeder, so 3.0 never executes it.

- `flush_length` in 12 preset(s), e.g. Elegoo Centauri 2 0.2 nozzle: toolchange_gcode
- `new_filament_e_feedrate` in 12 preset(s), e.g. Elegoo Centauri 2 0.2 nozzle: toolchange_gcode
- `new_filament_temp` in 12 preset(s), e.g. Elegoo Centauri 2 0.2 nozzle: toolchange_gcode
- `old_filament_e_feedrate` in 12 preset(s), e.g. Elegoo Centauri 2 0.2 nozzle: toolchange_gcode
- `old_filament_temp` in 12 preset(s), e.g. Elegoo Centauri 2 0.2 nozzle: toolchange_gcode

## Fallbacks that change the G-code

- thumbnails: entries in a format 3.0 cannot write left out (#30394F): 35 config(s): Elegoo Neptune 3 Max 0.2 nozzle, Elegoo Neptune 3 Max 0.4 nozzle, Elegoo Neptune 3 Max 0.6 nozzle, Elegoo Neptune 3 Max 0.8 nozzle, Elegoo Neptune 3 Max 1.0 nozzle, Elegoo Neptune 3 Plus 0.2 nozzle, Elegoo Neptune 3 Plus 0.4 nozzle, Elegoo Neptune 3 Plus 0.6 nozzle, Elegoo Neptune 3 Plus 0.8 nozzle, Elegoo Neptune 3 Plus 1.0 nozzle, Elegoo Neptune 3 Pro 0.2 nozzle, Elegoo Neptune 3 Pro 0.4 nozzle, Elegoo Neptune 3 Pro 0.6 nozzle, Elegoo Neptune 3 Pro 0.8 nozzle, Elegoo Neptune 3 Pro 1.0 nozzle, Elegoo Neptune 4 0.2 nozzle, Elegoo Neptune 4 0.4 nozzle, Elegoo Neptune 4 0.6 nozzle, Elegoo Neptune 4 0.8 nozzle, Elegoo Neptune 4 1.0 nozzle, Elegoo Neptune 4 Max 0.2 nozzle, Elegoo Neptune 4 Max 0.4 nozzle, Elegoo Neptune 4 Max 0.6 nozzle, Elegoo Neptune 4 Max 0.8 nozzle, Elegoo Neptune 4 Max 1.0 nozzle, Elegoo Neptune 4 Plus 0.2 nozzle, Elegoo Neptune 4 Plus 0.4 nozzle, Elegoo Neptune 4 Plus 0.6 nozzle, Elegoo Neptune 4 Plus 0.8 nozzle, Elegoo Neptune 4 Plus 1.0 nozzle, Elegoo Neptune 4 Pro 0.2 nozzle, Elegoo Neptune 4 Pro 0.4 nozzle, Elegoo Neptune 4 Pro 0.6 nozzle, Elegoo Neptune 4 Pro 0.8 nozzle, Elegoo Neptune 4 Pro 1.0 nozzle
- thumbnails: entries in a format 3.0 cannot write left out (COLPIC): 4 config(s): Elegoo OrangeStorm Giga 0.4 nozzle, Elegoo OrangeStorm Giga 0.6 nozzle, Elegoo OrangeStorm Giga 0.8 nozzle, Elegoo OrangeStorm Giga 1.0 nozzle

## Placeholder translations applied

| Orca | 3.0 | uses |
|---|---|---|
| `adaptive_bed_mesh_min/max, bed_mesh_probe_count, bed_mesh_algo` | `local variables, Orca's formula on first_layer_print_min/max` | 8 |
| `bed_temperature_initial_layer` | `first_layer_bed_temperature` | 283 |
| `bed_temperature_initial_layer_single` | `first_layer_bed_temperature[initial_extruder]` | 254 |
| `curr_bed_type` | `sheet_type` | 65 |
| `during_print_exhaust_fan_speed_num[i]` | `int(during_print_exhaust_fan_speed[i] / 100.0 * 255)` | 90 |
| `enable_pressure_advance[i]` | `pressure_advance[i] != "disabled"` | 8 |
| `enable_pressure_advance[i] == "true"` | `pressure_advance[i] != "disabled"` | 8 |
| `enable_prime_tower` | `wipe_tower` | 6 |
| `initial_no_support_extruder` | `initial_extruder (approximate)` | 136 |
| `nozzle_temperature_initial_layer` | `first_layer_temperature` | 117 |
| `nozzle_temperature_range_high` | `custom_parameter_filament_nozzle_temperature_range_high` | 12 |
| `outer_wall_acceleration` | `external_perimeter_acceleration` | 16 |
| `outer_wall_acceleration` | `external_perimeter_acceleration[current_extruder]` | 16 |
| `parallel_printheads_count` | `custom_parameter_printer_parallel_printheads_count` | 52 |
| `pressure_advance` | `pressure_advance_value` | 8 |
| `printable_height` | `max_print_height` | 175 |

## Notes

- Elegoo Centauri 2: asset 'elegoo_centuri_carbon_buildplate_texture.png' not in the Orca install; not referenced
- Elegoo Neptune 4: asset 'elegoo_neptune_pro_buildplate_model.stl' not in the Orca install; not referenced

## Dropped / unmapped Orca settings

Count = number of converted presets carrying the key.

| kind | Orca key | presets | reason |
|---|---|---|---|
| filament | `activate_chamber_temp_control` | 540 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance` | 540 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance_bridges` | 540 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance_model` | 540 | no 3.0 equivalent |
| filament | `adaptive_pressure_advance_overhangs` | 540 | no 3.0 equivalent |
| filament | `additional_cooling_fan_speed` | 540 | no 3.0 equivalent |
| filament | `additional_fan_full_speed_layer` | 540 | no 3.0 equivalent |
| filament | `bed_type` | 6 | no 3.0 equivalent |
| filament | `close_additional_fan_first_x_layers` | 540 | no 3.0 equivalent |
| filament | `disable_fan_first_layers` | 13 | no 3.0 equivalent |
| filament | `dont_slow_down_outer_wall` | 540 | no 3.0 equivalent |
| filament | `enable_overhang_bridge_fan` | 540 | no 3.0 equivalent |
| filament | `external_perimeter_speed` | 1 | no 3.0 equivalent |
| filament | `filament_adaptive_volumetric_speed` | 540 | no 3.0 equivalent |
| filament | `filament_adhesiveness_category` | 540 | no 3.0 equivalent |
| filament | `filament_change_length` | 540 | no 3.0 equivalent |
| filament | `filament_change_length_nc` | 540 | no 3.0 equivalent |
| filament | `filament_color` | 18 | no 3.0 equivalent |
| filament | `filament_cooling_before_tower` | 540 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_ams_limitations` | 540 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_heat_distortion_temperature` | 540 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_temperature` | 540 | no 3.0 equivalent |
| filament | `filament_dev_ams_drying_time` | 540 | no 3.0 equivalent |
| filament | `filament_dev_chamber_drying_bed_temperature` | 540 | no 3.0 equivalent |
| filament | `filament_dev_chamber_drying_time` | 540 | no 3.0 equivalent |
| filament | `filament_dev_drying_cooling_temperature` | 540 | no 3.0 equivalent |
| filament | `filament_dev_drying_softening_temperature` | 540 | no 3.0 equivalent |
| filament | `filament_extruder_compatibility` | 540 | no 3.0 equivalent |
| filament | `filament_extruder_variant` | 540 | no 3.0 equivalent |
| filament | `filament_flush_temp` | 540 | no 3.0 equivalent |
| filament | `filament_flush_temp_fast` | 540 | no 3.0 equivalent |
| filament | `filament_flush_volumetric_speed` | 540 | no 3.0 equivalent |
| filament | `filament_ids` | 540 | no 3.0 equivalent |
| filament | `filament_ironing_flow` | 540 | no 3.0 equivalent |
| filament | `filament_ironing_inset` | 540 | no 3.0 equivalent |
| filament | `filament_ironing_spacing` | 540 | no 3.0 equivalent |
| filament | `filament_ironing_speed` | 540 | no 3.0 equivalent |
| filament | `filament_is_support` | 540 | no 3.0 equivalent |
| filament | `filament_long_retractions_when_cut` | 321 | no 3.0 equivalent |
| filament | `filament_pre_cooling_temperature` | 540 | no 3.0 equivalent |
| filament | `filament_pre_cooling_temperature_nc` | 540 | no 3.0 equivalent |
| filament | `filament_preheat_temperature_delta` | 540 | no 3.0 equivalent |
| filament | `filament_prime_volume` | 540 | no 3.0 equivalent |
| filament | `filament_prime_volume_nc` | 540 | no 3.0 equivalent |
| filament | `filament_printable` | 540 | no 3.0 equivalent |
| filament | `filament_ramming_travel_time` | 540 | no 3.0 equivalent |
| filament | `filament_ramming_travel_time_nc` | 540 | no 3.0 equivalent |
| filament | `filament_ramming_volumetric_speed` | 540 | no 3.0 equivalent |
| filament | `filament_ramming_volumetric_speed_nc` | 540 | no 3.0 equivalent |
| filament | `filament_retract_layer_change` | 13 | no 3.0 equivalent |
| filament | `filament_retract_length_nc` | 540 | no 3.0 equivalent |
| filament | `filament_retract_lift_enforce` | 4 | 3.0 has no top/bottom-only z-hop setting |
| filament | `filament_retraction_distances_when_cut` | 321 | no 3.0 equivalent |
| filament | `filament_scarf_gap` | 321 | no 3.0 equivalent |
| filament | `filament_scarf_height` | 321 | no 3.0 equivalent |
| filament | `filament_scarf_length` | 321 | no 3.0 equivalent |
| filament | `filament_scarf_seam_type` | 321 | no 3.0 equivalent |
| filament | `filament_spool_weight` | 18 | no 3.0 equivalent |
| filament | `filament_tower_interface_pre_extrusion_dist` | 540 | no 3.0 equivalent |
| filament | `filament_tower_interface_pre_extrusion_length` | 540 | no 3.0 equivalent |
| filament | `filament_tower_interface_print_temp` | 540 | no 3.0 equivalent |
| filament | `filament_tower_interface_purge_volume` | 540 | no 3.0 equivalent |
| filament | `filament_tower_ironing_area` | 540 | no 3.0 equivalent |
| filament | `filament_wipe_distance` | 540 | 3.0 has no wipe distance |
| filament | `filament_z_hop_types` | 1 | 'Spiral Lift' has no 3.0 equivalent; printed as a normal vertical lift |
| filament | `first_x_layer_fan_speed` | 540 | no 3.0 equivalent |
| filament | `impact_strength_z` | 1 | no 3.0 equivalent |
| filament | `inherits` | 540 | no 3.0 equivalent |
| filament | `initial_layer_fan_speed` | 540 | no 3.0 equivalent |
| filament | `internal_bridge_fan_speed` | 540 | no 3.0 equivalent |
| filament | `ironing_fan_speed` | 540 | no 3.0 equivalent |
| filament | `long_retractions_when_ec` | 540 | no 3.0 equivalent |
| filament | `nozzle_temperature_range_high` | 540 | no 3.0 equivalent |
| filament | `nozzle_temperature_range_low` | 540 | no 3.0 equivalent |
| filament | `overhang_fan_speed` | 540 | Orca overhang fan has no 1:1 3.0 key (3.0 uses per-overlap overhang_fan_speed_N) |
| filament | `overhang_fan_threshold` | 540 | no 3.0 equivalent |
| filament | `overhang_threshold_participating_cooling` | 1 | no 3.0 equivalent |
| filament | `pellet_flow_coefficient` | 540 | no 3.0 equivalent |
| filament | `pre_start_fan_time` | 1 | no 3.0 equivalent |
| filament | `required_nozzle_HRC` | 540 | no 3.0 equivalent |
| filament | `retraction_distances_when_ec` | 540 | no 3.0 equivalent |
| filament | `support_material_interface_fan_speed` | 540 | no 3.0 equivalent |
| filament | `temperature_vitrification` | 540 | no 3.0 equivalent |
| filament | `volumetric_speed_coefficients` | 540 | no 3.0 equivalent |
| machine | `adaptive_bed_mesh_margin` | 75 | no 3.0 equivalent |
| machine | `auto_toolchange_command` | 12 | no 3.0 equivalent |
| machine | `auxiliary_fan` | 75 | no 3.0 equivalent |
| machine | `bbl_use_printhost` | 75 | physical printer setting, not a preset |
| machine | `bed_custom_model` | 2 | maps to printer box, not used for machine presets |
| machine | `bed_custom_texture` | 2 | maps to printer box, not used for machine presets |
| machine | `bed_exclude_area` | 75 | no 3.0 equivalent |
| machine | `bed_mesh_max` | 8 | no 3.0 equivalent |
| machine | `bed_mesh_min` | 8 | no 3.0 equivalent |
| machine | `bed_mesh_probe_distance` | 8 | no 3.0 equivalent |
| machine | `bed_temperature_formula` | 75 | no 3.0 equivalent |
| machine | `bed_texture_area` | 31 | no 3.0 equivalent |
| machine | `before_layer_change_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `belt_frame_tilt_angle` | 75 | no 3.0 equivalent |
| machine | `belt_frame_tilt_decouple` | 75 | no 3.0 equivalent |
| machine | `belt_printer` | 75 | no 3.0 equivalent |
| machine | `belt_printer_infinite_y` | 75 | no 3.0 equivalent |
| machine | `belt_slice_rotation` | 75 | no 3.0 equivalent |
| machine | `belt_slice_rotation_angle` | 75 | no 3.0 equivalent |
| machine | `belt_support_floor_offset` | 75 | no 3.0 equivalent |
| machine | `best_object_pos` | 75 | no 3.0 equivalent |
| machine | `build_plate_tilt_x` | 75 | no 3.0 equivalent |
| machine | `build_plate_tilt_y` | 75 | no 3.0 equivalent |
| machine | `change_extrusion_role_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `change_filament_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `cooling_filter_enabled` | 75 | no 3.0 equivalent |
| machine | `cooling_tube_length` | 2 | maps to printer box, not used for machine presets |
| machine | `cooling_tube_retraction` | 2 | maps to printer box, not used for machine presets |
| machine | `default_nozzle_volume_type` | 75 | no 3.0 equivalent |
| machine | `default_print_profile` | 2 | maps to printer box, not used for machine presets |
| machine | `deretract_speed_extruder_change` | 75 | no 3.0 equivalent |
| machine | `disable_m73` | 2 | maps to printer box, not used for machine presets |
| machine | `emit_machine_limits_to_gcode` | 40 | 3.0 cannot emit machine limits for Klipper flavor; used for time estimate only |
| machine | `emit_machine_limits_to_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `enable_belt_purge_tower` | 75 | no 3.0 equivalent |
| machine | `enable_filament_ramming` | 2 | maps to printer box, not used for machine presets |
| machine | `enable_long_retraction_when_cut` | 75 | no 3.0 equivalent |
| machine | `enable_power_loss_recovery` | 75 | no 3.0 equivalent |
| machine | `enable_pre_heating` | 75 | no 3.0 equivalent |
| machine | `extra_loading_move` | 2 | maps to printer box, not used for machine presets |
| machine | `extruder_clearance_dist_to_rod` | 75 | no 3.0 equivalent |
| machine | `extruder_clearance_height_to_lid` | 75 | 3.0 has a single clearance height (rod height used) |
| machine | `extruder_clearance_height_to_rod` | 2 | maps to printer box, not used for machine presets |
| machine | `extruder_clearance_radius` | 2 | maps to printer box, not used for machine presets |
| machine | `extruder_colour` | 75 | no 3.0 equivalent |
| machine | `extruder_max_nozzle_count` | 75 | no 3.0 equivalent |
| machine | `extruder_offset` | 2 | maps to printer box, not used for machine presets |
| machine | `extruder_printable_height` | 75 | no 3.0 equivalent |
| machine | `extruder_type` | 75 | no 3.0 equivalent |
| machine | `extruder_variant_list` | 75 | no 3.0 equivalent |
| machine | `fan_direction` | 75 | no 3.0 equivalent |
| machine | `fan_kickstart` | 75 | no 3.0 equivalent |
| machine | `fan_speedup_overhangs` | 75 | no 3.0 equivalent |
| machine | `fan_speedup_time` | 75 | no 3.0 equivalent |
| machine | `farthest_point_timelapse` | 75 | no 3.0 equivalent |
| machine | `file_start_gcode` | 75 | no 3.0 equivalent |
| machine | `flashforge_serial_number` | 75 | no 3.0 equivalent |
| machine | `gcode_flavor` | 2 | maps to printer box, not used for machine presets |
| machine | `gcode_skip_config_block` | 75 | no 3.0 equivalent |
| machine | `grab_length` | 75 | no 3.0 equivalent |
| machine | `group_algo_with_time` | 75 | no 3.0 equivalent |
| machine | `head_wrap_detect_zone` | 75 | no 3.0 equivalent |
| machine | `high_current_on_filament_swap` | 2 | maps to printer box, not used for machine presets |
| machine | `host_type` | 75 | physical printer setting, not a preset |
| machine | `hotend_cooling_rate` | 75 | no 3.0 equivalent |
| machine | `hotend_heating_rate` | 75 | no 3.0 equivalent |
| machine | `imex_carriage_margin` | 75 | no 3.0 equivalent |
| machine | `imex_firmware_managed_zones` | 75 | no 3.0 equivalent |
| machine | `imex_gantry_count` | 75 | no 3.0 equivalent |
| machine | `imex_mode_active_tools` | 75 | no 3.0 equivalent |
| machine | `imex_mode_gcodes` | 75 | no 3.0 equivalent |
| machine | `imex_mode_names` | 75 | no 3.0 equivalent |
| machine | `imex_nozzle_clearance_x` | 75 | no 3.0 equivalent |
| machine | `imex_nozzle_clearance_y` | 75 | no 3.0 equivalent |
| machine | `imex_tool_layout` | 75 | no 3.0 equivalent |
| machine | `imex_tools_per_gantry` | 75 | no 3.0 equivalent |
| machine | `imex_viz_theme` | 75 | no 3.0 equivalent |
| machine | `inherits` | 75 | no 3.0 equivalent |
| machine | `input_shaping_damp_x` | 75 | no 3.0 equivalent |
| machine | `input_shaping_damp_y` | 75 | no 3.0 equivalent |
| machine | `input_shaping_emit` | 75 | no 3.0 equivalent |
| machine | `input_shaping_freq_x` | 75 | no 3.0 equivalent |
| machine | `input_shaping_freq_y` | 75 | no 3.0 equivalent |
| machine | `input_shaping_type` | 75 | no 3.0 equivalent |
| machine | `is_imex` | 75 | no 3.0 equivalent |
| machine | `layer_change_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `long_retractions_when_cut` | 75 | no 3.0 equivalent |
| machine | `machine_bed_mass_Y` | 75 | no 3.0 equivalent |
| machine | `machine_end_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_hotend_change_time` | 75 | no 3.0 equivalent |
| machine | `machine_load_filament_time` | 75 | 3.0 filament_change_time is a different quantity |
| machine | `machine_max_acceleration_e` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_acceleration_extruding` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_acceleration_retracting` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_acceleration_travel` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_acceleration_x` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_acceleration_y` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_acceleration_z` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_force_Y` | 75 | no 3.0 equivalent |
| machine | `machine_max_jerk_e` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_jerk_x` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_jerk_y` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_jerk_z` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_junction_deviation` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_printed_mass` | 75 | no 3.0 equivalent |
| machine | `machine_max_speed_e` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_speed_x` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_speed_y` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_max_speed_z` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_min_extruding_rate` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_min_travel_rate` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_pause_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_prepare_compensation_time` | 75 | no 3.0 equivalent |
| machine | `machine_start_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `machine_tool_change_time` | 75 | no 3.0 equivalent |
| machine | `machine_unload_filament_time` | 75 | 3.0 filament_change_time is a different quantity |
| machine | `manual_filament_change` | 75 | no 3.0 equivalent |
| machine | `master_extruder_id` | 75 | no 3.0 equivalent |
| machine | `max_resonance_avoidance_speed` | 75 | no 3.0 equivalent |
| machine | `min_resonance_avoidance_speed` | 75 | no 3.0 equivalent |
| machine | `nozzle_flush_dataset` | 75 | no 3.0 equivalent |
| machine | `nozzle_height` | 75 | no 3.0 equivalent |
| machine | `nozzle_hrc` | 75 | no 3.0 equivalent |
| machine | `nozzle_type` | 75 | no 3.0 equivalent |
| machine | `nozzle_volume` | 75 | no 3.0 equivalent |
| machine | `parallel_printheads_bed_exclude_areas` | 75 | no 3.0 equivalent |
| machine | `parallel_printheads_count` | 75 | no 3.0 equivalent |
| machine | `parking_pos_retraction` | 2 | maps to printer box, not used for machine presets |
| machine | `part_cooling_fan_min_pwm` | 75 | no 3.0 equivalent |
| machine | `pellet_modded_printer` | 75 | no 3.0 equivalent |
| machine | `physical_extruder_map` | 75 | no 3.0 equivalent |
| machine | `preferred_orientation` | 75 | no 3.0 equivalent |
| machine | `print_host` | 75 | no 3.0 equivalent |
| machine | `print_host_webui` | 75 | no 3.0 equivalent |
| machine | `printable_area` | 2 | maps to printer box, not used for machine presets |
| machine | `printable_height` | 2 | maps to printer box, not used for machine presets |
| machine | `printer_agent` | 75 | no 3.0 equivalent |
| machine | `printer_extruder_id` | 75 | no 3.0 equivalent |
| machine | `printer_extruder_variant` | 75 | no 3.0 equivalent |
| machine | `printer_notes` | 2 | maps to printer box, not used for machine presets |
| machine | `printer_structure` | 75 | informational in Orca |
| machine | `printhost_apikey` | 75 | no 3.0 equivalent |
| machine | `printhost_authorization_type` | 75 | physical printer setting, not a preset |
| machine | `printhost_cafile` | 75 | no 3.0 equivalent |
| machine | `printhost_password` | 75 | no 3.0 equivalent |
| machine | `printhost_port` | 75 | no 3.0 equivalent |
| machine | `printhost_ssl_ignore_revoke` | 75 | physical printer setting, not a preset |
| machine | `printhost_user` | 75 | no 3.0 equivalent |
| machine | `printing_by_object_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `purge_in_prime_tower` | 2 | maps to printer box, not used for machine presets |
| machine | `resonance_avoidance` | 75 | no 3.0 equivalent |
| machine | `retract_after_wipe` | 75 | no 3.0 equivalent |
| machine | `retract_lift_enforce` | 75 | 3.0 has no top/bottom-only z-hop setting |
| machine | `retraction_distances_when_cut` | 75 | no 3.0 equivalent |
| machine | `scan_first_layer` | 75 | no 3.0 equivalent |
| machine | `silent_mode` | 75 | no 3.0 equivalent |
| machine | `single_extruder_multi_material` | 2 | maps to printer box, not used for machine presets |
| machine | `support_air_filtration` | 2 | maps to printer box, not used for machine presets |
| machine | `support_chamber_temp_control` | 75 | no 3.0 equivalent |
| machine | `support_cooling_filter` | 75 | no 3.0 equivalent |
| machine | `support_fast_purge_mode` | 75 | no 3.0 equivalent |
| machine | `support_multi_filament` | 16 | no 3.0 equivalent |
| machine | `support_object_skip_flush` | 75 | no 3.0 equivalent |
| machine | `support_parallel_printheads` | 75 | no 3.0 equivalent |
| machine | `support_wan_network` | 8 | no 3.0 equivalent |
| machine | `template_custom_gcode` | 2 | maps to printer box, not used for machine presets |
| machine | `time_cost` | 75 | no 3.0 equivalent |
| machine | `time_lapse_gcode` | 75 | no 3.0 equivalent |
| machine | `tool_change_on_wipe_tower` | 75 | no 3.0 equivalent |
| machine | `travel_slope` | 75 | no 3.0 equivalent |
| machine | `travel_slope` | 73 | used only with Orca 'Slope Lift' |
| machine | `upward_compatible_machine` | 75 | no 3.0 equivalent |
| machine | `use_3mf` | 75 | no 3.0 equivalent |
| machine | `use_firmware_retraction` | 2 | maps to printer box, not used for machine presets |
| machine | `use_relative_e_distances` | 2 | maps to printer box, not used for machine presets |
| machine | `wait_for_temp_on_wipe_tower` | 75 | no 3.0 equivalent |
| machine | `wipe_distance` | 75 | 3.0 has no wipe distance (wipe length follows retract_before_wipe) |
| machine | `wipe_tower_type` | 75 | no 3.0 equivalent |
| machine | `wrapping_detection_gcode` | 75 | no 3.0 equivalent |
| machine | `wrapping_detection_layers` | 75 | no 3.0 equivalent |
| machine | `wrapping_exclude_area` | 75 | no 3.0 equivalent |
| machine | `z_hop_types` | 75 | 'Auto Lift' has no 3.0 equivalent; printed as a normal vertical lift |
| machine | `z_offset` | 2 | maps to printer box, not used for machine presets |
| process | `accel_to_decel_enable` | 264 | no 3.0 equivalent |
| process | `accel_to_decel_factor` | 264 | no 3.0 equivalent |
| process | `align_infill_direction_to_model` | 264 | no 3.0 equivalent |
| process | `alternate_extra_wall` | 264 | no 3.0 equivalent |
| process | `belt_purge_tower_object` | 264 | no 3.0 equivalent |
| process | `belt_purge_tower_width` | 264 | no 3.0 equivalent |
| process | `bottom_layer_direction` | 264 | no 3.0 equivalent |
| process | `bottom_solid_infill_flow_ratio` | 264 | no 3.0 equivalent |
| process | `bottom_surface_density` | 264 | no 3.0 equivalent |
| process | `bottom_surface_filament_id` | 264 | no 3.0 equivalent |
| process | `bottom_surface_fill_order` | 264 | no 3.0 equivalent |
| process | `bridge_density` | 264 | no 3.0 equivalent |
| process | `bridge_line_width` | 264 | no 3.0 equivalent |
| process | `brim_ears_detection_length` | 264 | no 3.0 equivalent |
| process | `brim_ears_max_angle` | 264 | no 3.0 equivalent |
| process | `brim_ears_outer_only` | 264 | no 3.0 equivalent |
| process | `brim_flow_ratio` | 264 | no 3.0 equivalent |
| process | `brim_type` | 264 | Orca value 'auto_brim' has no 3.0 equivalent |
| process | `brim_use_efc_outline` | 264 | no 3.0 equivalent |
| process | `calib_flowrate_topinfill_special_order` | 264 | no 3.0 equivalent |
| process | `center_of_surface_pattern` | 264 | no 3.0 equivalent |
| process | `combine_brims` | 264 | no 3.0 equivalent |
| process | `counterbore_hole_bridging` | 264 | no 3.0 equivalent |
| process | `default_jerk` | 264 | 3.0 has no jerk settings |
| process | `default_junction_deviation` | 264 | no 3.0 equivalent |
| process | `detect_narrow_internal_solid_infill` | 264 | no 3.0 equivalent |
| process | `detect_overhang_wall` | 264 | no 3.0 equivalent |
| process | `dont_filter_internal_bridges` | 264 | no 3.0 equivalent |
| process | `elefant_foot_layers_density` | 264 | no 3.0 equivalent |
| process | `enable_extra_bridge_layer` | 264 | no 3.0 equivalent |
| process | `enable_mixed_color_sublayer` | 264 | no 3.0 equivalent |
| process | `enable_tower_interface_cooldown_during_tower` | 264 | no 3.0 equivalent |
| process | `enable_tower_interface_features` | 264 | no 3.0 equivalent |
| process | `enable_wrapping_detection` | 264 | no 3.0 equivalent |
| process | `enforce_support_layers` | 264 | no 3.0 equivalent |
| process | `extra_brim_width` | 264 | no 3.0 equivalent |
| process | `extra_solid_infills` | 264 | no 3.0 equivalent |
| process | `extrusion_rate_smoothing_external_perimeter_only` | 264 | no 3.0 equivalent |
| process | `filename_format` | 264 | uses filament_name, unknown to 3.0; default file name used |
| process | `fill_multiline` | 264 | no 3.0 equivalent |
| process | `filter_out_gap_fill` | 264 | no 3.0 equivalent |
| process | `flush_into_infill` | 264 | no 3.0 equivalent |
| process | `flush_into_objects` | 264 | no 3.0 equivalent |
| process | `flush_into_support` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin` | 264 | Orca value 'disabled_fuzzy' has no 3.0 equivalent |
| process | `fuzzy_skin_first_layer` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_layers_between_ripple_offset` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_mode` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_noise_type` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_octaves` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_persistence` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_ripple_offset` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_ripples_per_layer` | 264 | no 3.0 equivalent |
| process | `fuzzy_skin_scale` | 264 | no 3.0 equivalent |
| process | `gcode_add_line_number` | 264 | no 3.0 equivalent |
| process | `gyroid_optimized` | 264 | no 3.0 equivalent |
| process | `hole_to_polyhole` | 264 | no 3.0 equivalent |
| process | `hole_to_polyhole_max_edges` | 264 | no 3.0 equivalent |
| process | `hole_to_polyhole_threshold` | 264 | no 3.0 equivalent |
| process | `hole_to_polyhole_twisted` | 264 | no 3.0 equivalent |
| process | `imex_head_filament_map` | 264 | no 3.0 equivalent |
| process | `imex_parallel_mode` | 264 | no 3.0 equivalent |
| process | `independent_support_layer_height` | 264 | no 3.0 equivalent |
| process | `infill_complete_top` | 264 | no 3.0 equivalent |
| process | `infill_jerk` | 264 | 3.0 has no jerk settings |
| process | `infill_lock_depth` | 264 | no 3.0 equivalent |
| process | `infill_overhang_angle` | 264 | no 3.0 equivalent |
| process | `infill_shift_step` | 264 | no 3.0 equivalent |
| process | `inherits` | 264 | no 3.0 equivalent |
| process | `initial_layer_jerk` | 264 | 3.0 has no jerk settings |
| process | `initial_layer_min_bead_width` | 264 | no 3.0 equivalent |
| process | `initial_layer_travel_acceleration` | 264 | no 3.0 equivalent |
| process | `initial_layer_travel_jerk` | 264 | no 3.0 equivalent |
| process | `inner_wall_filament_id` | 264 | 0 = default filament (3.0 keeps its default) |
| process | `inner_wall_jerk` | 264 | 3.0 has no jerk settings |
| process | `internal_bridge_angle` | 264 | no 3.0 equivalent |
| process | `internal_bridge_density` | 264 | no 3.0 equivalent |
| process | `internal_bridge_flow` | 264 | no 3.0 equivalent |
| process | `internal_bridge_speed` | 264 | no 3.0 equivalent |
| process | `internal_solid_filament_id` | 264 | 0 = default filament (3.0 keeps its default) |
| process | `internal_solid_infill_pattern` | 264 | no 3.0 equivalent |
| process | `ironing_angle` | 264 | no 3.0 equivalent |
| process | `ironing_angle_fixed` | 264 | no 3.0 equivalent |
| process | `ironing_expansion` | 264 | no 3.0 equivalent |
| process | `ironing_inset` | 264 | no 3.0 equivalent |
| process | `ironing_pattern` | 264 | no 3.0 equivalent |
| process | `lateral_lattice_angle_1` | 264 | no 3.0 equivalent |
| process | `lateral_lattice_angle_2` | 264 | no 3.0 equivalent |
| process | `leading_brim_length` | 264 | no 3.0 equivalent |
| process | `lightning_overhang_angle` | 264 | no 3.0 equivalent |
| process | `lightning_prune_angle` | 264 | no 3.0 equivalent |
| process | `lightning_straightening_angle` | 264 | no 3.0 equivalent |
| process | `make_overhang_printable` | 264 | no 3.0 equivalent |
| process | `make_overhang_printable_angle` | 264 | no 3.0 equivalent |
| process | `make_overhang_printable_hole_size` | 264 | no 3.0 equivalent |
| process | `max_bridge_length` | 264 | no 3.0 equivalent |
| process | `max_volumetric_extrusion_rate_slope_segment_length` | 264 | no 3.0 equivalent |
| process | `min_length_factor` | 264 | no 3.0 equivalent |
| process | `min_width_top_surface` | 264 | no 3.0 equivalent |
| process | `outer_wall_filament_id` | 264 | no 3.0 equivalent |
| process | `outer_wall_jerk` | 264 | 3.0 has no jerk settings |
| process | `overhang_reverse` | 264 | no 3.0 equivalent |
| process | `overhang_reverse_internal_only` | 264 | no 3.0 equivalent |
| process | `overhang_reverse_threshold` | 264 | no 3.0 equivalent |
| process | `plugins` | 264 | no 3.0 equivalent |
| process | `precise_outer_wall` | 264 | no 3.0 equivalent |
| process | `precise_z_height` | 264 | no 3.0 equivalent |
| process | `preheat_steps` | 264 | no 3.0 equivalent |
| process | `preheat_time` | 264 | no 3.0 equivalent |
| process | `prime_tower_enable_framework` | 264 | no 3.0 equivalent |
| process | `prime_tower_flat_ironing` | 264 | no 3.0 equivalent |
| process | `prime_tower_infill_gap` | 264 | no 3.0 equivalent |
| process | `prime_tower_skip_points` | 264 | no 3.0 equivalent |
| process | `print_extruder_id` | 264 | no 3.0 equivalent |
| process | `print_extruder_variant` | 264 | no 3.0 equivalent |
| process | `print_order` | 264 | no 3.0 equivalent |
| process | `relative_bridge_angle` | 264 | no 3.0 equivalent |
| process | `scarf_angle_threshold` | 264 | no 3.0 equivalent |
| process | `scarf_joint_flow_ratio` | 264 | no 3.0 equivalent |
| process | `scarf_joint_speed` | 264 | no 3.0 equivalent |
| process | `scarf_overhang_threshold` | 264 | no 3.0 equivalent |
| process | `seam_slope_conditional` | 264 | no 3.0 equivalent |
| process | `seam_slope_steps` | 264 | no 3.0 equivalent |
| process | `separated_infills` | 264 | no 3.0 equivalent |
| process | `single_loop_draft_shield` | 264 | no 3.0 equivalent |
| process | `skeleton_infill_density` | 264 | no 3.0 equivalent |
| process | `skeleton_infill_line_width` | 264 | no 3.0 equivalent |
| process | `skin_infill_density` | 264 | no 3.0 equivalent |
| process | `skin_infill_depth` | 264 | no 3.0 equivalent |
| process | `skin_infill_line_width` | 264 | no 3.0 equivalent |
| process | `skirt_start_angle` | 264 | no 3.0 equivalent |
| process | `skirt_type` | 264 | no 3.0 equivalent |
| process | `slicing_pipeline_plugin` | 264 | no 3.0 equivalent |
| process | `slowdown_for_curled_perimeters` | 264 | no 3.0 equivalent |
| process | `small_area_infill_flow_compensation` | 264 | no 3.0 equivalent |
| process | `small_area_infill_flow_compensation_model` | 264 | no 3.0 equivalent |
| process | `small_support_perimeter_speed` | 264 | no 3.0 equivalent |
| process | `small_support_perimeter_threshold` | 264 | no 3.0 equivalent |
| process | `solid_infill_direction` | 264 | no 3.0 equivalent |
| process | `solid_infill_rotate_template` | 264 | no 3.0 equivalent |
| process | `sparse_infill_filament_id` | 264 | 0 = default filament (3.0 keeps its default) |
| process | `sparse_infill_rotate_template` | 264 | no 3.0 equivalent |
| process | `sparse_infill_smooth_factor` | 264 | no 3.0 equivalent |
| process | `spiral_finishing_flow_ratio` | 264 | no 3.0 equivalent |
| process | `spiral_mode_max_xy_smoothing` | 264 | no 3.0 equivalent |
| process | `spiral_mode_smooth` | 264 | no 3.0 equivalent |
| process | `spiral_starting_flow_ratio` | 264 | no 3.0 equivalent |
| process | `support_bottom_interface_spacing` | 264 | no 3.0 equivalent |
| process | `support_critical_regions_only` | 264 | no 3.0 equivalent |
| process | `support_expansion` | 264 | no 3.0 equivalent |
| process | `support_interface_loop_pattern` | 264 | no 3.0 equivalent |
| process | `support_interface_not_for_body` | 264 | no 3.0 equivalent |
| process | `support_ironing` | 264 | no 3.0 equivalent |
| process | `support_ironing_flow` | 264 | no 3.0 equivalent |
| process | `support_ironing_pattern` | 264 | no 3.0 equivalent |
| process | `support_ironing_spacing` | 264 | no 3.0 equivalent |
| process | `support_object_first_layer_gap` | 264 | no 3.0 equivalent |
| process | `support_remove_small_overhang` | 264 | no 3.0 equivalent |
| process | `support_threshold_overlap` | 264 | no 3.0 equivalent |
| process | `symmetric_infill_y_axis` | 264 | no 3.0 equivalent |
| process | `thick_internal_bridges` | 264 | no 3.0 equivalent |
| process | `timelapse_type` | 264 | no 3.0 equivalent |
| process | `toolchange_cyclic_first_layer` | 264 | no 3.0 equivalent |
| process | `toolchange_cyclic_order` | 264 | no 3.0 equivalent |
| process | `toolchange_ordering` | 264 | no 3.0 equivalent |
| process | `top_bottom_infill_wall_overlap` | 264 | no 3.0 equivalent |
| process | `top_layer_direction` | 264 | no 3.0 equivalent |
| process | `top_surface_density` | 264 | no 3.0 equivalent |
| process | `top_surface_expansion` | 264 | no 3.0 equivalent |
| process | `top_surface_expansion_direction` | 264 | no 3.0 equivalent |
| process | `top_surface_expansion_margin` | 264 | no 3.0 equivalent |
| process | `top_surface_filament_id` | 264 | no 3.0 equivalent |
| process | `top_surface_fill_order` | 264 | no 3.0 equivalent |
| process | `top_surface_jerk` | 264 | 3.0 has no jerk settings |
| process | `tpms_adaptive` | 264 | no 3.0 equivalent |
| process | `tpms_adaptive_gradient` | 264 | no 3.0 equivalent |
| process | `tpms_interior_density` | 264 | no 3.0 equivalent |
| process | `travel_jerk` | 264 | 3.0 has no jerk settings |
| process | `tree_support_auto_brim` | 264 | no 3.0 equivalent |
| process | `tree_support_branch_angle` | 264 | no 3.0 equivalent |
| process | `tree_support_branch_diameter` | 264 | no 3.0 equivalent |
| process | `tree_support_branch_distance` | 264 | no 3.0 equivalent |
| process | `tree_support_brim_width` | 264 | no 3.0 equivalent |
| process | `tree_support_wall_count` | 264 | no 3.0 equivalent |
| process | `unsupported_wall_last` | 264 | no 3.0 equivalent |
| process | `wall_direction` | 264 | no 3.0 equivalent |
| process | `wall_maximum_deviation` | 264 | no 3.0 equivalent |
| process | `wall_maximum_resolution` | 264 | no 3.0 equivalent |
| process | `wall_sequence` | 16 | 'inner-outer-inner wall' has no 3.0 equivalent |
| process | `wipe_before_external_loop` | 264 | no 3.0 equivalent |
| process | `wipe_inward` | 264 | no 3.0 equivalent |
| process | `wipe_inward_distance` | 264 | no 3.0 equivalent |
| process | `wipe_on_loops` | 264 | no 3.0 equivalent |
| process | `wipe_tower_extra_rib_length` | 264 | no 3.0 equivalent |
| process | `wipe_tower_fillet_wall` | 264 | no 3.0 equivalent |
| process | `wipe_tower_max_purge_speed` | 264 | no 3.0 equivalent |
| process | `wipe_tower_rib_width` | 264 | no 3.0 equivalent |
| process | `wipe_tower_rotation_angle` | 264 | no 3.0 equivalent |
| process | `wipe_tower_sparse_layers_combination` | 264 | no 3.0 equivalent |
| process | `wipe_tower_wall_type` | 264 | no 3.0 equivalent |
| process | `wiping_volumes_extruders` | 264 | no 3.0 equivalent |
| process | `zaa_dont_alternate_fill_direction` | 264 | no 3.0 equivalent |
| process | `zaa_enabled` | 264 | no 3.0 equivalent |
| process | `zaa_min_z` | 264 | no 3.0 equivalent |
| process | `zaa_minimize_perimeter_height` | 264 | no 3.0 equivalent |
