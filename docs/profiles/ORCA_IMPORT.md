# Importing OrcaSlicer vendor profiles

`tools/orca_import/` converts OrcaSlicer's stock vendor profiles (printers, processes,
filaments) into PiggieSlicer 3.0 preset bundles under `resources/presets/orca-import/`.
Proven on **Anycubic** and **Elegoo**. Only Orca's stock vendor profiles are used; nothing from
the user's Orca settings folder (`%APPDATA%\OrcaSlicer\user`, `user_backup-*`) is read.

## Source

Since Orca 2.5 the installed app ships presets only as binary per-vendor caches
(`C:\Program Files\OrcaSlicer\resources\profiles\<Vendor>.opc`, magic `ZCRO`). Orca's design
doc (`docs/HLSD/preset-cache.md`) states an `.opc` is equivalent to the vendor JSON it is built
from in CI, so the converter reads those JSON files from the release tag that matches the
install (`v2.5.0-alpha` of github.com/OrcaSlicer/OrcaSlicer, `resources/profiles/<Vendor>.json`
+ `<Vendor>/`, plus `OrcaFilamentLibrary`, which vendor filaments may inherit from).

The converter refuses to run unless the version in each installed `.opc` header equals the
`version` of the fetched JSON (Anycubic `2.4.0.9` = `02.04.00.09`, Elegoo `2.4.0.11` =
`02.04.00.11`, OrcaFilamentLibrary `2.4.0.16` = `02.04.00.16`). Bed models, bed textures and
cover images are copied from the install's `resources\profiles\<Vendor>\` folder.

Orca's own loading rules are reproduced, read from Orca's source at the same tag:

* **Inheritance** (`PresetBundle::load_vendor_configs_from_json`): a preset is its parent's
  resolved config with its own keys applied; missing parents are looked up in
  OrcaFilamentLibrary. Only `"instantiation": "true"` presets become visible presets.
* **Built-in defaults**: a root preset starts from Orca's default preset of its type. The fetch
  step extracts these defaults from `PrintConfig.cpp` (`set_default_value`) and the per-type key
  lists from `Preset.cpp` into `orca_defaults.json` (`orca_defaults.py`).
* **`PrintConfigDef::handle_legacy`**: key renames, value rewrites and dropped keys are applied
  per JSON file before merging (e.g. an old-style `"35%"` in `initial_layer_speed`,
  `support_interface_speed`, `support_object_xy_distance` is erased by Orca, so the parent value
  stays; `silent_mode` is ignored by Orca). See `orca_source.handle_legacy`.
* **Number parsing**: Orca reads `coFloat(s)`/`coInt(s)` values with `istringstream >>`, so a
  stray `"35%"` in e.g. `initial_layer_infill_speed` is 35. Option types come from
  `orca_config_types.json`.
* **Default bed type** (`Preset::get_default_bed_type`): a numeric `default_bed_type` > 0,
  otherwise High Temp Plate (non-numeric strings such as `"Textured PEI Plate"` also fall back to
  High Temp Plate in Orca, and therefore here).

## Running

```
# 1. fetch the JSON profiles + Orca defaults/types for the installed release (needs `gh`)
python tools/orca_import/fetch_orca_profiles.py --tag v2.5.0-alpha --out <scratch>/orca_profiles Anycubic Elegoo OrcaFilamentLibrary

# 2. convert (validates the output; exit code 1 if any vendor report is FAILED)
python tools/orca_import/convert.py --orca-install "C:/Program Files/OrcaSlicer" --orca-profiles <scratch>/orca_profiles --out resources/presets/orca-import Anycubic Elegoo

# 3. optional: re-validate an output directory on its own
python tools/orca_import/validate.py resources/presets/orca-import/Anycubic

# 4. end-to-end: slice a 20 mm box on every printer config and compare with the Orca values.
#    The CLI only finds printers that have a card, and the shipped output has one card per model, so for
#    the test run convert the tested vendors with --per-nozzle-configs into resources/presets/orca-import,
#    and convert them again without it afterwards (the converter is deterministic, both runs take seconds).
python tools/orca_import/e2e_test.py --exe build/src/slic3r-app-launcher/Release/piggie-slicer.exe --datadir <scratch>/datadir --orca-profiles <scratch>/orca_profiles --model tests/data/test_stl/ASCII/20mmbox-LF.stl --out <scratch>/gcode Anycubic Elegoo
```

Always give the app a scratch `--datadir`. Re-running the converter produces byte-identical
output (ids are derived from names: base64 of an MD5 of vendor/kind/name). Per-vendor reports
are written to `docs/profiles/orca-import/<Vendor>.md`.

`config_schema.json` is the 3.0 config definition as exported by the app
(`piggie-slicer --export-config-schema config_schema.json`); refresh it after config changes.

## Output layout

