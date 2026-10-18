# Orca-style left sidebar — design

Status: approved layout (user, 2026-10-17). Not implemented yet. Starts after the external multi-material unit work (docs/design/EXTERNAL_MMU.md), because that work's filament-slot logic is hosted here.

## Goal

All everyday settings visible in one left panel, like OrcaSlicer, instead of behind ⚙ → dialog → page. Top to bottom:

```
+------------------------------+
| PRINTER  [Kobra 3      v] ⚙  |   printer preset, edit button
|   Plate  [Textured PEI v]    |   sheet/plate (and nozzle info)
+------------------------------+
| FILAMENT  [+] [-] [sync]     |   + / - = number of slots (external unit),
| 1 ■ [Generic PLA     v] ⚙    |   sync = from printer (Moonraker lane_data)
| 2 ■ [Generic PETG    v] ⚙    |   colour swatch, preset combo, edit
+------------------------------+
| PRINT [0.20mm Standard v] ⚙  |   print preset
| [Global | Objects]  [search] |   Orca's switch
|  Global:  Quality|Strength|Speed|Support|Multimaterial|Others  (tabs)
|           settings rows inline, scrollable
|  Objects: object list; selecting an object/part/layer range shows
|           its override settings (same tabs), "add setting" like today
+------------------------------+          3D view (full remaining width)
```

The right-hand sidebar column is removed; the 3D view takes the width. The sketch above is the short
version; the exact layout is in "Orca's layout, read from its source" below.

## Orca's layout, read from its source (v2.5.0-alpha)
Files: `src/slic3r/GUI/Plater.cpp` (`Sidebar::Sidebar` 2763, `layout_printer` 1029, `init_filament_combo` 3688,
Process embedding 3540), `ParamsPanel.cpp` (title bar 437-446, switch 620-640), `Tab.cpp` (preset row 457-506),
`MainFrame.cpp` (Slice/Print buttons 2239-2250), spacing `Plater.hpp:145 SidebarProps`.
We copy Orca's structure, order, spacing and behaviour; widgets, colours and fonts stay PrusaSlicer 3.0 style
(our theme, piggie pink accent).

Spacing (px at 100 %): title-bar side margin 8 · content side margin 12 · content vertical margin 9 ·
icon spacing 10 · wide spacing 18 (between groups, against mis-clicks) · element spacing 5 (combo + its button).
Sidebar default width 39 em, resizable; the whole sidebar scrolls vertically.

Every section = title bar (min height 3 em) + 2 px separator + content. Clicking a title folds the section;
the folded Printer title reads "Printer  |  <printer name>".

```
PRINTER title   [icon] Printer ..................... [connection] 18 [sync] 18 [settings]
  content       [ [thumb 40x40] [printer combo ......] [edit*] ] 4 [Variant/nozzle] 4 [plate combo+img]
                (preset box min 70x60, radius 6; thumbnail hides when the box is narrower than 235;
                 *edit button shows only on hover)
                then the nozzle group(s): one, or Left/Right for dual nozzles, each with its unit(s)
FILAMENT title  [icon] Project Filaments (N) ....... [−] 10 [+] 18 [sync] 18 [settings]
                (Purge mode / Flushing volumes buttons sit left of [−] when shown; [+] is placed after
                 [−] so it does not move when [−] hides at one filament)
  content       two columns, filament i at column i%2, row i/2 (1 2 / 3 4 / ...):
                12 [■1] 3 [filament combo ......] 3 [edit ▾] 12  (right column starts at 3, not 12)
PROCESS         line #A6A9AA
  title         [icon] Process 18 [Global | Objects] 5 [tip] ...... [advanced] 5 [mode] 18 [table] 18 [compare] 8
                line #CECECE
  Objects only  search box ("Search plate, object and part.") + object list
  preset row    12 [undo] 5 [print preset combo ......] [undo-to-system] 10 [save] 10 [delete] 18 [search] 12
  tab strip     Quality | Strength | Speed | Support | Multimaterial | Others
  page          the selected page's settings, inline
```
Slice and Print are NOT in the sidebar: Orca puts "Slice plate" / "Print plate" at the right end of the top bar.

