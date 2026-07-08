# PiggieSlicer Experimental Feature Backlog

This note captures feature directions for PiggieSlicer beyond upstream
OrcaSlicer. It is intentionally an idea backlog, not a committed roadmap.

## Already-Grounded Ideas

These ideas extend areas PiggieSlicer already has or directly adapt work that
exists in other open projects.

### FullSpectrum calibration lab

Build a native workflow for printing color charts, importing camera or scanner
results, fitting color prediction data, and saving per-printer/per-filament LUTs.

Why it fits: PiggieSlicer already has virtual mixed filaments and Delta E based
matching. A calibration lab would make the color model personal and measurable
instead of relying only on generic prediction.

Related work:
- Prusa FDM Mixer: https://github.com/prusa3d/prusa-fdm-mixer
- Lumina Layers: https://github.com/lumina-layer-studio/Lumina-Layers

### Surface color stitch

Make same-layer spatial color distribution a real paint mode, not just a data
model hook. Instead of only alternating whole layers, split a painted region into
small local color cells or stripes that visually blend on the surface.

Why it fits: `MixedFilament` already has `SameLayerPointillisme`,
gradient component IDs, local-Z fields, and surface offset controls.

Related work:
- Snapmaker's open-source Full Spectrum / Surface Color Stitch direction:
  https://www.tomshardware.com/3d-printing/snapmaker-launches-usd150-000-innovation-fund-for-open-source-3d-printing-cash-rewards-target-developers-backing-the-u1-toolchanger-across-klipper-orcaslicer-and-moonraker-ecosystems

### Brick layers V2

Move beyond the current half-layer inner-wall shift by adding start layer,
extrusion multiplier, loop alternation, non-planar interlocking walls, optional
infill participation, and preview support.

Related work:
- TengerTechnologies Bricklayers: https://github.com/TengerTechnologies/Bricklayers
- GeekDetour BrickLayers: https://github.com/GeekDetour/BrickLayers

### Smart flow and temperature controller

Estimate local volumetric flow over time, vary nozzle temperature, and cap speed
to keep extrusion in a calibrated melt window. Preview the resulting speed and
temperature curves before export.

Related work:
- SB53 G-Code Flow/Temperature Controller:
  https://github.com/sb53systems/G-Code-Flow-Temperature-Controller

### Spoolman and ACE inventory sync

Connect ACE slots, PiggieSlicer filament presets, and Spoolman inventory. Warn
when a plate exceeds remaining grams, bind real spools to slots, and optionally
write back usage after print.

Related work:
- spoolman2slicer: https://github.com/bofh69/spoolman2slicer

### Image tool upgrades

Upgrade the current photo/lithophane tools with SVG input, background removal,
post-preview color replacement, outlines, backplates, transparent layers,
keychain/fridge-magnet holes, bed-size constraints, and model splitting.

Related work:
- Lumina Layers: https://github.com/lumina-layer-studio/Lumina-Layers

## Visible External Integration Candidates

These are the better fit for PiggieSlicer: visible print-result changes, working
external code or post-processors, and a plausible path to integration. The goal
is not to invent a new algorithm from scratch.

### Z anti-aliasing / Z contouring

Vary Z within a layer to reduce staircase artifacts on shallow surfaces. This is
exactly the kind of visible feature that belongs in PiggieSlicer, but note that
the current fork already appears to contain upstream Orca Z contouring settings
and G-code generation paths (`zaa_enabled`, `z_contoured`). Treat this as
"verify, expose, and polish" rather than "new feature".

Integration path:
- Audit the existing local ZAA implementation and UI.
- Add PiggieSlicer test models and preview screenshots.
- Check interactions with FullSpectrum, brick layers, ironing, and arc fitting.

Related work:
- GCodeZAA: https://github.com/Theaninova/GCodeZAA
- BambuStudio-ZAA: https://github.com/adob/BambuStudio-ZAA

### Displacement-map surface texturing

Use grayscale images or generated patterns to physically emboss subtle surface
texture into top/bottom surfaces by segmenting G-code and varying Z. This is
more visually interesting than ordinary fuzzy skin because it can print actual
patterns: fabric grain, leather, stipple, logos, relief maps, and tactile
textures.

Integration path:
- Start as a bundled post-processor or import the algorithm into a `Surface
  Texture` tool.
- Reuse PiggieSlicer's image-loading and top-surface selection flow.
- Add a preview mode that shows displaced surfaces, not just a setting value.

Related work:
- Fuzzyficator, including pattern/displacement-map mode:
  https://github.com/TengerTechnologies/Fuzzyficator
- Non-planar layer FDM wave/structured surfaces:
  https://github.com/makertum/non-planar-layer-fdm

### Paint-on non-planar fuzzy skin

Let users paint texture zones onto a model instead of applying global fuzzy skin.
This creates visibly different surface regions while keeping dimensional areas
clean.

Integration path:
- Use PiggieSlicer's existing paint-mask concepts.
- Route only selected surfaces through the Fuzzyficator-style G-code
  segmentation.
