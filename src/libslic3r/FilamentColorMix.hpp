// PiggieSlicer FilamentColorMix — predicts the apparent color of layer-alternated
// filament mixes and searches mixing recipes for a target color.
//
// Implements the openly published prusa-fdm-mixer model (Prusa Research, MIT,
// https://github.com/prusa3d/prusa-fdm-mixer): a Yule-Nielsen n=3 ratio average in
// linear RGB with empirically fitted lightness / chroma / cyan-hue corrections.
// Reimplemented for PiggieSlicer; model constants credited to that project.

#ifndef slic3r_FilamentColorMix_hpp_
#define slic3r_FilamentColorMix_hpp_

#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

namespace ColorMix {

struct Component
{
    std::string hex;   // "#rrggbb"
    double      ratio; // 0..1, ratios should sum to ~1
};

// Predicted apparent color ("#rrggbb") of filaments alternated in the given ratios.
// A component with ratio ~1 returns its own color unchanged (gradient-safe).
std::string predict_hex(const std::vector<Component> &components);

// CIEDE2000 difference between two "#rrggbb" colors (0 = identical; <2 imperceptible).
double delta_e(const std::string &hex_a, const std::string &hex_b);

struct Recipe
{
    size_t component_a = 0;   // 0-based physical filament index
    size_t component_b = 0;   // 0-based physical filament index (== a for single color)
    int    mix_b_percent = 0; // share of component_b, 0..100
    double delta_e = 1e9;     // ΔE2000 of predicted color vs target
    std::string predicted_hex;
};

// Search all filament pairs and mix percentages (5% steps) for the recipe whose
// predicted color is perceptually closest to `target_hex`. Also considers plain
// single filaments (mix 0%).
Recipe best_two_filament_recipe(const std::string &target_hex, const std::vector<std::string> &filament_hexes);

} // namespace ColorMix

} // namespace Slic3r

#endif // slic3r_FilamentColorMix_hpp_
