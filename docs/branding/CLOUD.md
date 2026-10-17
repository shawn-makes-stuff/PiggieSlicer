# Cloud / online services inventory

Scope: built code only (`src/slic3r-*`, `src/lib*`, `src/platform`, `resources/`). `src/slic3r/` ignored.
All paths relative to `src/` unless noted. SERVICE = data exchanged; LINK = user-clicked URL only.

## 0. Central switches (already present)

| Switch | Where | Effect |
|---|---|---|
| `ServiceConfig` | `slic3r-shared/src/Slic3r/Biz/Network/ServiceConfig.cpp:47-53` | Single source of all Prusa hosts (connect, account, media, preset-repo, printables). Env overrides `PRUSA_*_URL` (whitelisted to prusa/printables domains, :59-65). |
| `SLIC3R_REPO_URL` | root `CMakeLists.txt:30`, `slic3r-shared/CMakeLists.txt:1486`, `ServiceConfig.cpp:55` | Compile-time override of preset repo URL. |
| `SLIC3R_SENTRY_DSN` | root `CMakeLists.txt:54-58` (default empty = off) | Sentry compiled in only if non-empty. |
| `SLIC3R_ENABLE_WEBKIT` | root `CMakeLists.txt:50` (default ON) | Defines `SLIC3R_HAS_WEBKIT` in slic3r-shared / shared-wx / app-desktop; links WebView2/WebKitGTK. |
| AppConfig `enable_prusa_account` / `enable_printables` / `enable_preset_update` / `sentry` | `slic3r-shared/src/Slic3r/App/AppConfig.cpp:177-203, 344-352` | All default **false** already. |
| `AppConfig::is_prusa_account_enabled/is_printables_enabled` | `AppConfig.cpp:456-471` | Return false if no `SLIC3R_HAS_WEBKIT`, else the config key. |

## 1. Prusa Account (login/logout, tokens, avatar)
- **Purpose**: OAuth2 PKCE login to Prusa Account; token store; gates Connect/Printables features.
- **Endpoints (SERVICE)**: `https://account.prusa3d.com` + `/o/token/`, `/api/v1/me/`, `/logout` (`ServiceConfig.hpp:47-75`); client id `ServiceConfig.cpp:50`; redirect `prusaslicer://login` (`UserAccountCommunication.cpp:54`, `UserAccountSession.cpp:151`); `https://www.printables.com/auth/get-secret-token` (`ServiceConfig.hpp:62`); avatar downloaded from URL returned by /me (`UserAccountCommunication.cpp:107-140`, host not hardcoded).
- **UI entry points**: top/left-bar account button "Sign in" (`slic3r-app-desktop/src/.../LeftBarCtrl.cpp:29-41`, `TabsBarMenus.cpp:33-46`); login webview dialog (`MainFrame.cpp:132-139`, `BrowserLogicLogInRedirect`); Welcome dialog LogInScreen (`slic3r-shared/src/Slic3r/App/WelcomeDialog.cpp:1182-1330`); Preferences > Services checkbox "Enable Prusa Account" (`AppConfig.cpp:177-185`); notifications via `PopNotificationCenter` (IUserAccountListener, `DesktopApp.cpp:346-349`).
- **Code**: `slic3r-shared/src/Slic3r/Biz/UserAccount/*`; start = `UserAccountInteractor::init(bool)` (`UserAccountInteractor.cpp:16-49`) called `DesktopApp.cpp:537` and `MainFrame.cpp:353-358` (on config change). When false it installs `UserAccountCommunicationDummy` (no network).
- **Gate/disable point**: make `AppConfig::is_prusa_account_enabled()` (`AppConfig.cpp:464`) return false (or `enable_prusa_account` Hidden + false). Also `DesktopApp.cpp:300` resets token store when disabled.
- **Still to hide**: account button (`LeftBarCtrl.cpp:41` `ShowUserAccount(...)` already follows the same flag, verify account menu hidden), Welcome LogInScreen, Preferences checkbox (`AppConfig.cpp:176-194` block), `prusaslicer://login` URL scheme registration (`DesktopApp.cpp:294`, `slic3r-shared-wx/.../UrlProtocolRegistration*`; also handler `DesktopApp.cpp:723`).
- **Risks**: Welcome `sync_config()` (`WelcomeDialog.cpp:1572-1574`) writes these keys to `m_online` -> choosing "online" re-enables. `PhysicalPrinterSettingsDialog.cpp:59-68` and `PhysicalPrinterInteractor.cpp:125` rely on `is_logged_in()` (false when disabled = fine, hides Connect entry).

