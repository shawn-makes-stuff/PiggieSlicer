#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "libslic3r/Brim.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/MaterialType.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/PrintBase.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

// Orca SCALED_RESOLUTION
static constexpr double scaled_resolution = 0.0125 / SCALING_FACTOR;
// Orca POLY_SIDE_COUNT: the sides of a brim ear circle
static constexpr size_t poly_side_count = 24;

static void append_and_translate(ExPolygons &dst, const ExPolygons &src, const PrintInstance &instance) {
    size_t dst_idx = dst.size();
    Slic3r::append(dst, src);
    for (; dst_idx < dst.size(); ++dst_idx)
        dst[dst_idx].translate(instance.shift().x(), instance.shift().y());
}

static void append_and_translate(Polygons &dst, const Polygons &src, const PrintInstance &instance) {
    size_t dst_idx = dst.size();
    Slic3r::append(dst, src);
    for (; dst_idx < dst.size(); ++dst_idx)
        dst[dst_idx].translate(instance.shift().x(), instance.shift().y());
}

// Orca: the brim area translated into the print coordinates, per instance.
static void append_and_translate(const ExPolygons &src, const PrintInstance &instance, size_t instance_idx,
                                 std::map<ObjectInstanceID, ExPolygons> &brim_area_map) {
    ExPolygons src_shifted = src;
    for (ExPolygon &expoly : src_shifted)
        expoly.translate(instance.shift().x(), instance.shift().y());
    Slic3r::append(brim_area_map[{ instance.print_object->id(), instance_idx }], std::move(src_shifted));
}

// Returns ExPolygons of the bottom layer of the print object after elephant foot compensation.
static ExPolygons get_print_object_bottom_layer_expolygons(const PrintObject &print_object)
{
    ExPolygons ex_polygons;
    for (LayerRegion *region : print_object.layers().front()->regions())
        Slic3r::append(ex_polygons, closing_ex(region->slices().surfaces, float(SCALED_EPSILON)));
    return ex_polygons;
}

// Orca brim_use_efc_outline: the brim follows the outline after the elephant foot compensation.
static bool use_brim_efc_outline(const PrintObject &object)
{
    return object.config().get<bool>("brim_use_efc_outline") && object.config().get<double>("elefant_foot_compensation") > 0. &&
        object.config().get<int>("elefant_foot_compensation_layers") > 0 && object.config().get<int>("raft_layers") == 0;
}

// Orca Polygon::convex_points() / concave_points(): the vertices turning left (convex) / right (concave) by more than
// angle_threshold.
static Points convex_concave_points(const Polygon &polygon, double angle_threshold, bool convex)
{
    const Points &poly = polygon.points;
    if (poly.size() < 3)
        return {};
    // The filter function is cross2(v1, v2) {>,<} 0, then the angle at the vertex larger than the threshold: the
    // cos of the angle between the edges below the cos of the threshold (just above it, so a right angle does not
    // pass a threshold of a right angle).
    const double cos_threshold = std::cos(std::nextafter(angle_threshold, +INFINITY));
    Points out;
    Point  p1 = poly.back();
    Vec2d  v1 = (p1 - *(poly.end() - 2)).cast<double>();
    for (const Point &p2 : poly) {
        const Vec2d v2 = (p2 - p1).cast<double>();
        const double cross = cross2(v1, v2);
        if ((convex ? cross > 0. : cross < 0.) && (angle_threshold <= EPSILON || v1.normalized().dot(v2.normalized()) < cos_threshold))
            out.emplace_back(p1);
        v1 = v2;
        p1 = p2;
    }
    return out;
}

