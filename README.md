# PiggieSlicer

PiggieSlicer is a pink-themed fork of OrcaSlicer focused on a cleaner local workflow for Anycubic Kobra-series printers and ACE Pro material handling.

This project keeps the slicing foundation and broad printer compatibility inherited from OrcaSlicer, while adding PiggieSlicer-specific UI polish, Anycubic LAN controls, ACE material synchronization, and Full Spectrum mixed-filament tools.

## Current Focus

- Native Anycubic LAN device panel for local printer control.
- Send-to-printer support for Anycubic LAN printers from Prepare and Preview.
- ACE Pro material sync so project filaments can be populated from the materials reported by the printer.
- Editable ACE material slots, including material type and color.
- Full Spectrum mixed-filament sidebar panel for virtual blended colors.
- PiggieSlicer branding, icons, dialogs, and pink UI accents.

## Project Status

PiggieSlicer is under active development. Expect fast-moving UI and device-panel changes while the Anycubic workflow is being built out.

The current Windows release build artifact is produced from the local build tree. Packaging and release automation are still being cleaned up for this fork.

## Download

Releases will be published from:

https://github.com/shawn-makes-stuff/PiggieSlicer/releases

Until formal releases are available, use locally built artifacts from the project maintainers.

## Building

PiggieSlicer is based on the OrcaSlicer build system and still uses many upstream target names internally. The main application output is branded as `PiggieSlicer.exe` on Windows.

For now, use the existing OrcaSlicer build instructions as the baseline for dependencies and platform setup:

https://github.com/OrcaSlicer/OrcaSlicer/wiki/How-to-build

Windows development builds in this workspace have been validated with Visual Studio 2022 and the existing CMake/MSBuild project.

## Anycubic LAN Notes

PiggieSlicer's Anycubic support is intended to work locally on your LAN without requiring cloud printing.

The Anycubic device panel can:

- discover and remember local printers,
- auto-connect to a previously loaded online printer when the app starts,
- show live temperatures, job progress, fans, lights, and ACE status,
- send files for printing over LAN,
- control supported camera streaming,
- sync and edit ACE material slots.

## Full Spectrum Mixed Filaments

The Mixed Filaments panel lets you define virtual project filaments from two selected colors and blend percentages. These mixed filaments are used by the Full Spectrum slicing workflow and can be edited or removed like normal project-side filament entries.

## Attribution

PiggieSlicer is a fork of OrcaSlicer. OrcaSlicer itself builds on work from Bambu Studio, PrusaSlicer, Slic3r, SuperSlicer, and other open-source slicing projects.

We keep that lineage visible because this project depends on a large body of upstream work:

- OrcaSlicer: https://github.com/OrcaSlicer/OrcaSlicer
- Bambu Studio: https://github.com/bambulab/BambuStudio
- PrusaSlicer: https://github.com/prusa3d/PrusaSlicer
- Slic3r: https://github.com/Slic3r/Slic3r
- SuperSlicer: https://github.com/supermerill/SuperSlicer

## License

PiggieSlicer is licensed under the GNU Affero General Public License, version 3, following OrcaSlicer's licensing.

The bundled third-party components retain their own licenses. See `LICENSE.txt` and upstream notices for details.
