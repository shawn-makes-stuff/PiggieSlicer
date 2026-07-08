<div align="center">

# 🐷 PiggieSlicer

**A color-mixing, LAN-only fork of OrcaSlicer for the Anycubic Kobra 3 + ACE Pro.**

Based on [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer) v2.4.2 · AGPL-3.0 · Not affiliated with Anycubic, OrcaSlicer, Bambu Lab, or Prusa Research

</div>

---

PiggieSlicer is everything OrcaSlicer is, plus a set of experimental color and hardware features built around single-nozzle multi-material printing — and minus every cloud hook. It slices, paints, mixes, and talks to your printer over your own network. Nothing leaves your LAN.

## What's different

### 🎨 FullSpectrum color mixing
Print colors you don't own. Define **virtual mixed filaments** — blends of two physical spools created by alternating layers — and use them everywhere a real filament works: assign them to objects, paint with them in the color-painting tool, and preview them on the model.

- **Calibrated color prediction.** Mixed-color swatches use a print-calibrated model (the open-source [prusa-fdm-mixer](https://github.com/prusa3d/prusa-fdm-mixer) math, reimplemented; MIT) instead of naive color averaging — what you see is much closer to what prints.
- **Match a color.** Pick any target color and PiggieSlicer searches every filament pair and ratio for the perceptually closest recipe (ΔE2000).
- **Layer patterns.** Beyond smooth blends: fine stripes, 2/2, 4/4 and 8/8 bands, sinusoidal "wave" bands, and organic noise bands.
- **One-click palettes.** Add a full dark-to-light **shade ramp** between two spools, or a **3-color set** for multi-component palette experiments, straight from the Mixed Filaments dialog.
- **Mix-aware purge reduction.** Toolchanges between the two components of an active mix purge at 30% — the transition is supposed to blend, so most of the purge would be wasted time and filament.

### 🖼️ Image tools
- **Photo on Top Surface** — pick an image and PiggieSlicer paints it onto the selected object's top surface, dithering across your physical spools *and* your virtual mixes.
- **Color Lithophane** — HueForge-style relief plates: an image becomes a heightmap whose luminance shines through a dark-to-light filament stack, with filament changes placed automatically.
- **Colored model import** — OBJ files with vertex colors map onto your full palette, including mixes, at import time.
- **FullSpectrum Surface Shading** — pick a shadow filament, a highlight filament, and a simulated light direction; PiggieSlicer generates the intermediate mixes and paints the object's facets by light intensity for a shaded, sculptural look. Adjustable ramp steps and contrast (Soft/Normal/Hard).

### 🧱 Brick layers *(experimental)*
An off-by-default Special Mode option that raises inner wall loops by half a layer so wall beads interlock like brickwork, substantially improving Z-direction strength.

### 📡 Anycubic LAN device panel
A native device page for Anycubic printers — no account, no cloud, no vendor app:

- Auto-discovery on your network (plus add-by-IP and nicknames)
- Live status, temperatures, fans, progress, pause/resume/stop
- **ACE Pro control**: slot colors and materials (editable), auto-refill, drying with **material presets** (PLA/PETG/TPU/PA) and **auto-dry during prints**
- **Spool budgets**: track grams remaining per slot
- Axis jog, homing, lights, typed G-code console, embedded camera stream

The LAN protocol was independently reverse-engineered and lives in-tree (`src/slic3r/Utils/AcLan.*`, documented in `doc/AcLan_protocol.md`).

### 🔒 No phoning home
- **No Bambu backend.** Bambu Cloud login is removed, the system-info telemetry endpoint is deleted outright, and no code path calls `api.bambulab.com`. (Bambu *printer* LAN support and BambuStudio 3MF compatibility are kept.)
- **No Anycubic cloud.** Everything works over LAN; no account required.
- **Stealth mode is the default.** Profile syncing is local-only; use manual preset export/import when moving between machines.

### Everything from OrcaSlicer v2.4.2
Tracked against upstream — calibration suite, precise walls, scarf seams, the full printer profile library, and the rest. Upstream merges are part of this fork's routine.

## Printers

Built for and tested on the **Anycubic Kobra 3 + ACE Pro**. Other Anycubic LAN printers should largely work (the protocol is shared); other vendors' printers work exactly as they do in OrcaSlicer.

## Building (Windows)

```bat
build-deps.cmd    :: one-time dependency build (long)
build-slicer.cmd  :: configure + build + install into build\OrcaSlicer\
```

Requires VS2022 Build Tools, CMake 3.31.x, and Strawberry Perl. The app is `PiggieSlicer.exe` (a launcher shim) next to `OrcaSlicer.dll` (the actual program).

## Status & disclaimers

This is a personal, experimental fork. The color features print real prototypes but expect rough edges; **brick layers and the image tools are v1**. Use at your own risk — a misconfigured slicer can damage a printer.

## Credits & license

PiggieSlicer stands on [OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer), which stands on [Bambu Studio](https://github.com/bambulab/BambuStudio), [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer), and [Slic3r](https://github.com/Slic3r/Slic3r). Color-mixing prediction math from [prusa3d/prusa-fdm-mixer](https://github.com/prusa3d/prusa-fdm-mixer) (MIT). FullSpectrum concept inspired by [ratdoux/OrcaSlicer-FullSpectrum](https://github.com/ratdoux/OrcaSlicer-FullSpectrum) and Prusa ColorMix.

Licensed **AGPL-3.0** like its upstreams — see [LICENSE.txt](LICENSE.txt).
