# PiggieSlicer V2 - image catalog

Verified by grep on 2026-10-17. Built code = `src/*` except `src/slic3r/` (legacy tree: in no CMakeLists, so refs there are dead; noted "legacy only"). Dark theme = SVG colour replacement at load; no `_dark` files exist. "Depicts" is inferred from the file name unless stated. Paths in "Where" are relative to `src/` unless they start with `resources/`.

**Loading paths.** (a) ImGui registry: `Icon` enum -> name table `slic3r-render/src/Slic3r/App/Render/ImguiIconHelper.cpp:10-251`, loaded as `icons/<name>.svg` (`ImguiIconHelper.cpp:262-269`, `ImguiRender.cpp:51`). (b) wx: `get_bmp_bundle("name")` / `ScalableBitmap` -> `var(name + ".svg"|".png")` (`slic3r-shared-wx/src/Slic3r/App/WX/BitmapCache.cpp:320,432,465,483,520`). Name-based call sites with a plain string were found by grep of quoted stems/`stem.ext` across all built src.

## Done (2026-10-17)

App logo replaced by the PiggieSlicer sticker mark. Built by `branding/make_icons.py` from `piggie_logo.jpg`: `PiggieSlicer.svg/.ico/.icns/_128px.png/_192px_grayscale.png` and `splashscreen.jpg`. The old `PrusaSlicer*.svg/.ico/.icns/_128px.png` and the gcodeviewer logos are deleted. References were updated in MainFrame, SplashScreen (banner = piggie black, wordmark "Piggie"+"Slicer", one splash for both modes), MsgDialog, ImguiIconHelper, TestRenderModule, the three `.rc.in` files, `Info.plist.in` and the README. The Welcome dialog logo keeps its colours (`set_preserve_colors`).
Not done: app name/IDs (`SLIC3R_APP_NAME`, exe names, `.desktop` Icon names, rc VERSIONINFO), the missing macOS doc icons (stl/gcode/bgcode.icns), and the Prusa service logos (Printables/Connect stay, since they stand for those services).

## 1. Branding-critical (original state)