// Orca make_brim_ears_auto (from SuperSlicer): circles of the ear size at the sharp corners of the outline (the
// convex ones of the outer outline, the concave ones of the holes).
static ExPolygons make_brim_ears_auto(const ExPolygons &obj_expoly, coord_t size_ear, coord_t ear_detection_length,
                                      double brim_ears_max_angle, bool is_outer_brim)
{
    ExPolygons mouse_ears_ex;
    if (size_ear <= 0)
        return mouse_ears_ex;
    // the places to put the ears
    const double angle_threshold = (180 - brim_ears_max_angle) * PI / 180.0;
    Points pt_ears;
    for (const ExPolygon &poly : obj_expoly) {
        Polygon decimated_polygon = poly.contour;
        if (ear_detection_length > 0) {
            Points points = poly.contour.points;
            points.push_back(points.front());
            points = Algorithms::DouglasPeucker::douglas_peucker(points, double(ear_detection_length));
            // not below 4 points, as that surely fills everything anyway
            if (points.size() > 4) {
                points.erase(points.end() - 1);
                decimated_polygon.points = points;
            }
        }
        append(pt_ears, convex_concave_points(decimated_polygon, angle_threshold, is_outer_brim));
    }

    // the ear pattern
    Polygon point_round;
    for (size_t i = 0; i < poly_side_count; i++) {
        const double angle = (2.0 * PI * i) / poly_side_count;
        point_round.points.emplace_back(coord_t(size_ear * cos(angle)), coord_t(size_ear * sin(angle)));
    }
    for (const Point &pt : pt_ears) {
        mouse_ears_ex.emplace_back();
        mouse_ears_ex.back().contour = point_round;
        mouse_ears_ex.back().contour.translate(pt);
    }
    return mouse_ears_ex;
}

// Orca getadhesionCoeff (BBS): the adhesion coefficient of the material of the model parts printed on the first layer
// (the last one found).
static double get_adhesion_coefficient(const PrintObject &object)
{
    std::vector<unsigned int> first_layer_extruders;
    for (const LayerRegion *region : object.layers().front()->regions())
        if (region->has_extrusions())
            first_layer_extruders.push_back(region->region().extruder(frExternalPerimeter));
    const std::vector<std::string> filament_types = object.print()->config().get<std::vector<std::string>>("filament_type");
    double adhesion_coefficient = 1;
    for (const Domain::ModelVolume *model_volume : object.model_object()->volumes)
        for (unsigned int extruder : first_layer_extruders)
            if (model_volume->extruder_id() == int(extruder) && extruder >= 1 && extruder <= filament_types.size())
                MaterialType::get_adhesion_coefficient(filament_types[extruder - 1], adhesion_coefficient);
    return adhesion_coefficient;
}

// Orca Model::findMaxSpeed: the highest of the object's print speeds (250 mm/s if none).
static double find_max_speed(const PrintObject &object)
{
    double max_speed = -1.;
    for (const LayerRegion *layerm : object.layers().front()->regions()) {
        const PrintRegion &region             = layerm->region();
        const double       perimeter_speed    = region.extruder_config_value<double>("perimeter_speed", frPerimeter);
        const double       external_speed     = region.extruder_config_value<Domain::FloatOrPercentage>("external_perimeter_speed", frExternalPerimeter).get_abs_value(perimeter_speed);
        const double       infill_speed       = region.extruder_config_value<double>("infill_speed", frInfill);
        const double       solid_infill_speed = region.extruder_config_value<Domain::FloatOrPercentage>("solid_infill_speed", frSolidInfill).get_abs_value(infill_speed);
        const double       top_speed          = region.extruder_config_value<Domain::FloatOrPercentage>("top_solid_infill_speed", frTopSolidInfill).get_abs_value(solid_infill_speed);
        const double       small_speed        = region.extruder_config_value<Domain::FloatOrPercentage>("small_perimeter_speed", frExternalPerimeter).get_abs_value(external_speed);
        max_speed = std::max({ max_speed, perimeter_speed, external_speed, infill_speed, solid_infill_speed, top_speed, small_speed });
    }
    const double support_speed = object.config().get<double>("support_material_speed");
    max_speed = std::max({ max_speed, support_speed,
                           object.config().get<Domain::FloatOrPercentage>("small_support_perimeter_speed").get_abs_value(support_speed) });
    return max_speed <= 0 ? 250. : max_speed;
}

