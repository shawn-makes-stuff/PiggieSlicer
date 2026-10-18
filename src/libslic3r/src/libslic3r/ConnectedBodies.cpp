#include "libslic3r/ConnectedBodies.hpp"

#include <limits>

#include "Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {

std::vector<std::vector<size_t>> connected_bodies(const std::vector<const ExPolygons *> &layers, size_t &count,
                                                  const std::function<void()> &throw_if_canceled)
{
    // Union-find over the islands of all layers, numbered layer after layer.
    std::vector<size_t> first(layers.size() + 1, 0);
    for (size_t l = 0; l < layers.size(); ++l)
        first[l + 1] = first[l] + layers[l]->size();
    std::vector<size_t> parent(first.back());
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = i;
    const auto find = [&parent](size_t i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };

    std::vector<std::vector<BoundingBox>> boxes(layers.size());
    for (size_t l = 0; l < layers.size(); ++l)
        for (const ExPolygon &island : *layers[l])
            boxes[l].emplace_back(get_extents(island));
    for (size_t l = 0; l + 1 < layers.size(); ++l) {
        if (throw_if_canceled)
            throw_if_canceled();
        // Index the smaller of the two layers, so that a fragmented layer is not scanned island by island.
        size_t a_layer = l;
        size_t b_layer = l + 1;
        if (layers[a_layer]->size() < layers[b_layer]->size())
            std::swap(a_layer, b_layer);
        if (layers[b_layer]->empty())
            continue;
        using IslandTree = Biz::Algorithms::AABBTreeIndirect::Tree<2, coord_t>;
        std::vector<Biz::Algorithms::AABBTreeIndirect::BoundingBoxWrapper> wrappers;
        wrappers.reserve(boxes[b_layer].size());
        for (size_t b = 0; b < boxes[b_layer].size(); ++b)
            wrappers.emplace_back(b, boxes[b_layer][b]);
        IslandTree tree;
        tree.build_modify_input(wrappers);
        for (size_t a = 0; a < boxes[a_layer].size(); ++a) {
            const IslandTree::BoundingBox query(boxes[a_layer][a].min, boxes[a_layer][a].max);
            Biz::Algorithms::AABBTreeIndirect::traverse(
                tree, [&query](const IslandTree::Node &node) { return node.bbox.intersects(query); },
                [&](const IslandTree::Node &node) {
                    // The tree's boxes are widened by an epsilon, and islands already joined need no clipping.
                    const size_t b = node.idx;
                    if (boxes[a_layer][a].overlap(boxes[b_layer][b]) && find(first[a_layer] + a) != find(first[b_layer] + b) &&
                        !intersection_ex(ExPolygons{ (*layers[a_layer])[a] }, ExPolygons{ (*layers[b_layer])[b] }).empty())
                        parent[find(first[a_layer] + a)] = find(first[b_layer] + b);
                    return true;
                });
        }
    }

    std::vector<size_t>              body(parent.size(), std::numeric_limits<size_t>::max());
    std::vector<std::vector<size_t>> out(layers.size());
    count = 0;
    for (size_t l = 0; l < layers.size(); ++l)
        for (size_t i = 0; i < layers[l]->size(); ++i) {
            size_t &b = body[find(first[l] + i)];
            if (b == std::numeric_limits<size_t>::max())
                b = count++;
            out[l].emplace_back(b);
        }
    return out;
}

} // namespace Slic3r
