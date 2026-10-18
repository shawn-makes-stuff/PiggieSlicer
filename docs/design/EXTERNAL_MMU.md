# External multi-material units (AMS-style) — design

Status: approved by the user 2026-10-17 (decisions below). Steps 1-4 and 6 implemented and unit/regression tested (libraries + test targets); exe not relinked yet; Step 5 not started. See "Implementation status" at the end.

## Goal

Any single-nozzle printer can be used with an external multi-material unit (Anycubic ACE, Bambu AMS, Creality CFS, Happy Hare / ERCF / Box Turtle, MMU clones...), with the number of slots the user actually has (e.g. 4 for one ACE, 16 for four AMS units). The slicer only decides *when* and *to which slot* to switch, how much to purge and where; the unit's firmware does the physical swap. A purge (prime) tower is available, as in Orca.

## How Orca does it (verified in OrcaSlicer v2.5.0-alpha source)

- **No hardware definitions per unit.** A printer profile sets `single_extruder_multi_material` (one nozzle, many filaments). The number of filaments is just how many filaments the project has; there is no per-unit model and no global maximum constant.
- **Colour change** = `change_filament_gcode` (printer-specific, may be empty). If it doesn't issue a T command itself, Orca appends `Tn` (`GCode.cpp:1432`, `custom_gcode_changes_tool`). The Kobra 3 + ACE profile has an empty one, so the ACE firmware just receives `T0`/`T1`...
- **Purging:** `enable_prime_tower`; `purge_in_prime_tower` (off = the unit purges into its own chute); `flush_volumes_matrix` × `flush_multiplier`; flush into infill / objects / supports; `filament_flush_temp`, `filament_flush_volumetric_speed`.
- **Two tower implementations** (`wipe_tower_type`): Type 1 (Bambu, for units with a filament cutter) and Type 2 (Prusa-derived `WipeTower2`, for MMU/multi-tool).
- **Ramming is its own switch** (`enable_filament_ramming`), not implied by multi-material.
- **Estimates:** `machine_load_filament_time`, `machine_unload_filament_time`.

## What PiggieSlicer 3.0 has today (verified in this repo)

