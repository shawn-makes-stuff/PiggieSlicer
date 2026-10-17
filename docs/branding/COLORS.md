# Colours & theming — rebrand map

## Brand palette (applied 2026-10-17, sampled from `piggie_logo.jpg`)

| Name | Hex | RGB | Role |
|---|---|---|---|
| Piggie pink | `#D07682` | 208,118,130 | Accent everywhere orange/blue/green accents were (`k_piggie_pink`, `ColorRGB(A)::PIGGIE_PINK()`) |
| Piggie black | `#292929` | 41,41,41 | Light-theme text + icon recolour, dark-theme alt background (`k_piggie_black`) |
| Deep pink | `#A34B58` | 163,75,88 | Pink text on light backgrounds (4.7:1 on #EAEAEA) |
| Light pink | `#E08A96` | 224,138,150 | Pink text/hover on dark backgrounds (5.6:1 on #2B2B2B) |
| Pale pink | `#F0B8C0` | 240,184,192 | Default button label, dark |
| Selected (dark) | `#634448` | 99,68,72 | Pink blended into piggie black: selected button/header |
| Selected (light) | `#D7AEB4` | 215,174,180 | Selected button, light |

Kept: links stay blue, functional colours stay (error red, success/system green, warning, AccentSecondary blue, AccentTertiary purple for revert, gizmo/preview colours).

Everything below describes the **original** Prusa values and where they lived.

Prusa orange appears in several slightly different values. All are listed below with the file that defines them.

## 1. Central theme (main lever)

`src/slic3r-shared/src/Slic3r/App/Theme.cpp` — `Platform::Color` palette, dark + light, feeds ImGui, wx widgets (via `WidgetsConfig`) and scene background.

| Token | Dark | Light | Line |
|---|---|---|---|
| `k_prusa_orange` | — | `0.874,0.365,0.176` (#DF5D2D) | 14 |
| AccentPrimary | `223,93,45` (#DF5D2D), disabled `200,150,130` | `k_prusa_orange` | 131 / 226 |
| AccentSecondary | `0.32,0.48,0.84` blue | same | 134 / 227 |
| AccentTertiary | `175,119,255` purple | same | 135 / 228 |
| Error / Success / Warning | red / green / `255,193,7` | red / green / `0.85,0.47,0.02` | 136–138 / 229–231 |
| TextLink | `0.16,0.29,0.48` | `0.13,0.28,0.65` | 63 / 156 |
| NavCursor | `0.26,0.59,0.98` blue | same | 77 / 171 |
| Button active (selected) | `54,73,117` navy | `120,159,250` blue | 87,124 / 181,196,219 |
| RadioButton active | `78,128,248` blue | — | 110 |
| WindowBg / Alt | `27` / `41` grey | `235` / `220` grey | 67–68 / 160–161 |
| Scene background top/bottom | `0.35`/`0.23` grey | `0.75`/`0.62` grey | 141–144 / 234–237 |

ImGui style tables: `initialize_imgui_dark_style()` (250) still uses ImGui's stock **blues** for FrameBg, TitleBgActive, CheckMark, SliderGrab, Header, Separator, ResizeGrip, TextSelectedBg. `initialize_imgui_light_style()` (326) uses `k_prusa_orange` for FrameBgHovered/Active, CheckMark, SliderGrab, SeparatorActive, ResizeGripActive.

Light-theme icon recolour: `Theme::set_style` line 45 swaps white → `#0B0B0B` in Yoga icons.

`Theme.cpp.orig` is a merge leftover in git. Remove it.

Theme accent consumers: AccentPrimary 23 refs / 17 files, AccentSecondary 15 / 12, AccentTertiary 25 / 16 (`git grep AccentPrimary`).

## 2. wx (classic dialogs/tabs) colours

| What | Value | Where |
|---|---|---|
| Modified-value label (default) | dark `253,111,40` / light `252,77,1` (orange) | `WidgetsConfig.cpp:160` |
| System-value label (default) | dark `115,220,103` / light `26,132,57` (green) | `WidgetsConfig.cpp:155` |
| Hovered button label | dark `253,111,40` / light `252,77,1` | `WidgetsConfig.cpp:122` |
| Default button label | dark `255,181,100` / light `203,61,0` | `WidgetsConfig.cpp:123` |
| Selection highlight | dark `78,78,78` / light `190,200,215` | `WidgetsConfig.cpp:121` |
| Window default bg | dark `43` / light `234` | `WidgetsConfig.cpp:129` |
| Mode palette (Simple/Adv/Expert) | `#7DF028 #FFDC00 #E70000` | `WidgetsConfig.cpp:165` |
| Border hovered, focused bg | `0xED6B21` | `Widgets/UIColors.hpp:6,11` |
| Button checked | `0x00AE42` green, hover `0x37EE7C` | `Widgets/Button.cpp:34-35` |
| SwitchButton thumb on | `0x00AE42` green | `Widgets/SwitchButton.cpp:17` |
| Config diff dialog "orange" | `#ed6b21` | `DiffDVCModel.cpp:83` |
| Splash highlight text | `237,107,33` | `slic3r-app-desktop/.../SplashScreen.cpp:115` |

Users can override the sys/mod label colours in the descriptions dialog (`slic3r-app-desktop/.../GUI_Descriptions.cpp:56-57,165`). The defaults come from `WidgetsConfig`.

Note: `src/slic3r/` (old `GUI/` tree, incl. Preferences.cpp, HintNotification.cpp, Gizmos/) is **not built** (absent from `src/CMakeLists.txt`). Ignore its colours.

## 3. 3D scene / gizmo colours

| What | Value | Where |
|---|---|---|
| `ColorRGB(A)::ORANGE()` | `0.92,0.50,0.26` | `slic3r-domain/include/Slic3r/Domain/Color.hpp:58,173` |
| Scale gizmo handles | `ORANGE()` | `Plater/ScaleGizmo.cpp:274` |
| Bed contour, print-volume outline, bed label, selection border | `ORANGE()` | `Scene/BedMaterials.cpp:34,37,45,49` |
| Bed plate / grid / error | greys, red | `Scene/BedMaterials.cpp:21-42` |
| Arrange dialog primary | `ImColor{223,93,45}` hardcoded | `Plater/ArrangeDialog.cpp:299` |
| Cut gizmo parts/plane/connectors | cyan/magenta/yellow | `Plater/CutGizmo.cpp:44-60` |
| Measure gizmo features | teal/purple | `Plater/MeasureGizmo.cpp:41-45` |

These are functional colours rather than brand colours, except `ORANGE()` and ArrangeDialog. Changing `ORANGE()` changes every 3D highlight at once.

## 4. Icons (SVG)

- `#ED6B21` appears **433 times in 192 SVGs** in `resources/icons`. It is the accent in toolbar, menu, notification, gizmo and printer-status icons.
- `#808080` (384×) is the neutral grey. It becomes `#FFFFFF` in dark mode.
- `BitmapCache.cpp:425` and `:518` replace `#ED6B21` (and `#808080`) with a per-call `new_color` parameter of `get_bmp_bundle()` (`BitmapGetters.cpp:20`). It is used to tint individual icons, not as a global setting. Yoga/ImGui icons load through `Yoga/Icon.cpp`, which has its own replace map (`set_replace_strings`, line 156) and does not swap `#ED6B21`.
- **Simplest rebrand:** run a single sed of `#ED6B21` over `resources/icons/*.svg`. That covers both load paths. Then update the `BitmapCache.cpp` match keys to the new hex so per-call tinting keeps working.
- Printer thumbnails in `resources/presets/.../PrusaResearch/assets/mk*.svg` also contain `#ED6B21`. These are Prusa product art.

## 5. Web pages

`resources/web/*.html` (Connect/login/error pages, light + `_dark` pairs) use the accent `#FA6831` (14×), plus `#231F20`, `#121212` and `#FF0033`, all as inline CSS.

## 6. Non-brand colours (leave)

- libslic3r debug-SVG colour names ("orange", etc.). These only appear in debug dumps.
- G-code preview feature colours: `libpgcode/Types.hpp`, `Preview/FdmViewerWrapper.cpp`, `libvgcode`.
- Test data.

## Rebrand checklist (minimum set)

1. `Theme.cpp`: `k_prusa_orange`, dark AccentPrimary (+ its disabled value), and, if the blue selection is unwanted, Button/RadioButton active plus the dark ImGui blues.
2. `WidgetsConfig.cpp:122-123,160`: modified-label and button-label oranges.
3. `UIColors.hpp:6,11`, `DiffDVCModel.cpp:83`, `SplashScreen.cpp:115`, `ArrangeDialog.cpp:299`.
4. `Domain/Color.hpp` `ORANGE()` (3D highlights, bed outline).
5. Icons: sed `#ED6B21` in `resources/icons/*.svg`, plus the two match keys in `BitmapCache.cpp:425,518`.
6. `resources/web/*.html`: `#FA6831`.
7. Optional: green `0x00AE42` on Button/SwitchButton.

Hardcoded colour literals overall: 273 in 66 source files (counting the unbuilt `src/slic3r/`). Hotspots in built code are `Theme.cpp`, `ProjectSettingsInteractor.cpp`, `UIColors.hpp`, `CutGizmo.cpp` and `BitmapCache.cpp`. Pulling them all into Theme would be a separate cleanup job.
