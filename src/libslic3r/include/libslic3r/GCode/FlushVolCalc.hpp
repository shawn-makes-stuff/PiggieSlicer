#pragma once

// Colour-based purge (flush) volume estimate.
// Ported from OrcaSlicer v2.5.0-alpha (src/libslic3r/FlushVolCalc.{hpp,cpp},
// src/libslic3r/FlushVolPredictor.{hpp,cpp}, RGB2HSV from src/slic3r/Utils/ColorSpaceConvert.cpp),
// which is based on BambuStudio. Copyright (c) the OrcaSlicer and BambuStudio authors, AGPL-3.0.

#include <string>
#include <vector>

#include "Slic3r/Domain/Color.hpp"

namespace Slic3r::FlushVolCalc {

// Orca: upper clamp of a single flush volume (mm³).
extern const int g_max_flush_volume;

// Flush volume (mm³) needed to change from filament colour `from` to `to`, as Orca's
// FlushVolCalculator::calc_flush_vol() computes it for the standard nozzle data set (0).
// `min_flush_volume` is added to the result (Orca: nozzle volume minus cut retraction, 0 by default).
int calc_flush_volume(const Domain::ColorRGBA& from, const Domain::ColorRGBA& to, int min_flush_volume = 0);

// Row-major matrix [from * n + to] of flush volumes for the given "#RRGGBB" colours, 0 on the
// diagonal. Colours that cannot be decoded are treated as white.
std::vector<float> calc_flush_volumes(const std::vector<std::string>& colours, int min_flush_volume = 0);

} // namespace Slic3r::FlushVolCalc
