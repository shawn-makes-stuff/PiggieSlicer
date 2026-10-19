![PiggieSlicer logo](/resources/icons/PiggieSlicer_128px.png)

# PiggieSlicer

PiggieSlicer turns your 3D models into printing instructions and sends them to your 3D printer. It supports FDM and mSLA printers.

PiggieSlicer is a fork of [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) 3.0 by [Prusa Research](https://www.prusa3d.com/). PrusaSlicer is in turn based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community. PiggieSlicer is currently an **early preview** built on the PrusaSlicer 3.0.0 alpha, so expect rough edges.

## How it differs from PrusaSlicer

* **Offline by design.** There is no Prusa Account, Prusa Connect or Printables integration, and no online profile updates. Printer profiles are installed from the files that ship with the application, from ZIP files and from local sources.
* **Local network printing is fully supported.** Send prints straight to OctoPrint, PrusaLink, Moonraker (Klipper), Duet, Repetier, FlashAir, AstroBox, MKS and SL1 hosts, or export to a folder or removable drive.
* **Runs alongside PrusaSlicer.** PiggieSlicer keeps its own settings, presets and logs, and never touches an installed PrusaSlicer.
* **Compatible files.** Projects (3MF) and G-code are written in PrusaSlicer's formats, so they open in PrusaSlicer and work with printer hosts that expect them.

## OrcaSlicer features

PiggieSlicer ports features from [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer), copying Orca's own logic:

* **Orca's process settings and layout.** The print settings sidebar follows Orca's pages and groups (Quality, Strength, Speed, Support, Multimaterial, Others). It includes Orca's scarf seams, wipe options, precise wall and Z height, polyholes, brim ears, skirt types, per-feature jerk, fan kick-start, adaptive bed mesh and more.
* **Settings that fit the printer.** Options that cannot apply to the selected printer (single nozzle, multiple nozzles or a multi-material unit) are hidden, conflicting options cannot both be on, and search only finds settings you can see.
* **Orca printer profiles.** Printer, process and filament profiles converted from Orca's library, including Anycubic.
* **Anycubic Kobra and ACE.** G-code carries Anycubic Slicer Next's header, config and colour blocks so the printer can map filaments to ACE slots.
* **Purge tower.** Automatic brim width scaled to the tower height; Orca's wall types (rectangle, cone, rib) are in progress.
* **Sliced info by feature.** Time, weight and length per feature (walls, infill, supports, ...), coloured like the preview legend.

## Main features

* Set the **printing parameters** precisely, from settings for the **whole print** down to **single-layer adjustments**.
* Modify objects before printing: **paint**, **cut**, **arrange** and **much more**.
* Open **multiple projects** at once, each with **multiple beds** that can have **different settings**.
* Make full use of **multi-material printing**.
* **View the generated print instructions** in an advanced **3D preview**.
* Use the **command-line interface** (`piggie-slicer`) to run PiggieSlicer **without a GUI** in automation setups.

## Building

PiggieSlicer is built from source. See the [build documentation](doc/Build.md). The first build compiles all dependencies, which can take a few hours.

## Technical stack

PiggieSlicer is written in C++ (C++20) and uses CMake as its build system.

The slicing backend relies heavily on [Clipper](https://www.angusj.com/clipper2) by Angus Johnson for polygon boolean operations, offsets and similar work. [Eigen](https://libeigen.gitlab.io/) provides the basic types and the linear algebra.

[wxWidgets](https://wxwidgets.org) handles GUI windows and events across platforms. Most of the UI is a custom OpenGL-based toolkit built on the [Yoga layout engine](https://github.com/facebook/yoga) and [Dear ImGui](https://github.com/ocornut/imgui).

The application uses many other libraries; see [deps/](deps/) and [bundled_deps/](bundled_deps/) for the complete list. We are grateful to their authors and maintainers for open-sourcing their work.

## License

PiggieSlicer is licensed under the [GNU Affero General Public License, version 3](LICENSE), like PrusaSlicer and Slic3r. Copyright © Prusa Research and © Alessandro Ranellucci and the Slic3r contributors. PiggieSlicer changes © their respective authors.
