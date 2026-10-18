#include <Slic3r/Log.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <atomic>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <cassert>
#include <cstddef>

#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "libslic3r/HwConfigUtils.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ElephantFootCompensation.hpp"
#include "libslic3r/I18N_private.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/MultiMaterialSegmentation.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "admesh/stl.h"
#include "libslic3r/Feature/Interlocking/InterlockingGenerator.hpp"
#include "libslic3r/Feature/VirtualExtruder/VirtualExtruder.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Model.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Surface.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"

// DEBUGGING ONLY:
#include <chrono>
#include <fstream>
#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Algorithms/SVG.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

LayerPtrs new_layers(
    PrintObject* print_object,
    // Object layers as LayerZRange boundaries, without the raft.
    const Domain::LayerZRanges& object_layers
)
{
    LayerPtrs out;
    out.reserve(object_layers.size());
    auto     id   = int(print_object->slicing_parameters().raft_layers());
    double zmin = print_object->slicing_parameters().object_print_z_min;
    Layer   *prev = nullptr;

    for (const Domain::LayerZRange& layer_z_range : object_layers) {
        const double top_z   = layer_z_range.top_z;
        const double slice_z = layer_z_range.middle_z();
        Layer* layer = new Layer(id++, print_object, layer_z_range.height(), top_z + zmin, slice_z);
        out.emplace_back(layer);
        if (prev != nullptr) {
            prev->upper_layer = layer;
            layer->lower_layer = prev;
        }

        prev = layer;
    }

    return out;
}

// Slice single triangle mesh.
static std::vector<ExPolygons> slice_volume(
    const Domain::ModelVolume     &volume,
    const std::vector<float>      &zs, 
    const MeshSlicingParamsEx     &params,
    const std::function<void()>   &throw_on_cancel_callback)
{
    std::vector<ExPolygons> layers;
    if (! zs.empty()) {
        indexed_triangle_set its = volume.mesh().its;
        if (its.indices.size() > 0) {
            MeshSlicingParamsEx params2 { params };
            params2.trafo = params2.trafo * volume.get_matrix();
            if (params2.trafo.rotation().determinant() < 0.)
                Domain::its_flip_triangles(its);
            layers = slice_mesh_ex(its, zs, params2, throw_on_cancel_callback);
            throw_on_cancel_callback();
        }
    }
    return layers;
}

// Slice single triangle mesh.
// Filter the zs not inside the ranges. The ranges are closed at the bottom and open at the top, they are sorted lexicographically and non overlapping.
static std::vector<ExPolygons> slice_volume(
    const Domain::ModelVolume                   &volume,
    const std::vector<float>                    &z,
    const std::vector<Domain::LayerHeightRange> &ranges,
    const MeshSlicingParamsEx                   &params,
    const std::function<void()>                 &throw_on_cancel_callback)
{
    std::vector<ExPolygons> out;
    if (! z.empty() && ! ranges.empty()) {
        if (ranges.size() == 1 && z.front() >= ranges.front().first && z.back() < ranges.front().second) {
            // All layers fit into a single range.
            out = slice_volume(volume, z, params, throw_on_cancel_callback);
        } else {
            std::vector<float>                     z_filtered;
            std::vector<std::pair<size_t, size_t>> n_filtered;
            z_filtered.reserve(z.size());
            n_filtered.reserve(2 * ranges.size());
            size_t i = 0;
            for (const Domain::LayerHeightRange &range : ranges) {
                for (; i < z.size() && z[i] < range.first; ++ i) ;
                size_t first = i;
                for (; i < z.size() && z[i] < range.second; ++ i)
                    z_filtered.emplace_back(z[i]);
                if (i > first)
                    n_filtered.emplace_back(std::make_pair(first, i));
            }
            if (! n_filtered.empty()) {
                std::vector<ExPolygons> layers = slice_volume(volume, z_filtered, params, throw_on_cancel_callback);
                out.assign(z.size(), ExPolygons());
                i = 0;
                for (const std::pair<size_t, size_t> &span : n_filtered)
                    for (size_t j = span.first; j < span.second; ++ j)
                        out[j] = std::move(layers[i ++]);
            }
        }
    }
    return out;
}

struct VolumeSlices
{
    Domain::ObjectID        volume_id;
    std::vector<ExPolygons> slices;
};

static inline bool model_volume_needs_slicing(const Domain::ModelVolume &mv)
{
    Domain::ModelVolumeType type = mv.type();
    return type == Domain::ModelVolumeType::MODEL_PART || type == Domain::ModelVolumeType::NEGATIVE_VOLUME || type == Domain::ModelVolumeType::PARAMETER_MODIFIER;
}

// Slice printable volumes, negative volumes and modifier volumes, sorted by ModelVolume::id().
// Apply closing radius.
// Apply positive XY compensation to ModelVolumeType::MODEL_PART and ModelVolumeType::PARAMETER_MODIFIER, not to ModelVolumeType::NEGATIVE_VOLUME.
// Apply contour simplification.
static std::vector<VolumeSlices> slice_volumes_inner(
    const PrintConfigView                                    &print_config,
    const PrintObjectConfigView                              &print_object_config,
    const Transform3d                                        &object_trafo,
    Domain::ModelVolumePtrs                                   model_volumes,
    const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges,
    const std::vector<float>                                 &zs,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<VolumeSlices> out;
    out.reserve(model_volumes.size());

    std::vector<Domain::LayerHeightRange> slicing_ranges;
    if (layer_ranges.size() > 1)
        slicing_ranges.reserve(layer_ranges.size());

    MeshSlicingParamsEx params_base;
    params_base.closing_radius = print_object_config.get<double>("slice_closing_radius");
    params_base.extra_offset   = 0;
    params_base.trafo          = object_trafo;
    params_base.resolution     = print_config.get<double>("resolution");

    switch (print_object_config.get<Domain::SlicingMode>("slicing_mode")) {
    case Domain::SlicingMode::Regular:    params_base.mode = MeshSlicingParams::SlicingMode::Regular; break;
    case Domain::SlicingMode::EvenOdd:    params_base.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    case Domain::SlicingMode::CloseHoles: params_base.mode = MeshSlicingParams::SlicingMode::Positive; break;
    }

    params_base.mode_below     = params_base.mode;

    const size_t num_extruders = print_config.hw_config().material_slot_count();
    const bool   is_mm_painted = num_extruders > 1 && std::any_of(model_volumes.cbegin(), model_volumes.cend(), [](const Domain::ModelVolume *mv) { return mv->is_mm_painted(); });
    const auto   extra_offset  = is_mm_painted ? 0.f : std::max(0.f, float(print_object_config.get<double>("xy_size_compensation")));

    for (const Domain::ModelVolume *model_volume : model_volumes)
        if (model_volume_needs_slicing(*model_volume)) {
            MeshSlicingParamsEx params { params_base };
            if (! model_volume->is_negative_volume())
                params.extra_offset = extra_offset;
            if (layer_ranges.size() == 1) {
                if (const PrintObjectRegions::LayerRangeRegions &layer_range = layer_ranges.front(); layer_range.has_volume(model_volume->id())) {
                    if (model_volume->is_model_part() && print_config.get<bool>("spiral_vase")) {
                        auto it = std::find_if(layer_range.volume_regions.begin(), layer_range.volume_regions.end(), 
                            [model_volume](const auto &slice){ return model_volume == slice.model_volume; });
                        params.mode = MeshSlicingParams::SlicingMode::PositiveLargestContour;
                        // Slice the bottom layers with SlicingMode::Regular.
                        // This needs to be in sync with LayerRegion::make_perimeters() spiral_vase!
                        const PrintRegion &region = *it->region;
                        params.slicing_mode_normal_below_layer = size_t(region.extruder_config_value<int>("bottom_solid_layers", FlowRole::frSolidInfill));
                        for (; params.slicing_mode_normal_below_layer < zs.size() && zs[params.slicing_mode_normal_below_layer] < region.extruder_config_value<double>("bottom_solid_min_thickness", FlowRole::frSolidInfill) - EPSILON;
                            ++ params.slicing_mode_normal_below_layer);
                    }
                    out.push_back({
                        model_volume->id(), 
                        slice_volume(*model_volume, zs, params, throw_on_cancel_callback)
                    });
                }
            } else {
                assert(! print_config.get<bool>("spiral_vase"));
                slicing_ranges.clear();
                for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                    if (layer_range.has_volume(model_volume->id()))
                        slicing_ranges.emplace_back(layer_range.layer_height_range);
                if (! slicing_ranges.empty())
                    out.push_back({ 
                        model_volume->id(), 
                        slice_volume(*model_volume, zs, slicing_ranges, params, throw_on_cancel_callback)
                    });
            }
            if (! out.empty() && out.back().slices.empty())
                out.pop_back();
        }

    return out;
}

