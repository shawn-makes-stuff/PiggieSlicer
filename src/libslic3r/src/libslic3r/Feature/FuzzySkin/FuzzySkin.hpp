#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

#include <optional>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

namespace Slic3r::Arachne {
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r::PerimeterGenerator {
struct Parameters;
} // namespace Slic3r::PerimeterGenerator

namespace Slic3r::Feature::FuzzySkin {

// A region's fuzzy skin settings on a layer (Orca FuzzySkinConfig).
struct FuzzySkinConfig
{
    Domain::FuzzySkinType      type;
    coord_t                    thickness;      // scaled
    coord_t                    point_distance; // scaled
    bool                       first_layer;
    Domain::FuzzySkinNoiseType noise_type;
    double                     noise_scale;
    int                        noise_octaves;
    double                     noise_persistence;
    Domain::FuzzySkinMode      mode;
    int                        ripples_per_layer;
    double                     ripple_offset; // percent of a wavelength
    int                        layers_between_ripple_offset;
    int                        layer_id;

    FuzzySkinConfig(const PrintRegionConfigView &config, int layer_id);
};

void fuzzy_polyline(Points &poly, bool closed, double slice_z, const FuzzySkinConfig &cfg);

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, double slice_z, double layer_height, const FuzzySkinConfig &cfg, bool closed);

bool should_fuzzify(const FuzzySkinConfig &config, int layer_id, size_t perimeter_idx, bool is_contour);

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfigView &base_config, const PerimeterRegions &perimeter_regions, int layer_id, size_t perimeter_idx, bool is_contour, double slice_z, const std::optional<ExPolygons> &supported);

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfigView &base_config, const PerimeterRegions &perimeter_regions, int layer_id, size_t perimeter_idx, bool is_contour, double slice_z, double layer_height, const std::optional<ExPolygons> &supported);

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_FuzzySkin_hpp_
