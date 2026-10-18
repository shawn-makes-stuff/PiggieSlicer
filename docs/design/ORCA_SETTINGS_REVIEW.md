# Orca settings audit — review

Generated table: `docs/design/ORCA_SETTINGS_AUDIT.md` (`tools/orca_import/audit_tabs.py --tab-cpp <Orca
v2.5.0-alpha src/slic3r/GUI/Tab.cpp> --out ...`). It lists every setting on Orca's Process, Filament and
Printer tabs with Orca's page/group and what PiggieSlicer has (converter mapping → 3.0 key, with the 3.0
category/option group = where our UI shows it).

Counts (2026-10-18, after section A): Process 158 mapped / 203 not mapped, Filament 41 / 37, Printer 31 / 68
(+ structural / dropped). "Not mapped" = no converter mapping yet: either 3.0 has it under another name,
or it has to be ported. Below, the first pass over those keys. Every 3.0 key named here was checked in
3.0's config definitions (ConfigDefsFDM.cpp / ConfigCommon.cpp); semantics still to be checked per key
before mapping.

## A. Converter bugs / quick mappings (3.0 already has the setting) — applied in mapping.py, not yet re-converted
- **Legacy names in the map never fire.** `orca_source.py` applies Orca's `handle_legacy` renames before
  mapping (`wipe_tower_extruder` → `wipe_tower_filament`, `support_material_extruder` →
  `support_filament`, `support_material_interface_extruder` → `support_interface_filament`, …), but
  `mapping.MAP` still lists the old names, so these values are dropped. Map the Orca names:
  `wipe_tower_filament` → `wipe_tower_extruder`, `support_filament` → `support_material_extruder`,
  `support_interface_filament` → `support_material_interface_extruder`.
- Filament for features: `inner_wall_filament_id` → `perimeter_extruder`, `sparse_infill_filament_id` →
  `infill_extruder`, `internal_solid_filament_id` → `solid_infill_extruder` (Orca 0 = default: leave
  3.0's default; 3.0 has no separate outer-wall / top / bottom surface extruders).
- Flush options: 3.0's `wipe_into_infill` / `wipe_into_objects` are per-object (Volume / Object
  settings), not print-preset settings, so Orca's process defaults cannot be mapped (not applied).
- Same key: `chamber_minimal_temperature`, `enable_filament_ramming` mapped. Not yet: `toolchange_ordering`
  (check the enum values), `filament_colour` (decide: preset colour vs project colour).
