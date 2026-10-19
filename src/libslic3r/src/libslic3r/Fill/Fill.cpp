#include <oneapi/tbb/scalable_allocator.h>
#include <boost/container/vector.hpp>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <cstdlib>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/InfillPatternTraits.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Surface.hpp"
// for Arachne based infills
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Fill/FillBase.hpp"
#include "libslic3r/Fill/FillRectilinear.hpp"
#include "libslic3r/Fill/FillLightning.hpp"
#include "libslic3r/Fill/FillEnsuring.hpp"
#include "Slic3r/Biz/Algorithms/AABBTreeLines.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/ShortestPath.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "libslic3r/HwConfigUtils.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {
namespace BB = Biz::Algorithms::BoundingBox;
namespace AABBTreeLines = Biz::Algorithms::AABBTreeLines;

namespace FillAdaptive {
struct Octree;
}  // namespace FillAdaptive
namespace FillLightning {
class Generator;
}  // namespace FillLightning

//static constexpr const float NarrowInfillAreaThresholdMM = 3.f;

// Calculate infill rotation angle (in radians) for a given layer from a rotation template.
// Grammar subset handled (rotation only):
//   [±]α[*Z or !][joint][-][N|B|T][length][* or !]
//   [±]α*                    sets an initial angle only (no layer processed)
// Where:
// - α: angle in degrees. Without a sign it's absolute; with +/− it's relative. α% means a percentage of 360°.
// - Runtime: *Z repeats the instruction Z times; bare * is a no-op used for initialization; ! runs once globally and then stops.
// - Solid signs (D,S,O,M,R) are not processed here; if present they are treated as invalid/non-rotation characters.
// - Joint signs (shape of the turn across a range):
//     / linear;
//     N,n vertical sinus (n = lazy/half amplitude);
//     Z,z horizontal sinus (z = lazy/half amplitude);
//     $ arcsin; L quarter circle H→V; l quarter circle V→H;
//     U,u squared; Q,q cubic; ~ random; ^ pseudorandom; | middle step; # vertical step at end.
// - Counting / range length:
//     After the joint (or after α) a count determines duration of the turn:
//       N = layer count, B = bottom_shell_layers, T = top_shell_layers.
//     Prefix '-' flips the joint (swap initial/final orientation).
// - Length modifiers convert the count to a Z range instead of a pure layer count:
//     mm, cm, m, ' (feet), " (inches), # (standard height of N layers), % (percent of model height).
//
// Behavior:
// - The template string is tokenized by commas/whitespace and evaluated cyclically with one or more "ranges" per token.
// - Absolute α resets the accumulated angle at the start of its range; relative α accumulates.
// - *Z and ! control repetition and one-time execution of tokens across layers.
// - If the template contains no metalanguage symbols, it is treated as a simple comma-separated list of angles repeated by modulo.
// - Returns angle in radians for the requested layer_id. 0° aligns with +X; fillers may internally rotate as needed.
static double calculate_infill_rotation_angle(const PrintObject* object,
                                       size_t             layer_id,
                                       const double&      fixed_infill_angle,
                                       const std::string& template_string)
{
    if (template_string.empty()) {
        return deg2rad(fixed_infill_angle);
    }
    // Convert the id to an index. Layer::id() counts the raft layers, object->layers() does not.
    const size_t first_object_layer_id = object->get_layer(0)->id();
    layer_id = layer_id > first_object_layer_id ? layer_id - first_object_layer_id : 0;
    double             angle = 0.0;
    const std::string  search_string = "/NnZz$LlUuQq~^|#";
    if (regex_search(template_string, std::regex("[+\\-%*@\'\"cm" + search_string + "]"))) { // template metalanguage of rotating infill
        std::regex                 del("[\\s,]+");
        std::sregex_token_iterator it(template_string.begin(), template_string.end(), del, -1);
        std::vector<std::string>   tk;
        std::sregex_token_iterator end;
        while (it != end) {
            tk.push_back(*it++);
        }
        int    t            = 0;
        int    repeats      = 0;
        double angle_add    = 0;
        double angle_steps  = 1;
        double angle_start  = 0;
        double limit_fill_z = object->get_layer(0)->bottom_z();
        double start_fill_z = limit_fill_z;
        // The raft height, or 0 without a raft.
        const double print_z_offset = object->slicing_parameters().object_print_z_min;
        bool   _noop        = false;
        auto              fill_form = std::string::npos;
        bool              _absolute = false;
        bool              _negative = false;
        std::vector<bool> stop(tk.size(), false);

        for (int i = 0; i <= layer_id; i++) {
            double fill_z = object->get_layer(i)->bottom_z();

            // slice_z is measured from the bottom of the model, limit_fill_z from the build plate.
            if (limit_fill_z < object->get_layer(i)->slice_z + print_z_offset) {
                if (repeats) { // if repeats >0 then restore parameters for new iteration
                    limit_fill_z += limit_fill_z - start_fill_z;
                    start_fill_z = fill_z;
                    repeats--;
                } else {
                    start_fill_z = fill_z;
                    limit_fill_z = object->get_layer(i)->print_z;
                    // Solid handling removed: this function only computes rotation.
                    fill_form    = std::string::npos;
                    do {
                        if (!stop[t]) {
                            _noop     = false;
                            _absolute = false;
                            _negative = false;
                            angle_start += angle_add;
                            angle_add   = 0;
                            angle_steps = 1;
                            repeats     = 1;
                            if (tk[t].find('!') != std::string::npos) // this is an one-time instruction
                                stop[t] = true;

                            char* cs = &tk[t][0];

                            if ((cs[0] >= '0' && cs[0] <= '9') && !(cs[0] == '+' || cs[0] == '-')) // absolute/relative
                                _absolute = true;

                            angle_add = strtod(cs, &cs); // read angle parameter

                            if (cs[0] == '%') { // percentage of angles
                                angle_add *= 3.6;
                                cs = &cs[1];
                            }

                            int tit = tk[t].find('*');
                            if (tit != std::string::npos) // overall angle_cycles
                                repeats = strtol(&tk[t][tit + 1], &cs, 0);

                            if (repeats) {                                // run if overall cycles greater than 0
                                // Solid signs (D,S,O,M,R) are not handled here; if present they behave as invalid characters.

                                if (cs[0] == 'B') {
                                    angle_steps = object->get_layer(0)->regions().front()->region().extruder_config_value<int>("bottom_solid_layers", frSolidInfill);
                                } else if (cs[0] == 'T') {
                                    angle_steps = object->get_layer(0)->regions().front()->region().extruder_config_value<int>("top_solid_layers", frSolidInfill);
                                } else {
                                    fill_form = search_string.find(cs[0]);
                                    if (fill_form != std::string::npos)
                                        cs = &cs[1];

                                    _negative   = (cs[0] == '-'); // negative parameter
                                    angle_steps = abs(strtod(cs, &cs));

                                    if (angle_steps && cs[0] != '\0' && cs[0] != '!') {
                                        if (cs[0] == '%') // value in the percents of fill_z
                                            limit_fill_z = angle_steps * object->height() * 1e-8;
                                        else if (cs[0] == '#') // value in the feet
                                            limit_fill_z = angle_steps * object->config().get<double>("layer_height");
                                        else if (cs[0] == '\'') // value in the feet
                                            limit_fill_z = angle_steps * 12 * 25.4;
                                        else if (cs[0] == '\"') // value in the inches
                                            limit_fill_z = angle_steps * 25.4;
                                        else if (cs[0] == 'c') // value in centimeters
                                            limit_fill_z = angle_steps * 10.;
                                        else if (cs[0] == 'm') {
                                            if (cs[1] == 'm') { // value in the millimeters
                                                limit_fill_z = angle_steps * 1.;
                                            } else{
                                                limit_fill_z = angle_steps * 1000.;
                                            }
                                        }
                                        limit_fill_z += fill_z;
                                        angle_steps = 0; // limit_fill_z has already count
                                    }
                                }
                                if (angle_steps) { // if limit_fill_z does not setting by lenght method. Get count the layer id above model height
                                    if (fill_form == std::string::npos && !_absolute)
                                        angle_add *= (int) angle_steps;
                                    int idx      = i + std::max(angle_steps - 1, 0.);
                                    int sdx      = std::max(0, idx - (int) object->layers().size());
                                    idx          = std::min(idx, (int) object->layers().size() - 1);
                                    limit_fill_z = object->get_layer(idx)->print_z + sdx * object->config().get<double>("layer_height");
                                }
                                repeats = std::max(repeats - 1, 0);
                            } else
                                _noop = true; // set the dumb cycle
                            if (_absolute) {  // is absolute
                                angle_start = angle_add;
                                angle_add   = 0;
                            }
                        }
                        if (++t >= tk.size())
                            t = 0;
                    } while (std::all_of(stop.begin(), stop.end(), [](bool v) { return v; }) ?
                                 false :
                                 (t ? _noop : false) || stop[t]); // if this is a dumb instruction which never reaprated twice
                }
            }
            double top_z    = object->get_layer(i)->print_z;
            double negvalue = (_negative ? limit_fill_z - top_z : top_z - start_fill_z) / (limit_fill_z - start_fill_z);

            switch (fill_form) {
            case 0: break;                                                  // /-joint, linear
            case 1: negvalue -= sin(negvalue * PI * 2.) / (PI * 2.); break; // N-joint, sinus, vertical start
            case 2: negvalue -= sin(negvalue * PI * 2.) / (PI * 4.); break; // n-joint, sinus, vertical start, lazy
            case 3: negvalue += sin(negvalue * PI * 2.) / (PI * 2.); break; // Z-joint, sinus, horizontal start
            case 4: negvalue += sin(negvalue * PI * 2.) / (PI * 4.); break; // z-joint, sinus, horizontal start, lazy
            case 5: negvalue = asin(negvalue * 2. - 1.) / PI + 0.5; break;  // $-joint, arcsin
            case 6: negvalue = sin(negvalue * PI / 2.); break;              // L-joint, quarter of circle, horizontal start
            case 7: negvalue = 1. - cos(negvalue * PI / 2.); break;         // l-joint, quarter of circle, vertical start
            case 8: negvalue = 1. - pow(1. - negvalue, 2); break;           // U-joint, squared, x2
            case 9: negvalue = pow(1 - negvalue, 2); break;                 // u-joint, squared, x2 inverse
            case 10: negvalue = 1. - pow(1. - negvalue, 3); break;          // Q-joint, cubic, x3
            case 11: negvalue = pow(1. - negvalue, 3); break;               // q-joint, cubic, x3 inverse
            case 12: negvalue = (double) rand() / RAND_MAX; break;          // ~-joint, random, fill the whole angle
            case 13: negvalue += (double) rand() / RAND_MAX - 0.5; break;   // ^-joint, pseudorandom, disperse at middle line
            case 14: negvalue = 0.5; break;                                 // |-joint, like #-joint but placed at middle angle
            case 15: negvalue = _negative ? 0. : 1.; break;                 // #-joint, vertical at the end angle
            }
            angle = deg2rad(angle_start + angle_add * negvalue);
        }
    } else {
        // a list of angles, by the layer
        std::vector<double> rotate_angles;
        static const std::regex separators("[\\s,]+");
        for (std::sregex_token_iterator it(template_string.begin(), template_string.end(), separators, -1), end; it != end; ++it)
            if (! it->str().empty())
                rotate_angles.push_back(std::stod(it->str()));
        if (! rotate_angles.empty())
            angle = deg2rad(rotate_angles[layer_id % rotate_angles.size()]);
    }
    return angle;
}