## 2. Prusa Connect (left-bar tab, upload, printer list)
- **Purpose**: Connect web UI in a webview, G-code upload to Connect, printer sync for Welcome.
- **Endpoints (SERVICE)**: `https://connect.prusa3d.com` + `/slicer/status`, `/slicer/v2/printers`, `/slicer-select-printer`, `/app/printers/`, `/app/teams`, `/slicer-print` (`ServiceConfig.hpp:12-45`). Upload/register/test in `Biz/PrintHost/PrintHostPrusaConnect.cpp:101-260`; printer list `Biz/Connect/ConnectMessageHandler.cpp:93-98`.
- **UI entry points**: left-bar tab "Connect" (`MainFrame.cpp:618-632` `init_printers_page`, `LeftBarTabs::Printers`); File > Export > "Send G-code" (`MenuCommandRegistrar.cpp:2135-2152`, enabled only if logged in); preview sidebar primary button "Send to Connect" (`slic3r-shared/src/Slic3r/App/Preview/SidebarPreviewActionButtons.cpp:152-155`); physical-printer destination `PRUSA_CONNECT_UUID` always added to list (`Biz/PhysicalPrinter/PhysicalPrinterInteractor.cpp:71`, selectable only if logged in `:125`); ConnectSelect webview dialog (`App/ResultExport/ExportActions.cpp:75-115`); Welcome PrinterScreen printer sync (`WelcomeDialog.cpp:792-860`); resources `resources/web/connect_*.html` (local pages, no external hosts - verified).
- **Code**: `BrowserLogicConnectPage/Select`, `ConnectMessageHandler`, `PrintHostPrusaConnect`, `ProjectInteractor::do_result_upload_connect` (`Biz/ProjectInteractor.cpp:981-1004`).
- **Gate/disable point**: same as Account (`is_prusa_account_enabled()`): `init_printers_page` returns early when false; send-gcode menu and Connect destination require `is_logged_in()`.
- **Still to hide**: `PhysicalPrinterInteractor.cpp:71` Connect entry (harmless/filtered), "Send G-code" menu item (disabled, not hidden), Welcome marketing row "PrusaConnect cloud printing" (`WelcomeDialog.cpp:468-475`), `select_connect_upload*` callers (`PhysicalPrinterInteractor.cpp:162-172`, `ProjectInteractor.cpp:1004`).
- **Physical-printer host types**: `Domain::PrintHostType` (`slic3r-domain/include/Slic3r/Domain/ConfigPhysical.hpp:17-28`) has NO Connect type; Connect is the separate payload `PhysicalPrinter::ConnectUpload` (`PrintHostFactory.cpp:56-57`). Legacy enum `htPrusaConnect/htPrusaConnectNew` exists in `Biz/Config/Legacy/PrintConfig.hpp:82` (legacy config parsing only; unverified whether reachable in UI).

## 3. Printables
- **Purpose**: Printables.com web tab; download models via `prusaslicer://open`; send to Connect.
- **Endpoints**: SERVICE `https://www.printables.com` (+`/homepage`, `/auth/get-secret-token`; `BrowserLogicPrintables.cpp:23,663`), `https://media.printables.com` (`media_url`; defined but no caller found - verified by grep). File download whitelist `printables.com, testprusaverse.com, thingiverse.com, cults3d.com`, https only (`Biz/FileDownloader/FileDownloaderInteractor.cpp:45-61`, job `FileDownloaderJob.cpp`).
- **UI entry points**: left-bar tab "Printables" (`MainFrame.cpp:640-651`, `update_printables_left_bar` on config change `MainFrame.cpp:345-350`); Preferences > Services "Enable Printables" (`AppConfig.cpp:186-194`); Welcome row "Printables Database integration" (`WelcomeDialog.cpp:459-466`).
- **Code**: `slic3r-shared/src/Slic3r/App/Browser/BrowserLogicPrintables*.cpp`, `BrowserLogicPrintablesToConnect.cpp`.
- **Gate/disable point**: `AppConfig::is_printables_enabled()` (`AppConfig.cpp:456`) -> false. `init_printables_page` returns early.
- **Risks**: `prusaslicer://open?file=` handler (`DesktopApp.cpp:582,725`, `AbstractAppInstanceMessageHandler.cpp:133`, CLI `LoadPrintData.cpp:199`) still downloads from the whitelist even with tab off; decide whether to remove.

