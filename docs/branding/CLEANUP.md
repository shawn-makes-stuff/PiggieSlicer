# Rebrand cleanup list

Items left after the 2026-10-17 rename and cloud-disconnect pass. Users can't see any of these in the built app; they are dead code or docs to tidy later.

## Leftovers after the Prusa cloud removal
Account, Connect, Printables, the file downloader and the `prusaslicer://` scheme are deleted (2026-10-17). What is left:
- `Biz/Network/ServiceConfig` keeps only the preset repository URL (`preset-repo-api.prusa3d.com`, `PRUSA_PRESET_REPO_URL` env override still whitelists Prusa/Printables domains) and the `--webdev` flag used by the WebView. The preset updater never goes online (`online_allowed` returns false in `DesktopApp.cpp`), so the URL is never contacted.
- `enable_preset_update` stays `Category::Hidden`. The removed keys `enable_prusa_account`, `enable_printables`, `downloads_directory` in old config files load as ignored extra keys.
- Now unused, kept because it is generic code:
  - secret store (`ISecretStore`, `SecretStoreFactory*`, `SecretStoreLinux`, `Biz/WX/SecretStore`, `SecretStoreDummy`); only the token store used it. `SecretStoreLinux.cpp` log lines still say "UserAccount".
  - `IDialogManager::show_webview_dialog`, `WebViewDialog` and `new_web_view_dialog` (no caller left; the physical-printer tab uses `WebViewPanel`).
  - `AbstractBrowserLogic::access_token()`, the `LoadRequest` command and the token parameter of `load_request()`; the `SwitchToSlicing`, `EndModalOK/Cancel` and `DeleteCookiesWithCounter` commands; `AbstractWebViewPanel::set_next_show_url`; the `data` argument of `MainFrame::switch_left_tab`.
  - `TabsBarMenus` (only the empty `main`/`workspaces` menus are left), `TabsBarCtrl::ButtonWithPopup`, `LeftBarCtrl::UnselectPopupButtons` (empty).
  - `PopNotificationLayoutImageHeader` (was only used by the login notification).
  - `system_downloads_dir()` in `slic3r-base/Directories.cpp`.
- Legacy config parsing still knows `htPrusaConnect` / `htPrusaConnectNew` (`Biz/Config/Legacy/PrintConfig.*`).
- `docs/branding/CLOUD.md` describes the code before the removal; read it as history.

## Help menu
- Removed: Prusa Website, Sample G-codes, Software Releases, Report an Issue. Only "Show Configuration Folder" is left. Their labels are still in `MenuBuilder.cpp` (`PSWebsite`, `Samples`, `Releases`, `ReportAnIssue`).
- To do: add PiggieSlicer's own website/issues links once there is a public home.
- The preset dialog's "Online sources" heading now really means the bundled vendor list. Rename it if that's confusing.

## Docs and repo files still written for Prusa
- `doc/*` (build and localization guides still say PrusaSlicer). The README is rewritten; `.github/` (Prusa contributing guide, issue templates, and CI workflows that called Prusa's reusable actions) was removed.

## Files with Prusa names that are still used
- `src/platform/msw/PrusaSlicer.manifest.in` IS used (launcher CMakeLists). Only its file name is Prusa; the content is generic.

## Translations
- Catalogs are now `resources/localization/*/PiggieSlicer.{mo,po}` and `PiggieSlicer.pot`; the domain is the app key.
- Decision (2026-10-17): no new translations. Upstream catalogs predate the 3.0 UI (about 1,160 of ~2,960 source strings are untranslated in every language), so screens like Welcome and the preset updater are English everywhere. Our rename lost no existing translation. Our new strings stay English:
  - the Welcome intro and the "runs safely alongside PrusaSlicer" note
  - the preset dialog's offline note
  - "Presets can also be updated from a ZIP file."
- Strings of the removed Account/Connect/Printables UI (Welcome login/online/crash-report screens, account menu, download and Connect notifications) are now unused catalog entries.
- `resources/localization/list.txt` (xgettext input) lists 3 files that do not exist: `App/AddPrinterDialog.cpp`, `App/Yoga/ProjectButton.cpp`, `App/Yoga/VariableLayerHeightControl.cpp`.
- 4 catalog entries were already stale before the rename (different wording from the source): the window-position crash notice, SSL store detected, and the meters/inches unit questions.

## Kept on purpose (do not "fix")
- File-format identity: `SLIC3R_COMPAT_APP_KEY` / `SLIC3R_COMPAT_BUILD_ID` (`Version.hpp.in`) and `header_slic3r_generated()`. Also the 3MF `Prusa_Slicer_*` keys, the G-code `prusaslicer_config` block, `PrusaSlicer3_project.json`, the SL1 `prusaslicer.ini/json`, the `schemas.prusa3d.cz` relationship ID, and `GCodeProducer::PrusaSlicer`.
- Credits: the splash/credits text and copyright lines in the `.rc`/`Info.plist` keep Prusa Research and Alessandro Ranellucci.
- Text that really means PrusaSlicer: "PrusaSlicer 2.x projects", "newer version of PrusaSlicer", "does not contain PrusaSlicer configuration".
- GitHub issue links in code comments, and in the window-position crash dialog (they describe upstream bugs).

## Other
- `slic3r-shared/CMakeLists.txt` lists `src/Slic3r/Yaml/YamlAdapterLibfyaml.{hpp,cpp}` for `SLIC3R_YAML=libfyaml`; the files do not exist.
- macOS document icons `stl.icns`, `gcode.icns`, `bgcode.icns` are referenced by `Info.plist.in` but have never existed.
- App ID used for DBus, the macOS bundle and Flatpak: `io.github.shawn_makes_stuff.PiggieSlicer` (`io.github.shawn-makes-stuff.PiggieSlicer` for the bundle).