`resources/presets/orca-import/` is a separate preset repository directory. The bundle loader
(`BundleLoader.cpp`) loads every `<repo>/<Vendor>/vendor.yaml` under `resources/presets/` and
`populate_local_bundle` copies `<Vendor>/` and `<Vendor>.idx` into the data dir. No updater source
offers `orca-import`, so the preset updater leaves it alone ("No source offers orca-import ...
Skipping"). The Welcome dialog's printer list (`AddPrinterPanel`) shows every loaded vendor.

Per vendor:

| file | content |
|---|---|
| `vendor.yaml` | vendor (id/name = Orca vendor, version `2.4.0+orca.02.04.00.09`), features, `printer_families` (one per Orca machine model), one `printer` per Orca machine model (`model`/`base_model` = Orca `printer_model`, so `[printer_model]` keeps Orca's value), one `tool` per nozzle diameter (`0.4 nozzle`, condition `printer.supports_04_nozzle`), six `sheet`s = Orca bed types, one `printer_config` (picker card) per printer model and tool count, like Prusa's "XL 1T"/"XL 2T"; the nozzle is chosen afterwards as a tool. Orca's wizard has no single default nozzle (it installs every nozzle, `WebGuideDialog.cpp` `nozzle_selected = nozzle_diameter`), so the card starts with 0.4 when the model has it, else the model's first listed nozzle (`convert.py` `config_groups`). With `--per-nozzle-configs` there is one card per Orca machine preset instead (name `"<model> 0.4 nozzle"`, identical to Orca's); the e2e test needs that, because the CLI only finds printers that have a card |
| `preset-printer-<model>.yaml` | printer preset named after the model; one variant per nozzle (`tool.nozzle_diameter == d`) with that machine preset's printer settings |
| `preset-print-machine.yaml` | `*orca machine*` base: one variant per machine preset with its nozzle-specific print settings (retraction, z-hop, layer height limits). 3.0 rejects Tool overrides on single-tool printers (`Print.cpp`, `ToolOverridesInSingleToolPrint`), so - like Prusa's `*common MINI*` - they live in a print base every print preset inherits |
| `preset-print-<model>.yaml` | one root per instantiated Orca process, name = Orca name, condition = its `compatible_printers` as `(printer.model == "M" and tool.nozzle_diameter == d) or ...` |
| `preset-filament-<model>.yaml` | one root per instantiated Orca filament, same condition scheme; bed temperatures in variants per sheet (`sheet.type == "Textured PEI Plate"` ...) |
| `preset-tool-print.yaml` | the single `no tool` tool print preset single-tool printers need |
| `assets/` | Orca bed model/texture/cover files referenced by `visual` |
| `../<Vendor>.idx` | `min_slic3r_version = 3.0.0-alpha0` + the version line |

Bed types: sheet ids `cool_plate`, `engineering_plate`, `high_temp_plate`, `textured_pei_plate`,
`textured_cool_plate`, `supertack_plate`; sheet *types* are Orca's `curr_bed_type` keys
(`Cool Plate`, ... `Supertack Plate`), so Orca G-code comparing `curr_bed_type` works with 3.0's
`sheet_type`. Printers with `support_multi_bed_types` offer all six, others only their default
bed. Filament `<plate>_temp` / `<plate>_temp_initial_layer` become `bed_temperature` /
`first_layer_bed_temperature` in the matching sheet variant.

## Mapping table

Authoritative source: `tools/orca_import/mapping.py` (`MAP`, regenerate this table with
`python tools/orca_import/mapping.py`). Every Orca key a preset carries is mapped here, consumed
structurally (below), or dropped and listed in the vendor report. Enum values without a 3.0
counterpart are dropped and reported, never guessed.

Structural keys (handled in `convert.py`): `compatible_printers` (conditions),
`printer_model`, `nozzle_diameter` (tools), `default_bed_type`, `support_multi_bed_types`
(sheets), plate temperatures (sheet variants), `thumbnails` + `thumbnails_format`
(`"WxH/FMT, ..."`; BTT_TFT/COLPIC dropped), `enable_support` + `support_type`
(`support_material` none/everywhere/enforcers_only), `support_style` / `support_base_pattern`
(`default` = grid/rectilinear for normal and organic/hollow for tree supports, per Orca's
tooltips; hollow has no 3.0 value), `ironing_type` (`ironing` + `ironing_type`),
`exclude_object` / `gcode_label_objects` (firmware / octoprint / disabled), `wall_sequence`
(`external_perimeters_first`), `z_hop_types` (only `Slope Lift` = `travel_ramping_lift` with
`travel_max_lift` = z-hop and `travel_slope`; Auto/Spiral print as a normal lift and are reported),
`filament_shrink` / `filament_shrinkage_compensation_z` (3.0 value = 100 % - Orca value),
`idle_temperature` (0 = unset), `default_filament_profile`.

Other fixed rules: Klipper flavor with `emit_machine_limits_to_gcode = 1` becomes
`machine_limits_usage = time_estimate_only` (3.0 refuses to slice that combination,
`MachineLimitsWithKlipper`); a `filename_format` using variables 3.0 lacks is dropped (3.0's
default file name is used).

| Orca key | 3.0 preset | 3.0 key | conversion |
|---|---|---|---|
| `bed_custom_model` | printer | `bed_custom_model` | text |
| `bed_custom_texture` | printer | `bed_custom_texture` | text |
| `before_layer_change_gcode` | printer | `before_layer_gcode` | text |
| `bottom_shell_layers` | print | `bottom_solid_layers` | integer |
| `bottom_shell_thickness` | print | `bottom_solid_min_thickness` | num |
| `bottom_surface_pattern` | print | `bottom_fill_pattern` | enum: rectilinear->rectilinear, monotonic->monotonic, monotonicline->monotoniclines, alignedrectilinear->alignedrectilinear, concentric->concentric, hilbertcurve->hilbertcurve, archimedeanchords->archimedeanchords, octagramspiral->octagramspiral |
| `bridge_acceleration` | print | `bridge_acceleration` | '%' resolved against Orca outer_wall_acceleration |
| `bridge_angle` | print | `bridge_angle` | num |
| `bridge_flow` | print | `bridge_flow_ratio` | num |
| `bridge_no_support` | print | `dont_support_bridges` | boolean |
| `bridge_speed` | print | `bridge_speed` | num |
| `brim_object_gap` | print | `brim_separation` | num |
| `brim_type` | print | `brim_type` | enum: no_brim->no_brim, outer_only->outer_only, inner_only->inner_only, outer_and_inner->outer_and_inner |
| `brim_width` | print | `brim_width` | num |
| `chamber_temperature` | filament | `chamber_temperature` | integer |
| `change_filament_gcode` | printer | `toolchange_gcode` | text |
| `close_fan_the_first_x_layers` | filament | `disable_fan_first_layers` | integer |
| `cooling_tube_length` | printer | `cooling_tube_length` | num |
| `cooling_tube_retraction` | printer | `cooling_tube_retraction` | num |
| `default_acceleration` | print | `default_acceleration` | num |
| `default_filament_colour` | filament | `filament_colour` | text |
| `default_print_profile` | printer | `default_print` | text |
| `deretraction_speed` | print (machine base, per nozzle) | `deretract_speed` | num |
| `detect_thin_wall` | print | `thin_walls` | boolean |
| `disable_m73` | printer | `remaining_times` | not_bool |
| `draft_shield` | print | `draft_shield` | enum: disabled->disabled, enabled->enabled |
| `elefant_foot_compensation` | print | `elefant_foot_compensation` | num |
| `emit_machine_limits_to_gcode` | printer | `machine_limits_usage` | enum: 1->emit_to_gcode, 0->time_estimate_only |
| `enable_arc_fitting` | print | `arc_fitting` | enum: 1->emit_center, 0->disabled |
| `enable_overhang_speed` | print | `enable_dynamic_overhang_speeds` | boolean |
| `enable_pressure_advance` | filament | `pressure_advance` | enum: 1->enabled, 0->disabled |
| `enable_prime_tower` | print | `wipe_tower` | boolean |
| `ensure_vertical_shell_thickness` | print | `ensure_vertical_shell_thickness` | enum: none->disabled, ensure_all->enabled |
| `extra_loading_move` | printer | `extra_loading_move` | num |
| `extra_perimeters_on_overhangs` | print | `extra_perimeters_on_overhangs` | boolean |
| `extruder_clearance_height_to_rod` | printer | `extruder_clearance_height` | num |
| `extruder_clearance_radius` | printer | `extruder_clearance_radius` | num |
| `extruder_offset` | printer | `extruder_offset` | vec2_list |
| `fan_cooling_layer_time` | filament | `fan_below_layer_time` | integer |
| `fan_max_speed` | filament | `max_fan_speed` | integer |
| `fan_min_speed` | filament | `min_fan_speed` | integer |
| `filament_cooling_final_speed` | filament | `filament_cooling_final_speed` | num |
| `filament_cooling_initial_speed` | filament | `filament_cooling_initial_speed` | num |
| `filament_cooling_moves` | filament | `filament_cooling_moves` | integer |
| `filament_cost` | filament | `filament_cost` | num |
| `filament_density` | filament | `filament_density` | num |
| `filament_deretraction_speed` | filament | `deretract_speed` | num |
| `filament_diameter` | filament | `filament_diameter` | num |
| `filament_end_gcode` | filament | `end_filament_gcode` | text |
| `filament_flow_ratio` | filament | `extrusion_multiplier` | num |
| `filament_loading_speed` | filament | `filament_loading_speed` | num |
| `filament_loading_speed_start` | filament | `filament_loading_speed_start` | num |
| `filament_max_volumetric_speed` | filament | `filament_max_volumetric_speed` | num |
| `filament_minimal_purge_on_wipe_tower` | filament | `filament_minimal_purge_on_wipe_tower` | num |
| `filament_multitool_ramming` | filament | `filament_multitool_ramming` | boolean |
| `filament_multitool_ramming_flow` | filament | `filament_multitool_ramming_flow` | num |
| `filament_multitool_ramming_volume` | filament | `filament_multitool_ramming_volume` | num |
| `filament_notes` | filament | `filament_notes` | text |
| `filament_ramming_parameters` | filament | `filament_ramming_parameters` | text |
| `filament_retract_before_wipe` | filament | `retract_before_wipe` | percent |
| `filament_retract_lift_above` | filament | `retract_lift_above` | num |
| `filament_retract_lift_below` | filament | `retract_lift_below` | num |
| `filament_retract_restart_extra` | filament | `retract_restart_extra` | num |
| `filament_retract_when_changing_layer` | filament | `retract_layer_change` | boolean |
| `filament_retraction_length` | filament | `retract_length` | num |
| `filament_retraction_minimum_travel` | filament | `retract_before_travel` | num |
| `filament_retraction_speed` | filament | `retract_speed` | num |
| `filament_soluble` | filament | `filament_soluble` | boolean |
| `filament_stamping_distance` | filament | `filament_stamping_distance` | num |
| `filament_stamping_loading_speed` | filament | `filament_stamping_loading_speed` | num |
| `filament_start_gcode` | filament | `start_filament_gcode` | text |
| `filament_toolchange_delay` | filament | `filament_toolchange_delay` | num |
| `filament_type` | filament | `filament_type` | text |
| `filament_unloading_speed` | filament | `filament_unloading_speed` | num |
| `filament_unloading_speed_start` | filament | `filament_unloading_speed_start` | num |
| `filament_vendor` | filament | `filament_vendor` | text |
| `filament_wipe` | filament | `wipe` | boolean |
| `filament_z_hop` | filament | `retract_lift` | num |
| `filename_format` | print | `output_filename_format` | text |
| `full_fan_speed_layer` | filament | `full_fan_speed_layer` | integer |
| `fuzzy_skin` | print | `fuzzy_skin` | enum: none->none, external->external, all->all |
| `fuzzy_skin_point_distance` | print | `fuzzy_skin_point_dist` | num |
| `fuzzy_skin_thickness` | print | `fuzzy_skin_thickness` | num |
| `gap_fill_target` | print | `gap_fill_enabled` | enum: nowhere->False, everywhere->True, topbottom->True |
| `gap_infill_speed` | print | `gap_fill_speed` | num |
| `gcode_comments` | print | `gcode_comments` | boolean |
| `gcode_flavor` | printer | `gcode_flavor` | enum: marlin->marlin, klipper->klipper, reprapfirmware->reprapfirmware, repetier->repetier, marlin2->marlin2, reprap->reprap, teacup->teacup, makerware->makerware, sailfish->sailfish, smoothie->smoothie, mach3->mach3, machinekit->machinekit, no-extrusion->no-extrusion |
| `high_current_on_filament_swap` | printer | `high_current_on_filament_swap` | boolean |
| `infill_anchor` | print | `infill_anchor` | float_or_percent |
| `infill_anchor_max` | print | `infill_anchor_max` | float_or_percent |
| `infill_combination` | print | `automatic_infill_combination` | boolean |
| `infill_combination_max_layer_height` | print | `automatic_infill_combination_max_layer_height` | float_or_percent |
| `infill_direction` | print | `fill_angle` | num |
| `infill_wall_overlap` | print | `infill_overlap` | float_or_percent |
| `initial_layer_acceleration` | print | `first_layer_acceleration` | num |
| `initial_layer_infill_speed` | print | `first_layer_solid_infill_speed` | num |
| `initial_layer_infill_speed` | print | `first_layer_infill_speed` | num |
| `initial_layer_line_width` | print | `first_layer_extrusion_width` | float_or_percent |
| `initial_layer_print_height` | print | `first_layer_height` | float_or_percent |
| `initial_layer_speed` | print | `first_layer_speed` | num |
| `inner_wall_acceleration` | print | `perimeter_acceleration` | num |
| `inner_wall_line_width` | print | `perimeter_extrusion_width` | float_or_percent |
| `inner_wall_speed` | print | `perimeter_speed` | num |
| `interface_shells` | print | `interface_shells` | boolean |
| `interlocking_beam` | print | `interlocking_beam` | boolean |
| `interlocking_beam_layer_count` | print | `interlocking_beam_layer_count` | integer |
| `interlocking_beam_width` | print | `interlocking_beam_width` | num |
| `interlocking_boundary_avoidance` | print | `interlocking_boundary_avoidance` | integer |
| `interlocking_depth` | print | `interlocking_depth` | integer |
| `interlocking_orientation` | print | `interlocking_orientation` | num |
| `internal_solid_infill_acceleration` | print | `solid_infill_acceleration` | '%' resolved against Orca default_acceleration |
| `internal_solid_infill_line_width` | print | `solid_infill_extrusion_width` | float_or_percent |
| `internal_solid_infill_speed` | print | `solid_infill_speed` | num |
| `ironing_flow` | print | `ironing_flowrate` | percent |
| `ironing_spacing` | print | `ironing_spacing` | num |
| `ironing_speed` | print | `ironing_speed` | num |
| `is_infill_first` | print | `infill_first` | boolean |
| `layer_change_gcode` | printer | `layer_gcode` | text |
| `layer_height` | print | `layer_height` | num |
| `line_width` | print | `extrusion_width` | float_or_percent |
| `machine_end_gcode` | printer | `end_gcode` | text |
| `machine_max_acceleration_e` | printer | `machine_max_acceleration_e` | float_list |
| `machine_max_acceleration_extruding` | printer | `machine_max_acceleration_extruding` | float_list |
| `machine_max_acceleration_retracting` | printer | `machine_max_acceleration_retracting` | float_list |
| `machine_max_acceleration_travel` | printer | `machine_max_acceleration_travel` | float_list |
| `machine_max_acceleration_x` | printer | `machine_max_acceleration_x` | float_list |
| `machine_max_acceleration_y` | printer | `machine_max_acceleration_y` | float_list |
| `machine_max_acceleration_z` | printer | `machine_max_acceleration_z` | float_list |
| `machine_max_jerk_e` | printer | `machine_max_jerk_e` | float_list |
| `machine_max_jerk_x` | printer | `machine_max_jerk_x` | float_list |
| `machine_max_jerk_y` | printer | `machine_max_jerk_y` | float_list |
| `machine_max_jerk_z` | printer | `machine_max_jerk_z` | float_list |
| `machine_max_junction_deviation` | printer | `machine_max_junction_deviation` | float_list |
| `machine_max_speed_e` | printer | `machine_max_feedrate_e` | float_list |
| `machine_max_speed_x` | printer | `machine_max_feedrate_x` | float_list |
| `machine_max_speed_y` | printer | `machine_max_feedrate_y` | float_list |
| `machine_max_speed_z` | printer | `machine_max_feedrate_z` | float_list |
| `machine_min_extruding_rate` | printer | `machine_min_extruding_rate` | float_list |
| `machine_min_travel_rate` | printer | `machine_min_travel_rate` | float_list |
| `machine_pause_gcode` | printer | `pause_print_gcode` | text |
| `machine_start_gcode` | printer | `start_gcode` | text |
| `max_layer_height` | print (machine base, per nozzle) | `max_layer_height` | num |
| `max_travel_detour_distance` | print | `avoid_crossing_perimeters_max_detour` | num |
| `max_volumetric_extrusion_rate_slope` | print | `max_volumetric_extrusion_rate_slope_positive` | num |
| `max_volumetric_extrusion_rate_slope` | print | `max_volumetric_extrusion_rate_slope_negative` | num |
| `min_bead_width` | print | `min_bead_width` | float_or_percent |
| `min_feature_size` | print | `min_feature_size` | float_or_percent |
| `min_layer_height` | print (machine base, per nozzle) | `min_layer_height` | num |
| `min_skirt_length` | print | `min_skirt_length` | num |
| `minimum_sparse_infill_area` | print | `solid_infill_below_area` | num |
| `mmu_segmented_region_interlocking_depth` | print | `mmu_segmented_region_interlocking_depth` | num |
| `mmu_segmented_region_max_width` | print | `mmu_segmented_region_max_width` | num |
| `notes` | print | `notes` | text |
| `nozzle_temperature` | filament | `temperature` | integer |
| `nozzle_temperature_initial_layer` | filament | `first_layer_temperature` | integer |
| `only_one_wall_first_layer` | print | `only_one_perimeter_first_layer` | boolean |
| `only_one_wall_top` | print | `top_one_perimeter_type` | enum: 1->top, 0->none |
| `ooze_prevention` | print | `ooze_prevention` | boolean |
| `outer_wall_acceleration` | print | `external_perimeter_acceleration` | num |
| `outer_wall_line_width` | print | `external_perimeter_extrusion_width` | float_or_percent |
| `outer_wall_speed` | print | `external_perimeter_speed` | num |
| `overhang_1_4_speed` | print | `overhang_speed_3` | float_or_percent |
| `overhang_2_4_speed` | print | `overhang_speed_2` | float_or_percent |
| `overhang_3_4_speed` | print | `overhang_speed_1` | float_or_percent |
| `overhang_4_4_speed` | print | `overhang_speed_0` | float_or_percent |
| `parking_pos_retraction` | printer | `parking_pos_retraction` | num |
| `post_process` | print | `post_process` | <lambda> |
| `pressure_advance` | filament | `pressure_advance_value` | num |
| `prime_tower_brim_width` | print | `wipe_tower_brim_width` | num |
| `prime_tower_width` | print | `wipe_tower_width` | num |
| `print_sequence` | print | `complete_objects` | enum: by layer->False, by object->True |
| `printable_area` | printer | `bed_shape` | vec2_list |
| `printable_height` | printer | `max_print_height` | num |
| `printer_notes` | printer | `printer_notes` | text |
| `printing_by_object_gcode` | printer | `between_objects_gcode` | text |
| `raft_contact_distance` | print | `raft_contact_distance` | num |
| `raft_expansion` | print | `raft_expansion` | num |
| `raft_first_layer_density` | print | `raft_first_layer_density` | percent |
| `raft_first_layer_expansion` | print | `raft_first_layer_expansion` | num |
| `raft_layers` | print | `raft_layers` | integer |
| `reduce_crossing_wall` | print | `avoid_crossing_perimeters` | boolean |
| `reduce_fan_stop_start_freq` | filament | `fan_always_on` | boolean |
| `resolution` | print | `resolution` | num |
| `retract_before_wipe` | print (machine base, per nozzle) | `retract_before_wipe` | percent |
| `retract_length_toolchange` | print (machine base, per nozzle) | `retract_length_toolchange` | num |
| `retract_lift_above` | print (machine base, per nozzle) | `retract_lift_above` | num |
| `retract_lift_below` | print (machine base, per nozzle) | `retract_lift_below` | num |
| `retract_restart_extra` | print (machine base, per nozzle) | `retract_restart_extra` | num |
| `retract_restart_extra_toolchange` | print (machine base, per nozzle) | `retract_restart_extra_toolchange` | num |
| `retract_when_changing_layer` | print (machine base, per nozzle) | `retract_layer_change` | boolean |
| `retraction_length` | print (machine base, per nozzle) | `retract_length` | num |
| `retraction_minimum_travel` | print (machine base, per nozzle) | `retract_before_travel` | num |
| `retraction_speed` | print (machine base, per nozzle) | `retract_speed` | num |
| `seam_gap` | print | `seam_gap_distance` | float_or_percent |
| `seam_position` | print | `seam_position` | enum: nearest->nearest, aligned->aligned, back->rear, random->random |
| `seam_slope_entire_loop` | print | `scarf_seam_entire_loop` | boolean |
| `seam_slope_inner_walls` | print | `scarf_seam_on_inner_perimeters` | boolean |
| `seam_slope_min_length` | print | `scarf_seam_length` | num |
| `seam_slope_start_height` | print | `scarf_seam_start_height` | percent_only |
| `seam_slope_type` | print | `scarf_seam_placement` | enum: none->nowhere, external->everywhere, all->everywhere |
| `single_extruder_multi_material` | printer | `single_extruder_multi_material` | boolean |
| `single_extruder_multi_material_priming` | print | `single_extruder_multi_material_priming` | boolean |
| `skirt_distance` | print | `skirt_distance` | num |
| `skirt_height` | print | `skirt_height` | integer |
| `skirt_loops` | print | `skirts` | integer |
| `slice_closing_radius` | print | `slice_closing_radius` | num |
| `slicing_mode` | print | `slicing_mode` | enum: regular->regular, even_odd->even_odd, close_holes->close_holes |
| `slow_down_for_layer_cooling` | filament | `cooling` | boolean |
| `slow_down_layer_time` | filament | `slowdown_below_layer_time` | integer |
| `slow_down_min_speed` | filament | `min_print_speed` | num |
| `small_perimeter_speed` | print | `small_perimeter_speed` | '%' resolved against Orca outer_wall_speed |
| `sparse_infill_acceleration` | print | `infill_acceleration` | '%' resolved against Orca default_acceleration |
| `sparse_infill_density` | print | `fill_density` | percent |
| `sparse_infill_line_width` | print | `infill_extrusion_width` | float_or_percent |
| `sparse_infill_pattern` | print | `fill_pattern` | enum: rectilinear->rectilinear, alignedrectilinear->alignedrectilinear, zigzag->zigzag, line->line, grid->grid, triangles->triangles, tri-hexagon->stars, cubic->cubic, adaptivecubic->adaptivecubic, supportcubic->supportcubic, lightning->lightning, honeycomb->honeycomb, 3dhoneycomb->3dhoneycomb, gyroid->gyroid, hilbertcurve->hilbertcurve, archimedeanchords->archimedeanchords, octagramspiral->octagramspiral, concentric->concentric |
| `sparse_infill_speed` | print | `infill_speed` | num |
| `spiral_mode` | print | `spiral_vase` | boolean |
| `staggered_inner_seams` | print | `staggered_inner_seams` | boolean |
| `standby_temperature_delta` | print | `standby_temperature_delta` | integer |
| `support_angle` | print | `support_material_angle` | num |
| `support_base_pattern_spacing` | print | `support_material_spacing` | num |
| `support_bottom_z_distance` | print | `support_material_bottom_contact_distance` | num |
| `support_interface_bottom_layers` | print | `support_material_bottom_interface_layers` | integer |
| `support_interface_pattern` | print | `support_material_interface_pattern` | enum: auto->auto, rectilinear->rectilinear, concentric->concentric |
| `support_interface_spacing` | print | `support_material_interface_spacing` | num |
| `support_interface_speed` | print | `support_material_interface_speed` | num |
| `support_interface_top_layers` | print | `support_material_interface_layers` | integer |
| `support_line_width` | print | `support_material_extrusion_width` | float_or_percent |
| `support_object_xy_distance` | print | `support_material_xy_spacing` | float_or_percent |
| `support_on_build_plate_only` | print | `support_material_buildplate_only` | boolean |
| `support_speed` | print | `support_material_speed` | num |
| `support_threshold_angle` | print | `support_material_threshold` | integer |
| `support_top_z_distance` | print | `support_material_contact_distance` | num |
| `template_custom_gcode` | printer | `template_custom_gcode` | text |
| `thick_bridges` | print | `thick_bridges` | boolean |
| `top_shell_layers` | print | `top_solid_layers` | integer |
| `top_shell_thickness` | print | `top_solid_min_thickness` | num |
| `top_surface_acceleration` | print | `top_solid_infill_acceleration` | num |
| `top_surface_line_width` | print | `top_infill_extrusion_width` | float_or_percent |
| `top_surface_pattern` | print | `top_fill_pattern` | enum: rectilinear->rectilinear, monotonic->monotonic, monotonicline->monotoniclines, alignedrectilinear->alignedrectilinear, concentric->concentric, hilbertcurve->hilbertcurve, archimedeanchords->archimedeanchords, octagramspiral->octagramspiral |
| `top_surface_speed` | print | `top_solid_infill_speed` | num |
| `travel_acceleration` | print | `travel_acceleration` | num |
| `travel_speed` | print | `travel_speed` | num |
| `travel_speed_z` | print | `travel_speed_z` | num |
| `tree_support_angle_slow` | print | `support_tree_angle_slow` | num |
| `tree_support_branch_angle_organic` | print | `support_tree_angle` | num |
| `tree_support_branch_diameter_angle` | print | `support_tree_branch_diameter_angle` | num |
| `tree_support_branch_diameter_double_wall` | print | `support_tree_branch_diameter_double_wall` | num |
| `tree_support_branch_diameter_organic` | print | `support_tree_branch_diameter` | num |
| `tree_support_branch_distance_organic` | print | `support_tree_branch_distance` | num |
| `tree_support_tip_diameter` | print | `support_tree_tip_diameter` | num |
| `tree_support_top_rate` | print | `support_tree_top_rate` | percent |
| `use_firmware_retraction` | printer | `use_firmware_retraction` | boolean |
| `use_relative_e_distances` | printer | `use_relative_e_distances` | boolean |
| `wall_distribution_count` | print | `wall_distribution_count` | integer |
| `wall_generator` | print | `perimeter_generator` | enum: classic->classic, arachne->arachne |
| `wall_loops` | print | `perimeters` | integer |
| `wall_transition_angle` | print | `wall_transition_angle` | num |
| `wall_transition_filter_deviation` | print | `wall_transition_filter_deviation` | float_or_percent |
| `wall_transition_length` | print | `wall_transition_length` | float_or_percent |
| `wipe` | print (machine base, per nozzle) | `wipe` | boolean |
| `wipe_tower_bridging` | print | `wipe_tower_bridging` | num |
| `wipe_tower_cone_angle` | print | `wipe_tower_cone_angle` | num |
| `wipe_tower_extra_flow` | print | `wipe_tower_extra_flow` | percent |
| `wipe_tower_extra_spacing` | print | `wipe_tower_extra_spacing` | percent |
| `wipe_tower_extruder` | print | `wipe_tower_extruder` | integer |
| `wipe_tower_no_sparse_layers` | print | `wipe_tower_no_sparse_layers` | boolean |
| `xy_contour_compensation` | print | `xy_size_compensation` | num |
| `z_hop` | print (machine base, per nozzle) | `retract_lift` | num |
| `z_offset` | printer | `z_offset` | num |

## Placeholder translation

Custom G-code (`start/end/layer/before-layer/toolchange/pause/template/between-objects`,
filament start/end, output file name) is translated by `placeholders.py`:

* Every identifier in `[...]` and `{...}` must be a 3.0 variable: a printer/print/filament config
  key (`config_schema.json`), `nozzle_diameter`, `nozzle_high_flow`, the runtime variables set in
  `libslic3r` (`layer_num`, `layer_z`, `max_layer_z`, `initial_extruder`, `initial_tool`,
  `print_bed_max`, `first_layer_print_min`, `total_layer_count`, ...), timestamp variables,
  `global` variables declared in the vendor's G-code, and for file names the export-name variables.
  `first_layer_speed` is removed from the 3.0 placeholder config and is therefore not valid.
* Orca setting keys whose value carries over unchanged are renamed to their 3.0 key (all 1:1 entries
  of the mapping table, e.g. `nozzle_temperature_initial_layer` -> `first_layer_temperature`,
  `outer_wall_acceleration` -> `external_perimeter_acceleration`, `pressure_advance` ->
  `pressure_advance_value`, `retraction_length` -> `retract_length`).
* Orca runtime variables: `bed_temperature_initial_layer_single` ->
  `first_layer_bed_temperature[initial_extruder]`, `bed_temperature_initial_layer` ->
  `first_layer_bed_temperature`, `printable_height` -> `max_print_height`, `curr_bed_type` ->
  `sheet_type`.
* Rewrite: `enable_pressure_advance[i] == "true"` / `enable_pressure_advance[i]` ->
  `(pressure_advance[i] != "disabled")` (3.0 has an enum instead of a bool).
* Approximate (listed in the report): `initial_no_support_extruder` -> `initial_extruder` (equal
  unless a dedicated support filament is used; all imported printers are single-tool).
* Orca keys whose value is converted (enums, `%` resolved, inverted bools ...) are reported as
  unresolved when used in G-code instead of being renamed blindly.

Anything else is reported as unresolved and marks the vendor report FAILED.

## Results (Anycubic 02.04.00.09, Elegoo 02.04.00.11)

| | Anycubic | Elegoo |
|---|---|---|
| printer families (Orca machine models) | 21 | 18 |
| printer configs (instantiated Orca machines) | 32 | 73 |
| print presets | 113 | 264 |
| filament presets | 174 | 219 |
| report status | FAILED (unresolved placeholders) | FAILED (unresolved placeholders) |

All 105 printer configs load in the app (`--query-printer-models`, no loader errors) and both
vendors appear in the Welcome dialog's "Choose a printer" list (checked in the GUI with a scratch
datadir). `e2e_test.py` sliced a 20 mm box on every config with the Orca default print/filament:
**77/105 pass** (export OK, no placeholder left, bed shape, height, first-layer nozzle/bed
temperature and retraction length equal to the Orca source). The 28 failures are all explained:

* 3.0 validation (Orca has no such check): Kobra Max, Kobra Plus, Vyper use relative E with
  Marlin but no `G92 E0` in layer G-code (`MissingG92E0`).
* Unresolved placeholders listed above: Kobra S1 Max (`additional_cooling_fan_speed`), Centauri /
  Centauri Carbon (default PLA filaments' start G-code uses air-filtration variables), Centauri 2 /
  Carbon 2 (adaptive bed mesh), OrangeStorm Giga (`parallel_printheads_count`).
* Predator: no Orca filament lists it in `compatible_printers`.

Centauri Carbon 0.4 slices with `Elegoo PETG @ECC` (no air-filtration G-code): start G-code
`M140 S70`, `M190 S70`, `M109 S250`, `;curr_bed_type:Textured PEI Plate`, retraction 0.8 mm,
`M204 S5000` - all equal to the Orca source (default bed type 4 = Textured PEI, PETG 250 °C /
textured plate 70 °C, `retraction_length` 0.8, `outer_wall_acceleration` 5000).
Kobra 3 0.4: `G9111 bedTemp=60 extruderTemp=220`, `M900 K0.051`, retraction 1 mm at 60 mm/s,
z-hop 0.4 above 0.3 mm, bed 255x255, height 260 - equal to the Orca source.

## Known gaps

* **Unresolved placeholders** (see reports): Orca-only runtime variables in tool-change G-code
  (`flush_length`, `old/new_filament_temp`, `old/new_filament_e_feedrate`,
  `x/y_after_toolchange`, `nozzle_temperature_range_high`), adaptive bed mesh
  (`adaptive_bed_mesh_min/max`, `bed_mesh_probe_count`, `bed_mesh_algo`: Centauri 2 / Carbon 2),
  `parallel_printheads_count` (OrangeStorm Giga), `additional_cooling_fan_speed` (Kobra S1 Max),
  air filtration variables in some Elegoo filament start G-code. These G-code blocks are kept as
  Orca wrote them; slicing fails only when such a block is actually executed (tool-change blocks
  never run on the imported single-tool, feeder-less configs).
* **Multi-material** (ACE / CANVAS): Orca's single-extruder multi-material machines are imported
  as single-tool printers without a feeder; no slot count exists in Orca's data to build one.
* **Dropped behaviour with no 3.0 setting**: jerk limits, Orca `crosshatch`/other new infill
  patterns (3.0 default used), `auto_brim`/`brim_ears`, adaptive pressure advance, per-role
  filament selection, wipe distance, z-hop surface enforcement, auxiliary/exhaust fans, chamber
  control flags, bed exclude areas, Orca `overhang_fan_speed`, `ensure_moderate`/`critical_only`
  vertical shell modes, tree support wall count, and Klipper machine-limit emission.
* `compatible_printers_condition` / `compatible_prints(_condition)` are not translated (none of
  the two vendors' instantiated presets depend on them for compatibility).
* 3.0's own defaults apply to settings Orca has no counterpart for.

## Fallbacks that change behaviour

Values Orca computes at slicing time that 3.0 has no data for. The import uses the value below,
lists it in the vendor report and keeps the rest of the G-code exact.

| Orca variable / setting | Value used in 3.0 | Why |
|---|---|---|
| `max_print_z` | `max_print_height` | 3.0 has no total print height at print start (Orca: ceil of the tallest object) |
| `hold_chamber_temp_for_flat_print` | `false` | needs first-layer area and object height (Orca: true only below 0.3 mm with > 40000 mm² first layer) |
| `curr_y_acceleration_limit` | `machine_max_acceleration_y` | Orca's bed-mass model; it returns this value unless the A2L mass keys are set |
| `wipe_tower_center_pos_valid/x/y` | `false` / 0,0 | exact without a wipe tower; with one, 3.0 has no wipe-tower bounding box for G-code |
| `has_tpu_in_first_layer` | any used filament is TPU | Orca checks the filaments used on the first layer |
| COLPIC / BTT_TFT thumbnail entries | left out | 3.0 writes PNG, JPG and QOI only |
| `emit_machine_limits_to_gcode` with Klipper | `machine_limits_usage = time_estimate_only` | 3.0 refuses to emit machine limits for Klipper (Orca emits SET_VELOCITY_LIMIT) |
| Missing `G92 E0` with relative E (Marlin) | `G92 E0` appended to layer G-code | 3.0 MissingG92E0 rule |
| `G92 E0` in (before-)layer G-code with absolute E | line removed | 3.0 FoundG92E0 rule; the slicer tracks E itself |
| `initial_no_support_extruder` / `_filament_id` | `initial_extruder` | equal unless a support-only filament is used |

## Next steps / test strategy

Planned order: the multi-material work and a C++ start-up fix come first (the printer picker lists
printers from the vendor templates; only printers the user adds are built and evaluated, so start-up
no longer evaluates every bundled printer). The import work resumes after that.

1. Converting is fast; the slow part is the per-config e2e. Each test launch used to pay ~100 s for
   evaluating every bundled vendor. After the start-up fix, measure the new per-config time first,
   then plan the runs.
2. Re-test only what changed: the converter is deterministic, so after a converter change diff the
   output per vendor and re-test only vendors whose files changed.
3. App changes (multi-material layer, start-up fix) get a smoke test: one config per printer family,
   not the full suite.
4. Exactly one full e2e run at the very end, in the background, as the final gate before the import
   is committed. No repeated full runs.
5. Remaining work: re-convert all vendors with the newest converter; re-test the vendors that failed
   or were not re-tested yet (BBL, Creality, SeeMeCNC, Meltingplot, FELIX Printers, re3D, DeltaMaker,
   Elegoo, LONGER, Vivedino, M3D, Snapmaker); fix; update this document (results per vendor, C++
   list, multi-material list); then the single final full run.