// Orca Model::getThermalLength: the shortest thermal length of the materials of the model parts.
static double get_thermal_length(const Print &print, const std::vector<const Domain::ModelVolume*> &volumes)
{
    const std::vector<std::string> filament_types = print.config().get<std::vector<std::string>>("filament_type");
    double thermal_length = 200.;
    for (const Domain::ModelVolume *volume : volumes)
        if (volume->is_model_part()) {
            double volume_thermal_length = 200.;
            if (const int extruder = volume->extruder_id(); extruder >= 1 && size_t(extruder) <= filament_types.size())
                MaterialType::get_thermal_length(filament_types[extruder - 1], volume_thermal_length);
            thermal_length = std::min(thermal_length, volume_thermal_length);
        }
    return thermal_length;
}

// Orca compSecondMoment (BBS): the second moment of area of a polygon about the origin.
static bool second_moment(Polygon poly, Vec2d &sm)
{
    Algorithms::Polygon::make_counter_clockwise(poly);
    sm = Vec2d(0., 0.);
    if (poly.points.size() < 3)
        return false;
    Vec2d p1 = poly.points.back().cast<double>();
    for (const Point &p : poly.points) {
        const Vec2d  p2 = p.cast<double>();
        const double a  = cross2(p1, p2);
        sm += Vec2d((p1.y() * p1.y() + p1.y() * p2.y() + p2.y() * p2.y()), (p1.x() * p1.x() + p1.x() * p2.x() + p2.x() * p2.x())) * a / 12;
        p1 = p2;
    }
    return true;
}

// The area, centroid and second moment of area about the centroid of an expolygon.
struct ExPolyProp {
    double area = 0;
    Vec2d  centroid;
    Vec2d  second_moment;
};
static bool second_moment(const ExPolygon &expoly, ExPolyProp &prop)
{
    double area = expoly.contour.area();
    Vec2d  cent = expoly.contour.centroid().cast<double>() * area;
    Vec2d  sm;
    if (! second_moment(expoly.contour, sm))
        return false;
    for (const Polygon &hole : expoly.holes) {
        const double a = hole.area();
        area += hole.area();
        cent += hole.centroid().cast<double>() * a;
        Vec2d smh;
        if (second_moment(hole, smh))
            sm += -smh;
    }
    cent = cent / area;
    sm   = sm - Vec2d(cent.y() * cent.y(), cent.x() * cent.x()) * area;
    prop.area          = area;
    prop.centroid      = cent;
    prop.second_moment = sm;
    return true;
}

// The second moment of area of expolygons about their common centroid.
static bool second_moment(const ExPolygons &expolys, double &sm_x, double &sm_y)
{
    if (expolys.empty())
        return false;
    std::vector<ExPolyProp> props;
    for (const ExPolygon &expoly : expolys)
        if (ExPolyProp prop; second_moment(expoly, prop))
            props.push_back(prop);
    if (props.empty())
        return false;
    double total_area = 0.;
    Vec2d  static_moment(0., 0.);
    for (const ExPolyProp &prop : props) {
        total_area += prop.area;
        static_moment += prop.centroid * prop.area;
    }
    const double total_centroid_x = static_moment.x() / total_area;
    const double total_centroid_y = static_moment.y() / total_area;
    sm_x = 0;
    sm_y = 0;
    for (const ExPolyProp &prop : props) {
        const double delta_x = prop.centroid.x() - total_centroid_x;
        const double delta_y = prop.centroid.y() - total_centroid_y;
        sm_x += prop.second_moment.x() + prop.area * delta_y * delta_y;
        sm_y += prop.second_moment.y() + prop.area * delta_x * delta_x;
    }
    return true;
}

