#include "libslic3r/Slicing.hpp"

#include "Slic3r/Biz/Algorithms/LayerHeight.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/HwConfigUtils.hpp"
#include "libslic3r/ShrinkageCompensation.hpp"
#include "libslic3r/SlicingInput.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

#include <limits>
#include <algorithm>

using namespace Slic3r::Biz;

using Slic3r::Biz::Algorithms::LayerHeight::GenerateLayersParams;
using Slic3r::Biz::Algorithms::LayerHeight::ProfileFromRangesParams;
using Slic3r::Domain::LayerZRanges;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::ZHeightPairs;

namespace Slic3r {

constexpr double MIN_LAYER_HEIGHT         = 0.01;
constexpr double MIN_LAYER_HEIGHT_DEFAULT = 0.07;

static double
min_layer_height_from_nozzle(const PrintObjectConfigView& print_config, int idx_nozzle)
{
    double min_layer_height =
        print_config.get<std::vector<double>>("min_layer_height").at(idx_nozzle - 1);
    return (min_layer_height == 0.) ? MIN_LAYER_HEIGHT_DEFAULT :
                                      std::max(MIN_LAYER_HEIGHT, min_layer_height);
}

static double
max_layer_height_from_nozzle(const PrintObjectConfigView& print_config, int idx_nozzle)
{
    double min_layer_height = min_layer_height_from_nozzle(print_config, idx_nozzle);
    double max_layer_height =
        print_config.get<std::vector<double>>("max_layer_height").at(idx_nozzle - 1);
    double nozzle_dmr = Biz::Slicing::get_nozzle_diameter(print_config.hw_config(), idx_nozzle - 1);
    return std::max(
        min_layer_height,
        (max_layer_height == 0.) ? (0.75 * nozzle_dmr) : max_layer_height
    );
}

SlicingParameters SlicingParameters::create_from_config(
	const PrintObjectConfigView     &config,
	double				         object_height,
    const std::vector<unsigned int> &object_extruders,
    const Vec3d                     &object_shrinkage_compensation)
{
    const double layer_height{config.get<double>("layer_height")};
    assert(! config.get<Domain::FloatOrPercentage>("first_layer_height").is_percentage());
    double first_layer_height = (config.get<Domain::FloatOrPercentage>("first_layer_height").is_zero()) ?
        layer_height : config.get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(layer_height);

    // If object_config.support_material_extruder == 0 resp. object_config.support_material_interface_extruder == 0, use the 0th nozzle diameter,
    // which is consistent with the requirement that if support_material_extruder == 0 resp. support_material_interface_extruder == 0,
    // support will not trigger tool change, but it will use the current nozzle instead.
    // In that case all the nozzles have to be of the same diameter.
    const int support_material_extruder_idx           = std::max<int>(config.get<int>("support_material_extruder") - 1, 0);
    const int support_material_interface_extruder_idx = std::max<int>(config.get<int>("support_material_interface_extruder") - 1, 0);

    const double support_material_extruder_dmr           = Biz::Slicing::get_nozzle_diameter(config.hw_config(), support_material_extruder_idx);
    const double support_material_interface_extruder_dmr = Biz::Slicing::get_nozzle_diameter(config.hw_config(), support_material_interface_extruder_idx);
    const bool   soluble_interface                       = config.get<double>("support_material_contact_distance") == 0.;

    SlicingParameters params;
    params.layer_height = config.get<double>("layer_height");
    params.first_print_layer_height = first_layer_height;
    params.first_object_layer_height = first_layer_height;
    params.object_print_z_min = 0.;
    params.object_print_z_max = object_height * object_shrinkage_compensation.z();
    params.object_print_z_uncompensated_max = object_height;
    params.object_shrinkage_compensation_z = object_shrinkage_compensation.z();
    params.base_raft_layers = config.get<int>("raft_layers");
    params.soluble_interface = soluble_interface;

    // Miniumum/maximum of the minimum layer height over all extruders.
    params.min_layer_height = MIN_LAYER_HEIGHT;
    params.max_layer_height = std::numeric_limits<double>::max();
    if (config.get<Domain::SupportMode>("support_material") != Domain::SupportMode::None || params.base_raft_layers > 0 || config.get<int>("support_material_enforce_layers") > 0) {
        // Extruder indices in config are 1-based, but zero has special meaning (don't care).
        // Assume that first extruder will be used if zero is selected.
        const int support_material_extruder = std::max(1, config.get<int>("support_material_extruder"));
        const int support_material_interface_extruder = std::max(1, config.get<int>("support_material_interface_extruder"));

        // Has some form of support. Add the support layers to the minimum / maximum layer height limits.
        params.min_layer_height = std::max(
            min_layer_height_from_nozzle(config, support_material_extruder),
            min_layer_height_from_nozzle(config, support_material_interface_extruder));
        params.max_layer_height = std::min(
            max_layer_height_from_nozzle(config, support_material_extruder),
            max_layer_height_from_nozzle(config, support_material_interface_extruder));
        params.max_suport_layer_height = params.max_layer_height;
    }
    if (object_extruders.empty()) {
        params.min_layer_height = std::max(params.min_layer_height, min_layer_height_from_nozzle(config, 1));
        params.max_layer_height = std::min(params.max_layer_height, max_layer_height_from_nozzle(config, 1));
    } else {
        for (unsigned int extruder_id : object_extruders) {
            params.min_layer_height = std::max(params.min_layer_height, min_layer_height_from_nozzle(config, extruder_id + 1));
            params.max_layer_height = std::min(params.max_layer_height, max_layer_height_from_nozzle(config, extruder_id + 1));
        }
    }
    params.min_layer_height = std::min(params.min_layer_height, params.layer_height);
    params.max_layer_height = std::max(params.max_layer_height, params.layer_height);

    if (! soluble_interface) {
        params.gap_raft_object    = config.get<double>("raft_contact_distance");
        params.gap_object_support = config.get<double>("support_material_bottom_contact_distance");
        params.gap_support_object = config.get<double>("support_material_contact_distance");
        if (params.gap_object_support <= 0)
            params.gap_object_support = params.gap_support_object;
    }

    if (params.base_raft_layers > 0) {
		params.interface_raft_layers = (params.base_raft_layers + 1) / 2;
        params.base_raft_layers -= params.interface_raft_layers;
        // Use as large as possible layer height for the intermediate raft layers.
        params.base_raft_layer_height       = std::max(params.layer_height, 0.75 * support_material_extruder_dmr);
        params.interface_raft_layer_height  = std::max(params.layer_height, 0.75 * support_material_interface_extruder_dmr);
        params.first_object_layer_bridging  = false;
        params.contact_raft_layer_height    = std::max(params.layer_height, 0.75 * support_material_interface_extruder_dmr);
        params.first_object_layer_height    = params.layer_height;
    }

    if (params.has_raft()) {
        // Raise first object layer Z by the thickness of the raft itself plus the extra distance required by the support material logic.
        //FIXME The last raft layer is the contact layer, which shall be printed with a bridging flow for ease of separation. Currently it is not the case.
		if (params.raft_layers() == 1) {
            // There is only the contact layer.
            params.contact_raft_layer_height = first_layer_height;
            params.raft_contact_top_z        = first_layer_height;
		} else {
            assert(params.base_raft_layers > 0);
            assert(params.interface_raft_layers > 0);
            // Number of the base raft layers is decreased by the first layer.
            params.raft_base_top_z       = first_layer_height + double(params.base_raft_layers - 1) * params.base_raft_layer_height;
            // Number of the interface raft layers is decreased by the contact layer.
            params.raft_interface_top_z  = params.raft_base_top_z + double(params.interface_raft_layers - 1) * params.interface_raft_layer_height;
			params.raft_contact_top_z    = params.raft_interface_top_z + params.contact_raft_layer_height;
		}
        double print_z = params.raft_contact_top_z + params.gap_raft_object;
        params.object_print_z_min  = print_z;
        params.object_print_z_max += print_z;
        params.object_print_z_uncompensated_max += print_z;
    }

    params.valid = true;
    return params;
}

SlicingParameters SlicingParameters::create_from_config(
    const Domain::ConfigPackFDM& config,
    const Domain::ObjectSettings& object_settings,
    double object_height,
    const std::vector<unsigned int>& object_extruders,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    const auto full_config_fdm = prepare_slicing_input(config, object_extruders, hw_config);
    ASSERT(full_config_fdm.has_value());

    const PrintConfigView print_config_view{full_config_fdm.value()};
    const Vec3d shrinkage = Slicing::get_shrinkage_compensation(object_extruders, print_config_view)
                                .value_or(Vec3d{1., 1., 1.});

    const auto partial_object_config_fdm = prepare_slicing_object_input(
        object_settings,
        hw_config,
        hw_config.material_slot_count()
    );
    ASSERT(partial_object_config_fdm.has_value());

    const PrintObjectConfigView config_view{
        full_config_fdm.value(),
        partial_object_config_fdm.value()
    };
    return create_from_config(config_view, object_height, object_extruders, shrinkage);
}

ZHeightPairs layer_height_profile_from_ranges(
	const SlicingParameters         &slicing_params,
	const Domain::LayerConfigRanges &layer_config_ranges)
{
    const ProfileFromRangesParams profile_from_ranges_params{
        .layer_height                        = slicing_params.layer_height,
        .first_object_layer_height           = slicing_params.first_object_layer_height,
        .object_print_z_height               = slicing_params.object_print_z_height(),
        .object_print_z_uncompensated_height = slicing_params.object_print_z_uncompensated_height(),
        .first_object_layer_height_fixed     = slicing_params.first_object_layer_height_fixed()
    };

    return Algorithms::LayerHeight::layer_height_profile_from_ranges(
        profile_from_ranges_params,
        layer_config_ranges
    );
}

// Orca adjust_layer_series_to_align_object_height: the last 5 layers' heights adjusted within the layer height limits
// so the last one ends at the object height (at least 6 layers).
static bool adjust_layer_series_to_align_object_height(const SlicingParameters &slicing_params, LayerZRanges &layer_series)
{
    const double object_height = slicing_params.object_print_z_height();
    if (Domain::is_approx(layer_series.back().top_z, object_height))
        return true;
    const size_t layer_size = layer_series.size();
    if (layer_size < 6)
        return false;

    std::vector<double> last_5_layers_height;
    for (size_t i = 0; i < 5; ++i)
        last_5_layers_height.emplace_back(layer_series[layer_size - 5 + i].top_z - layer_series[layer_size - 5 + i].bottom_z);

    double            gap = std::abs(layer_series.back().top_z - object_height);
    std::vector<bool> can_adjust(5, true);
    const bool        taller_than_object = layer_series.back().top_z < object_height;
    auto get_valid_size = [&can_adjust]() {
        return int(std::count(can_adjust.begin(), can_adjust.end(), true));
    };
    auto adjust_layer_height = [&slicing_params, &last_5_layers_height, &can_adjust, &get_valid_size, &taller_than_object](double gap) {
        const double delta_gap  = gap / get_valid_size();
        double       remain_gap = 0;
        for (size_t i = 0; i < last_5_layers_height.size(); ++i) {
            double &l_height = last_5_layers_height[i];
            if (taller_than_object) {
                if (can_adjust[i] && Domain::is_approx(l_height, slicing_params.max_layer_height)) {
                    remain_gap += delta_gap;
                    can_adjust[i] = false;
                    continue;
                }
                if (can_adjust[i] && l_height + delta_gap > slicing_params.max_layer_height) {
                    remain_gap += l_height + delta_gap - slicing_params.max_layer_height;
                    l_height      = slicing_params.max_layer_height;
                    can_adjust[i] = false;
                } else
                    l_height += delta_gap;
            } else {
                if (can_adjust[i] && Domain::is_approx(l_height, slicing_params.min_layer_height)) {
                    remain_gap += delta_gap;
                    can_adjust[i] = false;
                    continue;
                }
                if (can_adjust[i] && l_height - delta_gap < slicing_params.min_layer_height) {
                    remain_gap += slicing_params.min_layer_height + delta_gap - l_height;
                    l_height      = slicing_params.min_layer_height;
                    can_adjust[i] = false;
                } else
                    l_height -= delta_gap;
            }
        }
        return remain_gap;
    };

    while (gap > 0) {
        // the 5 layers cannot reach the height within the valid layer heights
        if (get_valid_size() == 0)
            return false;
        gap = adjust_layer_height(gap);
        if (Domain::is_approx(gap, 0.0))
            break;
    }
    for (size_t i = 0; i < last_5_layers_height.size(); ++i) {
        Domain::LayerZRange &range = layer_series[layer_size - 5 + i];
        if (i > 0)
            range.bottom_z = layer_series[layer_size - 5 + i - 1].top_z;
        range.top_z = range.bottom_z + last_5_layers_height[i];
    }
    return true;
}

LayerZRanges generate_object_layers(
    const SlicingParameters& slicing_params,
    const ZHeightPairs& layer_height_profile,
    const bool precise_z_height
)
{
    const GenerateLayersParams generate_layers_params{
        .min_layer_height                = slicing_params.min_layer_height,
        .max_layer_height                = slicing_params.max_layer_height,
        .first_object_layer_height       = slicing_params.first_object_layer_height,
        .object_print_z_height           = slicing_params.object_print_z_height(),
        .object_shrinkage_compensation_z = slicing_params.object_shrinkage_compensation_z,
        .first_object_layer_height_fixed = slicing_params.first_object_layer_height_fixed()
    };

    LayerZRanges out = Algorithms::LayerHeight::generate_object_layers(
        generate_layers_params,
        layer_height_profile
    );
    // Orca precise_z_height
    if (precise_z_height && ! out.empty())
        adjust_layer_series_to_align_object_height(slicing_params, out);
    return out;
}

}; // namespace Slic3r