static inline VolumeSlices& volume_slices_find_by_id(std::vector<VolumeSlices> &volume_slices, const Domain::ObjectID id)
{
    auto it = lower_bound_by_predicate(volume_slices.begin(), volume_slices.end(), [id](const VolumeSlices &vs) { return vs.volume_id < id; });
    assert(it != volume_slices.end() && it->volume_id == id);
    return *it;
}

static inline bool overlap_in_xy(const PrintObjectRegions::BoundingBox &l, const PrintObjectRegions::BoundingBox &r)
{
    return ! (l.max().x() < r.min().x() || l.min().x() > r.max().x() ||
              l.max().y() < r.min().y() || l.min().y() > r.max().y());
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_first(const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges, double z)
{
    auto  it = lower_bound_by_predicate(layer_ranges.begin(), layer_ranges.end(),
        [z](const PrintObjectRegions::LayerRangeRegions &lr) { return lr.layer_height_range.second < z; });
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    if (z == it->layer_height_range.second)
        if (auto it_next = it; ++ it_next != layer_ranges.end() && it_next->layer_height_range.first == z)
            it = it_next;
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    return it;
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_next(
    const std::vector<PrintObjectRegions::LayerRangeRegions>            &layer_ranges, 
    std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator   it,
    double                                                               z)
{
    for (; it->layer_height_range.second <= z; ++ it)
        assert(it != layer_ranges.end());
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z < it->layer_height_range.second);
    return it;
}

static std::vector<std::vector<ExPolygons>> slices_to_regions(
    Domain::ModelVolumePtrs                                   model_volumes,
    const PrintObjectRegions                                 &print_object_regions,
    const std::vector<float>                                 &zs,
    std::vector<VolumeSlices>                               &&volume_slices,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<std::vector<ExPolygons>> slices_by_region(print_object_regions.all_regions.size(), std::vector<ExPolygons>(zs.size(), ExPolygons()));

    // First shuffle slices into regions if there is no overlap with another region possible, collect zs of the complex cases.
    // This is an optimization to avoid the expensive intersection operation in the parallel loop below.
    std::vector<std::pair<size_t, float>> zs_complex;
    {
        size_t z_idx = 0;
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.first; ++ z_idx) ;
            if (layer_range.volume_regions.empty()) {
            } else if (layer_range.volume_regions.size() == 1) {
                const Domain::ModelVolume *model_volume = layer_range.volume_regions.front().model_volume;
                assert(model_volume != nullptr);
                if (model_volume->is_model_part()) {
                    VolumeSlices &slices_src = volume_slices_find_by_id(volume_slices, model_volume->id());
                    auto         &slices_dst = slices_by_region[layer_range.volume_regions.front().region->print_object_region_id()];
                    for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx)
                        slices_dst[z_idx] = std::move(slices_src.slices[z_idx]);
                }
            } else {
                zs_complex.reserve(zs.size());
                std::vector<int> part_regions_idxs;
                for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx) {
                    float z = zs[z_idx];
                    part_regions_idxs.clear();
                    bool  complex = false;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()) && ! complex; ++ idx_region) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                        if (region.bbox->min().z() <= z && region.bbox->max().z() >= z) {
                            // Test for overlap with already identified printable regions.
                            for (int idx_region2 : part_regions_idxs) {
                                const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                if (overlap_in_xy(*region.bbox, *region2.bbox)) {
                                    complex = true;
                                    break;
                                }
                            }
                            if (! complex && region.model_volume->is_model_part())
                                part_regions_idxs.emplace_back(idx_region);
                        }
                    }
                    if (complex)
                        zs_complex.push_back({ z_idx, z });
                    else {
                        for (int region_idx : part_regions_idxs) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[region_idx];
                            slices_by_region[region.region->print_object_region_id()][z_idx] = std::move(volume_slices_find_by_id(volume_slices, region.model_volume->id()).slices[z_idx]);
                        }
                    }
                }
            }
            throw_on_cancel_callback();
        }
    }

    // Second perform region clipping and assignment in parallel. This is only done
    // for layers which were identified as complex in the first step (regions overlap).
    if (! zs_complex.empty()) {
        std::vector<std::vector<VolumeSlices*>> layer_ranges_regions_to_slices(print_object_regions.layer_ranges.size(), std::vector<VolumeSlices*>());
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[&layer_range - print_object_regions.layer_ranges.data()];
            layer_range_regions_to_slices.reserve(layer_range.volume_regions.size());
            for (const PrintObjectRegions::VolumeRegion &region : layer_range.volume_regions)
                layer_range_regions_to_slices.push_back(&volume_slices_find_by_id(volume_slices, region.model_volume->id()));
        }
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, zs_complex.size()),
            [&slices_by_region, &print_object_regions, &zs_complex, &layer_ranges_regions_to_slices, &throw_on_cancel_callback]
                (const tbb::blocked_range<size_t> &range) {
                float z              = zs_complex[range.begin()].second;
                auto  it_layer_range = layer_range_first(print_object_regions.layer_ranges, z);
                // Per volume_regions slices at this Z height.
                struct RegionSlice { 
                    ExPolygons  expolygons;
                    // Identifier of this region in PrintObjectRegions::all_regions
                    int              region_id;
                    Domain::ObjectID volume_id;
                    bool operator<(const RegionSlice &rhs) const {
                        bool this_empty = this->region_id < 0 || this->expolygons.empty();
                        bool rhs_empty  = rhs.region_id < 0 || rhs.expolygons.empty();
                        // Sort the empty items to the end of the list.
                        // Sort by region_id & volume_id lexicographically.
                        return ! this_empty && (rhs_empty || (this->region_id < rhs.region_id || (this->region_id == rhs.region_id && volume_id < volume_id)));
                    }
                };
                std::vector<RegionSlice> temp_slices;
                for (size_t zs_complex_idx = range.begin(); zs_complex_idx < range.end(); ++ zs_complex_idx) {
                    auto [z_idx, z] = zs_complex[zs_complex_idx];
                    it_layer_range = layer_range_next(print_object_regions.layer_ranges, it_layer_range, z);
                    const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                    {
                        std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[it_layer_range - print_object_regions.layer_ranges.begin()];
                        // Per volume_regions slices at thiz Z height.
                        temp_slices.clear();
                        temp_slices.reserve(layer_range.volume_regions.size());
                        for (VolumeSlices* &slices : layer_range_regions_to_slices) {
                            const PrintObjectRegions::VolumeRegion &volume_region = layer_range.volume_regions[&slices - layer_range_regions_to_slices.data()];
                            temp_slices.push_back({ std::move(slices->slices[z_idx]), volume_region.region ? volume_region.region->print_object_region_id() : -1, volume_region.model_volume->id() });
                        }
                    }
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region)
                        if (! temp_slices[idx_region].expolygons.empty()) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_modifier()) {
                                assert(region.parent > -1);
                                bool next_region_same_modifier = idx_region + 1 < int(temp_slices.size()) && layer_range.volume_regions[idx_region + 1].model_volume == region.model_volume;
                                RegionSlice &parent_slice = temp_slices[region.parent];
                                RegionSlice &this_slice   = temp_slices[idx_region];
                                ExPolygons   source       = std::move(this_slice.expolygons);
                                if (parent_slice.expolygons.empty()) {
                                    this_slice  .expolygons.clear();
                                } else {
                                    this_slice  .expolygons = intersection_ex(parent_slice.expolygons, source);
                                    parent_slice.expolygons = diff_ex        (parent_slice.expolygons, source);
                                }
                                if (next_region_same_modifier)
                                    // To be used in the following iteration.
                                    temp_slices[idx_region + 1].expolygons = std::move(source);
                            } else if (region.model_volume->is_model_part() || region.model_volume->is_negative_volume()) {
                                // Clip every non-zero region preceding it.
                                for (int idx_region2 = 0; idx_region2 < idx_region; ++ idx_region2)
                                    if (! temp_slices[idx_region2].expolygons.empty()) {
                                        if (const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                            ! region2.model_volume->is_negative_volume() && overlap_in_xy(*region.bbox, *region2.bbox))
                                            temp_slices[idx_region2].expolygons = diff_ex(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                    }
                            }
                        }
                    // Sort by region_id, push empty slices to the end.
                    std::sort(temp_slices.begin(), temp_slices.end());
                    // Remove the empty slices.
                    temp_slices.erase(std::find_if(temp_slices.begin(), temp_slices.end(), [](const auto &slice) { return slice.region_id == -1 || slice.expolygons.empty(); }), temp_slices.end());
                    // Merge slices and store them to the output.
                    for (int i = 0; i < int(temp_slices.size());) {
                        // Find a range of temp_slices with the same region_id.
                        int j = i;
                        bool merged = false;
                        ExPolygons &expolygons = temp_slices[i].expolygons;
                        for (++ j; j < int(temp_slices.size()) && temp_slices[i].region_id == temp_slices[j].region_id; ++ j)
                            if (ExPolygons &expolygons2 = temp_slices[j].expolygons; ! expolygons2.empty()) {
                                if (expolygons.empty()) {
                                    expolygons = std::move(expolygons2);
                                } else {
                                    append(expolygons, std::move(expolygons2));
                                    merged = true;
                                }
                            }
                        if (merged)
                            expolygons = closing_ex(expolygons, float(scale_(EPSILON)));
                        slices_by_region[temp_slices[i].region_id][z_idx] = std::move(expolygons);
                        i = j;
                    }
                    throw_on_cancel_callback();
                }
            });
    }

    return slices_by_region;
}