How it shifts:
- Sections fold to their title bar; the rest moves up.
- Filament rows are added or removed in reading order; the right column is padded with a spacer so rows stay level.
- Global ↔ Objects inserts or removes the search box and object list between the Process title and the preset row.
- In Objects mode the page shows the selected object's, part's or layer range's settings.

## What PiggieSlicer has today (verified)

- `AbstractRenderLayout`: left column = object list (collapsible, `init_left_column`), right column = `SidebarStackLayout` with `SidebarBed`, `SidebarPrint` (print preset combo + ⚙ opening `PrintSettingsDialog`), `SidebarObject` (selected object/part settings, "Add object settings").
- Settings pages already exist per `Domain::ConfigItemDef::Category`, built from `ConfigRowItems` / `ConfigRowItem` with dirty-tracking categorizers (`PrintSettingsDialog.cpp`, `AbstractSettingsDialog`); printer hardware in `LogicalPrinterSettingsDialog` (sheet combo, nozzle list); filament in `MaterialSettingsDialog`.
- The UI is ImGui + Yoga (custom layout), not wx: new widgets are Yoga items.

## Approach

- Reuse, don't rewrite: the inline tabs are the same category pages and `ConfigRowItems` the dialogs build today, hosted in the panel instead of a dialog. Category → tab mapping in one place; Orca's six tabs as the target grouping.
- Printer section reuses the preset combo + sheet combo + nozzle info from `LogicalPrinterSettingsDialog`; ⚙ still opens the full printer dialog for rare settings.
- Filament section: one row per material slot (existing per-slot material presets in `PresetInteractor`), colour swatch, combo, ⚙ → `MaterialSettingsDialog`; [+]/[-] and [sync] call the external-unit Biz API (`set_external_mmu`, filament sync) — no logic in the UI.
- Process section: `SidebarPrint`'s combo + the tabs; Objects mode embeds the existing object list and `SidebarObject`'s per-object settings.
- Remove the right column from `AbstractRenderLayout`; keep the left column resizable (`layout_main_left_column_width`).
- Keep the panel thin over existing interactors so a later visual pass (the user plans more UI changes) doesn't touch behaviour.

## Stages (each ends with one build and a look at the running app)
Status 2026-10-17: stages 1–5 built and seen running (screenshot). Fixes after the first look, with
stage 6, not built yet: sidebar 440 px default / 380 min with an always-on drag edge (user: wider and
resizable); the print tabs are one row of text tabs in the settings dialog's tab look (were a wrapped grid
with radio-like icons, third row clipped); filament rows hide the nozzle button on single-nozzle printers.
Stage 6 as built: the destination is a one-line button + cog next to Slice/Export in the top bar (not in
the Printer title as in Orca, so the dialogs keep one owner). Open: a fresh scratch datadir showed
"0.2 nozzle" for an Elegoo Centauri though every card is 0.4 (CLI confirms) - check in the printer dialog.
7. Left bar tabs (user, 2026-10-17): Orca's Prepare / Preview / Device tabs live in 3.0's left workspace bar
   (`LeftBarCtrl`), not along the top. Prepare and Preview are buttons that show the Slicing page in Plater
   or Preview mode (`MainFrame::bind_mode_buttons`, `Navigator::current_module_type`); the Slicing page's own
   button is hidden (`TabsBarCtrl::ShowPageButton`); the 3D view's Prepare/Preview switch is hidden but stays
   bound to its commands (shortcuts). Device is always there: the selected network printer's web page
   (existing `init_physical_printer_page`), or a short note when none is selected. Filament rows always show
   the settings button (was hover-only). Built and seen 2026-10-17.