// Orca configBrimWidthByVolumeGroups (BBS): the auto brim width of a group of model parts, by the adhesion, the speed,
// the group's height, its second moment of area and its thermal length; none below 5 mm, at most 18 mm.
static double brim_width_by_volume_group(const PrintObject &object, double adhesion, double max_speed,
                                         const std::vector<const Domain::ModelVolume*> &volumes, const ExPolygons &expolys, double &group_height)
{
    Domain::BoundingBox3d merged_bbox;
    for (const Domain::ModelVolume *volume : volumes)
        if (volume->is_model_part()) {
            const Domain::ModelObject &model_object = *object.model_object();
            const Transform3d t = model_object.instances.empty() ? volume->get_matrix() : Transform3d(model_object.instances.front()->get_matrix() * volume->get_matrix());
            merged_bbox = BB::merge(merged_bbox, Algorithms::ModelVolume::transformed_bounding_box(*volume, t));
        }
    const double height = BB::sizes(merged_bbox)(2);
    group_height = height;
    // the second moment of the first layer of the group
    double ixx = -1.e30, iyy = -1.e30;
    if (! expolys.empty() && ! second_moment(expolys, ixx, iyy))
        ixx = iyy = -1.e30;
    ixx = ixx * SCALING_FACTOR * SCALING_FACTOR * SCALING_FACTOR * SCALING_FACTOR;
    iyy = iyy * SCALING_FACTOR * SCALING_FACTOR * SCALING_FACTOR * SCALING_FACTOR;

    // the bounding box of the first layer of the group
    BoundingBox bbox2;
    for (const ExPolygon &expoly : expolys)
        bbox2 = BB::merge(bbox2, get_extents(expoly.contour));
    const Point  size2 = BB::sizes(bbox2);
    const double thermal_length     = std::sqrt(double(size2.x()) * size2.x() + double(size2.y()) * size2.y()) * SCALING_FACTOR;
    const double thermal_length_ref = get_thermal_length(*object.print(), volumes);

    const double height_to_area = std::max(height / ixx * (size2.y() * SCALING_FACTOR), height / iyy * (size2.x() * SCALING_FACTOR)) * height / 1920;
    double brim_width = adhesion * std::min(std::min(std::max(height_to_area * max_speed, thermal_length * 8. / thermal_length_ref * std::min(height, 30.) / 30.), 18.), 1.5 * thermal_length);
    // small brims are omitted
    if (brim_width < 5 && brim_width < 1.5 * thermal_length)
        brim_width = 0;
    // large brims are omitted
    if (brim_width > 18)
        brim_width = 18.;
    return brim_width;
}