// Layer::slicing_errors is no more set since 1.41.1 or possibly earlier, thus this code
// was not really functional for a long day and nobody missed it.
// Could we reuse this fixing code one day?
/*
std::string fix_slicing_errors(LayerPtrs &layers, const std::function<void()> &throw_if_canceled)
{
    // Collect layers with slicing errors.
    // These layers will be fixed in parallel.
    std::vector<size_t> buggy_layers;
    buggy_layers.reserve(layers.size());
    for (size_t idx_layer = 0; idx_layer < layers.size(); ++ idx_layer)
        if (layers[idx_layer]->slicing_errors)
            buggy_layers.push_back(idx_layer);

    SPDLOG_DEBUG("Slicing objects - fixing slicing errors in parallel - begin");
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [&layers, &throw_if_canceled, &buggy_layers](const tbb::blocked_range<size_t>& range) {
            for (size_t buggy_layer_idx = range.begin(); buggy_layer_idx < range.end(); ++ buggy_layer_idx) {
                throw_if_canceled();
                size_t idx_layer = buggy_layers[buggy_layer_idx];
                Layer *layer     = layers[idx_layer];
                assert(layer->slicing_errors);
                // Try to repair the layer surfaces by merging all contours and all holes from neighbor layers.
                SPDLOG_TRACE("Attempting to repair layer {}", idx_layer);
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    LayerRegion *layerm = layer->get_region(region_id);
                    // Find the first valid layer below / above the current layer.
                    const Surfaces *upper_surfaces = nullptr;
                    const Surfaces *lower_surfaces = nullptr;
                    for (size_t j = idx_layer + 1; j < layers.size(); ++ j)
                        if (! layers[j]->slicing_errors) {
                            upper_surfaces = &layers[j]->regions()[region_id]->slices().surfaces;
                            break;
                        }
                    for (int j = int(idx_layer) - 1; j >= 0; -- j)
                        if (! layers[j]->slicing_errors) {
                            lower_surfaces = &layers[j]->regions()[region_id]->slices().surfaces;
                            break;
                        }
                    // Collect outer contours and holes from the valid layers above & below.
                    Polygons outer;
                    outer.reserve(
                        ((upper_surfaces == nullptr) ? 0 : upper_surfaces->size()) + 
                        ((lower_surfaces == nullptr) ? 0 : lower_surfaces->size()));
                    size_t num_holes = 0;
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    Polygons holes;
                    holes.reserve(num_holes);
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    layerm->m_slices.set(diff_ex(union_(outer), holes), stInternal);
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    throw_if_canceled();
    SPDLOG_DEBUG("Slicing objects - fixing slicing errors in parallel - end");

    // remove empty layers from bottom
    while (! layers.empty() && (layers.front()->lslices.empty() || layers.front()->empty())) {
        delete layers.front();
        layers.erase(layers.begin());
        layers.front()->lower_layer = nullptr;
        for (size_t i = 0; i < layers.size(); ++ i)
            layers[i]->set_id(layers[i]->id() - 1);
    }

    return buggy_layers.empty() ? "" :
        "The model has overlapping or self-intersecting facets. I tried to repair it, "
        "however you might want to check the results or repair the input file and retry.\n";
}
*/

// Called by make_perimeters()
// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
// Orca PrintObject::_shrink_contour_holes: offset the contours and the holes of the expolygons
// separately (a positive hole_delta makes the holes bigger)
static ExPolygons shrink_contour_holes(double contour_delta, double hole_delta, const ExPolygons& polys)
{
    ExPolygons new_ex_polys;
    for (const ExPolygon& ex_poly : polys) {
        Polygons contours;
        Polygons holes;
        for (const Polygon& hole : ex_poly.holes) {
            if (hole_delta != 0) {
                for (Polygon& new_hole : offset(hole, float(-hole_delta))) {
                    Algorithms::Polygon::make_counter_clockwise(new_hole);
                    holes.emplace_back(std::move(new_hole));
                }
            } else {
                holes.push_back(hole);
                Algorithms::Polygon::make_counter_clockwise(holes.back());
            }
        }
        if (contour_delta != 0) {
            Polygons new_contours = offset(ex_poly.contour, float(contour_delta));
            if (new_contours.empty())
                continue;
            contours.insert(contours.end(), std::make_move_iterator(new_contours.begin()), std::make_move_iterator(new_contours.end()));
        } else {
            contours.push_back(ex_poly.contour);
        }
        ExPolygons temp = diff_ex(union_(contours), union_(holes));
        new_ex_polys.insert(new_ex_polys.end(), std::make_move_iterator(temp.begin()), std::make_move_iterator(temp.end()));
    }
    return union_ex(new_ex_polys);
}

void PrintObject::slice()
{
    if (! this->set_started(posSlice))
        return;
    m_print->set_status(Domain::Percentage{10}, Biz::Slicing::ProgressInfo::ProcessingTriangulatedMesh);
    Domain::ZHeightPairs layer_height_profile;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile);
    m_print->throw_if_canceled();
    m_typed_slices = false;
    this->clear_layers();
    m_layers = new_layers(this, generate_object_layers(m_slicing_params, layer_height_profile, m_config.get<bool>("precise_z_height")));
    this->slice_volumes();
    m_print->throw_if_canceled();
#if 0
    // Layer::slicing_errors is no more set since 1.41.1 or possibly earlier, thus this code
    // was not really functional for a long day and nobody missed it.
    // Could we reuse this fixing code one day?

    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = fix_slicing_errors(m_layers, [this](){ m_print->throw_if_canceled(); });
    m_print->throw_if_canceled();
    if (! warning.empty())
    SPDLOG_INFO("{}", warning);
#endif
    // Update bounding boxes, back up raw slices of complex models.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer &layer = *m_layers[layer_idx];
                layer.lslices_ex.clear();
                layer.lslices_ex.reserve(layer.lslices.size());
                for (const ExPolygon &expoly : layer.lslices)
                	layer.lslices_ex.push_back({ get_extents(expoly) });
                layer.backup_untyped_slices();
            }
        });
    // Interlink the lslices into a Z graph.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(1, m_layers.size()),
        [this](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer::build_up_down_graph(*m_layers[layer_idx - 1], *m_layers[layer_idx]);
            }
        });
    if (m_layers.empty()) {
        throw Biz::Slicing::Exception{
            Biz::Slicing::Error{Biz::Slicing::ErrorCode::NoLayers}
        };
    }
    this->set_done(posSlice);
}

