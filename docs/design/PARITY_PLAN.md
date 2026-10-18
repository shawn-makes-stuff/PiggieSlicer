# Orca parity plan (remaining settings)

Source of truth for the keys: `docs/design/ORCA_SETTINGS_AUDIT.md` (regenerated 2026-10-18 after
parity batches 1–6: Process 181, Filament 31, Printer 66 not mapped). Rules as for every port: copy
Orca's logic from its source (v2.5.0-alpha, `PrintConfig.cpp` for the definition and default, the
using code for the behaviour); native (Prusa) profiles use the new features too (user decision
2026-10-18); the converter writes Orca's values. Each port: definition (ConfigDefsFDM.cpp), steps
invalidation, behaviour, converter mapping, a test where the behaviour is G-code visible.
No full builds until the user clears one; single .cpp files are compile-checked as they change
(`tools/dev/cc.sh`, MSBuild ClCompile; statuses with `tools/dev/parity_status.py`).

Brim / skirt (user decision 2026-10-18): Orca's design replaces 3.0's shared brim and skirt: brims and
skirts per object instance, printed with the object, auto brim width, ears, skirt/brim groups. Painted
ears (gizmo, model points) come last; skipped only if very hard, after telling the user.
Internal solid infill follows `internal_solid_infill_pattern` as in Orca; 3.0's Ensuring is one of its choices and the default (3.0 forced it), and Ensuring areas are not split by the narrow detection.

Status: `todo` / `done` (written, compile-checked) / `skip` (out of scope, agreed 2026-10-18) /
`map` (3.0 has it: converter only).

## Out of scope (skip)
Belt printers (`belt_*`, `enable_belt_purge_tower`, `belt_purge_tower_width`), IDEX/IMEX (`is_imex`,
`imex_*`), machine frame transforms (`gcode_remap_*`, `build_plate_tilt_*`, `belt_frame_*`),
plugins (`*_plugin_config_overrides`, `slicing_pipeline_plugin`), pellet printers
(`pellet_modded_printer`, `pellet_flow_coefficient`), spaghetti / clumping detection
(`spaghetti_detector`, `enable_wrapping_detection`, `wrapping_detection_gcode`,
`wrapping_exclude_area`), Bambu cloud / hardware specifics (`printer_agent`, `use_3mf`,
`scan_first_layer`, `nozzle_volume_type`, `nozzle_volume`, `cooling_filter_enabled`,
`parallel_printheads_count`, `extruder_printable_area`, `extruder_printable_height`,
`long_retractions_when_ec`, `retraction_distances_when_ec`, `long_retractions_when_cut`,
`retraction_distances_when_cut`, `filament_change_length`, `best_object_pos`,
`preferred_orientation`).

## Tiers (work order)
1. **Mappings**: 3.0 has the setting under another name / meaning (verify both definitions).
2. **G-code level**: fans, temperatures, accelerations, speeds, G-code blocks, placeholders,
   comments — GCode.cpp / GCodeWriter / CoolingBuffer.
3. **Toolpath level**: walls, seams, wipes, ironing, bridges, top/bottom surfaces, skirt / brim,
   spiral vase, fuzzy skin — PerimeterGenerator, Fill, GCode extrusion.
4. **Slicing / support / tower**: precision (polyholes, precise outer wall / Z), overhang shaping,
   support details, prime tower variants, infill patterns new to 3.0 (TPMS, lateral lattice,
   multiline, skin/skeleton), Z contouring.

The per-key list with status follows (filled in as each key is checked against both sources).

## Keys
Counts: tier - note: 12, tier - skip: 49, tier 1 done: 4, tier 1 note: 2, tier 2 done: 53, tier 2 note: 2, tier 2 skip: 2, tier 2 todo: 4, tier 3 done: 22, tier 3 todo: 55, tier 4 done: 2, tier 4 todo: 73