8. User review 2026-10-17 (not built yet): Slice / destination back at the bottom of the sidebar (user liked
   3.0's block; stage 6's top-bar strip reverted, `TopBar::set_actions` removed); selected print tab and
   Global/Objects in the accent colour; a Favorites (star) tab first in Process, showing 3.0's
   `PrintToolFavoritesItem`; Plate on its own row under the printer, more margin around the printer picture;
   the Welcome printer picker fills the dialog (`WelcomeDialog` m_dialog width 100 %) and the Orca vendors
   group their cards by printer series (converter `family()`: first word of the model name + its number,
   e.g. Kobra 3 = Kobra 3 / Max / V2; Orca's own family is just the vendor).
9. User review 2026-10-18: search box above Printer (moved out of the top bar, `TopBar::take_search_bar`),
   print-setting results open the Process tab inline (`SidebarPrint::show_item`); plate / nozzle dropdowns
   full row, no nozzle / filament number when there is only one; printer cog opens the printer settings,
   the printer list rows lose their settings button; Printer title network-printer button opens the network
   printer editor (edit this printer's, else add; `PhysicalPrinterInteractor::edit_network_printer_for_current`),
   "Add physical printer" removed from the destination list; Preview greyed out until a selected bed is sliced;
   top bar without save / hide-sidebar buttons (sidebar always shown); no Preferences icon in the menu and no
   Preferences button in the left bar; dropdown lists exactly as wide as their field (`ComboBox`); popups keep
   5 px off the window bottom (`Popup`); tab row clear of the page; no green selection / grey hover tint on
   objects (`PlaterScenePresenter select_color`) - the green tint was what hid painted colours after closing
   the painting tool.
1. One left column: the settings column moves to the top of the left column, the right column is gone, popups open to the right (`AbstractRenderLayout`, `Position::Right`). Interim: object list sits below the settings until stage 4. — code written, not built.
2. Section title bars (icon, label, right-aligned buttons, fold on click) for Printer / Filament / Process, with Orca's spacing.
3. Filament section: "Project Filaments (N)" title with [−][+][sync][settings]; two-column rows (swatch with number, combo, edit). Biz: `PresetInteractor::change_external_mmu_slots`, `ProjectInteractor::sync_filaments_from_selected_printer` — written, not built.
4. Process section: Global | Objects switch; print preset row (save, delete); inline tab strip + page; Objects mode moves the object list (with search) here.
5. Printer section content: preset box with thumbnail, nozzle/variant box, plate box.
6. Slice / Print to the right end of the top bar; remove the sidebar action buttons. Plan (read from both sources):
   - Orca (`MainFrame.cpp` 2233-2270, 2152-2195): top bar right end `[▾|Slice plate] 15 [▾|Print plate] 19`.
     Slice menu: Slice all / Slice plate. Print menu for non-Bambu printers: Print (send G-code to the
     host), Export plate sliced file (only when the printer takes .gcode.3mf), Export G-code file. The
     printer connection is set from the Printer title's [connection] button (`Plater.cpp` 2827).
   - Ours today: `SidebarPlaterActionButtons` (Slice / Slice all / Cancel / Invalid settings / Add objects,
     by selected beds; navigates to preview) and `SidebarPreviewActionButtons` (Export / Send to Printer /
     Cancel / Slice, by destination), both `SidebarActionButtons` windows that also own the "Select
     Destination" physical-printer button and its two dialogs.
   - Change: keep both classes' state logic; make them plain items with horizontal buttons and hand them to
     the module's `TopBar` (new `TopBar::set_actions(ItemPtr)` into its right wrapper; `TopBar` is a window,
     so the actions must not be windows). Move the destination button + `PhysicalPrinterSettingsDialog` /
     advanced dialog into `SidebarBed` as the Printer title's [connection] button (opens the dialog; tooltip
     shows the selected destination). Print menu = Orca's third-party list mapped to ours: Print = send to
     the selected host, Export G-code file = file / removable drive.

## Requirement (user, 2026-10-17): presets stay fully editable
Printer, filament and print presets can be configured, edited, saved (save / save as / rename / delete) and switched from the panel, with the existing unsaved-changes handling on switch (`PresetInteractor::save_user_preset`, `IPresetDialogManager::show_unsaved_changes_dialog`). This includes printers with an external unit: a saved user printer preset's own values win over the unit's built-in values (`PresetEvaluator.cpp external_mmu_values`, `EvalPresetContext::user_keys`).

## Open points (decide during implementation)

- Simple/advanced mode toggle (Orca has one; 3.0 has config modes?) — check what 3.0 offers.
- Search: 3.0 has a settings search; place it in the Process header.
- Preview mode (G-code preview) sidebar contents: keep what `PreviewRenderModule` shows today, moved to the left.