## 4. Sentry crash reporting
- **Purpose**: crashpad/sentry-native upload on crash.
- **Endpoints**: DSN from `SLIC3R_SENTRY_DSN` (empty in repo; real DSN not in source - verified).
- **UI entry points**: Preferences > Services "Enable crash reporting" (`AppConfig.cpp:344-352`, only `#ifdef SLIC3R_SENTRY`); Welcome SentryScreen (`WelcomeDialog.cpp:1127, 1423`, skipped when `!is_sentry_available()`); `MainFrame.cpp:388-390` reacts to key.
- **Code**: `slic3r-app-launcher/src/Slic3r/App/Launcher/SentryScope.cpp` (all `#ifdef SLIC3R_SENTRY`), instantiated `main.cpp:116-119` iff `is_sentry_enabled()`; link in `slic3r-app-launcher/CMakeLists.txt:26-44`.
- **Gate/disable point**: leave `SLIC3R_SENTRY_DSN` empty (current default). Nothing else needed; UI auto-hides.
- **Risks**: none; just never set the cache var.

## 5. Online preset / profile updater
- **Purpose**: fetch preset-source list + vendor bundles/indices from repository servers; background check at startup.
- **Endpoints (SERVICE)**: `https://preset-repo-api.prusa3d.com/v2/repos` (`ServiceConfig.cpp:52`, `.hpp:92`, fetched in `Biz/PresetUpdater/PresetUpdaterRepositoryDatabase.cpp:679`); the per-repo URLs come from the downloaded manifest (not hardcoded). Bundled `resources/presets/RepositoryManifest.json` points at `http://localhost:8000/prusa-research-fff/` (dev placeholder, `selected: true`) - such repos are `is_online()==true` (`PresetUpdaterRepository.hpp:156`) so they are also skipped offline. LINK: `https://help.prusa3d.com/slicer-profiles/3` (`App/PresetUpdater/PresetUpdaterDialog.cpp:33,224`). Dead dev URLs `10.24.3.3:8001` only in a comment (`RepositoryDatabase.cpp:314-324`).
- **UI entry points**: Configuration menu > "Preset Sources & Updates" (`MenuCommandRegistrar.cpp:1819-1827`, `ModalDialog::PresetUpdater`); startup `LoadingRenderModule` / `preset_updater_controller().start()` (`DesktopApp.cpp:372-413`); background check `DesktopApp.cpp:588`; status/install/problem notifications (`App/PresetUpdater/PresetUpdater*Notification.cpp`); Preferences > Services "Update presets over the internet" (`AppConfig.cpp:196-204`); Welcome online decision (`WelcomeDialog.cpp:1581`); CLI (`slic3r-app-cli/.../ProcessActions.cpp`, `PresetUpdaterCLI.cpp`). Menu items `CheckConfigUpdates`, `CheckAppUpdates`, `OnlinePresetUpdate`, `SystemInfo`, `TipOfTheDay` exist only as stubs under undefined `SHOW_NOT_IMPLEMENTED_ITEMS` (`MenuCommandRegistrar.cpp:1768-1818,1890-1968`) -> not in the build.
- **Code**: `Biz/PresetUpdater/*`, `App/PresetUpdater/*`.
- **Gate/disable point**: `ControllerEnvironment.online_allowed` lambda `DesktopApp.cpp:358-359` (reads `enable_preset_update`, default false). Offline = "touches no network at all" (`PresetUpdaterInteractor.hpp:54`; `RepositorySync.cpp:672`). Safest hard disable: make that lambda return false and hide the Preferences row.
- **Risks**: updater is also the **local install path** for bundled presets (`resources/presets/prusa-research-fff`), so do NOT remove the controller/module (startup `LoadingRenderModule` depends on it). Dialog shows an "offline note" (`PresetUpdaterDialog.cpp:346`). Hide menu item only if user-added sources are not wanted.