// Orca outer_inner_brim_area (BBS): the brim areas per object instance (outer / inner / ears), not over the objects,
// their supports, the wipe tower and outside of the bed, each touching its object or another brim. Returns the area
// of all of them.
static ExPolygons outer_inner_brim_area(const Print &print, const float no_brim_offset, std::map<ObjectInstanceID, ExPolygons> &brim_area_map)
{
    const Flow flow = print.brim_flow();

    ExPolygons brim_area;
    ExPolygons no_brim_area;
    Polygons   holes;
    ExPolygons object_islands;
    for (const PrintObject *object : print.objects()) {
        const Domain::BrimType brim_type        = object->config().get<Domain::BrimType>("brim_type");
        const bool             has_brim_auto    = brim_type == Domain::BrimType::AutoBrim;
        const float            brim_offset      = scale_(object->config().get<double>("brim_separation"));
        const double           flow_width       = flow.scaled_spacing() * SCALING_FACTOR;
        float                  brim_width       = scale_(floor(object->config().get<double>("brim_width") / flow_width / 2) * flow_width * 2);
        const float            scaled_additional_brim_width = scale_(floor(5 / flow_width / 2) * flow_width * 2);
        const float            scaled_half_min_adh_length   = scale_(1.1);
        const float            scaled_flow_width = flow.scaled_spacing();
        const bool             use_auto_brim_ears  = brim_type == Domain::BrimType::BrimEars;
        const bool             use_inner_brim_ears = use_auto_brim_ears && ! object->config().get<bool>("brim_ears_outer_only");
        const bool             has_inner_brim = brim_type == Domain::BrimType::InnerOnly || brim_type == Domain::BrimType::OuterAndInner || use_inner_brim_ears;
        const bool             has_outer_brim = brim_type == Domain::BrimType::OuterOnly || brim_type == Domain::BrimType::OuterAndInner || has_brim_auto || use_auto_brim_ears;
        const coord_t          ear_detection_length = scale_(object->config().get<double>("brim_ears_detection_length"));
        const double           brim_ears_max_angle  = object->config().get<double>("brim_ears_max_angle");
        const ExPolygons       brim_slices = use_brim_efc_outline(*object) ? get_print_object_bottom_layer_expolygons(*object) : object->layers().front()->lslices;

        ExPolygons brim_area_object;
        ExPolygons no_brim_area_object;
        ExPolygons no_brim_area_support;
        Polygons   holes_object;
        Polygons   holes_support;
        // the brims by the groups of connected model parts
        const double adhesion  = has_brim_auto ? get_adhesion_coefficient(*object) : 1.;
        const double max_speed = has_brim_auto ? find_max_speed(*object) : 0.;
        for (const PrintObject::VolumeGroupSlices &volume_group : object->first_layer_volume_groups()) {
            std::vector<const Domain::ModelVolume*> group_volumes;
            for (const Domain::ObjectID volume_id : volume_group.volume_ids)
                for (const Domain::ModelVolume *volume : object->model_object()->volumes)
                    if (volume->id() == volume_id) {
                        group_volumes.push_back(volume);
                        break;
                    }
            if (group_volumes.empty())
                continue;
            double group_height = 0.;
            if (has_brim_auto) {
                const double brim_width_raw = brim_width_by_volume_group(*object, adhesion, max_speed, group_volumes, volume_group.slices, group_height);
                brim_width = scale_(floor(brim_width_raw / flow_width / 2) * flow_width * 2);
            }
            // with the EFC outline, the group's slices within the compensated first layer
            const ExPolygons group_slices = use_brim_efc_outline(*object) ? intersection_ex(brim_slices, volume_group.slices) : volume_group.slices;
            for (const ExPolygon &ex_poly : group_slices) {
                // an additional brim width if the part's adhesion area is too small and no brim is generated
                float brim_width_mod;
                if (brim_width < scale_(5.) && has_brim_auto && group_height > 10.)
                    brim_width_mod = Algorithms::ExPolygon::area(ex_poly) / ex_poly.contour.length() < scaled_half_min_adh_length && brim_width < scaled_flow_width ?
                        brim_width + scaled_additional_brim_width : brim_width;
                else
                    brim_width_mod = brim_width;
                // limited to 1.5 times the bounding box size of a single polygon
                if (has_brim_auto) {
                    const Point size = BB::sizes(Algorithms::Polygon::get_bounding_box(ex_poly.contour));
                    brim_width_mod = std::min(brim_width_mod, float(std::max(size.x(), size.y())));
                }
                // a whole number of loops
                brim_width_mod = floor(brim_width_mod / scaled_flow_width / 2) * scaled_flow_width * 2;

                Polygons ex_poly_holes_reversed = ex_poly.holes;
                Algorithms::Polygon::reverse(ex_poly_holes_reversed);

                if (has_outer_brim) {
                    // the inner and outer boundaries offset from the same polygon, against round off errors
                    const ExPolygons inner_expoly = offset_ex(Polygons{ ex_poly.contour }, brim_offset, ClipperLib::jtRound, scaled_resolution);
                    ExPolygons       outer_expoly;
                    if (use_auto_brim_ears) {
                        const coord_t size_ear = coord_t(brim_width_mod - brim_offset - flow.scaled_spacing());
                        outer_expoly = make_brim_ears_auto(inner_expoly, size_ear, ear_detection_length, brim_ears_max_angle, true);
                    } else
                        outer_expoly = offset_ex(inner_expoly, brim_width_mod, ClipperLib::jtRound, scaled_resolution);
                    append(brim_area_object, diff_ex(outer_expoly, inner_expoly));
                }
                if (has_inner_brim) {
                    const ExPolygons inner_expoly = offset_ex(ex_poly_holes_reversed, -brim_width - brim_offset);
                    ExPolygons       outer_expoly;
                    if (use_auto_brim_ears) {
                        const coord_t size_ear = coord_t(brim_width - brim_offset - flow.scaled_spacing());
                        outer_expoly = make_brim_ears_auto(offset_ex(ex_poly_holes_reversed, -brim_offset), size_ear, ear_detection_length, brim_ears_max_angle, false);
                    } else
                        outer_expoly = offset_ex(ex_poly_holes_reversed, -brim_offset);
                    append(brim_area_object, intersection_ex(diff_ex(outer_expoly, inner_expoly), ex_poly_holes_reversed));
                }
                // brims apart from the holes / the contour without them
                if (! has_inner_brim)
                    append(no_brim_area_object, diff_ex(ex_poly_holes_reversed, offset_ex(ex_poly_holes_reversed, -no_brim_offset)));
                if (! has_outer_brim)
                    append(no_brim_area_object, diff_ex(offset(ex_poly.contour, no_brim_offset), ex_poly_holes_reversed));
                append(holes_object, ex_poly_holes_reversed);
            }
        }
        const ExPolygons object_island = offset_ex(brim_slices, brim_offset, ClipperLib::jtRound, scaled_resolution);
        append(no_brim_area_object, object_island);

        for (size_t instance_idx = 0; instance_idx < object->instances().size(); ++instance_idx) {
            const PrintInstance &instance = object->instances()[instance_idx];
            if (! brim_area_object.empty())
                append_and_translate(brim_area_object, instance, instance_idx, brim_area_map);
            append_and_translate(no_brim_area, no_brim_area_object, instance);
            append_and_translate(holes, holes_object, instance);
            append_and_translate(object_islands, object_island, instance);
        }

        // no brim over the support
        if (! object->support_layers().empty())
            for (const Polygon &support_contour : object->support_layers().front()->support_fills.polygons_covered_by_spacing())
                no_brim_area_support.emplace_back(support_contour);
        for (const PrintInstance &instance : object->instances()) {
            append_and_translate(no_brim_area, no_brim_area_support, instance);
            append_and_translate(holes, holes_support, instance);
        }
    }

    // no brim over the wipe tower
    if (const Points wipe_tower = print.first_layer_wipe_tower_corners(); wipe_tower.size() >= 3)
        no_brim_area.emplace_back(Geometry::convex_hull(wipe_tower));

    // no brim outside of the bed
    {
        Polygon bed;
        for (const Vec2d &pt : print.config().get<std::vector<Vec2d>>("bed_shape"))
            bed.points.emplace_back(scaled(pt));
        if (bed.size() >= 3)
            if (const ExPolygons outside = diff_ex(offset(bed, scale_(30.), ClipperLib::jtRound, scaled_resolution), Polygons{ bed }); ! outside.empty())
                no_brim_area.push_back(outside.front());
    }
    for (auto &[key, areas] : brim_area_map)
        areas = diff_ex(areas, no_brim_area);

    // a brim touches at least an island of its object or a brim area
    for (auto map_it = brim_area_map.begin(); map_it != brim_area_map.end(); ++map_it) {
        ExPolygons other_expolys;
        for (const auto &[other_key, other_areas] : brim_area_map)
            if (other_key != map_it->first)
                append(other_expolys, other_areas);

        const ExPolygons temp_area = std::move(map_it->second);
        map_it->second.clear();
        for (size_t ia = 0; ia != temp_area.size(); ++ia) {
            // this object's other brim areas
            ExPolygons other_expoly;
            for (size_t iao = 0; iao != temp_area.size(); ++iao)
                if (iao != ia)
                    other_expoly.push_back(temp_area[iao]);

            const ExPolygons offseted_ta = offset_ex(temp_area[ia], flow.scaled_spacing() * 2, ClipperLib::jtRound, scaled_resolution);
            if (! intersection_ex(offseted_ta, object_islands).empty() || ! intersection_ex(offseted_ta, other_expoly).empty() ||
                ! intersection_ex(offseted_ta, other_expolys).empty())
                map_it->second.push_back(temp_area[ia]);
        }
        append(brim_area, map_it->second);
    }
    return brim_area;
}

