# CLAUDE.md

## Project Structure & Module Organization
PiggieSlicer's C++17 sources live in `src/`, split by feature modules and platform adapters. User assets, icons, and printer presets are in `resources/`; translations stay in `localization/`. Tests sit in `tests/`, grouped by domain (`libslic3r/`, `sla_print/`, etc.) with fixtures under `tests/data/`. CMake helpers reside in `cmake/`, and longer references live in `doc/` and `SoftFever_doc/`. Automation scripts belong in `scripts/` and `tools/`. Treat everything in `deps/` and `deps_src/` as vendored snapshots.

## Build, Test, and Development Commands
Use out-of-source builds:

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` configures dependencies and generates build files.
- `cmake --build build --target OrcaSlicer --config Release` compiles the app target inherited from upstream; the Windows app output is branded as `PiggieSlicer.exe`.
- `cmake --build build --target tests` then `ctest --test-dir build --output-on-failure` runs automated suites.

Platform helpers such as `build_linux.sh`, `build_release_macos.sh`, and `build_release_vs2022.bat` wrap the same flow with toolchain flags.

## Coding Style & Naming Conventions
`.clang-format` enforces 4-space indents, a 140-column limit, aligned initializers, and brace wrapping for classes and functions. Prefer `CamelCase` for classes, `snake_case` for functions and locals, and `SCREAMING_CASE` for constants, matching conventions in `src/`.

## Testing Guidelines
Unit tests rely on Catch2 (`tests/catch2/`). Name specs after the component under test and keep deterministic fixtures or sample G-code in `tests/data/`. Document manual printer validation when automated coverage is insufficient, especially for Anycubic LAN, ACE Pro, and Full Spectrum mixed-filament workflows.

## Commit & Pull Request Guidelines
Use concise, sentence-style commit subjects. Complete `.github/pull_request_template.md`, include screenshots for UI changes, and mention impacted presets, translations, printer protocols, or release packaging.

## Security & Configuration Tips
Follow `SECURITY.md` for vulnerability reporting. Keep API tokens and printer credentials out of tracked configs; use `sandboxes/` for experimental settings. When touching third-party code in `deps_src/`, record the upstream commit or release in your PR description.
