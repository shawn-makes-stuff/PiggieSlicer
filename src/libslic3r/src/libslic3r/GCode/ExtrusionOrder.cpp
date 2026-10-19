#include "ExtrusionOrder.hpp"

#include <algorithm>
#include <cassert>
#include <cinttypes>

#include "libslic3r/GCode/SmoothPath.hpp"
#include "libslic3r/GCode/OrderingStrategies.hpp"

#include <set>
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/GCode/WipeTowerIntegration.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"

namespace Slic3r::GCode::ExtrusionOrder {

bool is_overriden(const ExtrusionEntityCollection &eec, const LayerTools &layer_tools, const std::size_t instance_id) {
    return layer_tools.wiping_extrusions().get_extruder_override(&eec, instance_id) > -1;
}

int get_extruder_id(
    const ExtrusionEntityCollection &eec,
    const LayerTools &layer_tools,
    const PrintRegion &region,
    const std::size_t instance_id
) {
    if (is_overriden(eec, layer_tools, instance_id)) {
        return layer_tools.wiping_extrusions().get_extruder_override(&eec, instance_id);
    }

    const int extruder_id = layer_tools.extruder(eec, region);
    if (! layer_tools.has_extruder(extruder_id)) {
        // Extruder is not in layer_tools - we'll print it by last extruder on this layer (could
        // happen e.g. when a wiping object is taller than others - dontcare extruders are
        // eradicated from layer_tools)
        return layer_tools.extruders.back();
    }
    return extruder_id;
}

Point get_gcode_point(const InstancePoint &point, const Point &offset) {
    return point.local_point + offset;
}

InstancePoint get_instance_point(const Point &point, const Point &offset) {
    return {point - offset};
}

std::optional<Point> get_gcode_point(const std::optional<InstancePoint> &point, const Point &offset) {
    if (point) {
        return get_gcode_point(*point, offset);
    }
    return std::nullopt;
}

std::optional<InstancePoint> get_instance_point(const std::optional<Point> &point, const Point &offset) {
    if (point) {
        return get_instance_point(*point, offset);
    }
    return std::nullopt;
}

// Orca Skirt::find_start_point: the point at start_angle (degrees) on the circle around the loop's bounding box
// center through its corner
static Point skirt_start_point(const ExtrusionEntity &loop, const double start_angle)
{
    Points points;
    loop.collect_points(points);
    const BoundingBox bbox   = get_extents(points);
    const Vec2d       center = (bbox.min.cast<double>() + bbox.max.cast<double>()) / 2.;
    const double      r      = (bbox.min.cast<double>() - center).norm();
    const double      angle  = start_angle * M_PI / 180.;
    return (center + r * Vec2d(std::cos(angle), std::sin(angle))).cast<coord_t>();
}

using ExtractEntityPredicate = std::function<bool(const ExtrusionEntityCollection&, const PrintRegion&)>;

ExtrusionEntitiesPtr extract_infill_extrusions(
    const PrintRegion &region,
    const ExtrusionEntityCollection &fills,
    const LayerExtrusionRanges::const_iterator& begin,
    const LayerExtrusionRanges::const_iterator& end,
    const ExtractEntityPredicate &should_pick_extrusion
) {
    ExtrusionEntitiesPtr result;
    for (auto it = begin; it != end; ++ it) {
        assert(it->region() == begin->region());
        const LayerExtrusionRange &range{*it};
        for (uint32_t fill_id : range) {
            assert(dynamic_cast<ExtrusionEntityCollection*>(fills.entities[fill_id]));

            auto *eec{static_cast<ExtrusionEntityCollection*>(fills.entities[fill_id])};
            if (eec == nullptr || eec->empty() || !should_pick_extrusion(*eec, region)) {
                continue;
            }

            if (eec->can_reverse()) {
                // Flatten the infill collection for better path planning.
                for (auto *ee : eec->entities) {
                    result.emplace_back(ee);
                }
            } else {
                result.emplace_back(eec);
            }
        }
    }
    return result;
}

std::vector<Perimeter> extract_perimeter_extrusions(
    const Print &print,
    const Layer &layer,
    const LayerIsland &island,
    const ExtractEntityPredicate &should_pick_extrusion,
    const unsigned extruder_id,
    const Point &offset,
    std::optional<Point> &previous_position,
    const PathSmoothingFunction &smooth_path
) {
    std::vector<Perimeter> result;

    const LayerRegion &layerm = *layer.get_region(island.perimeters.region());
    const PrintRegion &region = print.get_print_region(layerm.region().print_region_id());

    for (uint32_t perimeter_id : island.perimeters) {
        // Extrusions inside islands are expected to be ordered already.
        // Don't reorder them.
        assert(dynamic_cast<ExtrusionEntityCollection*>(layerm.perimeters().entities[perimeter_id]));
        auto *eec = static_cast<ExtrusionEntityCollection*>(layerm.perimeters().entities[perimeter_id]);
        if (eec == nullptr || eec->empty() || !should_pick_extrusion(*eec, region)) {
            continue;
        }

        for (ExtrusionEntity *ee : *eec) {
            if (ee != nullptr) {
                std::optional<InstancePoint> last_position{get_instance_point(previous_position, offset)};
                bool reverse_loop{false};
                if (auto loop = dynamic_cast<const ExtrusionLoop *>(ee)) {
                    const bool is_hole = loop->is_clockwise();
                    reverse_loop = print.config().get<bool>("prefer_clockwise_movements") ? !is_hole : is_hole;
                    // Orca overhang_reverse
                    if (loop->reverse_direction)
                        reverse_loop = !reverse_loop;
                }
                auto [path, wipe_offset]{smooth_path(&layer, &region, ExtrusionEntityReference{*ee, reverse_loop}, extruder_id, last_position)};
                previous_position = get_gcode_point(last_position, offset);
                if (!path.empty()) {
                    result.push_back(Perimeter{std::move(path), reverse_loop, ee, wipe_offset});
                }
            }
        }
    }

    return result;
}

std::vector<ExtrusionEntityReference> sort_fill_extrusions(const ExtrusionEntitiesPtr &fills, const Point* start_near) {
    if (fills.empty()) {
        return {};
    }
    std::vector<ExtrusionEntityReference> sorted_extrusions;

    for (const ExtrusionEntityReference &fill : chain_extrusion_references(fills, start_near)) {
        if (auto *eec = dynamic_cast<const ExtrusionEntityCollection*>(&fill.extrusion_entity()); eec) {
            for (const ExtrusionEntityReference &ee : chain_extrusion_references(*eec, start_near, fill.flipped())) {
                sorted_extrusions.push_back(ee);
            }
        } else {
            sorted_extrusions.push_back(fill);
        }
    }
    return sorted_extrusions;
}

std::vector<InfillRange> extract_infill_ranges(
    const Print &print,
    const Layer &layer,
    const LayerIsland &island,
    const Point &offset,
    std::optional<Point> &previous_position,
    const ExtractEntityPredicate &should_pick_extrusion,
    const PathSmoothingFunction &smooth_path,
    const unsigned extruder_id
) {
    std::vector<InfillRange> result;
    for (auto it = island.fills.begin(); it != island.fills.end();) {
        // Gather range of fill ranges with the same region.
        auto it_end = it;
        for (++ it_end; it_end != island.fills.end() && it->region() == it_end->region(); ++ it_end) ;
        const LayerRegion &layerm = *layer.get_region(it->region());
        // PrintObjects own the PrintRegions, thus the pointer to PrintRegion would be unique to a PrintObject, they would not
        // identify the content of PrintRegion accross the whole print uniquely. Translate to a Print specific PrintRegion.
        const PrintRegion &region = print.get_print_region(layerm.region().print_region_id());

        ExtrusionEntitiesPtr extrusions{extract_infill_extrusions(
            region,
            layerm.fills(),
            it,
            it_end,
            should_pick_extrusion
        )};

        const std::optional<InstancePoint> previous_instance_point{get_instance_point(previous_position, offset)};
        const Point* start_near{previous_instance_point ? &(previous_instance_point->local_point) : nullptr};
        const ExtrusionEntityReferences sorted_extrusions{sort_fill_extrusions(extrusions, start_near)};

        std::vector<SmoothPath> paths;
        for (const ExtrusionEntityReference &extrusion_reference : sorted_extrusions) {
            std::optional<InstancePoint> last_position{get_instance_point(previous_position, offset)};
            auto [path, _]{smooth_path(&layer, &region, extrusion_reference, extruder_id, last_position)};
            if (!path.empty()) {
                paths.push_back(std::move(path));
            }
            previous_position = get_gcode_point(last_position, offset);
        }
        if (!paths.empty()) {
            result.push_back({std::move(paths), &region});
        }
        it = it_end;
    }
    return result;
}

// Returns LayerIslands ordered by the shortest distance.
std::vector<std::reference_wrapper<const LayerIsland>> get_ordered_islands(
    const LayerSlice &lslice,
    const std::optional<Point> &previous_position
) {
    std::vector<std::reference_wrapper<const LayerIsland>> islands_to_order;
    for (const LayerIsland &island : lslice.islands) {
        islands_to_order.emplace_back(island);
    }

    chain_and_reorder_layer_islands(islands_to_order, previous_position.has_value() ? std::addressof(*previous_position) : nullptr);
    return islands_to_order;
}

std::vector<IslandExtrusions> extract_island_extrusions(
    const LayerSlice &lslice,
    const Print &print,
    const Layer &layer,
    const ExtractEntityPredicate &should_pick_extrusion,
    const PathSmoothingFunction &smooth_path,
    const Point &offset,
    const unsigned extruder_id,
    std::optional<Point> &previous_position
) {
    const auto should_pick_infill = [&should_pick_extrusion](const ExtrusionEntityCollection &eec, const PrintRegion &region) {
        return should_pick_extrusion(eec, region) && eec.role() != ExtrusionRole::Ironing;
    };

    std::vector<std::reference_wrapper<const LayerIsland>> ordered_islands = get_ordered_islands(lslice, previous_position);

    std::vector<IslandExtrusions> result;
    for (const LayerIsland &island : ordered_islands) {
        const LayerRegion &layerm = *layer.get_region(island.perimeters.region());
        // PrintObjects own the PrintRegions, thus the pointer to PrintRegion would be
        // unique to a PrintObject, they would not identify the content of PrintRegion
        // accross the whole print uniquely. Translate to a Print specific PrintRegion.
        const PrintRegion &region = print.get_print_region(layerm.region().print_region_id());

        result.push_back(IslandExtrusions{&region});
        IslandExtrusions &island_extrusions{result.back()};
        island_extrusions.infill_first = print.config().get<bool>("infill_first");

        if (print.config().get<bool>("infill_first")) {
            island_extrusions.infill_ranges = extract_infill_ranges(
                print, layer, island, offset, previous_position, should_pick_infill, smooth_path, extruder_id
            );

            island_extrusions.perimeters = extract_perimeter_extrusions(print, layer, island, should_pick_extrusion, extruder_id, offset, previous_position, smooth_path);
        } else {
            island_extrusions.perimeters = extract_perimeter_extrusions(print, layer, island, should_pick_extrusion, extruder_id, offset, previous_position, smooth_path);

            island_extrusions.infill_ranges = extract_infill_ranges(
                print, layer, island, offset, previous_position, should_pick_infill, smooth_path, extruder_id
            );
        }
    }
    return result;
}

std::vector<InfillRange> extract_ironing_extrusions(
    const LayerSlice &lslice,
    const Print &print,
    const Layer &layer,
    const ExtractEntityPredicate &should_pick_extrusion,
    const PathSmoothingFunction &smooth_path,
    const Point &offset,
    const unsigned extruder_id,
    std::optional<Point> &previous_position
) {
    const auto should_pick_ironing = [&should_pick_extrusion](const auto &eec, const auto &region) {
        return should_pick_extrusion(eec, region) && eec.role() == ExtrusionRole::Ironing;
    };

    std::vector<std::reference_wrapper<const LayerIsland>> ordered_islands = get_ordered_islands(lslice, previous_position);

    std::vector<InfillRange> result;
    for (const LayerIsland &island : ordered_islands) {
        const std::vector<InfillRange> ironing_ranges{extract_infill_ranges(
            print, layer, island, offset, previous_position, should_pick_ironing, smooth_path, extruder_id
        )};
        result.insert(
            result.end(), ironing_ranges.begin(), ironing_ranges.end()
        );
    }
    return result;
}

std::vector<SliceExtrusions> get_slices_extrusions(
    const Print &print,
    const Layer &layer,
    const ExtractEntityPredicate &should_pick_extrusion,
    const PathSmoothingFunction &smooth_path,
    const Point &offset,
    const unsigned extruder_id,
    std::optional<Point> &previous_position,
    // the slices to print, all in the print order if none
    const std::vector<size_t> &slice_indices = {}
) {
    // Note: ironing.
    // FIXME move ironing into the loop above over LayerIslands?
    // First Ironing changes extrusion rate quickly, second single ironing may be done over
    // multiple perimeter regions. Ironing in a second phase is safer, but it may be less
    // efficient.

    std::vector<SliceExtrusions> result;

    for (size_t idx : slice_indices.empty() ? layer.lslice_indices_sorted_by_print_order : slice_indices) {
        const LayerSlice &lslice = layer.lslices_ex[idx];
        std::vector<IslandExtrusions> island_extrusions{extract_island_extrusions(
            lslice, print, layer, should_pick_extrusion, smooth_path, offset, extruder_id, previous_position
        )};
        std::vector<InfillRange> ironing_extrusions{extract_ironing_extrusions(
            lslice, print, layer, should_pick_extrusion, smooth_path, offset, extruder_id, previous_position
        )};
        if (!island_extrusions.empty() || !ironing_extrusions.empty()) {
            result.emplace_back(
                SliceExtrusions{std::move(island_extrusions), std::move(ironing_extrusions)}
            );
        }
    }
    return result;
}

unsigned translate_support_extruder(
    const int configured_extruder,
    const LayerTools &layer_tools,
    const std::vector<bool> &is_soluable
) {
    if (configured_extruder <= 0) {
        // Some support will be printed with "don't care" material, preferably non-soluble.
        // Is the current extruder assigned a soluble filament?
        auto it_nonsoluble = std::find_if(layer_tools.extruders.begin(), layer_tools.extruders.end(),
            [&is_soluable](unsigned int extruder_id) { return ! is_soluable.at(extruder_id); });
        // There should be a non-soluble extruder available.
        assert(it_nonsoluble != layer_tools.extruders.end());
        return it_nonsoluble == layer_tools.extruders.end() ? layer_tools.extruders.front() : *it_nonsoluble;
    } else {
        return configured_extruder - 1;
    }
}

std::vector<SupportPath> get_support_extrusions(
    const unsigned int extruder_id,
    const GCode::ObjectLayerToPrint &layer_to_print,
    unsigned int support_extruder,
    unsigned int interface_extruder,
    const PathSmoothingFunction &smooth_path,
    std::optional<Point> &previous_position
) {
    if (const SupportLayer &support_layer = *layer_to_print.support_layer;
        !support_layer.support_fills.entities.empty()) {
        ExtrusionRole role = support_layer.support_fills.role();
        bool has_support = role.is_mixed() || role.is_support_base();
        bool has_interface = role.is_mixed() || role.is_support_interface();

        bool extrude_support = has_support && support_extruder == extruder_id;
        bool extrude_interface = has_interface && interface_extruder == extruder_id;

        if (extrude_support || extrude_interface) {
            ExtrusionEntitiesPtr entities_cache;
            const ExtrusionEntitiesPtr &entities = extrude_support && extrude_interface ?
                support_layer.support_fills.entities :
                entities_cache;
            if (!extrude_support || !extrude_interface) {
                auto role = extrude_support ? ExtrusionRole::SupportMaterial :
                                              ExtrusionRole::SupportMaterialInterface;
                entities_cache.reserve(support_layer.support_fills.entities.size());
                for (ExtrusionEntity *ee : support_layer.support_fills.entities)
                    if (ee->role() == role)
                        entities_cache.emplace_back(ee);
            }
            std::vector<SupportPath> paths;
            for (const ExtrusionEntityReference &entity_reference : chain_extrusion_references(entities)) {
                auto collection{dynamic_cast<const ExtrusionEntityCollection *>(&entity_reference.extrusion_entity())};
                const bool is_interface{entity_reference.extrusion_entity().role() != ExtrusionRole::SupportMaterial};
                if (collection != nullptr) {
                    for (const ExtrusionEntity * sub_entity : *collection) {
                        std::optional<InstancePoint> last_position{get_instance_point(previous_position, {0, 0})};
                        auto [path, _]{smooth_path(nullptr, nullptr, {*sub_entity, entity_reference.flipped()}, extruder_id, last_position)};
                        if (!path.empty()) {
                            paths.push_back({std::move(path), is_interface});
                        }
                        previous_position = get_gcode_point(last_position, {0, 0});
                    }
                } else {
                    std::optional<InstancePoint> last_position{get_instance_point(previous_position, {0, 0})};
                    auto [path, _]{smooth_path(nullptr, nullptr, entity_reference, extruder_id, last_position)};
                    if (!path.empty()) {
                        paths.push_back({std::move(path), is_interface});
                    }
                    previous_position = get_gcode_point(last_position, {0, 0});
                }
            }
            return paths;
        }
    }
    return {};
}

std::vector<OverridenExtrusions> get_overriden_extrusions(
    const Print &print,
    const GCode::ObjectsLayerToPrint &layers,
    const LayerTools &layer_tools,
    const std::vector<InstanceToPrint> &instances_to_print,
    const unsigned int extruder_id,
    const PathSmoothingFunction &smooth_path,
    std::optional<Point> &previous_position
) {
    std::vector<OverridenExtrusions> result;

    for (const InstanceToPrint &instance : instances_to_print) {
        if (const Layer *layer = layers[instance.object_layer_to_print_id].object_layer; layer) {
            const auto should_pick_extrusion = [&layer_tools, &instance, &extruder_id](const ExtrusionEntityCollection &entity_collection,
                                       const PrintRegion &region) {
                if (!is_overriden(entity_collection, layer_tools, instance.instance_id)) {
                    return false;
                }

                if (get_extruder_id(
                        entity_collection, layer_tools, region, instance.instance_id
                    ) != static_cast<int>(extruder_id)) {
                    return false;
                }
                return true;
            };

            const PrintObject &print_object{instance.print_object};
            const Point offset{print_object.instances()[instance.instance_id].shift()};

            std::vector<SliceExtrusions> slices_extrusions{get_slices_extrusions(
                print, *layer, should_pick_extrusion, smooth_path, offset, extruder_id, previous_position
            )};
            result.push_back({offset, std::move(slices_extrusions)});
        }
    }
    return result;
}

// Whether a slice has anything to print with the predicate.
static bool has_extrusions(const Print &print, const Layer &layer, const LayerSlice &lslice, const ExtractEntityPredicate &should_pick_extrusion)
{
    const auto picks = [&print, &layer, &should_pick_extrusion](const LayerExtrusionRange &range, const ExtrusionEntityCollection &entities) {
        const PrintRegion &region = print.get_print_region(layer.get_region(range.region())->region().print_region_id());
        for (uint32_t idx : range)
            if (const auto *eec = dynamic_cast<const ExtrusionEntityCollection *>(entities.entities[idx]);
                eec != nullptr && ! eec->empty() && should_pick_extrusion(*eec, region))
                return true;
        return false;
    };
    for (const LayerIsland &island : lslice.islands) {
        if (picks(island.perimeters, layer.get_region(island.perimeters.region())->perimeters()))
            return true;
        for (const LayerExtrusionRange &fill : island.fills)
            if (picks(fill, layer.get_region(fill.region())->fills()))
                return true;
    }
    return false;
}

// The extrusions of an instance picked for the extruder.
static ExtractEntityPredicate instance_extrusion_predicate(const LayerTools &layer_tools, const InstanceToPrint &instance, const unsigned int extruder_id)
{
    return [&layer_tools, &instance, extruder_id](const ExtrusionEntityCollection &entity_collection, const PrintRegion &region) {
        if (is_overriden(entity_collection, layer_tools, instance.instance_id)) {
            return false;
        }

        if (get_extruder_id(entity_collection, layer_tools, region, instance.instance_id) != static_cast<int>(extruder_id)) {
            return false;
        }
        return true;
    };
}

// Orca print_order: a visit of an instance, with the slices to print (all in the print order if none) and whether
// it is its first visit in the layer (with the support).
struct InstanceVisit {
    std::size_t instance_index;
    std::vector<std::size_t> slices;
    bool first_visit;
};

// Orca print_order: the instances in their order, or, unless the instances are printed whole (as object list,
// sequential printing), the islands of all the instances in one tour, starting near the wipe tower and reversed to
// end the layer near it. The consecutive islands of an instance make one visit.
static std::vector<InstanceVisit> get_instance_visits(
    const Print &print,
    const GCode::ObjectsLayerToPrint &layers,
    const LayerTools &layer_tools,
    const std::vector<InstanceToPrint> &instances_to_print,
    const unsigned int extruder_id,
    TourCache &tour_cache
) {
    std::vector<InstanceVisit> visits;
    const Domain::PrintOrder print_order = print.config().get<Domain::PrintOrder>("print_order");
    if (print_order == Domain::PrintOrder::AsObjectList || print.config().get<bool>("complete_objects")) {
        for (std::size_t i{0}; i < instances_to_print.size(); ++i)
            visits.push_back({i, {}, true});
        return visits;
    }

    // the islands with something to print, or the whole instance if it has none (support only); at 1 mm so a small
    // drift between the layers keeps the tour
    struct Node {
        std::size_t instance_index;
        std::size_t slice;
        Point       pos;
    };
    const auto quantize_to_mm = [](const Point &pt) -> Point {
        const coord_t grid = coord_t(scale_(1.));
        const auto q = [grid](coord_t v) -> coord_t { return ((v >= 0 ? v + grid / 2 : v - grid / 2) / grid) * grid; };
        return Point(q(pt.x()), q(pt.y()));
    };
    std::vector<Node> nodes;
    for (std::size_t i{0}; i < instances_to_print.size(); ++i) {
        const InstanceToPrint &instance{instances_to_print[i]};
        const Point shift = instance.print_object.instances()[instance.instance_id].shift();
        const size_t first_node = nodes.size();
        if (const Layer *layer = layers[instance.object_layer_to_print_id].object_layer; layer) {
            const ExtractEntityPredicate should_pick_extrusion = instance_extrusion_predicate(layer_tools, instance, extruder_id);
            for (size_t idx : layer->lslice_indices_sorted_by_print_order)
                if (has_extrusions(print, *layer, layer->lslices_ex[idx], should_pick_extrusion))
                    nodes.push_back({i, idx, quantize_to_mm(layer->lslices[idx].contour.centroid() + shift)});
        }
        if (nodes.size() == first_node && layers[instance.object_layer_to_print_id].support_layer != nullptr)
            nodes.push_back({i, size_t(-1), quantize_to_mm(shift)});
    }

    Points node_points;
    node_points.reserve(nodes.size());
    for (const Node &node : nodes)
        node_points.emplace_back(node.pos);
    // Orca builds the wipe tower point from the unscaled position, here it is scaled.
    const std::optional<Domain::ModelWipeTower> wipe_tower = print.wipe_tower();
    const std::optional<Point> wipe_tower_point = wipe_tower ? std::optional<Point>(scaled(wipe_tower->position)) : std::nullopt;
    // the tour depends on the points only (the strategy and the wipe tower are the same for the whole export)
    auto &[cached_points, tour] = tour_cache[extruder_id];
    if (cached_points != node_points || tour.size() != node_points.size()) {
        tour = order_points_with_strategy(node_points, print_order, wipe_tower_point ? &*wipe_tower_point : nullptr);
        std::reverse(tour.begin(), tour.end());
        cached_points = std::move(node_points);
    }

    std::vector<bool> instance_seen(instances_to_print.size(), false);
    for (size_t node_idx : tour) {
        const Node &node = nodes[node_idx];
        if (visits.empty() || visits.back().instance_index != node.instance_index) {
            visits.push_back({node.instance_index, {}, ! instance_seen[node.instance_index]});
            instance_seen[node.instance_index] = true;
        }
        if (node.slice != size_t(-1))
            visits.back().slices.emplace_back(node.slice);
    }
    return visits;
}

// The loops of a skirt (Orca single_loop_draft_shield: above the first layer only the last one), the first loop of
// the first layer starting at skirt_start_angle.
static void append_skirt_paths(
    const Print &print,
    const ExtrusionEntityCollection &skirt,
    const bool is_first_layer,
    const unsigned extruder_id,
    const PathSmoothingFunction &smooth_path,
    std::optional<Point> &previous_position,
    std::vector<std::pair<std::size_t, SmoothPath>> &out
) {
    if (skirt.entities.empty())
        return;
    const size_t first_loop = ! is_first_layer && print.config().get<bool>("single_loop_draft_shield") ? skirt.entities.size() - 1 : 0;
    for (std::size_t i = first_loop; i < skirt.entities.size(); ++i) {
        bool reverse{false};
        if (auto loop = dynamic_cast<const ExtrusionLoop *>(skirt.entities[i])) {
            const bool is_hole = loop->is_clockwise();
            reverse = print.config().get<bool>("prefer_clockwise_movements") ? !is_hole : is_hole;
        }
        const ExtrusionEntityReference entity{*skirt.entities[i], reverse};
        std::optional<InstancePoint> last_position{get_instance_point(previous_position, {0, 0})};
        // Orca skirt_start_angle: the first skirt loop of the first layer starts there
        if (is_first_layer && i == 0)
            last_position = get_instance_point(
                skirt_start_point(*skirt.entities[i], print.config().get<double>("skirt_start_angle")), {0, 0});
        auto [path, _]{smooth_path(nullptr, nullptr, entity, extruder_id, last_position)};
        previous_position = get_gcode_point(last_position, {0, 0});
        out.emplace_back(i, std::move(path));
    }
}

// The paths of a brim.
static void append_brim_paths(
    const Print &print,
    const ExtrusionEntityCollection &brim,
    const unsigned extruder_id,
    const PathSmoothingFunction &smooth_path,
    std::optional<Point> &previous_position,
    std::vector<BrimPath> &out
) {
    for (const ExtrusionEntity *entity : brim.entities) {
        bool reverse{false};
        bool is_loop{false};
        if (auto loop = dynamic_cast<const ExtrusionLoop *>(entity)) {
            const bool is_hole = loop->is_clockwise();
            is_loop = true;
            reverse = print.config().get<bool>("prefer_clockwise_movements") ? !is_hole : is_hole;
        }
        const ExtrusionEntityReference entity_reference{*entity, reverse};
        std::optional<InstancePoint> last_position{get_instance_point(previous_position, {0, 0})};
        auto [path, _]{smooth_path(nullptr, nullptr, entity_reference, extruder_id, last_position)};
        previous_position = get_gcode_point(last_position, {0, 0});
        out.push_back({std::move(path), is_loop});
    }
}

// Orca find_skirt_brim_group_idx
static size_t find_skirt_brim_group(const Print &print, const ObjectInstanceID &instance)
{
    const std::vector<Print::SkirtBrimGroup> &groups = print.skirt_brim_groups();
    for (size_t idx = 0; idx < groups.size(); ++idx)
        if (std::find(groups[idx].instances.begin(), groups[idx].instances.end(), instance) != groups[idx].instances.end())
            return idx;
    return size_t(-1);
}

std::vector<NormalExtrusions> get_normal_extrusions(
    const Print &print,
    const GCode::ObjectsLayerToPrint &layers,
    const LayerTools &layer_tools,
    const std::vector<InstanceToPrint> &instances_to_print,
    const unsigned int extruder_id,
    const PathSmoothingFunction &smooth_path,
    const bool is_first_layer,
    const SkirtGroupDueFunction &skirt_group_due,
    std::set<ObjectInstanceID> &pending_brims,
    TourCache &tour_cache,
    std::optional<Point> &previous_position
) {
    std::vector<NormalExtrusions> result;
    const bool per_object_skirt = print.config().get<Domain::SkirtType>("skirt_type") == Domain::SkirtType::PerObject;

    for (const InstanceVisit &visit : get_instance_visits(print, layers, layer_tools, instances_to_print, extruder_id, tour_cache)) {
        const InstanceToPrint &instance{instances_to_print[visit.instance_index]};
        const PrintObject &print_object = instance.print_object;
        const Point offset = print_object.instances()[instance.instance_id].shift();

        result.emplace_back();
        result.back().instance_index  = visit.instance_index;
        result.back().instance_offset = offset;

        const ObjectInstanceID key{ print_object.id(), instance.instance_id };
        if (const size_t group_idx = visit.first_visit ? find_skirt_brim_group(print, key) : size_t(-1); group_idx != size_t(-1)) {
            const Print::SkirtBrimGroup &group = print.skirt_brim_groups()[group_idx];
            // Orca generate_object_skirt_group: a per object skirt at the first visit of its group
            if (per_object_skirt)
                if (const std::optional<double> skirt_height = skirt_group_due(group_idx)) {
                    result.back().skirt_height = *skirt_height;
                    append_skirt_paths(print, group.skirt, is_first_layer, extruder_id, smooth_path, previous_position, result.back().skirt);
                }
            // Orca generate_object_brim: the brims of the instance on its first layer, once for a combined brim
            if (is_first_layer)
                for (const Print::SkirtBrimGroup::Brim &brim : group.brims)
                    if (std::find(brim.instances.begin(), brim.instances.end(), key) != brim.instances.end() &&
                        std::any_of(brim.instances.begin(), brim.instances.end(), [&pending_brims](const ObjectInstanceID &id) { return pending_brims.count(id) > 0; })) {
                        append_brim_paths(print, brim.brim, extruder_id, smooth_path, previous_position, result.back().brim);
                        for (const ObjectInstanceID &id : brim.instances)
                            pending_brims.erase(id);
                    }
        }

        if (visit.first_visit && layers[instance.object_layer_to_print_id].support_layer != nullptr) {
            result.back().support_extrusions = get_support_extrusions(
                extruder_id,
                layers[instance.object_layer_to_print_id],
                translate_support_extruder(instance.print_object.config().get<int>("support_material_extruder"), layer_tools, print.config().get<std::vector<bool>>("filament_soluble")),
                translate_support_extruder(instance.print_object.config().get<int>("support_material_interface_extruder"), layer_tools, print.config().get<std::vector<bool>>("filament_soluble")),
                smooth_path,
                previous_position
            );
        }

        if (const Layer *layer = layers[instance.object_layer_to_print_id].object_layer; layer) {
            result.back().slices_extrusions = get_slices_extrusions(
                print,
                *layer,
                instance_extrusion_predicate(layer_tools, instance, extruder_id),
                smooth_path,
                offset,
                extruder_id,
                previous_position,
                visit.slices
            );
        }
    }
    return result;
}

bool is_empty(const std::vector<SliceExtrusions> &extrusions) {
    for (const SliceExtrusions &slice_extrusions : extrusions) {
        for (const IslandExtrusions &island_extrusions : slice_extrusions.common_extrusions) {
            if (!island_extrusions.perimeters.empty() || !island_extrusions.infill_ranges.empty()) {
                return false;
            }
        }
        if (!slice_extrusions.ironing_extrusions.empty()) {
            return false;
        }
    }
    return true;
}

bool is_empty(const ExtruderExtrusions &extruder_extrusions) {
    for (const OverridenExtrusions &overriden_extrusions : extruder_extrusions.overriden_extrusions) {
        if (!is_empty(overriden_extrusions.slices_extrusions)) {
            return false;
        }
    }
    for (const NormalExtrusions &normal_extrusions : extruder_extrusions.normal_extrusions) {
        if (!normal_extrusions.skirt.empty() || !normal_extrusions.brim.empty()) {
            return false;
        }
        if (!normal_extrusions.support_extrusions.empty()) {
            return false;
        }
        if (!is_empty(normal_extrusions.slices_extrusions)) {
            return false;
        }
    }
    return true;
}


std::vector<ExtruderExtrusions> get_extrusions(
    const Print &print,
    const GCode::WipeTowerIntegration *wipe_tower,
    const GCode::ObjectsLayerToPrint &layers,
    const bool is_first_layer,
    const LayerTools &layer_tools,
    const std::vector<InstanceToPrint> &instances_to_print,
    const SkirtGroupDueFunction &skirt_group_due,
    std::set<ObjectInstanceID> &pending_brims,
    TourCache &tour_cache,
    unsigned current_extruder_id,
    const PathSmoothingFunction &smooth_path,
    std::optional<Point> previous_position
) {
    unsigned toolchange_number{0};

    std::vector<ExtruderExtrusions> extrusions;
    for (const unsigned int extruder_id : layer_tools.extruders)
    {
        ExtruderExtrusions extruder_extrusions{extruder_id};

        if (layer_tools.has_wipe_tower && wipe_tower != nullptr) {
            const bool finish_wipe_tower{extruder_id == layer_tools.extruders.back()};
            if (finish_wipe_tower || is_toolchange_required(is_first_layer, layer_tools.extruders.back(), extruder_id, current_extruder_id)) {
                const bool ignore_sparse{print.config().get<bool>("wipe_tower_no_sparse_layers")};
                if (const auto tool_change{wipe_tower->get_toolchange(toolchange_number, ignore_sparse)}) {
                    toolchange_number++;
                    previous_position = scaled(wipe_tower->transform_wt_pt(tool_change->end_pos));
                    current_extruder_id = tool_change->new_tool;
                    extruder_extrusions.wipe_tower_start = scaled(wipe_tower->transform_wt_pt(tool_change->start_pos));
                }
            }
        }


        // Orca: the combined skirts, with the first extruder of the layer on the layers with a skirt
        if (extruder_id == layer_tools.extruders.front() && layer_tools.has_skirt &&
            print.config().get<Domain::SkirtType>("skirt_type") == Domain::SkirtType::Combined)
            for (size_t group_idx = 0; group_idx < print.skirt_brim_groups().size(); ++group_idx)
                if (const std::optional<double> skirt_height = skirt_group_due(group_idx)) {
                    extruder_extrusions.skirt_height = *skirt_height;
                    append_skirt_paths(print, print.skirt_brim_groups()[group_idx].skirt, is_first_layer, extruder_id, smooth_path,
                                       previous_position, extruder_extrusions.skirt);
                }

        using GCode::ExtrusionOrder::get_overriden_extrusions;
        bool is_anything_overridden = layer_tools.wiping_extrusions().is_anything_overridden();
        if (is_anything_overridden) {
            extruder_extrusions.overriden_extrusions = get_overriden_extrusions(
                print, layers, layer_tools, instances_to_print, extruder_id, smooth_path,
                previous_position
            );
        }

        using GCode::ExtrusionOrder::get_normal_extrusions;
        extruder_extrusions.normal_extrusions = get_normal_extrusions(
            print, layers, layer_tools, instances_to_print, extruder_id, smooth_path,
            is_first_layer, skirt_group_due, pending_brims, tour_cache, previous_position
        );

        extrusions.push_back(std::move(extruder_extrusions));
    }
    return extrusions;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const SmoothPath &path) {
    for (const SmoothPathElement & element : path) {
        if (!element.path.empty()) {
            return element.path.front();
        }
    }
    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<SmoothPath> &smooth_paths) {
    for (const SmoothPath &path : smooth_paths) {
        if (auto result = get_first_point(path)) {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<InfillRange> &infill_ranges) {
    for (const InfillRange &infill_range : infill_ranges) {
        if (auto result = get_first_point(infill_range.items)) {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<Perimeter> &perimeters) {
    for (const Perimeter &perimeter : perimeters) {
        if (auto result = get_first_point(perimeter.smooth_path)) {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<IslandExtrusions> &extrusions) {
    for (const IslandExtrusions &island : extrusions) {
        if (island.infill_first) {
            if (auto result = get_first_point(island.infill_ranges)) {
                return result;
            }
            if (auto result = get_first_point(island.perimeters)) {
                return result;
            }
        } else {
            if (auto result = get_first_point(island.perimeters)) {
                return result;
            }
            if (auto result = get_first_point(island.infill_ranges)) {
                return result;
            }
        }
    }
    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<SliceExtrusions> &extrusions) {
    for (const SliceExtrusions &slice : extrusions) {
        if (auto result = get_first_point(slice.common_extrusions)) {
            return result;
        }
    }
    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const ExtruderExtrusions &extrusions) {
    for (const auto&[_, path] : extrusions.skirt) {
        if (auto result = get_first_point(path)) {
            return result;
        };
    }
    for (const OverridenExtrusions &overriden_extrusions : extrusions.overriden_extrusions) {
        if (auto result = get_first_point(overriden_extrusions.slices_extrusions)) {
            result->point += overriden_extrusions.instance_offset;
            return result;
        }
    }

    for (const NormalExtrusions &normal_extrusions : extrusions.normal_extrusions) {
        // the per object skirt and the brims are in the print coordinates
        for (const auto&[_, path] : normal_extrusions.skirt) {
            if (auto result = get_first_point(path)) {
                return result;
            }
        }
        for (const BrimPath &brim_path : normal_extrusions.brim) {
            if (auto result = get_first_point(brim_path.path)) {
                return result;
            }
        }
        for (const SupportPath &support_path : normal_extrusions.support_extrusions) {
            if (auto result = get_first_point(support_path.path)) {
                result->point += normal_extrusions.instance_offset;
                return result;
            }
        }
        if (auto result = get_first_point(normal_extrusions.slices_extrusions)) {
            result->point += normal_extrusions.instance_offset;
            return result;
        }
    }

    return std::nullopt;
}

std::optional<Geometry::ArcWelder::Segment> get_first_point(const std::vector<ExtruderExtrusions> &extrusions) {
    if (extrusions.empty()) {
        return std::nullopt;
    }

    if (extrusions.front().wipe_tower_start) {
        return {{*(extrusions.front().wipe_tower_start)}};
    }

    for (const ExtruderExtrusions &extruder_extrusions : extrusions) {
        if (auto result = get_first_point(extruder_extrusions)) {
            return result;
        }
    }

    return std::nullopt;
}

const PrintInstance * get_first_instance(
    const std::vector<ExtruderExtrusions> &extrusions,
    const std::vector<InstanceToPrint> &instances_to_print
) {
    if (instances_to_print.empty()) {
        return nullptr;
    }

    for (const ExtruderExtrusions &extruder_extrusions : extrusions) {
        if (!extruder_extrusions.overriden_extrusions.empty()) {
            for (std::size_t i{0}; i < instances_to_print.size(); ++i) {
                const OverridenExtrusions &overriden_extrusions{extruder_extrusions.overriden_extrusions[i]};
                if (!is_empty(overriden_extrusions.slices_extrusions)) {
                    const InstanceToPrint &instance{instances_to_print[i]};
                    return &instance.print_object.instances()[instance.instance_id];
                }
            }
        }
        for (const NormalExtrusions &normal_extrusions : extruder_extrusions.normal_extrusions) {
            const InstanceToPrint &instance{instances_to_print[normal_extrusions.instance_index]};
            if (!normal_extrusions.skirt.empty() || !normal_extrusions.brim.empty() ||
                !normal_extrusions.support_extrusions.empty() || !is_empty(normal_extrusions.slices_extrusions)) {
                return &instance.print_object.instances()[instance.instance_id];
            }
        }
    }
    const InstanceToPrint &instance{instances_to_print.front()};
    return &instance.print_object.instances()[instance.instance_id];
}

}
