# Colour stacking mode (ColorStack in the slicer) — design

Status: proposal, 2026-10-19. Source: github.com/shawn-makes-stuff/ColorStack (local `D:\Projects\ColorStack`,
`colorstack/plan.py`, `emit.py`, `tower.py`, STATUS.md).

## What ColorStack does today
A post-processor on finished G-code. Layers are grouped into bands; inside a band each colour prints its own
layers bottom-up before the next colour (fewer swaps: Stitch 318 -> 181, cube 77 -> 41). The nozzle is a flat tip
under a cone: material standing dz above the tip must be `tip_radius + dz*tan(cone_angle)` away (less
`contact`). A band is valid when no later nozzle hits earlier material; material that would be hit, or that
would rest only on a colour printed later in the band, is handed to the later colour (`plan_band`:
`mask |= on_T & ~on_E`). Visible surfaces may move at most `surface_tolerance` (default 0), else the band is
shortened.

## The problem to fix (user report)
When one layer has overhangs in two places — colour A hanging over B's region below and B hanging over A's —
whichever colour goes second in the band inherits the other's overhang: the back-fill comes out in the wrong
colour, or the band collapses to one layer. Also pinned in ColorStack: wall edges overhanging a later colour by
part of the bead (the resting test only probes the line centre).

Root cause: the post-processor sees only sampled centrelines, so its only tools are "re-colour this piece" or
"shorten the band". Inside the slicer we have the region polygons per colour per layer, the extrusion roles,
and we control the segmentation, so we can remove the conflicts before they exist.

## In-slicer approach
1. **Support graph from geometry, not samples.** Per layer and colour, the printed area (LayerRegion slices of
   that colour's regions, perimeters + fills). A colour piece at layer k+1 depends on the colour(s) under it at
   layer k (overlap of its area with layer k's areas, probed across the full bead width — fixes the pinned edge
   case). Band order inside a band must respect these dependencies; the nozzle-cone rule is the same as today.
2. **Recolour the invisible interior instead of the visible surface.** The colour of internal volume (infill,
   inner perimeters at a colour interface) cannot be seen. Before perimeters / infill are generated, adjust the
   multi-material segmentation inside the part so each colour's visible shell stands on its own colour: the
   interior under an overhang of A becomes A (bounded by a minimum wall of the other colour where needed).
   Cross overhangs (A over B and B over A on one layer) stop being a cycle: each overhang rests on its own
   colour's interior. Visible colours never change. This is a pre-pass on the segmentation (3.0's
   MultiMaterialSegmentation already owns interlocking / `mmu_segmented_region_max_width`).
3. **Visible conflicts: island order, never recolouring (user decision 2026-10-19).** Outer walls are never
   recoloured, ideally not even with a tolerance. Where colours depend on each other in a cycle (A's overhang on
   B, B's on A), the band is ordered per island instead of per colour: island one stacks its layers (e.g. 4 of
   blue), the other colour prints, then island two stacks its 4 layers. Only where even that fails does the band
   end (a swap). Island-by-island ordering is also its own option, as it cuts travel.
