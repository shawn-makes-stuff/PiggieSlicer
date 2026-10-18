# PiggieSlicer V2 — notes

Topics: Build · Shell/tooling · Brand · Rename & compatibility · Translations · Cloud removal · Presets (3.0 format) · Orca import · Multi-material (feeders) · UI · User rules

## Build
- Windows, VS 2022 Build Tools at `D:\VSBuildTools`, generator "Visual Studio 17 2022" x64. Deps are built in `deps/build` (Release only, `-DDEP_DEBUG=OFF`); app build dir is `build/`, configured with `-DCMAKE_PREFIX_PATH=D:/Projects/PiggieSlicer_V2/deps/build/destdir/usr/local`.
- Outside a VS developer prompt the WinRT header for Win10 mesh repair is not found. Set the environment variables `WindowsSdkDir="C:/Program Files (x86)/Windows Kits/10/"` and `WindowsSDKVersion="10.0.26100.0"` (`cmake/modules/WinSDK.cmake:15`).
- Upstream bug, fixed in `CMakeLists.txt` (commit b5be80598d): a re-configure on MSVC failed because `SLIC3R_PCH=ON` stayed cached. It is now forced OFF in the cache.
- ATL headers were missing. The VS component `Microsoft.VisualStudio.Component.VC.ATL` was installed 2026-10-17 (`RemovableDriveServiceWin32.cpp` and the WebView utils need it).
- Memory: unlimited `--parallel` (16 projects × 16 cl) ran the 32 GB machine out of memory and the build was killed. Use `-- /m:2 /p:CL_MPCount=4 /nodeReuse:false` (≈8 compiles at once). The user allows ~20 GB for builds.
- The deps build took ~2 h. A full app build took ~1 h at those limits. Incremental builds take minutes.
- The executable is `build/src/slic3r-app-launcher/Release/piggie-slicer.exe` (target `slic3r-app-launcher`, `OUTPUT_NAME piggie-slicer`). The link fails with LNK1104 if the app is running.

## Shell/tooling
- Git Bash rewrites `/m` into `M:/`. Use `MSYS_NO_PATHCONV=1`, or `cmake --build ... --parallel N`.
- The Bash tool collapses `\\` inside heredocs. Write Python scripts that need backslashes with the Write tool, not heredocs.
- Many source files are CRLF, so keep each file's line endings (edit with `newline=''`). `core.autocrlf=true`, so LF→CRLF warnings are expected noise.
- `tools/dev/prebuild_checks.py` (deleted headers still included, removed symbols, icon table → files, CMake source lists), `check_i18n.py LANG` (renamed msgids exist in source) and `untranslated.py LANG` (count of missing translations). Run them from the repo root.

## Brand
- Piggie pink `#D07682` (208,118,130) and piggie black `#292929` (41,41,41), sampled from `piggie_logo.jpg`. The sticker mark is used everywhere. The user rejected the badge (white disc).
- Theme tokens are in `src/slic3r-shared/src/Slic3r/App/Theme.cpp` (`k_piggie_pink`, `k_piggie_black`). The 3D accent is `ColorRGB(A)::PIGGIE_PINK()` in `Domain/Color.hpp`. Map of every colour: `docs/branding/COLORS.md`.
- Assets come from `branding/clean_logo.py` + `branding/make_icons.py` (run them with user-site Python, i.e. `python`, not `python -I`; PIL/numpy/scipy/potrace are installed in the user site). Splash bg `#F9EFF0`.

## Rename & compatibility
- `version.inc`: APP_NAME/KEY = PiggieSlicer, BUILD_ID = PiggieSlicer-<ver>. The data dir is APP_NAME+major (`PiggieSlicer3-dev`, `Directories.cpp:227`).
- Files must stay PrusaSlicer-compatible (user decision). `SLIC3R_COMPAT_APP_KEY`/`SLIC3R_COMPAT_BUILD_ID` in `Version.hpp.in` are used for the 3MF Application metadata, SL1 and the G-code producer. Both 3MF readers require the `PrusaSlicer-` prefix (`3mf.cpp:765`, `Model3mf.cpp:921`). `header_slic3r_generated()` is hard-coded "PrusaSlicer". Keep all of these.
- Single-instance IPC: Windows prop `Is_Piggie_Slicer`; DBus `io.github.shawn_makes_stuff.piggieslicer.*`; mac `OtherPiggieSlicer*`. The app id is `io.github.shawn_makes_stuff.PiggieSlicer`.
- Versions: `PIGGIESLICER_VERSION` in `version.inc` is the ONE user-facing version (0.0.1-alpha). CMake derives `SLIC3R_APP_VERSION_DISPLAY` ("0.0.1 Alpha"), the Windows RC numbers and `SLIC3R_BUILD_ID` from it. The splash, Welcome title, `--version`, window title, file properties and user-agent use those. `SLIC3R_VERSION` stays 3.0.0-alpha12 (PrusaSlicer base) for G-code/3MF headers, the preset updater's vendor-index compatibility (`PresetUpdaterIndex.cpp`), the `[version]` placeholder and the settings folder name.

## Translations
- The domain is the app key, so the catalogs are `resources/localization/*/PiggieSlicer.{mo,po}` and `PiggieSlicer.pot`. The .mo files were edited directly (sorted, no hash table) and verified with Python `gettext` round-trips.
- User decision: no new translations. Upstream catalogs predate the 3.0 UI (~1,160 of ~2,960 strings are untranslated in every language).