template<typename ThrowOnCancel>
void apply_mm_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns MM segmentation based on painting in MM segmentation gizmo
    std::vector<std::vector<ExPolygons>> segmentation = multi_material_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    if (!print_object.print()->virtual_extruders().empty()) {
        std::vector<double> print_z_per_layer(segmentation.size());
        for (size_t layer_id = 0; layer_id < segmentation.size(); ++layer_id) {
            print_z_per_layer[layer_id] = print_object.get_layer(int(layer_id))->print_z;
        }

        Feature::VirtualExtruder::remap_virtual_extruders_to_physical(
            segmentation,
            print_z_per_layer,
            print_object.print()->num_physical_extruders(),
            print_object.print()->virtual_extruders()
        );
    }

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
            const auto  &layer_ranges   = print_object.shared_regions()->layer_ranges;
            double       z              = print_object.get_layer(int(range.begin()))->slice_z;
            auto         it_layer_range = layer_range_first(layer_ranges, z);
            const size_t num_extruders = print_object.print()->config().hw_config().material_slot_count();

            struct ByExtruder {
                ExPolygons  expolygons;
                BoundingBox bbox;
            };

            struct ByRegion {
                ExPolygons expolygons;
                bool       needs_merge { false };
            };

            std::vector<ByExtruder> by_extruder;
            std::vector<ByRegion>   by_region;
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++layer_id) {
                throw_on_cancel();
                Layer &layer = *print_object.get_layer(int(layer_id));
                it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
                const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                // Gather per extruder expolygons.
                by_extruder.assign(num_extruders, ByExtruder());
                by_region.assign(layer.region_count(), ByRegion());
                bool layer_split = false;
                for (size_t extruder_id = 0; extruder_id < num_extruders; ++ extruder_id) {
                    ByExtruder &region = by_extruder[extruder_id];
                    append(region.expolygons, std::move(segmentation[layer_id][extruder_id]));
                    if (! region.expolygons.empty()) {
                        region.bbox = get_extents(region.expolygons);
                        layer_split = true;
                    }
                }

                if (!layer_split)
                    continue;

                // Split LayerRegions by by_extruder regions.
                // layer_range.painted_regions are sorted by extruder ID and parent PrintObject region ID.
                auto it_painted_region_begin = layer_range.painted_regions.cbegin();
                for (int parent_layer_region_idx = 0; parent_layer_region_idx < static_cast<int>(layer.region_count()); ++parent_layer_region_idx) {
                    if (it_painted_region_begin == layer_range.painted_regions.cend())
                        continue;

                    const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                    const PrintRegion &parent_print_region = parent_layer_region.region();
                    assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                    if (parent_layer_region.slices().empty())
                        continue;

                    // Find the first PaintedRegion, which overrides the parent PrintRegion.
                    auto it_first_painted_region = std::find_if(it_painted_region_begin, layer_range.painted_regions.cend(), [&layer_range, &parent_print_region](const auto &painted_region) {
                        return layer_range.volume_regions[painted_region.parent].region->print_object_region_id() == parent_print_region.print_object_region_id();
                    });

                    if (it_first_painted_region == layer_range.painted_regions.cend())
                        continue; // This LayerRegion isn't overrides by any PaintedRegion.

                    assert(&parent_print_region == layer_range.volume_regions[it_first_painted_region->parent].region);

                    // Update the beginning PaintedRegion iterator for the next iteration.
                    it_painted_region_begin = it_first_painted_region;

                    const BoundingBox parent_layer_region_bbox = get_extents(parent_layer_region.slices().surfaces);
                    bool              self_trimmed             = false;
                    int               self_extruder_id         = -1; // 1-based extruder ID
                    for (int extruder_id = 1; extruder_id <= int(by_extruder.size()); ++extruder_id) {
                        const ByExtruder &segmented = by_extruder[extruder_id - 1];
                        if (!segmented.bbox.defined || !parent_layer_region_bbox.overlap(segmented.bbox))
                            continue;

                        // Find the first target region iterator.
                        auto it_target_region = std::find_if(it_painted_region_begin, layer_range.painted_regions.cend(), [extruder_id](const auto &painted_region) {
                            return int(painted_region.extruder_id) >= extruder_id;
                        });

                        assert(it_target_region != layer_range.painted_regions.end());
                        assert(layer_range.volume_regions[it_target_region->parent].region == &parent_print_region && int(it_target_region->extruder_id) == extruder_id);

                        // Update the beginning PaintedRegion iterator for the next iteration.
                        it_painted_region_begin = it_target_region;

                        // FIXME: Don't trim by self, it is not reliable.
                        if (it_target_region->region == &parent_print_region) {
                            self_extruder_id = extruder_id;
                            continue;
                        }

                        // Steal from this region.
                        int        target_region_id = it_target_region->region->print_object_region_id();
                        ExPolygons stolen           = intersection_ex(parent_layer_region.slices().surfaces, segmented.expolygons);
                        if (!stolen.empty()) {
                            ByRegion &dst = by_region[target_region_id];
                            if (dst.expolygons.empty()) {
                                dst.expolygons = std::move(stolen);
                            } else {
                                append(dst.expolygons, std::move(stolen));
                                dst.needs_merge = true;
                            }
                        }
                    }

                    if (!self_trimmed) {
                        // Trim slices of this LayerRegion with all the MM regions.
                        Polygons mine = to_polygons(parent_layer_region.slices().surfaces);
                        for (auto &segmented : by_extruder) {
                            if (&segmented - by_extruder.data() + 1 != self_extruder_id && segmented.bbox.defined && parent_layer_region_bbox.overlap(segmented.bbox)) {
                                mine = diff(mine, segmented.expolygons);
                                if (mine.empty())
                                    break;
                            }
                        }

                        // Filter out unprintable polygons produced by subtraction multi-material painted regions from layerm.region().
                        // ExPolygon returned from multi-material segmentation does not precisely match ExPolygons in layerm.region()
                        // (because of preprocessing of the input regions in multi-material segmentation). Therefore, subtraction from
                        // layerm.region() could produce a huge number of small unprintable regions for the model's base extruder.
                        // This could, on some models, produce bulges with the model's base color (#7109).
                        if (!mine.empty()) {
                            mine = opening(union_ex(mine), scaled<float>(5. * EPSILON), scaled<float>(5. * EPSILON));
                        }

                        if (!mine.empty()) {
                            ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                            if (dst.expolygons.empty()) {
                                dst.expolygons = union_ex(mine);
                            } else {
                                append(dst.expolygons, union_ex(mine));
                                dst.needs_merge = true;
                            }
                        }
                    }
                }

                // Re-create Surfaces of LayerRegions.
                for (int region_id = 0; region_id < static_cast<int>(layer.region_count()); ++region_id) {
                    ByRegion &src = by_region[region_id];
                    if (src.needs_merge) {
                        // Multiple regions were merged into one.
                        src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                    }

                    layer.get_region(region_id)->m_slices.set(std::move(src.expolygons), stInternal);
                }
            }
        });
}

template<typename ThrowOnCancel>
void apply_fuzzy_skin_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns fuzzy skin segmentation based on painting in the fuzzy skin painting gizmo.
    std::vector<std::vector<ExPolygons>> segmentation = fuzzy_skin_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());

    struct ByRegion
    {
        ExPolygons expolygons;
        bool       needs_merge { false };
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))), [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
        const auto &layer_ranges   = print_object.shared_regions()->layer_ranges;
        auto        it_layer_range = layer_range_first(layer_ranges, print_object.get_layer(int(range.begin()))->slice_z);

        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel();

            Layer &layer = *print_object.get_layer(int(layer_idx));
            it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer.slice_z);
            const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;

            assert(segmentation[layer_idx].size() == 1);
            const ExPolygons &fuzzy_skin_segmentation      = segmentation[layer_idx][0];
            const BoundingBox fuzzy_skin_segmentation_bbox = get_extents(fuzzy_skin_segmentation);
            if (fuzzy_skin_segmentation.empty())
                continue;

            // Split LayerRegions by painted fuzzy skin regions.
            // layer_range.fuzzy_skin_painted_regions are sorted by parent PrintObject region ID.
            std::vector<ByRegion> by_region(layer.region_count());
            auto                  it_fuzzy_skin_region_begin = layer_range.fuzzy_skin_painted_regions.cbegin();
            for (int parent_layer_region_idx = 0; parent_layer_region_idx < static_cast<int>(layer.region_count()); ++parent_layer_region_idx) {
                if (it_fuzzy_skin_region_begin == layer_range.fuzzy_skin_painted_regions.cend())
                    continue;

                const LayerRegion &parent_layer_region = *layer.get_region(parent_layer_region_idx);
                const PrintRegion &parent_print_region = parent_layer_region.region();
                assert(parent_print_region.print_object_region_id() == parent_layer_region_idx);
                if (parent_layer_region.slices().empty())
                    continue;

                // Find the first FuzzySkinPaintedRegion, which overrides the parent PrintRegion.
                auto it_fuzzy_skin_region = std::find_if(it_fuzzy_skin_region_begin, layer_range.fuzzy_skin_painted_regions.cend(), [&layer_range, &parent_print_region](const auto &fuzzy_skin_region) {
                    return fuzzy_skin_region.parent_print_object_region_id(layer_range) == parent_print_region.print_object_region_id();
                });

                if (it_fuzzy_skin_region == layer_range.fuzzy_skin_painted_regions.cend())
                    continue; // This LayerRegion isn't overrides by any FuzzySkinPaintedRegion.

                assert(it_fuzzy_skin_region->parent_print_object_region(layer_range) == &parent_print_region);

                // Update the beginning FuzzySkinPaintedRegion iterator for the next iteration.
                it_fuzzy_skin_region_begin = std::next(it_fuzzy_skin_region);

                const BoundingBox parent_layer_region_bbox        = get_extents(parent_layer_region.slices().surfaces);
                Polygons          layer_region_remaining_polygons = to_polygons(parent_layer_region.slices().surfaces);
                // Don't trim by self, it is not reliable.
                if (parent_layer_region_bbox.overlap(fuzzy_skin_segmentation_bbox) && it_fuzzy_skin_region->region != &parent_print_region) {
                    // Steal from this region.
                    const int  target_region_id = it_fuzzy_skin_region->region->print_object_region_id();
                    ExPolygons stolen           = intersection_ex(parent_layer_region.slices().surfaces, fuzzy_skin_segmentation);
                    if (!stolen.empty()) {
                        ByRegion &dst = by_region[target_region_id];
                        if (dst.expolygons.empty()) {
                            dst.expolygons = std::move(stolen);
                        } else {
                            append(dst.expolygons, std::move(stolen));
                            dst.needs_merge = true;
                        }
                    }

                    // Trim slices of this LayerRegion by the fuzzy skin region.
                    layer_region_remaining_polygons = diff(layer_region_remaining_polygons, fuzzy_skin_segmentation);

                    // Filter out unprintable polygons. Detailed explanation is inside apply_mm_segmentation.
                    if (!layer_region_remaining_polygons.empty()) {
                        layer_region_remaining_polygons = opening(union_ex(layer_region_remaining_polygons), scaled<float>(5. * EPSILON), scaled<float>(5. * EPSILON));
                    }
                }

                if (!layer_region_remaining_polygons.empty()) {
                    ByRegion &dst = by_region[parent_print_region.print_object_region_id()];
                    if (dst.expolygons.empty()) {
                        dst.expolygons = union_ex(layer_region_remaining_polygons);
                    } else {
                        append(dst.expolygons, union_ex(layer_region_remaining_polygons));
                        dst.needs_merge = true;
                    }
                }
            }

            // Re-create Surfaces of LayerRegions.
            for (int region_id = 0; region_id < static_cast<int>(layer.region_count()); ++region_id) {
                ByRegion &src = by_region[region_id];
                if (src.needs_merge) {
                    // Multiple regions were merged into one.
                    src.expolygons = closing_ex(src.expolygons, scaled<float>(10. * EPSILON));
                }

                layer.get_region(region_id)->m_slices.set(std::move(src.expolygons), stInternal);
            }
        }
    }); // end of parallel_for
}

// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
// Orca doesVolumeIntersect: two volumes are connected if their slices overlap on a layer or the layers next to it.
static bool volumes_intersect(const VolumeSlices &vs1, const VolumeSlices &vs2)
{
    if (vs1.volume_id == vs2.volume_id)
        return true;
    // two volumes in the same object should have same number of layers, otherwise the slicing is incorrect.
    if (vs1.slices.size() != vs2.slices.size())
        return false;
    const auto overlaps = [](const ExPolygons &a, const ExPolygons &b) {
        for (const ExPolygon &ea : a)
            for (const ExPolygon &eb : b)
                if (Algorithms::ExPolygon::overlaps(ea, eb))
                    return true;
        return false;
    };
    std::atomic<bool> is_intersect{ false };
    tbb::parallel_for(tbb::blocked_range<int>(0, int(vs1.slices.size())), [&vs1, &vs2, &is_intersect, &overlaps](const tbb::blocked_range<int> &range) {
        for (int i = range.begin(); i != range.end() && ! is_intersect; ++i) {
            if (vs1.slices[i].empty())
                continue;
            if (overlaps(vs1.slices[i], vs2.slices[i]) ||
                (i + 1 != int(vs2.slices.size()) && overlaps(vs1.slices[i], vs2.slices[i + 1])) ||
                (i - 1 >= 0 && overlaps(vs1.slices[i], vs2.slices[i - 1])))
                is_intersect = true;
        }
    });
    return is_intersect;
}

// Orca groupingVolumesForBrim (groupingVolumes, findPartVolumes, applyNegtiveVolumes): the model parts grouped by their
// connections, with their first layer slices less the negative volumes.
static std::vector<PrintObject::VolumeGroupSlices> group_volumes_for_brim(
    const std::vector<VolumeSlices> &volume_slices, const Domain::ModelVolumePtrs &model_volumes, const double resolution)
{
    const auto volume_of = [&model_volumes](const Domain::ObjectID id) -> const Domain::ModelVolume* {
        for (const Domain::ModelVolume *mv : model_volumes)
            if (mv->id() == id)
                return mv;
        return nullptr;
    };
    std::vector<VolumeSlices> parts;
    for (const VolumeSlices &vs : volume_slices)
        if (const Domain::ModelVolume *mv = volume_of(vs.volume_id); mv != nullptr && mv->is_model_part())
            parts.push_back(vs);

    const double offset_value = 0.05 / SCALING_FACTOR;
    for (VolumeSlices &vs : parts)
        tbb::parallel_for(tbb::blocked_range<size_t>(0, vs.slices.size()), [&vs, resolution, offset_value](const tbb::blocked_range<size_t> &range) {
            for (size_t k = range.begin(); k != range.end(); ++k) {
                for (ExPolygon &poly_ex : vs.slices[k])
                    Algorithms::DouglasPeucker::douglas_peucker(poly_ex, resolution);
                vs.slices[k] = offset_ex(vs.slices[k], float(offset_value));
            }
        });

    std::vector<int> group_index(parts.size(), -1);
    for (size_t i = 0; i != parts.size(); ++i) {
        if (group_index[i] < 0)
            group_index[i] = int(i);
        for (size_t j = i + 1; j != parts.size(); ++j)
            if (volumes_intersect(parts[i], parts[j])) {
                if (group_index[j] < 0)
                    group_index[j] = group_index[i];
                if (group_index[j] != group_index[i]) {
                    const int retain = std::min(group_index[i], group_index[j]);
                    const int cover  = std::max(group_index[i], group_index[j]);
                    for (int &gi : group_index)
                        if (gi == cover)
                            gi = retain;
                }
            }
    }
    std::vector<int> group_vector;
    for (int gi : group_index)
        if (std::find(group_vector.begin(), group_vector.end(), gi) == group_vector.end())
            group_vector.push_back(gi);

    std::vector<PrintObject::VolumeGroupSlices> groups;
    for (int gv : group_vector) {
        PrintObject::VolumeGroupSlices gvs;
        for (size_t i = 0; i != parts.size(); ++i)
            if (group_index[i] == gv) {
                gvs.volume_ids.push_back(parts[i].volume_id);
                if (! parts[i].slices.empty())
                    append(gvs.slices, parts[i].slices.front());
            }
        // the slices of a group should be unioned
        gvs.slices = offset_ex(union_ex(gvs.slices), float(-offset_value));
        for (ExPolygon &poly_ex : gvs.slices)
            Algorithms::DouglasPeucker::douglas_peucker(poly_ex, resolution);
        groups.push_back(std::move(gvs));
    }

    // less the negative volumes
    ExPolygons neg_total;
    for (const VolumeSlices &vs : volume_slices)
        if (const Domain::ModelVolume *mv = volume_of(vs.volume_id); mv != nullptr && mv->is_negative_volume() && ! vs.slices.empty())
            append(neg_total, vs.slices.front());
    if (! neg_total.empty())
        for (PrintObject::VolumeGroupSlices &g : groups) {
            g.slices = diff_ex(g.slices, neg_total);
            for (ExPolygon &poly_ex : g.slices)
                Algorithms::DouglasPeucker::douglas_peucker(poly_ex, resolution);
        }
    return groups;
}

// Orca reGroupingLayerPolygons: the actual first layer slices sorted into the volume groups (each into the group
// covering the most of it).
static void regroup_layer_polygons(std::vector<PrintObject::VolumeGroupSlices> &groups, const ExPolygons &eps, const double resolution)
{
    std::vector<int> eps_index(eps.size(), -1);
    std::vector<PrintObject::VolumeGroupSlices> groups_simplified = groups;
    ExPolygons eps_simplified = eps;
    for (ExPolygon &poly_ex : eps_simplified)
        Algorithms::DouglasPeucker::douglas_peucker(poly_ex, resolution);
    for (PrintObject::VolumeGroupSlices &g : groups_simplified)
        for (ExPolygon &poly_ex : g.slices)
            Algorithms::DouglasPeucker::douglas_peucker(poly_ex, resolution);

    tbb::parallel_for(tbb::blocked_range<size_t>(0, eps_simplified.size()), [&eps_simplified, &groups_simplified, &eps_index](const tbb::blocked_range<size_t> &range) {
        for (size_t ie = range.begin(); ie != range.end(); ++ie) {
            if (Algorithms::ExPolygon::area(eps_simplified[ie]) <= 0)
                continue;
            double min_area = Algorithms::ExPolygon::area(eps_simplified[ie]);
            for (size_t iv = 0; iv != groups_simplified.size(); ++iv) {
                const double area = Algorithms::ExPolygon::area(diff_ex(ExPolygons{ eps_simplified[ie] }, groups_simplified[iv].slices));
                if (area < min_area) {
                    min_area      = area;
                    eps_index[ie] = int(iv);
                }
            }
        }
    });

    for (PrintObject::VolumeGroupSlices &g : groups)
        g.slices.clear();
    for (size_t ie = 0; ie != eps.size(); ++ie)
        if (eps_index[ie] >= 0)
            groups[eps_index[ie]].slices.push_back(eps[ie]);
}