// Orca align_infill_direction_to_model: the rotation of the model on the plate
static double model_rotation(const PrintObject &object)
{
    const auto &m = object.trafo().matrix();
    return std::atan2(double(m(1, 0)), double(m(0, 0)));
}

struct SurfaceFillParams
{
	// Zero based extruder ID.
    unsigned int 	extruder = 0;
	// Infill pattern, adjusted for the density etc.
    Domain::InfillPattern  	pattern = Domain::InfillPattern(0);

    // FillBase
    // in unscaled coordinates
    double    	spacing = 0.;
    // infill / perimeter overlap, in unscaled coordinates
//    double    	overlap = 0.;
    // Angle as provided by the region config, in radians.
    float       	angle = 0.f;
    // Orca: a fixed angle (top / bottom layer direction), not alternated per layer
    bool            fixed_angle = false;
    // Orca top_surface_fill_order / bottom_surface_fill_order and center_of_surface_pattern
    Domain::SurfaceFillOrder       fill_order = Domain::SurfaceFillOrder::Default;
    Domain::CenterOfSurfacePattern center_of_surface_pattern = Domain::CenterOfSurfacePattern::EachSurface;
    // Orca fill_multiline / sparse_infill_smooth_factor
    int                            multiline = 1;
    double                         smooth_factor = 0.;
    // Orca infill_complete_top (3D Honeycomb)
    bool                           infill_complete_top = false;
    // Orca lateral_lattice_angle_1 / _2, infill_overhang_angle (Lateral Lattice, Lateral Honeycomb)
    double                         lateral_lattice_angle_1 = 0.;
    double                         lateral_lattice_angle_2 = 0.;
    float                          infill_overhang_angle = 60.f;
    // Orca gyroid_optimized, tpms_adaptive / tpms_interior_density / tpms_adaptive_gradient
    bool                           gyroid_optimized = false;
    Domain::TpmsAdaptiveMode       tpms_adaptive = Domain::TpmsAdaptiveMode::Disabled;
    float                          tpms_interior_density = 0.f;
    Domain::TpmsAdaptiveGradient   tpms_adaptive_gradient = Domain::TpmsAdaptiveGradient::Linear;
    // Orca separated_infills
    bool                           separated_infills = false;
    // Orca Locked Zag (scaled depths), Zig Zag / Cross Zag / Locked Zag symmetric_infill_y_axis
    float                          infill_lock_depth = 0.f;
    float                          skin_infill_depth = 0.f;
    bool                           symmetric_infill_y_axis = false;
    // Is bridging used for this fill? Bridging parameters may be used even if this->flow.bridge() is not set.
    bool 			bridge;
    // Non-negative for a bridge.
    float 			bridge_angle = 0.f;

    // FillParams
    float       	density = 0.f;
    // Don't adjust spacing to fill the space evenly.
//    bool        	dont_adjust = false;
    // Length of the infill anchor along the perimeter line.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float 			anchor_length     = 1000.f;
    float 			anchor_length_max = 1000.f;

    // width, height of extrusion, nozzle diameter, is bridge
    // For the output, for fill generator.
    Flow 			flow;

	// For the output
	ExtrusionRole	extrusion_role{ ExtrusionRole::None };

	// Various print settings?

	// Index of this entry in a linear vector.
    size_t 			idx = 0;


	bool operator<(const SurfaceFillParams &rhs) const {
#define RETURN_COMPARE_NON_EQUAL(KEY) if (this->KEY < rhs.KEY) return true; if (this->KEY > rhs.KEY) return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) if (TYPE(this->KEY) < TYPE(rhs.KEY)) return true; if (TYPE(this->KEY) > TYPE(rhs.KEY)) return false;

		// Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
		if (this->bridge_angle > rhs.bridge_angle) return true; 
		if (this->bridge_angle < rhs.bridge_angle) return false;

		RETURN_COMPARE_NON_EQUAL(extruder);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
		RETURN_COMPARE_NON_EQUAL(spacing);
//		RETURN_COMPARE_NON_EQUAL(overlap);
		RETURN_COMPARE_NON_EQUAL(angle);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, fixed_angle);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, fill_order);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, center_of_surface_pattern);
		RETURN_COMPARE_NON_EQUAL(multiline);
		RETURN_COMPARE_NON_EQUAL(smooth_factor);
		RETURN_COMPARE_NON_EQUAL(infill_complete_top);
		RETURN_COMPARE_NON_EQUAL(lateral_lattice_angle_1);
		RETURN_COMPARE_NON_EQUAL(lateral_lattice_angle_2);
		RETURN_COMPARE_NON_EQUAL(infill_overhang_angle);
		RETURN_COMPARE_NON_EQUAL(gyroid_optimized);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, tpms_adaptive);
		RETURN_COMPARE_NON_EQUAL(tpms_interior_density);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, tpms_adaptive_gradient);
		RETURN_COMPARE_NON_EQUAL(separated_infills);
		RETURN_COMPARE_NON_EQUAL(infill_lock_depth);
		RETURN_COMPARE_NON_EQUAL(skin_infill_depth);
		RETURN_COMPARE_NON_EQUAL(symmetric_infill_y_axis);
		RETURN_COMPARE_NON_EQUAL(density);
//		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);
		RETURN_COMPARE_NON_EQUAL(anchor_length);
		RETURN_COMPARE_NON_EQUAL(anchor_length_max);
		RETURN_COMPARE_NON_EQUAL(flow.width());
		RETURN_COMPARE_NON_EQUAL(flow.height());
		RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter());
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, bridge);
		return this->extrusion_role.lower(rhs.extrusion_role);
	}

	bool operator==(const SurfaceFillParams &rhs) const {
		return  this->extruder 			== rhs.extruder 		&&
				this->pattern 			== rhs.pattern 			&&
				this->spacing 			== rhs.spacing 			&&
//				this->overlap 			== rhs.overlap 			&&
				this->angle   			== rhs.angle   			&&
				this->fill_order		== rhs.fill_order		&&
				this->center_of_surface_pattern == rhs.center_of_surface_pattern &&
				this->multiline == rhs.multiline && this->smooth_factor == rhs.smooth_factor &&
				this->infill_complete_top == rhs.infill_complete_top &&
				this->lateral_lattice_angle_1 == rhs.lateral_lattice_angle_1 && this->lateral_lattice_angle_2 == rhs.lateral_lattice_angle_2 &&
				this->infill_overhang_angle == rhs.infill_overhang_angle &&
				this->gyroid_optimized == rhs.gyroid_optimized && this->tpms_adaptive == rhs.tpms_adaptive &&
				this->tpms_interior_density == rhs.tpms_interior_density && this->tpms_adaptive_gradient == rhs.tpms_adaptive_gradient &&
				this->separated_infills == rhs.separated_infills &&
				this->infill_lock_depth == rhs.infill_lock_depth && this->skin_infill_depth == rhs.skin_infill_depth &&
				this->symmetric_infill_y_axis == rhs.symmetric_infill_y_axis &&
				this->bridge   			== rhs.bridge   		&&
//				this->bridge_angle 		== rhs.bridge_angle		&&
				this->density   		== rhs.density   		&&
//				this->dont_adjust   	== rhs.dont_adjust 		&&
				this->anchor_length  	== rhs.anchor_length    &&
				this->anchor_length_max == rhs.anchor_length_max &&
				this->flow 				== rhs.flow 			&&
				this->extrusion_role	== rhs.extrusion_role;
	}
};

struct SurfaceFill {
	SurfaceFill(const SurfaceFillParams& params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params) {}

	size_t 				region_id;
	Surface 			surface;
	ExPolygons       	expolygons;
	SurfaceFillParams	params;
};

static inline bool fill_type_monotonic(Domain::InfillPattern pattern)
{
	return pattern == Domain::InfillPattern::ipMonotonic || pattern == Domain::InfillPattern::ipMonotonicLines;
}