- A **feeder** concept: `kind: feeder` (type `mmu`/`manual`, `slot_count`) attached to fixed vendor printer configs, e.g. "MK4 MMU3". The MMU3 is single-nozzle; the multi-toolhead case is the XL/INDX `tools`.
- Filament count = `HwPrinterConfig::material_slot_count()` = tools + Σ(feeder slots − 1). G-code, tool ordering and the wipe tower all size from it.
- Each printer **instance** stores its own `HwPrinterConfig` (tools, `feeders` map with per-feeder `slot_count`), so per-instance slot counts already fit the data model.
- `FeederType` is loaded but drives no behaviour; behaviour comes from settings switched by `feeder.*` conditions in presets.
- Single-nozzle multi-material **without** a tower works (`GCode.cpp:4171`); the `Tn` fallback exists (`GCode.cpp:4152`).
- The wipe tower's Prusa-MMU moves (cooling tube, parking, extra loading move, high current) come from settings and can be zeroed.
- **Missing:** no UI references feeders at all (users can't add one or set slots); no ramming on/off switch; no load/unload time for estimates.

## Design

### 1. A generic external unit (data)
- One feeder definition, **"Multi-material unit"**, `feeder_type: mmu` (keeps project files readable by PrusaSlicer — no new enum value), `model.base_model: EXTERNAL_MMU`.
- Offered for every single-tool FFF printer of every vendor (`condition: printer tool count == 1`). The Orca converter emits it into each vendor; Prusa's own MMU2S/MMU3 stay unchanged.
- `slot_count` is set **per printer instance** by the user: 2–64, default 4.

### 2. UI (C++)
- Printer setup (Welcome "Choose a printer" and Add Printer): a "Multi-material unit" option — None / Multi-material unit + slot count spinner.
- The same control in the printer's settings afterwards, so slots can be changed when the user adds or removes units.
- Changing the count resizes the filament list exactly like a slot change does today.

### 3. Behaviour profile (presets, condition `feeder.base_model == "EXTERNAL_MMU"`)
- `single_extruder_multi_material = 1`.
- Purge tower **on by default**, user can turn it off (no tower = unit purges into its chute, as Orca's `purge_in_prime_tower = 0`).
- Prusa MMU moves off: `cooling_tube_length`, `cooling_tube_retraction`, `parking_pos_retraction`, `extra_loading_move`, `filament_cooling_moves`, `high_current_on_filament_swap` = 0.
- Ramming **off** by default (new switch, see 4).
- `toolchange_gcode` = the printer's Orca `change_filament_gcode` (translated); empty → plain `Tn` via the existing fallback.
- Purge volumes from Orca's flush data (`flush_volumes_matrix` × `flush_multiplier` → `wiping_volumes_matrix` + `filament_purge_multiplier`), plus Orca's automatic colour-based purge-volume calculation (see 4).

### 4. Small C++ additions
- `enable_filament_ramming` (printer setting, default on so Prusa MMU behaviour is unchanged; off for external units) — gate the SEMM ramming sequence in `WipeTower.cpp`.
- `machine_load_filament_time` / `machine_unload_filament_time` — added to tool-change time in the G-code processor's estimate.
- Orca-only tool-change variables the import already flagged (`flush_length`, old/new filament temperatures) — expose as placeholders.
- Port Orca's colour-based purge-volume calculation (recompute the matrix from filament colours, as Orca's flushing-volumes dialog does).
- Port Orca's Type 1 (Bambu-style, filament cutter) tower alongside the existing Prusa-derived one, selectable per printer like Orca's `wipe_tower_type`; converter sets it from each Orca profile.

### 4b. Filament sync from the printer (Orca-like)
- Orca (`MoonrakerPrinterAgent::fetch_filament_info`, v2.5.0-alpha): reads Moonraker's database `GET /server/database/item?namespace=lane_data` (published by AFC and recent Happy Hare: per lane material, colour, loaded), falls back to Happy Hare's own status, maps each material name to a generic filament preset, groups lanes in units of 4. Orca also has agents for Bambu, Qidi, Creality and Snapmaker.
- PiggieSlicer: add "Sync filaments from printer" for physical printers of host type Moonraker first (reuse the existing Moonraker print host). It sets the unit's slot count from the lane count, and each slot's colour and closest matching filament preset. Bambu/Qidi/Creality/Snapmaker sources later.
- The user's COSMOS printer runs the AFC driver (`D:\Projects\cosmoace-integration`), so it is the first real test.

### 5. Converter
- Per printer: carry `change_filament_gcode`, flush matrix/multiplier, load/unload times and purge-in-tower into the external-unit variant presets.

### 6. Testing
- Unit/regression: material slot count with an external unit; Prusa MMU3 G-code unchanged byte-for-byte.
- Slice a multi-colour model on Kobra 3 + unit (tower on and off), a Klipper/Happy Hare printer and a Bambu printer: check `T` commands, temperatures, purge volumes, no ramming/parking moves.
- Real print on the user's Kobra 3 + ACE.

## Implementation plan (file level, from reading the code)

Step 1 — unit + slot count (Domain/Biz, then thin UI)
- `slic3r-domain/.../Preset/HwConfig.hpp`: constants (feeder id `external_mmu`, `base_model` `EXTERNAL_MMU`, slots 2–64, default 4) and helpers `make_external_mmu_feeder(slots)`, `is_external_mmu(const HwFeederConfig&)`. Type stays `FeederType::MMU`.
- `slic3r-shared/.../Biz/Preset/PresetInteractor.{hpp,cpp}`: `set_external_mmu(std::optional<uint32_t> slots, bool user)` built like `select_printer_sheet` — a `change_hw_config_and_validate` modifier that adds/replaces/removes the feeder at address `[0]`; only allowed when `tool_count == 1` and the printer has no vendor feeder (Prusa MMU stays as is). Notify `IPresetChangedListener` with a new `HwItemType::FeederItem`.
- Filament list must follow the new slot count (`material_slot_count()`): verify `select_printer_preset_internal` re-sizes `selected_preset.materials`; fix if it only does that on printer switch.
- `Biz/Preset/ValueMapBuilder.cpp`: also expose `feeder.slot_count` for conditions.
- UI: `App/LogicalPrinterSettingsDialog.cpp` (next to the sheet combo and nozzle list) — "Multi-material unit" toggle + slot spinner; Welcome/Add-printer flow gets the same control. Thin: all logic in `set_external_mmu`.

Step 2 — built-in behaviour layer
- Presets are evaluated per vendor (`PresetEvaluator{vendor_bundle.presets}`), so the external-unit defaults (design §3) are applied by a built-in layer when `feeder.base_model == EXTERNAL_MMU`, before vendor presets, so vendor feeder-conditioned values (e.g. Orca's `change_filament_gcode`) still override. Works for every vendor, including Prusa and custom printers.

Step 3 — C++ settings: `enable_filament_ramming` (gate SEMM ramming in `libslic3r/GCode/WipeTower.cpp`), `machine_load_filament_time` / `machine_unload_filament_time` (G-code processor estimate), Orca tool-change placeholders (`flush_length`, old/new filament temps).

Step 4 — colour-based purge volumes (port Orca's calculation; recompute `wiping_volumes_matrix` from filament colours).

Step 5 — Type 1 (Bambu-style) tower: port Orca's `WipeTower` (Type 1) next to the existing Prusa-derived one, selectable via a `wipe_tower_type`-like option; converter maps it from Orca profiles.

Step 6 — filament sync from Moonraker (`lane_data`, Happy Hare fallback), reusing the Moonraker print host.

Step 7 — converter: carry each printer's multi-material settings (design §5); after the profile import agent finishes.

Each step ends with an incremental build (same memory limits) and its tests; Prusa MMU3 G-code must stay byte-identical.

## UI note
The user plans to rework the UI later. Keep the new controls thin and the logic in Biz/Domain code (slot count, unit attach/detach, filament sync, purge calculation) so the UI can be replaced without touching behaviour.

## Decisions (user, 2026-10-17)
1. Slots 2–64, default 4.
2. Offered for every printer and every tool (Orca-like; corrected, see below).
3. Colour-based automatic purge volumes: in this feature.
4. Bambu-style Type 1 tower: in this feature.

Out of scope here: sending prints to Bambu printers (their network protocol), and Prusa's own MMU (unchanged).

## Correction (user, 2026-10-17): units per tool, Orca-like
Replicate Orca, add no restrictions it doesn't have: a unit can be attached to ANY tool of ANY
printer (single tool, multi-tool, dual nozzle), next to vendor feeders such as Prusa's MMU3 on
another tool. `HwPrinterConfig.feeders` is keyed by tool address, so this is the native model;
`material_slot_count()` = tools + Î£(feeder slots âˆ’ 1); `MaterialIterator` maps slot â†’ tool
(the equivalent of Orca's `filament_map`). Replaces "single-nozzle printers only" above (Â§1, Decision 2).

## Implementation status (2026-10-17, end of agent session)

Build: slic3r-domain-tests, slic3r-shared-tests, fff_print_tests build and pass in full
(domain 39 cases, shared 450 cases / 7722 assertions, fff_print 148 cases). The
slic3r-app-launcher target compiles; its final link failed only because a running
piggie-slicer.exe (PID 10556, not started by this agent, not killed) locks the file. Relink it
and run the end-to-end checks below. No commits.

Regression (Prusa paths): `fff_print_tests "[SEMM]"` with `PIGGIE_GCODE_DUMP_DIR` dumps
semm_wipe_tower.gcode (1 tool + 4-slot MMU feeder, SEMM, wipe tower, ramming on) and
single_material.gcode. Dumped before the libslic3r changes and after: byte-identical except the
timestamp / `"time"` / `config_id` lines.

Step 1 - unit + slot count: DONE in Biz/Domain, NO UI.
- Domain `Preset/HwConfig.{hpp,cpp}`: `EXTERNAL_MMU_*` constants (id `external_mmu`, base model
  `EXTERNAL_MMU`, 2-64 slots, default 4), `is_external_mmu`, `find_tool_feeder`,
  `find_external_mmu(printer, tool)`, `has_external_mmu`, `make_external_mmu_feeder`,
  `external_mmu_feeder_def`, `add_external_mmu_feeder_def`.
- `IO/BundleLoader.cpp`: def added to every vendor (vendor's own def wins); cache epoch 15.
- `PresetInteractor::can_attach_external_mmu(tool)`, `external_mmu_slot_count(tool)`,
  `set_external_mmu(tool, slots|nullopt, user)`: any tool of any FFF printer without a vendor
  feeder at that address; notifies `HwItemType::FeederItem`. Filament list follows through the
  existing `resize_tool_dependant_presets`. Printer name is not changed.
- Per-tool feeder values: `ValueMapBuilder` `feeder.slot_count`, `append_feeder_values`,
  `append_tool_feeder_values`; `PresetEvaluator` uses the evaluated tool's feeder for tool and
  material presets (material cache key includes the feeder); printer level sees the first
  feeder. Single-feeder asserts removed (ValueMapBuilder, PresetMetadataLegacyLoader).
- Per-tool printer values (e.g. `extruder_offset`) are spread to the slots of each tool
  (`Domain/Config.cpp ensure_slot_parity`) - the fixed-by-hardware part of Orca's filament_map.
- Tests: HwConfigTests "External multi-material unit"; PresetInteractorTests "External
  multi-material unit" (test bundle), "... next to a vendor feeder" and multi-tool (app bundle);
  test_multi "Per-tool values follow the tool feeding each slot". All pass.
- Unverified: UI (none added - to come with the sidebar rework), saving/reloading a printer
  instance and a 3MF with the unit, 3MF opened in upstream PrusaSlicer 3 (unknown feeder id).

Step 2 - built-in behaviour: DONE, changed from the plan. Vendor presets set the keys
unconditionally (Prusa `single_extruder_multi_material: 0`, the Orca import too), so the layer is
applied AFTER the preset values (`PresetEvaluator.cpp external_mmu_values`), only for single-tool
printers with a unit: SEMM on, `enable_filament_ramming` off, `wiping_volumes_by_colour` on,
cooling tube / parking / extra loading 0, high current off. Purge tower left to the presets.
Open decision: a saved user printer preset cannot override these (session edits can).

Step 3 - settings: DONE. `enable_filament_ramming` (Printer, default on) gates ramming, initial
delay, cooling-tube retraction, cooling moves, parking, loading move and the reserved ramming
depth in `GCode/WipeTower.{hpp,cpp}` (as Orca WipeTower2). `wiping_volumes_by_colour` (Printer,
default off). StepsInvalidation entries added. Load/unload time: existing `filament_change_time`
already feeds the estimate (converter: Orca load + unload). Placeholders in `toolchange_gcode`
(GCode.cpp): `flush_length`, `old/new_filament_temp`, `old/new_filament_e_feedrate`.
Not added: `x/y_after_toolchange`, `nozzle_temperature_range_high`. Tests: test_multi
"External multi-material unit: no ramming, cooling or parking" passes; placeholders untested.

Step 4 - colour purge: DONE. New `libslic3r/GCode/FlushVolCalc.{hpp,cpp}` (Orca FlushVolCalc +
FlushVolPredictor + RGB2HSV, standard data set embedded); used by
`WipeTower::extract_wipe_volumes` when `wiping_volumes_by_colour` (colours from
`extruder_colour`, else `filament_colour`), x `filament_purge_multiplier`. Also fixed: project
matrix of another size than the slot count (out-of-range read for > 5 slots). Not ported: support
filament rules (no `filament_is_support`), dual-nozzle data sets. Test "Purge volumes from
filament colours (Orca)" passes (560/80 formula, 236/26 table values, 16-slot matrix).

Step 5 - Type 1 (Bambu) tower: NOT STARTED. Orca WipeTower.cpp is 5544 lines with its own
settings, GCodeProcessor/ArcFitter/MultiNozzleUtils dependencies and a different GCode.cpp
integration. Needs its own plan.

Step 6 - filament sync: DONE in Biz, NO UI. `PrintHostMoonraker.{hpp,cpp}`:
`parse_moonraker_lane_data` (AFC format checked in AFC_lane.py), `parse_happy_hare_gates`,
`normalize_lane_color`, `generic_filament_family` (Orca ports), `fetch_filament_lanes(printer)`
(sync, Moonraker only, run in a job). `ProjectInteractor::sync_filaments_from_lanes(tool, lanes)`
sets the slot count (highest lane + 1), colours and "Generic <family>" presets. Tests pass
(parsers, sync on the app bundle). Unverified: against a real printer (COSMOS + AFC).

Open (multi-tool printers with units): slot ids are used as tool numbers in G-code (`Tn` = slot,
as Orca's filament id), per-tool offsets now spread; but the external-unit settings of Step 2 are
not applied on multi-tool printers (SEMM is printer-wide here) and the wipe tower treats each slot
as its own extruder. Orca's automatic filament grouping (filament_map_mode auto, H2D) not ported.
End-to-end checks still to do after relink: Kobra 3 + unit tower on/off, Klipper/Happy Hare,
Bambu printer, real print on Kobra 3 + ACE.
