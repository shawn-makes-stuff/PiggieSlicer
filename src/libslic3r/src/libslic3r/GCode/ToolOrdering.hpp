// Ordering of the tools to minimize tool switches.

#ifndef slic3r_ToolOrdering_hpp_
#define slic3r_ToolOrdering_hpp_

#include <boost/container/small_vector.hpp>
#include <stdint.h>
#include <boost/container/vector.hpp>
#include <boost/cstdint.hpp>
#include <utility>
#include <cstddef>
#include <algorithm>
#include <map>
#include <vector>
#include <cinttypes>

#include "Slic3r/Domain/CustomGCode.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/GCode/ColorStack.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

class Print;
class PrintObject;
class PrintInstance;
class LayerTools;
class ToolOrdering;

class PrintRegion;
class ExtrusionEntity;
class ExtrusionEntityCollection;

// Object of this class holds information about whether an extrusion is printed immediately
// after a toolchange (as part of infill/perimeter wiping) or not. One extrusion can be a part
// of several copies - this has to be taken into account.
class WipingExtrusions
{
public:
    bool is_anything_overridden() const {   // if there are no overrides, all the agenda can be skipped - this function can tell us if that's the case
        return m_something_overridden;
    }

    // When allocating extruder overrides of an object's ExtrusionEntity, overrides for maximum 3 copies are allocated in place.
    using ExtruderPerCopy =
#ifdef NDEBUG
        boost::container::small_vector<int32_t, 3>;
#else // NDEBUG
        std::vector<int32_t>;
#endif // NDEBUG

    // This is called from GCode::process_layer_single_object()
    // Returns positive number if the extruder is overridden.
    // Returns -1 if not.
    int get_extruder_override(const ExtrusionEntity* entity, uint32_t instance_id) const {
        auto entity_map_it = m_entity_map.find(entity);
        return entity_map_it == m_entity_map.end() ? -1 : entity_map_it->second[instance_id];
    }

    // This function goes through all infill entities, decides which ones will be used for wiping and
    // marks them by the extruder id. Returns volume that remains to be wiped on the wipe tower:
    float mark_wiping_extrusions(const Print& print, const LayerTools& lt, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe);

    void ensure_perimeters_infills_order(const Print& print, const LayerTools& lt);

    void set_something_overridable() { m_something_overridable = true; }

private:
    // This function is called from mark_wiping_extrusions and sets extruder that it should be printed with (-1 .. as usual)
    void set_extruder_override(const ExtrusionEntity* entity, size_t copy_id, int extruder, size_t num_of_copies);

    // Returns true in case that entity is not printed with its usual extruder for a given copy:
    bool is_entity_overridden(const ExtrusionEntity* entity, size_t copy_id) const {
        auto it = m_entity_map.find(entity);
        return it == m_entity_map.end() ? false : it->second[copy_id] != -1;
    }

    std::map<const ExtrusionEntity*, ExtruderPerCopy> m_entity_map;  // to keep track of who prints what
    bool m_something_overridable = false;
    bool m_something_overridden = false;
};

// Colour stacking: the leaf paths of an extrusion entity
void color_stack_leaves(const ExtrusionEntity &entity, std::vector<const ExtrusionEntity*> &out);

// Colour stacking: one unit's material of one layer, printed in one go (docs/design/COLORSTACK_MODE.md)
struct ColorStackChunk
{
    size_t       layer{0};             // index into layer_tools()
    unsigned int tool{0};
    int          swap_tower_layer{-1}; // a swap to the tool wipes on this layer's tower (layer_tools() index)
    int          finish_from{-1};      // then the tower layers [finish_from, finish_to) are completed
    int          finish_to{-1};
    // the leaf paths it prints, per instance; their pieces in instance coordinates, none: the whole path
    std::map<std::pair<const PrintInstance*, const ExtrusionEntity*>, Polylines> pieces;
    ExPolygons   obstacles;            // material printed before it above its layer, grown by the nozzle's reach
    double       obstacle_top{0.};     // print coordinates and z of that material
    int          covered_by{-1};       // the first chunk printed on it (index into color_stack()), -1: none
};

class LayerTools
{
public:
    // Changing these operators to epsilon version can make a problem in cases where support and object layers get close to each other.
    // In case someone tries to do it, make sure you know what you're doing and test it properly (slice multiple objects at once with supports).
    bool operator< (const LayerTools &rhs) const { return print_z < rhs.print_z; }
    bool operator==(const LayerTools &rhs) const { return print_z == rhs.print_z; }

    bool is_extruder_order(unsigned int a, unsigned int b) const;
    bool has_extruder(unsigned int extruder) const { return std::find(this->extruders.begin(), this->extruders.end(), extruder) != this->extruders.end(); }

    // Return a zero based extruder from the region, or extruder_override if overriden.
    unsigned int perimeter_extruder(const PrintRegion &region) const;
    unsigned int infill_extruder(const PrintRegion &region) const;
    unsigned int solid_infill_extruder(const PrintRegion &region) const;
    // Orca's feature filaments (0 = the wall / solid infill extruder)
    unsigned int outer_wall_extruder(const PrintRegion &region) const;
    unsigned int top_surface_extruder(const PrintRegion &region) const;
    unsigned int bottom_surface_extruder(const PrintRegion &region) const;
	// Returns a zero based extruder this eec should be printed with, according to PrintRegion config or extruder_override if overriden.
	unsigned int extruder(const ExtrusionEntityCollection &extrusions, const PrintRegion &region) const;

