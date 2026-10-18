# Map

Paths are relative to the repo root. Built code: `src/slic3r-*`, `src/lib*`, `src/platform`, `tests/`.

## Docs
- `STATUS.md` — current state and queue (read first)
- `NOTES.md` — durable facts by topic (grep the headings)
- `docs/branding/COLORS.md` — every colour source; `IMAGES.md` — image catalog; `CLOUD.md` — Prusa cloud inventory (historical); `CLEANUP.md` — rebrand leftovers
- `docs/design/EXTERNAL_MMU.md` — multi-material units: design, decisions, implementation status
- `docs/design/ORCA_SIDEBAR.md` — planned Orca-style left sidebar
- `docs/profiles/ORCA_IMPORT.md` — Orca → 3.0 converter: usage, mapping, fallbacks, test strategy, next steps; `docs/profiles/orca-import/<Vendor>.md` — per-vendor reports

## Branding
- `branding/clean_logo.py` — piggie_logo.jpg → vector marks (`build`, `render`, `write_svg`)
- `branding/make_icons.py` — app icons, splash, grayscale logo → `resources/icons/PiggieSlicer*`, `splashscreen.jpg`
- `src/slic3r-shared/src/Slic3r/App/Theme.cpp` — theme palette (`initialize_dark_colors`, `initialize_light_colors`, ImGui styles)
- `src/slic3r-shared-wx/src/Slic3r/App/WX/WidgetsConfig.cpp` — wx label/button colours (`init_ui_colours`)
- `src/slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp` — splash composition (`make_bitmap`, `Decorate`)

## Identity / compatibility
- `version.inc`, `src/slic3r-base/include/Slic3r/Version.hpp.in` — app name/key/build id, `SLIC3R_COMPAT_*`
- `src/slic3r-base/src/Slic3r/Directories.cpp` — data/cache dir naming (`get_default_datadir`)
- `src/platform/{msw,osx,unix}` — rc, Info.plist, desktop entries

## Presets & profiles
- `resources/presets/prusa-research-fff/` — Prusa's bundled vendor (reference for the format)
- `resources/presets/orca-import/<Vendor>/` + `<Vendor>.idx` — converted Orca vendors (uncommitted, ~140 MB)
- `tools/orca_import/` — converter: `fetch_orca_profiles.py`, `orca_source.py` (inheritance, .opc version check), `orca_defaults.py`, `orca_condition.py`, `mapping.py`, `placeholders.py`, `variants.py`, `convert.py`, `validate.py`, `e2e_test.py`, `config_schema.json`
- `specs/presets/{vendor,preset}-schema.json` — 3.0 format schemas
- `src/slic3r-domain/src/Slic3r/Domain/ConfigDefsFDM.cpp` — all valid setting keys
- `src/slic3r-shared/src/Slic3r/Biz/Preset/PresetInteractor.cpp` — bundle loading (`load_preset_bundle`), hardware changes (`change_hw_config_and_validate`, `select_printer_sheet`, `select_printer_tool_item`), preset re-evaluation (`update_changed_selected_preset_hw_config`)
- `src/slic3r-shared/src/Slic3r/Biz/Preset/{PresetEvaluator,HwConfigEvaluator,ValueMapBuilder}.cpp` — condition evaluation, template → HwPrinterConfig, condition variables
- `src/slic3r-shared/src/Slic3r/App/AddPrinterPanel.cpp` — "Choose a printer" picker (`rebuild_printer_families`, `create_printer_family`)
- `src/slic3r-shared/src/Slic3r/App/WelcomeDialog.cpp` — first-run dialog (changelog screen, `PrinterScreen`)

## Multi-material
- `src/slic3r-domain/include/Slic3r/Domain/Preset/HwConfig.hpp` / `.cpp` — `HwPrinterConfig`, `HwFeederConfig`, `material_slot_count`, external-unit helpers (`EXTERNAL_MMU_*`, `make_external_mmu_feeder`)
- `src/libslic3r/src/libslic3r/GCode/FlushVolCalc.{hpp,cpp}` — colour-based purge volumes (Orca port)
- `src/libslic3r/src/libslic3r/GCode/WipeTower.cpp`, `GCode.cpp` (`custom_gcode_changes_tool`, toolchange), `GCode/ToolOrdering.cpp` — G-code side
- see `docs/design/EXTERNAL_MMU.md` "Implementation status" for the full file list

## UI (sidebar rework area)
- `src/slic3r-shared/src/Slic3r/App/AbstractRenderLayout.cpp` — columns (`init_left_column`, right column `SidebarStackLayout`)
- `src/slic3r-shared/src/Slic3r/App/Sidebar*.cpp` — current sidebar sections
- `src/slic3r-shared/src/Slic3r/App/{PrintSettingsDialog,LogicalPrinterSettingsDialog,MaterialSettingsDialog}.cpp`, `Yoga/AbstractSettingsDialog.cpp` — settings dialogs (tabs/pages per category)
- `src/slic3r-shared/src/Slic3r/App/Config/ConfigRowItems.cpp` — setting row widgets to reuse

## Dev tools
- `tools/dev/prebuild_checks.py`, `check_i18n.py`, `untranslated.py` — static checks (see NOTES.md "Shell/tooling")