// Flip orientation of open polylines to minimize travel distance.
static void optimize_polylines_by_reversing(Polylines *polylines)
{
    for (size_t poly_idx = 1; poly_idx < polylines->size(); ++poly_idx) {
        const Polyline &prev = (*polylines)[poly_idx - 1];
        Polyline &      next = (*polylines)[poly_idx];

        if (!next.is_closed()) {
            double dist_to_start = (next.first_point() - prev.last_point()).cast<double>().norm();
            double dist_to_end   = (next.last_point() - prev.last_point()).cast<double>().norm();

            if (dist_to_end < dist_to_start)
                next.reverse();
        }
    }
}

static Polylines connect_brim_lines(Polylines &&polylines, const Polygons &brim_area, float max_connection_length)
{
    if (polylines.empty())
        return {};

    BoundingBox bbox = get_extents(polylines);
    bbox = BB::merge(bbox, get_extents(brim_area));

    EdgeGrid::Grid grid(BB::inflated(bbox, SCALED_EPSILON));
    grid.create(brim_area, polylines, coord_t(scale_(10.)));

    struct Visitor
    {
        explicit Visitor(const EdgeGrid::Grid &grid) : grid(grid) {}

        bool operator()(coord_t iy, coord_t ix)
        {
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            this->intersect      = false;
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, brim_line.a, brim_line.b)) {
                    this->intersect = true;
                    return false;
                }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid &grid;
        Line                  brim_line;
        bool                  intersect = false;

    } visitor(grid);

    // Connect successive polylines if they are open, their ends are closer than max_connection_length.
    // Remove empty polylines.
    {
        // Skip initial empty lines.
        size_t poly_idx = 0;
        for (; poly_idx < polylines.size() && polylines[poly_idx].empty(); ++ poly_idx) ;
        size_t end = ++ poly_idx;
        double max_connection_length2 = Slic3r::sqr(max_connection_length);
        for (; poly_idx < polylines.size(); ++poly_idx) {
            Polyline &next = polylines[poly_idx];
            if (! next.empty()) {
                Polyline &prev = polylines[end - 1];
                bool   connect = false;
                if (! prev.is_closed() && ! next.is_closed()) {
                    double dist2 = (prev.last_point() - next.first_point()).cast<double>().squaredNorm();
                    if (dist2 <= max_connection_length2) {
                        visitor.brim_line.a = prev.last_point();
                        visitor.brim_line.b = next.first_point();
                        // Shrink the connection line to avoid collisions with the brim centerlines.
                        visitor.brim_line.extend(-SCALED_EPSILON);
                        grid.visit_cells_intersecting_line(visitor.brim_line.a, visitor.brim_line.b, visitor);
                        connect = ! visitor.intersect;
                    }
                }
                if (connect) {
                    append(prev.points, std::move(next.points));
                } else {
                    if (end < poly_idx)
                        polylines[end] = std::move(next);
                    ++ end;
                }
            }
        }
        if (end < polylines.size())
            polylines.erase(polylines.begin() + int(end), polylines.end());
    }

    return std::move(polylines);
}