// Orca split_solid_surface: the narrow parts of an internal solid fill, to be filled concentric. The line patterns are
// split by the sections the anti-vibration filter of FillEnsuring keeps, the other patterns by an opening.
static void split_solid_surface(size_t layer_id, const SurfaceFill &fill, ExPolygons &normal_infill, ExPolygons &narrow_infill)
{
    assert(fill.surface.surface_type == stInternalSolid);
    const coord_t scaled_spacing = scaled<coord_t>(fill.params.spacing);

    const bool line_based_pattern =
        fill.params.pattern == Domain::InfillPattern::ipRectilinear || fill.params.pattern == Domain::InfillPattern::ipMonotonic ||
        fill.params.pattern == Domain::InfillPattern::ipMonotonicLines || fill.params.pattern == Domain::InfillPattern::ipAlignedRectilinear;
    if (! line_based_pattern) {
        // the "core" without the thin features, clamped to the original
        for (const ExPolygon &expolygon : fill.expolygons) {
            const Polygons filled_area = Algorithms::ExPolygon::to_polygons(expolygon);
            const Polygons inner_area  = intersection(filled_area, opening(filled_area, float(scaled_spacing), float(scaled_spacing)));
            if (inner_area.empty()) {
                narrow_infill.emplace_back(expolygon);
                continue;
            }
            const ExPolygons inner_ex = union_ex(inner_area);
            const ExPolygons expolys{ expolygon };
            append(normal_infill, intersection_ex(expolys, inner_ex));
            append(narrow_infill, diff_ex(expolys, inner_ex));
        }
        return;
    }

    Polygons normal_fill_areas;
    constexpr bool connect_extrusions                  = true;
    const double   distance_limit_reconnection         = 2.0 * double(scaled_spacing);
    const double   squared_distance_limit_reconnection = distance_limit_reconnection * distance_limit_reconnection;
    // the infill direction, see Fill::_infill_direction; alternated except for the aligned rectilinear
    double base_angle = fill.params.angle + float(M_PI / 2.);
    if (fill.params.pattern != Domain::InfillPattern::ipAlignedRectilinear && ! fill.params.fixed_angle) {
        const size_t idx = layer_id / std::max<size_t>(1, fill.surface.thickness_layers);
        base_angle += (idx & 1) ? float(M_PI / 2.) : 0;
    }
    const double aligning_angle = -base_angle + PI;

    for (const ExPolygon &expolygon : fill.expolygons) {
        Polygons filled_area = Algorithms::ExPolygon::to_polygons(expolygon);
        Algorithms::Polygon::rotate(filled_area, aligning_angle);
        const BoundingBox bb = get_extents(filled_area);

        Polygons inner_area = intersection(filled_area, opening(filled_area, float(2 * scaled_spacing), float(3 * scaled_spacing)));
        inner_area          = shrink(inner_area, float(scaled_spacing * 0.5));

        AABBTreeLines::LinesDistancer<Line> area_walls{ Algorithms::Polygon::to_lines(inner_area) };

        const size_t  n_vlines = (bb.max.x() - bb.min.x() + scaled_spacing - 1) / scaled_spacing;
        const coord_t y_min    = bb.min.y();
        const coord_t y_max    = bb.max.y();
        Lines         vertical_lines(n_vlines);
        for (size_t i = 0; i < n_vlines; i++) {
            const coord_t x     = bb.min.x() + i * double(scaled_spacing);
            vertical_lines[i].a = Point{ x, y_min };
            vertical_lines[i].b = Point{ x, y_max };
        }
        if (! vertical_lines.empty()) {
            vertical_lines.push_back(vertical_lines.back());
            vertical_lines.back().a = Point{ coord_t(bb.min.x() + n_vlines * double(scaled_spacing) + scaled_spacing * 0.5), y_min };
            vertical_lines.back().b = Point{ vertical_lines.back().a.x(), y_max };
        }

        std::vector<Lines> polygon_sections(n_vlines);
        for (size_t i = 0; i < n_vlines; i++) {
            const auto intersections = area_walls.intersections_with_line<true>(vertical_lines[i]);
            for (int intersection_idx = 0; intersection_idx < int(intersections.size()) - 1; intersection_idx++) {
                const auto &a = intersections[intersection_idx];
                const auto &b = intersections[intersection_idx + 1];
                if (area_walls.outside((a.first + b.first) / 2) < 0 && std::abs(a.first.y() - b.first.y()) > scaled_spacing)
                    polygon_sections[i].emplace_back(a.first, b.first);
            }
        }

        polygon_sections = filter_vibrating_extrusions(polygon_sections);

        // the area of the kept sections, traced section by section
        Polygons reconstructed_area;
        {
            struct TracedPoly
            {
                Points lows;
                Points highs;
            };

            std::vector<Lines> polygon_sections_w_width = polygon_sections;
            for (Lines &slice : polygon_sections_w_width)
                for (Line &l : slice) {
                    l.a -= Point{ 0.0, 0.5 * scaled_spacing };
                    l.b += Point{ 0.0, 0.5 * scaled_spacing };
                }

            std::vector<TracedPoly> current_traced_polys;
            for (const Lines &polygon_slice : polygon_sections_w_width) {
                std::unordered_set<const Line *> used_segments;
                for (TracedPoly &traced_poly : current_traced_polys) {
                    auto candidates_begin = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.lows.back(),
                                                             [](const Point &low, const Line &seg) { return seg.b.y() > low.y(); });
                    auto candidates_end   = std::upper_bound(polygon_slice.begin(), polygon_slice.end(), traced_poly.highs.back(),
                                                             [](const Point &high, const Line &seg) { return seg.a.y() > high.y(); });

                    bool segment_added = false;
                    for (auto candidate = candidates_begin; candidate != candidates_end && ! segment_added; candidate++) {
                        if (used_segments.find(&(*candidate)) != used_segments.end())
                            continue;
                        if (connect_extrusions && (traced_poly.lows.back() - candidates_begin->a).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.lows.push_back(candidates_begin->a);
                        } else {
                            traced_poly.lows.push_back(traced_poly.lows.back() + Point{ scaled_spacing / 2, coord_t(0) });
                            traced_poly.lows.push_back(candidates_begin->a - Point{ scaled_spacing / 2, 0 });
                            traced_poly.lows.push_back(candidates_begin->a);
                        }
                        if (connect_extrusions && (traced_poly.highs.back() - candidates_begin->b).cast<double>().squaredNorm() <
                                                      squared_distance_limit_reconnection) {
                            traced_poly.highs.push_back(candidates_begin->b);
                        } else {
                            traced_poly.highs.push_back(traced_poly.highs.back() + Point{ scaled_spacing / 2, 0 });
                            traced_poly.highs.push_back(candidates_begin->b - Point{ scaled_spacing / 2, 0 });
                            traced_poly.highs.push_back(candidates_begin->b);
                        }
                        segment_added = true;
                        used_segments.insert(&(*candidates_begin));
                    }

                    if (! segment_added) {
                        // zero or multiple overlapping segments: this polygon is closed, maybe several new ones opened
                        traced_poly.lows.push_back(traced_poly.lows.back() + Point{ scaled_spacing / 2, 0 });
                        traced_poly.highs.push_back(traced_poly.highs.back() + Point{ scaled_spacing / 2, 0 });
                        Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                        new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
                        traced_poly.lows.clear();
                        traced_poly.highs.clear();
                    }
                }

                current_traced_polys.erase(std::remove_if(current_traced_polys.begin(), current_traced_polys.end(),
                                                          [](const TracedPoly &tp) { return tp.lows.empty(); }),
                                           current_traced_polys.end());

                for (const Line &segment : polygon_slice)
                    if (used_segments.find(&segment) == used_segments.end()) {
                        TracedPoly &new_tp = current_traced_polys.emplace_back();
                        new_tp.lows.push_back(segment.a - Point{ scaled_spacing / 2, 0 });
                        new_tp.lows.push_back(segment.a);
                        new_tp.highs.push_back(segment.b - Point{ scaled_spacing / 2, 0 });
                        new_tp.highs.push_back(segment.b);
                    }
            }

            // the polygons not closed
            for (TracedPoly &traced_poly : current_traced_polys) {
                Polygon &new_poly = reconstructed_area.emplace_back(std::move(traced_poly.lows));
                new_poly.points.insert(new_poly.points.end(), traced_poly.highs.rbegin(), traced_poly.highs.rend());
            }
        }
        append(normal_fill_areas, std::move(reconstructed_area));
    }

    Algorithms::Polygon::rotate(normal_fill_areas, -aligning_angle);

    // the split
    ExPolygons normal_fill_areas_ex = union_safety_offset_ex(normal_fill_areas);
    ExPolygons narrow_fill_areas    = diff_ex(fill.expolygons, normal_fill_areas_ex);

    // the areas narrower than a line touching a normal infill are merged into it
    for (auto iter = narrow_fill_areas.begin(); iter != narrow_fill_areas.end();) {
        if (offset_ex(*iter, -float(scaled_spacing * 0.5)).empty()) {
            const ExPolygons expanded = offset_ex(*iter, float(scaled_spacing * 0.3));
            const Polygons   normal_fill_area_clipped = Algorithms::ClipperUtils::clip_clipper_polygons_with_subject_bbox(
                Algorithms::ExPolygon::to_polygons(normal_fill_areas_ex), get_extents(expanded));
            if (! intersection_ex(normal_fill_area_clipped, expanded).empty()) {
                normal_fill_areas_ex.emplace_back(*iter);
                iter = narrow_fill_areas.erase(iter);
                continue;
            }
        }
        iter++;
    }

    if (narrow_fill_areas.empty())
        return;

    // the normal infills expanded back over the half spacing the inner area was shrunk by, without gaps to the narrow ones
    normal_infill = intersection_ex(offset_ex(normal_fill_areas_ex, float(scaled_spacing * 0.5)), fill.expolygons);
    narrow_infill = std::move(narrow_fill_areas);
}