| tab | page | Orca key | type | label | Orca default | tier | status | note |
|---|---|---|---|---|---|---|---|---|
| Process | Quality | enable_mixed_color_sublayer | coBool | Mixed color sublayer | Bool(false) | 4 | todo |  |
| Process | Quality | bridge_line_width | coFloatOrPercent | Bridge | FloatOrPercent(100., true) | 3 | done | bridging_flow |
| Process | Quality | seam_slope_conditional | coBool | Conditional scarf joint | Bool(false) | 3 | done | = scarf_seam_only_on_smooth, now Orca's whole-loop rule |
| Process | Quality | scarf_angle_threshold | coInt | Conditional angle threshold | Int(155) | 3 | done | SeamPlacer: vertex angles over a nozzle-diameter arm |
| Process | Quality | scarf_overhang_threshold | coPercent | Conditional overhang threshold | Percent(40) | 3 | done | SeamPlacer: seam distance outside the lower layer |
| Process | Quality | scarf_joint_speed | coFloatOrPercent | Scarf joint speed | FloatOrPercent(100, true) | 2 | done | _extrude sloped part; % of wall speed |
| Process | Quality | seam_slope_steps | coInt | Scarf steps | Int(10) | 3 | done | scarf segment length <= length / steps |
| Process | Quality | scarf_joint_flow_ratio | coFloat | Scarf joint flow ratio | Float(1) | 2 | done | _extrude sloped part |
| Process | Quality | wipe_on_loops | coBool | Wipe on loops | Bool(false) | 3 | done | the external perimeter inward move (3.0: only with wipe off) |
| Process | Quality | wipe_inward | coBool | Wipe inward | Bool(false) | 3 | done | WipePathHelpers port; non-retracting wipe after external loops |
| Process | Quality | wipe_inward_distance | coFloatOrPercent | Wipe inward distance | FloatOrPercent(50, true) | 3 | done | with wipe_inward |
| Process | Quality | wipe_before_external_loop | coBool | Wipe before external loop | Bool(false) | 3 | done | extrude_perimeters: de-retract at Orca's inside point, move to the start without extruding |
| Process | Quality | elefant_foot_layers_density | coPercent | Elephant foot layers density | Percent(100) | 3 | done | make_fills |
| Process | Quality | precise_outer_wall | coBool | Precise wall | Bool{true} | 4 | done | classic ext spacing + Arachne offset / wall_0_inset (inner walls first) |
| Process | Quality | precise_z_height | coBool | Precise Z height | Bool(0) | 4 | done | Slicing generate_object_layers adjust (last 5 layers) |
| Process | Quality | hole_to_polyhole | coBool | Convert holes to polyholes | Bool(false) | 4 | done | PrintObject::transform_hole_to_polyholes |
| Process | Quality | hole_to_polyhole_threshold | coFloatOrPercent | Polyhole detection margin | FloatOrPercent(0.01, false) | 4 | done | with hole_to_polyhole |
| Process | Quality | hole_to_polyhole_twisted | coBool | Polyhole twist | Bool(true) | 4 | done | with hole_to_polyhole |
| Process | Quality | hole_to_polyhole_max_edges | coInt | Maximum Polyhole edge count | Int(50) | 4 | done | with hole_to_polyhole |
| Process | Quality | ironing_pattern | coEnum | Ironing Pattern | Enum<InfillPattern>(ipRectilinear) | 3 | done | make_ironing filler per pattern |
| Process | Quality | ironing_inset | coFloat | Ironing inset | Float(0) | 3 | done | make_ironing area offset |
| Process | Quality | ironing_angle | coFloat | Ironing angle offset | Float(0) | 3 | done | offset from the fill angle (+45°) or absolute |
| Process | Quality | ironing_angle_fixed | coBool | Fixed ironing angle | Bool(false) | 3 | done | Fill::fixed_angle (no per-layer alternation) |
| Process | Quality | zaa_enabled | coBool | Z contouring enabled | Bool(false) | 4 | todo |  |
| Process | Quality | zaa_minimize_perimeter_height | coFloat | Minimize wall height angle | Float(35) | 4 | todo |  |
| Process | Quality | zaa_min_z | coFloat | Minimum Z height | Float(0.05) | 4 | todo |  |
| Process | Quality | zaa_dont_alternate_fill_direction | coBool | Don't alternate fill direction | Bool(false) | 4 | todo |  |
| Process | Quality | ironing_expansion | coFloat | Ironing expansion | Float(0) | 4 | todo |  |
| Process | Quality | initial_layer_min_bead_width | coPercent | First layer minimum wall width | Percent(85) | 3 | done | WallToolPaths min bead width on the first layer |
| Process | Quality | min_length_factor | coFloat | Minimum wall length | Float(0.5) | 3 | done | WallToolPaths::removeSmallLines (not bottom / topmost layers) |
| Process | Quality | wall_maximum_resolution | coFloat | Maximum wall resolution | Float(0.5f) | 3 | done | WallToolPaths (was meshfix constant) |
| Process | Quality | wall_maximum_deviation | coFloat | Maximum wall deviation | Float(0.025f) | 3 | done | WallToolPaths (was meshfix constant) |
| Process | Quality | wall_direction | coEnum | Wall loop direction | Enum<WallDirection>(WallDirection::CounterClockwise) | 3 | done | converter -> 3.0 prefer_clockwise_movements |
| Process | Quality | bottom_solid_infill_flow_ratio | coFloat | Bottom surface flow ratio | Float(1) | - | note | 3.0 has no bottom-surface extrusion role (bottom solid infill is SolidInfill) |
| Process | Quality | min_width_top_surface | coFloatOrPercent | One wall threshold | FloatOrPercent(300, true) | 3 | done | single top wall threshold (classic + Arachne) |
| Process | Quality | small_area_infill_flow_compensation | coBool | Small area flow compensation (beta) | Bool(false) | 3 | done | GCode/SmallAreaInfillFlowCompensator in _extrude (solid infill, rectilinear/monotonic) |
| Process | Quality | small_area_infill_flow_compensation_model | coStrings | Flow Compensation Model | Strings{"0,0", "\n0.2,0.4444", "\n0.4,0.6145", "\n0.6,0.7059", " | 3 | done | validated in Print::validate |
| Process | Quality | internal_bridge_flow | coFloat | Internal bridge flow ratio | Float(1) | 3 | done | _extrude flow ratio |
| Process | Quality | bridge_density | coPercent | External bridge density | Percent(100) | 3 | done | make_fills |
| Process | Quality | internal_bridge_density | coPercent | Internal bridge density | Percent(100) | 3 | done | make_fills |
| Process | Quality | thick_internal_bridges | coBool | Thick internal bridges | Bool(true) | 3 | done | bridging_flow thick flag |
| Process | Quality | enable_extra_bridge_layer | coEnum | Extra bridge layers (beta) | Enum<EnableExtraBridgeLayer>(eblDisabled) | 3 | done | detect_surfaces_type + bridge_over_infill |
| Process | Quality | dont_filter_internal_bridges | coEnum | Filter out small internal bridges | Enum<InternalBridgeFilter>(ibfDisabled) | 3 | done | bridge_over_infill |
| Process | Quality | counterbore_hole_bridging | coEnum | Bridge counterbore holes | Enum<CounterboreHoleBridgingOption>(chbNone) | 3 | done | process_no_bridge per slice + precise BridgeDetector coverage |
| Process | Quality | detect_overhang_wall | coBool | Detect overhang walls | Bool(true) | 1 | done | → overhangs |
| Process | Quality | unsupported_wall_last | coBool | Print unsupported walls last | Bool(false) | 3 | done | defer_unsupported_loops + extrude_slices after infill |
| Process | Quality | make_overhang_printable | coBool | Make overhangs printable | Bool(false) | 4 | done | PrintObject::apply_conical_overhang |
| Process | Quality | make_overhang_printable_angle | coFloat | Make overhangs printable - Maximum angle | Float(55.) | 4 | done | with make_overhang_printable |
| Process | Quality | make_overhang_printable_hole_size | coFloat | Make overhangs printable - Hole area | Float(0.) | 4 | done | with make_overhang_printable |
| Process | Quality | overhang_reverse | coBool | Reverse on even | Bool(false) | 3 | done | steep overhang detection, ExtrusionLoop::reverse_direction in ExtrusionOrder |
| Process | Quality | overhang_reverse_internal_only | coBool | Reverse only internal perimeters | Bool(false) | 3 | done | reorient_perimeters |
| Process | Quality | overhang_reverse_threshold | coFloatOrPercent | Reverse threshold | FloatOrPercent(50, true) | 3 | done | detect_steep_overhang |
| Process | Strength | alternate_extra_wall | coBool | Alternate extra wall | Bool(false) | 3 | done | PerimeterGenerator loop_number on odd layers |
| Process | Strength | top_surface_density | coPercent | Top surface density | Percent(100) | 3 | done | group_fills; 0% leaves it out |
| Process | Strength | top_surface_fill_order | coEnum | Top surface fill order | Enum<SurfaceFillOrder>(SurfaceFillOrder::Default) | 3 | done | FillConcentric/FillPlanePath + no_sort |
| Process | Strength | top_layer_direction | coFloat | Top layer direction | Float(-1) | 3 | done | fixed angle (Fill::fixed_angle) |
| Process | Strength | top_surface_expansion | coFloat | Top surface expansion | Float(0) | 3 | done | detect_surfaces_type per island |
| Process | Strength | top_surface_expansion_margin | coFloat | Top expansion wall margin | Float(0) | 3 | done | with top_surface_expansion |
| Process | Strength | top_surface_expansion_direction | coEnum | Top expansion direction | Enum<TopSurfaceExpansionDirection>(TopSurfaceExpansionDirection: | 3 | done | with top_surface_expansion |
| Process | Strength | bottom_surface_density | coPercent | Bottom surface density | Percent(100) | 3 | done | group_fills |
| Process | Strength | bottom_surface_fill_order | coEnum | Bottom surface fill order | Enum<SurfaceFillOrder>(SurfaceFillOrder::Default) | 3 | done | same as top |
| Process | Strength | bottom_layer_direction | coFloat | Bottom layer direction | Float(-1) | 3 | done | fixed angle |
| Process | Strength | center_of_surface_pattern | coEnum | Center surface pattern on | Enum<CenterOfSurfacePattern>(CenterOfSurfacePattern::Each_Surfac | 3 | done | FillPlanePath; per-body with separated_infills |
| Process | Strength | top_bottom_infill_wall_overlap | coPercent | Top/Bottom solid infill/wall overlap | Percent(25) | 3 | done | first/topmost layers and single wall top fills |
| Process | Strength | fill_multiline | coInt | Fill Multiline | Int(1) | 4 | done | batches 52-56: all of 3.0's multiline-capable fills (Rectilinear family trapezoidal, Gyroid, 3D Honeycomb, Adaptive / Support Cubic non-crossing paths) |
| Process | Strength | tpms_adaptive | coEnum | Adaptive density (experimental) | Enum<TpmsAdaptiveMode>(TpmsAdaptiveMode::Disabled) | 4 | done | batch 60 TpmsRadialField + FillTpmsAdaptive |
| Process | Strength | tpms_interior_density | coPercent | Interior density | Percent(5) | 4 | done | batch 60 |
| Process | Strength | tpms_adaptive_gradient | coEnum | Adaptive gradient | Enum<TpmsAdaptiveGradient>(TpmsAdaptiveGradient::Linear) | 4 | done | batch 60 |
| Process | Strength | gyroid_optimized | coBool | Z-buckling bias optimization (experimental) | Bool(false) | 4 | done | batch 60 Orca's Gyroid (marching squares variant) |
| Process | Strength | sparse_infill_smooth_factor | coPercent | Sparse infill smooth factor | Percent(0) | 4 | done | batch 52 FillCornerSmoothing |
| Process | Strength | sparse_infill_rotate_template | coString | Sparse infill rotation template | String("") | 4 | done | Fill.cpp calculate_infill_rotation_angle (Orca's bridge-over-infill coupling for Hilbert/Octagram not ported: part of Orca's reworked algorithm) |
| Process | Strength | skin_infill_density | coPercent | Skin infill density | Percent(25) | 4 | done | batch 61 LockRegionParam |
| Process | Strength | skeleton_infill_density | coPercent | Skeleton infill density | Percent(25) | 4 | done | batch 61 |
| Process | Strength | infill_lock_depth | coFloat | Infill lock depth | Float(1.0) | 4 | done | batch 61 |
| Process | Strength | skin_infill_depth | coFloat | Skin infill depth | Float(2.0) | 4 | done | batch 61 |
| Process | Strength | skin_infill_line_width | coFloatOrPercent | Skin line width | FloatOrPercent(100, true) | 4 | done | batch 61 |
| Process | Strength | skeleton_infill_line_width | coFloatOrPercent | Skeleton line width | FloatOrPercent(100, true) | 4 | done | batch 61 |
| Process | Strength | symmetric_infill_y_axis | coBool | Symmetric infill Y axis | Bool(false) | 4 | done | batch 61 |
| Process | Strength | infill_complete_top | coBool | Fill pattern tops | Bool(false) | 4 | done | batch 55 Orca 3D Honeycomb generator; 3.0's kept as 3D Honeycomb (classic) |
| Process | Strength | infill_shift_step | coFloat | Infill shift step | Float(0.4) | 4 | done | batch 61 Cross / Locked Zag |
| Process | Strength | lateral_lattice_angle_1 | coFloat | Lateral lattice angle 1 | Float(-45) | 4 | done | batch 57 Lateral Lattice |
| Process | Strength | lateral_lattice_angle_2 | coFloat | Lateral lattice angle 2 | Float(45) | 4 | done | batch 57 Lateral Lattice |
| Process | Strength | infill_overhang_angle | coFloat | Infill overhang angle | Float(60) | 4 | done | batch 57 Lateral Honeycomb |
| Process | Strength | lightning_overhang_angle | coFloat | Lightning overhang angle | Float(45) | 4 | done | Lightning Generator |
| Process | Strength | lightning_prune_angle | coFloat | Prune angle | Float(45) | 4 | done | Lightning Generator |
| Process | Strength | lightning_straightening_angle | coFloat | Straightening angle | Float(45) | 4 | done | Lightning Generator |
| Process | Strength | internal_solid_infill_pattern | coEnum | Internal solid infill pattern | Enum<InfillPattern>(ipMonotonic) | 3 | done | group_fills (internal solid, solid over bridge); + 3.0 Ensuring, the default |
| Process | Strength | solid_infill_direction | coFloat | Solid infill direction | Float(45) | 3 | done | group_fills solid angle |
| Process | Strength | solid_infill_rotate_template | coString | Solid infill rotation template | String("") | 4 | done | solid + ironing + internal solid fills |
| Process | Strength | filter_out_gap_fill | coFloat | Filter out tiny gaps | Float(0) | 3 | done | classic perimeter gap fill (3.0 has no solid infill gap fill) |
| Process | Strength | separated_infills | coBool | Separated infills | Bool(false) | 4 | done | batch 62 connected bodies, per body boxes and octrees |
| Process | Strength | align_infill_direction_to_model | coBool | Align directions to model | Bool(false) | 4 | done | infill, ironing, bridges, internal bridges, counterbore detection |
| Process | Strength | extra_solid_infills | coString | Insert solid layers | String() | 4 | done | batch 63 |
| Process | Strength | internal_bridge_angle | coFloat | Internal bridge infill direction | Float(0.) | 3 | done | bridge_over_infill |
| Process | Strength | relative_bridge_angle | coBool | Relative bridge angle | Bool(false) | 3 | done | LayerRegion + bridge_over_infill |
| Process | Strength | detect_narrow_internal_solid_infill | coBool | Detect narrow internal solid infills | Bool(true) | 3 | done | split_solid_surface + FillConcentricInternal; internal solids follow internal_solid_infill_pattern (Ensuring default, not split) |
| Process | Speed | small_support_perimeter_speed | coFloatsOrPercents | Small support perimeters | FloatsOrPercents{FloatOrPercent(50, true)} | 2 | done | extrude_support; % of support/interface speed |
| Process | Speed | small_support_perimeter_threshold | coFloats | Small support perimeters threshold | Floats{0} | 2 | done | 2π×threshold length test |
| Process | Speed | slowdown_for_curled_perimeters | coBools | Slow down for curled perimeters | Bools{ false } | 2 | done | Tool-overridable; 3.0 default on; off skips curled lines (estimation + overhang speed) |
| Process | Speed | internal_bridge_speed | coFloatsOrPercents | Internal | FloatsOrPercents{FloatOrPercent(150, true)} | 2 | done | InternalBridgeInfill role, % of bridge speed |
| Process | Speed | initial_layer_travel_acceleration | coFloatsOrPercents | First layer travel | FloatsOrPercents{FloatOrPercent(100, true)} | 2 | done | batch 8 |
| Process | Speed | accel_to_decel_enable | coBool | Enable accel_to_decel | Bool(true) | 2 | done | batch 8 (default off: 3.0 output) |
| Process | Speed | accel_to_decel_factor | coPercent | accel_to_decel | Percent(50) | 2 | done | batch 8 |
| Process | Speed | default_junction_deviation | coFloats | Junction Deviation | Floats{0.f} | 2 | done | batch 8 |
| Process | Speed | initial_layer_travel_jerk | coFloatsOrPercents | First layer travel | FloatsOrPercents{FloatOrPercent(100, true)} | 2 | done | batch 8 (with Orca's jerk set) |
| Process | Speed | max_volumetric_extrusion_rate_slope_segment_length | coFloat | Smoothing segment length | Float(3.0) | 2 | done | PressureEqualizer member; 3.0 default 5 |
| Process | Speed | extrusion_rate_smoothing_external_perimeter_only | coBool | Apply only on external features | Bool(false) | 2 | done | PressureEqualizer role check |
| Process | Support | support_threshold_overlap | coFloatOrPercent | Threshold overlap | FloatOrPercent(50., true) | 4 | done | batch 64 (SupportMaterial + organic tree; 50% = 3.0's half width) |
| Process | Support | support_critical_regions_only | coBool | Support critical regions only | Bool(false) | - | note | as tree_support_branch_distance (Bambu tree) |
| Process | Support | support_remove_small_overhang | coBool | Ignore small overhangs | Bool(true) | 4 | deferred | needs BBS normal support's sharp tail / cantilever detection and overhang clusters (~400 lines); Orca default on would change 3.0's stock supports; ask the user |
| Process | Support | enforce_support_layers | coInt | Enforce support for the first | Int(0) | 1 | done | → support_material_enforce_layers |
| Process | Support | support_interface_not_for_body | coBool | Avoid interface filament for base | Bool(true) | 4 | done | batch 64 ToolOrdering (support collected after the object, as Orca) |
| Process | Support | support_ironing | coBool | Ironing Support Interface | Bool(false) | 4 | done | batch 64 (top contacts ironed, solid) |
| Process | Support | support_ironing_pattern | coEnum | Support Ironing Pattern | Enum<InfillPattern>(ipRectilinear) | 4 | done | batch 64 |
| Process | Support | support_ironing_flow | coPercent | Support Ironing flow | Percent(10) | 4 | done | batch 64 |
| Process | Support | support_ironing_spacing | coFloat | Support Ironing line spacing | Float(0.1) | 4 | done | batch 64 |
| Process | Support | tree_support_wall_count | coInt | Support wall loops | Int(0) | - | note | as tree_support_branch_distance |
| Process | Support | support_bottom_interface_spacing | coFloat | Bottom interface spacing | Float(0.5) | 4 | done | batch 64 bottom_interface_density |
| Process | Support | support_expansion | coFloat | Normal support expansion | Float(0) | 4 | done | batch 64 |
| Process | Support | support_interface_loop_pattern | coBool | Loop pattern interface | Bool(false) | 1 | done | → support_material_interface_contact_loops |
| Process | Support | support_object_first_layer_gap | coFloat | Support/object first layer gap | Float(0.2) | 4 | done | batch 64 (1st layer trimming) |
| Process | Support | max_bridge_length | coFloat | Max bridge length | Float(10) | - | note | as tree_support_branch_distance (Bambu tree) |
| Process | Support | independent_support_layer_height | coBool | Independent support layer height | Bool(true) | 1 | note | 3.0 synchronizes only soluble supports (zero contact distance); Orca's default (independent) is 3.0's behaviour |
| Process | Support | tree_support_branch_distance | coFloat | Tree support branch distance | Float(5.) | - | note | Orca's non-organic (Bambu) tree support; 3.0's tree is organic (the *_organic keys are mapped) |
| Process | Support | tree_support_branch_diameter | coFloat | Tree support branch diameter | Float(5.) | - | note | as tree_support_branch_distance |
| Process | Support | tree_support_branch_angle | coFloat | Tree support branch angle | Float(40.) | - | note | as tree_support_branch_distance |
| Process | Support | tree_support_auto_brim | coBool | Auto brim width | Bool(1) | - | note | as tree_support_branch_distance |
| Process | Support | tree_support_brim_width | coFloat | Tree support brim width | Float(3) | - | note | as tree_support_branch_distance |
| Process | Multimaterial | prime_tower_skip_points | coBool | Skip points | Bool(true) | 4 | todo |  |
| Process | Multimaterial | enable_tower_interface_features | coBool | Enable tower interface features | Bool(false) | 4 | todo |  |
| Process | Multimaterial | enable_tower_interface_cooldown_during_tower | coBool | Cool down from interface boost during prime tower | Bool(false) | 4 | todo |  |
| Process | Multimaterial | prime_tower_enable_framework | coBool | Internal ribs | Bool(false) | 4 | todo |  |
| Process | Multimaterial | belt_purge_tower_width | coFloat | Belt purge tower width | Float(35.) | - | skip | out of scope |
| Process | Multimaterial | prime_tower_infill_gap | coPercent | Infill gap | Percent(150) | 4 | todo |  |
| Process | Multimaterial | wipe_tower_rotation_angle | coFloat | Wipe tower rotation angle | Float(0.) | 4 | note | 3.0 keeps the tower rotation in the model (rotated in the 3D scene) |
| Process | Multimaterial | wipe_tower_max_purge_speed | coFloat | Maximum wipe tower print speed | Float(90.) | 4 | todo |  |
| Process | Multimaterial | wipe_tower_wall_type | coEnum | Wall type | Enum<WipeTowerWallType>(wtwRib) | 4 | todo |  |
| Process | Multimaterial | wipe_tower_extra_rib_length | coFloat | Extra rib length | Float(0) | 4 | todo |  |
| Process | Multimaterial | wipe_tower_rib_width | coFloat | Rib width | Float(8) | 4 | todo |  |
| Process | Multimaterial | wipe_tower_fillet_wall | coBool | Fillet wall | Bool(true) | 4 | todo |  |
| Process | Multimaterial | wipe_tower_sparse_layers_combination | coBool | Combine sparse layers | Bool(false) | 4 | todo |  |
| Process | Multimaterial | outer_wall_filament_id | coInt | Outer walls | Int(0) | 4 | todo |  |
| Process | Multimaterial | top_surface_filament_id | coInt | Top surface | Int(0) | 4 | todo |  |
| Process | Multimaterial | bottom_surface_filament_id | coInt | Bottom surface | Int(0) | 4 | todo |  |
| Process | Multimaterial | preheat_time | coFloat | Preheat time | Float(30.0) | 2 | done | drives 3.0's M104 backtrace; on with ooze prevention + multi-tool, or tool-preheating printers; 3.0 default 120 |
| Process | Multimaterial | preheat_steps | coInt | Preheat steps | Int(1) | 2 | done | backtrace steps; 3.0 default 10 |
| Process | Multimaterial | flush_into_infill | coBool | Flush into objects' infill | Bool(false) | - | note | 3.0's wipe_into_* are object / volume settings, not print-preset settings |
| Process | Multimaterial | flush_into_objects | coBool | Flush into this object | Bool(false) | - | note | as flush_into_infill |
| Process | Multimaterial | flush_into_support | coBool | Flush into objects' support | Bool(true) | - | note | as flush_into_infill |
| Process | Multimaterial | toolchange_ordering | coEnum | Toolchange ordering | Enum<ToolChangeOrderingType>(ToolChangeOrderingType::Default) | 1 | done | → toolchange_ordering (default→optimized, cyclic→cyclic) |
| Process | Multimaterial | toolchange_cyclic_order | coString | Cyclic order | String("") | 2 | done | ToolOrdering::reorder_extruders |
| Process | Multimaterial | toolchange_cyclic_first_layer | coBool | Apply cyclic order to first layer | Bool(false) | 2 | done | first layer otherwise optimized |
| Process | Others | skirt_type | coEnum | Skirt type | Enum<SkirtType>(stCombined) | 3 | done | skirt/brim groups, per object emission at the first visit |
| Process | Others | skirt_start_angle | coFloat | Skirt start point | Float(-135) | 3 | done | ExtrusionOrder: first skirt loop's seam point on the first layer |
| Process | Others | single_loop_draft_shield | coBool | Single loop after first layer | Bool(false) | 3 | done | ExtrusionOrder: last loop above the first layer |
| Process | Others | leading_brim_length | coFloat | Leading brim length | Float(0.) | 3 | skip | belt printers only (out of scope) |
| Process | Others | extra_brim_width | coFloat | Extra brim width | Float(0.) | 3 | skip | belt printers only (out of scope) |
| Process | Others | brim_flow_ratio | coFloat | Brim flow ratio | Float(1) | 3 | done | Brim role, _extrude flow ratio |
| Process | Others | brim_use_efc_outline | coBool | Brim follows compensated outline | Bool(false) | 3 | done | Brim.cpp use_brim_efc_outline |
| Process | Others | combine_brims | coBool | Combine brims | Bool(false) | 3 | done | Print::_make_skirt skirt/brim groups |
| Process | Others | brim_ears_max_angle | coFloat | Brim ear max angle | Float(125) | 3 | done | Brim.cpp make_brim_ears_auto (brim_type brim_ears) |
| Process | Others | brim_ears_detection_length | coFloat | Brim ear detection radius | Float(1) | 3 | done | with brim ears |
| Process | Others | brim_ears_outer_only | coBool | Brim ears outer only | Bool(false) | 3 | done | with brim ears |
| Process | Others | print_order | coEnum | Intra-layer order | Enum<PrintOrder>(PrintOrder::Default) | 3 | done | OrderingStrategies port; island tours per extruder in ExtrusionOrder (instance visits) |
| Process | Others | spiral_mode_smooth | coBool | Smooth Spiral | Bool(false) | 3 | done | SpiralVase; 3.0 default on |
| Process | Others | spiral_mode_max_xy_smoothing | coFloatOrPercent | Max XY Smoothing | FloatOrPercent(200, true) | 3 | done | % of the largest nozzle; 3.0 default 200% |
| Process | Others | spiral_starting_flow_ratio | coFloat | Spiral starting flow ratio | Float(0) | 3 | done | SpiralVase transition in |
| Process | Others | spiral_finishing_flow_ratio | coFloat | Spiral finishing flow ratio | Float(0) | 3 | done | SpiralVase transition out |
| Process | Others | timelapse_type | coEnum | Timelapse | Enum<TimelapseType>(tlTraditional) | 2 | done | batch 10 (smooth needs the tower's timelapse: tier 4) |
| Process | Others | enable_wrapping_detection | coBool | Enable clumping detection | Bool(false) | - | skip | out of scope |
| Process | Others | fuzzy_skin_mode | coEnum | Fuzzy skin generator mode | Enum<FuzzySkinMode>(FuzzySkinMode::Displacement) | 3 | done | FuzzySkin fuzzy_extrusion_line modes |
| Process | Others | fuzzy_skin_noise_type | coEnum | Fuzzy skin noise type | Enum<NoiseType>(NoiseType::Classic) | 3 | done | libnoise vendored (bundled_deps/libnoise) + ripple |
| Process | Others | fuzzy_skin_scale | coFloat | Fuzzy skin feature size | Float(1.0) | 3 | done | noise frequency |
| Process | Others | fuzzy_skin_octaves | coInt | Fuzzy Skin Noise Octaves | Int(4) | 3 | done | noise octaves |
| Process | Others | fuzzy_skin_persistence | coFloat | Fuzzy skin noise persistence | Float(0.5) | 3 | done | noise persistence |
| Process | Others | fuzzy_skin_ripples_per_layer | coInt | Number of ripples per layer | Int(15) | 3 | done | ripple |
| Process | Others | fuzzy_skin_ripple_offset | coPercent | Ripple offset | Percent(50) | 3 | done | ripple phase shift |
| Process | Others | fuzzy_skin_layers_between_ripple_offset | coInt | Layers between ripple offset | Int(1) | 3 | done | ripple phase period |
| Process | Others | fuzzy_skin_first_layer | coBool | Apply fuzzy skin to first layer | Bool(0) | 3 | done | should_fuzzify |
| Process | Others | gcode_add_line_number | coBool | Add line number | Bool(0) | 2 | done | batch 9 (on the finished result) |
| Process | Others | slicing_pipeline_plugin | coStrings | Slicing Pipeline Plugin | Strings() | - | skip | out of scope |
| Process | Others | print_plugin_config_overrides | NO DEF |  |  | - | skip | out of scope |
| Filament | Filament | filament_is_support | coBools | Support material | Bools { false } | 2 | done | batch 11 (first non-support filament; Orca's support filament auto pick: todo) |
| Filament | Filament | filament_change_length | coFloats | Filament ramming length | Floats{10} | - | skip | out of scope |
| Filament | Filament | filament_colour | coStrings | Color | Strings{ "#F2754E" } | 1 | note | Orca's project slot colour; the preset colour is default_filament_colour (mapped) |
| Filament | Filament | filament_adhesiveness_category | coInts | Adhesiveness Category | Ints{0} | 3 | todo |  |
| Filament | Filament | temperature_vitrification | coInts | Softening temperature | Ints{ 100 } | 2 | done | batch 11 (min_vitrification_temperature) |
| Filament | Filament | nozzle_temperature_range_low | coInts | Min | Ints { 190 } | 2 | done | batch 11 (setting; Orca's range warning: todo) |
| Filament | Filament | nozzle_temperature_range_high | coInts | Max | Ints { 240 } | 2 | done | batch 11 (flush temperature fallback; range warning: todo) |
| Filament | Filament | pellet_flow_coefficient | coFloats | Pellet flow coefficient | Floats{ 0.4157 } | - | skip | out of scope |
| Filament | Filament | adaptive_pressure_advance | coBools | Enable adaptive pressure advance (beta) | Bools{ false } | 2 | done | GCode/AdaptivePAProcessor (PCHIP model) after the cooling buffer; PA_CHANGE tags in _extrude |
| Filament | Filament | adaptive_pressure_advance_overhangs | coBools | Enable adaptive pressure advance within features (beta) | Bools{ false } | 2 | done | tags at speed/flow changes within a smooth path |
| Filament | Filament | adaptive_pressure_advance_bridges | coFloats | Static pressure advance for bridges | Floats { 0.0 } | 2 | done | bridge + overhang perimeter override |
| Filament | Filament | adaptive_pressure_advance_model | coStrings | Adaptive pressure advance measurements (beta) | Strings{"0,0,0\n0,0,0"} | 2 | done | validated in Print::validate |
| Filament | Filament | activate_chamber_temp_control | coBools | Activate temperature control | Bools{false} | 2 | done | batch 11 (enum: auto = 3.0, off, on = Orca M191) |
| Filament | Filament | filament_adaptive_volumetric_speed | coBools | Adaptive volumetric speed | Bools {false} | 2 | done | auto speed limit; + hidden volumetric_speed_coefficients |
| Filament | Cooling | initial_layer_fan_speed | coInts | First layer fan speed | Ints { -1 } | 2 | done | batch 7 |
| Filament | Cooling | dont_slow_down_outer_wall | coBools | Don't slow down outer walls | Bools { false } | 2 | done | batch 7 |
| Filament | Cooling | enable_overhang_bridge_fan | coBools | Force cooling for overhangs and bridges | Bools{ true } | 2 | done | ExtrusionProcessor step fan + overhang regions |
| Filament | Cooling | overhang_fan_threshold | coEnums | Overhang cooling activation threshold | EnumsGeneric{ (int)Overhang_threshold_bridge } | 2 | done | enum 0%..95% |
| Filament | Cooling | internal_bridge_fan_speed | coInts | Internal bridges fan speed | Ints{ -1 } | 2 | done | _INTERNAL_BRIDGE role fan (-1: bridge fan) |
| Filament | Cooling | support_material_interface_fan_speed | coInts | Support interface fan speed | Ints{ -1 } | 2 | done | batch 7 |
| Filament | Cooling | ironing_fan_speed | coInts | Ironing fan speed | Ints{ -1 } | 2 | done | batch 7 |
| Filament | Cooling | additional_cooling_fan_speed | coInts | Fan speed | Ints { 0 } | 2 | done | batch 7 |
| Filament | Advanced | filament_plugin_config_overrides | NO DEF |  |  | - | skip | out of scope |
| Filament | Multimaterial | filament_tower_interface_pre_extrusion_dist | coFloats | Interface layer pre-extrusion distance | Floats { 10. } | 4 | todo |  |
| Filament | Multimaterial | filament_tower_interface_pre_extrusion_length | coFloats | Interface layer pre-extrusion length | Floats { 0. } | 4 | todo |  |
| Filament | Multimaterial | filament_tower_ironing_area | coFloats | Tower ironing area | Floats { 4. } | 4 | todo |  |
| Filament | Multimaterial | filament_tower_interface_purge_volume | coFloats | Interface layer purge length | Floats { 20. } | 4 | todo |  |
| Filament | Multimaterial | filament_tower_interface_print_temp | coInts | Interface layer print temperature | Ints { -1 } | 4 | todo |  |
| Filament | Multimaterial | filament_flush_temp | coInts | Flush temperature | Ints{0} | 4 | done | batch 11 (flush_temperatures) |
| Filament | Multimaterial | filament_flush_volumetric_speed | coFloats | Flush volumetric speed | Floats{ 0 } | 4 | done | batch 11 (flush_volumetric_speeds) |
| Filament | Multimaterial | long_retractions_when_ec | coBools | Long retraction when extruder change | Bools {false} | - | skip | out of scope |
| Filament | Multimaterial | retraction_distances_when_ec | coFloats | Retraction distance when extruder change | Floats{10} | - | skip | out of scope |
| Printer | Basic information | parallel_printheads_count | coInt | Parallel printheads count | Int{1} | - | skip | out of scope |
| Printer | Basic information | bed_exclude_area | coPoints | Excluded bed area | Points{ Vec2d(0, 0) } | 2 | todo | arrange + bed render (UI batch) |
| Printer | Basic information | best_object_pos | coPoint | Best object position | Point(Vec2d(0.5, 0.5)) | - | skip | out of scope |
| Printer | Basic information | preferred_orientation | coFloat | Preferred orientation | Float(0.0) | - | skip | out of scope |
| Printer | Basic information | gcode_skip_config_block | coBool | Skip G-code config block | Bool(false) | 2 | done | batch 9 |
| Printer | Basic information | pellet_modded_printer | coBool | Pellet Modded Printer | Bool(false) | - | skip | out of scope |
| Printer | Basic information | printer_agent | coString | Printer Agent | String("") | - | skip | out of scope |
| Printer | Basic information | use_3mf | coBool | Use 3MF instead of G-code | Bool(false) | - | skip | out of scope |
| Printer | Basic information | scan_first_layer | coBool | Scan first layer | Bool(false) | - | skip | out of scope |
| Printer | Basic information | enable_power_loss_recovery | coEnum | Power Loss Recovery | Enum<PowerLossRecoveryMode>(PowerLossRecoveryMode::PrinterConfig | 2 | done | Marlin M413 at 2nd layer, off at end |
| Printer | Basic information | wrapping_exclude_area | coPoints | Probing exclude area of clumping | Points() | - | skip | out of scope |
| Printer | Basic information | belt_printer | coBool | Enable belt printing | Bool(false) | - | skip | out of scope |
| Printer | Basic information | belt_printer_infinite_y | coBool | Infinite Y axis | Bool(true) | - | skip | out of scope |
| Printer | Basic information | belt_slice_rotation_angle | coFloat | Belt tilt angle | Float(45.) | - | skip | out of scope |
| Printer | Basic information | belt_slice_rotation | coEnum | Belt tilt axis | Enum<BeltRotationAxis>(BeltRotationAxis::X) | - | skip | out of scope |
| Printer | Basic information | belt_support_floor_offset | coFloat | Support Floor Z offset | Float(0) | - | skip | out of scope |
| Printer | Basic information | gcode_remap_x | NO DEF |  |  | - | skip | out of scope |
| Printer | Basic information | gcode_remap_y | NO DEF |  |  | - | skip | out of scope |
| Printer | Basic information | gcode_remap_z | NO DEF |  |  | - | skip | out of scope |
| Printer | Basic information | belt_frame_tilt_decouple | coBool | Decouple machine-frame tilt | Bool(false) | - | skip | out of scope |
| Printer | Basic information | belt_frame_tilt_angle | coFloat | Machine-frame tilt angle | Float(45.) | - | skip | out of scope |
| Printer | Basic information | spaghetti_detector | coBool | Enable spaghetti detector | Bool(false) | - | skip | out of scope |
| Printer | Basic information | time_cost | coFloat | Time cost | Float(0) | 2 | done | total cost += time_cost × hours |
| Printer | Basic information | build_plate_tilt_x | coFloat | Build plate tilt X | Float(0.) | - | skip | out of scope |
| Printer | Basic information | build_plate_tilt_y | coFloat | Build plate tilt Y | Float(0.) | - | skip | out of scope |
| Printer | Basic information | printer_plugin_config_overrides | NO DEF |  |  | - | skip | out of scope |
| Printer | Basic information | fan_speedup_time | coFloat | Fan speed-up time | Float(0) | 2 | done | GCode/FanMover pass after the cooling buffer |
| Printer | Basic information | fan_speedup_overhangs | coBool | Only overhangs | Bool(true) | 2 | done | FanMover |
| Printer | Basic information | fan_kickstart | coFloat | Fan kick-start time | Float(0) | 2 | done | FanMover |
| Printer | Basic information | part_cooling_fan_min_pwm | coInt | Minimum non-zero part cooling fan speed | Int(0) | 2 | done | batch 7 |
| Printer | Basic information | extruder_clearance_dist_to_rod | coFloat | Distance to rod | Float(40) | 2 | note | Orca reads it only in the compacted wipe tower clearance; goes with that (tier 4) |
| Printer | Basic information | bed_mesh_min | coPoint | Bed mesh min | Point(Vec2d(-99999, -99999)) | 2 | done | batch 11 (adaptive mesh variables native) |
| Printer | Basic information | bed_mesh_max | coPoint | Bed mesh max | Point(Vec2d(99999, 99999)) | 2 | done | batch 11 |
| Printer | Basic information | bed_mesh_probe_distance | coPoint | Probe point distance | Point(Vec2d(50, 50)) | 2 | done | batch 11 |
| Printer | Basic information | adaptive_bed_mesh_margin | coFloat | Mesh margin | Float(0) | 2 | done | batch 11 |
| Printer | Basic information | nozzle_hrc | coInt | Nozzle HRC | Int{0} | 2 | skip | only Bambu device calibration (CalibUtils) reads it |
| Printer | Basic information | auxiliary_fan | coBool | Auxiliary part cooling fan | Bool(false) | 2 | done | batch 7 |
| Printer | Basic information | fan_direction | coEnum | Fan direction | Enum<FanDirection>(fdUndefine) | 2 | note | only Orca's auto-orient (OrientJob) reads it; 3.0 has no auto-orient |
| Printer | Basic information | support_chamber_temp_control | coBool | Support controlling chamber temperature | Bool(true) | 2 | done | batch 11 |
| Printer | Basic information | cooling_filter_enabled | coBool | Use cooling filter | Bool(false) | - | skip | out of scope |
| Printer | Machine G-code | file_start_gcode | coString | File header G-code | String("") | 2 | done | batch 10 (start-up placeholders set before the header) |
| Printer | Machine G-code | time_lapse_gcode | coString | Timelapse G-code | String() | 2 | done | batch 10 (per layer; Orca's i3 / multi-extruder position picker: todo) |
| Printer | Machine G-code | wrapping_detection_gcode | coString | Clumping detection G-code | String("") | - | skip | out of scope |
| Printer | Multimaterial | manual_filament_change | coBool | Manual Filament Change | Bool(false) | 2 | done | batch 9 (tag + processor) |
| Printer | Multimaterial | bed_temperature_formula | coEnum | Bed temperature type | Enum<BedTempFormula>(BedTempFormula::btfHighestTemp) | 2 | done | batch 9 (GCodeGenerator::bed_temperature) |
| Printer | Multimaterial | wipe_tower_type | coEnum | Wipe tower type | Enum<WipeTowerType>(WipeTowerType::Type2) | 4 | todo |  |
| Printer | Multimaterial | tool_change_on_wipe_tower | coBool | Tool change on wipe tower | Bool(false) | 4 | todo |  |
| Printer | Multimaterial | wait_for_temp_on_wipe_tower | coBool | Wait for temperature on wipe tower | Bool(false) | 2 | todo | wipe tower batch (WipeTower2 toolchange_Change + set_extruder defer) |
| Printer | Multimaterial | enable_belt_purge_tower | coBool | Enable belt purge tower | Bool(false) | - | skip | out of scope |
| Printer | Multimaterial | machine_tool_change_time | coFloat | Tool change time | Float { 0. } | 2 | done | batch 9 → filament_change_time (with load/unload) |
| Printer | Multimaterial | is_imex | coBool | IDEX/IQEX Printer | Bool(false) | - | skip | out of scope |
| Printer | Multimaterial | imex_firmware_managed_zones | coBool | Firmware-managed zones | Bool(false) | - | skip | out of scope |
| Printer | Multimaterial | imex_gantry_count | coInt | Gantry Count | Int(1) | - | skip | out of scope |
| Printer | Multimaterial | imex_tools_per_gantry | coInt | Tools per Gantry | Int(2) | - | skip | out of scope |
| Printer | Multimaterial | imex_tool_layout | coEnum | Tool 0 Position | Enum<ImexToolLayout>(ImexToolLayout::FrontLeft) | - | skip | out of scope |
| Printer | Multimaterial | imex_nozzle_clearance_x | coFloat | Nozzle Clearance X | Float(30.0) | - | skip | out of scope |
| Printer | Multimaterial | imex_nozzle_clearance_y | coFloat | Nozzle Clearance Y | Float(30.0) | - | skip | out of scope |
| Printer | Multimaterial | imex_carriage_margin | coFloat | Safety Margin | Float(0.0) | - | skip | out of scope |
| Printer | Multimaterial | imex_viz_theme | coEnum | Visualization Theme | Enum<ImexVizTheme>(ImexVizTheme::Standard) | - | skip | out of scope |
| Printer | Extruder | nozzle_type | coEnums | Nozzle type | EnumsGeneric({ ntUndefine }) | 2 | skip | only Bambu device calibration / HRC lookup |
| Printer | Extruder | nozzle_volume_type | coEnums |  | EnumsGeneric{ NozzleVolumeType::nvtStandard } | - | skip | out of scope |
| Printer | Extruder | nozzle_volume | coFloats | Nozzle volume | Floats { 0.0 } | - | skip | out of scope |
| Printer | Extruder | extruder_printable_height | coFloats | Extruder printable height | Floats{0} | - | skip | out of scope |
| Printer | Extruder | extruder_printable_area | coPointsGroups | Extruder printable area | PointsGroups{} | - | skip | out of scope |
| Printer | Extruder | long_retractions_when_cut | coBools | Long retraction when cut (beta) | Bools {false} | - | skip | out of scope |
| Printer | Extruder | retraction_distances_when_cut | coFloats | Retraction distance when cut | Floats {18} | - | skip | out of scope |

## Converter drop list: features to port as well
| Orca key(s) | tier | status | note |
|---|---|---|---|
| wipe_distance, filament_wipe_distance | 3 | done | batch 27; wipe at most this long, the rest retracted before it (Orca calculateWipeRetractionLengths) |
| retract_lift_enforce, filament_retract_lift_enforce | 2 | done | batch 20; travel lift gated by the last non-gap-fill role / first layer |
| default_jerk, outer_wall_jerk, inner_wall_jerk, infill_jerk, top_surface_jerk, initial_layer_jerk, travel_jerk, initial_layer_travel_jerk | 2 | done | batch 8; per-feature jerk (M205 X Y / Klipper square corner velocity) |
| extruder_clearance_height_to_lid | 2 | done | batch 19; full-bed extruder slice in the fallback sequential geometry |
| overhang_fan_speed (+ enable_overhang_bridge_fan, overhang_fan_threshold) | 1/2 | done | batch 12; Orca step fan in ExtrusionProcessor, converter bridge_fan_speed conv |
| machine_load_filament_time, machine_unload_filament_time (+ machine_tool_change_time) | 2 | done | batch 9: converter sums them into 3.0's filament_change_time; time estimate of filament changes |
| wall_sequence "inner wall/outer wall/inner wall" | 4 | todo | Orca's inner-outer-inner wall order (3.0 has only external_perimeters_first) |
| sparse_infill_pattern crosszag, lockedzag, quartercubic, lateral-honeycomb, lateral-lattice, crosshatch, tpmsd, tpmsfk, spiralinset (+ monotonic / monotonicline as sparse) | 4 | done | batches 57-61; Spiral Inset is a top / bottom / internal solid pattern (support interface Spiral Inset goes with the support group); Orca v2.5.0's sparse list has no monotonic; 3.0's 3D Honeycomb kept as "3D Honeycomb (classic)" |
| brim_type painted (brim ear points, gizmo, 3MF) | 3 | todo | deferred to the end (user decision 2026-10-18): new gizmo in 3.0's GUI, model points, 3MF |
| support_interface_pattern spiralinset, rectilinear_interlaced, grid | 4 | done | batch 64; 3.0's rectilinear keeps its alternating ±45° interface angle (Orca's rectilinear is a fixed angle; Orca profiles' rectilinear map to 3.0's) |
| wipe tower Type 2 features (wipe_tower_wall_type rib / cone / rectangle, rib width / extra length, fillet, max purge speed, skip points, tower interface features + cooldown + filament_tower_*, wait_for_temp_on_wipe_tower, tool_change_on_wipe_tower) | 4 | todo | plan: 3-way merge of Orca's WipeTower2 into 3.0's WipeTower.cpp over PrusaSlicer 919740fb (the base Orca synced to, 2024-07-09; `build/prusa_base/`: `git merge-file` leaves 26 conflicts), then Orca config access to 3.0's, Orca's Circle (ArcSegment) for the rounded rib walls, the GCode integration (skip point routing, the M109 wait tag, interface temperatures) and Print (rib offset, first layer outline) |
| wipe_tower_rotation_angle | 4 | note | 3.0 keeps the tower rotation in the model (the tower is rotated in the 3D scene), not in the profile |
| wipe_tower_type type1 (Bambu tower: prime_tower_enable_framework, prime_tower_infill_gap, wipe_tower_sparse_layers_combination, filament_adhesiveness_category) | 4 | todo | Orca's BBS tower, 5.5k lines + ToolOrdering / GCode paths; ask the user before porting |

## Orca runtime G-code variables (batch 11, native in GCode.cpp)
The import used to re-create these as local variables in the converted G-code; 3.0 now sets them
with Orca's formulas (3.0's material slots = Orca's filaments, 3.0's tools = Orca's extruders /
nozzles / hotends, no nozzle grouping): filament_map, first_tools / first_filaments,
first_non_support_*, initial_no_support_*, initial_filament_id, current_filament_id /
current_extruder_id / current_nozzle_id / current_hotend (updated at tool changes),
initial_nozzle_id, nozzle_diameter_at_nozzle_id, max_additional_fan, overall_chamber_temperature,
min_vitrification_temperature, is_all_bbl_filament, during_print_exhaust_fan_speed_num,
has_tpu_in_first_layer, flush_temperatures, flush_volumetric_speeds, outer_wall_volumetric_speed
(3.0's real outer wall flow), adaptive_bed_mesh_min/max, bed_mesh_probe_count, bed_mesh_algo,
max_print_z, first_layer_center_no_wipe_tower, hold_chamber_temp_for_flat_print,
wipe_tower_center_pos_x/y/valid, print_time_total_sec/_day/_hour/_minute/_sec and
used_filament_length (replaced in the finished G-code), curr_y_acceleration_limit (Orca without the
Bambu bed-mass keys: the lowest machine Y acceleration), curr_accumulated_mass, curr_layer_mass; tool
change: next_filament_id / next_nozzle_id / next_hotend, old/new_retract_length(_toolchange),
x/y/z_after_toolchange, first/second_flush_volume, flush_length_1..4, travel_point_1..3 (Orca's
fixed points without a Bambu cutter path), wipe_avoid_perimeter / _pos_x, is_prime_tower_interface,
relative_e_axis, fan_speed. Still emulated by the import: the Bambu cutter / extruder-change
retraction values (out of scope).