    double 					print_z	= 0.;
    bool 						has_object = false;
    bool						has_support = false;
    // Zero based extruder IDs, ordered to minimize tool switches.
    std::vector<unsigned int> 	extruders;
    // If per layer extruder switches are inserted by the G-code preview slider, this value contains the new (1 based) extruder, with which the whole object layer is being printed with.
    // If not overriden, it is set to 0.
    unsigned int 				extruder_override = 0;
    // Colour stacking: this LayerTools prints one chunk (extruders: its tool only)
    const ColorStackChunk      *color_stack = nullptr;
    // For multi-extruder printers, when there is a color change, this contains an extruder (1 based) on which the color change will be performed.
    // Otherwise, it is set to 0.
    unsigned int                extruder_needed_for_color_changer = 0;
    // Should a skirt be printed at this layer?
    // Layers are marked for infinite skirt aka draft shield. Not all the layers have to be printed.
    bool                        has_skirt = false;
    // Will there be anything extruded on this layer for the wipe tower?
    // Due to the support layers possibly interleaving the object layers,
    // wipe tower will be disabled for some support only layers.
    bool 						has_wipe_tower = false;
    // Number of wipe tower partitions to support the required number of tool switches
    // and to support the wipe tower partitions above this one.
    size_t                      wipe_tower_partitions = 0;
    double 					wipe_tower_layer_height = 0.;
    // Custom G-code (color change, extruder switch, pause) to be performed before this layer starts to print.
    const Domain::CustomGCode::Item    *custom_gcode = nullptr;

    WipingExtrusions&       wiping_extrusions_nonconst() { return m_wiping_extrusions; }
    const WipingExtrusions& wiping_extrusions() const    { return m_wiping_extrusions; }

private:
    // to access LayerTools private constructor
    friend class ToolOrdering;
    LayerTools(const double z) : print_z(z) {}

    // This object holds list of extrusion that will be used for extruder wiping
    WipingExtrusions m_wiping_extrusions;
};

class ToolOrdering
{
public:
    ToolOrdering() = default;

    // For the use case when each object is printed separately
    // (print.config.complete_objects is true).
    ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material = false);

    // For the use case when all objects are printed at once.
    // (print.config.complete_objects is false).
    ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material = false);

    void 				clear() { m_layer_tools.clear(); }

    // Only valid for non-sequential print:
	// Assign a pointer to a custom G-code to the respective ToolOrdering::LayerTools.
	// Ignore color changes, which are performed on a layer and for such an extruder, that the extruder will not be printing above that layer.
	// If multiple events are planned over a span of a single layer, use the last one.
	void 				assign_custom_gcodes(const Print &print);

    // Get the first extruder printing, including the extruder priming areas, returns -1 if there is no layer printed.
    unsigned int   		first_extruder() const { return m_first_printing_extruder; }

    // Get the first extruder printing the layer_tools, returns -1 if there is no layer printed.
    unsigned int   		last_extruder() const { return m_last_printing_extruder; }

    // For a multi-material print, the printing extruders are ordered in the order they shall be primed.
    const std::vector<unsigned int>& all_extruders() const { return m_all_printing_extruders; }

    // Find LayerTools with the closest print_z.
    const LayerTools&	tools_for_layer(double print_z) const;
    LayerTools&			tools_for_layer(double print_z) { return const_cast<LayerTools&>(std::as_const(*this).tools_for_layer(print_z)); }

    const LayerTools&   front()       const { return m_layer_tools.front(); }
    const LayerTools&   back()        const { return m_layer_tools.back(); }
    std::vector<LayerTools>::const_iterator begin() const { return m_layer_tools.begin(); }
    std::vector<LayerTools>::const_iterator end()   const { return m_layer_tools.end(); }
    bool 				empty()       const { return m_layer_tools.empty(); }
    std::vector<LayerTools>& layer_tools() { return m_layer_tools; }
    bool 				has_wipe_tower() const { return ! m_layer_tools.empty() && m_first_printing_extruder != (unsigned int)-1 && m_layer_tools.front().wipe_tower_partitions > 0; }
    int                 toolchanges_count() const;

    // Colour stacking (docs/design/COLORSTACK_MODE.md): when the print has it on, plans it and rewrites the layers'
    // extruders to the swaps each tower layer takes, so the wipe tower plans the real swaps only. Returns whether
    // it was planned.
    bool                apply_color_stacking(const Print &print);
    // Whether colour stacking applies to the print (on, one nozzle, several filaments, not sequential, no vase).
    static bool         color_stacking_enabled(const Print &print);
    // The chunks in print order; empty without colour stacking.
    const std::vector<ColorStackChunk>& color_stack() const { return m_color_stack; }

private:
    void				initialize_layers(std::vector<double> &zs);
    void 				collect_extruders(const PrintObject &object, const std::vector<std::pair<double, unsigned int>> &per_layer_extruder_switches, const std::vector<std::pair<double, unsigned int>> &per_layer_color_changes);
    void				reorder_extruders(unsigned int last_extruder_id);
    void 				fill_wipe_tower_partitions(const Domain::ConfigView &config, double object_bottom_z, double max_layer_height);
    bool                insert_wipe_tower_extruder();
    void                mark_skirt_layers(const Domain::ConfigView &config, double max_layer_height);
    void 				collect_extruder_statistics(bool prime_multi_material);

    std::vector<LayerTools>    m_layer_tools;
    std::vector<ColorStackChunk> m_color_stack;
    bool                       m_prime_multi_material = false;
    // First printing extruder, including the multi-material priming sequence.
    unsigned int               m_first_printing_extruder = (unsigned int)-1;
    // Final printing extruder.
    unsigned int               m_last_printing_extruder  = (unsigned int)-1;
    // All extruders, which extrude some material over m_layer_tools.
    std::vector<unsigned int>  m_all_printing_extruders;

    const PrintConfigView*         m_print_config_ptr = nullptr;
};

} // namespace SLic3r

#endif /* slic3r_ToolOrdering_hpp_ */