// Orca tryExPolygonOffset (BBS): the loops of a brim area, from its outline inwards.
static Polygons brim_area_loops(const ExPolygons &brim_area, const Print &print)
{
    const Flow flow = print.brim_flow();
    Polygons   loops;
    ExPolygons islands_ex = brim_area;
    for (ExPolygon &poly_ex : islands_ex)
        Algorithms::DouglasPeucker::douglas_peucker(poly_ex, scaled_resolution);
    islands_ex = offset_ex(islands_ex, -0.5f * float(flow.scaled_spacing()), ClipperLib::jtRound, scaled_resolution);
    while (! islands_ex.empty()) {
        for (ExPolygon &poly_ex : islands_ex)
            Algorithms::DouglasPeucker::douglas_peucker(poly_ex, scaled_resolution);
        append(loops, Algorithms::ExPolygon::to_polygons(islands_ex));
        islands_ex = offset_ex(islands_ex, -1.3f * float(flow.scaled_spacing()), ClipperLib::jtRound, scaled_resolution);
        for (ExPolygon &poly_ex : islands_ex)
            Algorithms::DouglasPeucker::douglas_peucker(poly_ex, scaled_resolution);
        islands_ex = offset_ex(islands_ex, 0.3f * float(flow.scaled_spacing()), ClipperLib::jtRound, scaled_resolution);
    }
    return loops;
}