std::vector<SurfaceFill> group_fills(const Layer &layer, LockRegionParam &lock_param)
{
	std::vector<SurfaceFill> surface_fills;
	// Orca: the Locked Zag flows and densities, by the surfaces having them
	auto append_flow_param = [](std::map<Flow, ExPolygons> &flow_params, Flow flow, const ExPolygon &exp) {
        auto it = flow_params.find(flow);
        if (it == flow_params.end())
            flow_params.insert({flow, {exp}});
        else
            it->second.push_back(exp);
    };
	auto append_density_param = [](std::map<float, ExPolygons> &density_params, float density, const ExPolygon &exp) {
        auto it = density_params.find(density);
        if (it == density_params.end())
            density_params.insert({density, {exp}});
        else
            it->second.push_back(exp);
    };

	// Fill in a map of a region & surface to SurfaceFillParams.
	std::set<SurfaceFillParams> 						set_surface_params;
	std::vector<std::vector<const SurfaceFillParams*>> 	region_to_surface_params(layer.regions().size(), std::vector<const SurfaceFillParams*>());
    SurfaceFillParams									params;
    bool 												has_internal_voids = false;
	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion  &layerm = *layer.regions()[region_id];
		region_to_surface_params[region_id].assign(layerm.fill_surfaces().size(), nullptr);
	    for (const Surface &surface : layerm.fill_surfaces())
	        if (surface.surface_type == stInternalVoid)
	        	has_internal_voids = true;
	        else {
		        const PrintRegion &region = layerm.region();
		        const PrintRegionConfigView &region_config = region.config();
		        FlowRole extrusion_role = surface.is_top() ? frTopSolidInfill : (surface.is_solid() ? frSolidInfill : frInfill);
		        bool     is_bridge 	    = layer.id() > 0 && surface.is_bridge();
		        params.extruder 	 = layerm.region().extruder(extrusion_role);
		        params.pattern 		 = layerm.region().extruder_config_value<Domain::InfillPattern>("fill_pattern", extrusion_role);
		        params.density       = float(region.extruder_config_value<Domain::Percentage>("fill_density", FlowRole::frInfill).value);
		        params.lateral_lattice_angle_1 = region_config.get<double>("lateral_lattice_angle_1");
		        // Orca Locked Zag depths, Zig Zag / Cross Zag / Locked Zag mirroring
		        params.infill_lock_depth = params.pattern == Domain::InfillPattern::ipLockedZag ? float(scale_(region_config.get<double>("infill_lock_depth"))) : 0.f;
		        params.skin_infill_depth = params.pattern == Domain::InfillPattern::ipLockedZag ? float(scale_(region_config.get<double>("skin_infill_depth"))) : 0.f;
		        params.symmetric_infill_y_axis = (params.pattern == Domain::InfillPattern::ipZigZag || params.pattern == Domain::InfillPattern::ipCrossZag ||
		                                          params.pattern == Domain::InfillPattern::ipLockedZag) && region_config.get<bool>("symmetric_infill_y_axis");
		        params.lateral_lattice_angle_2 = region_config.get<double>("lateral_lattice_angle_2");
		        params.infill_overhang_angle   = float(region_config.get<double>("infill_overhang_angle"));

		        if (surface.is_solid()) {
		            params.density = 100.f;
					//FIXME for non-thick bridges, shall we allow a bottom surface pattern?
		            if (surface.is_external() && ! is_bridge) {
		                // Orca top_surface_density / bottom_surface_density (a top surface at 0% is left out)
		                params.pattern = surface.is_top() ? region_config.get<Domain::InfillPattern>("top_fill_pattern") : region_config.get<Domain::InfillPattern>("bottom_fill_pattern");
		                params.density = float(region_config.get<Domain::Percentage>(surface.is_top() ? "top_surface_density" : "bottom_surface_density").value);
		                if (params.density <= 0.f)
		                    continue;
		            } else if (! is_bridge && (surface.surface_type == stInternalSolid || surface.surface_type == stSolidOverBridge))
		                // Orca internal_solid_infill_pattern
		                params.pattern = region_config.get<Domain::InfillPattern>("internal_solid_infill_pattern");
		            else
		                params.pattern = fill_type_monotonic(region_config.get<Domain::InfillPattern>("top_fill_pattern")) ? Domain::InfillPattern::ipMonotonic : Domain::InfillPattern::ipRectilinear;
		        } else if (params.density <= 0)
		            continue;

		        if (is_bridge) {
		            // Orca: internal bridges have their own role
		            params.extrusion_role = surface.surface_type == stInternalBridge ? ExtrusionRole::InternalBridgeInfill : ExtrusionRole::BridgeInfill;
                } else {
                    if (surface.is_solid()) {
                        if (surface.is_top()) {
                            params.extrusion_role = ExtrusionRole::TopSolidInfill;
                        } else if (surface.is_bottom()) {
                            // Orca: bottom surfaces have their own role (speed, flow ratio, filament)
                            params.extrusion_role = ExtrusionRole::BottomSurface;
                        } else if (surface.surface_type == stSolidOverBridge) {
                            params.extrusion_role = ExtrusionRole::InfillOverBridge;
                        } else {
                            params.extrusion_role = ExtrusionRole::SolidInfill;
                        }
                    } else {
                        params.extrusion_role = ExtrusionRole::InternalInfill;
                    }
                }
		        // Orca: top / bottom surfaces in their own filament
		        if (const int filament = params.extrusion_role == ExtrusionRole::TopSolidInfill ? region_config.get<int>("top_surface_filament_id") :
		                                 params.extrusion_role == ExtrusionRole::BottomSurface  ? region_config.get<int>("bottom_surface_filament_id") : 0;
		            filament > 0)
		            params.extruder = filament;
		        params.bridge_angle = float(surface.bridge_angle);
		        // Orca: sparse infill at the fill angle, solid infill at solid_infill_direction, top / bottom surfaces
		        // at their fixed direction if set
		        // (Orca sparse_infill_rotate_template / solid_infill_rotate_template: by the layer, not alternated)
		        params.fixed_angle  = false;
		        if (params.extrusion_role == ExtrusionRole::InternalInfill) {
		            const std::string rotate_template = region_config.get<std::string>("sparse_infill_rotate_template");
		            params.angle       = float(calculate_infill_rotation_angle(layer.object(), layer.id(), region_config.get<double>("fill_angle"), rotate_template));
		            params.fixed_angle = ! rotate_template.empty();
		        } else if (const double direction = surface.is_top() ? region_config.get<double>("top_layer_direction") :
		                     surface.is_bottom() ? region_config.get<double>("bottom_layer_direction") : -1.;
		                 direction >= 0.) {
		            params.angle       = float(deg2rad(direction));
		            params.fixed_angle = true;
		        } else {
		            const std::string rotate_template = region_config.get<std::string>("solid_infill_rotate_template");
		            params.angle       = float(calculate_infill_rotation_angle(layer.object(), layer.id(), region_config.get<double>("solid_infill_direction"), rotate_template));
		            params.fixed_angle = ! rotate_template.empty();
		        }
		        // Orca fill_multiline (the sparse infill only) and sparse_infill_smooth_factor (the patterns rounding their corners)
		        params.multiline     = params.extrusion_role == ExtrusionRole::InternalInfill ? region_config.get<int>("fill_multiline") : 1;
		        params.smooth_factor = 0.;
		        if (params.extrusion_role == ExtrusionRole::InternalInfill && is_smoothable_infill_pattern(params.pattern, params.multiline))
		            params.smooth_factor = 0.01 * region_config.get<Domain::Percentage>("sparse_infill_smooth_factor").value;
		        params.infill_complete_top = params.pattern == Domain::InfillPattern::ip3DHoneycomb && region_config.get<bool>("infill_complete_top");
		        // Orca: separated_infills only where it can move the pattern
		        params.separated_infills = region_config.get<bool>("separated_infills") && is_separable_infill_pattern(params.pattern) &&
		                                   ! (surface.is_external() && ! is_bridge);
		        // Orca: only the TPMS sparse infill is graded; reset otherwise, as params is reused.
		        params.tpms_adaptive = is_tpms_adaptive_pattern(params.pattern) && params.extrusion_role == ExtrusionRole::InternalInfill ?
		                                   region_config.get<Domain::TpmsAdaptiveMode>("tpms_adaptive") : Domain::TpmsAdaptiveMode::Disabled;
		        const bool tpms_adaptive      = params.tpms_adaptive != Domain::TpmsAdaptiveMode::Disabled;
		        params.tpms_interior_density  = tpms_adaptive ? std::max(1.f, float(region_config.get<Domain::Percentage>("tpms_interior_density").value)) : 0.f;
		        params.tpms_adaptive_gradient = tpms_adaptive ? region_config.get<Domain::TpmsAdaptiveGradient>("tpms_adaptive_gradient") : Domain::TpmsAdaptiveGradient::Linear;
		        // Orca: gyroid_optimized only for the Gyroid; the adaptive density replaces it.
		        params.gyroid_optimized = params.pattern == Domain::InfillPattern::ipGyroid && region_config.get<bool>("gyroid_optimized") && ! tpms_adaptive;
		        // Orca align_infill_direction_to_model
		        if (region_config.get<bool>("align_infill_direction_to_model"))
		            params.angle += float(model_rotation(*layer.object()));
		        // Orca: the forced fill order only for the top / bottom surfaces with a center based pattern
		        params.center_of_surface_pattern = region_config.get<Domain::CenterOfSurfacePattern>("center_of_surface_pattern");
		        params.fill_order = Domain::SurfaceFillOrder::Default;
		        if (surface.is_external() && ! is_bridge &&
		            (params.pattern == Domain::InfillPattern::ipConcentric || params.pattern == Domain::InfillPattern::ipSpiralInset || params.pattern == Domain::InfillPattern::ipArchimedeanChords ||
		             params.pattern == Domain::InfillPattern::ipOctagramSpiral))
		            params.fill_order = region_config.get<Domain::SurfaceFillOrder>(surface.is_top() ? "top_surface_fill_order" : "bottom_surface_fill_order");

		        // Calculate the actual flow we'll be using for this infill.
		        params.bridge = is_bridge || Fill::use_bridge_flow(params.pattern);
				params.flow   = params.bridge ?
					// Always enable thick bridges for internal bridges.
					// Orca thick_bridges / thick_internal_bridges
					layerm.bridging_flow(extrusion_role, false, surface.is_bridge() &&
						layer.object()->config().get<bool>(surface.surface_type == stInternalBridge ? "thick_internal_bridges" : "thick_bridges")) :
					layerm.flow(extrusion_role, (surface.thickness == -1) ? layer.height : surface.thickness);

				// Calculate flow spacing for infill pattern generation.
		        if (surface.is_solid() || is_bridge) {
		            params.spacing = params.flow.spacing();
		            // Don't limit anchor length for solid or bridging infill.
		            params.anchor_length = 1000.f;
					params.anchor_length_max = 1000.f;
		        } else {
					// Internal infill. Calculating infill line spacing independent of the current layer height and 1st layer status,
					// so that internall infill will be aligned over all layers of the current region.
		            params.spacing = layerm.region().flow(*layer.object(), frInfill, layer.object()->config().get<double>("layer_height"), false).spacing();
		            // Anchor a sparse infill to inner perimeters with the following anchor length:
			        params.anchor_length = float(layerm.region().extruder_config_value<Domain::FloatOrPercentage>("infill_anchor", FlowRole::frInfill).get_abs_value(params.spacing));
					params.anchor_length_max = float(layerm.region().extruder_config_value<Domain::FloatOrPercentage>("infill_anchor_max", FlowRole::frInfill).get_abs_value(params.spacing));
					params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
				}

				// Orca: the Locked Zag skin and skeleton flows and densities
				if (params.pattern == Domain::InfillPattern::ipLockedZag) {
					const float nozzle_diameter = float(region.nozzle_diameter(extrusion_role));
					const float height          = float((surface.thickness == -1) ? layer.height : surface.thickness);
					Flow skin_flow = params.bridge ? params.flow : Flow::new_from_config_width(extrusion_role, region_config.get<Domain::FloatOrPercentage>("skin_infill_line_width"), nozzle_diameter, height);
					append_flow_param(lock_param.skin_flow_params, skin_flow, surface.expolygon);
					Flow skeleton_flow = params.bridge ? params.flow : Flow::new_from_config_width(extrusion_role, region_config.get<Domain::FloatOrPercentage>("skeleton_infill_line_width"), nozzle_diameter, height);
					append_flow_param(lock_param.skeleton_flow_params, skeleton_flow, surface.expolygon);
					append_density_param(lock_param.skin_density_params, float(0.01 * region_config.get<Domain::Percentage>("skin_infill_density").value), surface.expolygon);
					append_density_param(lock_param.skeleton_density_params, float(0.01 * region_config.get<Domain::Percentage>("skeleton_infill_density").value), surface.expolygon);
				}

		        auto it_params = set_surface_params.find(params);
		        if (it_params == set_surface_params.end())
		        	it_params = set_surface_params.insert(it_params, params);
		        region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()] = &(*it_params);
		    }
	}

	surface_fills.reserve(set_surface_params.size());
	for (const SurfaceFillParams &params : set_surface_params) {
		const_cast<SurfaceFillParams&>(params).idx = surface_fills.size();
		surface_fills.emplace_back(params);
	}

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion &layerm = *layer.regions()[region_id];
	    for (const Surface &surface : layerm.fill_surfaces())
	        if (surface.surface_type != stInternalVoid) {
	        	const SurfaceFillParams *params = region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()];
				if (params != nullptr) {
	        		SurfaceFill &fill = surface_fills[params->idx];
                    if (fill.region_id == size_t(-1)) {
	        			fill.region_id = region_id;
	        			fill.surface = surface;
	        			fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
	        		} else
	        			fill.expolygons.emplace_back(surface.expolygon);
				}
	        }
	}

	{
		Polygons all_polygons;
		for (SurfaceFill &fill : surface_fills)
			if (! fill.expolygons.empty()) {
				if (fill.expolygons.size() > 1 || ! all_polygons.empty()) {
					Polygons polys = Algorithms::ExPolygon::to_polygons(std::move(fill.expolygons));
		            // Make a union of polygons, use a safety offset, subtract the preceding polygons.
				    // Bridges are processed first (see SurfaceFill::operator<())
		            fill.expolygons = all_polygons.empty() ? union_safety_offset_ex(polys) : diff_ex(polys, all_polygons, ApplySafetyOffset::Yes);
					append(all_polygons, std::move(polys));
				} else if (&fill != &surface_fills.back())
					append(all_polygons, Algorithms::ExPolygon::to_polygons(fill.expolygons));
	        }
	}

    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    if (has_internal_voids) {
    	// Internal voids are generated only if "infill_only_where_needed" or "infill_every_layers" are active.
        coord_t  distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
		int      region_internal_infill = -1;
		int		 region_solid_infill = -1;
		int		 region_some_infill = -1;
    	for (SurfaceFill &surface_fill : surface_fills)
			if (! surface_fill.expolygons.empty()) {
    			distance_between_surfaces = std::max(distance_between_surfaces, surface_fill.params.flow.scaled_spacing());
				append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons, Algorithms::ExPolygon::to_polygons(surface_fill.expolygons));
				if (surface_fill.surface.surface_type == stInternalSolid)
					region_internal_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.is_solid())
					region_solid_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.surface_type != stInternalVoid)
					region_some_infill = (int)surface_fill.region_id;
			}
    	if (! voids.empty() && ! surfaces_polygons.empty()) {
    		// First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
    		voids = diff(voids, surfaces_polygons);
	        // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
	        Polygons collapsed = diff(
	            surfaces_polygons,
				opening(surfaces_polygons, float(distance_between_surfaces /2), float(distance_between_surfaces / 2 + ClipperSafetyOffset)));
	        //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
	        // added if two offsetted void regions merge.
	        // polygons_append(voids, collapsed);
	        ExPolygons extensions = intersection_ex(expand(collapsed, float(distance_between_surfaces)), voids, ApplySafetyOffset::Yes);
	        // Now find an internal infill SurfaceFill to add these extrusions to.
	        SurfaceFill *internal_solid_fill = nullptr;
			unsigned int region_id = 0;
			if (region_internal_infill != -1)
				region_id = region_internal_infill;
			else if (region_solid_infill != -1)
				region_id = region_solid_infill;
			else if (region_some_infill != -1)
				region_id = region_some_infill;
			const LayerRegion& layerm = *layer.regions()[region_id];
	        for (SurfaceFill &surface_fill : surface_fills)
	        	if (surface_fill.surface.surface_type == stInternalSolid && std::abs(layer.height - surface_fill.params.flow.height()) < EPSILON) {
	        		internal_solid_fill = &surface_fill;
	        		break;
	        	}
	        if (internal_solid_fill == nullptr) {
	        	// Produce another solid fill.
		        params.extruder 	 = layerm.region().extruder(frSolidInfill);
	            params.pattern 		 = fill_type_monotonic(layerm.region().config().get<Domain::InfillPattern>("top_fill_pattern")) ? Domain::InfillPattern::ipMonotonic : Domain::InfillPattern::ipRectilinear;
	            params.density 		 = 100.f;
		        params.extrusion_role = ExtrusionRole::InternalInfill;
		        // Orca solid_infill_rotate_template
		        const std::string rotate_template = layerm.region().config().get<std::string>("solid_infill_rotate_template");
		        params.angle 		= float(calculate_infill_rotation_angle(layer.object(), layer.id(), layerm.region().config().get<double>("solid_infill_direction"), rotate_template));
		        params.fixed_angle  = ! rotate_template.empty();
		        // calculate the actual flow we'll be using for this infill
				params.flow = layerm.flow(frSolidInfill);
		        params.spacing = params.flow.spacing();	        
				surface_fills.emplace_back(params);
				surface_fills.back().surface.surface_type = stInternalSolid;
				surface_fills.back().surface.thickness = layer.height;
				surface_fills.back().expolygons = std::move(extensions);
	        } else {
	        	append(extensions, std::move(internal_solid_fill->expolygons));
	        	internal_solid_fill->expolygons = union_ex(extensions);
	        }
		}
    }

    // Orca detect_narrow_internal_solid_infill: the narrow internal solid areas are filled concentric
    if (layer.object()->config().get<bool>("detect_narrow_internal_solid_infill")) {
        const size_t surface_fills_size = surface_fills.size();
        for (size_t i = 0; i < surface_fills_size; ++ i) {
            // Ensuring fills the narrow parts itself
            if (surface_fills[i].surface.surface_type != stInternalSolid || surface_fills[i].params.pattern == Domain::InfillPattern::ipEnsuring)
                continue;
            ExPolygons normal_infill;
            ExPolygons narrow_infill;
            split_solid_surface(layer.id(), surface_fills[i], normal_infill, narrow_infill);
            if (narrow_infill.empty())
                continue;
            if (normal_infill.empty()) {
                // all narrow
                surface_fills[i].params.pattern = Domain::InfillPattern::ipConcentricInternal;
            } else {
                SurfaceFillParams narrow_params = surface_fills[i].params;
                narrow_params.pattern = Domain::InfillPattern::ipConcentricInternal;
                SurfaceFill &narrow_fill = surface_fills.emplace_back(narrow_params);
                narrow_fill.region_id                = surface_fills[i].region_id;
                narrow_fill.surface.surface_type     = stInternalSolid;
                narrow_fill.surface.thickness        = surface_fills[i].surface.thickness;
                narrow_fill.surface.thickness_layers = surface_fills[i].surface.thickness_layers;
                narrow_fill.expolygons               = std::move(narrow_infill);
                surface_fills[i].expolygons          = std::move(normal_infill);
            }
        }
    }

    return surface_fills;
}

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
void export_group_fills_to_svg(const char *path, const std::vector<SurfaceFill> &fills)
{
    BoundingBox bbox;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            bbox.merge(get_extents(expoly));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    Biz::Algorithms::SVG::SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            svg.draw(expoly, surface_type_to_color_name(fill.surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close(); 
}
#endif

static void insert_fills_into_islands(Layer &layer, uint32_t fill_region_id, uint32_t fill_begin, uint32_t fill_end)
{
	if (fill_begin < fill_end) {
    	// Sort the extrusion range into its LayerIsland.
	    // Traverse the slices in an increasing order of bounding box size, so that the islands inside another islands are tested first,
	    // so we can just test a point inside ExPolygon::contour and we may skip testing the holes.
	    auto point_inside_surface = [&layer](const size_t lslice_idx, const Point &point) {
	        const BoundingBox &bbox = layer.lslices_ex[lslice_idx].bbox;
	        return point.x() >= bbox.min.x() && point.x() < bbox.max.x() &&
	               point.y() >= bbox.min.y() && point.y() < bbox.max.y() &&
                   Algorithms::Polygon::contains(layer.lslices[lslice_idx].contour, point);
	    };
	    Point point = layer.get_region(fill_region_id)->fills().entities[fill_begin]->first_point();
	    int lslice_idx = int(layer.lslices_ex.size()) - 1;
	    for (; lslice_idx >= 0; -- lslice_idx)
	        if (point_inside_surface(lslice_idx, point))
	        	break;
	    assert(lslice_idx >= 0);
	    if (lslice_idx >= 0) {
	    	LayerSlice &lslice = layer.lslices_ex[lslice_idx];
	    	// Find an island.
	    	LayerIsland *island = nullptr;
	    	if (lslice.islands.size() == 1) {
	    		// Cool, just save the extrusions in there.
	    		island = &lslice.islands.front();
	    	} else {
	    		// The infill was created for one of the infills.
	    		// In case of ironing, the infill may not fall into any of the infill expolygons either.
	    		// In case of some numerical error, the infill may not fall into any of the infill expolygons either.
	    		// 1) Try an exact test, it should be cheaper than a closest region test.
	    		for (LayerIsland &li : lslice.islands) {
	    			const BoundingBoxes &bboxes     = li.fill_expolygons_composite() ?
	    				layer.get_region(li.perimeters.region())->fill_expolygons_composite_bboxes() :
	    				layer.get_region(li.fill_region_id)->fill_expolygons_bboxes();
	    			const ExPolygons 	&expolygons = li.fill_expolygons_composite() ? 
	    				layer.get_region(li.perimeters.region())->fill_expolygons_composite() :
	    				layer.get_region(li.fill_region_id)->fill_expolygons();
	    			for (uint32_t fill_expolygon_id : li.fill_expolygons)
	    				if (bboxes[fill_expolygon_id].contains(point) && Algorithms::ExPolygon::contains(expolygons[fill_expolygon_id], point)) {
	    					island = &li;
	    					goto found;
	    				}
	    		}
	    		// 2) Find closest fill_expolygon, branch and bound by distance to bounding box.
				{
					struct Island {
						uint32_t island_idx;
						uint32_t expolygon_idx;
						double   distance2;
					};
	    			std::vector<Island> islands_sorted;
	    			for (uint32_t island_idx = 0; island_idx < uint32_t(lslice.islands.size()); ++ island_idx) {
	    				const LayerIsland   &li     = lslice.islands[island_idx];
	    				const BoundingBoxes &bboxes = li.fill_expolygons_composite() ?
	    					layer.get_region(li.perimeters.region())->fill_expolygons_composite_bboxes() :
	    					layer.get_region(li.fill_region_id)->fill_expolygons_bboxes();
	    				for (uint32_t fill_expolygon_id : li.fill_expolygons)
							islands_sorted.push_back({ island_idx, fill_expolygon_id, BB::distance_squared(bboxes[fill_expolygon_id], point) });
	    			}
	    			std::sort(islands_sorted.begin(), islands_sorted.end(), [](auto &l, auto &r){ return l.distance2 < r.distance2; });
	    			auto dist_min2 = std::numeric_limits<double>::max();
	    			for (uint32_t sorted_bbox_idx = 0; sorted_bbox_idx < uint32_t(islands_sorted.size()); ++ sorted_bbox_idx) {
	    				const Island &isl = islands_sorted[sorted_bbox_idx];
	    				if (isl.distance2 > dist_min2)
							// Branch & bound condition.
	    					break;
	    				LayerIsland		  &li         = lslice.islands[isl.island_idx];
	    				const ExPolygons  &expolygons = li.fill_expolygons_composite() ?
	    					layer.get_region(li.perimeters.region())->fill_expolygons_composite() :
	    					layer.get_region(li.fill_region_id)->fill_expolygons();
	    				double d2 = (Slic3r::point_projection(expolygons[isl.expolygon_idx], point) - point).cast<double>().squaredNorm();
	    				if (d2 < dist_min2) {
	    					dist_min2 = d2;
	    					island = &li;
	    				}
	    			}
				}
	    	found:;
	    	}
	    	assert(island);
	    	if (island)
	    		island->add_fill_range(LayerExtrusionRange{ fill_region_id, { fill_begin, fill_end }});
	    }
    }
}

void Layer::clear_fills()
{
    for (LayerRegion *layerm : m_regions)
        layerm->m_fills.clear();
    for (LayerSlice &lslice : lslices_ex)
		for (LayerIsland &island : lslice.islands)
			island.fills.clear();
}

// Orca infill_body: anchors and printed infill must share the same body origin. Keep the choice here so per-model surface
// centering and separated sparse infill cannot drift apart. Returns the connected body the fill region is laid out on,
// or -1 to keep the object's origin.
static int infill_body(const Layer &layer, const SurfaceFill &fill, const ExPolygon &expoly)
{
    const auto &params = fill.params;
    // the top / bottom surfaces (3.0 has no bottom surface role)
    const bool per_model = fill.surface.is_external() && ! fill.surface.is_bridge() &&
                           params.center_of_surface_pattern == Domain::CenterOfSurfacePattern::EachModel &&
                           (params.pattern == Domain::InfillPattern::ipArchimedeanChords || params.pattern == Domain::InfillPattern::ipOctagramSpiral);
    int body = -1;
    if (per_model || params.separated_infills || params.pattern == Domain::InfillPattern::ipAdaptiveCubic || params.pattern == Domain::InfillPattern::ipSupportCubic) {
        const BoundingBox box          = get_extents(expoly);
        double            best_overlap = 0.;
        for (size_t i = 0; i < layer.lslices.size() && i < layer.lslices_separated_component_ids.size(); ++i) {
            if (! get_extents(layer.lslices[i]).overlap(box))
                continue;
            const double overlap = Algorithms::ExPolygon::area(intersection_ex(ExPolygons{ layer.lslices[i] }, ExPolygons{ expoly }));
            if (overlap > best_overlap) {
                best_overlap = overlap;
                body         = int(layer.lslices_separated_component_ids[i]);
            }
        }
    }
    return body;
}

void Layer::make_fills(const FillAdaptive::RegionOctrees* fill_octrees, FillLightning::Generator* lightning_generator)
{
	this->clear_fills();

    LockRegionParam               lock_param;
    std::vector<SurfaceFill>      surface_fills       = group_fills(*this, lock_param);
    const Domain::BoundingBox2crd bbox                = this->object()->bounding_box();
    const auto                    resolution          = this->object()->print()->config().get<double>("gcode_resolution");
    const auto                    perimeter_generator = this->object()->config().get<Domain::PerimeterGeneratorType>("perimeter_generator");

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
	{
		static int iRun = 0;
		export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun ++).c_str(), surface_fills);
	}
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

	size_t first_object_layer_id = this->object()->get_layer(0)->id();
    for (SurfaceFill &surface_fill : surface_fills) {
        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
		// Layer ID is used for orienting the infill in alternating directions.
		// Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
		// from raft.
        f->layer_id = this->id() - first_object_layer_id;
        f->z 		= this->print_z;
        f->angle 	= surface_fill.params.angle;
        f->fixed_angle = surface_fill.params.fixed_angle;
        // Orca: the octrees of the region's line spacing, the one of the body below
        const FillAdaptive::Octrees *octrees = fill_octrees ? fill_octrees->region(surface_fill.region_id) : nullptr;
        f->tpms_radial_field   = this->object()->tpms_radial_field(surface_fill.params.tpms_adaptive);
        f->region_config = layerm.region().config();

        if (surface_fill.params.pattern == Domain::InfillPattern::ipLightning)
            dynamic_cast<FillLightning::Filler*>(f.get())->generator = lightning_generator;

        if (surface_fill.params.pattern == Domain::InfillPattern::ipEnsuring) {
            auto *fill_ensuring = dynamic_cast<FillEnsuring *>(f.get());
            assert(fill_ensuring != nullptr);
            fill_ensuring->print_region_config = m_regions[surface_fill.region_id]->region().config();
        }

        // calculate flow spacing for infill pattern generation
        bool using_internal_flow = ! surface_fill.surface.is_solid() && ! surface_fill.params.bridge;
        double link_max_length = 0.;
        if (! surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t)scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density                    = float(0.01 * surface_fill.params.density);
        params.dont_adjust                = false; //  surface_fill.params.dont_adjust;
        // Orca bridge_density / internal_bridge_density
        if (surface_fill.params.bridge && surface_fill.surface.is_external() && surface_fill.params.density > 99.) {
            params.density     = float(m_regions[surface_fill.region_id]->region().config().get<Domain::Percentage>("bridge_density").get_abs_value(1.));
            params.dont_adjust = true;
        }
        if (surface_fill.surface.surface_type == stInternalBridge) {
            params.density     = float(this->object()->config().get<Domain::Percentage>("internal_bridge_density").get_abs_value(1.));
            params.dont_adjust = true;
        }
        // Orca elefant_foot_layers_density: the solid infill of the elephant foot compensation layers above the first
        if (const float elefant_density = float(this->object()->config().get<Domain::Percentage>("elefant_foot_layers_density").get_abs_value(1.));
            std::abs(elefant_density - 1.f) > EPSILON && surface_fill.surface.surface_type == stInternalSolid) {
            const size_t elefant_layers = size_t(std::max(1, this->object()->config().get<int>("elefant_foot_compensation_layers")));
            if (f->layer_id > 0 && f->layer_id <= elefant_layers)
                params.density = 1.f - (1.f - elefant_density) * float(elefant_layers - (f->layer_id - 1)) / float(elefant_layers);
        }
        params.anchor_length              = surface_fill.params.anchor_length;
        params.anchor_length_max          = surface_fill.params.anchor_length_max;
        params.resolution                 = resolution;
        params.use_arachne                = (perimeter_generator == Domain::PerimeterGeneratorType::Arachne && surface_fill.params.pattern == Domain::InfillPattern::ipConcentric) || surface_fill.params.pattern == Domain::InfillPattern::ipEnsuring ||
                                            surface_fill.params.pattern == Domain::InfillPattern::ipConcentricInternal ||
                                            surface_fill.params.pattern == Domain::InfillPattern::ipSpiralInset;
        params.layer_height               = layerm.layer()->height;
        params.prefer_clockwise_movements = this->object()->print()->config().get<bool>("prefer_clockwise_movements");
        FlowRole flow_role{
            surface_fill.surface.is_top() ?
                frTopSolidInfill :
                (surface_fill.surface.is_solid() ? frSolidInfill : frInfill)};
        ASSERT(layerm.region().extruder(flow_role) > 0);
        params.extruder_id = layerm.region().extruder(flow_role) - 1;
        // Orca: for the fill order and the centering of the surface patterns
        params.extrusion_role             = surface_fill.params.extrusion_role;
        params.fill_order                 = surface_fill.params.fill_order;
        params.center_of_surface_pattern  = surface_fill.params.center_of_surface_pattern;
        params.pattern                    = surface_fill.params.pattern;
        params.multiline                  = surface_fill.params.multiline;
        params.smooth_factor              = surface_fill.params.smooth_factor;
        params.infill_complete_top        = surface_fill.params.infill_complete_top;
        params.lateral_lattice_angle_1    = surface_fill.params.lateral_lattice_angle_1;
        params.lateral_lattice_angle_2    = surface_fill.params.lateral_lattice_angle_2;
        params.infill_overhang_angle      = surface_fill.params.infill_overhang_angle;
        params.gyroid_optimized           = surface_fill.params.gyroid_optimized;
        params.tpms_adaptive              = surface_fill.params.tpms_adaptive;
        params.tpms_interior_density      = float(0.01 * surface_fill.params.tpms_interior_density);
        params.tpms_adaptive_gradient     = surface_fill.params.tpms_adaptive_gradient;
        // Orca Locked Zag, Cross Zag
        if (surface_fill.params.pattern == Domain::InfillPattern::ipLockedZag) {
            params.locked_zag        = true;
            params.infill_lock_depth = surface_fill.params.infill_lock_depth;
            params.skin_infill_depth = surface_fill.params.skin_infill_depth;
            f->set_lock_region_param(lock_param);
        }
        if (surface_fill.params.pattern == Domain::InfillPattern::ipCrossZag || surface_fill.params.pattern == Domain::InfillPattern::ipLockedZag) {
            const double infill_shift_step = layerm.region().config().get<double>("infill_shift_step");
            if (f->layer_id % 2 == 0)
                params.horiz_move -= scale_(infill_shift_step) * (f->layer_id / 2);
            else
                params.horiz_move += scale_(infill_shift_step) * (f->layer_id / 2);
        }
        params.symmetric_infill_y_axis = surface_fill.params.symmetric_infill_y_axis;

        for (ExPolygon &expoly : surface_fill.expolygons) {
            // Orca separated_infills: laid out on the box of the body of the region, with its octree
            const int body = infill_body(*this, surface_fill, expoly);
            f->set_bounding_box(body >= 0 ? this->object()->separated_body_bboxes()[body] : bbox);
            f->adapt_fill_octree = octrees ? octrees->get(body) : nullptr;
            // Orca symmetric_infill_y_axis: the surface mirrored about the center of the object, the lines back
            if (params.symmetric_infill_y_axis) {
                params.symmetric_y_axis = BB::center(f->extended_object_bounding_box()).x();
                symmetric_y(expoly.contour.points, params.symmetric_y_axis);
                for (Polygon &hole : expoly.holes)
                    symmetric_y(hole.points, params.symmetric_y_axis);
            }
			// Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
			f->spacing = surface_fill.params.spacing;
			surface_fill.surface.expolygon = std::move(expoly);
            // Orca Locked Zag: the skin and skeleton at their own flows
            if (auto *locked_zag = dynamic_cast<FillLockedZag*>(f.get())) {
                std::vector<std::pair<Polylines, Flow>> multi_width_polyline;
                try {
                    locked_zag->fill_surface_locked_zag(&surface_fill.surface, params, multi_width_polyline);
                } catch (InfillFailedException &) {
                }
                if (!multi_width_polyline.empty()) {
                    ExtrusionEntityCollection *eec        = new ExtrusionEntityCollection();
                    auto                       fill_begin = uint32_t(layerm.fills().size());
                    eec->no_sort = f->no_sort();
                    for (std::pair<Polylines, Flow> &poly_with_flow : multi_width_polyline) {
                        // calculate actual flow from spacing (which might have been adjusted by the infill pattern generator)
                        double flow_mm3_per_mm = poly_with_flow.second.mm3_per_mm();
                        double flow_width      = poly_with_flow.second.width();
                        if (! using_internal_flow) {
                            Flow new_flow   = poly_with_flow.second.with_spacing(float(f->spacing));
                            flow_mm3_per_mm = new_flow.mm3_per_mm();
                            flow_width      = new_flow.width();
                        }
                        extrusion_entities_append_paths(
                            eec->entities, std::move(poly_with_flow.first),
                            ExtrusionAttributes{ surface_fill.params.extrusion_role,
                                                 ExtrusionFlow{ flow_mm3_per_mm, float(flow_width), poly_with_flow.second.height() },
                                                 f->is_self_crossing() },
                            !params.prefer_clockwise_movements);
                    }
                    layerm.m_fills.entities.push_back(eec);
                    insert_fills_into_islands(*this, uint32_t(surface_fill.region_id), fill_begin, uint32_t(layerm.fills().size()));
                }
                continue;
            }
            Polylines      polylines;
            ThickPolylines thick_polylines;
			try {
                if (params.use_arachne)
                    thick_polylines = f->fill_surface_arachne(&surface_fill.surface, params);
                else
				    polylines = f->fill_surface(&surface_fill.surface, params);
			} catch (InfillFailedException &) {
			}
            if (!polylines.empty() || !thick_polylines.empty()) {
                // calculate actual flow from spacing (which might have been adjusted by the infill
		        // pattern generator)
		        double flow_mm3_per_mm = surface_fill.params.flow.mm3_per_mm();
		        double flow_width      = surface_fill.params.flow.width();
		        if (using_internal_flow) {
		            // if we used the internal flow we're not doing a solid infill
		            // so we can safely ignore the slight variation that might have
		            // been applied to f->spacing
		        } else {
		            Flow new_flow   = surface_fill.params.flow.with_spacing(float(f->spacing));
		        	flow_mm3_per_mm = new_flow.mm3_per_mm();
		        	flow_width      = new_flow.width();
		        }
                // Save into layer.
                ExtrusionEntityCollection *eec        = new ExtrusionEntityCollection();
                auto                       fill_begin = uint32_t(layerm.fills().size());
                // Only concentric fills are not sorted. Orca: a forced surface fill order must survive the path planner.
                const bool keep_fill_order = params.fill_order != Domain::SurfaceFillOrder::Default;
                eec->no_sort = f->no_sort() || keep_fill_order;
                if (params.use_arachne) {
                    for (const ThickPolyline &thick_polyline : thick_polylines) {
                        Flow new_flow = surface_fill.params.flow.with_spacing(float(f->spacing));

                        ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(thick_polyline, surface_fill.params.extrusion_role, new_flow, scaled<float>(0.05), float(SCALED_EPSILON));
                        // Append paths to collection.
                        if (!multi_path.empty()) {
                            if (multi_path.paths.front().first_point() == multi_path.paths.back().last_point())
                                eec->entities.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
                            else
                                eec->entities.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
                        }
                    }

                    if (!eec->empty())
                        layerm.m_fills.entities.push_back(eec);
                    else
                        delete eec;

                    thick_polylines.clear();
                } else {
                    // When prefer_clockwise_movements is true, we have to ensure that extrusion paths will not be reversed during path planning.
                    extrusion_entities_append_paths(
                        eec->entities, std::move(polylines),
						ExtrusionAttributes{
                            surface_fill.params.extrusion_role,
							ExtrusionFlow{ flow_mm3_per_mm, float(flow_width), surface_fill.params.flow.height() },
                            f->is_self_crossing()
						}, !params.prefer_clockwise_movements && !keep_fill_order && surface_fill.params.pattern != Domain::InfillPattern::ipGrid);
                    layerm.m_fills.entities.push_back(eec);
                }
                insert_fills_into_islands(*this, uint32_t(surface_fill.region_id), fill_begin, uint32_t(layerm.fills().size()));
		    }
		}
    }

	for (LayerSlice &lslice : this->lslices_ex)
		for (LayerIsland &island : lslice.islands) {
			if (! island.thin_fills.empty()) {
				// Copy thin fills into fills packed as a collection.
				// Fills are always stored as collections, the rest of the pipeline (wipe into infill, G-code generator) relies on it.
				LayerRegion				  &layerm	  = *this->get_region(island.perimeters.region());
				ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
				layerm.m_fills.entities.push_back(&collection);
				collection.entities.reserve(island.thin_fills.size());
				for (uint32_t fill_id : island.thin_fills)
					collection.entities.push_back(layerm.thin_fills().entities[fill_id]->clone());
	    		island.add_fill_range({ island.perimeters.region(), { uint32_t(layerm.m_fills.entities.size() - 1), uint32_t(layerm.m_fills.entities.size()) } });
			}
			// Sort the fills by region ID.
			std::sort(island.fills.begin(), island.fills.end(), [](auto &l, auto &r){ return l.region() < r.region() || (l.region() == r.region() && *l.begin() < *r.begin()); });
			// Compress continuous fill ranges of the same region.
			{
				size_t k = 0;
				for (size_t i = 0; i < island.fills.size();) {
					uint32_t region_id = island.fills[i].region();
					uint32_t begin     = *island.fills[i].begin();
					uint32_t end       = *island.fills[i].end();
					size_t   j         = i + 1;
					for (; j < island.fills.size() && island.fills[j].region() == region_id && *island.fills[j].begin() == end; ++ j)
						end = *island.fills[j].end();
					island.fills[k ++] = { region_id, { begin, end } };
					i = j;
				}
				island.fills.erase(island.fills.begin() + k, island.fills.end());
			}
		}

#ifndef NDEBUG
	for (LayerRegion *layerm : m_regions)
	    for (const ExtrusionEntity *e : layerm->fills())
    	    assert(dynamic_cast<const ExtrusionEntityCollection*>(e) != nullptr);
#endif
}

