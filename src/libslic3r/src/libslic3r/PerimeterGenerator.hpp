#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ConfigViews.hpp"

namespace Slic3r {
class ExtrusionEntityCollection;
class LayerRegion;
class Surface;
class PrintRegion;
struct ThickPolyline;

struct PerimeterRegion
{
    const PrintRegion *region;
    ExPolygons         expolygons;
    BoundingBox        bbox;

    explicit PerimeterRegion(const LayerRegion &layer_region);

    // If there is any incompatibility, we don't need to create separate LayerRegions.
    // Because it is enough to split perimeters by PerimeterRegions.
    static bool has_compatible_perimeter_regions(const PrintRegionConfigView &config, const PrintRegionConfigView &other_config);

    static void merge_compatible_perimeter_regions(std::vector<PerimeterRegion> &perimeter_regions);
};

using PerimeterRegions = std::vector<PerimeterRegion>;

} // namespace Slic3r

namespace Slic3r::PerimeterGenerator {

struct Parameters {    
    Parameters(
        double                      layer_height,
        double                      slice_z,
        int                         layer_id,
        Flow                        perimeter_flow,
        Flow                        ext_perimeter_flow,
        Flow                        overhang_flow,
        Flow                        solid_infill_flow,
        const PrintRegionConfigView    &config,
        const PerimeterRegions     &perimeter_regions,
        const bool                  spiral_vase) :   
            layer_height(layer_height),
            slice_z(slice_z),
            layer_id(layer_id),
            perimeter_flow(perimeter_flow), 
            ext_perimeter_flow(ext_perimeter_flow),
            overhang_flow(overhang_flow), 
            solid_infill_flow(solid_infill_flow),
            config(config), 
            perimeter_regions(perimeter_regions),
            spiral_vase(spiral_vase),
            scaled_resolution(scaled<double>(config.get<double>("gcode_resolution"))),
            ext_mm3_per_mm(ext_perimeter_flow.mm3_per_mm()),
            mm3_per_mm(perimeter_flow.mm3_per_mm()),
            mm3_per_mm_overhang(overhang_flow.mm3_per_mm())
        {
        }

    // Input parameters
    double                       layer_height;
    // the layer's slicing Z: the third coordinate of the fuzzy skin noise
    double                       slice_z;
    int                          layer_id;
    Flow                         perimeter_flow;
    Flow                         ext_perimeter_flow;
    Flow                         overhang_flow;
    Flow                         solid_infill_flow;
    const PrintRegionConfigView     &config;
    const PerimeterRegions      &perimeter_regions;

    // Derived parameters
    bool                         spiral_vase;
    double                       scaled_resolution;
    double                       ext_mm3_per_mm;
    double                       mm3_per_mm;
    double                       mm3_per_mm_overhang;
    // Orca: fuzzy skin only within reach of the layer below (none: everywhere)
    std::optional<ExPolygons>    fuzzy_supported_area;
    // Orca: whether the contours / holes get fuzzy skin on this layer (overhang_reverse reverses them always)
    bool                         has_fuzzy_skin{false};
    bool                         has_fuzzy_hole{false};
    // the layer below (Orca overhang_reverse_threshold)
    const ExPolygons            *lower_slices{nullptr};
    // Orca: the rotation of the model on the plate (align_infill_direction_to_model)
    double                       model_rotation{0.};

private:
    Parameters() = delete;
};

void process_classic(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    const ExPolygons           *upper_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons);

void process_arachne(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    const ExPolygons           *upper_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons);

// Orca counterbore_hole_bridging: takes the unsupported parts over the holes out of the slices, to be bridged. Returns
// the infill made of them per slice; a slice may be left empty, and may be split, the parts appended.
std::vector<ExPolygons> process_no_bridge(const Parameters &params, Surfaces &all_surfaces, const ExPolygons *lower_slices);

ExtrusionMultiPath thick_polyline_to_multi_path(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, float tolerance, float merge_tolerance, const std::optional<uint32_t> &perimeter_index = std::nullopt);

} // namespace Slic3r::PerimeterGenerator

#endif
