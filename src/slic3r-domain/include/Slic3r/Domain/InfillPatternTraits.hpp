#pragma once

#include "Slic3r/Domain/ConfigDefsFDM.hpp"

// Infill pattern traits shared by the infill generation (libslic3r Fill) and the sidebar's settings rules
// (SettingsToggles).
namespace Slic3r::Domain {

// Orca is_separable_infill_pattern: the patterns separated_infills can center on each connected body
inline bool is_separable_infill_pattern(InfillPattern pattern)
{
    switch (pattern) {
    case InfillPattern::ipMonotonic:
    case InfillPattern::ipMonotonicLines:
    case InfillPattern::ipRectilinear:
    case InfillPattern::ipAlignedRectilinear:
    case InfillPattern::ipZigZag:
    case InfillPattern::ipCrossZag:
    case InfillPattern::ipLockedZag:
    case InfillPattern::ipLine:
    case InfillPattern::ipGrid:
    case InfillPattern::ipTriangles:
    case InfillPattern::ipStars:
    case InfillPattern::ipCubic:
    case InfillPattern::ipQuarterCubic:
    case InfillPattern::ipHoneycomb:
    case InfillPattern::ip3DHoneycomb:
    case InfillPattern::ip3DHoneycombClassic:
    case InfillPattern::ipLateralHoneycomb:
    case InfillPattern::ipLateralLattice:
    case InfillPattern::ipCrossHatch:
    case InfillPattern::ipTpmsD:
    case InfillPattern::ipTpmsFK:
    case InfillPattern::ipGyroid:
    case InfillPattern::ipHilbertCurve:
    case InfillPattern::ipArchimedeanChords:
    case InfillPattern::ipOctagramSpiral:
        return true;
    default:
        return false;
    }
}

// Orca is_tpms_adaptive_pattern: the patterns graded by tpms_adaptive
inline bool is_tpms_adaptive_pattern(InfillPattern pattern)
{
    return pattern == InfillPattern::ipGyroid || pattern == InfillPattern::ipTpmsD || pattern == InfillPattern::ipTpmsFK;
}

// Orca is_smoothable_infill_pattern: the patterns rounding their corners (sparse_infill_smooth_factor); Grid,
// Triangles, Tri-hexagon and Cubic only in their trapezoidal (multiline) form.
inline bool is_smoothable_infill_pattern(InfillPattern pattern, int multiline)
{
    switch (pattern) {
    case InfillPattern::ipHilbertCurve:
    case InfillPattern::ipOctagramSpiral:
    case InfillPattern::ipLightning:
    case InfillPattern::ipHoneycomb:
    case InfillPattern::ip3DHoneycomb:
    case InfillPattern::ipConcentric:
    case InfillPattern::ipCrossHatch:
        return true;
    case InfillPattern::ipGrid:
    case InfillPattern::ipTriangles:
    case InfillPattern::ipStars:
    case InfillPattern::ipCubic:
        return multiline > 1;
    default:
        return false;
    }
}

} // namespace Slic3r::Domain
