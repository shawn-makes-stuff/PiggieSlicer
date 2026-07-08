// PiggieSlicer: paint an image onto the upward-facing surface of the selected
// object, mapping each pixel to the closest achievable filament color
// (physical filaments + enabled FullSpectrum mixes) via the ColorMix model.

#ifndef slic3r_SurfaceImageTool_hpp_
#define slic3r_SurfaceImageTool_hpp_

class wxWindow;

namespace Slic3r { namespace GUI {

// Interactive flow: pick an image file, then paint the top surface of the
// currently selected object. Shows message boxes on error.
void apply_photo_to_top_surface(wxWindow *parent);

// HueForge-style color lithophane: image -> relief plate whose height maps
// luminance through a dark-to-light filament stack, with automatic filament
// changes at the layer transitions.
void generate_hueforge_lithophane(wxWindow *parent);

} } // namespace Slic3r::GUI

#endif // slic3r_SurfaceImageTool_hpp_