// Orca create_polyholes (from SuperSlicer): the polyhole(s) of a hole of a radius: edges by the nozzle diameter,
// rotated copies for a twisted polyhole.
static Polygons create_polyholes(const Point center, const coord_t radius, const coord_t nozzle_diameter, bool multiple, int max_edges)
{
    // n = max(round(2 * d), 3); // for 0.4mm nozzle
    const size_t nb_edges = size_t(std::min(max_edges, std::max(3, int(std::round(4.0 * unscaled(radius) * 0.4 / unscaled(nozzle_diameter))))));
    // cylinder(h = h, r = d / cos (180 / n), $fn = n);
    int   nb_polyhole = 1;
    float rotation    = 0;
    if (multiple) {
        nb_polyhole = 5;
        rotation    = 2 * float(PI) / (nb_edges * nb_polyhole);
    }
    Polygons list(nb_polyhole);
    for (int i_poly = 0; i_poly < nb_polyhole; i_poly++) {
        // alternated
        Polygon    &pts        = ((i_poly % 2) == 0) ? list[i_poly / 2] : list[(nb_polyhole + 1) / 2 + i_poly / 2];
        const float new_radius = radius / float(std::cos(PI / nb_edges));
        for (size_t i_edge = 0; i_edge < nb_edges; ++i_edge) {
            const float angle = rotation * i_poly + (float(PI) * 2 * float(i_edge)) / nb_edges;
            pts.points.emplace_back(coord_t(center.x() + new_radius * cos(angle)), coord_t(center.y() + new_radius * sin(angle)));
        }
        Algorithms::Polygon::make_clockwise(pts);
    }
    return list;
}

// Orca _transform_hole_to_polyholes (from SuperSlicer): the round holes spanning at least 2 layers (or on the first
// layer only) replaced by polyholes, in the region slices and the layer slices.
void PrintObject::transform_hole_to_polyholes()
{
    // the circular holes per layer: center, max diameter, extruder, max variation, twist, max edges
    using HoleId = std::tuple<Point, float, int, coord_t, bool, int>;
    std::vector<std::vector<std::pair<HoleId, Polygon*>>> layerid2center(m_layers.size());
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layers.size()), [this, &layerid2center](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            m_print->throw_if_canceled();
            Layer *layer = m_layers[layer_idx];
            for (LayerRegion *layerm : layer->m_regions) {
                const PrintRegion &region = layerm->region();
                if (! region.config().get<bool>("hole_to_polyhole"))
                    continue;
                for (Surface &surf : layerm->m_slices.surfaces)
                    for (Polygon &hole : surf.expolygon.holes) {
                        // Orca tests convex_points(PI), which selects no point: any hole of more than 8 points
                        if (hole.points.size() <= 8)
                            continue;
                        // the circle's center
                        const Point center = hole.centroid();
                        double diameter_min = std::numeric_limits<float>::max(), diameter_max = 0, diameter_sum = 0;
                        for (const Point &pt : hole.points) {
                            const double dist = (pt - center).cast<double>().norm();
                            diameter_min = std::min(diameter_min, dist);
                            diameter_max = std::max(diameter_max, dist);
                            diameter_sum += dist;
                        }
                        // the centers of the lines too, to tell from a rectangle
                        double diameter_line_min = std::numeric_limits<float>::max(), diameter_line_max = 0;
                        for (const Line &l : Algorithms::Polygon::to_lines(hole)) {
                            const double dist = (Point((l.a + l.b) / 2) - center).cast<double>().norm();
                            diameter_line_min = std::min(diameter_line_min, dist);
                            diameter_line_max = std::max(diameter_line_max, dist);
                        }
                        const coord_t max_variation = std::max(coord_t(SCALED_EPSILON), coord_t(scale_(
                            region.config().get<Domain::FloatOrPercentage>("hole_to_polyhole_threshold").get_abs_value(unscaled(diameter_sum / hole.points.size())))));
                        if (diameter_max - diameter_min < max_variation * 2 && diameter_line_max - diameter_line_min < max_variation * 2)
                            layerid2center[layer_idx].emplace_back(
                                HoleId{ center, float(diameter_max), int(region.extruder(frExternalPerimeter)), max_variation,
                                        region.config().get<bool>("hole_to_polyhole_twisted"), region.config().get<int>("hole_to_polyhole_max_edges") },
                                &hole);
                    }
            }
        }
    });

    // the holes spanning at least 2 layers, by center and diameter
    std::map<HoleId, std::vector<std::pair<Polygon*, int>>> id2layerz2hole;
    const size_t min_nb_layers = 2;
    for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++layer_idx)
        for (size_t hole_idx = 0; hole_idx < layerid2center[layer_idx].size(); ++hole_idx) {
            HoleId &id    = layerid2center[layer_idx][hole_idx].first;
            float   max_z = float(m_layers[layer_idx]->print_z);
            std::vector<std::pair<Polygon*, int>> holes;
            holes.emplace_back(layerid2center[layer_idx][hole_idx].second, int(layer_idx));
            for (size_t search_layer_idx = layer_idx + 1; search_layer_idx < m_layers.size(); ++search_layer_idx) {
                if (m_layers[search_layer_idx]->print_z - m_layers[search_layer_idx]->height - max_z > EPSILON)
                    break;
                // another polygon of the same id
                for (size_t search_hole_idx = 0; search_hole_idx < layerid2center[search_layer_idx].size(); ++search_hole_idx) {
                    const HoleId &search_id = layerid2center[search_layer_idx][search_hole_idx].first;
                    if (std::get<2>(id) == std::get<2>(search_id) &&
                        (std::get<0>(id) - std::get<0>(search_id)).cast<double>().norm() < std::get<3>(id) &&
                        std::abs(std::get<1>(id) - std::get<1>(search_id)) < std::get<3>(id)) {
                        max_z = float(m_layers[search_layer_idx]->print_z);
                        holes.emplace_back(layerid2center[search_layer_idx][search_hole_idx].second, int(search_layer_idx));
                        layerid2center[search_layer_idx].erase(layerid2center[search_layer_idx].begin() + search_hole_idx);
                        --search_hole_idx;
                        break;
                    }
                }
            }
            // a straight hole, or a first layer hole (the first layer compensation)
            if (holes.size() >= min_nb_layers || (holes.size() == 1 && holes[0].second == 0))
                id2layerz2hole.emplace(std::move(id), std::move(holes));
        }

    // a polyhole per id replacing the holes' points
    for (auto &entry : id2layerz2hole) {
        const coord_t  nozzle_diameter = scaled<coord_t>(Biz::Slicing::get_nozzle_diameter(m_print->config().hw_config(), std::get<2>(entry.first) - 1));
        const Polygons polyholes       = create_polyholes(std::get<0>(entry.first), coord_t(std::get<1>(entry.first)), nozzle_diameter,
                                                          std::get<4>(entry.first), std::get<5>(entry.first));
        for (auto &poly_to_replace : entry.second) {
            const Polygon &polyhole = polyholes[poly_to_replace.second % polyholes.size()];
            // the clone in the layer slices
            for (ExPolygon &explo_slice : m_layers[poly_to_replace.second]->lslices)
                for (Polygon &poly_slice : explo_slice.holes)
                    if (poly_slice.points == poly_to_replace.first->points)
                        poly_slice.points = polyhole.points;
            poly_to_replace.first->points = polyhole.points;
        }
    }
}

