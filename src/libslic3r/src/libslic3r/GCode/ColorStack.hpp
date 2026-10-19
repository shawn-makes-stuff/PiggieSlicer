#ifndef slic3r_GCode_ColorStack_hpp_
#define slic3r_GCode_ColorStack_hpp_

// Colour stacking: ColorStack (github.com/shawn-makes-stuff/ColorStack, colorstack/plan.py) ported into the slicer.
// Layers are grouped into bands; inside a band each unit (a colour, per object or per island) prints its layers
// bottom-up before the next unit, so a multicolour print swaps filament far less often. Material an earlier unit
// would leave in a later nozzle's way, or that would rest only on a later unit's layer, is handed to the later unit
// where it is not a visible surface. Additions over ColorStack (user, 2026-10-19): the resting test probes across
// the bead width, and island units. Design: docs/design/COLORSTACK_MODE.md.

#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r::ColorStack {

// The nozzle: a flat tip under a cone. Material dz above the tip must be at least reach(dz) away.
struct Nozzle
{
    double tip_radius{0.5};   // mm
    double cone_angle{45.};   // degrees from vertical
    double clear_height{2.};  // mm the tip sticks out below the heater block / sock / duct
    double contact{0.2};      // mm a neighbour may stand above the tip under the nozzle face

    double reach(double dz) const { return tip_radius + dz * std::tan(cone_angle * M_PI / 180.); }
    double over(double dz) const { return dz - contact; }
};

struct Options
{
    int    max_band_layers{10};
    double surface_tolerance{0.};  // mm a visible colour edge may move
    double resolution{0.1};        // sampling step / 2
    double min_piece{2.};          // shorter leftovers between handed-over pieces are handed over too
    bool   per_object{true};       // units per object (ColorStack per_object)
    // additions
    bool   islands{false};         // units per island
    double overhang_tolerance{0.1}; // mm of a bead edge that may hang over a unit printed later
    std::vector<std::vector<float>> purge; // mm3 purged per swap [from][to]; empty: the plans compare by swaps
};

enum class PathType { Other, OuterWall, OverhangWall, TopSurface, BottomSurface, Ironing, Support };

// One extrusion path, in print coordinates (mm).
struct Path
{
    int                tool{-1};
    PathType           type{PathType::Other};
    double             width{0.4};
    std::vector<Vec2d> pts;
    int                obj{0};    // the object (instance) it belongs to
    int                island{0}; // its island over the whole print (staggered island units)
    int                slice{-1}; // its slice of the layer, -1: none (support); band island units join slices
    int                seq{0};    // its place in the slicer's print order
    int                source{-1}; // the slicer's path it is (a piece of)
    int                cache{-1};  // the planner's: its index in its layer's paths while whole (its samples are kept)

    bool   visible() const { return type == PathType::OuterWall || type == PathType::OverhangWall || type == PathType::TopSurface ||
                                    type == PathType::BottomSurface || type == PathType::Ironing; }
    double length() const;
};

struct Layer
{
    double            z{0.};
    double            height{0.};
    bool              barrier{false}; // pause / filament change / custom G-code: no band crosses it
    std::vector<Path> paths;
    std::vector<std::pair<int, int>> links; // its slices to the slices of the layer below they overlap
    // the parts' outlines: an outer wall away from them is between two colours, not a surface (addition; none:
    // ColorStack's test, plastic on both sides)
    std::vector<std::vector<Vec2d>> outline;
};

// A unit prints its material of a band in one go: a colour, per object or per island.
struct Unit
{
    int tool{-1};
    int part{-1}; // object or island, -1: the whole colour
    bool operator==(const Unit &rhs) const { return tool == rhs.tool && part == rhs.part; }
    bool operator<(const Unit &rhs) const { return tool < rhs.tool || (tool == rhs.tool && part < rhs.part); }
};
inline const Unit DONE{-2, -2}; // owner of material already printed

// Layers [first, first + count): the units in order, each all its layers bottom-up. work: per layer, each piece
// with the unit printing it.
struct Band
{
    size_t                                       first{0};
    size_t                                       count{0};
    std::vector<Unit>                            order;
    std::vector<std::vector<std::pair<Unit, Path>>> work;
    double                                       ceded_mm{0.};
    double                                       visible_ceded_mm{0.};
    bool                                         run{false};     // a staggered run (one unit)
    size_t                                       complete_to{0}; // a run: after it the tower layers below are due
    std::vector<int>                             tools() const; // the tools in order, consecutive repeats merged
};

// The plan for a print: the best of ColorStack's variants (bands; staggered when a layer has more than two
// colours and staggered is on; per object when a tool spans two objects), least purged, then fewest swaps.
std::vector<Band> plan(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &options, bool staggered);

// ColorStack's planners, for tests.
std::vector<Band> plan_bands(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &options);
std::vector<Band> plan_staggered(const std::vector<Layer> &layers, int first_tool, const Nozzle &nozzle, const Options &options);
// Swaps a plan makes from first_tool.
size_t swaps(const std::vector<Band> &bands, int first_tool);
// What a plan purges from first_tool (mm3 by the purge volumes).
double purge(const std::vector<Band> &bands, int first_tool, const std::vector<std::vector<float>> &volumes);

} // namespace Slic3r::ColorStack

#endif // slic3r_GCode_ColorStack_hpp_
