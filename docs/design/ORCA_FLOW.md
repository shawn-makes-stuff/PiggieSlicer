# Orca user flow vs PiggieSlicer

Map of OrcaSlicer v2.5.0-alpha's everyday flow (read from its source: `Plater.cpp`, `PresetComboBoxes.cpp`,
`MainFrame.cpp`, `WebGuideDialog.cpp`, `Tab.cpp`, `ParamsPanel.cpp`) against PiggieSlicer, with the gaps.
Rule (user, 2026-10-18): replicate Orca's functionality, keep PrusaSlicer 3.0's benefits; don't move UI
elements around for its own sake; don't send users to menus they would not normally need.

Status: ✓ = same as Orca · ~ = works, differently · ✗ = missing.

## 1. First run
| Orca | Ours | |
|---|---|---|
| Wizard: vendor list → model cards → tick nozzles (all of a model's nozzles by default, `WebGuideDialog.cpp:622`) | Welcome → Choose a printer → model card (series groups) → nozzle in the printer's nozzle row | ~ |
| Wizard filament page: installs the model's `default_materials` (filament dropdown then shows only those) | Printer definitions carry `default_materials` (converter: Orca's); the first time a printer's filament list has no favourite, they are favourited | ~ |

## 2. Printer
| Orca | Ours | |
|---|---|---|
| Dropdown lists only installed printers + "Select/Remove printers", "Create printer" (`PresetComboBoxes.cpp:1536`) | Printer list = favourites (installed) by default (`printers_only_favorites` true, 2026-10-18) + "Add printer" | ✓ |
| Edit button → printer settings tab | Printer cog → printer settings | ✓ |
| Nozzle / variant combo, bed type combo | Nozzle row, plate dropdown | ✓ |
| Connection button → physical printer dialog (host, API key) | Network printer button → editor (edit this printer's, else add) | ✓ |

## 3. Filaments
| Orca | Ours | |
|---|---|---|
| One row per filament: colour, preset combo, edit; two columns | Same (number hidden for one filament) | ✓ |
| Add / remove filament (+ / −) | [+]/[−] change the generic unit's slots | ✓ |
| Sync from printer (AMS / Moonraker lane data), shown with a connected printer | [sync] (Moonraker lane data, Happy Hare), shown when the destination is a network printer | ✓ |
| Dropdown lists only installed filaments | Favourites only by default (`materials_only_favorites` true); all when the printer has none | ✓ |
| "Flushing volumes" button → matrix dialog, auto-calc from colours; a colour change recalculates that filament's row / column | Flushing volumes button (several filaments) → `FlushingVolumesDialog` (matrix, Auto-calculate); colour changes recalculate as Orca on printers that purge by colour | ✓ |

## 4. Process
| Orca | Ours | |
|---|---|---|
| Global / Objects, preset combo + save, tabs Quality…Others, settings inline | Same + Favorites tab | ✓ |
| Search in settings | Search box above Printer; print settings open inline | ✓ |
| Delete preset (user presets; confirm; selects the default one) | Process: delete icon; filament / printer settings: Delete preset | ✓ |
| Simple / Advanced / Develop visibility | — | ✗ (not needed yet) |

## 5. Prepare → slice → output
| Orca | Ours | |
|---|---|---|
| "Slice plate / Slice all" → switches to Preview | Slice / Slice all (by selected beds) → Preview | ✓ |
| "Print plate" menu: Print (send), Export G-code | Destination + Export / Send to printer | ~ |
| Preview tab disabled until sliced | Preview greyed until a selected bed is sliced | ✓ |

## 6. Device / Calibration
| Orca | Ours | |
|---|---|---|
| Device tab: printer's web UI (Mainsail / Fluidd / OctoPrint) | Device tab: same, or a note when no network printer | ✓ |
| Calibration menu (temperature tower, flow, PA, retraction, VFA, max volumetric speed) | — | ✗ (large; separate plan) |

## Gaps to work on (order)
1. Calibration: plan in `docs/design/CALIBRATION.md` (needs a calibration mode in the G-code writer).
2. The multiplier of the flush matrix is the filament's `filament_purge_multiplier` here (Orca: project
   `flush_multiplier`); the dialog does not show it.