void PrintObject::slice_volumes()
{
    SPDLOG_INFO("Slicing volumes... {}", log_memory_info());
    const Print *print                      = this->print();
    const auto   throw_on_cancel_callback   = std::function<void()>([print](){ print->throw_if_canceled(); });

    // Clear old LayerRegions, allocate for new PrintRegions.
    for (Layer* layer : m_layers) {
        layer->m_regions.clear();
        layer->m_regions.reserve(m_shared_regions->all_regions.size());
        for (const std::unique_ptr<PrintRegion> &pr : m_shared_regions->all_regions)
            layer->m_regions.emplace_back(new LayerRegion(layer, pr.get()));
    }

    std::vector<float>                   slice_zs      = zs_from_layers(m_layers);
    std::vector<VolumeSlices>            volume_slices = slice_volumes_inner(
            print->config(), this->config(), this->trafo_centered(),
            this->model_object()->volumes, m_shared_regions->layer_ranges, slice_zs, throw_on_cancel_callback);
    // Orca groupingVolumesForBrim: the model parts grouped by their connections, for the auto brim
    const double resolution = scaled<double>(print->config().get<double>("resolution"));
    m_first_layer_volume_groups = group_volumes_for_brim(volume_slices, this->model_object()->volumes, resolution);
    std::vector<std::vector<ExPolygons>> region_slices = slices_to_regions(this->model_object()->volumes, *m_shared_regions, slice_zs,
        std::move(volume_slices), throw_on_cancel_callback);

    for (size_t region_id = 0; region_id < region_slices.size(); ++ region_id) {
        std::vector<ExPolygons> &by_layer = region_slices[region_id];
        for (size_t layer_id = 0; layer_id < by_layer.size(); ++ layer_id)
            m_layers[layer_id]->regions()[region_id]->m_slices.append(std::move(by_layer[layer_id]), stInternal);
    }
    region_slices.clear();
    
    SPDLOG_DEBUG("Slicing volumes - removing top empty layers");
    while (! m_layers.empty()) {
        const Layer *layer = m_layers.back();
        if (! layer->empty())
            break;
        delete layer;
        m_layers.pop_back();
    }
    if (! m_layers.empty())
        m_layers.back()->upper_layer = nullptr;
    m_print->throw_if_canceled();

    // Orca make_overhang_printable
    this->apply_conical_overhang();
    m_print->throw_if_canceled();

    // Is any ModelVolume multi-material painted?
    if (m_print->config().hw_config().material_slot_count() > 1 && this->model_object()->is_mm_painted()) {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object is multi-material painted.
        if (m_config.get<double>("xy_size_compensation") != 0.f) {
            m_print->append_warning_callback(
                Slicing::Warning{
                    Slicing::WarningCode::XYSizeCompensationIgnoredMultiMaterialPainting,
                    {},
                    this->model_object()->id(),
                    {},
                    Slicing::WarningSeverity::HIGH
                }
            );
        }

        SPDLOG_DEBUG("Slicing volumes - MMU segmentation");
        apply_mm_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }

    // Is any ModelVolume fuzzy skin painted?
    if (this->model_object()->is_fuzzy_skin_painted()) {
        // If XY Size compensation is also enabled, notify the user that XY Size compensation
        // would not be used because the object has custom fuzzy skin painted.
        if (m_config.get<double>("xy_size_compensation") != 0.f) {
            m_print->append_warning_callback(
                Slicing::Warning{
                    Slicing::WarningCode::XYSizeCompensationIgnoredFuzzySkinPainting,
                    {},
                    this->model_object()->id(),
                    {},
                    Slicing::WarningSeverity::HIGH
                }
            );
        }

        SPDLOG_DEBUG("Slicing volumes - Fuzzy skin segmentation");
        apply_fuzzy_skin_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }

    if (!m_print->virtual_extruders().empty()) {
        Feature::VirtualExtruder::remap_virtual_region_slices_to_physical(
            *this,
            m_print->num_physical_extruders(),
            m_print->virtual_extruders()
        );
    }

    if (m_config.get<bool>("interlocking_beam")) {
        SPDLOG_DEBUG("Slicing volumes - Applying multi-material interlocking");
        InterlockingGenerator::generate_interlocking_structure(*this);
        m_print->throw_if_canceled();
    }

    SPDLOG_DEBUG("Slicing volumes - make_slices in parallel - begin");
    {
        // Compensation value, scaled. Only applying the negative scaling here, as the positive scaling has already been applied during slicing.
        const size_t num_extruders = print->config().hw_config().material_slot_count();
        const auto   xy_compensation_scaled            = (num_extruders > 1 && this->is_mm_painted()) ? scaled<float>(0.f) : scaled<float>(std::min(m_config.get<double>("xy_size_compensation"), 0.));
        const float  elephant_foot_compensation_scaled = (m_config.get<int>("raft_layers") == 0) ?
        	// Only enable Elephant foot compensation if printing directly on the print bed.
            float(scale_(m_config.get<double>("elefant_foot_compensation"))) :
        	0.f;
        // Orca: hole compensation (on top of 3.0's XY size compensation, which moves the holes too)
        const auto   xy_hole_scaled = (num_extruders > 1 && this->is_mm_painted()) ? scaled<float>(0.f) : scaled<float>(m_config.get<double>("xy_hole_compensation"));
        // Orca: the elephant foot compensation fades out over elefant_foot_compensation_layers layers
        const int    elfoot_layers = std::max(1, m_config.get<int>("elefant_foot_compensation_layers"));
        // Uncompensated slices of the layers the Elephant foot compensation is applied to.
	    std::vector<ExPolygons> lslices_elfoot_uncompensated(
            elephant_foot_compensation_scaled > 0 ? std::min(size_t(elfoot_layers), m_layers.size()) : 0);
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size()),
			[this, xy_compensation_scaled, xy_hole_scaled, elfoot_layers, elephant_foot_compensation_scaled, &lslices_elfoot_uncompensated](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
	                m_print->throw_if_canceled();
	                Layer *layer = m_layers[layer_id];
	                // Apply size compensation and perform clipping of multi-part objects.
	                float elfoot = elephant_foot_compensation_scaled > 0 && layer_id < size_t(elfoot_layers) ?
	                    elephant_foot_compensation_scaled - (elephant_foot_compensation_scaled / elfoot_layers) * layer_id :
	                    0.f;
	                if (layer->m_regions.size() == 1) {
	                    // Optimized version for a single region layer.
	                    // Single region, growing or shrinking.
	                    LayerRegion *layerm = layer->m_regions.front();
	                    if (elfoot > 0) {
		                    // Apply the elephant foot compensation and store the layer's slices without the Elephant foot compensation applied.
		                    ExPolygons& lslices_1st_layer = lslices_elfoot_uncompensated[layer_id];
		                    lslices_1st_layer = to_expolygons(std::move(layerm->m_slices.surfaces));
		                    float delta = xy_compensation_scaled;
	                        if (delta > elfoot) {
	                            delta -= elfoot;
	                            elfoot = 0.f;
	                        } else if (delta > 0)
	                            elfoot -= delta;
							layerm->m_slices.set(
								union_ex(
									Slic3r::elephant_foot_compensation(
										(delta == 0.f) ? lslices_1st_layer : offset_ex(lslices_1st_layer, delta), 
	                            		layerm->flow(frExternalPerimeter), unscale<double>(elfoot))),
								stInternal);
							if (xy_compensation_scaled < 0.f)
								lslices_1st_layer = offset_ex(std::move(lslices_1st_layer), xy_compensation_scaled);
							if (xy_hole_scaled != 0.f)
								lslices_1st_layer = shrink_contour_holes(0., xy_hole_scaled, lslices_1st_layer);
	                    } else if (xy_compensation_scaled < 0.f) {
	                        // Apply the XY compensation.
	                        layerm->m_slices.set(
                                offset_ex(to_expolygons(std::move(layerm->m_slices.surfaces)), xy_compensation_scaled),
	                            stInternal);
	                    }
	                } else {
	                    if (xy_compensation_scaled < 0.f || elfoot > 0.f) {
	                        // Apply the negative XY compensation.
	                        Polygons trimming;
	                        static const float eps = float(scale_(m_config.get<double>("slice_closing_radius")) * 1.5);
	                        if (elfoot > 0.f) {
	                        	ExPolygons& lslices_1st_layer = lslices_elfoot_uncompensated[layer_id];
	                        	lslices_1st_layer = offset_ex(layer->merged(eps), std::min(xy_compensation_scaled, 0.f) - eps);
								trimming = Algorithms::ExPolygon::to_polygons(Slic3r::elephant_foot_compensation(lslices_1st_layer,
									layer->m_regions.front()->flow(frExternalPerimeter), unscale<double>(elfoot)));
	                        } else
		                        trimming = offset(layer->merged(float(SCALED_EPSILON)), xy_compensation_scaled - float(SCALED_EPSILON));
	                        for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id)
	                            layer->m_regions[region_id]->trim_surfaces(trimming);
	                    }
	                }
	                // Orca: the hole compensation (contour compensation 0 here: 3.0's XY size compensation is applied above)
	                if (xy_hole_scaled != 0.f) {
	                    if (layer->m_regions.size() == 1) {
	                        LayerRegion *layerm = layer->m_regions.front();
	                        layerm->m_slices.set(
	                            shrink_contour_holes(0., xy_hole_scaled, to_expolygons(std::move(layerm->m_slices.surfaces))),
	                            stInternal);
	                    } else if (xy_hole_scaled > 0.f) {
	                        // Orca: bigger holes in a multi-region layer: holes of the merged layer, the regions
	                        // clipped to it (Orca's smaller holes leave the regions as they are)
	                        const ExPolygons merged = shrink_contour_holes(0., xy_hole_scaled, union_ex(layer->merged(float(SCALED_EPSILON))));
	                        Polygons processed;
	                        for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id) {
	                            ExPolygons slices = intersection_ex(
	                                offset_ex(to_expolygons(std::move(layer->m_regions[region_id]->m_slices.surfaces)), xy_hole_scaled),
	                                merged);
	                            if (region_id > 0)
	                                slices = diff_ex(Algorithms::ExPolygon::to_polygons(slices), processed);
	                            if (region_id + 1 < layer->m_regions.size())
	                                append(processed, Algorithms::ExPolygon::to_polygons(slices));
	                            layer->m_regions[region_id]->m_slices.set(std::move(slices), stInternal);
	                        }
	                    }
	                }
	                // Merge all regions' slices to get islands sorted topologically, chain them by a shortest path in separate index list
	                layer->make_slices();
	            }
	        });
	    if (elephant_foot_compensation_scaled > 0.f && ! m_layers.empty()) {
	    	// The Elephant foot has been compensated, therefore the 1st layer's lslices are shrank with the Elephant foot compensation value.
	    	// Store the uncompensated value there.
            //FIXME is this operation needed? MMU painting and brim now have to do work arounds to work with compensated layer, not with the uncompensated layer.
            // There may be subtle issues removing this block such as support raft sticking too well with the first object layer.
            for (size_t i = 0; i < lslices_elfoot_uncompensated.size(); ++ i) {
                Layer &layer = *m_layers[i];
                assert(layer.id() == i);
                layer.lslices = std::move(lslices_elfoot_uncompensated[i]);
                layer.lslice_indices_sorted_by_print_order = chain_expolygons(layer.lslices);
            }
		}
	}

    // Orca hole_to_polyhole, then the actual first layer slices sorted into the volume groups (groupingVolumesForBrim)
    this->transform_hole_to_polyholes();
    if (! m_layers.empty())
        regroup_layer_polygons(m_first_layer_volume_groups, m_layers.front()->lslices, resolution);

    m_print->throw_if_canceled();
    SPDLOG_DEBUG("Slicing volumes - make_slices in parallel - end");
}