Polylines Layer::generate_sparse_infill_polylines_for_anchoring(const FillAdaptive::RegionOctrees* fill_octrees, FillLightning::Generator* lightning_generator) const
{
    LockRegionParam               lock_param;
    std::vector<SurfaceFill>      surface_fills = group_fills(*this, lock_param);
    const Domain::BoundingBox2crd bbox          = this->object()->bounding_box();
    const auto                    resolution    = this->object()->print()->config().get<double>("gcode_resolution");

    Polylines sparse_infill_polylines{};

    for (SurfaceFill &surface_fill : surface_fills) {
		if (surface_fill.surface.surface_type != stInternal) {
			continue;
		}

        switch (surface_fill.params.pattern) {
        case Domain::InfillPattern::ipCount: continue; break;
        case Domain::InfillPattern::ipSupportBase: continue; break;
        case Domain::InfillPattern::ipEnsuring: continue; break;
        case Domain::InfillPattern::ipConcentricInternal: continue; break;
        case Domain::InfillPattern::ipLightning:
		case Domain::InfillPattern::ipAdaptiveCubic:
        case Domain::InfillPattern::ipSupportCubic:
        case Domain::InfillPattern::ipRectilinear:
        case Domain::InfillPattern::ipMonotonic:
        case Domain::InfillPattern::ipMonotonicLines:
        case Domain::InfillPattern::ipAlignedRectilinear:
        case Domain::InfillPattern::ipGrid:
        case Domain::InfillPattern::ipTriangles:
        case Domain::InfillPattern::ipStars:
        case Domain::InfillPattern::ipCubic:
        case Domain::InfillPattern::ipLine:
        case Domain::InfillPattern::ipConcentric:
        case Domain::InfillPattern::ipHoneycomb:
        case Domain::InfillPattern::ip3DHoneycomb:
        case Domain::InfillPattern::ip3DHoneycombClassic:
        case Domain::InfillPattern::ipQuarterCubic:
        case Domain::InfillPattern::ipLateralLattice:
        case Domain::InfillPattern::ipLateralHoneycomb:
        case Domain::InfillPattern::ipCrossHatch:
        case Domain::InfillPattern::ipSpiralInset:
        case Domain::InfillPattern::ipTpmsD:
        case Domain::InfillPattern::ipTpmsFK:
        case Domain::InfillPattern::ipCrossZag:
        case Domain::InfillPattern::ipLockedZag:
        case Domain::InfillPattern::ipGyroid:
        case Domain::InfillPattern::ipHilbertCurve:
        case Domain::InfillPattern::ipArchimedeanChords:
        case Domain::InfillPattern::ipOctagramSpiral:
        case Domain::InfillPattern::ipZigZag: break;
        }


        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id() - this->object()->get_layer(0)->id(); // We need to subtract raft layers.
        f->z        = this->print_z;
        f->angle    = surface_fill.params.angle;
        f->fixed_angle = surface_fill.params.fixed_angle;
        // Orca: the octrees of the region's line spacing, the one of the body below
        const FillAdaptive::Octrees *octrees = fill_octrees ? fill_octrees->region(surface_fill.region_id) : nullptr;
        f->tpms_radial_field   = this->object()->tpms_radial_field(surface_fill.params.tpms_adaptive);
        f->region_config = layerm.region().config();

        if (surface_fill.params.pattern == Domain::InfillPattern::ipLightning)
            dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

        // calculate flow spacing for infill pattern generation
        double link_max_length = 0.;
        if (!surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t) scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density           = float(0.01 * surface_fill.params.density);
        params.dont_adjust       = false; //  surface_fill.params.dont_adjust;
        params.anchor_length     = surface_fill.params.anchor_length;
        params.anchor_length_max = surface_fill.params.anchor_length_max;
        params.resolution        = resolution;
        params.use_arachne       = false;
        params.layer_height      = layerm.layer()->height;
        ASSERT(layerm.region().extruder(frInfill) > 0);
        params.extruder_id       = layerm.region().extruder(frInfill) - 1;

        for (ExPolygon &expoly : surface_fill.expolygons) {
            // Orca separated_infills: laid out on the box of the body of the region, with its octree
            const int body = infill_body(*this, surface_fill, expoly);
            f->set_bounding_box(body >= 0 ? this->object()->separated_body_bboxes()[body] : bbox);
            f->adapt_fill_octree = octrees ? octrees->get(body) : nullptr;
            // Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
            f->spacing                     = surface_fill.params.spacing;
            surface_fill.surface.expolygon = std::move(expoly);
            try {
                Polylines polylines = f->fill_surface(&surface_fill.surface, params);
                sparse_infill_polylines.insert(sparse_infill_polylines.end(), polylines.begin(), polylines.end());
            } catch (InfillFailedException &) {}
        }
    }

    return sparse_infill_polylines;
}