## 6. Hints / news / version check / system info / telemetry
- **Verified absent in built tree**: no app version-check URL, no news/hints fetch (`hints` hits are unrelated UI text), no telemetry/analytics, no system-info upload. `CheckAppUpdates`/`SystemInfo` are unbuilt stubs (see #5). Network callers of `IHttp`/curl are only: UserAccount, ConnectMessageHandler, PrintHost*, PresetUpdater repo/db, FileDownloaderJob, SentryScope (grep list in scratch, all covered here).

## 7. Help-menu / misc LINKS (not services)
All in `slic3r-shared/src/Slic3r/App/MenuCommandRegistrar.cpp` `register_main_menu_help_commands` (:1831+), via `open_browser` (`App/OpenBrowser.cpp`), gated by Preferences "Open hyperlinks in web browser" (`AppConfig.cpp:285`, `hyperlinks_allowed()` `OpenBrowser.cpp:153`):
- Help > Prusa Website -> `https://www.prusa3d.com/slicerweb` (:1842, label `MenuBuilder.cpp:124`)
- Help > Samples -> `https://help.prusa3d.com/article/sample-g-codes_529630` (:1861)
- Help > Software Releases -> `https://github.com/prusa3d/PrusaSlicer/releases` (:1879)
- Help > Report an Issue -> `https://github.com/prusa3d/slic3r/issues/new/choose` (:1921)
- Preset dialog repo link (#5). Other: launcher `main.cpp:71` crash description string `https://github.com/prusa3d/PrusaSlicer`; `BrowserLogicPrintables.cpp:376 openExternalUrl`; `3mf Relations.cpp:23` schema id `schemas.prusa3d.cz` (namespace string, not a request; leave). `resources/lua/com.prusa3d.slicer.calibration/manifest.json` = id only. Localization `.po/.mo` contain prusa3d URLs in translated strings (text only).
- Disable: replace/remove these 4 registrations (they register only here; labels in `MenuBuilder.cpp:124-134`), repoint to PiggieSlicer URLs.

## Keep (LAN)
- Physical-printer hosts via `PrintHostFactory.cpp:25-60`: PrusaLink, PrusaLinkStorage, SL1Host, OctoPrint, Moonraker, Duet, FlashAir, AstroBox, Repetier, MKS (`Biz/PrintHost/PrintHost*.cpp`), plus local `FileSystemExport` (SD/removable). Connect payload is the only cloud one.
- Bonjour/mDNS discovery: `Biz/Network/Bonjour.cpp` (used by PrintHostOctoPrint.cpp, PrintHostPrusaLink.cpp).
- Physical-printer web tab: `BrowserLogicPhysicalPrinter` + `init_physical_printer_page` (`MainFrame.cpp:663-678`) - uses the same webview, so WebView must remain compiled.
- `PrusaLinkStorageListener`, IHttp/HttpCurl core, TLS.

## Recommended disable plan
1. Keep `SLIC3R_SENTRY_DSN` empty (already). Sentry UI auto-hides.
2. Do NOT set `SLIC3R_ENABLE_WEBKIT=OFF`: webview code (`MainFrame.cpp` login/physical-printer panels) is not `#ifdef`-guarded, build breakage likely (unverified), and the LAN printer tab needs it.
3. Force `AppConfig::is_prusa_account_enabled()` and `is_printables_enabled()` (`AppConfig.cpp:456-471`) to return false; this kills Account init, Connect tab, Printables tab, account button, token store.
4. Remove/hide the 3 Preferences checkboxes (`AppConfig.cpp:176-204`: set `Category::Hidden`), keep defaults false; force `online_allowed` false (`DesktopApp.cpp:358`).
5. Welcome dialog: skip online-decision, sentry and login screens; set `m_online=false` and go changelog -> printer screen (`WelcomeDialog.cpp:1370-1470`); remove Printables/Connect marketing rows (:459-475) and make `sync_config()` (:1572-1581) write false.
6. Hide "Send G-code" menu item and Connect destination (`MenuCommandRegistrar.cpp:2135`, `PhysicalPrinterInteractor.cpp:71`, `SidebarPreviewActionButtons.cpp:152`).
7. Hide Configuration > "Preset Sources & Updates" (`MenuCommandRegistrar.cpp:1819`) only if user sources are unwanted; keep updater controller for bundled presets.
8. Repoint/remove the 4 Help links (#7) and `PresetUpdaterDialog.cpp:33`.
9. Decide on `prusaslicer://` scheme registration/handlers (`DesktopApp.cpp:294,582,723`) and `ServiceConfig` defaults (blank them last as a backstop).
