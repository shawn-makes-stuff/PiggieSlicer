#include "libslic3r/Print.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Fill/FillCornerSmoothing.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Fill/Lightning/Generator.hpp"
#include "libslic3r/Fill/FillLightning.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Fill/Lightning/Layer.hpp"
#include "libslic3r/Point.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::FillLightning {

void Filler::_fill_surface_single(
    const FillParams              &params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point> &direction,
    ExPolygon                      expolygon,
    Polylines                     &polylines_out)
{
    const Layer &layer      = generator->getTreesForLayer(this->layer_id);
    Polylines    fill_lines = layer.convertToLines(Algorithms::ExPolygon::to_polygons(expolygon), scaled<coord_t>(0.5 * this->spacing - this->overlap));

    // Orca: the turns of the branches rounded, the hairpins left sharp; the reach capped at half the distance
    // between two branches (a branch is as long as the object, half of a leg would merge it with its neighbour),
    // and with more than one line per infill wall at half the printed width (the outlines of the branches merge
    // into one, moving a branch more breaks it up into separate loops).
    const double branch_width   = scaled<double>(this->spacing) * params.multiline;
    const double branch_spacing = branch_width / std::max(double(params.density), EPSILON);
    const double max_reach      = 0.5 * (params.multiline > 1 ? branch_width : branch_spacing);
    smooth_polylines_corners(fill_lines, params.smooth_factor, scaled<double>(params.resolution), max_reach);

    // Orca fill_multiline
    multiline_fill(fill_lines, params, spacing);
    fill_lines = intersection_pl(std::move(fill_lines), expolygon);

    if (params.dont_connect() || fill_lines.size() <= 1) {
        append(polylines_out, chain_polylines(std::move(fill_lines)));
    } else
        connect_infill(std::move(fill_lines), expolygon, polylines_out, this->spacing, params);
}

void GeneratorDeleter::operator()(Generator *p) {
    delete p;
}

GeneratorPtr build_generator(const PrintObject &print_object, const double fill_density, const std::function<void()> &throw_on_cancel_callback)
{
    return GeneratorPtr(new Generator(print_object, fill_density, throw_on_cancel_callback));
}

} // namespace Slic3r::FillAdaptive
