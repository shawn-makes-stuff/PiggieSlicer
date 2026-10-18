#include "libslic3r/Fill/FillConcentricInternal.hpp"

#include <algorithm>

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/HwConfigUtils.hpp"
#include "libslic3r/ShortestPath.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

void FillConcentricInternal::_fill_surface_single(const FillParams              &params,
                                                  unsigned int                   thickness_layers,
                                                  const std::pair<float, Point> &direction,
                                                  ExPolygon                      expolygon,
                                                  ThickPolylines                &thick_polylines_out)
{
    // no rotation is supported for this infill pattern
    const Point   bbox_size   = BB::sizes(Algorithms::Polygon::get_bounding_box(expolygon.contour));
    const coord_t min_spacing = scaled<coord_t>(this->spacing);
    const coord_t loops_count = std::max(bbox_size.x(), bbox_size.y()) / min_spacing + 1;
    // the area without the overlap with the walls, as in FillConcentric
    const Polygons polygons = offset(expolygon, float(min_spacing) / 2.f);

    const std::vector<double> nozzle_diameters = Biz::Slicing::get_nozzle_diameters(this->region_config.hw_config());
    const double              min_nozzle_diameter = *std::min_element(nozzle_diameters.begin(), nozzle_diameters.end());
    Arachne::WallToolPathsParams input_params;
    input_params.min_bead_width                   = 0.85 * min_nozzle_diameter;
    input_params.min_feature_size                 = 0.25 * min_nozzle_diameter;
    input_params.wall_transition_length           = 0.4;
    input_params.wall_transition_angle            = 10;
    input_params.wall_transition_filter_deviation = 0.25 * min_nozzle_diameter;
    input_params.wall_distribution_count          = 1;
    Arachne::WallToolPaths wallToolPaths(polygons, min_spacing, min_spacing, loops_count, 0, params.layer_height, input_params);

    std::vector<Arachne::VariableWidthLines>    loops = wallToolPaths.getToolPaths();
    std::vector<const Arachne::ExtrusionLine *> all_extrusions;
    for (Arachne::VariableWidthLines &loop : loops)
        for (const Arachne::ExtrusionLine &wall : loop)
            all_extrusions.emplace_back(&wall);

    // the closed loops start nearest to the origin, then all are ordered by the shortest traverse
    const size_t first_poly_idx = thick_polylines_out.size();
    const Point  last_pos(0, 0);
    for (const Arachne::ExtrusionLine *extrusion : all_extrusions) {
        if (extrusion->empty())
            continue;
        ThickPolyline thick_polyline = Arachne::to_thick_polyline(*extrusion);
        if (extrusion->is_closed)
            thick_polyline.start_at_index(nearest_point_index(thick_polyline.points, last_pos));
        thick_polylines_out.emplace_back(std::move(thick_polyline));
    }

    // clip the paths to prevent the extruder from getting exactly on the first point of the loop
    // Keep valid paths only.
    size_t j = first_poly_idx;
    for (size_t i = first_poly_idx; i < thick_polylines_out.size(); ++i) {
        thick_polylines_out[i].clip_end(this->loop_clipping);
        if (thick_polylines_out[i].is_valid()) {
            if (j < i)
                thick_polylines_out[j] = std::move(thick_polylines_out[i]);
            ++j;
        }
    }
    if (j < thick_polylines_out.size())
        thick_polylines_out.erase(thick_polylines_out.begin() + int(j), thick_polylines_out.end());

    // Orca reorder_by_shortest_traverse
    Points start_points;
    start_points.reserve(thick_polylines_out.size());
    for (const ThickPolyline &thick_polyline : thick_polylines_out)
        start_points.push_back(thick_polyline.points.front());
    ThickPolylines unordered = std::move(thick_polylines_out);
    thick_polylines_out.clear();
    for (size_t i : chain_points(start_points))
        thick_polylines_out.emplace_back(std::move(unordered[i]));
}

} // namespace Slic3r