- Candidate: `seam_slope_conditional` ↔ `scarf_seam_only_on_smooth` (check Orca's meaning first).

## B. Orca features PiggieSlicer does not have (port candidates), by Orca page

Ported (2026-10-18, built in one batch; defaults keep 3.0's behaviour, converted Orca profiles carry
Orca's values): wipe speed + role based wipe speed (Wipe), skirt speed, first layer travel speed
(GCodeWriter), extrusion role change G-code (printer / process / filament, Orca's role names),
`reduce_infill_retraction` (= 3.0's only_retract_when_crossing_perimeters; Orca also retracts before a
perimeter, not ported), flow ratios (print, top surface, set_other_flow_ratios: outer/inner wall,
overhang, sparse, internal solid, gap fill, support, support interface, first layer; no bottom-surface /
brim role in 3.0), air filtration (exhaust fan M106 P3), slow down layers, small perimeter threshold
(3.0's fixed 6.5 mm is the default), purge in prime tower + prime volume, X-Y hole compensation
(the converter adds Orca's contour compensation, since 3.0's XY size compensation moves holes too),
elephant foot compensation layers.
Pending, needs more than a setting: file header G-code (3.0 writes its header before the placeholder
parser is ready), timelapse G-code (Orca's i3 "traditional" path picks a park position), G-code line
numbers (Orca rewrites the exported file; 3.0 has no export post-processing hook yet), hole / contour
XY compensation (slicing pipeline).
- Quality › Seam: wipe speed / role-based wipe speed, wipe on loops, wipe inward (+distance), wipe
  before external loop; scarf angle / overhang thresholds, scarf joint speed / flow, steps.
- Quality › Precision: hole compensation (`xy_hole_compensation`), polyholes, elephant-foot layers,
  precise outer wall, precise Z height.
- Quality › Ironing: pattern, inset, angle (fixed); Z contouring (`zaa_*`).
- Quality › Walls and surfaces: per-feature flow ratios (`print_flow_ratio`, outer/inner wall, top/bottom
  solid, sparse, gap fill, support…), wall direction, small-area flow compensation, min top surface width.
- Quality › Bridging / Overhangs: bridge densities, internal bridge flow/speed/angle, extra bridge layer,
  counterbore bridging; overhang reverse, make overhang printable, detect overhang wall.
- Strength: top/bottom surface density and direction, top surface expansion, infill extras (multiline,
  TPMS/lateral lattice variants, infill rotation templates, skin/skeleton infill), alternate extra wall.
- Speed: initial-layer travel speed/accel, slow-down layers, small perimeter / support thresholds,
  junction deviation, accel-to-decel.
- Support: support ironing, threshold overlap, critical-regions-only, remove small overhang, tree-support
  branch distance/diameter/angle, tree brim, max bridge length, independent layer height.
- Multimaterial: prime tower extras (rib/fillet walls, skip points, interface features, rotation, max
  purge speed), preheat (ooze prevention), cyclic toolchange order.
- Others: skirt type / speed / start angle, draft shield single loop; brim ears and brim flow / outline
  options; print order, spiral smoothing, timelapse type; fuzzy skin modes (noise, ripples, first layer);
  reduce infill retraction; G-code line numbers; change-extrusion-role G-code.
- Filament: adaptive pressure advance, filament adaptive volumetric speed, initial-layer fan speed,
  overhang/bridge fan details, auxiliary fan, exhaust fan / air filtration, flush temperature /
  volumetric speed, tower interface parameters, support-filament flag, vitrification temperature,
  nozzle temperature range.
- Printer: bed exclude area, adaptive bed mesh, timelapse / file-start / role-change G-code, manual
  filament change, fan speed-up/kickstart/min PWM, accessories (aux fan, chamber control, air filtration,
  nozzle type/HRC), extruder printable area/height, machine tool-change time, power-loss recovery.
- Out of scope unless asked: belt printers, IDEX/IMEX, machine frame transforms, plugin configuration,
  pellet printers, spaghetti detection.

## C. Filament settings layout (Orca vs ours)
Orca's Filament tab pages: Filament (Basic information · Flow ratio and Pressure Advance · Print chamber
temperature · Print temperature · Bed temperature · Volumetric speed limitation), Cooling (Cooling for
specific layer · Part cooling fan · Auxiliary · Exhaust), Setting Overrides, Advanced (G-code), Multimaterial,
Dependencies, Notes. Ours: Material & Temperatures, Extrusion & Calibration, Cooling, Multi-Material,
Overrides, Custom G-code, Notes. The generated table gives, for every mapped filament key, Orca's page and
group next to our category — the rows where they differ are the rearrangement list.

Mapped filament settings, Orca page › group → our category (first pass):
- Filament › Basic information: type, soluble, density, cost → Material & Temperatures; **diameter →
  Extrusion & Calibration** (Orca: basic information); vendor, default colour → hidden.
- Filament › Flow ratio and Pressure Advance → Extrusion & Calibration (Orca keeps them on the first page).
- Filament › Print chamber / Print temperature → Material & Temperatures (same).
- Filament › Bed temperature: Orca's per-plate temperatures are structural for the converter (3.0's
  sheets); see where 3.0 shows bed temperatures.
- **Filament › Volumetric speed limitation: max volumetric speed → Overrides** (Orca: first page).
- Cooling › all → Cooling (same). Advanced › start / end G-code → Custom G-code. Multimaterial › all →
  Multi-Material (same). Notes → Notes.
So Orca's first page = our Material & Temperatures + Extrusion & Calibration + max volumetric speed.

## Next
1. Apply section A (converter + a re-convert of Anycubic / Elegoo), verify with the converter report.
2. Choose which of section B to port (user), in Orca's page order.
3. Rearrange our filament categories to Orca's pages (section C), once B's filament ports are known.