// Create ironing extrusions over top surfaces.
void Layer::make_ironing()
{
	// LayerRegion::slices contains surfaces marked with SurfaceType.
	// Here we want to collect top surfaces extruded with the same extruder.
	// A surface will be ironed with the same extruder to not contaminate the print with another material leaking from the nozzle.

	// First classify regions based on the extruder used.
	struct IroningParams {
		int 		extruder 	= -1;
		bool 		just_infill = false;
		// Spacing of the ironing lines, also to calculate the extrusion flow from.
		double 		line_spacing;
		// Height of the extrusion, to calculate the extrusion flow from.
		double 		height;
		double 		speed;
		double 		angle;
		bool        fixed_angle;
		double      inset;
		Domain::InfillPattern pattern;

		bool operator<(const IroningParams &rhs) const {
			if (this->extruder < rhs.extruder)
				return true;
			if (this->extruder > rhs.extruder)
				return false;
			if (int(this->just_infill) < int(rhs.just_infill))
				return true;
			if (int(this->just_infill) > int(rhs.just_infill))
				return false;
			if (this->line_spacing < rhs.line_spacing)
				return true;
			if (this->line_spacing > rhs.line_spacing)
				return false;
			if (this->height < rhs.height)
				return true;
			if (this->height > rhs.height)
				return false;
			if (this->speed < rhs.speed)
				return true;
			if (this->speed > rhs.speed)
				return false;
			if (this->angle < rhs.angle)
				return true;
			if (this->angle > rhs.angle)
				return false;
			if (int(this->fixed_angle) != int(rhs.fixed_angle))
				return int(this->fixed_angle) < int(rhs.fixed_angle);
			if (this->inset != rhs.inset)
				return this->inset < rhs.inset;
			return int(this->pattern) < int(rhs.pattern);
		}

		bool operator==(const IroningParams &rhs) const {
			return this->extruder == rhs.extruder && this->just_infill == rhs.just_infill &&
				   this->line_spacing == rhs.line_spacing && this->height == rhs.height && this->speed == rhs.speed &&
				   this->angle == rhs.angle && this->fixed_angle == rhs.fixed_angle && this->inset == rhs.inset &&
				   this->pattern == rhs.pattern;
		}

		LayerRegion *layerm;
		uint32_t     region_id;

		// IdeaMaker: ironing
		// ironing flowrate (5% percent)
		// ironing speed (10 mm/sec)

		// Kisslicer: 
		// iron off, Sweep, Group
		// ironing speed: 15 mm/sec

		// Cura:
		// Pattern (zig-zag / concentric)
		// line spacing (0.1mm)
		// flow: from normal layer height. 10%
		// speed: 20 mm/sec
	};

	std::vector<IroningParams> by_extruder;
    double default_layer_height = this->object()->config().get<double>("layer_height");

	for (uint32_t region_id = 0; region_id < uint32_t(this->regions().size()); ++region_id)
		if (LayerRegion *layerm = this->get_region(region_id); ! layerm->slices().empty()) {
			IroningParams ironing_params;
			const PrintRegion &region = layerm->region();
			const PrintRegionConfigView &config = layerm->region().config();
			if (config.get<bool>("ironing") && 
				(config.get<Domain::IroningType>("ironing_type") == Domain::IroningType::AllSolid ||
				 	(region.extruder_config_value<int>("top_solid_layers", FlowRole::frTopSolidInfill) > 0 && 
						(config.get<Domain::IroningType>("ironing_type") == Domain::IroningType::TopSurfaces ||
					 	(config.get<Domain::IroningType>("ironing_type") == Domain::IroningType::TopmostOnly && layerm->layer()->upper_layer == nullptr))))) {
				if (config.get<int>("perimeter_extruder") == config.get<int>("solid_infill_extruder") || region.extruder_config_value<int>("perimeters", FlowRole::frPerimeter) == 0) {
					// Iron the whole face.
					ironing_params.extruder = config.get<int>("solid_infill_extruder");
				} else {
					// Iron just the infill.
					ironing_params.extruder = config.get<int>("solid_infill_extruder");
				}
				// Orca: ironing in the top surface's filament
				if (config.get<int>("top_surface_filament_id") > 0)
					ironing_params.extruder = config.get<int>("top_surface_filament_id");
			}
			if (ironing_params.extruder != -1) {
				//TODO just_infill is currently not used.
				ironing_params.just_infill 	= false;
				ironing_params.line_spacing = config.get<double>("ironing_spacing");
				ironing_params.height 		= config.get<Domain::Percentage>("ironing_flowrate").get_abs_value(default_layer_height);
				ironing_params.speed 		= config.get<double>("ironing_speed");
				// Orca: the ironing angle offset from the top surface's (3.0: the fill angle rotated by 45°), or absolute
				// Orca: from the top layer direction or the solid infill direction (its rotation template), turned
				// by ironing_angle and with the model (align_infill_direction_to_model)
				const bool        top_layer_direction_set = config.get<double>("top_layer_direction") >= 0.;
				const std::string rotate_template         = config.get<std::string>("solid_infill_rotate_template");
				const double      top_layer_base_angle    = top_layer_direction_set ? deg2rad(config.get<double>("top_layer_direction")) :
					calculate_infill_rotation_angle(this->object(), this->id(), config.get<double>("solid_infill_direction"), rotate_template);
				double ironing_angle = (config.get<bool>("ironing_angle_fixed") ? 0. : top_layer_base_angle) + config.get<double>("ironing_angle") * M_PI / 180.;
				if (config.get<bool>("align_infill_direction_to_model"))
					ironing_angle += model_rotation(*this->object());
				ironing_params.angle        = ironing_angle;
				ironing_params.fixed_angle  = config.get<bool>("ironing_angle_fixed") || top_layer_direction_set || ! rotate_template.empty();
				ironing_params.inset        = config.get<double>("ironing_inset");
				ironing_params.pattern      = config.get<Domain::InfillPattern>("ironing_pattern");
				ironing_params.layerm 		= layerm;
				ironing_params.region_id    = region_id;
				by_extruder.emplace_back(ironing_params);
			}
		}
	std::sort(by_extruder.begin(), by_extruder.end());

    // the filler of the ironing pattern (Orca ironing_pattern)
    std::unique_ptr<Fill> fill;
    Domain::InfillPattern fill_pattern{};
    FillParams 			fill_params;
    fill_params.density 	 = 1.;
    fill_params.monotonic    = true;

	for (size_t i = 0; i < by_extruder.size();) {
		// Find span of regions equivalent to the ironing operation.
		IroningParams &ironing_params = by_extruder[i];
		if (!fill || fill_pattern != ironing_params.pattern) {
			fill_pattern = ironing_params.pattern;
			fill.reset(Fill::new_from_type(fill_pattern));
			fill->set_bounding_box(this->object()->bounding_box());
			// Layer ID is used for orienting the infill in alternating directions.
			// Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
			// from raft.
			fill->layer_id = this->id() - this->object()->get_layer(0)->id();
			fill->z        = this->print_z;
			fill->overlap  = 0;
		}
		size_t j = i;
		for (++ j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++ j) ;

		// Create the ironing extrusions for regions <i, j)
		ExPolygons ironing_areas;
		double nozzle_dmr = Biz::Slicing::get_nozzle_diameter(this->object()->config().hw_config(), ironing_params.extruder - 1);
		if (ironing_params.just_infill) {
			//TODO just_infill is currently not used.
			// Just infill.
		} else {
			// Infill and perimeter.
			// Merge top surfaces with the same ironing parameters.
			Polygons polys;
			Polygons infills;
			for (size_t k = i; k < j; ++ k) {
				const IroningParams		 &ironing_params  = by_extruder[k];
				const PrintRegion &region   = ironing_params.layerm->region();
				const PrintRegionConfigView  &region_config   = region.config();
				bool					  iron_everything = region_config.get<Domain::IroningType>("ironing_type") == Domain::IroningType::AllSolid;
				bool					  iron_completely = iron_everything;
				if (iron_everything) {
					// Check whether there is any non-solid hole in the regions.
					bool internal_infill_solid = region.extruder_config_value<Domain::Percentage>("fill_density", FlowRole::frInfill) > Domain::Percentage{95.};
					for (const Surface &surface : ironing_params.layerm->fill_surfaces())
						if ((! internal_infill_solid && surface.surface_type == stInternal) || surface.surface_type == stInternalBridge || surface.surface_type == stInternalVoid) {
							// Some fill region is not quite solid. Don't iron over the whole surface.
							iron_completely = false;
							break;
						}
				}
				if (iron_completely) {
					// Iron everything. This is likely only good for solid transparent objects.
					for (const Surface &surface : ironing_params.layerm->slices())
                        Algorithms::Polygon::append(polys, surface.expolygon);
				} else {
					for (const Surface &surface : ironing_params.layerm->slices())
						if (surface.surface_type == stTop || (iron_everything && surface.surface_type == stBottom))
							// stBottomBridge is not being ironed on purpose, as it would likely destroy the bridges.
                            Algorithms::Polygon::append(polys, surface.expolygon);
				}
				if (iron_everything && ! iron_completely) {
					// Add solid fill surfaces. This may not be ideal, as one will not iron perimeters touching these
					// solid fill surfaces, but it is likely better than nothing.
					for (const Surface &surface : ironing_params.layerm->fill_surfaces())
						if (surface.surface_type == stInternalSolid)
                            Algorithms::Polygon::append(infills, surface.expolygon);
				}
			}

			if (! infills.empty() || j > i + 1) {
				// Ironing over more than a single region or over solid internal infill.
				if (! infills.empty())
					// For Domain::IroningType::AllSolid only:
					// Add solid infill areas for layers, that contain some non-ironable infil (sparse infill, bridge infill).
					append(polys, std::move(infills));
				polys = union_safety_offset(polys);
			}
			// Trim the top surfaces with half the nozzle diameter, or Orca's ironing inset.
			const double ironing_areas_offset = ironing_params.inset == 0 ? scale_(0.5 * nozzle_dmr) : scale_(ironing_params.inset);
			ironing_areas = intersection_ex(polys, offset(this->lslices, - float(ironing_areas_offset)));
		}

        // Create the filler object.
        fill->spacing = ironing_params.line_spacing;
        fill->angle = float(ironing_params.angle);
        fill->fixed_angle = ironing_params.fixed_angle;
        fill->link_max_length = (coord_t)scale_(3. * fill->spacing);
		double extrusion_height = ironing_params.height * fill->spacing / nozzle_dmr;
		float  extrusion_width  = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle_dmr), float(extrusion_height));
		double flow_mm3_per_mm = nozzle_dmr * extrusion_height;
        Surface surface_fill(stTop, ExPolygon());
        for (ExPolygon &expoly : ironing_areas) {
			surface_fill.expolygon = std::move(expoly);
			Polylines polylines;
			try {
                assert(!fill_params.use_arachne);
				polylines = fill->fill_surface(&surface_fill, fill_params);
			} catch (InfillFailedException &) {
			}
	        if (! polylines.empty()) {
		        // Save into layer.
				auto fill_begin = uint32_t(ironing_params.layerm->fills().size());
				ExtrusionEntityCollection *eec = nullptr;
		        ironing_params.layerm->m_fills.entities.push_back(eec = new ExtrusionEntityCollection());
		        // Don't sort the ironing infill lines as they are monotonicly ordered.
				eec->no_sort = true;
		        extrusion_entities_append_paths(
		            eec->entities, std::move(polylines),
					ExtrusionAttributes{ ExtrusionRole::Ironing,
						ExtrusionFlow{ flow_mm3_per_mm, extrusion_width, float(extrusion_height) }
					});
				insert_fills_into_islands(*this, ironing_params.region_id, fill_begin, uint32_t(ironing_params.layerm->fills().size()));
		    }
		}

		// Regions up to j were processed.
		i = j;
	}
}

} // namespace Slic3r