## Cloud removal
- All Prusa Account / Connect / Printables / `prusaslicer://` code is deleted, along with the legacy `src/slic3r/` tree. The preset updater never goes online (`DesktopApp.cpp` `online_allowed` returns false). LAN print hosts are kept (`PrintHostFactory.cpp`). Leftovers: `docs/branding/CLEANUP.md`.

## Presets (3.0 format)
- Vendor = `vendor.yaml` (features, printer_families, kind: printer/tool/feeder/sheet/printer_config) + `preset-*.yaml` condition trees with stable base64 ids. Schemas: `specs/presets/*.json`. Valid setting keys come from `src/slic3r-domain/src/Slic3r/Domain/ConfigDefsFDM.cpp`.
- Start-up cost: `PresetInteractor::load_preset_bundle` ("TODO: remove when config wizard is ready" block) builds a HwPrinterConfig for every template of every vendor the user has no printer from, then `update_vendor_presets` evaluates all of them. With 70 vendors that is ~100 s per launch. `AddPrinterPanel` lists printers from those pre-evaluated configs (`evaluated_presets`), so removing the block alone empties the picker.

## Orca import
- The source is the user's OrcaSlicer 2.5.0-alpha install. Its profiles are binary `.opc` caches (magic `ZCRO`, `C:\Program Files\OrcaSlicer\resources\profiles\`), equivalent to the JSONs at github `OrcaSlicer/OrcaSlicer` tag `v2.5.0-alpha` (Orca `docs/HLSD/preset-cache.md`). The converter fetches the JSONs and refuses to run if a vendor's version differs from the installed `.opc`. Assets (covers/bed STL/SVG) are read from the install folder.
- Stock profiles only. Never read `%APPDATA%\OrcaSlicer\user` (user decision).
- The converted profiles use only upstream 3.0 keys (checked against commit 30ef59195e). The 4 non-upstream names are Snapmaker vendor feature flags, which the format allows. Details, fallbacks and per-vendor status: `docs/profiles/ORCA_IMPORT.md`, `docs/profiles/orca-import/*.md`.
- Last full e2e (older converter): 974/1184 configs; 51/70 vendors fully pass. Many failures came from an over-strict test script; that is fixed but not yet re-run.

## Multi-material (feeders)
- Orca (v2.5.0-alpha): filaments are independent of nozzles and mapped to extruders (`filament_map`, `physical_extruder_map`), with units per extruder (`extruder_ams_count`). The printer's `change_filament_gcode` decides the mechanics. If that G-code has no T command, Tn is appended (`custom_gcode_changes_tool`). Two towers (`wipe_tower_type` type1 Bambu / type2 Prusa-derived), `purge_in_prime_tower`, separate `enable_filament_ramming`, `flush_volumes_matrix` × `flush_multiplier`, colour-based flush calc, Moonraker filament sync via `/server/database/item?namespace=lane_data` (AFC, recent Happy Hare) with Happy Hare fallback (`MoonrakerPrinterAgent::fetch_filament_info`).
- PiggieSlicer 3.0: `HwPrinterConfig.feeders` is keyed by Address = tool index (`HwConfig.cpp` printer naming looks up `feeders[{i}]`). `material_slot_count()` = tools + Σ(feeder slots − 1). `FeederType` drives no behaviour. Feeder vars come from the instance (`ValueMapBuilder.cpp`). The user-set `slot_count` persists (`HwConfigJson.cpp`). SEMM works without a tower (`GCode.cpp` `if (single_extruder_multi_material && !wipe_tower)`).
- Design + decisions + implementation status: `docs/design/EXTERNAL_MMU.md`.

## UI
- Orca flow map and gaps: `docs/design/ORCA_FLOW.md`; sidebar layout read from Orca's source: `docs/design/ORCA_SIDEBAR.md`.
- Dialogs open only if registered in the module's `DialogNavigation` (`init_dialog_navigation` in Plater and Preview); an unregistered dialog silently stays closed. A child dialog opens its whole branch.
- Printer / filament lists show favourites (= installed, Orca) by default; adding a printer favourites it; a printer's `default_materials` (vendor.yaml printer def) are favourited when its filament list has none.
- Purge matrix: project `wiping_volumes_matrix` + `wiping_volumes_use_custom_matrix`; `ProjectSettingsInteractor::wiping_volumes / set_wiping_volumes`; a colour change recalculates that slot's row/column (Orca `Sidebar::auto_calc_flushing_volumes`).
- ImGui + Yoga layout (not wx widgets). Today the left column holds the object list and the right column the sidebar (`SidebarStackLayout`: `SidebarBed`, `SidebarPrint` with ⚙ → `PrintSettingsDialog`, `SidebarObject`). Settings pages are built per `ConfigItemDef::Category` from `ConfigRowItems`.
- Planned Orca-style left sidebar: `docs/design/ORCA_SIDEBAR.md`.

## User rules
- Replicate Orca's real logic from its source; no invented restrictions.
- No subagents unless the user asks. Ask before long builds/runs (give a time estimate). Builds only when the user allows. Don't touch the user's settings folder; use a scratch `--datadir`.
- Commits only when asked. No AI attribution lines.
