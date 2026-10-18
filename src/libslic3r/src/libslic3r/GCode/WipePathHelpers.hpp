#ifndef slic3r_GCode_WipePathHelpers_hpp_
#define slic3r_GCode_WipePathHelpers_hpp_

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

// Orca wipe_inward (GCode/WipePathHelpers): the wipe after an external perimeter moved over the inner walls.
namespace Slic3r {

// Printed prefix of one region's perimeter sequence. Append each entity only after extrusion; later walls and other
// regions cannot support an inward wipe.
struct WipeInwardSupport {
    Lines printed_lines;
    Lines inner_lines;
    void append(const ExtrusionEntity &entity);
};

// The side of the printed path the material lies on: +1 left, -1 right.
int wipe_offset_direction(bool is_ccw, bool is_hole);

// Orca: identify the adjacent inner perimeter from the outgoing wall, excluding support on the air side of a closed
// zero-gap loop. Clamp the requested offset to the distance from the seam end to that support, then select the safest
// supported offset or translated path. If a wide seam gap at a corner truncates every forward candidate, the incoming
// printed wall may be followed backwards instead. All earlier printed perimeters still participate in the complete
// path safety check. Returns false and leaves polyline unchanged when no candidate is supported.
// The polyline starts at seam_start; on success its first point stays seam_start, a sentinel for the nozzle position
// (wipe_start), the rest is the path to wipe along.
// Precondition: preferred_dir is +1 or -1. Distances must be positive.
bool offset_wipe_path_toward_support(Polyline &polyline, Point seam_start, Point seam_end, Point wipe_start,
                                     int preferred_dir, double offset_dist, double max_wipe_length,
                                     const Lines &target_perimeter_lines, const Lines &printed_perimeter_lines,
                                     const Lines &current_perimeter_lines,
                                     double max_support_distance);

} // namespace Slic3r

#endif // slic3r_GCode_WipePathHelpers_hpp_