ExtrusionEntityCollection make_brim_infill(const ExPolygons &brim_area, const Print &print)
{
    const Flow flow  = print.brim_flow();
    Polygons   loops = union_pt_chained_outside_in(brim_area_loops(brim_area, print));

    std::vector<Polylines> loops_pl_by_levels;
    {
        Polylines loops_pl = Algorithms::Polygon::to_polylines(loops);
        loops_pl_by_levels.assign(loops_pl.size(), Polylines());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, loops_pl.size()),
            [&loops_pl_by_levels, &loops_pl](const tbb::blocked_range<size_t> &range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                    loops_pl_by_levels[i] = chain_polylines({ std::move(loops_pl[i]) });
            });
    }

    // Reduce down to the ordered list of polylines.
    Polylines all_loops;
    for (Polylines &polylines : loops_pl_by_levels)
        append(all_loops, std::move(polylines));
    loops_pl_by_levels.clear();

    // Flip orientation of open polylines to minimize travel distance.
    optimize_polylines_by_reversing(&all_loops);
    all_loops = connect_brim_lines(std::move(all_loops), offset(brim_area, float(SCALED_EPSILON)), float(flow.scaled_spacing()) * 2.f);

    ExtrusionEntityCollection brim;
    extrusion_entities_append_loops_and_paths(brim.entities, std::move(all_loops),
        ExtrusionAttributes{ ExtrusionRole::Brim,
            ExtrusionFlow{ float(flow.mm3_per_mm()), float(flow.width()), float(print.skirt_first_layer_height()) } });
    return brim;
}

ExtrusionEntityCollection make_brim(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area,
                                    std::map<ObjectInstanceID, ExtrusionEntityCollection> &brim_by_instance,
                                    std::map<ObjectInstanceID, ExPolygons> &brim_areas_by_instance)
{
    std::map<ObjectInstanceID, ExPolygons> brim_area_map;
    const Flow       flow            = print.brim_flow();
    const ExPolygons islands_area_ex = outer_inner_brim_area(print, float(flow.scaled_spacing()), brim_area_map);

    // Orca combine_brims: unless combined (later, by the skirt / brim groups), the brims do not overlap: each takes
    // what the ones before it left
    if (! print.config().get<bool>("combine_brims")) {
        ExPolygons claimed_area;
        for (auto &[_, areas] : brim_area_map) {
            areas = diff_ex(areas, claimed_area);
            append(claimed_area, areas);
        }
    }

    islands_area           = Algorithms::ExPolygon::to_polygons(islands_area_ex);
    brim_areas_by_instance = brim_area_map;

    ExtrusionEntityCollection brims;
    for (auto &[key, areas] : brim_area_map)
        if (! areas.empty()) {
            try_cancel();
            ExtrusionEntityCollection brim = make_brim_infill(areas, print);
            brims.append(brim.entities);
            brim_by_instance.emplace(key, std::move(brim));
        }
    return brims;
}

} // namespace Slic3r
