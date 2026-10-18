# Calibration (Orca) — plan

Not started. Orca v2.5.0-alpha's Calibration menu (`Plater.cpp` 16672-17900):

| Test | Orca entry | What it needs |
|---|---|---|
| Temperature tower | `calib_temp` 17311 | new project, tower model from `resources/calib`, a temperature per block applied by the G-code writer (calibration mode in the config, not a plain setting) |
| Flow rate (coarse / fine, YOLO linear) | `calib_flowrate` 17257 | test plates with per-object flow ratio overrides |
| Pressure advance (line / pattern / tower) | `calib_pa` 16769, `_calib_pa_pattern_gen_gcode` 17008 | pattern: G-code generated directly (`CalibPressureAdvancePattern`); tower: PA changed per layer by the G-code writer |
| Max volumetric speed | `calib_max_vol_speed` 17604 | vase model, speed ramped with height by the G-code writer |
| Retraction | `calib_retraction` 17685 | tower, retraction length per height by the G-code writer |
| VFA | `calib_VFA` 17748 | model, speed ramped with height |
| Input shaping frequency / damping | `calib_input_shaping_*` 17832, 17897 | firmware commands per height (Klipper / Marlin) |

Orca does most of these in its G-code writer (`calib_params` / `CalibMode` read while generating layers).

## What PrusaSlicer 3.0 already has (found 2026-10-18)
3.0 ships a Lua plugin system with a **Calibration menu**: `resources/lua/com.prusa3d.slicer.calibration`
(AGPL-3.0, `"menu": "Calibration/..."`, `type = "project.plugin"`):
- `temp_tower.lua`: Temperature Tower; the temperature per step is per-layer custom G-code
  (`api.project:insert_layer_custom_gcode(bed, z, gcode)`), the labels are embossed text.
- `flow_tower.lua`: Flow Tower = max volumetric speed; each step is a volume with its own speed
  (`other_volumes[i].params = {perimeter_speed=..}`), plus print/material preset values set on the bed.
The plugin API also has `api.make_cube`, `api.load_stl`, `api.emboss_svg`, `api.emboss_text`,
`bed:print_presets():set(..)`, `bed:material_presets(i):set(..)`, `api.project:add_object{..}`.

So Orca's tests can be ports to Lua plugins in the same menu, without a calibration mode in the
G-code writer, where the per-height change is either a setting on a volume or a per-layer command:
- per-layer custom G-code: temperature tower (exists), PA tower (`SET_PRESSURE_ADVANCE` / `M900` per
  layer, as Orca's tower emits), input shaping (firmware commands per height, as Orca).
- per-volume settings: max volumetric speed (exists as Flow Tower; check against Orca's vase test),
  VFA (speed per height).
- needs checking in 3.0 first: flow rate (is a per-object flow/extrusion multiplier available?),
  retraction tower (retraction length per height is a G-code-writer setting in Orca), PA pattern
  (Orca writes the G-code directly).

Order when started: compare the two existing plugins with Orca's temperature and max-volumetric-speed
tests (models, ranges, defaults) → PA tower → VFA → input shaping → flow rate → retraction → PA
pattern. Each one verified by slicing and reading the G-code. Orca's calibration models: check their
licence before copying.