4. **Native emission.** G-code generation prints band by band: for each band, each colour in order prints its
   layers bottom-up at their own Z. Travel lift over taller material inside the band (ColorStack's rule), per
   layer cooling / min layer time from the normal cooling code, the wipe tower planned per band (one wipe per
   real swap; the band's stacked colour prints the band's tower layers), exact time and filament statistics from
   the G-code processor, true order in the preview.

## Settings (experimental)
- Print: `color_stacking` (off/on), `color_stacking_max_layers` (10), `color_stacking_surface_tolerance` (0),
  `color_stacking_min_piece` (2 mm).
- Printer: nozzle tip diameter, cone angle, clear height, contact (ColorStack's `nozzle.*`; not measured on the
  user's nozzle yet).
- Only single-nozzle multi-material (MMU, ACE, AMS-style units). Off for IDEX / multi-nozzle, sequential printing,
  spiral vase.

## Phases (one build each)
1. Settings + support graph + band planner in libslic3r (port of `plan.py`'s band growth and nozzle rule onto
   per-layer extrusion collections from ExtrusionOrder), with a test comparing swap counts to ColorStack on the
   same models.
2. Native band emission in GCodeGenerator (Z changes inside a band, travel lift, cooling, statistics). Tower per
   band.
3. Interior recolouring pre-pass on the segmentation (the cross-overhang fix), with tests built from the user's
   failing cases.
4. Preview / sliced-info: swaps saved, band view.

## Decisions
- Phase order: planner + native emission first, then the interior recolouring (user, 2026-10-19).
- Visible colours: never recoloured; island ordering, then a swap (user, 2026-10-19).

## Open questions for the user
- Sample files of the cross-overhang case, to make them the tests.

## Implementation status (2026-10-19)
- Planner: `GCode/ColorStack.{hpp,cpp}` (support + nozzle-collision precedence between units; whole colours first,
  islands when they cycle or with `color_stacking_island_order`; greedy band growth while it costs no more swaps
  than starting a new band; bands stop at custom G-code layers and at the nozzle clear height). Tests:
  `tests/libslic3r/test_colorstack.cpp`.
- Tool ordering: `ToolOrdering::apply_color_stacking` (Print, with and without a wipe tower): per layer per colour
  paths + bead area from the layer regions (extruder by `LayerTools::extruder`, feature filaments included,
  "any extruder" support with the layer's first colour), bands planned from the filament loaded after priming,
  layer extruders rewritten (band's first layer: its colours; other layers: the last colour), no purging into
  objects.
- Emission: `GCodeGenerator::process_layers_color_stack`: chunks (band, step, layer) through `process_layer`
  with a one-colour LayerTools (`color_stack_chunk`: no remapping to the layer's last extruder;
  `color_stack_region`: island filter). A lower chunk after higher ones lifts over the stack in `change_layer`
  and descends at its first travel; travels within the nozzle's reach of the band's taller plastic go over it
  (`travel_floor` = tallest + retract_lift, ColorStack's rule). Tower: `WipeTowerIntegration::band_tool_change`
  (swaps on the band's first tower layer, the last colour prints the band's tower layers at their z). Skirt and
  custom G-code once per layer.
- Verified: fff test "Colour stacking prints each colour several layers in a row" (two cubes, SEMM + tower: tool
  changes 68 -> 11, every extrusion in its cube's colour, same object heights).
- Not yet: interior recolouring (phase 3), per-chunk cooling tuned to stacked layers (cooling runs per chunk),
  preview layer view with z going down inside a band, support with "any extruder" across islands, layers that
  print the tower only (no object, no support) inside a band.

## Faithful port (user, 2026-10-19 evening): ColorStack's logic exactly, plus two additions
User: "check the default settings for color stack and use those. Check the logic in color stack thoroughly as
well, the only addition we are making is the extra overhang check and the island mode."
- Defaults = the user's saved `colorstack.toml`: tip 1.0, cone 45, clear height 2.0, contact 0.2 (now a print
  setting "Edge contact" in the colour stacking group), max_band_layers 10, surface_tolerance 0, min_piece 2.0,
  per_object on, staggered off, resolution 0.1.
- The first in-slicer planner (polygon precedence, reorder only) deviated: no handing over, no staggered plan,
  no per-object units. It is replaced by a line-by-line port of `colorstack/plan.py`: Samples at resolution*2,
  nearest-distance tests, `interface_mask`, `absorb_short_runs`, `split`, `plan_band` (handing over, DONE,
  starts), `_best_band` (colour orders), greedy `plan`, `plan_staggered`, `_max_layers`; pipeline variants
  (staggered only with >2 colours a layer, per object only when a tool spans two objects).
- Additions: resting probed across the bead width (the pinned wall-edge issue), island units (island mode).
- Tower: RunTower's rule for both plans (a swap wipes on the lowest unprinted tower layer; a tower layer completes
  when every unit is past it), which for bands equals `compensate`.
- Emission: collections owned whole print as before; collections with handed-over parts print as pieces, each by
  its owner, through extrude_smooth_path.
- Deviation, stated: ColorStack keeps the fastest variant by a G-code time estimate; the slicer cannot time a plan
  before writing it and keeps the fewest swaps (ties: fewer ceded mm).
- Island mode ordering (addition): ColorStack's `_best_band` keeps a tool's units together, so islands could not
  go A: 0→1, B: 1→0. With island units the band goes island by island (first printed first), each island's colours
  in the order ceding least, the loaded colour first when the island has it, chosen against the islands before it.
- Built (2026-10-19): `ColorStack.cpp` (port), `ToolOrdering::apply_color_stacking` (leaf paths → planner,
  islands by `LayerSlice::overlaps_below` union-find, bands → `ColorStackChunk`s with per-leaf pieces, tower
  swap/finish layers, obstacles), ExtrusionOrder (chunk predicate by leaves, smooth paths cut to the pieces),
  GCode chunk driver, settings Edge shift / Min piece / Per-object / Staggered / Resolution.
- Verified: tests/libslic3r/test_colorstack.cpp expects exactly what ColorStack's Python planner gives on the
  same layers (scratch cs_cases.py): far 1 band of 10; touching 2-layer bands (contact 0.2) / 1 (contact 0);
  cylinder-like wall/inner/infill (0,2,012) (2,10,210), staggered likewise.
- Not ported yet: ColorStack's cooling (respect_min_layer_time, stacked_fan); the slicer's cooling runs per chunk.