| Image | Depicts | Used for | Where |
|---|---|---|---|
| resources/icons/PrusaSlicer.ico | App icon (Windows multi-size) | EXE icon of slic3r-app-launcher (resource #2); main window icon on Windows via `wxIconBundle("#2")` | resources: platform/msw/slic3r-app-launcher.rc.in:24 and PrusaSlicer.rc.in:24; slic3r-app-launcher/CMakeLists.txt:3,12; slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:70-72 |
| resources/icons/PrusaSlicer-gcodeviewer.ico | G-code viewer Windows icon | **MISSING FILE.** Referenced by platform/msw/PrusaSlicer-gcodeviewer.rc.in:24; that .rc.in is not configured by any CMakeLists (unwired) | platform/msw/PrusaSlicer-gcodeviewer.rc.in:24 |
| resources/icons/PrusaSlicer.icns | App icon (macOS) | CFBundleIconFile (+ doc-type icons for 3mf/amf/etc.) | platform/osx/Info.plist.in:10,51,68,85; src/CMakeLists.txt:53 (configure_file only, no bundle copy rule found) |
| stl.icns, gcode.icns, bgcode.icns | macOS document icons | **MISSING FILES.** Referenced by Info.plist.in | platform/osx/Info.plist.in:34,102,119 |
| resources/icons/PrusaSlicer.svg | Round Prusa-slicer logo, vector (editor) | Splash-screen logo (svg rendered at 25% width); message-dialog default icon; ImGui `Icon::PrusaSlicerIcon` (menu) | slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:191; slic3r-shared-wx/src/Slic3r/App/WX/MsgDialog.cpp:116; slic3r-shared/src/Slic3r/App/MenuBuilder.cpp:291; ImguiIconHelper.cpp:98 |
| resources/icons/PrusaSlicer-gcodeviewer.svg | G-code viewer logo, vector | Splash logo when launched as G-code viewer; render test texture | slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:191; slic3r-shared/src/Slic3r/App/TestRenderModule.cpp:120 |
| resources/icons/PrusaSlicer_128px.png | 128px PNG app logo | Main frame icon on Linux/macOS (non-Windows) | slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:78 |
| resources/icons/PrusaSlicer-gcodeviewer-mac_128px.png | 128px G-code viewer logo (mac) | Only in a commented-out line; effectively unreferenced | slic3r-shared/src/Slic3r/App/TestRenderModule.cpp:118 (comment) |
| resources/icons/splashscreen.jpg | Splash background photo/art | Splash screen background; text/version/logo drawn on top at runtime | slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:42 (SplashScreen is created at DesktopApp.cpp:277) |
| splashscreen-gcodepreview.jpg | G-code viewer splash background | **MISSING FILE.** Loaded when `is_editor` is false | slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:42 |
| PrusaSlicer_192px_grayscale.png | Greyscale logo | **MISSING FILE.** ErrorDialog logo (error dialog would fail to load it) | slic3r-shared-wx/src/Slic3r/App/WX/MsgDialog.cpp:233 |
| PrusaSlicer_192px.png, PrusaSlicer_192px_transparent.png, PrusaSlicer-gcodeviewer_192px.png, PrusaSlicer.png | Logo PNGs | **MISSING FILES**, referenced only from legacy unbuilt code: About dialog, ConfigWizard background, system-info HTML, Linux desktop-integration icon copy | slic3r/GUI/AboutDialog.cpp:30; ConfigWizard.cpp:2272; SendSystemInfoDialog.cpp:612; DesktopIntegrationDialog.cpp:320,333,419 (all legacy) |
| bullet_white.png | ConfigWizard bullet | **MISSING FILE**, legacy only | slic3r/GUI/ConfigWizard.cpp:2275 |
| resources/icons/printables.svg, connect.svg, connect_gcode.svg, cloud.svg, lb_printables.svg | Prusa services logos (Printables, Connect, cloud) | Printables notification header; Welcome dialog Connect/cloud; physical-printer Connect upload button; left bar Printables tab | ImguiIconHelper.cpp:99-101,161; slic3r-shared/src/Slic3r/App/PopNotification/PopNotificationCenter.cpp:326,361; WelcomeDialog.cpp:439,472,1308; PhysicalPrinterSettingsButton.cpp:88; slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:647 |
| resources/icons/robot.svg, notification_clippy.svg | Mascot-style icons (check if Prusa branded) | robot: settings explanation panel; clippy: registered, enum unused | ImguiIconHelper.cpp:97,252; slic3r-shared/src/Slic3r/App/Config/ExplanationContainer.cpp:62 |
| src/platform/unix/PrusaSlicer.desktop | Linux launcher entry | Name=PrusaSlicer, Icon=PrusaSlicer, Exec=prusa-slicer; icon name resolved from icon theme (no PNG of that name ships in resources/icons) | platform/unix/PrusaSlicer.desktop:2-5 |
| src/platform/unix/PrusaGcodeviewer.desktop | Linux G-code viewer entry | Name=Prusa GCode viewer, Icon=PrusaSlicer-gcodeviewer | platform/unix/PrusaGcodeviewer.desktop:2-4 |
| src/platform/unix/com.prusa3d.PrusaSlicer.desktop.in | Flatpak entry | Icon=com.prusa3d.PrusaSlicer (configured to build dir) | platform/unix/com.prusa3d.PrusaSlicer.desktop.in:2-5; CMakeLists.txt:100-103 |
| src/platform/msw/*.rc.in VERSIONINFO | Windows version resource text | CompanyName "Prusa Research", OriginalFilename "prusa-slicer.exe", copyright (not an image but identity) | platform/msw/PrusaSlicer.rc.in:4-22; gcodeviewer/launcher rc.in same |
| piggie_logo.jpg (repo root) | New PiggieSlicer logo (208 KB, JPEG) | Not referenced by any code or CMake (grep for "piggie" found only the file itself); candidate source for rebrand assets | piggie_logo.jpg (repo root); now the source for `branding/` |
| resources/web/*.html inline SVG (logo 10584x1835, 250px wide) | Prusa Connect wordmark inline SVG | Connect loading / logged-out pages | resources/web/connect_loading.html:13, connect_loading_dark.html:29, connect_logged_out.html:18, connect_logged_out_dark.html:30 |

Windows runtime icon wiring: `slic3r-app-launcher.rc.in:24` is compiled into the only GUI exe (`slic3r-app-launcher/CMakeLists.txt:3,12`); `PrusaSlicer.rc.in` and `PrusaSlicer-gcodeviewer.rc.in` are not configured anywhere in built CMake. Install: `slic3r-shared/CMakeLists.txt:1479` (FHS) / `:1482` copy the whole `resources/` dir; `cmake/modules/Utils.cmake:1` (`slic3r_link_resources`) symlinks `resources/` into the build tree (Windows mklink /J). No rule generates .ico/.icns; no installer/NSIS/dmg images found.

## 3. Main toolbar

| Image | Depicts | Used for | Where |
|---|---|---|---|
| add_object.svg | add object | Plater toolbar (Icon::AddObject, 2 uses) | ImguiIconHelper.cpp:209; MenuBuilder.cpp:311 Plater/PlaterRenderModule.cpp:652 |
| arrow_right.svg | arrow right | Top bar (Icon::ArrowRight, 1 use) | ImguiIconHelper.cpp:250; TopBar.cpp:217 |
| elipsis.svg | elipsis | Toolbar (Icon::Ellipsis, 1 use) | ImguiIconHelper.cpp:221; ToolBar/ToolBar.cpp:18 |
| rectangle_add.svg | rectangle add | Plater toolbar (Icon::RectangleAdd, 1 use) | ImguiIconHelper.cpp:239; Plater/PlaterRenderModule.cpp:662 |
| tb_redo.svg | tb redo | Top bar (Icon::TopBarRedo, 1 use) | ImguiIconHelper.cpp:110; TopBar.cpp:334 |
| tb_show_ui.svg | tb show ui | Top bar (Icon::TobBarShowUI, 1 use) | ImguiIconHelper.cpp:113; TopBar.cpp:353 |
| tb_undo.svg | tb undo | Top bar (Icon::TopBarUndo, 1 use) | ImguiIconHelper.cpp:109; TopBar.cpp:314 |
| toolbar_history.svg | toolbar history | Plater toolbar (Icon::ToolbarHistory, 1 use) | ImguiIconHelper.cpp:104; Plater/PlaterRenderModule.cpp:634 |

## 4. Gizmos

| Image | Depicts | Used for | Where |
|---|---|---|---|
| arrange_bottom_left.svg | arrange bottom left | Gizmo (Arrange) (Icon::ArrangeBottomLeft, 1 use) | ImguiIconHelper.cpp:144; Plater/ArrangeDialog.cpp:71 |
| arrange_bottom_right.svg | arrange bottom right | Gizmo (Arrange) (Icon::ArrangeBottomRight, 1 use) | ImguiIconHelper.cpp:145; Plater/ArrangeDialog.cpp:73 |
| arrange_center.svg | arrange center | Gizmo (Arrange) (Icon::ArrangeCenter, 1 use) | ImguiIconHelper.cpp:146; Plater/ArrangeDialog.cpp:75 |
| arrange_top_left.svg | arrange top left | Gizmo (Arrange) (Icon::ArrangeTopLeft, 1 use) | ImguiIconHelper.cpp:142; Plater/ArrangeDialog.cpp:67 |
| arrange_top_right.svg | arrange top right | Gizmo (Arrange) (Icon::ArrangeTopRight, 1 use) | ImguiIconHelper.cpp:143; Plater/ArrangeDialog.cpp:69 |
| arrow_up_from_line.svg | arrow up from line | Gizmo (Height range) (Icon::ArrowUpFromLine, 1 use) | ImguiIconHelper.cpp:153; Plater/HeightRangeDialog.cpp:190 |
| arrow_up_to_line.svg | arrow up to line | Gizmo (MM paint) (Icon::ArrowUpToLine, 5 uses) | ImguiIconHelper.cpp:152; Plater/HeightRangeDialog.cpp:220 Plater/MultiMaterialPaintingDialog.cpp:384 |
| caret_down.svg | caret down | Gizmo (SVG) (Icon::CaretDown, 6 uses) | ImguiIconHelper.cpp:206; PhysicalPrinterSettingsButton.cpp:51 Plater/SvgDialog.cpp:66 |
| chevron_left.svg | chevron left | Gizmo (Cut) (Icon::ChevronLeft, 3 uses) | ImguiIconHelper.cpp:213; OverrideSettingsDialog.cpp:67 Plater/CutDialog.cpp:760 |
| circle.svg | circle | Gizmo (Cut) (Icon::Circle, 6 uses) | ImguiIconHelper.cpp:118; Plater/CutDialog.cpp:830 Plater/MultiMaterialPaintingDialog.cpp:153 |
| color_replace.svg | color replace | Gizmo (MM paint) (Icon::ColorReplace, 1 use) | ImguiIconHelper.cpp:132; Plater/MultiMaterialPaintingDialog.cpp:118 |
| cut.svg | cut | Gizmo (Icon::Cut, 1 use) | ImguiIconHelper.cpp:107; Plater/ToolGizmosUiInfo.cpp:79 |
| dividing_line.svg | dividing line | Gizmo (Cut) (Icon::DividingLine, 1 use) | ImguiIconHelper.cpp:127; Plater/CutDialog.cpp:246 |
| dove.svg | dove | Gizmo (Cut) (Icon::Dove, 1 use) | ImguiIconHelper.cpp:128; Plater/CutDialog.cpp:247 |
| dowel.svg | dowel | Gizmo (Cut) (Icon::DowelMarker, 1 use) | ImguiIconHelper.cpp:20; Plater/CutDialog.cpp:791 |
| edit_button.svg | edit button | Gizmo (Text) (Icon::SliderFloatEditBtnIcon, 1 use) | ImguiIconHelper.cpp:16; Plater/TextDialog.cpp:168 |
| fill_drip.svg | fill drip | Gizmo (MM paint) (Icon::FillDrip, 1 use) | ImguiIconHelper.cpp:131; Plater/MultiMaterialPaintingDialog.cpp:116 |
| frustum.svg | frustum | Gizmo (Cut) (Icon::Frustum, 1 use) | ImguiIconHelper.cpp:126; Plater/CutDialog.cpp:810 |
| height_range.svg | height range | Gizmo (Icon::HeightRange, 1 use) | ImguiIconHelper.cpp:249; Plater/ToolGizmosUiInfo.cpp:89 |
| hexagon.svg | hexagon | Gizmo (Cut) (Icon::Hexagon, 1 use) | ImguiIconHelper.cpp:121; Plater/CutDialog.cpp:829 |
| layout.svg | layout | Gizmo (Icon::Layout, 2 uses) | ImguiIconHelper.cpp:224; Plater/ToolGizmosUiInfo.cpp:42 PopNotification/PopNotificationCenter.cpp:79 |
| line_height.svg | line height | Gizmo (MM paint) (Icon::LineHeight, 1 use) | ImguiIconHelper.cpp:133; Plater/MultiMaterialPaintingDialog.cpp:120 |
| lock_closed.svg | lock closed | Gizmo (SVG) (Icon::Lock, 7 uses) | ImguiIconHelper.cpp:53; Plater/SvgDialog.cpp:191 Plater/SvgDialog.cpp:200 |
| lock_open.svg | lock open | Gizmo (SVG) (Icon::Unlock, 5 uses) | ImguiIconHelper.cpp:54; Plater/SvgDialog.cpp:200 Plater/SvgDialog.cpp:267 |
| mouse_left.svg | mouse left | Gizmo (Cut) (Icon::MouseLeft, 17 uses) | ImguiIconHelper.cpp:91; Plater/MMPaintingColorDropdowns.hpp:59 Plater/CutDialog.cpp:995 |
| mouse_right.svg | mouse right | Gizmo (Cut) (Icon::MouseRight, 7 uses) | ImguiIconHelper.cpp:92; Plater/MMPaintingColorDropdowns.hpp:76 Plater/CutDialog.cpp:999 |
| mouse_wheel.svg | mouse wheel | Gizmo (MM paint) (Icon::MouseWheel, 2 uses) | ImguiIconHelper.cpp:93; Plater/MultiMaterialPaintingDialog.cpp:291 Plater/VariableLayerHeightDialog.cpp:150 |
| move.svg | move | Gizmo (Icon::Move, 1 use) | ImguiIconHelper.cpp:218; Plater/ToolGizmosUiInfo.cpp:17 |
| multiple_squares.svg | multiple squares | Gizmo (Arrange) (Icon::MultipleSquares, 1 use) | ImguiIconHelper.cpp:148; Plater/ArrangeDialog.cpp:193 |
| new_button.svg | new button | Gizmo (Text) (Icon::NewBtnIcon, 2 uses) | ImguiIconHelper.cpp:138; MenuBuilder.cpp:304 Plater/TextDialog.cpp:149 |
| notification_warning.svg | notification warning | Gizmo (SVG) (Icon::WarningMarker, 6 uses) | ImguiIconHelper.cpp:86; Plater/SvgDialog.cpp:96 Plater/TextDialog.cpp:46 |
| paint_fuzzy_skin.svg | paint fuzzy skin | Gizmo (Icon::PaintFuzzySkin, 1 use) | ImguiIconHelper.cpp:228; Plater/ToolGizmosUiInfo.cpp:57 |
| paint_multi_material.svg | paint multi material | Gizmo (Icon::PaintMultiMaterial, 1 use) | ImguiIconHelper.cpp:229; Plater/ToolGizmosUiInfo.cpp:62 |
| paint_seams.svg | paint seams | Gizmo (Icon::PaintSeams, 1 use) | ImguiIconHelper.cpp:227; Plater/ToolGizmosUiInfo.cpp:52 |
| paint_supports.svg | paint supports | Gizmo (Icon::PaintSupports, 1 use) | ImguiIconHelper.cpp:226; Plater/ToolGizmosUiInfo.cpp:47 |
| paintbrush.svg | paintbrush | Gizmo (MM paint) (Icon::PaintBrush, 3 uses) | ImguiIconHelper.cpp:123; Plater/MultiMaterialPaintingDialog.cpp:112 Plater/PaintOnFuzzySkinDialog.cpp:39 |
| place.svg | place | Gizmo (Icon::PlaceOnFace, 1 use) | ImguiIconHelper.cpp:220; Plater/ToolGizmosUiInfo.cpp:32 |
| plug.svg | plug | Gizmo (Cut) (Icon::PlugMarker, 1 use) | ImguiIconHelper.cpp:19; Plater/CutDialog.cpp:790 |
| plus_height_range.svg | plus height range | Gizmo (Height range) (Icon::PlusHeightRange, 1 use) | ImguiIconHelper.cpp:156; Plater/HeightRangeDialog.cpp:143 |
| plus_modifier.svg | plus modifier | Gizmo (Height range) (Icon::PlusModifier, 2 uses) | ImguiIconHelper.cpp:157; Plater/HeightRangeDialog.cpp:266 Plater/HeightRangeDialog.cpp:297 |
| prism.svg | prism | Gizmo (Cut) (Icon::Prism, 1 use) | ImguiIconHelper.cpp:125; Plater/CutDialog.cpp:809 |
| reflection_x.svg | reflection x | Gizmo (SVG) (Icon::ReflectionX, 1 use) | ImguiIconHelper.cpp:174; Plater/SvgDialog.cpp:277 |
| reflection_y.svg | reflection y | Gizmo (SVG) (Icon::ReflectionY, 1 use) | ImguiIconHelper.cpp:175; Plater/SvgDialog.cpp:285 |
| refresh.svg | refresh | Gizmo (SVG) (Icon::Reload, 2 uses) | ImguiIconHelper.cpp:173; Plater/SvgDialog.cpp:55 WelcomeDialog.cpp:454 |
| rotate.svg | rotate | Gizmo (Icon::Rotate, 1 use) | ImguiIconHelper.cpp:240; Plater/ToolGizmosUiInfo.cpp:22 |
| ruler.svg | ruler | Gizmo (Icon::Ruler, 1 use) | ImguiIconHelper.cpp:230; Plater/ToolGizmosUiInfo.cpp:77 |
| scale.svg | scale | Gizmo (Icon::Scale, 1 use) | ImguiIconHelper.cpp:219; Plater/ToolGizmosUiInfo.cpp:27 |
| simplify.svg | simplify | Gizmo (Icon::Simplify, 2 uses) | ImguiIconHelper.cpp:241; Plater/ToolGizmosUiInfo.cpp:37 PopNotification/PopNotificationCenter.cpp:89 |
| single_square.svg | single square | Gizmo (Arrange) (Icon::SingleSquare, 1 use) | ImguiIconHelper.cpp:149; Plater/ArrangeDialog.cpp:198 |
| snap.svg | snap | Gizmo (Cut) (Icon::SnapMarker, 1 use) | ImguiIconHelper.cpp:21; Plater/CutDialog.cpp:792 |
| sphere.svg | sphere | Gizmo (MM paint) (Icon::Sphere, 5 uses) | ImguiIconHelper.cpp:122; Plater/MultiMaterialPaintingDialog.cpp:151 Plater/PaintOnFuzzySkinDialog.cpp:65 |
| square.svg | square | Gizmo (Cut) (Icon::Square, 1 use) | ImguiIconHelper.cpp:120; Plater/CutDialog.cpp:828 |
| tb_save.svg | tb save | Gizmo (Text) (Icon::TobBarSave, 3 uses) | ImguiIconHelper.cpp:112; MenuBuilder.cpp:309 Plater/TextDialog.cpp:159 |
| text.svg | text | Gizmo (Icon::Text, 1 use) | ImguiIconHelper.cpp:231; Plater/ToolGizmosUiInfo.cpp:67 |
| toolbar_svg.svg | toolbar svg | Gizmo (Icon::Svg, 1 use) | ImguiIconHelper.cpp:232; Plater/ToolGizmosUiInfo.cpp:72 |
| triangle.svg | triangle | Gizmo (Cut) (Icon::Triangle, 4 uses) | ImguiIconHelper.cpp:119; Plater/CutDialog.cpp:827 Plater/MultiMaterialPaintingDialog.cpp:155 |
| variable_layer_height.svg | variable layer height | Gizmo (Icon::VariableLayerHeight, 1 use) | ImguiIconHelper.cpp:248; Plater/ToolGizmosUiInfo.cpp:84 |
| wand_magic_sparkles.svg | wand magic sparkles | Gizmo (MM paint) (Icon::WandMagicSparkles, 3 uses) | ImguiIconHelper.cpp:124; Plater/MultiMaterialPaintingDialog.cpp:114 Plater/PaintOnFuzzySkinDialog.cpp:40 |

## 5. Object list

| Image | Depicts | Used for | Where |
|---|---|---|---|
| add_bed.svg | add bed | Object list (Icon::AddBedIcon, 2 uses) | ImguiIconHelper.cpp:49; ObjectList.cpp:852 ObjectList.cpp:915 |
| add_text_modifier.svg | add text modifier | Object list (Icon::TextModifierVolume, 1 use) | ImguiIconHelper.cpp:32; ObjectList.cpp:265 |
| add_text_negative.svg | add text negative | Object list (Icon::TextNegativeVolume, 1 use) | ImguiIconHelper.cpp:31; ObjectList.cpp:263 |
| add_text_part.svg | add text part | Object list (Icon::TextSolidPartVolume, 1 use) | ImguiIconHelper.cpp:30; ObjectList.cpp:261 |
| all_beds.svg | all beds | Object list (Icon::AllBeds, 2 uses) | ImguiIconHelper.cpp:52; MenuBuilder.cpp:299 ObjectList.cpp:969 |
| calculator.svg | calculator | Object list (Icon::Calculator, 1 use) | ImguiIconHelper.cpp:136; ObjectListWindow.cpp:87 |
| config_container.svg | config container | Object list (Icon::ConfigContainer, 3 uses) | ImguiIconHelper.cpp:47; ObjectList.cpp:321 ObjectListWindow.cpp:42 |
| copy_for_gizmo.svg | copy for gizmo | Object list (Icon::CopyForGizmo, 2 uses) | ImguiIconHelper.cpp:137; ObjectListWindow.cpp:270 Plater/MeasureDialog.cpp:253 |
| cut_connectors.svg | cut connectors | Object list (Icon::CutConnectors, 1 use) | ImguiIconHelper.cpp:40; ObjectList.cpp:1302 |
| del_bed.svg | del bed | Object list (Icon::DelBedIcon, 1 use) | ImguiIconHelper.cpp:50; ObjectList.cpp:1672 |
| details.svg | details | Object list (Icon::Details, 1 use) | ImguiIconHelper.cpp:44; ObjectListWindow.cpp:67 |
| dont_print.svg | dont print | Object list (Icon::EyeOpen, 1 use) | ImguiIconHelper.cpp:23; ObjectList.cpp:1632 |
| dont_print_active.svg | dont print active | Object list (Icon::EyeClosed, 2 uses) | ImguiIconHelper.cpp:24; ObjectList.cpp:1632 ObjectList.cpp:1634 |
| down_arrow.svg | down arrow | Object list (Icon::OpenArrow, 2 uses) | ImguiIconHelper.cpp:45; Config/PrintToolRowButton.cpp:149 ObjectList.cpp:759 |
| edit_layers_all.svg | edit layers all | Object list (Icon::HRModifier, 3 uses) | ImguiIconHelper.cpp:37; ObjectList.cpp:404 ObjectList.cpp:1854 |
| error_tick_f.svg | error tick f | Object list (Icon::ErrorTickHovered, 3 uses) | ImguiIconHelper.cpp:60; ObjectList.cpp:1783 Preview/DoubleSliderForLayers.cpp:1077 |
| exclude.svg | exclude | Object list (Icon::ModifierVolume, 2 uses) | ImguiIconHelper.cpp:27; MenuBuilder.cpp:324 ObjectList.cpp:291 |
| fdm_supports.svg | fdm supports | Object list (Icon::CustomSupports, 2 uses) | ImguiIconHelper.cpp:38; ObjectList.cpp:395 ObjectList.cpp:1849 |
| fuzzy_skin_painting.svg | fuzzy skin painting | Object list (Icon::FuzzySkin, 2 uses) | ImguiIconHelper.cpp:43; ObjectList.cpp:399 ObjectList.cpp:1853 |
| instances_icon.svg | instances icon | Object list (Icon::InstancesIcon, 1 use) | ImguiIconHelper.cpp:48; ObjectList.cpp:1482 |
| mmu_segmentation_.svg | mmu segmentation | Object list (Icon::MmSegmentation, 2 uses) | ImguiIconHelper.cpp:41; ObjectList.cpp:401 ObjectList.cpp:1851 |
| object_icon.svg | object icon | Object list (Icon::ObjectIcon, 5 uses) | ImguiIconHelper.cpp:36; ObjectList.cpp:1233 ObjectList.cpp:1241 |
| overrides_marker.svg | overrides marker | Object list (Icon::OverridesMarker, 2 uses) | ImguiIconHelper.cpp:51; ObjectList.cpp:767 ObjectList.cpp:798 |
| palette.svg | palette | Object list (Icon::Palette, 1 use) | ImguiIconHelper.cpp:233; ObjectList.cpp:1188 |
| printer.svg | printer | Object list (Icon::PrinterIconMarker, 2 uses) | ImguiIconHelper.cpp:12; Config/CategoryUtils.cpp:73 ObjectList.cpp:326 |
| seam_.svg | seam | Object list (Icon::CustomSeam, 2 uses) | ImguiIconHelper.cpp:39; ObjectList.cpp:397 ObjectList.cpp:1850 |
| sinking.svg | sinking | Object list (Icon::Sinking, 2 uses) | ImguiIconHelper.cpp:42; ObjectList.cpp:1852 ObjectList.cpp:1867 |
| sla_printer.svg | sla printer | Object list (Icon::PrinterSlaIconMarker, 2 uses) | ImguiIconHelper.cpp:13; Config/CategoryUtils.cpp:74 ObjectList.cpp:325 |
| subtract.svg | subtract | Object list (Icon::NegativeVolume, 2 uses) | ImguiIconHelper.cpp:26; MenuBuilder.cpp:322 ObjectList.cpp:289 |
| support_blocker.svg | support blocker | Object list (Icon::SupportBlocker, 2 uses) | ImguiIconHelper.cpp:28; MenuBuilder.cpp:326 ObjectList.cpp:293 |
| support_enforcer.svg | support enforcer | Object list (Icon::SupportModifier, 2 uses) | ImguiIconHelper.cpp:29; MenuBuilder.cpp:328 ObjectList.cpp:295 |
| svg_modifier.svg | svg modifier | Object list (Icon::SvgModifierVolume, 1 use) | ImguiIconHelper.cpp:35; ObjectList.cpp:278 |
| svg_negative.svg | svg negative | Object list (Icon::SvgNegativeVolume, 1 use) | ImguiIconHelper.cpp:34; ObjectList.cpp:276 |
| svg_part.svg | svg part | Object list (Icon::SvgSolidPartVolume, 1 use) | ImguiIconHelper.cpp:33; ObjectList.cpp:274 |
| union.svg | union | Object list (Icon::SolidPartVolume, 2 uses) | ImguiIconHelper.cpp:25; MenuBuilder.cpp:320 ObjectList.cpp:287 |

## 6. Tabs/settings

| Image | Depicts | Used for | Where |
|---|---|---|---|
| bed.svg | bed | Settings category (Icon::Bed, 1 use) | ImguiIconHelper.cpp:192; Config/CategoryUtils.cpp:77 |
| bed_adhesion.svg | bed adhesion | Settings category (Icon::BedAdhesion, 1 use) | ImguiIconHelper.cpp:180; Config/CategoryUtils.cpp:26 |
| cooling.svg | cooling | Settings category (Icon::Fan, 1 use) | ImguiIconHelper.cpp:208; Config/CategoryUtils.cpp:67 |
| custom_gcode.svg | custom gcode | Settings category (Icon::CustomGCode, 1 use) | ImguiIconHelper.cpp:188; Config/CategoryUtils.cpp:50 |
| delete_button.svg | delete button | Settings UI (Icon::DeleteBtnIcon, 8 uses) | ImguiIconHelper.cpp:139; ColorMix/ColorMixRecipeRow.cpp:112 Config/ConfigItemFilePicker.cpp:87 |
| extrusion_calibration.svg | extrusion calibration | Settings category (Icon::ExtrusionCalibration, 1 use) | ImguiIconHelper.cpp:190; Config/CategoryUtils.cpp:64 |
| extrusion_retraction.svg | extrusion retraction | Settings category (Icon::ExtrusionRetraction, 1 use) | ImguiIconHelper.cpp:185; Config/CategoryUtils.cpp:38 |
| infill.svg | infill | Settings category (Icon::Infill, 1 use) | ImguiIconHelper.cpp:179; Config/CategoryUtils.cpp:23 |
| layers.svg | layers | Settings category (Icon::Layers, 1 use) | ImguiIconHelper.cpp:177; Config/CategoryUtils.cpp:17 |
| machine_limits.svg | machine limits | Settings category (Icon::MachineLimits, 1 use) | ImguiIconHelper.cpp:193; Config/CategoryUtils.cpp:80 |
| minus.svg | minus | Settings UI (Icon::Minus, 7 uses) | ImguiIconHelper.cpp:211; ColorMix/ColorMixDialog.cpp:955 Config/ConfigItemSubstitutions.cpp:38 |
| motions.svg | motions | Settings category (Icon::Motions, 1 use) | ImguiIconHelper.cpp:184; Config/CategoryUtils.cpp:35 |
| multi-material.svg | multi-material | Settings category (Icon::MultiMaterial, 1 use) | ImguiIconHelper.cpp:186; Config/CategoryUtils.cpp:42 |
| multiple_extruders.svg | multiple extruders | Settings category (Icon::MultipleExtruders, 1 use) | ImguiIconHelper.cpp:194; Config/CategoryUtils.cpp:83 |
| notes.svg | notes | Settings category (Icon::Notes, 1 use) | ImguiIconHelper.cpp:202; Config/CategoryUtils.cpp:58 |
| output.svg | output | Settings category (Icon::Output, 1 use) | ImguiIconHelper.cpp:201; Config/CategoryUtils.cpp:53 |
| precision.svg | precision | Settings category (Icon::Precision, 1 use) | ImguiIconHelper.cpp:187; Config/CategoryUtils.cpp:45 |
| right_arrow.svg | right arrow | Settings UI (Icon::CloseArrow, 4 uses) | ImguiIconHelper.cpp:46; Config/PrintToolRowButton.cpp:27 Config/PrintToolRowButton.cpp:149 |
| robot.svg | robot | Settings UI (Icon::Robot, 1 use) | ImguiIconHelper.cpp:252; Config/ExplanationContainer.cpp:62 |
| single_extruder.svg | single extruder | Settings category (Icon::SingleExtruder, 1 use) | ImguiIconHelper.cpp:195; Config/CategoryUtils.cpp:86 |
| star.svg | star | Settings UI (Icon::Star, 15 uses) | ImguiIconHelper.cpp:197; Config/FavoriteButton.cpp:8 Config/FavoriteButton.cpp:35 |
| star_solid.svg | star solid | Settings UI (Icon::StarSolid, 10 uses) | ImguiIconHelper.cpp:198; Config/FavoriteButton.cpp:34 Config/FavoriteButton.cpp:44 |
| support.svg | support | Settings category (Icon::Support, 1 use) | ImguiIconHelper.cpp:182; Config/CategoryUtils.cpp:29 |
| temperature.svg | temperature | Settings category (Icon::Temperature, 1 use) | ImguiIconHelper.cpp:189; Config/CategoryUtils.cpp:61 |
| time.svg | time | Settings category (Icon::Time, 1 use) | ImguiIconHelper.cpp:183; Config/CategoryUtils.cpp:32 |
| undo_gizmo.svg | undo gizmo | Settings UI (Icon::UndoGizmo, 9 uses) | ImguiIconHelper.cpp:247; Config/ConfigRowItem.cpp:45 Config/PrintToolRowButton.cpp:36 |
| vector.svg | vector | Settings category (Icon::Vector, 1 use) | ImguiIconHelper.cpp:191; Config/CategoryUtils.cpp:70 |
| walls_and_perimeters.svg | walls and perimeters | Settings category (Icon::WallsPerimeters, 1 use) | ImguiIconHelper.cpp:178; Config/CategoryUtils.cpp:20 |

## 7. Preview/legend

| Image | Depicts | Used for | Where |
|---|---|---|---|
| cog_.svg | cog | Preview/legend (Icon::DSSettings, 1 use) | ImguiIconHelper.cpp:58; Preview/DoubleSliderForLayers.cpp:90 |
| colorchange_del_f.svg | colorchange del f | Preview/legend (Icon::RemoveTickHovered, 1 use) | ImguiIconHelper.cpp:66; Preview/DoubleSliderForLayers.cpp:1061 |
| edit_gcode.svg | edit gcode | Preview/legend (Icon::EditGCode, 1 use) | ImguiIconHelper.cpp:63; Preview/DoubleSliderForLayers.cpp:1109 |
| edit_gcode_f.svg | edit gcode f | Preview/legend (Icon::EditGCodeHovered, 1 use) | ImguiIconHelper.cpp:64; Preview/DoubleSliderForLayers.cpp:1110 |
| error_tick.svg | error tick | Preview/legend (Icon::ErrorTick, 3 uses) | ImguiIconHelper.cpp:59; PhysicalPrinterSettingsButton.cpp:31 Preview/DoubleSliderForLayers.cpp:1076 |
| layers_inspect.svg | layers inspect | Preview/legend (Icon::LayersInspect, 1 use) | ImguiIconHelper.cpp:225; Preview/PreviewRenderModule.cpp:993 |
| legend_cog.svg | legend cog | Preview/legend (Icon::LegendCOG, 1 use) | ImguiIconHelper.cpp:83; Preview/PreviewRenderModule.cpp:953 |
| legend_colorchanges.svg | legend colorchanges | Preview/legend (Icon::LegendColorChanges, 1 use) | ImguiIconHelper.cpp:80; Preview/PreviewRenderModule.cpp:932 |
| legend_customgcodes.svg | legend customgcodes | Preview/legend (Icon::LegendCustomGCodes, 1 use) | ImguiIconHelper.cpp:82; Preview/PreviewRenderModule.cpp:946 |
| legend_deretract.svg | legend deretract | Preview/legend (Icon::LegendDeretract, 1 use) | ImguiIconHelper.cpp:77; Preview/PreviewRenderModule.cpp:911 |
| legend_pauseprints.svg | legend pauseprints | Preview/legend (Icon::LegendPausePrints, 1 use) | ImguiIconHelper.cpp:81; Preview/PreviewRenderModule.cpp:939 |
| legend_retract.svg | legend retract | Preview/legend (Icon::LegendRetract, 1 use) | ImguiIconHelper.cpp:76; Preview/PreviewRenderModule.cpp:904 |
| legend_seams.svg | legend seams | Preview/legend (Icon::LegendSeams, 1 use) | ImguiIconHelper.cpp:78; Preview/PreviewRenderModule.cpp:918 |
| legend_shells.svg | legend shells | Preview/legend (Icon::LegendShells, 1 use) | ImguiIconHelper.cpp:84; Preview/PreviewRenderModule.cpp:967 |
| legend_toolchanges.svg | legend toolchanges | Preview/legend (Icon::LegendToolChanges, 1 use) | ImguiIconHelper.cpp:79; Preview/PreviewRenderModule.cpp:925 |
| legend_toolmarker.svg | legend toolmarker | Preview/legend (Icon::LegendToolMarker, 1 use) | ImguiIconHelper.cpp:85; Preview/PreviewRenderModule.cpp:960 |
| legend_travel.svg | legend travel | Preview/legend (Icon::LegendTravel, 1 use) | ImguiIconHelper.cpp:74; Preview/PreviewRenderModule.cpp:890 |
| legend_wipe.svg | legend wipe | Preview/legend (Icon::LegendWipe, 1 use) | ImguiIconHelper.cpp:75; Preview/PreviewRenderModule.cpp:897 |
| pause_print.svg | pause print | Preview/legend (Icon::PausePrint, 1 use) | ImguiIconHelper.cpp:61; Preview/DoubleSliderForLayers.cpp:1100 |
| pause_print_f.svg | pause print f | Preview/legend (Icon::PausePrintHovered, 1 use) | ImguiIconHelper.cpp:62; Preview/DoubleSliderForLayers.cpp:1101 |
| preview.svg | preview | Preview/legend (Icon::Preview, 2 uses) | ImguiIconHelper.cpp:217; Plater/PlaterRenderModule.cpp:773 Preview/PreviewRenderModule.cpp:984 |
| undo_r.svg | undo r | Preview/legend (Icon::DSRevert, 2 uses) | ImguiIconHelper.cpp:57; Plater/GizmoWindow.cpp:237 Preview/DoubleSliderForLayers.cpp:70 |

## 8. Notifications

| Image | Depicts | Used for | Where |
|---|---|---|---|
| notification_close_gray.svg | notification close gray | Notification (Icon::NotificationCloseGray, 1 use) | ImguiIconHelper.cpp:89; PopNotification/PopNotificationView.cpp:287 |
| notification_error.svg | notification error | Notification (Icon::ErrorMarker, 5 uses) | ImguiIconHelper.cpp:88; PopNotification/PopNotificationView.cpp:235 PresetUpdater/PresetUpdaterSourceRow.cpp:104 |
| notification_warning_white.svg | notification warning white | Notification (Icon::WarningMarkerWhite, 2 uses) | ImguiIconHelper.cpp:87; Plater/SidebarPlaterActionButtons.cpp:209 PopNotification/PopNotificationView.cpp:232 |
| printables.svg | printables | Notification (Icon::Printables, 6 uses) | ImguiIconHelper.cpp:99; PopNotification/PopNotificationCenter.cpp:326 PopNotification/PopNotificationCenter.cpp:361 |

## 9. Dialogs/menus

| Image | Depicts | Used for | Where |
|---|---|---|---|
| add_volume.svg | add volume | Menu (Icon::AddVolume, 1 use) | ImguiIconHelper.cpp:210; MenuBuilder.cpp:318 |
| align_horizontal_center.svg | align horizontal center | Align buttons (Icon::AlignHCenterBtn, 1 use) | ImguiIconHelper.cpp:166; Yoga/AlignmentButtons.cpp:26 |
| align_horizontal_left.svg | align horizontal left | Align buttons (Icon::AlignHLeftBtn, 1 use) | ImguiIconHelper.cpp:165; Yoga/AlignmentButtons.cpp:25 |
| align_horizontal_right.svg | align horizontal right | Align buttons (Icon::AlignHRightBtn, 1 use) | ImguiIconHelper.cpp:167; Yoga/AlignmentButtons.cpp:27 |
| align_vertical_bottom.svg | align vertical bottom | Align buttons (Icon::AlignVBottomBtn, 1 use) | ImguiIconHelper.cpp:170; Yoga/AlignmentButtons.cpp:41 |
| align_vertical_center.svg | align vertical center | Align buttons (Icon::AlignVCenterBtn, 1 use) | ImguiIconHelper.cpp:169; Yoga/AlignmentButtons.cpp:40 |
| align_vertical_top.svg | align vertical top | Align buttons (Icon::AlignVTopBtn, 1 use) | ImguiIconHelper.cpp:168; Yoga/AlignmentButtons.cpp:39 |
| caret_left.svg | caret left | Printer settings (Icon::CaretLeft, 1 use) | ImguiIconHelper.cpp:203; LogicalPrinterSettingsDialog.cpp:377 |
| caret_right.svg | caret right | Preset updater dialog (Icon::CaretRight, 2 uses) | ImguiIconHelper.cpp:204; PresetUpdater/PresetUpdaterSourceRow.cpp:183 PresetUpdater/PresetUpdaterSourceRow.cpp:281 |
| caret_up.svg | caret up | UI (Icon::CaretUp, 2 uses) | ImguiIconHelper.cpp:205; Yoga/CollapsibleWindow.cpp:28 Yoga/CollapsibleWindow.cpp:69 |
| checked.svg | checked | Preset updater dialog (Icon::CheckMark, 4 uses) | ImguiIconHelper.cpp:68; PresetUpdater/PresetUpdaterSourceRow.cpp:110 PresetUpdater/PresetUpdaterSourceRow.cpp:270 |
| chevron_right.svg | chevron right | UI (Icon::ChevronRight, 2 uses) | ImguiIconHelper.cpp:212; OverrideCategoryButton.cpp:19 Plater/LayerRangeSettingsDialog.cpp:70 |
| cloud.svg | cloud | Welcome dialog (Icon::Cloud, 1 use) | ImguiIconHelper.cpp:101; WelcomeDialog.cpp:439 |
| cog.svg | cog | Printer settings (Icon::Cog, 7 uses) | ImguiIconHelper.cpp:11; LogicalPrinterSettingsDialog.cpp:434 MaterialSelectionRow.cpp:72 |
| colorchange_del.svg | colorchange del | Preset updater dialog (Icon::RemoveTick, 3 uses) | ImguiIconHelper.cpp:65; PresetUpdater/PresetUpdaterSourceRow.cpp:228 PresetUpdater/PresetUpdaterVendorRow.cpp:187 |
| compare.svg | compare | Printer settings (Icon::Compare, 3 uses) | ImguiIconHelper.cpp:214; MaterialSettingsDialog.cpp:61 PrinterAdvancedSettingsDialog.cpp:101 |
| connect.svg | connect | Welcome dialog (Icon::Connect, 2 uses) | ImguiIconHelper.cpp:100; WelcomeDialog.cpp:472 WelcomeDialog.cpp:1308 |
| connect_gcode.svg | connect gcode | Printer settings (Icon::ConnectUpload, 1 use) | ImguiIconHelper.cpp:161; PhysicalPrinterSettingsButton.cpp:88 |
| cross_new.svg | cross new | Printer settings (Icon::TopBarCross, 3 uses) | ImguiIconHelper.cpp:115; AddPrinterPanel.cpp:91 ProjectButton.cpp:50 |
| exclamation_triangle.svg | exclamation triangle | UI (Icon::ExclamationTriangle, 1 use) | ImguiIconHelper.cpp:242; WarningPanel.cpp:29 |
| export_to_sd.svg | export to sd | Printer settings (Icon::ExportToSD, 1 use) | ImguiIconHelper.cpp:160; PhysicalPrinterSettingsButton.cpp:109 |
| funnel.svg | funnel | UI (Icon::Funnel, 1 use) | ImguiIconHelper.cpp:196; PrintSettingsDialog.cpp:144 |
| key_backspace.svg | key backspace | UI (Icon::KeyBackspace, 1 use) | ImguiIconHelper.cpp:95; Plater/MeasureDialog.cpp:90 |
| light_lock_closed.svg | light lock closed | UI (Icon::LightLockClosed, 5 uses) | ImguiIconHelper.cpp:55; MaterialSelectionRow.cpp:21 MaterialSelectionRow.cpp:98 |
| light_lock_opened.svg | light lock opened | UI (Icon::LightLockOpened, 1 use) | ImguiIconHelper.cpp:56; Plater/ScaleWidget.cpp:232 |
| open_folder.svg | open folder | Menu (Icon::OpenFolder, 2 uses) | ImguiIconHelper.cpp:236; MenuBuilder.cpp:306 PresetUpdater/PresetUpdaterSourceRow.cpp:214 |
| plus.svg | plus | UI (Icon::Plus, 6 uses) | ImguiIconHelper.cpp:243; ColorMix/ColorMixDialog.cpp:368 ColorMix/ColorMixDialog.cpp:945 |
| print_idle.svg | print idle | UI (Icon::PrintIdle, 3 uses) | ImguiIconHelper.cpp:22; Plater/GizmoWindow.cpp:90 SidebarObject.cpp:257 |
| printer_physical.svg | printer physical | Printer settings (Icon::PhysicalPrinterIcon, 1 use) | ImguiIconHelper.cpp:67; PhysicalPrinterSettingsButton.cpp:99 |
| recent_projects.svg | recent projects | Menu (Icon::RecentProjects, 1 use) | ImguiIconHelper.cpp:238; MenuBuilder.cpp:316 |
| save.svg | save | Printer settings (Icon::ExportToLocal, 1 use) | ImguiIconHelper.cpp:162; PhysicalPrinterSettingsButton.cpp:109 |
| search_gray.svg | search gray | Printer settings (Icon::Search, 5 uses) | ImguiIconHelper.cpp:207; AddPrinterPanel.cpp:101 LogicalPrinterSettingsDialog.cpp:211 |
| shapes.svg | shapes | Menu (Icon::Shapes, 1 use) | ImguiIconHelper.cpp:237; MenuBuilder.cpp:314 |
| switch.svg | switch | UI (Icon::Switch, 1 use) | ImguiIconHelper.cpp:134; Plater/MMPaintingColorDropdowns.hpp:93 |
| tb_load.svg | tb load | Printer settings (Icon::TobBarLoad, 2 uses) | ImguiIconHelper.cpp:111; Config/ConfigItemFilePicker.cpp:58 PhysicalPrinterAdvancedSettingsDialog.cpp:237 |

## 10. Widgets and dialogs (wx)

| Image | Depicts | Used for | Where |
|---|---|---|---|
| check_off.svg | check off | Checkbox widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/CheckBox.cpp:10 |
| check_off_disabled.svg | check off disabled | Checkbox widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/CheckBox.cpp:12 |
| check_off_focused.svg | check off focused | Checkbox widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/CheckBox.cpp:14 |
| check_on.svg | check on | Checkbox widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/CheckBox.cpp:9 |
| check_on_disabled.svg | check on disabled | Checkbox widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/CheckBox.cpp:11 |
| check_on_focused.svg | check on focused | Checkbox widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/CheckBox.cpp:13 |
| cog_wx.svg | cog wx | Left bar Preferences button | slic3r-app-desktop/src/Slic3r/App/Desktop/LeftBarCtrl.cpp:26 |
| cross.svg | cross | Unsaved-changes dialog Cancel button | slic3r-shared-wx/src/Slic3r/App/WX/UnsavedChangesDialog.cpp:222 |
| dot_small.svg | dot small | Diff dialog modified marker | slic3r-shared-wx/src/Slic3r/App/WX/DiffDVCModel.cpp:129 |
| drop_down.svg | drop down | Dropdown arrow (tabs bar, combo box) | slic3r-app-desktop/src/Slic3r/App/Desktop/TabsBarCtrl.cpp:107; slic3r-shared-wx/src/Slic3r/App/WX/Widgets/WXComboBox.cpp:39 |
| equal.svg | equal | Diff dialog equal marker | slic3r-shared-wx/src/Slic3r/App/WX/DiffNamespace.cpp:139,213 |
| exclamation.svg | exclamation | Message dialog warning icon; save-preset validation | slic3r-shared-wx/src/Slic3r/App/WX/MsgDialog.cpp:114; slic3r-shared-wx/src/Slic3r/App/WX/SavePresetDialog.cpp:263 |
| exclamation_manifold.svg | exclamation manifold | Save-preset dialog warning marker | slic3r-shared-wx/src/Slic3r/App/WX/SavePresetDialog.cpp:261 |
| exit.svg | exit | Unsaved-changes dialog Discard button | slic3r-shared-wx/src/Slic3r/App/WX/UnsavedChangesDialog.cpp:209 |
| info.svg | info | Message dialog information icon | slic3r-shared-wx/src/Slic3r/App/WX/MsgDialog.cpp:115 |
| lb_printables.svg | lb printables | Left bar tab Printables | slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:647 |
| lb_printers.svg | lb printers | Left bar tab Printers / Physical printer | slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:627,673 |
| lb_slicing.svg | lb slicing | Left bar tab "Slicing" | slic3r-app-desktop/src/Slic3r/App/Desktop/MainFrame.cpp:635 |
| lock_closed_f.svg | lock closed f | Lock button hover | slic3r-shared-wx/src/Slic3r/App/WX/LockButton.cpp:13 |
| lock_open_f.svg | lock open f | Lock button hover | slic3r-shared-wx/src/Slic3r/App/WX/LockButton.cpp:15 |
| login.svg | login | Account menu "Sign in" item | slic3r-app-desktop/src/Slic3r/App/Desktop/TabsBarMenus.cpp:36,46 |
| logout.svg | logout | Account menu item when logged in | slic3r-app-desktop/src/Slic3r/App/Desktop/TabsBarMenus.cpp:46 |
| mode.svg | mode | Mode (Simple/Advanced/Expert) marker, recoloured per palette | slic3r-app-desktop/src/Slic3r/App/Desktop/ModePaletteComboBox.cpp:68 |
| not_equal.svg | not equal | Diff dialog not-equal marker | slic3r-shared-wx/src/Slic3r/App/WX/DiffNamespace.cpp:214 |
| paste_menu.svg | paste menu | Unsaved-changes dialog Transfer button | slic3r-shared-wx/src/Slic3r/App/WX/UnsavedChangesDialog.cpp:202,375 |
| question.svg | question | Message dialog question icon | slic3r-shared-wx/src/Slic3r/App/WX/MsgDialog.cpp:116 |
| search_blink.svg | search blink | Highlighter blinking search icon (default arg) | slic3r-shared-wx/include/Slic3r/App/WX/Highlighter.hpp:20 |
| spin_dec.svg | spin dec | Spin input button | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/SpinInput.cpp:50 |
| spin_dec_act.svg | spin dec act | Spin input pressed | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/SpinInput.cpp:45 |
| spin_inc.svg | spin inc | Spin input button | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/SpinInput.cpp:50 |
| spin_inc_act.svg | spin inc act | Spin input pressed | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/SpinInput.cpp:44 |
| tick_mark.svg | tick mark | Save-preset dialog valid marker | slic3r-shared-wx/src/Slic3r/App/WX/SavePresetDialog.cpp:126,198,264 |
| toggle_off.svg | toggle off | Switch widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/SwitchButton.cpp:14 |
| toggle_on.svg | toggle on | Switch widget | slic3r-shared-wx/src/Slic3r/App/WX/Widgets/SwitchButton.cpp:13 |
| user.svg | user | Left bar account button (logged out); also the default avatar user.svg | slic3r-app-desktop/src/Slic3r/App/Desktop/LeftBarCtrl.cpp:30,74,77; slic3r-shared/src/Slic3r/Biz/UserAccount/UserAccountCommunication.cpp:115 |

## 11. Branding icons already listed in section 1 (icons dir, with usage)

See section 1; `PrusaSlicer`, `PrusaSlicer-gcodeviewer`, `PrusaSlicer_128px`, `splashscreen` counted as referenced.

## 12. Printer models (resources/presets/prusa-research-fff/PrusaResearch/assets, 51 files)

Keys (`vendor.yaml` per printer under `visual`): `bed_model` (.stl), `bed_texture` (.svg), `thumbnail` (.svg). Parsed at `slic3r-shared/src/Slic3r/Biz/Config/HwConfigJson.cpp:624,631,638`; path = `"/presets/<repo>/<vendor>/assets/" + file` (`slic3r-domain/src/Slic3r/Domain/Preset/HwConfig.cpp:318`). Bed model/texture used by `slic3r-domain/src/Slic3r/Domain/BedContainer.cpp:49-55` (only if bed shape equals the system preset; texture checked png/svg at `Bed.cpp:19-24`). Thumbnails shown in `slic3r-shared/src/Slic3r/App/AddPrinterPanel.cpp:25-30` (add-printer panel), `LogicalPrinterSettingsButton.cpp:75-78`, `LogicalPrinterSettingsDialog.cpp:483-486`, `PhysicalPrinterSettingsButton.cpp:93-96`.

| Printer id (vendor.yaml line) | bed_model | bed_texture | thumbnail |
|---|---|---|---|
| xl (133) | xl_bed.stl | xl.svg | XL_thumbnail.svg |
| xl+ (157) | xl_bed.stl | xl.svg | XLP1T_thumbnail.svg |
| COREONEL (181) | coreonel_bed.stl | coreonel.svg | COREONEL_thumbnail.svg |
| COREONE (201) | coreone_bed.stl | coreone.svg | COREONE_thumbnail.svg |
| COREONEOAK (221) | coreone_bed.stl | coreoneoak.svg | COREONEOAK_thumbnail.svg |
| COREONE_INDX (241) | coreoneindx_bed.stl | coreoneindx.svg | COREONEINDX_thumbnail.svg |
| MK4 (293) | mk4_bed.stl | mk4.svg | MK4_thumbnail.svg |
| MK4S (313) | mk4_bed.stl | mk4s.svg | MK4S_thumbnail.svg |
| MK3.9 (333) | mk4_bed.stl | mk39.svg | MK4_thumbnail.svg |
| MK3.9S (354) | mk4_bed.stl | mk39s.svg | MK4S_thumbnail.svg |
| MK3.5 (374) | mk3_bed.stl | mk35.svg | MK3.5S_thumbnail.svg |
| MK3 (398) | mk3_bed.stl | mk3.svg | MK3_thumbnail.svg |
| MK3S (427) | mk3_bed.stl | mk3.svg | MK3_thumbnail.svg |
| MINI (455) | mini_bed.stl | mini.svg | MINI_thumbnail.svg |
| MK2.5S (479) | mk3_bed.stl | mk3.svg | MK2.5S_thumbnail.svg |
| MK2.5 (507) | mk3_bed.stl | mk3.svg | MK2.5_thumbnail.svg |
| MK2S (535) | mk2_bed.stl | mk2.svg | MK2S_thumbnail.svg |
| xl-2t (814), xl-5t (829), xl+2t (854), xl+5t (869) variants | - | - | XL2_thumbnail.svg, XL5_thumbnail.svg, XLP2T_thumbnail.svg, XLP5T_thumbnail.svg (vendor.yaml:822,837,862,877) |
| MK2SMM (commented out, 580-582); coreonelindx (commented, 286-288) | mk2_bed.stl | mk2.svg | MK2SMM_thumbnail.svg (commented out) |

Unreferenced in the assets dir (grep of vendor.yaml and src found nothing): `MK2.5MMU2_thumbnail.svg, MK2.5SMMU2S_thumbnail.svg, MK39_thumbnail.svg, MK3MMU2_thumbnail.svg, MK3SMMU3_thumbnail.svg, MK3S_thumbnail.svg, miniIS.svg, miniISbeta.svg, mk4is.svg, xlis_alpha.svg, xlis_beta.svg`. `MK2SMM_thumbnail.svg` is referenced only in a commented-out block. `coreonelindx*` files do not exist. Whether the thumbnails/textures carry Prusa branding was not inspected visually. Related: `resources/icons/bed_thumbnail.png` (590x523 PNG) has no quoted-name reference (unreferenced).

## 13. Other resources, docs, tests

| Image | What | Used for | Where |
|---|---|---|---|
| resources/lua/com.prusa3d.slicer.calibration/hreben.svg | Comb-shaped base outline (Inkscape SVG, 200mm) | Flow tower base, embossed to mesh | resources/lua/com.prusa3d.slicer.calibration/flow_tower.lua:42 |
| resources/web/*.html (18 files, light + _dark) | Inline SVG only (80px hexagon status art; Connect wordmark in connect_loading/logged_out). No <img>, no data: URIs, no external images (grep) | Embedded browser pages (connect_loading, connect_error, login_redirected ...) | slic3r-shared/src/Slic3r/App/Browser/BrowserLogicConnectPage.cpp:19; BrowserLogicLogInRedirect.cpp:30 |
| doc/images/long_text_on_button.png | Screenshot | Localization guide only | doc/Localization_guide.md:63 |
| tests/data/*.svg (9), src/slic3r-shared/test/data/presets/prusa-research-sla/.../assets/{sl1.svg,sl1s.svg,SL1_thumbnail.png,SL1S_thumbnail.png} | Test fixtures | Test-only | tests/data/, slic3r-shared/test/data/ |
| bundled_deps/avrdude/avrdude/atmel-docs/** (about 40 png/gif) | Third-party Atmel HTML docs | Not shipped UI | bundled_deps/avrdude/ |

## 14. Embedded, generated and downloaded images

| Image | What | Where |
|---|---|---|
| XPM arrays | None found in built src (grep xpm/XPM) | - |
| data: / base64 image URIs | None (base64 only for G-code thumbnail encoding and JWT/PKCE) | libslic3r/src/libslic3r/GCode/Thumbnails.hpp:5,84-85 |
| Splash screen composite | Runtime-drawn: splashscreen.jpg + SVG logo + version/info text drawn into a bitmap | slic3r-app-desktop/src/Slic3r/App/Desktop/SplashScreen.cpp:42,143-227 |
| G-code thumbnails (PNG/JPG/QOI) | Rendered offscreen from the bed, base64-embedded in G-code | libslic3r/src/libslic3r/GCode/Thumbnails.hpp:49-85; slic3r-shared/src/Slic3r/App/Plater/ThumbnailImageGenerator.cpp:92 |
| 3MF Metadata/thumbnail.png | Plate thumbnail stored in project files | slic3r-shared/src/Slic3r/Biz/Format/3mf.cpp:62,900 |
| User avatar | Downloaded at runtime from the account server (url key avatar_small), saved to temp dir as slic3r3-avatar-<hash>.<ext>; fallback resources/icons/user.svg | slic3r-shared/src/Slic3r/Biz/UserAccount/UserAccountInteractor.cpp:285-288; UserAccountCommunication.cpp:105-126 |
| Remote image URLs (.png/.svg/.jpg literals) | None found in built src, resources/web or resources/lua | - |
| SVG debug output (arrange, CutSurface, seq-arrange) | Developer-only SVG dumps | slic3r-biz-arrange/.../SVGDebugOutputKernelWrapper.cpp:42; CutSurface.cpp:3717 |

## 15. Missing files referenced by built code

`PrusaSlicer_192px_grayscale.png` (MsgDialog.cpp:233 ErrorDialog), `splashscreen-gcodepreview.jpg` (SplashScreen.cpp:42, viewer mode), `PrusaSlicer-gcodeviewer.ico` (unwired rc.in), `stl/gcode/bgcode.icns` (Info.plist.in). Others only from legacy unbuilt code (section 1).

## 16. Unreferenced (found no use in built code)

Includes icons whose only match is a registry entry with an unused enum, or legacy-only refs. Generic names (arrange, copy, paste, redo, search, seam, open, map, empty, ironing, hollow, intersect, plater, export_gcode, fuzzy_skin, undo_menu, redo_menu) matched only unrelated quoted strings and are listed here. Dynamic names: none resolved by concatenation except `mode` (colour-replaced) and Icon registry names; `mode_simple/advanced/expert` files are unused (the single `mode` svg is recoloured, ButtonsDescription/ModePaletteComboBox).

| Image | Note |
|---|---|
| PrusaSlicer-gcodeviewer-mac_128px.png | only commented-out ref TestRenderModule.cpp:118 |
| add.svg |  |
| add_copies.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:88 |
| add_gcode.svg |  |
| add_modifier.svg |  |
| add_negative.svg |  |
| add_part.svg |  |
| advanced_plus.svg |  |
| arrange.svg |  |
| attention.svg |  |
| bed_object_list.svg |  |
| bed_thumbnail.png |  |
| browse.svg |  |
| burn.svg |  |
| cancel.svg |  |
| chain.svg | registered ImguiIconHelper.cpp:244 as Icon::Chain, enum never used |
| change_extruder.svg |  |
| clock.svg |  |
| close_gizmo.svg | registered ImguiIconHelper.cpp:246 as Icon::CloseGizmo, enum never used |
| cog_f.svg |  |
| cogs.svg | registered ImguiIconHelper.cpp:200 as Icon::Cogs, enum never used |
| collapse.svg |  |
| collapse_btn.svg |  |
| colorchange_add.svg |  |
| colorchange_add_f.svg |  |
| colorchange_add_m.svg |  |
| convert_file.svg |  |
| copy.svg |  |
| copy_menu.svg |  |
| cross_focus.svg |  |
| cross_focus_large.svg |  |
| cube.svg | registered ImguiIconHelper.cpp:222 as Icon::Cube, enum never used |
| cube_add.svg | registered ImguiIconHelper.cpp:223 as Icon::CubeAdd, enum never used |
| custom-gcode_gcode.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:224 |
| custom-gcode_measure.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:206 |
| custom-gcode_object-info.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:198 |
| custom-gcode_single.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:393 |
| custom-gcode_slicing-state.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:179 |
| custom-gcode_slicing-state_global.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:163 |
| custom-gcode_stats.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:190 |
| custom-gcode_vector.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:394 |
| custom-gcode_vector-index.svg | only legacy unbuilt src/slic3r: slic3r/GUI/EditGCodeDialog.cpp:395 |
| cut_.svg |  |
| delete.svg |  |
| delete_all.svg |  |
| delete_all_menu.svg |  |
| dot.svg |  |
| dot_white.svg |  |
| drop_to_bed.svg |  |
| edit.svg |  |
| edit_button_pressed.svg | registered ImguiIconHelper.cpp:17 as Icon::SliderFloatEditBtnPressedIcon, enum never used |
| edit_layers_some.svg |  |
| edit_uni.svg |  |
| editor.svg |  |
| editor_menu.svg |  |
| eject_sd.svg |  |
| empty.svg | only legacy unbuilt src/slic3r: slic3r/GUI/InstanceCheck.cpp:759 |
| expand_btn.svg |  |
| export_config.svg |  |
| export_config_bundle.svg |  |
| export_gcode.svg |  |
| export_plater.svg |  |
| extruder_marker.svg |  |
| eye_closed.svg |  |
| eye_open.svg |  |
| fdm_supports_.svg |  |
| filter.svg | registered ImguiIconHelper.cpp:251 as Icon::Filter, enum never used |
| flag_green.svg |  |
| flag_red.svg |  |
| flip_vertically.svg |  |
| fuzzy_skin.svg |  |
| hollow.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Gizmos/GLGizmosManager.cpp:113 |
| hollowing.svg | registered ImguiIconHelper.cpp:215 as Icon::Hollowing, enum never used |
| horizontal_hide.svg |  |
| horizontal_show.svg |  |
| import_config.svg |  |
| import_config_bundle.svg |  |
| import_plater.svg |  |
| instance_add.svg |  |
| instance_remove.svg |  |
| intersect.svg |  |
| ironing.svg |  |
| language.svg |  |
| layers_icon.svg | registered ImguiIconHelper.cpp:154 as Icon::LayersIcon, enum never used |
| layers_white.svg |  |
| lb_message_smile.svg |  |
| lb_notifications.svg |  |
| lb_projects.svg |  |
| lock.svg | only legacy unbuilt src/slic3r: slic3r/GUI/GalleryDialog.cpp:192 |
| lock_closed_white.svg |  |
| lock_open_sys.svg |  |
| make_bold.svg |  |
| make_italic.svg |  |
| make_unbold.svg |  |
| make_unitalic.svg |  |
| map.svg |  |
| mark_X.svg |  |
| mark_Y.svg |  |
| mark_Z.svg |  |
| measure.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Gizmos/GLGizmosManager.cpp:119 |
| minus_modifier.svg | registered ImguiIconHelper.cpp:155 as Icon::MinusModifier, enum never used |
| mirroring_off.svg |  |
| mirroring_on.svg |  |
| mirroring_transparent.svg |  |
| mmu_segmentation.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Gizmos/GLGizmosManager.cpp:118 |
| mode_advanced.svg |  |
| mode_expert.svg |  |
| mode_simple.svg |  |
| mouse_drag.svg | registered ImguiIconHelper.cpp:94 as Icon::MouseDrag, enum never used |
| notification_cancel.svg |  |
| notification_cancel_hover.svg |  |
| notification_clippy.svg | registered ImguiIconHelper.cpp:97 as Icon::ClippyMarker, enum never used |
| notification_close.svg |  |
| notification_close_hover.svg |  |
| notification_documentation.svg |  |
| notification_documentation_hover.svg |  |
| notification_eject_sd.svg |  |
| notification_eject_sd_hover.svg |  |
| notification_info.svg |  |
| notification_minimalize.svg |  |
| notification_minimalize_hover.svg |  |
| notification_open.svg |  |
| notification_open_hover.svg |  |
| notification_pause.svg |  |
| notification_pause_hover.svg |  |
| notification_play.svg |  |
| notification_play_hover.svg |  |
| notification_preferences.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Search.cpp:843 |
| notification_preferences_hover.svg |  |
| notification_right.svg |  |
| notification_right_hover.svg |  |
| notification_warning_grey.svg |  |
| number_of_copies.svg |  |
| objlist_info.svg |  |
| open.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Downloader.cpp:30 |
| open_browser.svg |  |
| output+page_white.svg |  |
| overriden_object_icon.svg |  |
| pad.svg | registered ImguiIconHelper.cpp:216 as Icon::Pad, enum never used |
| paste.svg |  |
| pin.svg |  |
| plater.svg | only legacy unbuilt src/slic3r: slic3r/GUI/HintNotification.cpp:435 |
| plus_new.svg | registered ImguiIconHelper.cpp:114 as Icon::TobBarPlus, enum never used |
| preview_menu.svg |  |
| print_finished.svg |  |
| print_running.svg |  |
| printer_available.svg |  |
| printer_busy.svg |  |
| printer_offline.svg |  |
| printer_white.svg |  |
| re_slice.svg |  |
| redo.svg |  |
| redo_menu.svg |  |
| redo_toolbar.svg |  |
| remove.svg |  |
| remove_copies.svg | only legacy unbuilt src/slic3r: slic3r/GUI/ExtruderSequenceDialog.cpp:46 |
| remove_menu.svg |  |
| resin.svg | registered ImguiIconHelper.cpp:15 as Icon::MaterialIconMarker, enum never used |
| revert_all_.svg |  |
| save_print.svg | registered ImguiIconHelper.cpp:70 as Icon::SavePrint, enum never used |
| save_print_add_bookmark.svg | registered ImguiIconHelper.cpp:73 as Icon::SavePrintAddBookmark, enum never used |
| save_print_to_flash.svg | registered ImguiIconHelper.cpp:71 as Icon::SavePrintToFlash, enum never used |
| save_print_to_local.svg | registered ImguiIconHelper.cpp:72 as Icon::SavePrintToLocal, enum never used |
| scalar_param.svg |  |
| scissors.svg | registered ImguiIconHelper.cpp:234 as Icon::Scissors, enum never used |
| seam.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Gizmos/GLGizmosManager.cpp:116 |
| search.svg |  |
| search_.svg |  |
| set_separate_obj.svg |  |
| settings.svg | only legacy unbuilt src/slic3r: slic3r/GUI/HintNotification.cpp:423 |
| settings_set.svg |  |
| shape_gallery.svg |  |
| skirt+brim.svg |  |
| skirt_brim.svg | registered ImguiIconHelper.cpp:181 as Icon::SkirtBrim, enum never used |
| sla_printer_available.svg |  |
| sla_printer_busy.svg |  |
| sla_printer_offline.svg |  |
| sla_supports.svg | only legacy unbuilt src/slic3r: slic3r/GUI/Gizmos/GLGizmosManager.cpp:114 |
| sla_view_original.svg |  |
| sla_view_processed.svg |  |
| slice_all.svg |  |
| split_object_SMALL.svg |  |
| split_objects.svg |  |
| split_parts.svg |  |
| split_parts_SMALL.svg |  |
| spool.svg | registered ImguiIconHelper.cpp:14 as Icon::FilamentIconMarker, enum never used |
| switch_presets.svg |  |
| tb_new.svg |  |
| test.svg | only legacy unbuilt src/slic3r: slic3r/Utils/Secrets.cpp:21 |
| thumb_down.svg |  |
| thumb_left.svg |  |
| thumb_right.svg |  |
| thumb_up.svg |  |
| timer_dot.svg |  |
| timer_dot_empty.svg |  |
| toolbar_arrow.svg |  |
| toolbar_arrow_2.svg |  |
| toolbar_gcode.svg | registered ImguiIconHelper.cpp:105 as Icon::ToolbarGCode, enum never used |
| toolbar_graph.svg | registered ImguiIconHelper.cpp:106 as Icon::ToolbarGraph, enum never used |
| toolbar_objects.svg | registered ImguiIconHelper.cpp:103 as Icon::ToolbarObjects, enum never used |
| toolbar_pizza.svg |  |
| toolbar_rotation.svg |  |
| toolbar_text.svg |  |
| unchain.svg | registered ImguiIconHelper.cpp:245 as Icon::Unchain, enum never used |
| undo.svg | registered ImguiIconHelper.cpp:18 as Icon::RevertButton, enum never used |
| undo_disabled.svg |  |
| undo_f.svg |  |
| undo_menu.svg |  |
| undo_toolbar.svg |  |
| upload_queue.svg |  |
| vector_filament_param.svg |  |
| vector_param.svg |  |
| view_cube_test.svg |  |
| wrench.svg |  |
| wrench_white.svg |  |
