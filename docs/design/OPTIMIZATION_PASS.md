# Optimisation / review pass (started 2026-10-19)
Goal (user): a high-quality, responsive app. Full pass: code review, efficiency, correctness, over
slicing + G-code export, UI responsiveness, start-up, Preview G-code load.

## Rules (user, product owner)
- Don't change core slicing logic beyond efficiency; models must slice correctly. Where a slicing
  change looks worth it, write it under "Advice" and ask; don't apply it.
- No builds without asking. Measure before claiming a speedup (sampler / CLI timings).
- Guard: test suites pass; G-code of a fixed model set compared before/after.

## Phases
1. Fork-added code in libslic3r (16k lines vs master): GCode.cpp, Fill*, PerimeterGenerator, Brim,
   PrintObject*, Support, new GCode/* processors. Per-call config lookups, copies, O(n^2), wrong results.
2. UI per-frame work in our sidebar / dialogs (SidebarPrint, SidebarBed, PrintTabListView,
   FlushingVolumesDialog, LegendWindow); Preview switch.
3. Start-up (bundle parsing, 14.3 s of 15.4 s with 70 vendors).
4. Preview G-code load (load_from_result path).
5. After a build: measure 2-4 with tools/dev/sampler.py, CLI slice timings for 1.

## Done
- Preview switch: bed label textures were re-rasterised, mipmapped, DXT5-compressed and re-uploaded
  on every bed rebuild (each Prepare -> Preview switch rebuilds all bed nodes; unselected beds get a
  second label colour). `BedRenderHelper::label_texture` now returns the live texture of the same
  name (`TextureManager::find_dynamic`). Compiled per file, not built.

- CLI loaded every printer: measured 2026-10-19 (exe of 10-18, scratch datadir, all 70 vendors,
  frog_legs.obj on Kobra 3 / 0.20mm Standard / Anycubic PLA): 107 s total = 30 s preset load and
  evaluation, 1.2 s slice + export (slice 0.25 s, export 0.4 s), 73 s process exit; working set
  19.9-22 GB at slice start. Cause: CLIRuntime never set added printers, so load_preset_bundle built
  and evaluated every template (the GUI builds only added ones). Fix: with --printer-profile (not
  --query-printer-models) the CLI finds the templates of that name
  (`PresetInteractor::template_printers_named`, templates only, no presets) and loads just those, as
  the GUI does; no match (user printer, project printer) keeps the full load. Compiled per file,
  not built; re-measure after the build. Affects e2e_test.py run time (each call paid ~105 s).
  Note: the CLI now reads only that printer's vendor's presets, the same as the GUI.
  Measured after the build (exe 2026-10-19 11:00): same slice 1.2 s total, working set 89 MB; G-code body
  byte-identical to the 10-18 exe (only timestamp, ids, metadata key order differ), so the review batch's
  ExtrudeConfig caching is output-neutral on frog_legs.

- Review 2026-10-19 (compiled per file; NOT in the build started 10-19, needs one more incremental build):
  - Island tour cache (efficiency, output identical): Orca caches the per-layer island tour per filament
    (`m_ordering_cache`, GCode.cpp ~7190) and reuses it while the 1 mm-quantised island points are unchanged; our
    port kept the quantising but recomputed the TSP (nearest neighbour + 2-opt + crossing removal, up to O(n^4))
    on every layer and extruder in the serial G-code stage. Now `GCode::ExtrusionOrder::TourCache`
    (GCodeGenerator::m_tour_cache, cleared per export) keyed on the points (the tour depends on nothing else in
    an export). GCode.hpp changed: the next build recompiles its includers.
  - group_volumes_for_brim (efficiency, output identical): copied and offset every layer of every model part on
    each slice; a single part only uses its first layer (the other layers only test connections between parts).
    It now copies only the first layer for a single-part object.
  - Invalidation bugs (StepsInvalidation.cpp): bridge_angle / relative_bridge_angle are read by counterbore hole
    bridging in the perimeter step but only invalidated prepare-infill (stale walls/bridges after a change); the
    speeds Orca's auto brim width reads (perimeter, external perimeter, infill, solid, top, small perimeter,
    support, small support perimeter) only invalidated G-code export (stale auto brim width). Now perimeters, and
    skirt/brim respectively (Orca invalidates supports for the speeds).

## Findings (open)
- Ironing settings invalidate everything (all_steps, upstream "TODO: Probably too harsh"); our new ironing keys
  copy that. Could be posIroning only; not changed (upstream left it).
- Infill rotate templates (`calculate_infill_rotation_angle`, Fill.cpp:88): with a template, every surface of every
  layer compiles a std::regex and replays all layers below (quadratic in layers). Same as Orca. Could compute the
  angle per layer once per object. Only with a template set (default empty).
- detect_surfaces_type copies every region's slices per layer (`layerm_slices_surfaces`) even with counterbore
  bridging off; small next to the diff_ex it feeds. Not changed.
- Verified as Orca: _extrude flow ratios, jerk, slow_down_layers ramp, skirt speed, FanMover (with_D_option unused in
  Orca; relative E read from the writer), extrude_config cache lifetime (cleared per export, serial stage),
  overhang reverse detection, process_no_bridge gating, extra bridge layers (read pass, then write pass),
  conical overhang / polyholes / fuzzy supported area gated by their settings.
- Full-load CLI exit takes 73 s freeing ~22 GB (CLIRuntime destructor -> project interactor). Still
  happens for --query-printer-models and printers loaded from a project. Options: measure where the
  teardown goes; or skip destruction after the output is flushed. Not changed.
- Preview's first open builds a second full sidebar (all Orca print tabs) that Preview keeps hidden
  (PreviewRenderLayout.cpp:99). Upstream builds it too; measure the first switch before changing.
- Prepare -> Preview still rebuilds all bed nodes and shells on every switch (geometry is cached,
  nodes and materials are not). Measure after the label fix before changing.

## Advice (slicing changes for the user to decide)
- Scarf joint speed differs from Orca (GCode.cpp _extrude): Orca caps only inner/outer wall speed, only when the path
  has no forced speed, before the first layer / slow-down rules; ours caps after them and also overhang perimeters,
  so first-layer seams are slower than Orca's. Fix: move the cap into the `speed == -1` Perimeter /
  ExternalPerimeter branches.
- bottom_solid_infill_flow_ratio (Orca, erBottomSurface) is not ported; 3.0 has no bottom surface role.
- Timelapse G-code: Orca moves it later in the layer for bed-slinger (i3) and multi-nozzle printers
  (need_insert_timelapse_gcode_for_traditional); ours always emits it at the layer start. No Anycubic/Elegoo
  profile sets time_lapse_gcode.
- Draft shield: 3.0 made the skirt first (around the objects only) and trimmed the brim to it; the Orca skirt/brim
  groups always put the skirt around the brims (Orca's draft_shield only makes the skirt infinite). The 3.0
  behaviour is gone, not selectable (conflicts with "keep replaced behaviour as an option").
- Solid infill now follows solid_infill_direction (Orca) instead of fill_angle; same for every Prusa profile (both
  45), differs only for a user preset with another fill_angle.