- Make texture masks first-class project data so they survive reloads.

Related work:
- Fuzzyficator paint-on mode:
  https://github.com/TengerTechnologies/Fuzzyficator

### Non-planar ironing / top-surface smoothing

Generate a non-planar final surface pass that follows the intended model surface
instead of rubbing a flat ironing path over stair-stepped layers. Visible result:
smoother roofs, domes, curved logos, and shallow organic surfaces.

Integration path:
- Start with generated ironing paths on selected upward-facing surfaces.
- Keep it opt-in and previewable because collision and flow handling matter.
- Prefer integrating with existing ZAA paths if possible.

Related work:
- Non-planar ironing: https://github.com/etinaude/Non-planar-ironing
- TengerTechnologies NonPlanarIroning:
  https://github.com/TengerTechnologies/NonPlanarIroning
- GCodeZAA ironing support: https://github.com/Theaninova/GCodeZAA

### Non-planar infill and interlocking interiors

Deform infill, internal walls, or selected internal features in Z waves. This is
visible in transparent/sectioned prints and can materially change strength and
failure behavior.

Integration path:
- Add infill-only wave deformation first.
- Then combine with brick layers and selective reinforcement zones.
- Keep external surfaces planar unless explicitly requested.

Related work:
- NonPlanarInfill: https://github.com/TengerTechnologies/NonPlanarInfill
- Bricklayers non-planar interlocking walls:
  https://github.com/TengerTechnologies/Bricklayers

### Conical / radial non-planar slicing

Transform geometry and G-code to print supportless or low-support overhangs using
conical or radial layer geometry. This has major visible impact but likely needs
clear machine/nozzle-geometry guardrails.

Integration path:
- Treat as a separate experimental print mode, not a normal checkbox.
- Start with "export to external transformer and re-import preview" before
  native integration.
- Restrict to compatible models/printers until collision logic is reliable.

Related work:
- EasyConical: https://github.com/DigitalGrin/EasyConical
- Radial Non-Planar Slicer:
  https://github.com/jyjblrd/Radial_Non_Planar_Slicer
- RotBot non-planar G-code transformation:
  https://github.com/RotBotSlicer/Nonplanar_Slicing
- S4 generic non-planar slicer:
  https://github.com/jyjblrd/S4_Slicer

### Curved-layer slicing for normal 3-axis printers

Use curved layers through more of the part, not only the top surface, to reduce
stair-stepping and improve curved shells. This is more ambitious than ZAA and
less mature, but there is an AGPL research implementation.

Integration path:
- Prototype as a standalone "CurviSlicer bridge" before pulling code into
  libslic3r.
- Use only on known-safe demonstration models at first.
- Require collision/nozzle-clearance checks before enabling general use.

Related work:
- CurviSlicer: https://github.com/mfx-inria/curvislicer

### Procedural toolpath generators

Add a "generated prints" workspace for objects that are not sliced from a mesh at
all: waves, woven vessels, lampshades, spring lattices, texture coupons, purge
art, and decorative mathematical surfaces. These are visually distinct because
the toolpath is the design.

Integration path:
- Embed or call a Python generator initially.
- Generate G-code plus a lightweight preview mesh/path.
- Let generated objects coexist with normal sliced plates where possible.

Related work:
- FullControl: https://github.com/FullControlXYZ/fullcontrol
- Parametric non-planar library:
  https://github.com/kobrahillbily/parametric-nonplanar-library

### Layer-stacked photo models

Build a native or semi-native importer for projects that convert 2D images into
multi-color stacked 3D models, beyond the current simple lithophane path. This
is adjacent to FullSpectrum but produces a visibly different artifact: flat
photo-like relief panels using transmission/stacking behavior.

Integration path:
- Add "Import layered image model" using external generator output first.
- Map generated layers directly to PiggieSlicer physical and mixed filaments.
- Later fold in calibration and auto-selection.

Related work:
- ChromaStack: https://github.com/borealis-zhe/ChromaStack
- Lumina Layers: https://github.com/lumina-layer-studio/Lumina-Layers

### Purge-as-visible-object mode

This one is not sourced from a single mature repo, so keep it lower priority, but
it has real visible output: swatch tiles, labels, color chips, calibration
coupons, keychains, or small utility parts made from purge that would otherwise
go to a tower.

Integration path:
- Reuse existing purge planning and generated primitive meshes.
- Start with color chips and calibration coupons, not arbitrary user models.
- Show tower waste converted into printed objects.

## Avoid For Now

### Restored Bambu cloud/network bypass work

This is visible in the ecosystem, but it conflicts with PiggieSlicer's LAN-only
positioning and adds legal/vendor-risk noise.

Reference:
- OrcaSlicer-bambulab shutdown coverage:
  https://www.tomshardware.com/3d-printing/developer-re-enables-3d-printer-features-that-bambu-lab-disabled-firm-promptly-threatens-legal-action-orcaslicer-bambulab-project-now-shuttered
