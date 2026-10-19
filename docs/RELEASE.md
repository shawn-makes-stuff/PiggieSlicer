# Making a release

Windows only for now: a portable zip (no installer) on a GitHub release.

1. **Version.** Set `PIGGIESLICER_VERSION` in `version.inc` (for example `0.0.2-alpha`). The splash, window title,
   `--version` and the file properties take it from there. `SLIC3R_VERSION` stays the PrusaSlicer base.
2. **Build** the Release app (the app must not be running, it locks the exe):
   ```
   cmake --build build --config Release --target slic3r-app-launcher
   ```
   From Git Bash set `WindowsSdkDir` and `WindowsSDKVersion` first if CMake has to regenerate.
3. **Test** what changed: the tests (`libslic3r_tests`, `fff_print_tests`) and a few slices with the CLI, for example
   `build/src/slic3r-app-launcher/Release/piggie-slicer.exe --datadir <empty folder> --export-gcode -o out.gcode project.3mf`.
4. **Package:**
   ```
   python tools/release/package.py
   ```
   It writes `build/release/PiggieSlicer-<version>-win64.zip`: the exe, its DLLs, `resources/` without
   `test_data` (tests only) and the licence. It then unzips it into a temporary folder and checks that the app
   starts and prints the version.
5. **Commit and merge** to `master`, then push.
6. **Release** (pre-release while the version says alpha or beta):
   ```
   gh release create v<version> build/release/PiggieSlicer-<version>-win64.zip --target master --prerelease \
       --title "PiggieSlicer <version>" --notes-file notes.md
   ```
   The notes say what is new, where to find it, and the known limits.