// Orca apply_conical_overhang: from the top, each layer gets the layer above shrunk by what the angle bridges per layer,
// so the overhangs turn into printable cones (the holes in the base of the model up to the hole size kept open).
void PrintObject::apply_conical_overhang()
{
    if (m_layers.empty())
        return;
    const double conical_overhang_angle = this->config().get<double>("make_overhang_printable_angle");
    if (conical_overhang_angle == 90.0)
        return;
    const double angle_radians = conical_overhang_angle * M_PI / 180.;
    const double max_hole_area = this->config().get<double>("make_overhang_printable_hole_size"); // in MM^2
    const double tan_angle     = tan(angle_radians); // the XY-component of the angle
    const double layer_thickness            = m_config.get<double>("layer_height");
    const double max_dist_from_lower_layer  = tan_angle * layer_thickness; // max dist which can be bridged, in MM
    const double scaled_max_dist_from_lower_layer = -float(scale_(max_dist_from_lower_layer));
    const double scaled_max_hole_area             = float(scale_(scale_(max_hole_area)));

    for (auto i = m_layers.rbegin() + 1; i != m_layers.rend(); ++i) {
        m_print->throw_if_canceled();
        Layer *layer       = *i;
        Layer *upper_layer = layer->upper_layer;
        if (upper_layer->empty())
            continue;
        // the whole layer disabled
        if (std::all_of(layer->m_regions.begin(), layer->m_regions.end(), [](const LayerRegion *r) {
                return r->slices().empty() || ! r->region().config().get<bool>("make_overhang_printable"); }))
            continue;

        // the layers merged, to offset them uniformly (the model could break at a region boundary otherwise)
        ExPolygons upper_poly   = union_ex(upper_layer->merged(float(SCALED_EPSILON)));
        const ExPolygons current_poly = union_ex(layer->merged(float(SCALED_EPSILON)));

        // A hole in the base of the model the layer above covers completely is cut from the layer above, against
        // closing it up.
        if (scaled_max_hole_area > 0.0)
            for (const ExPolygon &layer_polygon : current_poly)
                for (const Polygon &hole : layer_polygon.holes)
                    if (std::abs(hole.area()) < scaled_max_hole_area) {
                        const ExPolygon  hole_poly(hole);
                        const ExPolygons hole_with_above = intersection_ex(upper_poly, hole_poly);
                        if (! hole_with_above.empty() && xor_ex(hole_with_above, hole_poly).empty())
                            upper_poly = diff_ex(upper_poly, ExPolygons{ hole_poly });
                    }

        // the upper layer offset, to be added to this one
        upper_poly = offset_ex(upper_poly, float(scaled_max_dist_from_lower_layer));

        for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
            if (! upper_layer->m_regions[region_id]->region().config().get<bool>("make_overhang_printable"))
                continue;
            // the part of the upper poly of this region, without the islands this layer covers already
            ExPolygons p = union_ex(intersection_ex(upper_layer->m_regions[region_id]->slices().surfaces, upper_poly));
            p.erase(std::remove_if(p.begin(), p.end(), [&current_poly](const ExPolygon &ex) { return diff_ex(ExPolygons{ ex }, current_poly).empty(); }), p.end());
            // added to this region
            const ExPolygons layer_polygons = to_expolygons(layer->m_regions[region_id]->slices().surfaces);
            layer->m_regions[region_id]->m_slices.set(union_ex(layer_polygons, p), stInternal);
            // and taken from the others, against overlapping regions
            for (size_t other_region = 0; other_region < this->num_printing_regions(); ++other_region) {
                if (other_region == region_id)
                    continue;
                const ExPolygons s = to_expolygons(layer->m_regions[other_region]->slices().surfaces);
                layer->m_regions[other_region]->m_slices.set(diff_ex(s, p, ApplySafetyOffset::Yes), stInternal);
            }
        }
    }
}

std::vector<Polygons> PrintObject::slice_support_volumes(const Domain::ModelVolumeType model_volume_type) const
{
    auto it_volume     = this->model_object()->volumes.begin();
    auto it_volume_end = this->model_object()->volumes.end();
    for (; it_volume != it_volume_end && (*it_volume)->type() != model_volume_type; ++ it_volume) ;
    std::vector<Polygons> slices;
    if (it_volume != it_volume_end) {
        // Found at least a single support volume of model_volume_type.
        std::vector<float> zs = zs_from_layers(this->layers());
        std::vector<char>  merge_layers;
        bool               merge = false;
        const Print       *print = this->print();
        auto               throw_on_cancel_callback = std::function<void()>([print](){ print->throw_if_canceled(); });
        MeshSlicingParamsEx params;
        params.trafo = this->trafo_centered();
        for (; it_volume != it_volume_end; ++ it_volume)
            if ((*it_volume)->type() == model_volume_type) {
                std::vector<ExPolygons> slices2 = slice_volume(*(*it_volume), zs, params, throw_on_cancel_callback);
                if (slices.empty()) {
                    slices.reserve(slices2.size());
                    for (ExPolygons &src : slices2)
                        slices.emplace_back(Algorithms::ExPolygon::to_polygons(std::move(src)));
                } else if (!slices2.empty()) {
                    if (merge_layers.empty())
                        merge_layers.assign(zs.size(), false);
                    for (size_t i = 0; i < zs.size(); ++ i) {
                        if (slices[i].empty())
                            slices[i] = Algorithms::ExPolygon::to_polygons(std::move(slices2[i]));
                        else if (! slices2[i].empty()) {
                            append(slices[i], Algorithms::ExPolygon::to_polygons(std::move(slices2[i])));
                            merge_layers[i] = true;
                            merge = true;
                        }
                    }
                }
            }
        if (merge) {
            std::vector<Polygons*> to_merge;
            to_merge.reserve(zs.size());
            for (size_t i = 0; i < zs.size(); ++ i)
                if (merge_layers[i])
                    to_merge.emplace_back(&slices[i]);
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, to_merge.size()),
                [&to_merge](const tbb::blocked_range<size_t> &range) {
                    for (size_t i = range.begin(); i < range.end(); ++ i)
                        *to_merge[i] = union_(*to_merge[i]);
            });
        }
    }
    return slices;
}

} // namespace Slic3r
