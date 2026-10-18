#include "Slic3r/Biz/GCodeReader/Utils.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"
#include "Slic3r/Domain/enum_bitmask.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/ExtrusionProcessor.hpp"
#include "libslic3r/I18N_private.hpp"
#include "libslic3r/GCode.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/GCode/PostProcessor.hpp"
#include "libslic3r/GCode/PrintExtents.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTowerIntegration.hpp"
#include "libslic3r/GCode/WipePathHelpers.hpp"
#include "libslic3r/GCode/OrderingStrategies.hpp"
#include "libslic3r/GCode/Travels.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ShortestPath.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "LocalesUtils.hpp"
#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/Time.hpp"
#include "libslic3r/CustomParametersHandling.hpp"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <math.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/filesystem.hpp>
#include <Slic3r/Log.hpp>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/cstdlib.hpp>

#include "Slic3r/Biz/Algorithms/SVG.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Log.hpp"
#include "LocalesUtils.hpp"

#include <tbb/parallel_for.h>
#include <fstream>

// Intel redesigned some TBB interface considerably when merging TBB with their oneAPI set of libraries, see GH #7332.
// We are using quite an old TBB 2017 U7. Before we update our build servers, let's use the old API, which is deprecated in up to date TBB.
#if ! defined(TBB_VERSION_MAJOR)
    #include <tbb/version.h>
#endif
#if ! defined(TBB_VERSION_MAJOR)
    static_assert(false, "TBB_VERSION_MAJOR not defined");
#endif
#if TBB_VERSION_MAJOR >= 2021
    #include <tbb/parallel_pipeline.h>
    using slic3r_tbb_filtermode = tbb::filter_mode;
#else
    #include <tbb/pipeline.h>
    using slic3r_tbb_filtermode = tbb::filter;
#endif

using namespace std::literals::string_view_literals;

#if 0
// Enable debugging and asserts, even in the release build.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

using namespace Slic3r::Biz;

using Slic3r::Domain::PressureAdvance;

namespace Slic3r {
    using Biz::libpgcode::ProcessorResult;
    using Biz::GCodeReader::GCodeReader;
    namespace CustomGCode = Domain::CustomGCode;
    using ParserConfig = Biz::Parser::IO::Config;
    using ParserValue = Biz::Parser::IO::Value;
    using ParserVector = Biz::Parser::IO::Vector;
    using ParserScalar = Biz::Parser::IO::Scalar;
    using Biz::Parser::IO::is_vector;
    using Biz::Parser::IO::is_scalar;
    using Biz::GCodeReader::contains_reserved_tags;
    using Domain::GCodeFlavor;

    namespace BB = Biz::Algorithms::BoundingBox;

static bool custom_gcode_sets_temperature(const std::string &gcode, const int mcode_set_temp_dont_wait, const int mcode_set_temp_and_wait, const bool include_g10, int &temp_out);

// Orca (GCode::_do_export): the highest exhaust fan speed of the used filaments that activate air
// filtration during the print / on completion; nullopt when none does
static std::optional<int> exhaust_fan_speed(const Print& print, const GCodeWriter& writer, bool during_print)
{
    const auto& config   = print.config();
    const auto activate  = config.get<std::vector<bool>>("activate_air_filtration");
    const auto when      = config.get<std::vector<bool>>(
        during_print ? "activate_air_filtration_during_print" : "activate_air_filtration_on_completion");
    const auto speeds    = config.get<std::vector<int>>(
        during_print ? "during_print_exhaust_fan_speed" : "complete_print_exhaust_fan_speed");
    std::optional<int> ret;
    for (const Extruder& extruder : writer.extruders()) {
        const size_t id = extruder.id();
        if (id < activate.size() && id < when.size() && id < speeds.size() && activate[id] && when[id])
            ret = std::max(ret.value_or(0), speeds[id]);
    }
    return ret;
}

// Only add a newline in case the current G-code does not end with a newline.
    static inline void check_add_eol(std::string& gcode)
    {
        if (!gcode.empty() && gcode.back() != '\n')
            gcode += '\n';
    }

    // Return true if tch_prefix is found in custom_gcode
    static bool custom_gcode_changes_tool(const std::string& custom_gcode, const std::string& tch_prefix, unsigned next_extruder)
    {
        bool ok = false;
        size_t from_pos = 0;
        size_t pos = 0;
        while ((pos = custom_gcode.find(tch_prefix, from_pos)) != std::string::npos) {
            if (pos + 1 == custom_gcode.size())
                break;
            from_pos = pos + 1;
            // only whitespace is allowed before the command
            while (--pos < custom_gcode.size() && custom_gcode[pos] != '\n') {
                if (!std::isspace(custom_gcode[pos]))
                    goto NEXT;
            }
            {
                // we should also check that the extruder changes to what was expected
                std::istringstream ss(custom_gcode.substr(from_pos, std::string::npos));
                unsigned num = 0;
                if (ss >> num)
                    ok = (num == next_extruder);
            }
        NEXT:;
        }
        return ok;
    }

    std::string OozePrevention::pre_toolchange(GCodeGenerator &gcodegen, const Domain::ConfigView& config)
    {
        std::string gcode;

        unsigned int extruder_id = gcodegen.writer().extruder()->id();
        const auto& filament_idle_temp = config.get<std::vector<std::optional<int>>>("idle_temperature");
        if (!filament_idle_temp.at(extruder_id)) {
            // There is no idle temperature defined in filament settings.
            // Use the delta value from print config.
            if (config.get<int>("standby_temperature_delta") != 0) {
                // we assume that heating is always slower than cooling, so no need to block
                gcode += gcodegen.writer().set_temperature
                (this->_get_temp(gcodegen, config) + config.get<int>("standby_temperature_delta"), false, extruder_id);
                gcode.pop_back();
                gcode += " ;cooldown\n"; // this is a marker for GCodeProcessor, so it can supress the commands when needed
            }
        } else {
            // Use the value from filament settings. That one is absolute, not delta.
            gcode += gcodegen.writer().set_temperature(*filament_idle_temp.at(extruder_id), false, extruder_id);
            gcode.pop_back();
            gcode += " ;cooldown\n"; // this is a marker for GCodeProcessor, so it can supress the commands when needed
        }

        return gcode;
    }

    std::string OozePrevention::post_toolchange(GCodeGenerator &gcodegen, const Domain::ConfigView& config)
    {
        return (config.get<int>("standby_temperature_delta") != 0) ?
            gcodegen.writer().set_temperature(this->_get_temp(gcodegen, config), true, gcodegen.writer().extruder()->id()) :
            std::string();
    }

    int OozePrevention::_get_temp(const GCodeGenerator &gcodegen, const Domain::ConfigView& config) const
    {
        // First layer temperature should be used when on the first layer (obviously) and when
        // "other layers" is set to zero (which means it should not be used).
        return (gcodegen.layer() == nullptr || gcodegen.layer()->id() == 0
             || config.get<std::vector<int>>("temperature").at(gcodegen.writer().extruder()->id()) == 0)
            ? config.get<std::vector<int>>("first_layer_temperature").at(gcodegen.writer().extruder()->id())
            : config.get<std::vector<int>>("temperature").at(gcodegen.writer().extruder()->id());
    }

    const std::vector<std::string> ColorPrintColors::Colors = { "#C0392B", "#E67E22", "#F1C40F", "#27AE60", "#1ABC9C", "#2980B9", "#9B59B6" };

void GCodeGenerator::PlaceholderParserIntegration::reset()
{
    this->failed_templates.clear();
    this->output_config = ParserConfig{};
    this->num_extruders = 0;
    this->position.clear();
    this->e_position.clear();
    this->e_retracted.clear();
    this->e_restart_extra.clear();
}

void GCodeGenerator::PlaceholderParserIntegration::init(const GCodeWriter &writer)
{
    this->reset();
    const std::vector<Extruder> &extruders = writer.extruders();
    if (! extruders.empty()) {
        this->num_extruders = extruders.back().id() + 1;
        this->e_retracted.assign(num_extruders, 0);
        this->e_restart_extra.assign(num_extruders, 0);
        this->output_config.set("e_retracted", this->e_retracted);
        this->output_config.set("e_restart_extra", this->e_restart_extra);
        if (! writer.config.use_relative_e_distances) {
            e_position.assign(num_extruders, 0);
            this->output_config.set("e_position", e_position);
        }
    }
    this->parser.set("extruded_volume", std::vector<double>(this->num_extruders, -1.f));
    this->parser.set("extruded_weight", std::vector<double>(this->num_extruders, 0.f));
    this->parser.set("extruded_volume_total", 0.0);
    this->parser.set("extruded_weight_total", 0.0);

    // Reserve buffer for current position.
    this->position.assign(3, 0);
    this->output_config.set("position", this->position);

    // Store zhop variable into the parser itself, it is a read-only variable to the script.
    this->parser.set("zhop", writer.get_zhop());
}

void GCodeGenerator::PlaceholderParserIntegration::update_from_gcodewriter(
    const GCodeWriter& writer,
    const std::optional<WipeTowerData>& wipe_tower_data
)
{
    memcpy(this->position.data(), writer.get_position().data(), sizeof(double) * 3);
    this->output_config.set("position", this->position);

    if (this->num_extruders > 0) {
        const std::vector<Extruder> &extruders = writer.extruders();
        assert(! extruders.empty() && num_extruders == extruders.back().id() + 1);
        this->e_retracted.assign(num_extruders, 0);
        this->e_restart_extra.assign(num_extruders, 0);
        double total_volume = 0.;
        double total_weight = 0.;

        std::vector<double> extruded_volume(num_extruders);
        std::vector<double> extruded_weight(num_extruders);
        for (const Extruder &e : extruders) {
            this->e_retracted[e.id()]     = e.retracted();
            this->e_restart_extra[e.id()] = e.restart_extra();

            // Wipe tower filament consumption has to be added separately, because that gcode is not generated by GCodeWriter.
            double wt_vol = 0.;
            if (wipe_tower_data) {
                const std::vector<std::pair<float, std::vector<float>>>& wtuf = wipe_tower_data->used_filament_until_layer;
                auto it = std::lower_bound(wtuf.begin(), wtuf.end(), writer.get_position().z(),
                                [](const auto& a, const float& val) { return a.first < val; });
                if (it == wtuf.end())
                    it = wtuf.end() - 1;
                wt_vol = it->second[e.id()] * e.filament_crossection();
            }

            double v = e.extruded_volume() + wt_vol;
            double w = v * e.filament_density() * 0.001;
            extruded_volume.at(e.id()) = v;
            extruded_weight.at(e.id()) = w;
            total_volume += v;
            total_weight += w;
        }

        this->output_config.set("extruded_volume", extruded_volume);
        this->output_config.set("extruded_weight", extruded_weight);

        this->output_config.set("extruded_volume_total", total_volume);
        this->output_config.set("extruded_weight_total", total_weight);

        this->output_config.set("e_retracted", this->e_retracted);
        this->output_config.set("e_restart_extra", this->e_restart_extra);
        if (! writer.config.use_relative_e_distances) {
            this->e_position.assign(num_extruders, 0);
            for (const Extruder &e : extruders)
                this->e_position[e.id()] = e.position();
            this->output_config.set("e_position", this->e_position);
        }
    }
}

template <typename T>
std::vector<T> get_vector(const ParserConfig& config, const std::string& key) {
    const ParserValue value{*ASSERT_VAL(config.option(key))};
    ASSERT(is_vector(value));
    const ParserVector vector{std::get<ParserVector>(value)};
    return vector.get<T>();
}


// Throw if any of the output vector variables were resized by the script.
void GCodeGenerator::PlaceholderParserIntegration::validate_output_vector_variables()
{
    const auto position{get_vector<double>(output_config, "position")};

    if (position.size() != 3)
        throw Slic3r::RuntimeError("\"position\" output variable must not be resized by the script.");
    if (this->num_extruders > 0) {
        const auto e_position{
            output_config.option("e_position")
                ? std::optional{get_vector<double>(output_config, "e_position")}
                : std::nullopt
        };
        const auto e_retracted{get_vector<double>(output_config, "e_retracted")};
        const auto e_restart_extra{get_vector<double>(output_config, "e_restart_extra")};

        if (e_position && e_position->size() != this->num_extruders)
            throw Slic3r::RuntimeError("\"e_position\" output variable must not be resized by the script.");
        if (e_retracted.size() != this->num_extruders)
            throw Slic3r::RuntimeError("\"e_retracted\" output variable must not be resized by the script.");
        if (e_restart_extra.size() != this->num_extruders)
            throw Slic3r::RuntimeError("\"e_restart_extra\" output variable must not be resized by the script.");
    }
}

// Collect pairs of object_layer + support_layer sorted by print_z.
// object_layer & support_layer are considered to be on the same print_z, if they are not further than EPSILON.
GCodeGenerator::ObjectsLayerToPrint GCodeGenerator::collect_layers_to_print(const PrintObject& object)
{
    GCodeGenerator::ObjectsLayerToPrint layers_to_print;
    layers_to_print.reserve(object.layers().size() + object.support_layers().size());

    /*
    // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
    // This is the same logic as in support generator.
    //FIXME should we use the printing extruders instead?
    double gap_over_supports = object.config().support_material_contact_distance;
    // FIXME should we test object.config().support_material_synchronize_layers ? Currently the support layers are synchronized with object layers iff soluble supports.
    assert(!object.has_support() || gap_over_supports != 0. || object.config().support_material_synchronize_layers);
    if (gap_over_supports != 0.) {
        gap_over_supports = std::max(0., gap_over_supports);
        // Not a soluble support,
        double support_layer_height_min = 1000000.;
        for (auto lh : object.print()->config().min_layer_height.values)
            support_layer_height_min = std::min(support_layer_height_min, std::max(0.01, lh));
        gap_over_supports += support_layer_height_min;
    }*/

    std::vector<std::pair<double, double>> warning_ranges;

    // Pair the object layers with the support layers by z.
    size_t idx_object_layer = 0;
    size_t idx_support_layer = 0;
    const ObjectLayerToPrint* last_extrusion_layer = nullptr;
    while (idx_object_layer < object.layers().size() || idx_support_layer < object.support_layers().size()) {
        ObjectLayerToPrint layer_to_print;
        layer_to_print.object_layer = (idx_object_layer < object.layers().size()) ? object.layers()[idx_object_layer++] : nullptr;
        layer_to_print.support_layer = (idx_support_layer < object.support_layers().size()) ? object.support_layers()[idx_support_layer++] : nullptr;
        if (layer_to_print.object_layer && layer_to_print.support_layer) {
            if (layer_to_print.object_layer->print_z < layer_to_print.support_layer->print_z - EPSILON) {
                layer_to_print.support_layer = nullptr;
                --idx_support_layer;
            }
            else if (layer_to_print.support_layer->print_z < layer_to_print.object_layer->print_z - EPSILON) {
                layer_to_print.object_layer = nullptr;
                --idx_object_layer;
            }
        }

        layers_to_print.emplace_back(layer_to_print);

        bool has_extrusions = (layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions())
            || (layer_to_print.support_layer && layer_to_print.support_layer->has_extrusions());

        // Check that there are extrusions on the very first layer. The case with empty
        // first layer may result in skirt/brim in the air and maybe other issues.
        // UPDATE: Commented out for now - if we want to support floating objects,
        // this check must go. It is possible that some things (raft/skirt/brim and such)
        // are still broken though.
        /*if (layers_to_print.size() == 1u) {
            if (!has_extrusions) {
                throw Biz::Slicing::Exception{
                    Biz::Slicing::Error{
                        Biz::Slicing::ErrorCode::NoExtrusionInFirstLayer,
                        {},
                        object.model_object()->id()
                    }
                };
            }
        }*/

        // In case there are extrusions on this layer, check there is a layer to lay it on.
        if ((layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions())
            // Allow empty support layers, as the support generator may produce no extrusions for non-empty support regions.
            || (layer_to_print.support_layer /* && layer_to_print.support_layer->has_extrusions() */)) {
            double top_cd = object.config().get<double>("support_material_contact_distance");
            double bottom_cd = object.config().get<double>("support_material_bottom_contact_distance") == 0. ? top_cd : object.config().get<double>("support_material_bottom_contact_distance");

            double extra_gap = (layer_to_print.support_layer ? bottom_cd : top_cd);

            double maximal_print_z = (last_extrusion_layer ? last_extrusion_layer->print_z() : 0.)
                + layer_to_print.layer()->height
                + std::max(0., extra_gap);
            // Negative support_contact_z is not taken into account, it can result in false positives in cases
            // where previous layer has object extrusions too (https://github.com/prusa3d/PrusaSlicer/issues/2752)

            if (has_extrusions && layer_to_print.print_z() > maximal_print_z + 2. * EPSILON)
                warning_ranges.emplace_back(std::make_pair((last_extrusion_layer ? last_extrusion_layer->print_z() : 0.), layers_to_print.back().print_z()));
        }
        // Remember last layer with extrusions.
        if (has_extrusions)
            last_extrusion_layer = &layers_to_print.back();
    }

    if (! warning_ranges.empty()) {
        const_cast<Print*>(object.print())
            ->append_warning_callback(
                Biz::Slicing::Warning{
                    Biz::Slicing::WarningCode::EmptyLayers,
                    {},
                    object.model_object()->id(),
                    Biz::Slicing::EmptyLayersWarningPayload{warning_ranges}
                }
            );
    }

    return layers_to_print;
}

// Prepare for non-sequential printing of multiple objects: Support resp. object layers with nearly identical print_z
// will be printed for  all objects at once.
// Return a list of <print_z, per object ObjectLayerToPrint> items.
std::vector<std::pair<double, GCodeGenerator::ObjectsLayerToPrint>> GCodeGenerator::collect_layers_to_print(const Print& print)
{
    struct OrderingItem {
        double    print_z;
        size_t      object_idx;
        size_t      layer_idx;
    };

    std::vector<ObjectsLayerToPrint>  per_object(print.objects().size(), ObjectsLayerToPrint());
    std::vector<OrderingItem>         ordering;
    for (size_t i = 0; i < print.objects().size(); ++i) {
        per_object[i] = collect_layers_to_print(*print.objects()[i]);
        OrderingItem ordering_item;
        ordering_item.object_idx = i;
        ordering.reserve(ordering.size() + per_object[i].size());
        const ObjectLayerToPrint &front = per_object[i].front();
        for (const ObjectLayerToPrint &ltp : per_object[i]) {
            ordering_item.print_z = ltp.print_z();
            ordering_item.layer_idx = &ltp - &front;
            ordering.emplace_back(ordering_item);
        }
    }

    std::sort(ordering.begin(), ordering.end(), [](const OrderingItem& oi1, const OrderingItem& oi2) { return oi1.print_z < oi2.print_z; });

    std::vector<std::pair<double, ObjectsLayerToPrint>> layers_to_print;

    // Merge numerically very close Z values.
    for (size_t i = 0; i < ordering.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        double zmax = ordering[i].print_z + EPSILON;
        for (; j < ordering.size() && ordering[j].print_z <= zmax; ++j);
        // Merge into layers_to_print.
        std::pair<double, ObjectsLayerToPrint> merged;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        merged.first = 0.5 * (ordering[i].print_z + ordering[j - 1].print_z);
        merged.second.assign(print.objects().size(), ObjectLayerToPrint());
        for (; i < j; ++i) {
            const OrderingItem& oi = ordering[i];
            assert(merged.second[oi.object_idx].layer() == nullptr);
            merged.second[oi.object_idx] = std::move(per_object[oi.object_idx][oi.layer_idx]);
        }
        layers_to_print.emplace_back(std::move(merged));
    }

    return layers_to_print;
}

// free functions called by GCodeGenerator::do_export()
namespace DoExport {
    using namespace Biz::libpgcode;
    // if any reserved keyword is found, returns a std::vector containing the first MAX_COUNT keywords found
    // into pairs containing:
    // first: source
    // second: keyword
    // to be shown in the warning notification
    // The returned vector is empty if no keyword has been found
    static std::vector<std::pair<std::string, std::string>> validate_custom_gcode(const Print& print) {
        static const unsigned int MAX_TAGS_COUNT = 5;
        std::vector<std::pair<std::string, std::string>> ret;

        auto check = [&ret](const std::string& source, const std::string& gcode) {
            std::vector<std::string> tags;
            if (contains_reserved_tags(gcode, Biz::libpgcode::RESERVED_TAGS, MAX_TAGS_COUNT, tags)) {
                if (!tags.empty()) {
                    size_t i = 0;
                    while (ret.size() < MAX_TAGS_COUNT && i < tags.size()) {
                        ret.push_back({ source, tags[i] });
                        ++i;
                    }
                }
            }
        };

        const PrintConfigView& config = print.config();
        check(_u8L("Start G-code"), config.get<std::string>("start_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("End G-code"), config.get<std::string>("end_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Before layer change G-code"), config.get<std::string>("before_layer_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("After layer change G-code"), config.get<std::string>("layer_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Tool change G-code"), config.get<std::string>("toolchange_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Between objects G-code (for sequential printing)"), config.get<std::string>("between_objects_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Color Change G-code"), config.get<std::string>("color_change_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Pause Print G-code"), config.get<std::string>("pause_print_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) check(_u8L("Template Custom G-code"), config.get<std::string>("template_custom_gcode"));
        if (ret.size() < MAX_TAGS_COUNT) {
            for (const std::string& value : config.get<std::vector<std::string>>("start_filament_gcode")) {
                check(_u8L("Filament Start G-code"), value);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }
        if (ret.size() < MAX_TAGS_COUNT) {
            for (const std::string& value : config.get<std::vector<std::string>>("end_filament_gcode")) {
                check(_u8L("Filament End G-code"), value);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }
        if (ret.size() < MAX_TAGS_COUNT) {
            const auto custom_gcode_per_print_z{print.custom_gcode()};
            if (custom_gcode_per_print_z) {
                for (const auto& gcode : custom_gcode_per_print_z->get().gcodes) {
                    check(_u8L("Custom G-code"), gcode.extra);
                    if (ret.size() == MAX_TAGS_COUNT)
                        break;
                }
            }
        }

        return ret;
    }

    std::vector<float> double_to_float(const std::vector<double>& src)
    {
        std::vector<float> ret;
        std::transform(src.begin(), src.end(), std::back_inserter(ret), [](double value) { return float(value); });
        return ret;
    }

    static MachineLimitsConfig convert(const PrintConfigView& config)
    {
        MachineLimitsConfig ret;
        ret.usage = MachineLimitsUsageType(int(config.get<Domain::MachineLimitsUsage>("machine_limits_usage")));
        ret.max_acceleration_x = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_x"));
        ret.max_acceleration_y = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_y"));
        ret.max_acceleration_z = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_z"));
        ret.max_acceleration_e = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_e"));
        ret.max_feedrate_x = double_to_float(config.get<std::vector<double>>("machine_max_feedrate_x"));
        ret.max_feedrate_y = double_to_float(config.get<std::vector<double>>("machine_max_feedrate_y"));
        ret.max_feedrate_z = double_to_float(config.get<std::vector<double>>("machine_max_feedrate_z"));
        ret.max_feedrate_e = double_to_float(config.get<std::vector<double>>("machine_max_feedrate_e"));
        ret.max_jerk_x = double_to_float(config.get<std::vector<double>>("machine_max_jerk_x"));
        ret.max_jerk_y = double_to_float(config.get<std::vector<double>>("machine_max_jerk_y"));
        ret.max_jerk_z = double_to_float(config.get<std::vector<double>>("machine_max_jerk_z"));
        ret.max_jerk_e = double_to_float(config.get<std::vector<double>>("machine_max_jerk_e"));
        ret.max_acceleration_extruding = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_extruding"));
        ret.max_acceleration_retracting = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_retracting"));
        ret.max_acceleration_travel = double_to_float(config.get<std::vector<double>>("machine_max_acceleration_travel"));
        ret.max_junction_deviation = double_to_float(config.get<std::vector<double>>("machine_max_junction_deviation"));
        ret.min_travel_rate = double_to_float(config.get<std::vector<double>>("machine_min_travel_rate"));
        ret.min_extruding_rate = double_to_float(config.get<std::vector<double>>("machine_min_extruding_rate"));
        return ret;
    }

    static ProcessorConfig populate_processor_config(
        const PrintConfigView& config,
        const Domain::Preset::HwPrinterConfig& hw_printer_config
    )
    {
        ProcessorConfig processor_config;
        processor_config.producer = GCodeProducer::PrusaSlicer;
        processor_config.flavor = config.get<GCodeFlavor>("gcode_flavor");
        processor_config.use_volumetric_e = config.get<bool>("use_volumetric_e");
        processor_config.export_remaining_time_enabled = config.get<bool>("remaining_times");
        processor_config.spiral_vase_enabled = config.get<bool>("spiral_vase");

        const std::optional<bool> supports_tool_preheating{Domain::Preset::get_feature<bool>(
            hw_printer_config.features,
            "supports_tool_preheating"
        )};
        // Orca: tools are also preheated with ooze prevention on a multi-tool printer (not a single extruder MMU)
        processor_config.preheat_time  = float(config.get<double>("preheat_time"));
        processor_config.preheat_steps = unsigned(std::max(1, config.get<int>("preheat_steps")));
        processor_config.do_M104_backtrace = processor_config.preheat_time > 0.f &&
            ((supports_tool_preheating && *supports_tool_preheating) ||
             (config.get<bool>("ooze_prevention") && !config.get<bool>("single_extruder_multi_material") &&
              hw_printer_config.tool_slot_counts().size() > 1));
        processor_config.extruders.count = config.hw_config().material_slot_count();

        std::vector<Vec2f> out_bed_shape;
        const auto in_bed_shape{config.get<std::vector<Vec2d>>("bed_shape")};
        std::transform(in_bed_shape.begin(), in_bed_shape.end(), std::back_inserter(out_bed_shape),
            [](const Vec2d& v) { return v.cast<float>(); });
        processor_config.bed_shape = out_bed_shape;

        processor_config.extruders.temps_first_layer_config = config.get<std::vector<int>>("first_layer_temperature");
        processor_config.extruders.temps_config = config.get<std::vector<int>>("temperature");
        processor_config.extruders.offsets.reserve(config.get<std::vector<Vec2d>>("extruder_offset").size());
        for (size_t i = 0; i < config.get<std::vector<Vec2d>>("extruder_offset").size(); ++i) {
            const Vec2f offset2d = config.get<std::vector<Vec2d>>("extruder_offset").at(i).cast<float>().eval();
            const Vec3f offset3d = { offset2d.x(), offset2d.y(), 0.0f };
            processor_config.extruders.offsets.emplace_back(offset3d);
        }
        processor_config.filaments.costs = double_to_float(config.get<std::vector<double>>("filament_cost"));
        processor_config.filaments.densities = double_to_float(config.get<std::vector<double>>("filament_density"));
        processor_config.filaments.diameters = double_to_float(config.get<std::vector<double>>("filament_diameter"));

        processor_config.z_offset = config.get<double>("z_offset");

        const auto layer_height{config.get<double>("layer_height")};
        processor_config.first_layer_height = config.get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(layer_height);
        processor_config.max_print_height = config.get<double>("max_print_height");
        processor_config.color_change_gcode = config.get<std::string>("color_change_gcode");
        processor_config.pause_print_gcode = config.get<std::string>("pause_print_gcode");
        processor_config.template_custom_gcode = config.get<std::string>("template_custom_gcode");
        processor_config.single_extruder_multi_material = config.get<bool>("single_extruder_multi_material");
        if (processor_config.single_extruder_multi_material && processor_config.extruders.count > 1 && config.get<bool>("wipe_tower")) {
            processor_config.parking_pos_retraction = float(config.get<double>("parking_pos_retraction"));
            processor_config.extra_loading_move = float(config.get<double>("extra_loading_move"));
        }

        if ((processor_config.flavor == GCodeFlavor::gcfMarlinLegacy
             || processor_config.flavor == GCodeFlavor::gcfMarlinFirmware
             || processor_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy
             || processor_config.flavor == GCodeFlavor::gcfRepRapFirmware
             || processor_config.flavor == GCodeFlavor::gcfKlipper)
            && config.get<Domain::MachineLimitsUsage>("machine_limits_usage")
                != Domain::MachineLimitsUsage::Ignore)
        {
            processor_config.machine_limits = convert(config);

            if (processor_config.flavor == GCodeFlavor::gcfMarlinLegacy || processor_config.flavor == GCodeFlavor::gcfKlipper) {
                // Legacy Marlin and Klipper don't have separate travel acceleration, they use the 'extruding' value instead.
                processor_config.machine_limits.max_acceleration_travel = processor_config.machine_limits.max_acceleration_extruding;
            }
            if (processor_config.flavor == GCodeFlavor::gcfRepRapFirmware) {
                // RRF does not support setting min feedrates. Set them to zero.
                processor_config.machine_limits.min_travel_rate.assign(processor_config.machine_limits.min_travel_rate.size(), 0.0f);
                processor_config.machine_limits.min_extruding_rate.assign(processor_config.machine_limits.min_extruding_rate.size(), 0.0f);
            }
        }
        else
            processor_config.machine_limits.usage = MachineLimitsUsageType::Ignore;

        // No Klipper here, it does not support silent mode.
        if (processor_config.flavor == GCodeFlavor::gcfMarlinLegacy
            || processor_config.flavor == GCodeFlavor::gcfMarlinFirmware
            || processor_config.flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
        {
            if (config.get<bool>("silent_mode")
                && processor_config.machine_limits.max_acceleration_x.size() > 1)
            {
                processor_config.stealth_time_estimator_enabled = true;
            }
        }

        // Filament load / unload times are not specific to a firmware flavor. Let anybody use it if they find it useful.
        // As of now the fields are shown at the UI dialog in the same combo box as the ramming values, so they
        // are considered to be active for the single extruder multi-material printers only.
        processor_config.filament_change_time = (float) config.get<double>("filament_change_time");

        processor_config.extruders.str_colors = config.get<std::vector<std::string>>("extruder_colour");

        processor_config.callbacks.cb_log = [](const std::string& msg){
            SPDLOG_WARN(msg);
        };

        return processor_config;
    }

} // namespace DoExport

GCodeGenerator::GCodeGenerator(const Print* print) :
    m_origin(Vec2d::Zero()),
    m_extruder_offset(print->config().get<std::vector<Vec2d>>("extruder_offset")),
    m_wipe_enabled(print->config().get<std::vector<bool>>("wipe")),
    m_scaled_resolution(scaled<double>(print->config().get<double>("gcode_resolution"))),
    m_writer(Slicing::GCodeWriterConfig{print->config()}),
    m_enable_loop_clipping(true),
    m_enable_cooling_markers(false),
    m_enable_extrusion_role_markers(false),
    m_last_processor_extrusion_role(GCodeExtrusionRole::None),
    m_layer_count(0),
    m_layer_index(-1),
    m_layer(nullptr),
    m_object_layer_over_raft(false),
    m_last_extrusion_role(GCodeExtrusionRole::None),
    m_last_width(0.0f),
    m_second_layer_things_done(false),
    m_print(print)
{
}


Biz::libpgcode::ProcessorResult GCodeGenerator::do_export(
    Print* print,
    const Biz::Slicing::SerializedConfig& serialized_config
)
{
    using namespace Biz::libpgcode;
    CNumericLocalesSetter locales_setter;

    // Enabled and either not done, or marked as done while the output file is missing.
    print->set_started(psGCodeExport);

    // check if any custom gcode contains keywords used by the gcode processor to
    // produce time estimation and gcode toolpaths
    std::vector<std::pair<std::string, std::string>> validation_res = DoExport::validate_custom_gcode(*print);
    if (!validation_res.empty()) {
        std::string reports;
        for (const auto& [source, keyword] : validation_res) {
            reports += source + ": \"" + keyword + "\"\n";
        }
        print->append_warning_callback(
            Biz::Slicing::Warning{
                Biz::Slicing::WarningCode::CustomGCodeReservedKeywords,
                {},
                std::nullopt,
                Biz::Slicing::CustomGCodeReservedKeywordsWarningPayload{reports}
            }
        );
    }

    SPDLOG_INFO("Exporting G-code... {}", log_memory_info());

    ProcessorConfig processor_config = DoExport::populate_processor_config(
        print->config(),
        print->config().hw_config()
    );
    Processor processor(std::move(processor_config));
    GCodeOutputStream file(processor);

    const Domain::ExtraPrintStatistics extra_print_statistics{this->_do_export(*print, file, serialized_config)};

    if (! m_placeholder_parser_integration.failed_templates.empty()) {
        // G-code export proceeded, but some of the PlaceholderParser substitutions failed.
        std::vector<std::string> failed_config_keys;
        for (const auto& [name, error] : m_placeholder_parser_integration.failed_templates)
            failed_config_keys.emplace_back(name);
        throw Biz::Slicing::Exception{
            Biz::Slicing::Error{
                Biz::Slicing::ErrorCode::PlaceholderParser,
                failed_config_keys,
                {},
                Biz::Slicing::PlaceholderParserErrorPayload{m_placeholder_parser_integration.failed_templates}
        }};
    }

    SPDLOG_DEBUG("Start processing gcode, {}", log_memory_info());
    ProcessorResult result{processor.finalize()};
    PostProcessorConfig post_processor_config = processor.post_processor_config();
    result                                    = GCode::post_process(
        post_processor_config,
        std::move(result),
        m_writer.extruders(),
        extra_print_statistics,
        [print](Biz::Slicing::Warning warning)
        { print->append_warning_callback(std::move(warning)); }
    );
    // Orca (gcode_add_line_number): every line of the finished G-code starts with N<line>
    if (print->config().get<bool>("gcode_add_line_number")) {
        const std::string_view src = result.gcode().str();
        std::string numbered;
        numbered.reserve(src.size() + src.size() / 4);
        size_t line_number = 1;
        for (size_t pos = 0; pos < src.size();) {
            size_t end = src.find('\n', pos);
            if (end == std::string_view::npos)
                end = src.size();
            numbered += 'N';
            numbered += std::to_string(line_number++);
            numbered += ' ';
            numbered.append(src.substr(pos, end - pos));
            numbered += '\n';
            pos = end + 1;
        }
        result.set_gcode(std::move(numbered));
    }
    SPDLOG_DEBUG("Finished processing gcode, {}", log_memory_info());

    print->set_done(psGCodeExport);

    return result;
}

// free functions called by GCodeGenerator::_do_export()
namespace DoExport {

    static double extruder_autospeed_volumetric_limit(const Print &print, unsigned extruder_id)
	{
        using Domain::FloatOrPercentage;
	    // get the minimum cross-section used in the print
	    std::vector<double> mm3_per_mm;
	    for (auto object : print.objects()) {
	        for (size_t region_id = 0; region_id < object->num_printing_regions(); ++ region_id) {
	            const PrintRegion &region = object->printing_region(region_id);
	            for (auto layer : object->layers()) {
	                const LayerRegion* layerm = layer->regions()[region_id];

	                if (region.config().get<std::vector<double>>("perimeter_speed").at(extruder_id) == 0.0 ||
	                    region.config().get<std::vector<FloatOrPercentage>>("small_perimeter_speed").at(extruder_id).is_zero() ||
	                    region.config().get<std::vector<FloatOrPercentage>>("external_perimeter_speed").at(extruder_id).is_zero() ||
	                    region.config().get<std::vector<double>>("bridge_speed").at(extruder_id) == 0.0)
	                    mm3_per_mm.push_back(layerm->perimeters().min_mm3_per_mm());
	                if (region.config().get<std::vector<double>>("infill_speed").at(extruder_id) == 0.0 ||
	                    region.config().get<std::vector<FloatOrPercentage>>("solid_infill_speed").at(extruder_id).is_zero() ||
	                    region.config().get<std::vector<FloatOrPercentage>>("top_solid_infill_speed").at(extruder_id).is_zero() ||
                        region.config().get<std::vector<double>>("bridge_speed").at(extruder_id) == 0.0 ||
                        region.config().get<std::vector<FloatOrPercentage>>("over_bridge_speed").at(extruder_id).is_zero())
                    {
                        // Minimal volumetric flow should not be calculated over ironing extrusions.
                        // Use following lambda instead of the built-it method.
                        // https://github.com/prusa3d/PrusaSlicer/issues/5082
                        auto min_mm3_per_mm_no_ironing = [](const ExtrusionEntityCollection& eec) -> double {
                            double min = std::numeric_limits<double>::max();
                            for (const ExtrusionEntity* ee : eec.entities)
                                if (ee->role() != ExtrusionRole::Ironing)
                                    min = std::min(min, ee->min_mm3_per_mm());
                            return min;
                        };

                        mm3_per_mm.push_back(min_mm3_per_mm_no_ironing(layerm->fills()));
                    }
	            }
	        }
	        if (object->config().get<double>("support_material_speed") == 0.0 ||
	            object->config().get<FloatOrPercentage>("support_material_interface_speed").is_zero())
	            for (auto layer : object->support_layers())
	                mm3_per_mm.push_back(layer->support_fills.min_mm3_per_mm());
	    }
	    // filter out 0-width segments
	    mm3_per_mm.erase(std::remove_if(mm3_per_mm.begin(), mm3_per_mm.end(), [](double v) { return v < 0.000001; }), mm3_per_mm.end());
	    double volumetric_speed = 0.;
	    if (! mm3_per_mm.empty()) {
	        // In order to honor max_print_speed we need to find a target volumetric
	        // speed that we can use throughout the print. So we define this target 
	        // volumetric speed as the volumetric speed produced by printing the 
	        // smallest cross-section at the maximum speed: any larger cross-section
	        // will need slower feedrates.
	        volumetric_speed = *std::min_element(mm3_per_mm.begin(), mm3_per_mm.end()) * print.config().get<std::vector<double>>("max_print_speed").at(extruder_id);
	        // limit such volumetric speed with max_volumetric_speed if set
	        if (print.config().get<std::vector<double>>("max_volumetric_speed").at(extruder_id) > 0)
	            volumetric_speed = std::min(volumetric_speed, print.config().get<std::vector<double>>("max_volumetric_speed").at(extruder_id));
	    }
	    return volumetric_speed;
	}

    static std::map<unsigned, double> autospeed_volumetric_limit(const Print &print)
    {
        std::map<unsigned, double> result;
        for (unsigned extruder_id : print.get_extruder_candidates()) {
            result.insert({extruder_id, extruder_autospeed_volumetric_limit(print, extruder_id)});
        }
        return result;
    }

    static void init_ooze_prevention(const Print &print, OozePrevention &ooze_prevention)
	{
	    ooze_prevention.enable = print.config().get<bool>("ooze_prevention") && ! print.config().get<bool>("single_extruder_multi_material");
    }

    static Domain::ExtraPrintStatistics get_extra_print_statistics(
        const std::optional<WipeTowerData>& wipe_tower_data,
        const PrintConfigView& config,
        const std::vector<Extruder>& extruders,
        unsigned int initial_extruder_id,
        int total_toolchanges
    )
    {
        Domain::ExtraPrintStatistics print_statistics;
        std::string filament_stats_string_out;

        print_statistics.total_toolchanges   = total_toolchanges;
        print_statistics.time_cost           = config.get<double>("time_cost");
        print_statistics.initial_extruder_id = initial_extruder_id;
        if (!extruders.empty()) {
            for (const Extruder& extruder : extruders) {
                print_statistics.printing_extruders.emplace_back(extruder.id());
                print_statistics.printing_filament_types.emplace_back(
                    config.get<std::vector<std::string>>("filament_type").at(extruder.id())
                );

                if (wipe_tower_data) {
                    const double wipe_tower_filament{
                        wipe_tower_data->used_filament_until_layer.back().second[extruder.id()]
                    };

                    print_statistics.total_wipe_tower_filament += wipe_tower_filament;

                    const double wipe_tower_filament_volume{
                        wipe_tower_filament * extruder.filament_crossection() * 0.001
                    };
                    print_statistics.total_wipe_tower_filament_volume += wipe_tower_filament_volume;

                    const double wipe_tower_filament_weight{
                        wipe_tower_filament
                        * extruder.filament_crossection()
                        * extruder.filament_density()
                        * 0.001
                    };
                    print_statistics.total_wipe_tower_filament_weight += wipe_tower_filament_weight;
                    print_statistics.total_wipe_tower_cost +=
                        wipe_tower_filament_weight * extruder.filament_cost() * 0.001;
                }
            }

            print_statistics.initial_filament_type =
                config.get<std::vector<std::string>>("filament_type").at(initial_extruder_id);
            std::ranges::sort(print_statistics.printing_filament_types);
        }
        return print_statistics;
    }
}

#if 0
// Sort the PrintObjects by their increasing Z, likely useful for avoiding colisions on Deltas during sequential prints.
static inline std::vector<const PrintInstance*> sort_object_instances_by_max_z(const Print &print)
{
    std::vector<const PrintObject*> objects(print.objects().begin(), print.objects().end());
    std::sort(objects.begin(), objects.end(), [](const PrintObject *po1, const PrintObject *po2) { return po1->height() < po2->height(); });
    std::vector<const PrintInstance*> instances;
    instances.reserve(objects.size());
    for (const PrintObject *object : objects)
        for (size_t i = 0; i < object->instances().size(); ++ i)
            instances.emplace_back(&object->instances()[i]);
    return instances;
}
#endif

// Produce a vector of PrintObjects in the order of their respective ModelObjects in print.model().
std::vector<const PrintInstance*> sort_object_instances_by_model_order(const Print& print)
{
    std::vector<const PrintInstance*> instances;
    for (const PrintObject *print_object : print.objects()) {
        for (const PrintInstance &print_instance : print_object->instances()) {
            instances.emplace_back(&print_instance);
        }
    }
    std::sort(instances.begin(), instances.end(), [](const PrintInstance* a, const PrintInstance* b) {
        return a->model_instance_index < b->model_instance_index;
    });

    return instances;
}

static inline bool arc_welder_enabled(const PrintConfigView& print_config)
{
    return
        // Enabled
        print_config.get<Domain::ArcFittingType>("arc_fitting") != Domain::ArcFittingType::Disabled &&
        // Not a spiral vase print
        !print_config.get<bool>("spiral_vase") &&
        // Presure equalizer not used
        print_config.get<double>("max_volumetric_extrusion_rate_slope_negative") == 0. &&
        print_config.get<double>("max_volumetric_extrusion_rate_slope_positive") == 0.;
}

static inline GCode::SmoothPathCache::InterpolationParameters interpolation_parameters(const PrintConfigView& print_config)
{
    return {
        scaled<double>(print_config.get<double>("gcode_resolution")),
        arc_welder_enabled(print_config) ? Geometry::ArcWelder::default_arc_length_percent_tolerance : 0
    };
}

static inline GCode::SmoothPathCache smooth_path_interpolate_global(const Print& print)
{
    const GCode::SmoothPathCache::InterpolationParameters interpolation_params = interpolation_parameters(print.config());
    GCode::SmoothPathCache out;
    out.interpolate_add(print.skirt(), interpolation_params);
    out.interpolate_add(print.brim(), interpolation_params);
    return out;
}

static inline bool is_mk2_or_mk3(const std::string &printer_model) {
    if (boost::starts_with(printer_model, "MK2")) {
        return true;
    } else if (boost::starts_with(printer_model, "MK3") && (printer_model.size() <= 3 || printer_model[3] != '.')) {
        // Ignore MK3.5 and MK3.9.
        return true;
    }

    return false;
}

static inline std::optional<std::string> find_M84(const std::string &gcode) {
    std::istringstream gcode_is(gcode);
    std::string gcode_line;
    while (std::getline(gcode_is, gcode_line)) {
        boost::trim(gcode_line);

        if (gcode_line == "M84" || boost::starts_with(gcode_line, "M84 ") || boost::starts_with(gcode_line, "M84;")) {
            return gcode_line;
        }
    }

    return std::nullopt;
}

Domain::ExtraPrintStatistics GCodeGenerator::_do_export(
    Print& print,
    GCodeOutputStream& file,
    const Biz::Slicing::SerializedConfig& serialized_config
)
{
    std::string prepared_by_info;
    if (const char* extras = boost::nowide::getenv("SLIC3R_PREPARED_BY_INFO"); extras) {
        std::string str(extras);
        if (str.size() < 50 && std::all_of(str.begin(), str.end(), [](char c) { return c < 127 && c != '\n' && c != '\r'; }))
            prepared_by_info = extras;
        else {
            SPDLOG_ERROR("Value in SLIC3R_PREPARED_BY_INFO env variable is invalid. Closing.");
            std::terminate();
        }
    }

    using Biz::libpgcode::reserved_tag;
    using Biz::libpgcode::Tags;

    if (! print.config().get<std::vector<std::string>>("gcode_substitutions").empty()) {
        m_find_replace = std::make_unique<GCodeFindReplace>(print.config());
        file.set_find_replace(m_find_replace.get(), false);
    }

    // resets analyzer's tracking data
    m_last_height  = 0.f;
    m_last_layer_z = 0.f;
    m_max_layer_z  = 0.f;
    m_last_width = 0.f;

    // How many times will be change_layer() called?gcode.cpp
    // change_layer() in turn increments the progress bar status.
    m_layer_count = 0;
    if (print.config().get<bool>("complete_objects")) {
        // Add each of the object's layers separately.
        for (auto object : print.objects()) {
            std::vector<double> zs;
            zs.reserve(object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.push_back(layer->print_z);
            for (auto layer : object->support_layers())
                zs.push_back(layer->print_z);
            std::sort(zs.begin(), zs.end());
            m_layer_count += (unsigned int)(object->instances().size() * (std::unique(zs.begin(), zs.end()) - zs.begin()));
        }
    }
    print.throw_if_canceled();

    m_enable_cooling_markers = true;

    m_volumetric_speed = DoExport::autospeed_volumetric_limit(print);
    print.throw_if_canceled();

    if (print.config().get<bool>("spiral_vase"))
        m_spiral_vase = std::make_unique<SpiralVase>(print.config());

    if (print.config().get<double>("max_volumetric_extrusion_rate_slope_positive") > 0 ||
        print.config().get<double>("max_volumetric_extrusion_rate_slope_negative") > 0)
        m_pressure_equalizer = std::make_unique<PressureEqualizer>(print.config());
    m_enable_extrusion_role_markers = (bool)m_pressure_equalizer;

    if (const double fan_speedup_time = print.config().get<double>("fan_speedup_time"), fan_kickstart = print.config().get<double>("fan_kickstart");
        fan_speedup_time != 0. || fan_kickstart > 0.)
        m_fan_mover = std::make_unique<FanMover>(m_writer, float(std::abs(fan_speedup_time)),
                                                 print.config().get<bool>("fan_speedup_overhangs"), float(fan_kickstart),
                                                 print.config().get<int>("part_cooling_fan_min_pwm"));

    if (print.config().get<bool>("small_area_infill_flow_compensation"))
        m_small_area_infill_flow_compensator.emplace(print.config().get<std::string>("small_area_infill_flow_compensation_model"));

    if (auto pa_processor = std::make_unique<AdaptivePAProcessor>(m_writer, print.config()); pa_processor->enabled()) {
        const auto pressure_advance = print.config().get<std::vector<PressureAdvance>>("pressure_advance");
        const auto adaptive_pa      = print.config().get<std::vector<bool>>("adaptive_pressure_advance");
        const auto overhangs        = print.config().get<std::vector<bool>>("adaptive_pressure_advance_overhangs");
        for (size_t i = 0; i < pressure_advance.size(); ++i) {
            m_adaptive_pa.push_back(pressure_advance[i] != PressureAdvance::Disabled && adaptive_pa.at(i));
            m_adaptive_pa_overhangs.push_back(m_adaptive_pa.back() && overhangs.at(i));
        }
        m_pa_processor = std::move(pa_processor);
    }

    if (print.config().get<bool>("avoid_crossing_curled_overhangs")){
        const auto pts = print.config().get<std::vector<Vec2d>>("bed_shape");
        Points pts_scaled(pts.size());
        std::transform(pts.cbegin(), pts.cend(), pts_scaled.begin(), [](const Vec2d& pt) -> Point { return scaled(pt);});
        this->m_avoid_crossing_curled_overhangs.init_bed_shape(pts_scaled);
    }


    // Prepare the helper object for replacing placeholders in custom G-code and output filename.
    m_placeholder_parser_integration.parser = print.placeholder_parser();
    m_placeholder_parser_integration.parser.update_timestamp();
    m_placeholder_parser_integration.context.rng = std::mt19937(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    // Enable passing global variables between PlaceholderParser invocations.
    m_placeholder_parser_integration.context.global_config = std::make_unique<ParserConfig>();

    m_placeholder_parser_integration.parser.apply_config(print.get_object_placeholders());

    // Get optimal tool ordering to minimize tool switches of a multi-exruder print.
    // For a print by objects, find the 1st printing object.
    ToolOrdering tool_ordering;
    unsigned int initial_extruder_id = (unsigned int)-1;
    unsigned int final_extruder_id   = (unsigned int)-1;
    bool         has_wipe_tower      = false;
    std::vector<const PrintInstance*> 					print_object_instances_ordering;
    std::vector<const PrintInstance*>::const_iterator 	print_object_instance_sequential_active;
    if (print.config().get<bool>("complete_objects")) {
        // Order object instances for sequential print.
        print_object_instances_ordering = sort_object_instances_by_model_order(print);
//        print_object_instances_ordering = sort_object_instances_by_max_z(print);
        // Find the 1st printing object, find its tool ordering and the initial extruder ID.
        print_object_instance_sequential_active = print_object_instances_ordering.begin();
        for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++ print_object_instance_sequential_active) {
            tool_ordering = ToolOrdering(*(*print_object_instance_sequential_active)->print_object, initial_extruder_id);
            if ((initial_extruder_id = tool_ordering.first_extruder()) != static_cast<unsigned int>(-1))
                break;
        }
        if (initial_extruder_id == static_cast<unsigned int>(-1)) {
            // No object to print was found, cancel the G-code export.
            throw Biz::Slicing::Exception{
                Biz::Slicing::Error{Biz::Slicing::ErrorCode::NoExtrusions}
            };
        }
        // We don't allow switching of extruders per layer by Model::custom_gcode_per_print_z in sequential mode.
        // Use the extruder IDs collected from Regions.
        this->set_extruders(print.extruders(), print.config());
    } else {
        // Find tool ordering for all the objects at once, and the initial extruder ID.
        // If the tool ordering has been pre-calculated by Print class for wipe tower already, reuse it.
        tool_ordering = print.tool_ordering();
        tool_ordering.assign_custom_gcodes(print);
        if (tool_ordering.all_extruders().empty()) {
            // No object to print was found, cancel the G-code export.
            throw Biz::Slicing::Exception{
                Biz::Slicing::Error{Biz::Slicing::ErrorCode::NoExtrusions}
            };
        }
        has_wipe_tower = print.wipe_tower_data().has_value();
        initial_extruder_id = (has_wipe_tower && ! print.config().get<bool>("single_extruder_multi_material_priming")) ?
            // The priming towers will be skipped.
            tool_ordering.all_extruders().back() :
            // Don't skip the priming towers.
            tool_ordering.first_extruder();
        // In non-sequential print, the printing extruders may have been modified by the extruder switches stored in Model::custom_gcode_per_print_z.
        // Therefore initialize the printing extruders from there.
        this->set_extruders(tool_ordering.all_extruders(), print.config());
        // Orca print_order: the order of the object instances
        switch (print.config().get<Domain::PrintOrder>("print_order")) {
        case Domain::PrintOrder::AsObjectList:     print_object_instances_ordering = sort_object_instances_by_model_order(print); break;
        case Domain::PrintOrder::Snake:            print_object_instances_ordering = chain_print_object_instances_snake(print); break;
        case Domain::PrintOrder::BestOfStrategies: print_object_instances_ordering = chain_print_object_instances_best_of(print); break;
        default:                                   print_object_instances_ordering = chain_print_object_instances(print); break;
        }
        m_layer_count = tool_ordering.layer_tools().size();
    }
    // Orca bed_temperature_formula "by highest temp": the filaments printing the first layer / all used ones
    m_highest_bed_temperature.reset();
    if (print.config().get<Domain::BedTemperatureFormula>("bed_temperature_formula") == Domain::BedTemperatureFormula::ByHighestTemp
        && !tool_ordering.layer_tools().empty()) {
        const auto first_layer_temps = print.config().get<std::vector<int>>("first_layer_bed_temperature");
        const auto temps             = print.config().get<std::vector<int>>("bed_temperature");
        int first_layer = 0;
        int other       = 0;
        for (unsigned int id : tool_ordering.layer_tools().front().extruders)
            first_layer = std::max(first_layer, first_layer_temps.at(id));
        for (unsigned int id : tool_ordering.all_extruders())
            other = std::max(other, temps.at(id));
        m_highest_bed_temperature = std::make_pair(first_layer, other);
    }
    if (initial_extruder_id == (unsigned int)-1) {
        // Nothing to print!
        initial_extruder_id = 0;
        final_extruder_id   = 0;
    } else {
        final_extruder_id = tool_ordering.last_extruder();
        assert(final_extruder_id != (unsigned int)-1);
    }
    print.throw_if_canceled();

    // Update output variables after the extruders were initialized.
    m_placeholder_parser_integration.init(m_writer);
    // Let the start-up script prime the 1st printing tool.
    this->placeholder_parser().set("initial_tool", static_cast<int>(initial_extruder_id));
    this->placeholder_parser().set("initial_extruder", static_cast<int>(initial_extruder_id));
    this->placeholder_parser().set("current_extruder", static_cast<int>(initial_extruder_id));
    //Set variable for total layer count so it can be used in custom gcode.
    this->placeholder_parser().set("total_layer_count", static_cast<int>(m_layer_count));
    // Useful for sequential prints.
    this->placeholder_parser().set("current_object_idx", 0);
    // For the start / end G-code to do the priming and final filament pull in case there is no wipe tower provided.
    this->placeholder_parser().set("has_wipe_tower", has_wipe_tower);
    this->placeholder_parser().set("has_single_extruder_multi_material_priming", has_wipe_tower && print.config().get<bool>("single_extruder_multi_material_priming"));
    this->placeholder_parser().set("total_toolchanges", tool_ordering.toolchanges_count());
    {
        BoundingBoxf bbox(BB::construct(print.config().get<std::vector<Vec2d>>("bed_shape")));
        assert(bbox.defined);
        if (! bbox.defined)
            // This should not happen, but let's make the compiler happy.
            bbox.min = bbox.max = Vec2d::Zero();
        this->placeholder_parser().set("print_bed_min",  std::vector<double>{ bbox.min.x(), bbox.min.y() });
        this->placeholder_parser().set("print_bed_max",  std::vector<double>{ bbox.max.x(), bbox.max.y() });
        this->placeholder_parser().set("print_bed_size", std::vector<double>{ BB::sizes(bbox).x(), BB::sizes(bbox).y() });
    }
    {
        // Convex hull of the 1st layer extrusions, for bed leveling and placing the initial purge line.
        // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
        // It does NOT encompass user extrusions generated by custom G-code,
        // therefore it does NOT encompass the initial purge line.
        // It does NOT encompass MMU/MMU2 starting (wipe) areas.
        std::vector<Vec2d> pts;
        pts.reserve(print.first_layer_convex_hull().size());
        for (const Point &pt : print.first_layer_convex_hull().points)
            pts.emplace_back(unscale(pt));
        BoundingBoxf bbox(BB::construct(pts));
        this->placeholder_parser().set("first_layer_print_convex_hull", pts);
        this->placeholder_parser().set("first_layer_print_min",  std::vector<double>{ bbox.min.x(), bbox.min.y() });
        this->placeholder_parser().set("first_layer_print_max",  std::vector<double>{ bbox.max.x(), bbox.max.y() });
        this->placeholder_parser().set("first_layer_print_size", std::vector<double>{ BB::sizes(bbox).x(), BB::sizes(bbox).y() });
        // PlaceholderParser currently substitues non-existent vector values with the zero'th value, which is harmful in the case of "is_extruder_used[]"
        // as Slicer may lie about availability of such non-existent extruder.
        // We rather sacrifice 256B of memory before we change the behavior of the PlaceholderParser, which should really only fill in the non-existent
        // vector elements for filament parameters.
        std::vector<bool> is_extruder_used(std::max(size_t(255), print.config().hw_config().material_slot_count()), false);
        for (unsigned int extruder_id : tool_ordering.all_extruders())
            is_extruder_used[extruder_id] = true;
        this->placeholder_parser().set("is_extruder_used", is_extruder_used);

        // Orca's runtime variables (GCode::_do_export), 3.0's material slots being Orca's filaments
        // and 3.0's tools Orca's extruders / nozzles (no nozzle grouping: nozzle id = hotend = extruder)
        {
            const Domain::Preset::HwPrinterConfig& hw = print.config().hw_config();
            const size_t tool_count = std::max<size_t>(1, hw.tool_count);
            const auto   tool_of    = [&hw](size_t slot) { return int(hw.tool_of_material_slot(slot)); };
            const std::vector<unsigned int>& used = tool_ordering.all_extruders();

            // ToolOrdering::cal_non_support_filaments: the first filament of each extruder, the first
            // non-support filament (filament_is_support) overall and of each extruder
            const auto filament_is_support = print.config().get<std::vector<bool>>("filament_is_support");
            const bool has_non_support = std::any_of(used.begin(), used.end(), [&](unsigned int id) { return !filament_is_support.at(id); });
            std::vector<int> first_filaments(tool_count, -1);
            std::vector<int> first_non_support_filaments(tool_count, -1);
            int initial_non_support = -1;
            for (const LayerTools& layer_tools : tool_ordering.layer_tools())
                for (unsigned int id : layer_tools.extruders) {
                    const int tool = tool_of(id);
                    if (first_filaments[tool] < 0)
                        first_filaments[tool] = int(id);
                    if (has_non_support && !filament_is_support.at(id)) {
                        if (initial_non_support < 0)
                            initial_non_support = int(id);
                        if (first_non_support_filaments[tool] < 0)
                            first_non_support_filaments[tool] = int(id);
                    }
                }
            if (initial_non_support < 0)
                initial_non_support = int(initial_extruder_id);
            std::vector<int> first_non_support_hotends;
            for (int id : first_non_support_filaments)
                first_non_support_hotends.push_back(id < 0 ? -1 : tool_of(id));
            const int initial_tool = tool_of(initial_extruder_id);
            {
                // Orca filament_map: the (1-based) extruder of each filament
                std::vector<int> filament_map;
                for (size_t slot = 0; slot < hw.material_slot_count(); ++slot)
                    filament_map.push_back(tool_of(slot) + 1);
                this->placeholder_parser().set("filament_map", filament_map);
            }
            this->placeholder_parser().set("first_tools", first_filaments);
            this->placeholder_parser().set("first_filaments", first_filaments);
            this->placeholder_parser().set("first_non_support_tools", first_non_support_filaments);
            this->placeholder_parser().set("first_non_support_filaments", first_non_support_filaments);
            this->placeholder_parser().set("first_non_support_hotend", first_non_support_hotends);
            this->placeholder_parser().set("initial_no_support_tool", initial_non_support);
            this->placeholder_parser().set("initial_no_support_extruder", initial_non_support);
            this->placeholder_parser().set("initial_no_support_filament_id", initial_non_support);
            this->placeholder_parser().set("initial_no_support_hotend", tool_of(initial_non_support));
            this->placeholder_parser().set("initial_filament_id", int(initial_extruder_id));
            this->placeholder_parser().set("current_filament_id", int(initial_extruder_id));
            this->placeholder_parser().set("current_extruder_id", initial_tool);
            this->placeholder_parser().set("current_hotend", initial_tool);
            this->placeholder_parser().set("current_nozzle_id", initial_tool);
            this->placeholder_parser().set("initial_nozzle_id", initial_tool);
            {
                // the nozzle diameter of each tool (its first material slot)
                const auto nozzle_diameter = print.config().get<std::vector<double>>("nozzle_diameter");
                const std::vector<size_t> slot_counts = hw.tool_slot_counts();
                std::vector<double> diameters;
                size_t slot = 0;
                for (size_t count : slot_counts) {
                    diameters.push_back(nozzle_diameter.at(std::min(slot, nozzle_diameter.size() - 1)));
                    slot += count;
                }
                this->placeholder_parser().set("nozzle_diameter_at_nozzle_id", diameters);
            }

            // over the used filaments
            const auto additional_fan  = print.config().get<std::vector<int>>("additional_cooling_fan_speed");
            const auto chamber_temps   = print.config().get<std::vector<int>>("chamber_temperature");
            const auto vitrification   = print.config().get<std::vector<int>>("temperature_vitrification");
            const auto vendors         = print.config().get<std::vector<std::string>>("filament_vendor");
            int max_additional_fan = 0;
            int max_chamber_temp   = 0;
            int min_vitrification  = std::numeric_limits<int>::max();
            for (unsigned int id : used) {
                max_additional_fan = std::max(max_additional_fan, additional_fan.at(id));
                max_chamber_temp   = std::max(max_chamber_temp, chamber_temps.at(id));
                min_vitrification  = std::min(min_vitrification, vitrification.at(id));
            }
            this->placeholder_parser().set("max_additional_fan", max_additional_fan);
            this->placeholder_parser().set("overall_chamber_temperature", max_chamber_temp);
            this->placeholder_parser().set("min_vitrification_temperature", min_vitrification);
            this->placeholder_parser().set("is_all_bbl_filament",
                std::all_of(used.begin(), used.end(), [&](unsigned int id) { return vendors.at(id) == "Bambu Lab"; }));
            {
                std::vector<int> speeds;
                for (int speed : print.config().get<std::vector<int>>("during_print_exhaust_fan_speed"))
                    speeds.push_back(int(speed / 100.0 * 255));
                this->placeholder_parser().set("during_print_exhaust_fan_speed_num", speeds);
            }
            {
                const auto filament_type = print.config().get<std::vector<std::string>>("filament_type");
                bool has_tpu = false;
                if (!tool_ordering.layer_tools().empty())
                    for (unsigned int id : tool_ordering.layer_tools().front().extruders)
                        has_tpu |= filament_type.at(id) == "TPU";
                this->placeholder_parser().set("has_tpu_in_first_layer", has_tpu);
            }
            // flush temperature / volumetric speed (0: the top of the nozzle temperature range / the max
            // volumetric speed)
            {
                const auto flush_temp     = print.config().get<std::vector<int>>("filament_flush_temp");
                const auto temp_high      = print.config().get<std::vector<int>>("nozzle_temperature_range_high");
                const auto flush_speed    = print.config().get<std::vector<double>>("filament_flush_volumetric_speed");
                const auto max_volumetric = print.config().get<std::vector<double>>("filament_max_volumetric_speed");
                std::vector<int>    temps;
                std::vector<double> speeds;
                for (size_t id = 0; id < flush_temp.size(); ++id) {
                    temps.push_back(flush_temp[id] == 0 ? temp_high.at(id) : flush_temp[id]);
                    speeds.push_back(flush_speed.at(id) == 0. ? max_volumetric.at(id) : flush_speed.at(id));
                }
                this->placeholder_parser().set("flush_temperatures", temps);
                this->placeholder_parser().set("flush_volumetric_speeds", speeds);
            }
            // get_outer_wall_volumetric_speed: the outer wall's speed x flow, at most the filament's maximum
            this->placeholder_parser().set("outer_wall_volumetric_speed", this->outer_wall_volumetric_speed(print, unsigned(initial_non_support)));

            // the bed mesh over the first layer (+ margin), inside the printer's mesh area
            {
                const Vec2d  mesh_min_cfg = print.config().get<Vec2d>("bed_mesh_min");
                const Vec2d  mesh_max_cfg = print.config().get<Vec2d>("bed_mesh_max");
                const Vec2d  probe_dist   = print.config().get<Vec2d>("bed_mesh_probe_distance");
                const double margin       = print.config().get<double>("adaptive_bed_mesh_margin");
                const Vec2d  mesh_min{std::max(mesh_min_cfg.x(), bbox.min.x() - margin), std::max(mesh_min_cfg.y(), bbox.min.y() - margin)};
                const Vec2d  mesh_max{std::min(mesh_max_cfg.x(), bbox.max.x() + margin), std::min(mesh_max_cfg.y(), bbox.max.y() + margin)};
                this->placeholder_parser().set("adaptive_bed_mesh_min", std::vector<double>{mesh_min.x(), mesh_min.y()});
                this->placeholder_parser().set("adaptive_bed_mesh_max", std::vector<double>{mesh_max.x(), mesh_max.y()});
                int probe_count_x = std::max(3, int(std::ceil((mesh_max.x() - mesh_min.x()) / std::max(1., probe_dist.x()))) + 1);
                int probe_count_y = std::max(3, int(std::ceil((mesh_max.y() - mesh_min.y()) / std::max(1., probe_dist.y()))) + 1);
                std::string algo = "bicubic";
                if (probe_count_x * probe_count_y <= 6) { // lagrange needs up to a total of 6 mesh points
                    algo = "lagrange";
                } else if (print.config().get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfKlipper) {
                    // bicubic needs 4 probe points per axis
                    probe_count_x = std::max(probe_count_x, 4);
                    probe_count_y = std::max(probe_count_y, 4);
                }
                this->placeholder_parser().set("bed_mesh_probe_count", std::vector<int>{probe_count_x, probe_count_y});
                this->placeholder_parser().set("bed_mesh_algo", algo);
            }

            // the highest object, the first layer's centre without the wipe tower, the flat print flag
            {
                double max_print_z = 0.;
                double first_layer_area = 0.; // mm2: objects, supports, brim, wipe tower
                std::vector<Vec2d> first_layer_pts;
                const double area_scale = SCALING_FACTOR * SCALING_FACTOR;
                for (const PrintObject* object : print.objects()) {
                    if (!object->layers().empty())
                        max_print_z = std::max(max_print_z, object->layers().back()->print_z);
                    if (!object->layers().empty()
                        && object->layers().front()->print_z < print.config().get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(1.0) + EPSILON) {
                        for (const Domain::ExPolygon& expoly : object->layers().front()->lslices) {
                            first_layer_area += expoly.area() * area_scale;
                            for (const PrintInstance& instance : object->instances())
                                for (const Point& pt : expoly.contour.points)
                                    first_layer_pts.emplace_back(unscale(Point(pt + instance.shift())));
                        }
                    }
                    if (!object->support_layers().empty())
                        for (const Domain::ExPolygon& expoly : object->support_layers().front()->support_islands)
                            first_layer_area += expoly.area() * area_scale;
                }
                for (const Domain::Polygon& polygon : print.brim().polygons_covered_by_spacing())
                    first_layer_area += std::abs(polygon.area()) * area_scale;
                if (has_wipe_tower && print.wipe_tower_data())
                    first_layer_area += double(print.wipe_tower_data()->width) * double(print.wipe_tower_data()->depth);
                this->placeholder_parser().set("max_print_z", int(std::ceil(max_print_z)));
                const BoundingBoxf no_tower(BB::construct(first_layer_pts));
                const Vec2d center = first_layer_pts.empty() ? Vec2d::Zero() : Vec2d(0.5 * (no_tower.min + no_tower.max));
                this->placeholder_parser().set("first_layer_center_no_wipe_tower", std::vector<double>{center.x(), center.y()});
                // Orca: under 0.3 mm high with more than 40000 mm2 on the first layer
                this->placeholder_parser().set("hold_chamber_temp_for_flat_print", max_print_z > 0. && max_print_z < 0.3 && first_layer_area > 40000.);
            }

            // the point next to the wipe tower to heat the nozzle at (its side towards the bed centre)
            {
                Vec2d center = Vec2d::Zero();
                bool  valid  = false;
                if (has_wipe_tower && print.wipe_tower_data()) {
                    const WipeTowerData& data = *print.wipe_tower_data();
                    const double         brim = data.brim_width;
                    const BoundingBoxf   tower(BB::construct(std::vector<Vec2d>{
                        Vec2d(data.position.x() - brim, data.position.y() - brim),
                        Vec2d(data.position.x() + data.width + brim, data.position.y() + data.depth + brim)}));
                    const BoundingBoxf   bed(BB::construct(print.config().get<std::vector<Vec2d>>("bed_shape")));
                    const Vec2d tower_center = 0.5 * (tower.min + tower.max);
                    center = tower_center.x() < 0.5 * (bed.min.x() + bed.max.x()) ? Vec2d(tower.max.x() + 2., tower_center.y())
                                                                                 : Vec2d(tower.min.x() - 2., tower_center.y());
                    center.x() = std::clamp(center.x(), bed.min.x(), bed.max.x());
                    valid      = true;
                }
                this->placeholder_parser().set("wipe_tower_center_pos_x", center.x());
                this->placeholder_parser().set("wipe_tower_center_pos_y", center.y());
                this->placeholder_parser().set("wipe_tower_center_pos_valid", valid);
            }
        }

        // Orca: print time and used filament, replaced in the finished G-code (PostProcessor)
        this->placeholder_parser().set("print_time_total_sec", std::string(reserved_tag(Tags::Print_Time_Total_Sec_Placeholder)));
        this->placeholder_parser().set("print_time_day", std::string(reserved_tag(Tags::Print_Time_Day_Placeholder)));
        this->placeholder_parser().set("print_time_hour", std::string(reserved_tag(Tags::Print_Time_Hour_Placeholder)));
        this->placeholder_parser().set("print_time_minute", std::string(reserved_tag(Tags::Print_Time_Minute_Placeholder)));
        this->placeholder_parser().set("print_time_sec", std::string(reserved_tag(Tags::Print_Time_Sec_Placeholder)));
        this->placeholder_parser().set("used_filament_length", std::string(reserved_tag(Tags::Used_Filament_Length_Placeholder)));

        // Now the custom parameters:
        add_custom_parameters_into_placeholder_parser(
            print.config().get<std::string>("custom_parameters_print"),
            print.config().get<std::string>("custom_parameters_printer"),
            print.config().get<std::vector<std::string>>("custom_parameters_filament"),
            this->placeholder_parser()
        );
    }

    // Orca: the file header G-code comes first, processed with the start-up placeholders set above
    if (const std::string& file_start_gcode = print.config().get<std::string>("file_start_gcode"); !file_start_gcode.empty())
        file.writeln(this->placeholder_parser_process("file_start_gcode", file_start_gcode, initial_extruder_id));

    // Write information on the generator, in Anycubic Slicer Next's header block: the Kobra firmware maps
    // the used filaments to its ACE slots from paint_info.
    file.write_format("; HEADER_BLOCK_START\n");
    file.write_format("; %s\n", Slic3r::header_slic3r_generated().c_str());
    if (! prepared_by_info.empty())
        file.write_format("; prepared by %s\n", prepared_by_info.c_str());
    {
        std::vector<std::string>       colours          = print.config().get<std::vector<std::string>>("extruder_colour");
        const std::vector<std::string> filament_colours = print.config().get<std::vector<std::string>>("filament_colour");
        const std::vector<std::string> filament_types   = print.config().get<std::vector<std::string>>("filament_type");
        std::string paint_info;
        for (unsigned int id : tool_ordering.all_extruders()) {
            std::string colour = id < colours.size() ? colours[id] : std::string{};
            if (colour.empty() && id < filament_colours.size())
                colour = filament_colours[id];
            Domain::ColorRGB rgb;
            if (!Biz::Algorithms::Color::decode_color(colour, rgb))
                rgb = Domain::ColorRGB{1.f, 1.f, 1.f};
            const auto byte = [](float c) { return int(std::lround(c * 255.f)); };
            paint_info += (paint_info.empty() ? "" : ",")
                + fmt::format(R"({{"material_type":"{}","paint_color":[{},{},{}],"paint_index":{}}})",
                         id < filament_types.size() ? filament_types[id] : std::string("PLA"),
                         byte(rgb.r()), byte(rgb.g()), byte(rgb.b()), id);
        }
        file.write_format("; paint_info = [%s]\n", paint_info.c_str());
    }
    file.write_format("; HEADER_BLOCK_END\n\n");

    if (print.thumbnails.raw_data.valid()) {
        Biz::Slicing::ThumbnailImageResults thumbnails{print.thumbnails.raw_data.get()};

        if (!thumbnails.empty() && !thumbnails.front().images.empty()) {
            ASSERT(thumbnails.size() == 1);
            GCodeThumbnails::export_thumbnails_to_file(
                std::move(thumbnails.front()),
                print.thumbnails.formats,
                [&file](const char* sz) { file.write(sz); },
                [&print]() { print.throw_if_canceled(); }
            );
        }
    }

    // Write notes (content of the Print Settings tab -> Notes)
    {
        std::list<std::string> lines;
        boost::split(lines, print.config().get<std::string>("notes"), boost::is_any_of("\n"), boost::token_compress_off);
        for (auto line : lines) {
            // Remove the trailing '\r' from the '\r\n' sequence.
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            file.write_format("; %s\n", line.c_str());
        }
        if (! lines.empty())
            file.write("\n");
    }
    print.throw_if_canceled();

    // Write some terse information on the slicing parameters.
    const PrintObject *first_object         = print.objects().front();
    const double       layer_height         = first_object->config().get<double>("layer_height");
    assert(! print.config().get<Domain::FloatOrPercentage>("first_layer_height").is_percentage());
    const double       first_layer_height   = print.config().get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(1.0); // The 1.0 is to keep the legacy behavior.
    for (size_t region_id = 0; region_id < print.num_print_regions(); ++ region_id) {
        const PrintRegion &region = print.get_print_region(region_id);
        file.write_format("; external perimeters extrusion width = %.2fmm\n", region.flow(*first_object, frExternalPerimeter, layer_height).width());
        file.write_format("; perimeters extrusion width = %.2fmm\n",          region.flow(*first_object, frPerimeter,         layer_height).width());
        file.write_format("; infill extrusion width = %.2fmm\n",              region.flow(*first_object, frInfill,            layer_height).width());
        file.write_format("; solid infill extrusion width = %.2fmm\n",        region.flow(*first_object, frSolidInfill,       layer_height).width());
        file.write_format("; top infill extrusion width = %.2fmm\n",          region.flow(*first_object, frTopSolidInfill,    layer_height).width());
        if (print.has_support_material())
            file.write_format("; support material extrusion width = %.2fmm\n", support_material_flow(first_object).width());
        if (!region.extruder_config_value<Domain::FloatOrPercentage>("first_layer_extrusion_width", frPerimeter).is_zero())
            file.write_format("; first layer extrusion width = %.2fmm\n",   region.flow(*first_object, frPerimeter, first_layer_height, true).width());
        file.write_format("\n");
    }
    print.throw_if_canceled();

    // adds tags for time estimators
    if (print.config().get<bool>("remaining_times"))
        file.write_format(";%s\n", reserved_tag(Tags::First_Line_M73_Placeholder).data());

    // Starting now, the G-code find / replace post-processor will be enabled.
    file.find_replace_enable();

    m_cooling_buffer =
        std::make_unique<CoolingBuffer>(*this, Biz::Slicing::CoolingBufferConfig{print.config()});
    m_cooling_buffer->set_current_extruder(initial_extruder_id);

    // Emit machine envelope limits for the Marlin firmware.
    this->print_machine_envelope(file, print);

    // Orca: the auxiliary fan is off while the fan is off for the first layers
    if (print.config().get<bool>("auxiliary_fan")
        && print.config().get<std::vector<int>>("disable_fan_first_layers").at(initial_extruder_id) > 0)
        file.write(GCodeWriter::set_additional_fan(print.config().get<bool>("gcode_comments"), 0));

    // Label all objects so printer knows about them since the start.
    m_label_objects.init(print.objects(), print.config().get<Domain::LabelObjectsStyle>("gcode_label_objects"), print.config().get<GCodeFlavor>("gcode_flavor"));
    // Orca: the skirt / brim groups
    m_skirt_group_done.assign(print.skirt_brim_groups().size(), {});
    m_pending_brims.clear();
    for (const Print::SkirtBrimGroup &group : print.skirt_brim_groups())
        for (const Print::SkirtBrimGroup::Brim &brim : group.brims)
            if (! brim.brim.empty())
                m_pending_brims.insert(brim.instances.begin(), brim.instances.end());
    file.write(m_label_objects.all_objects_header());

    // Enable ooze prevention if configured so.
    DoExport::init_ooze_prevention(print, m_ooze_prevention);

    const std::string start_gcode = this->_process_start_gcode(print, initial_extruder_id);

    const auto chamber_control = print.config().get<std::vector<Domain::ChamberTemperatureControl>>("activate_chamber_temp_control");
    const bool chamber_auto    = chamber_control.at(initial_extruder_id) == Domain::ChamberTemperatureControl::Auto;
    if (chamber_auto)
        this->_print_first_layer_chamber_temperature(file, print, start_gcode, print.config().get<std::vector<int>>("chamber_temperature").at(initial_extruder_id), false, false);
    // Orca: a used filament with chamber temperature control on heats the chamber to the highest
    // chamber temperature of the used filaments before the start G-code, waiting for it
    if (std::any_of(tool_ordering.all_extruders().begin(), tool_ordering.all_extruders().end(),
            [&](unsigned int id) { return chamber_control.at(id) == Domain::ChamberTemperatureControl::On; })) {
        int max_chamber_temp = 0;
        for (unsigned int id : tool_ordering.all_extruders())
            max_chamber_temp = std::max(max_chamber_temp, print.config().get<std::vector<int>>("chamber_temperature").at(id));
        int temp_by_gcode = -1;
        if (max_chamber_temp > 0 && !custom_gcode_sets_temperature(start_gcode, 141, 191, false, temp_by_gcode))
            file.write(m_writer.set_chamber_temperature(max_chamber_temp, true, false));
    }
    this->_print_first_layer_bed_temperature(file, print, start_gcode, initial_extruder_id, true);
    // Orca: with air filtration, the highest during-print exhaust speed of the filaments that use it
    if (print.config().get<bool>("support_air_filtration")) {
        if (const std::optional<int> speed = exhaust_fan_speed(print, m_writer, true))
            file.write(m_writer.set_exhaust_fan(*speed));
    }
    this->_print_first_layer_extruder_temperatures(file, print, start_gcode, initial_extruder_id, false);

    // adds tag for processor
    file.write_format(";%s%s\n", reserved_tag(Tags::Role).data(), gcode_extrusion_role_to_string(GCodeExtrusionRole::Custom).c_str());

    // Write the custom start G-code
    file.writeln(start_gcode);

    this->_print_first_layer_extruder_temperatures(file, print, start_gcode, initial_extruder_id, true);
    if (chamber_auto) {
        this->_print_first_layer_chamber_temperature(file, print, start_gcode, print.config().get<std::vector<int>>("chamber_minimal_temperature").at(initial_extruder_id), true, false);
        this->_print_first_layer_chamber_temperature(file, print, start_gcode, print.config().get<std::vector<int>>("chamber_temperature").at(initial_extruder_id), false, false);
    }
    print.throw_if_canceled();

    // Set other general things.
    file.write(this->preamble(print.config().get<double>("z_offset")));

    print.throw_if_canceled();

    // Collect custom seam data from all objects.
    std::function<void(void)> throw_if_canceled_func = [&print]() { print.throw_if_canceled();};

    const Seams::Params params{Seams::Placer::get_params(print.config())};
    m_seam_placer.init(print.objects(), params, throw_if_canceled_func);

    if (! (has_wipe_tower && print.config().get<bool>("single_extruder_multi_material_priming"))) {
        // Set initial extruder only after custom start G-code.
        // Ugly hack: Do not set the initial extruder if the extruder is primed using the MMU priming towers at the edge of the print bed.
        file.write(this->set_extruder(initial_extruder_id, 0., print.config()));
    }

    GCode::SmoothPathCache smooth_path_cache_global = smooth_path_interpolate_global(print);

    const std::vector<double> retract_speed{print.config().get<std::vector<double>>("retract_speed")};
    const double travel_speed{print.config().get<double>("travel_speed")};

    // Do all objects for each layer.
    if (print.config().get<bool>("complete_objects")) {
        size_t finished_objects = 0;
        const PrintObject *prev_object = (*print_object_instance_sequential_active)->print_object;
        for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++ print_object_instance_sequential_active) {
            const PrintObject &object = *(*print_object_instance_sequential_active)->print_object;
            if (&object != prev_object || tool_ordering.first_extruder() != final_extruder_id) {
                tool_ordering = ToolOrdering(object, final_extruder_id);
                unsigned int new_extruder_id = tool_ordering.first_extruder();
                if (new_extruder_id == (unsigned int)-1)
                    // Skip this object.
                    continue;
                initial_extruder_id = new_extruder_id;
                final_extruder_id   = tool_ordering.last_extruder();
                assert(final_extruder_id != (unsigned int)-1);
            }
            print.throw_if_canceled();
            this->set_origin(unscale((*print_object_instance_sequential_active)->shift()));

            if (finished_objects > 0) {
                // Move to the origin position for the copy we're going to print.
                // This happens before Z goes down to layer 0 again, so that no collision happens hopefully.
                m_enable_cooling_markers = false; // we're not filtering these moves through CoolingBuffer
                m_avoid_crossing_perimeters.use_external_mp_once = true;
                file.write(this->retract_and_wipe(retract_speed, travel_speed));
                file.write(m_label_objects.maybe_stop_instance());
                const double last_z{this->writer().get_position().z()};
                file.write(this->writer().travel_to_z_force(last_z, "ensure z position"));
                const double travel_z = std::max(last_z, double(m_max_layer_z));
                file.write(this->writer().travel_to_z_force(travel_z, "ensure z position to clear all already printed objects"));
                const Vec3crd from{to_3d(*this->last_position, scaled(travel_z))};
                const Vec3crd to{0, 0, scaled(travel_z)};
                const Slicing::ExtrudeConfig extrude_config{object.config()};
                file.write(this->travel_to(from, to, ExtrusionRole::None, "move to origin position for next object", [](){return "";}, extrude_config));
                m_enable_cooling_markers = true;
                // Disable motion planner when traveling to first object point.
                m_avoid_crossing_perimeters.disable_once();
                // Ff we are printing the bottom layer of an object, and we have already finished
                // another one, set first layer temperatures. This happens before the Z move
                // is triggered, so machine has more time to reach such temperatures.
                this->placeholder_parser().set("current_object_idx", int(finished_objects));
                std::string between_objects_gcode = this->placeholder_parser_process("between_objects_gcode", print.config().get<std::string>("between_objects_gcode"), initial_extruder_id);
                // Set first layer bed and extruder temperatures, don't wait for it to reach the temperature.
                this->_print_first_layer_bed_temperature(file, print, between_objects_gcode, initial_extruder_id, false);
                this->_print_first_layer_extruder_temperatures(file, print, between_objects_gcode, initial_extruder_id, false);
                file.writeln(between_objects_gcode);
            }
            // Reset the cooling buffer internal state (the current position, feed rate, accelerations).
            m_cooling_buffer->reset(this->writer().get_position());
            m_cooling_buffer->set_current_extruder(initial_extruder_id);
            // Process all layers of a single object instance (sequential mode) with a parallel pipeline:
            // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
            // and export G-code into file.
            this->process_layers(print, tool_ordering, collect_layers_to_print(object),
                *print_object_instance_sequential_active - object.instances().data(), 
                smooth_path_cache_global, file);
            // Orca: power loss recovery off after each object
            if (m_second_layer_things_done && print.config().get<Domain::PowerLossRecoveryMode>("enable_power_loss_recovery") == Domain::PowerLossRecoveryMode::Enable)
                file.write(m_writer.set_power_loss_recovery(Domain::PowerLossRecoveryMode::Disable));
            ++ finished_objects;
            // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
            // Reset it when starting another object from 1st layer.
            m_second_layer_things_done = false;
            prev_object = &object;
        }

        file.write(m_label_objects.maybe_stop_instance());
    } else {
        // Sort layers by Z.
        // All extrusion moves with the same top layer height are extruded uninterrupted.
        std::vector<std::pair<double, ObjectsLayerToPrint>> layers_to_print = collect_layers_to_print(print);
        // Prusa Multi-Material wipe tower.
        if (has_wipe_tower && ! layers_to_print.empty()) {
            m_wipe_tower = std::make_unique<GCode::WipeTowerIntegration>(
                print.wipe_tower()->position.cast<float>(),
                print.wipe_tower()->rotation,
                print.config(),
                *print.wipe_tower_data()->priming.get(),
                print.wipe_tower_data()->tool_changes,
                *print.wipe_tower_data()->final_purge.get()
            );

            // Set position for wipe tower generation.
            Vec3d new_position = this->writer().get_position();
            new_position.z() = first_layer_height;
            this->writer().update_position(new_position);

            if (print.config().get<bool>("single_extruder_multi_material_priming")) {
                file.write(m_wipe_tower->prime(*this, print.config()));
                // Verify, whether the print overaps the priming extrusions.
                BoundingBoxf bbox_print(get_print_extrusions_extents(print));
                double twolayers_printz = ((layers_to_print.size() == 1) ? layers_to_print.front() : layers_to_print[1]).first + EPSILON;
                for (const PrintObject *print_object : print.objects())
                    bbox_print = BB::merge(bbox_print, get_print_object_extrusions_extents(*print_object, twolayers_printz));
                bbox_print = BB::merge(bbox_print, get_wipe_tower_extrusions_extents(print, twolayers_printz));
                BoundingBoxf bbox_prime(get_wipe_tower_priming_extrusions_extents(print));
                bbox_prime = BB::inflated(bbox_prime, 0.5f);
                bool overlap = bbox_prime.overlap(bbox_print);

                const GCodeFlavor flavor = print.config().get<GCodeFlavor>("gcode_flavor");
                if (flavor == GCodeFlavor::gcfMarlinLegacy
                    || flavor == GCodeFlavor::gcfMarlinFirmware
                    || flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
                {
                    file.write(this->retract_and_wipe(retract_speed, travel_speed));
                    file.write("M300 S800 P500\n"); // Beep for 500ms, tone 800Hz.
                    if (overlap) {
                        // Wait for the user to remove the priming extrusions.
                        file.write("M1 Remove priming towers and click button.\n");
                    } else {
                        // Just wait for a bit to let the user check, that the priming succeeded.
                        //TODO Add a message explaining what the printer is waiting for. This needs a firmware fix.
                        file.write("M1 S10\n");
                    }
                } else {
                    // This is not Marlin, M1 command is probably not supported.
                    // (See https://github.com/prusa3d/PrusaSlicer/issues/5441.)
                    if (overlap) {
                        print.append_warning_callback(
                            Biz::Slicing::Warning{
                                Biz::Slicing::WarningCode::CloseToPrimingRegions
                            }
                        );
                    } else {
                        // Just continue printing, no action necessary.
                    }
                }

                // When priming is enabled, extruders are ordered (inside ToolOrdering::collect_extruder_statistics())
                // in such a way that the last one is the first printing extruder (actually printing, not just priming).
                const unsigned int first_printing_extruder_after_priming = tool_ordering.all_extruders().back();

                // Because CoolingBuffer doesn't process the priming of extruders, set the current extruder
                // to the actual first printing extruder (that is also the last primed extruder).
                m_cooling_buffer->set_current_extruder(first_printing_extruder_after_priming);
            }
            print.throw_if_canceled();
        }
        // Process all layers of all objects (non-sequential mode) with a parallel pipeline:
        // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
        // and export G-code into file.
        this->process_layers(print, tool_ordering, print_object_instances_ordering, layers_to_print, 
            smooth_path_cache_global, file);
        file.write(m_label_objects.maybe_stop_instance());
        // Orca: power loss recovery off at the end
        if (m_second_layer_things_done && print.config().get<Domain::PowerLossRecoveryMode>("enable_power_loss_recovery") == Domain::PowerLossRecoveryMode::Enable)
            file.write(m_writer.set_power_loss_recovery(Domain::PowerLossRecoveryMode::Disable));
        if (m_wipe_tower)
            // Purge the extruder, pull out the active filament.
            file.write(m_wipe_tower->finalize(*this, print.config()));
    }

    // Write end commands to file.
    file.write(this->retract_and_wipe(retract_speed, travel_speed));
    file.write(m_writer.set_fan(0));
    // Orca: the auxiliary fan is off at the end
    if (print.config().get<bool>("auxiliary_fan"))
        file.write(GCodeWriter::set_additional_fan(print.config().get<bool>("gcode_comments"), 0));

    // adds tag for processor
    file.write_format(";%s%s\n", reserved_tag(Tags::Role).data(), gcode_extrusion_role_to_string(GCodeExtrusionRole::Custom).c_str());

    // Process filament-specific gcode in extruder order.
    {
        ParserConfig config;
        config.set("layer_num", m_layer_index);
        config.set("layer_z", m_writer.get_position().z() - print.config().get<double>("z_offset"));
        config.set("max_layer_z", double{m_max_layer_z});
        if (print.config().get<bool>("single_extruder_multi_material")) {
            // Process the end_filament_gcode for the active filament only.
            int extruder_id = m_writer.extruder()->id();
            config.set("filament_extruder_id", extruder_id);
            file.writeln(this->placeholder_parser_process("end_filament_gcode", print.config().get<std::vector<std::string>>("end_filament_gcode").at(extruder_id), extruder_id, &config));
        } else {
            const std::vector<std::string>& end_filament_gcode{
                print.config().get<std::vector<std::string>>("end_filament_gcode")};
            for (int extruder_id{}; extruder_id < end_filament_gcode.size(); ++extruder_id) {
                config.set("filament_extruder_id", extruder_id);
                file.writeln(this->placeholder_parser_process(
                    "end_filament_gcode",
                    end_filament_gcode.at(extruder_id),
                    extruder_id,
                    &config));
            }
        }
        file.writeln(this->placeholder_parser_process("end_gcode", print.config().get<std::string>("end_gcode"), m_writer.extruder()->id(), &config));
    }
    // Orca: with air filtration, the highest on-completion exhaust speed of the filaments that use it
    if (print.config().get<bool>("support_air_filtration")) {
        if (const std::optional<int> speed = exhaust_fan_speed(print, m_writer, false))
            file.write(m_writer.set_exhaust_fan(*speed));
    }
    file.write(m_writer.update_progress(m_layer_count, m_layer_count, true)); // 100%
    file.write(m_writer.postamble());

    // From now to the end of G-code, the G-code find / replace post-processor will be disabled.
    // Thus the PrusaSlicer generated config will NOT be processed by the G-code post-processor, see GH issue #7952.
    file.find_replace_supress();

    // adds tags for time estimators
    if (print.config().get<bool>("remaining_times"))
        file.write_format(";%s\n", reserved_tag(Tags::Last_Line_M73_Placeholder).data());

    print.throw_if_canceled();

    const Domain::ExtraPrintStatistics extra_print_statistics{DoExport::get_extra_print_statistics(
        print.wipe_tower_data(),
        print.config(),
        m_writer.extruders(),
        initial_extruder_id,
        tool_ordering.toolchanges_count()
    )};

    // if exporting gcode in ascii format, config export in new format is done here
    // Append full config, delimited by two 'phony' configuration keys prusaslicer_config = begin and prusaslicer_config = end.
    // The delimiters are structured as configuration key / value pairs to be parsable by older versions of PrusaSlicer G-code viewer.
    // Orca: gcode_skip_config_block leaves the config blocks out
    const bool write_config_block = !print.config().get<bool>("gcode_skip_config_block");
    if (write_config_block) {
        file.write("; prusaslicer_json_config = begin\n");
        file.write(std::string("; ") + boost::replace_all_copy(serialized_config.json, "\n", "\n; ") + "\n");
        file.write("; prusaslicer_json_config = end\n");
    }

    file.write_format("; objects_info = %s\n", m_label_objects.all_objects_header_singleline_json().c_str());
    file.write_format(";%s\n", reserved_tag(Tags::Print_Statistics_Placeholder).data());

    // if exporting gcode in ascii format, config export is done here
    // Append full config, delimited by two 'phony' configuration keys prusaslicer_config = begin and prusaslicer_config = end.
    // The delimiters are structured as configuration key / value pairs to be parsable by older versions of PrusaSlicer G-code viewer.
    if (write_config_block) {
        file.write("\n; prusaslicer_config = begin\n");
        file.write(std::string("; ") + boost::replace_all_copy(serialized_config.ini, "\n", "\n; ") + "\n");
        file.write("; prusaslicer_config = end\n\n");
    }

    if (std::optional<std::string> line_M84 = find_M84(print.config().get<std::string>("end_gcode"));
        is_mk2_or_mk3(print.config().get<std::string>("printer_model")) && line_M84.has_value()) {
        file.writeln(*line_M84);
    }

    print.throw_if_canceled();
    return extra_print_statistics;
}

// Fill in cache of smooth paths for perimeters, fills and supports of the given object layers.
// Based on params, the paths are either decimated to sparser polylines, or interpolated with circular arches.
void GCodeGenerator::smooth_path_interpolate(
    const ObjectLayerToPrint                                &object_layer_to_print, 
    const GCode::SmoothPathCache::InterpolationParameters   &params, 
    GCode::SmoothPathCache                                  &out)
{
    if (const Layer *layer = object_layer_to_print.object_layer; layer) {
        for (const LayerRegion *layerm : layer->regions()) {
            out.interpolate_add(layerm->perimeters(), params);
            out.interpolate_add(layerm->fills(), params);
        }
    }
    if (const SupportLayer *layer = object_layer_to_print.support_layer; layer)
        out.interpolate_add(layer->support_fills, params);
}

// Process all layers of all objects (non-sequential mode) with a parallel pipeline:
// Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
// and export G-code into file.
void GCodeGenerator::process_layers(
    const Print                                                         &print,
    const ToolOrdering                                                  &tool_ordering,
    const std::vector<const PrintInstance*>                             &print_object_instances_ordering,
    const std::vector<std::pair<double, ObjectsLayerToPrint>>         &layers_to_print,
    const GCode::SmoothPathCache                                        &smooth_path_cache_global,
    GCodeOutputStream                                                   &output_stream)
{
    size_t layer_to_print_idx = 0;
    const GCode::SmoothPathCache::InterpolationParameters interpolation_params = interpolation_parameters(print.config());
    std::vector<GCode::SmoothPathCache> smooth_path_cache_per_layer{layers_to_print.size()};
    const auto smooth_path_interpolator = tbb::make_filter<void, size_t>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &layers_to_print, &layer_to_print_idx, &interpolation_params, &smooth_path_cache_per_layer](tbb::flow_control &fc) -> size_t {
            if (layer_to_print_idx >= layers_to_print.size()) {
                if (layer_to_print_idx == layers_to_print.size() + (m_pressure_equalizer ? 1 : 0)) {
                    fc.stop();
                    return {};
                } else {
                    // Pressure equalizer need insert empty input. Because it returns one layer back.
                    // Insert NOP (no operation) layer;
                    return layer_to_print_idx++;
                }
            } else {
                print.throw_if_canceled();
                const size_t idx = layer_to_print_idx++;
                GCode::SmoothPathCache &smooth_path_cache = smooth_path_cache_per_layer[idx];
                for (const ObjectLayerToPrint &l : layers_to_print[idx].second) {
                    GCodeGenerator::smooth_path_interpolate(l, interpolation_params, smooth_path_cache);
                }

                return idx;
            }
        });
    const auto generator = tbb::make_filter<size_t, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &tool_ordering, &print_object_instances_ordering, &layers_to_print, &smooth_path_cache_global, &smooth_path_cache_per_layer](const size_t layer_to_print_idx) -> LayerResult {
            if (layer_to_print_idx == layers_to_print.size()) {
                // Pressure equalizer need insert empty input. Because it returns one layer back.
                // Insert NOP (no operation) layer;
                return LayerResult::make_nop_layer_result();
            } else {
                const std::pair<double, ObjectsLayerToPrint> &layer = layers_to_print[layer_to_print_idx];
                const LayerTools& layer_tools = tool_ordering.tools_for_layer(layer.first);
                if (m_wipe_tower && layer_tools.has_wipe_tower)
                    m_wipe_tower->next_layer();
                print.throw_if_canceled();

                const GCode::SmoothPathCache &smooth_path_cache = smooth_path_cache_per_layer[layer_to_print_idx];
                LayerResult layer_result = this->process_layer(print, layer.second, layer_tools,
                    GCode::SmoothPathCaches{ smooth_path_cache_global, smooth_path_cache },
                    &layer == &layers_to_print.back(), &print_object_instances_ordering, size_t(-1));

                // Free the SmoothPathCache for this layer.
                smooth_path_cache_per_layer[layer_to_print_idx] = GCode::SmoothPathCache{};
                return layer_result;
            }
        });
    // The pipeline is variable: The vase mode filter is optional.
    const auto spiral_vase = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [spiral_vase = this->m_spiral_vase.get(), &layers_to_print](LayerResult in) -> LayerResult {
            if (in.nop_layer_result)
                return in;
            spiral_vase->enable(in.spiral_vase_enable);
            bool last_layer = in.layer_id == layers_to_print.size() - 1;
            return { spiral_vase->process_layer(std::move(in.gcode), last_layer), in.layer_id, in.spiral_vase_enable, in.cooling_buffer_flush};
        });
    const auto pressure_equalizer = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [pressure_equalizer = this->m_pressure_equalizer.get()](LayerResult in) -> LayerResult {
            return pressure_equalizer->process_layer(std::move(in));
        });
    const auto cooling = tbb::make_filter<LayerResult, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [cooling_buffer = this->m_cooling_buffer.get()](LayerResult in) -> std::string {
             if (in.nop_layer_result)
                return in.gcode;

             return cooling_buffer->process_layer(std::move(in.gcode), in.layer_id, in.cooling_buffer_flush);
        });
    const auto find_replace = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [find_replace = this->m_find_replace.get()](std::string s) -> std::string {
            return find_replace->process_layer(std::move(s));
        });
    const auto output = tbb::make_filter<std::string, void>(slic3r_tbb_filtermode::serial_in_order,
        [&output_stream](std::string s) { output_stream.write(s); }
    );

    tbb::filter<void, LayerResult> pipeline_to_layerresult = smooth_path_interpolator & generator;
    if (m_spiral_vase)
        pipeline_to_layerresult = pipeline_to_layerresult & spiral_vase;
    if (m_pressure_equalizer)
        pipeline_to_layerresult = pipeline_to_layerresult & pressure_equalizer;

    tbb::filter<LayerResult, std::string> pipeline_to_string = cooling;
    // Orca fan mover, once the cooling buffer has set the fan speeds (a whole layer: flushed)
    const auto fan_mover = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [fan_mover = this->m_fan_mover.get()](std::string s) -> std::string {
            return fan_mover->process_gcode(s, true);
        });
    if (m_fan_mover)
        pipeline_to_string = pipeline_to_string & fan_mover;
    // Orca adaptive pressure advance, once the cooling buffer has set the final speeds
    const auto pa_processor = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [pa_processor = this->m_pa_processor.get()](std::string s) -> std::string {
            return pa_processor->process_layer(std::move(s));
        });
    if (m_pa_processor)
        pipeline_to_string = pipeline_to_string & pa_processor;
    if (m_find_replace)
        pipeline_to_string = pipeline_to_string & find_replace;

    // It registers a handler that sets locales to "C" before any TBB thread starts participating in tbb::parallel_pipeline.
    // Handler is unregistered when the destructor is called.
    TBBLocalesSetter locales_setter;
    // The pipeline elements are joined using const references, thus no copying is performed.
    output_stream.find_replace_supress();
    tbb::parallel_pipeline(12, pipeline_to_layerresult & pipeline_to_string & output);
    output_stream.find_replace_enable();
}

// Process all layers of a single object instance (sequential mode) with a parallel pipeline:
// Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
// and export G-code into file.
void GCodeGenerator::process_layers(
    const Print                             &print,
    const ToolOrdering                      &tool_ordering,
    ObjectsLayerToPrint                      layers_to_print,
    const size_t                             single_object_idx,
    const GCode::SmoothPathCache            &smooth_path_cache_global,
    GCodeOutputStream                       &output_stream)
{
    size_t layer_to_print_idx = 0;
    const GCode::SmoothPathCache::InterpolationParameters interpolation_params = interpolation_parameters(print.config());
    std::vector<GCode::SmoothPathCache> smooth_path_cache_per_layer{layers_to_print.size()};
    const auto smooth_path_interpolator = tbb::make_filter<void, size_t> (slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &layers_to_print, &layer_to_print_idx, interpolation_params, &smooth_path_cache_per_layer](tbb::flow_control &fc) -> size_t {
            if (layer_to_print_idx >= layers_to_print.size()) {
                if (layer_to_print_idx == layers_to_print.size() + (m_pressure_equalizer ? 1 : 0)) {
                    fc.stop();
                    return {};
                } else {
                    // Pressure equalizer need insert empty input. Because it returns one layer back.
                    // Insert NOP (no operation) layer;
                    return layer_to_print_idx++;
                }
            } else {
                print.throw_if_canceled();
                const size_t idx = layer_to_print_idx ++;
                GCode::SmoothPathCache &smooth_path_cache = smooth_path_cache_per_layer[idx];
                GCodeGenerator::smooth_path_interpolate(layers_to_print[idx], interpolation_params, smooth_path_cache);
                return idx;
            }
        });
    const auto generator = tbb::make_filter<size_t, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &tool_ordering, &layers_to_print, &smooth_path_cache_global, single_object_idx, &smooth_path_cache_per_layer](const size_t layer_to_print_idx) -> LayerResult {
            if (layer_to_print_idx == layers_to_print.size()) {
                // Pressure equalizer need insert empty input. Because it returns one layer back.
                // Insert NOP (no operation) layer;
                return LayerResult::make_nop_layer_result();
            } else {
                ObjectLayerToPrint &layer = layers_to_print[layer_to_print_idx];
                print.throw_if_canceled();

                const GCode::SmoothPathCache &smooth_path_cache = smooth_path_cache_per_layer[layer_to_print_idx];

                LayerResult layer_result = this->process_layer(print, { std::move(layer) }, tool_ordering.tools_for_layer(layer.print_z()),
                    GCode::SmoothPathCaches{ smooth_path_cache_global, smooth_path_cache },
                    &layer == &layers_to_print.back(), nullptr, single_object_idx);

                // Free the SmoothPathCache for this layer.
                smooth_path_cache_per_layer[layer_to_print_idx] = GCode::SmoothPathCache{};
                return layer_result;
            }
        });
    // The pipeline is variable: The vase mode filter is optional.
    const auto spiral_vase = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [spiral_vase = this->m_spiral_vase.get(), &layers_to_print](LayerResult in)->LayerResult {
            if (in.nop_layer_result)
                return in;
            spiral_vase->enable(in.spiral_vase_enable);
            bool last_layer = in.layer_id == layers_to_print.size() - 1;
            return { spiral_vase->process_layer(std::move(in.gcode), last_layer), in.layer_id, in.spiral_vase_enable, in.cooling_buffer_flush };
        });
    const auto pressure_equalizer = tbb::make_filter<LayerResult, LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [pressure_equalizer = this->m_pressure_equalizer.get()](LayerResult in) -> LayerResult {
             return pressure_equalizer->process_layer(std::move(in));
        });
    const auto cooling = tbb::make_filter<LayerResult, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [cooling_buffer = this->m_cooling_buffer.get()](LayerResult in)->std::string {
            if (in.nop_layer_result)
                return in.gcode;
            return cooling_buffer->process_layer(std::move(in.gcode), in.layer_id, in.cooling_buffer_flush);
        });
    const auto find_replace = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [find_replace = this->m_find_replace.get()](std::string s) -> std::string {
            return find_replace->process_layer(std::move(s));
        });
    const auto output = tbb::make_filter<std::string, void>(slic3r_tbb_filtermode::serial_in_order,
        [&output_stream](std::string s) { output_stream.write(s); }
    );

    tbb::filter<void, LayerResult> pipeline_to_layerresult = smooth_path_interpolator & generator;
    if (m_spiral_vase)
        pipeline_to_layerresult = pipeline_to_layerresult & spiral_vase;
    if (m_pressure_equalizer)
        pipeline_to_layerresult = pipeline_to_layerresult & pressure_equalizer;

    tbb::filter<LayerResult, std::string> pipeline_to_string = cooling;
    // Orca fan mover, once the cooling buffer has set the fan speeds (a whole layer: flushed)
    const auto fan_mover = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [fan_mover = this->m_fan_mover.get()](std::string s) -> std::string {
            return fan_mover->process_gcode(s, true);
        });
    if (m_fan_mover)
        pipeline_to_string = pipeline_to_string & fan_mover;
    // Orca adaptive pressure advance, once the cooling buffer has set the final speeds
    const auto pa_processor = tbb::make_filter<std::string, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [pa_processor = this->m_pa_processor.get()](std::string s) -> std::string {
            return pa_processor->process_layer(std::move(s));
        });
    if (m_pa_processor)
        pipeline_to_string = pipeline_to_string & pa_processor;
    if (m_find_replace)
        pipeline_to_string = pipeline_to_string & find_replace;

    // It registers a handler that sets locales to "C" before any TBB thread starts participating in tbb::parallel_pipeline.
    // Handler is unregistered when the destructor is called.
    TBBLocalesSetter locales_setter;
    // The pipeline elements are joined using const references, thus no copying is performed.
    output_stream.find_replace_supress();
    tbb::parallel_pipeline(12, pipeline_to_layerresult & pipeline_to_string & output);
    output_stream.find_replace_enable();
}

std::string GCodeGenerator::placeholder_parser_process(
    const std::string   &name,
    const std::string   &templ,
    unsigned int         current_extruder_id,
    const ParserConfig *config_override)
{
    PlaceholderParserIntegration &ppi = m_placeholder_parser_integration;
    try {
        ppi.update_from_gcodewriter(m_writer, m_print->wipe_tower_data());
        std::string output = ppi.parser.process(templ, current_extruder_id, config_override, &ppi.output_config, &ppi.context);
        ppi.validate_output_vector_variables();


        if (auto pos{get_vector<double>(ppi.output_config, "position")}; pos != ppi.position) {
            // Update G-code writer.
            m_writer.update_position({ pos[0], pos[1], pos[2] });
            this->last_position = this->gcode_to_point({ pos[0], pos[1] });
        }

        const bool user_relative_e{m_writer.config.use_relative_e_distances};
        const auto output_e_position{
            !user_relative_e
                ? std::optional{get_vector<double>(ppi.output_config, "e_position")}
                : std::nullopt
        };
        const auto output_e_retracted{get_vector<double>(ppi.output_config, "e_retracted")};
        const auto output_e_restart_extra{get_vector<double>(ppi.output_config, "e_restart_extra")};

        for (const Extruder &e : m_writer.extruders()) {
            unsigned int eid = e.id();
            assert(eid < ppi.num_extruders);
            if ( eid < ppi.num_extruders) {
                if (! user_relative_e && ! is_approx(ppi.e_position[eid], output_e_position->at(eid)))
                    const_cast<Extruder&>(e).set_position(output_e_position->at(eid));
                if (! is_approx(ppi.e_retracted[eid], output_e_retracted[eid]) ||
                    ! is_approx(ppi.e_restart_extra[eid], output_e_restart_extra[eid]))
                    const_cast<Extruder&>(e).set_retracted(output_e_retracted[eid], output_e_restart_extra[eid]);
            }
        }

        return output;
    } 
    catch (const std::runtime_error& err)
    {
        // Collect the names of failed template substitutions for error reporting.
        auto it = ppi.failed_templates.find(name);
        if (it == ppi.failed_templates.end())
            // Only if there was no error reported for this template, store the first error message into the map to be reported.
            // We don't want to collect error message for each and every occurence of a single custom G-code section.
            ppi.failed_templates.insert(it, std::make_pair(name, std::string(err.what())));
        // Insert the macro error message into the G-code.
        return
            std::string("\n!!!!! Failed to process the custom G-code template ") + name + "\n" +
            err.what() +
            "!!!!! End of an error report for the custom G-code template " + name + "\n\n";
    }
}

// Parse the custom G-code, try to find mcode_set_temp_dont_wait and mcode_set_temp_and_wait or optionally G10 with temperature inside the custom G-code.
// Returns true if one of the temp commands are found, and try to parse the target temperature value into temp_out.
static bool custom_gcode_sets_temperature(const std::string &gcode, const int mcode_set_temp_dont_wait, const int mcode_set_temp_and_wait, const bool include_g10, int &temp_out)
{
    temp_out = -1;
    if (gcode.empty())
        return false;

    const char *ptr = gcode.data();
    bool temp_set_by_gcode = false;
    while (*ptr != 0) {
        // Skip whitespaces.
        for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
        if (*ptr == 'M' || // Line starts with 'M'. It is a machine command.
            (*ptr == 'G' && include_g10)) { // Only check for G10 if requested
            bool is_gcode = *ptr == 'G';
            ++ ptr;
            // Parse the M or G code value.
            char *endptr = nullptr;
            int mgcode = int(strtol(ptr, &endptr, 10));
            if (endptr != nullptr && endptr != ptr && 
                is_gcode ?
                    // G10 found
                    mgcode == 10 :
                    // M104/M109 or M140/M190 found.
                    (mgcode == mcode_set_temp_dont_wait || mgcode == mcode_set_temp_and_wait)) {
                ptr = endptr;
                if (! is_gcode)
                    // Let the caller know that the custom M-code sets the temperature.
                    temp_set_by_gcode = true;
                // Now try to parse the temperature value.
                // While not at the end of the line:
                while (strchr(";\r\n\0", *ptr) == nullptr) {
                    // Skip whitespaces.
                    for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
                    if (*ptr == 'S') {
                        // Skip whitespaces.
                        for (++ ptr; *ptr == ' ' || *ptr == '\t'; ++ ptr);
                        // Parse an int.
                        endptr = nullptr;
                        long temp_parsed = strtol(ptr, &endptr, 10);
                        if (endptr > ptr) {
                            ptr = endptr;
                            temp_out = temp_parsed;
                            // Let the caller know that the custom G-code sets the temperature
                            // Only do this after successfully parsing temperature since G10
                            // can be used for other reasons
                            temp_set_by_gcode = true;
                        }
                    } else {
                        // Skip this word.
                        for (; strchr(" \t;\r\n\0", *ptr) == nullptr; ++ ptr);
                    }
                }
            }
        }
        // Skip the rest of the line.
        for (; *ptr != 0 && *ptr != '\r' && *ptr != '\n'; ++ ptr);
        // Skip the end of line indicators.
        for (; *ptr == '\r' || *ptr == '\n'; ++ ptr);
    }
    return temp_set_by_gcode;
}

// Print the machine envelope G-code for the Marlin firmware based on the "machine_max_xxx" parameters.
// Do not process this piece of G-code by the time estimator, it already knows the values through another sources.
void GCodeGenerator::print_machine_envelope(GCodeOutputStream &file, const Print &print)
{
    const GCodeFlavor flavor = print.config().get<GCodeFlavor>("gcode_flavor");
    if ((flavor == GCodeFlavor::gcfMarlinLegacy
         || flavor == GCodeFlavor::gcfMarlinFirmware
         || flavor == GCodeFlavor::gcfPrusaFirmwareBuddy
         || flavor == GCodeFlavor::gcfRepRapFirmware)
        && print.config().get<Domain::MachineLimitsUsage>("machine_limits_usage")
            == Domain::MachineLimitsUsage::EmitToGCode)
    {
        int factor = flavor == GCodeFlavor::gcfRepRapFirmware ? 60 : 1; // RRF M203 and M566 are in mm/min
        file.write_format("M201 X%d Y%d Z%d E%d ; sets maximum accelerations, mm/sec^2\n",
            int(print.config().get<std::vector<double>>("machine_max_acceleration_x").front() + 0.5),
            int(print.config().get<std::vector<double>>("machine_max_acceleration_y").front() + 0.5),
            int(print.config().get<std::vector<double>>("machine_max_acceleration_z").front() + 0.5),
            int(print.config().get<std::vector<double>>("machine_max_acceleration_e").front() + 0.5));
        file.write_format("M203 X%d Y%d Z%d E%d ; sets maximum feedrates, %s\n",
            int(print.config().get<std::vector<double>>("machine_max_feedrate_x").front() * factor + 0.5),
            int(print.config().get<std::vector<double>>("machine_max_feedrate_y").front() * factor + 0.5),
            int(print.config().get<std::vector<double>>("machine_max_feedrate_z").front() * factor + 0.5),
            int(print.config().get<std::vector<double>>("machine_max_feedrate_e").front() * factor + 0.5),
            factor == 60 ? "mm / min" : "mm / sec");

        // Now M204 - acceleration. This one is quite hairy...
        if (flavor == GCodeFlavor::gcfRepRapFirmware) {
            // Uses M204 P[print] T[travel]
            file.write_format("M204 P%d T%d ; sets acceleration (P, T), mm/sec^2\n",
                int(print.config().get<std::vector<double>>("machine_max_acceleration_extruding").front() + 0.5),
                int(print.config().get<std::vector<double>>("machine_max_acceleration_travel").front() + 0.5));
        } else if (flavor == GCodeFlavor::gcfMarlinLegacy) {
            // Legacy Marlin uses M204 S[print] T[retract]
            file.write_format("M204 S%d T%d ; sets acceleration (S) and retract acceleration (R), mm/sec^2\n",
                int(print.config().get<std::vector<double>>("machine_max_acceleration_extruding").front() + 0.5),
                int(print.config().get<std::vector<double>>("machine_max_acceleration_retracting").front() + 0.5));
        } else if (flavor == GCodeFlavor::gcfMarlinFirmware
                   || flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
        {
            // New Marlin uses M204 P[print] R[retract] T[travel]
            file.write_format("M204 P%d R%d T%d ; sets acceleration (P, T) and retract acceleration (R), mm/sec^2\n",
                int(print.config().get<std::vector<double>>("machine_max_acceleration_extruding").front() + 0.5),
                int(print.config().get<std::vector<double>>("machine_max_acceleration_retracting").front() + 0.5),
                int(print.config().get<std::vector<double>>("machine_max_acceleration_travel").front() + 0.5));
        } else {
            assert(false);
        }

        assert(is_decimal_separator_point());
        file.write_format(flavor == GCodeFlavor::gcfRepRapFirmware
            ? "M566 X%.2lf Y%.2lf Z%.2lf E%.2lf ; sets the jerk limits, mm/min\n"
            : "M205 X%.2lf Y%.2lf Z%.2lf E%.2lf ; sets the jerk limits, mm/sec\n",
            print.config().get<std::vector<double>>("machine_max_jerk_x").front() * factor,
            print.config().get<std::vector<double>>("machine_max_jerk_y").front() * factor,
            print.config().get<std::vector<double>>("machine_max_jerk_z").front() * factor,
            print.config().get<std::vector<double>>("machine_max_jerk_e").front() * factor);

        if (flavor == GCodeFlavor::gcfMarlinFirmware
            || flavor == GCodeFlavor::gcfPrusaFirmwareBuddy)
        {
            // New Marlin uses M205 J[mm] for junction deviation (only apply if it is > 0)
            file.write_format(writer().set_junction_deviation(print.config().get<std::vector<double>>("machine_max_junction_deviation").front()).c_str());
        }

        if (flavor != GCodeFlavor::gcfRepRapFirmware)
            file.write_format("M205 S%d T%d ; sets the minimum extruding and travel feed rate, mm/sec\n",
                int(print.config().get<std::vector<double>>("machine_min_extruding_rate").front() + 0.5),
                int(print.config().get<std::vector<double>>("machine_min_travel_rate").front() + 0.5));
        else {
            // M205 Sn Tn not supported in RRF. They use M203 Inn to set minimum feedrate for
            // all moves. This is currently not implemented.
        }
    }
}

double GCodeGenerator::outer_wall_volumetric_speed(const Print& print, unsigned int extruder_id) const
{
    // Orca get_outer_wall_volumetric_speed: the outer wall's speed x its flow, at most the filament's
    // max volumetric speed (the default region, the first object's layer height)
    if (print.objects().empty() || print.num_print_regions() == 0)
        return 0.;
    const PrintObject& object = *print.objects().front();
    const Flow   flow  = print.get_print_region(0).flow(object, frExternalPerimeter, object.config().get<double>("layer_height"));
    const double speed = print.config().get<std::vector<Domain::FloatOrPercentage>>("external_perimeter_speed").at(extruder_id).float_value();
    const double max   = print.config().get<std::vector<double>>("filament_max_volumetric_speed").at(extruder_id);
    double volumetric = speed * flow.mm3_per_mm();
    if (volumetric > max)
        volumetric = max;
    return volumetric;
}

int GCodeGenerator::bed_temperature(const Print& print, bool first_layer, unsigned int first_printing_extruder_id) const
{
    // Orca bed_temperature_formula: the highest bed temperature of the filaments printing
    if (m_highest_bed_temperature)
        return first_layer ? m_highest_bed_temperature->first : m_highest_bed_temperature->second;
    // based on the first printing extruder or on the extruder in bed_temperature_extruder
    const int  num_extruders               = static_cast<int>(print.config().hw_config().material_slot_count());
    const int  bed_temperature_extruder    = print.config().get<int>("bed_temperature_extruder");
    const bool use_first_printing_extruder = bed_temperature_extruder <= 0 || bed_temperature_extruder > num_extruders;
    return print.config().get<std::vector<int>>(first_layer ? "first_layer_bed_temperature" : "bed_temperature")
        .at(use_first_printing_extruder ? first_printing_extruder_id : bed_temperature_extruder - 1);
}

std::string GCodeGenerator::_process_start_gcode(const Print& print, unsigned int current_extruder_id)
{
    const int num_extruders            = static_cast<int>(print.config().hw_config().material_slot_count());
    const int bed_temperature_extruder = print.config().get<int>("bed_temperature_extruder");
    if (m_highest_bed_temperature || (0 < bed_temperature_extruder && bed_temperature_extruder <= num_extruders)) {
        const int first_layer_bed_temperature = this->bed_temperature(print, true, current_extruder_id);
        ParserConfig config;
        config.set("first_layer_bed_temperature", std::vector<int>(num_extruders, first_layer_bed_temperature));
        return this->placeholder_parser_process("start_gcode", print.config().get<std::string>("start_gcode"), current_extruder_id, &config);
    } else {
        return this->placeholder_parser_process("start_gcode", print.config().get<std::string>("start_gcode"), current_extruder_id);
    }
}

// Write 1st layer bed temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M140 - Set Extruder Temperature
// M190 - Set Extruder Temperature and Wait
void GCodeGenerator::_print_first_layer_bed_temperature(GCodeOutputStream &file, const Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    const bool autoemit = print.config().get<bool>("autoemit_temperature_commands");

    // Initial bed temperature (GCodeGenerator::bed_temperature).
    int temp = this->bed_temperature(print, true, first_printing_extruder_id);

    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode     = -1;
    bool temp_set_by_gcode = custom_gcode_sets_temperature(gcode, 140, 190, false, temp_by_gcode);
    if (autoemit && temp_set_by_gcode && temp_by_gcode >= 0 && temp_by_gcode < 1000)
        temp = temp_by_gcode;
    // Always call m_writer.set_bed_temperature() so it will set the internal "current" state of the bed temp as if
    // the custom start G-code emited these.
    std::string set_temp_gcode = m_writer.set_bed_temperature(temp, wait);
    if (autoemit && ! temp_set_by_gcode)
        file.write(set_temp_gcode);
}



// Write chamber temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling chamber temperature.
// M141 - Set chamber Temperature
// M191 - Set chamber Temperature and Wait
void GCodeGenerator::_print_first_layer_chamber_temperature(GCodeOutputStream &file, const Print &print, const std::string &gcode, int temp, bool wait, bool accurate)
{
    if (temp == 0)
        return;
    bool autoemit = print.config().get<bool>("autoemit_temperature_commands");
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode     = -1;
    bool temp_set_by_gcode = custom_gcode_sets_temperature(gcode, 141, 191, false, temp_by_gcode);
    if (autoemit && temp_set_by_gcode && temp_by_gcode >= 0 && temp_by_gcode < 1000)
        temp = temp_by_gcode;
    // Always call m_writer.set_bed_temperature() so it will set the internal "current" state of the bed temp as if
    // the custom start G-code emited these.
    std::string set_temp_gcode = m_writer.set_chamber_temperature(temp, wait, accurate);
    if (autoemit && ! temp_set_by_gcode)
        file.write(set_temp_gcode);
}



// Write 1st layer extruder temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M104 - Set Extruder Temperature
// M109 - Set Extruder Temperature and Wait
// RepRapFirmware: G10 Sxx
void GCodeGenerator::_print_first_layer_extruder_temperatures(GCodeOutputStream &file, const Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    bool autoemit = print.config().get<bool>("autoemit_temperature_commands");
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode = -1;
    bool include_g10   = print.config().get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfRepRapFirmware;
    if (! autoemit  || custom_gcode_sets_temperature(gcode, 104, 109, include_g10, temp_by_gcode)) {
        // Set the extruder temperature at m_writer, but throw away the generated G-code as it will be written with the custom G-code.
        int temp = print.config().get<std::vector<int>>("first_layer_temperature").at(first_printing_extruder_id);
        if (autoemit && temp_by_gcode >= 0 && temp_by_gcode < 1000)
            temp = temp_by_gcode;
        m_writer.set_temperature(temp, wait, first_printing_extruder_id);
    } else {
        // Custom G-code does not set the extruder temperature. Do it now.
        if (print.config().get<bool>("single_extruder_multi_material")) {
            // Set temperature of the first printing extruder only.
            int temp = print.config().get<std::vector<int>>("first_layer_temperature").at(first_printing_extruder_id);
            if (temp > 0)
                file.write(m_writer.set_temperature(temp, wait, first_printing_extruder_id));
        } else {
            // Set temperatures of all the printing extruders.
            for (unsigned int tool_id : print.extruders()) {
                int temp = print.config().get<std::vector<int>>("first_layer_temperature").at(tool_id);

                if (print.config().get<bool>("ooze_prevention") && tool_id != first_printing_extruder_id) {
                    if (!print.config().get<std::vector<std::optional<int>>>("idle_temperature").at(tool_id))
                        temp += print.config().get<int>("standby_temperature_delta");
                    else
                        temp = *print.config().get<std::vector<std::optional<int>>>("idle_temperature").at(tool_id);
                }

                if (temp > 0)
                    file.write(m_writer.set_temperature(temp, wait, tool_id));
            }
        }
    }
}

std::vector<GCode::InstanceToPrint> GCodeGenerator::sort_print_object_instances(
    const std::vector<ObjectLayerToPrint>       &object_layers,
    // Ordering must be defined for normal (non-sequential print).
    const std::vector<const PrintInstance*>     *ordering,
    // For sequential print, the instance of the object to be printing has to be defined.
    const size_t                                 single_object_instance_idx)
{
    std::vector<InstanceToPrint> out;

    if (ordering == nullptr) {
        // Sequential print, single object is being printed.
        assert(object_layers.size() == 1);
        out.emplace_back(0, *object_layers.front().object(), single_object_instance_idx);
    } else {
        // Create mapping from PrintObject* to ObjectLayerToPrint ID.
        std::vector<std::pair<const PrintObject*, size_t>> sorted;
        sorted.reserve(object_layers.size());
        for (const ObjectLayerToPrint &object : object_layers)
            if (const PrintObject* print_object = object.object(); print_object)
                sorted.emplace_back(print_object, &object - object_layers.data());
        std::sort(sorted.begin(), sorted.end());

        if (! sorted.empty()) {
            out.reserve(sorted.size());
            for (const PrintInstance *instance : *ordering) {
                const PrintObject &print_object = *instance->print_object;
                std::pair<const PrintObject*, size_t> key(&print_object, 0);
                auto it = std::lower_bound(sorted.begin(), sorted.end(), key);
                if (it != sorted.end() && it->first == &print_object)
                    // ObjectLayerToPrint for this PrintObject was found.
                    out.emplace_back(it->second, print_object, instance - print_object.instances().data());
            }
        }
    }
    return out;
}

namespace ProcessLayer
{

static std::string emit_custom_color_change_gcode_per_print_z(
    GCodeGenerator& gcodegen,
    const Domain::CustomGCode::Item& custom_gcode,
    unsigned int current_extruder_id,
    unsigned int first_extruder_id, // ID of the first extruder printing this layer.
    const PrintConfigView& config
)
{
    using Biz::libpgcode::reserved_tag;
    using Biz::libpgcode::Tags;

    const bool single_extruder_multi_material = config.get<bool>("single_extruder_multi_material");
    const bool single_extruder_printer        = config.hw_config().tool_count == 1;
    const bool color_change                   = custom_gcode.type == Domain::CustomGCode::Type::ColorChange;

    std::string gcode;

    int color_change_extruder = -1;
    if (color_change && custom_gcode.extruder > 0)
        color_change_extruder = single_extruder_printer ? 0 : custom_gcode.extruder - 1;

    assert(color_change_extruder >= 0);
    // Color Change or Tool Change as Color Change.
    // add tag for processor
    gcode += ";" + std::string(reserved_tag(Tags::Color_Change)) + ",T" + std::to_string(color_change_extruder) +
        "," + custom_gcode.color + "\n";

    ParserConfig cfg;
    cfg.set("color_change_extruder", color_change_extruder);
    if (single_extruder_multi_material && !single_extruder_printer && color_change_extruder >= 0 && first_extruder_id != unsigned(color_change_extruder)) {
        //! FIXME_in_fw show message during print pause
        // FIXME: Why is pause_print_gcode here? Why is it supplied "color_change_extruder"?
        gcode += gcodegen.placeholder_parser_process("pause_print_gcode", config.get<std::string>("pause_print_gcode"), current_extruder_id, &cfg);
        gcode += "\n";
        gcode += "M117 Change filament for Extruder " + std::to_string(color_change_extruder) + "\n";
    } else {
        gcode += gcodegen.placeholder_parser_process("color_change_gcode", config.get<std::string>("color_change_gcode"), current_extruder_id, &cfg);
        gcode += "\n";
        //FIXME Tell G-code writer that M600 filled the extruder, thus the G-code writer shall reset the extruder to unretracted state after
        // return from M600. Thus the G-code generated by the following line is ignored.
        // see GH issue #6362
        gcodegen.writer().unretract();
    }

    return gcode;
}

static std::string emit_custom_gcode_per_print_z(
    GCodeGenerator& gcodegen,
    const Domain::CustomGCode::Item& custom_gcode,
    unsigned int current_extruder_id,
    // ID of the first extruder printing this layer.
    unsigned int first_extruder_id,
    const PrintConfigView& config
)
{
    using Domain::CustomGCode::Type;
    using Biz::libpgcode::reserved_tag;
    using Biz::libpgcode::Tags;
    std::string gcode;

    // Extruder switches are processed by LayerTools, they should be filtered out.
    assert(custom_gcode.type != CustomGCode::Type::ToolChange);

    CustomGCode::Type gcode_type = custom_gcode.type;
    const bool color_change = gcode_type == CustomGCode::Type::ColorChange;
    const bool tool_change = gcode_type == CustomGCode::Type::ToolChange;
    // Tool Change is applied as Color Change for a single extruder printer only.
    assert(!tool_change || config.hw_config().material_slot_count() == 1);

    // we should add or not colorprint_change in respect to nozzle_diameter count instead of really
    // used extruders count
    if (color_change || tool_change) {
        gcode += emit_custom_color_change_gcode_per_print_z(
            gcodegen, custom_gcode, current_extruder_id, first_extruder_id, config
        );
    } else {
        if (gcode_type == CustomGCode::Type::PausePrint) { // Pause print
            const std::string pause_print_msg = custom_gcode.extra;

            // add tag for processor
            gcode += ";" + std::string(reserved_tag(Tags::Pause_Print)) + "\n";
            //! FIXME_in_fw show message during print pause
            if (!pause_print_msg.empty())
                gcode += "M117 " + pause_print_msg + "\n";

            ParserConfig cfg;
            cfg.set("color_change_extruder", int(current_extruder_id));
            gcode += gcodegen.placeholder_parser_process(
                "pause_print_gcode", config.get<std::string>("pause_print_gcode"),
                current_extruder_id, &cfg
            );
        } else {
            // add tag for processor
            gcode += ";" + std::string(reserved_tag(Tags::Custom_Code)) + "\n";
            if (gcode_type == CustomGCode::Type::Template)
                // Template Custom Gcode
                gcode += gcodegen.placeholder_parser_process(
                    "template_custom_gcode", config.get<std::string>("template_custom_gcode"),
                    current_extruder_id
                );
            else
                // custom Gcode
                gcode += custom_gcode.extra;
        }
        gcode += "\n";
    }

    return gcode;
}
} // namespace ProcessLayer

bool GCodeGenerator::line_distancer_is_required(
    const std::vector<unsigned int>& extruder_ids,
    const std::vector<InstanceToPrint>& instances_to_print
)
{
    for (const InstanceToPrint& instance : instances_to_print) {
        const PrintObjectConfigView& object_config{instance.print_object.config()};
        for (const unsigned id : extruder_ids) {
            const double travel_slope{object_config.get<std::vector<double>>("travel_slope").at(id)};
            if (
                object_config.get<std::vector<bool>>("travel_lift_before_obstacle").at(id)
                && object_config.get<std::vector<double>>("travel_max_lift").at(id) > 0
                && travel_slope > 0
                && travel_slope < 90
            ) {
                return true;
            }
        }
    }
    return false;
}

Polyline GCodeGenerator::get_layer_change_xy_path(const Vec3d &from, const Vec3d &to, const Biz::Slicing::ExtrudeConfig& extrude_config) {
    bool could_be_wipe_disabled{false};
    const bool needs_retraction{true};

    const Point start_point{this->gcode_to_point(from.head<2>())};
    const Point end_point{this->gcode_to_point(to.head<2>())};

    Polyline xy_path{
        this->generate_travel_xy_path(start_point, end_point, needs_retraction, extrude_config, could_be_wipe_disabled)};
    std::vector<Vec2d> gcode_xy_path;
    gcode_xy_path.reserve(xy_path.size());
    for (const Point &point : xy_path.points) {
        gcode_xy_path.push_back(this->point_to_gcode(point));
    }

    Polyline result;
    for (const Vec2d& point : gcode_xy_path) {
        result.points.push_back(gcode_to_point(point));
    }

    return result;
}

GCode::Impl::Travels::ElevatedTravelParams get_ramping_layer_change_params(
    const Vec3d &from,
    const Vec3d &to,
    const Polyline &xy_path,
    const Biz::Slicing::ExtrudeConfig &config,
    const unsigned extruder_id,
    const GCode::TravelObstacleTracker &obstacle_tracker
) {
    using namespace GCode::Impl::Travels;

    ElevatedTravelParams elevation_params{
        get_elevated_traval_params(xy_path, config, extruder_id, obstacle_tracker)};

    const double z_change = to.z() - from.z();
    elevation_params.lift_height = std::max(z_change, elevation_params.lift_height);

    const double path_length = unscaled(xy_path.length());
    const double lift_at_travel_end = std::min(
        elevation_params.lift_height,
        elevation_params.lift_height / elevation_params.slope_end * path_length
    );
    if (lift_at_travel_end < z_change) {
        elevation_params.lift_height = z_change;
        elevation_params.slope_end = path_length;
    }

    return elevation_params;
}

std::string GCodeGenerator::get_ramping_layer_change_gcode(
    const Vec3d& from, const Vec3d& to, const unsigned extruder_id, const Biz::Slicing::ExtrudeConfig& config
)
{
    const Polyline xy_path{this->get_layer_change_xy_path(from, to, config)};

    const GCode::Impl::Travels::ElevatedTravelParams elevation_params{
        get_ramping_layer_change_params(
            from, to, xy_path, config, extruder_id, m_travel_obstacle_tracker
        )};
    return this->generate_ramping_layer_change_gcode(xy_path, from.z(), elevation_params);
}

std::string GCodeGenerator::generate_ramping_layer_change_gcode(
    const Polyline &xy_path,
    const double initial_elevation,
    const GCode::Impl::Travels::ElevatedTravelParams &elevation_params
) {
    using namespace GCode::Impl::Travels;

    const std::vector<double> ensure_points_at_distances = linspace(
        elevation_params.slope_end - elevation_params.blend_width / 2.0,
        elevation_params.slope_end + elevation_params.blend_width / 2.0,
        elevation_params.parabola_points_count
    );

    Points3 travel{generate_elevated_travel(
        xy_path.points, ensure_points_at_distances, initial_elevation,
        ElevatedTravelFormula{elevation_params}
    )};

    std::string travel_gcode;
    for (const Vec3crd &point : travel) {
        const Vec3d gcode_point{this->point_to_gcode(point)};
        travel_gcode += this->m_writer
                            .travel_to_xyz_force(gcode_point, "layer change");
    }
    return travel_gcode;
}

#ifndef NDEBUG
static inline bool validate_smooth_path(const GCode::SmoothPath &smooth_path, bool loop)
{
    assert(!smooth_path.empty());

    for (auto it = std::next(smooth_path.begin()); it != smooth_path.end(); ++ it) {
        assert(it->path.size() >= 2);
        assert(std::prev(it)->path.back().point == it->path.front().point);
    }
    assert(! loop || smooth_path.front().path.front().point == smooth_path.back().path.back().point);
    return true;
}
#endif //NDEBUG

namespace GCode {

std::pair<GCode::SmoothPath, std::size_t> split_with_seam(
    const ExtrusionLoop &loop,
    const boost::variant<Point, Seams::Scarf::Scarf> &seam,
    const bool flipped,
    const GCode::SmoothPathCache &smooth_path_cache,
    const double scaled_resolution,
    const double seam_point_merge_distance_threshold
) {
    if (loop.paths.empty() || loop.paths.front().empty()) {
        return {SmoothPath{}, 0};
    }
    const auto seam_point{boost::get<Point>(&seam)};
    const auto scarf{boost::get<Seams::Scarf::Scarf>(&seam)};

    if (seam_point != nullptr) {
        return {
            smooth_path_cache.resolve_or_fit_split_with_seam(
                loop, flipped, scaled_resolution, *seam_point, seam_point_merge_distance_threshold
            ),
            0};
    } else if (scarf != nullptr && scarf->start_point == scarf->end_point && !scarf->entire_loop) {
        return {smooth_path_cache.resolve_or_fit_split_with_seam(
            loop, flipped, scaled_resolution, scarf->start_point, seam_point_merge_distance_threshold
        ), 0};
    } else if (scarf != nullptr) {
        ExtrusionPaths paths{loop.paths};
        const auto apply_smoothing{[&](std::span<const ExtrusionPath> paths){
            return smooth_path_cache.resolve_or_fit(paths, false, scaled<double>(0.0015));
        }};
        return Seams::Scarf::add_scarf_seam(std::move(paths), *scarf, apply_smoothing, flipped);
    } else {
        throw std::runtime_error{"Unknown seam type!"};
    }
}
} // namespace GCode

static inline double get_seam_gap_distance_value(const PrintConfigView &config, const unsigned extruder_id)
{
    const double nozzle_diameter = Biz::Slicing::get_nozzle_diameter(config.hw_config(), extruder_id);
    const auto seam_gap_distance_override = config.get<std::vector<Domain::FloatOrPercentage>>("seam_gap_distance").at(extruder_id);
    if (!std::isnan(seam_gap_distance_override.get_abs_value(1.0))) {
        return seam_gap_distance_override.get_abs_value(nozzle_diameter);
    }

    return config.get<Domain::FloatOrPercentage>("seam_gap_distance").get_abs_value(nozzle_diameter);
}

using GCode::ExtrusionOrder::InstancePoint;

struct SmoothPathGenerator
{
    const Seams::Placer &seam_placer;
    const GCode::SmoothPathCaches &smooth_path_caches;
    double scaled_resolution;
    const PrintConfigView &config;
    bool enable_loop_clipping;

    GCode::ExtrusionOrder::PathSmoothingResult operator()(
        const Layer *layer,
        const PrintRegion *region,
        const ExtrusionEntityReference &extrusion_reference,
        const unsigned extruder_id,
        std::optional<InstancePoint> &previous_position
    ) {
        const ExtrusionEntity *extrusion_entity{&extrusion_reference.extrusion_entity()};

        GCode::SmoothPath result;
        std::size_t wipe_offset{0};

        if (auto loop = dynamic_cast<const ExtrusionLoop *>(extrusion_entity)) {
            // Because the G-code export has 1um resolution, don't generate segments shorter
            // than 1.5 microns, thus empty path segments will not be produced by G-code export.
            const auto seam_point_merge_distance_threshold{scaled<double>(0.0015)};
            const GCode::SmoothPathCache &smooth_path_cache{
                loop->role().is_perimeter() ? smooth_path_caches.layer_local() :
                                              smooth_path_caches.global()};
            const Point previous_point{
                previous_position ? previous_position->local_point : Point::Zero()};

            if (!config.get<bool>("spiral_vase") && loop->role().is_perimeter() && layer != nullptr && region != nullptr) {
                boost::variant<Point, Seams::Scarf::Scarf> seam{
                    this->seam_placer
                        .place_seam(layer, region, *loop, extrusion_reference.flipped(), previous_point)};
                std::tie(result, wipe_offset) = split_with_seam(
                    *loop, seam, extrusion_reference.flipped(), smooth_path_cache,
                    scaled_resolution, seam_point_merge_distance_threshold
                );
            } else {
                result = smooth_path_cache.resolve_or_fit_split_with_seam(
                    *loop, extrusion_reference.flipped(), scaled_resolution, previous_point,
                    seam_point_merge_distance_threshold
                );
            }

            // Clip the path to avoid the extruder to get exactly on the first point of the
            // loop; if polyline was shorter than the clipping distance we'd get a null
            // polyline, so we discard it in that case.
            if (const double extrusion_clipping = get_seam_gap_distance_value(config, extruder_id); enable_loop_clipping && extrusion_clipping > 0.) {
                clip_end(
                    result,
                    scaled<double>(extrusion_clipping),
                    scaled<double>(GCode::ExtrusionOrder::min_gcode_segment_length)
                );
            } else if (enable_loop_clipping && extrusion_clipping < 0.) {
                // Extend the extrusion slightly after the seam.
                const double      smooth_path_extension_length     = -1. * scaled<double>(extrusion_clipping);
                const double      smooth_path_extension_cut_length = length(result) - smooth_path_extension_length;
                GCode::SmoothPath smooth_path_extension            = result;

                clip_end(smooth_path_extension, smooth_path_extension_cut_length, scaled<double>(GCode::ExtrusionOrder::min_gcode_segment_length));
                Slic3r::append(result, smooth_path_extension);
            }

            assert(validate_smooth_path(result, !enable_loop_clipping));
        } else if (auto multipath = dynamic_cast<const ExtrusionMultiPath *>(extrusion_entity)) {
            result =
                smooth_path_caches.layer_local()
                    .resolve_or_fit(*multipath, extrusion_reference.flipped(), scaled_resolution);
        } else if (auto path = dynamic_cast<const ExtrusionPath *>(extrusion_entity)) {
            result = GCode::SmoothPath{GCode::SmoothPathElement{
                path->attributes(),
                smooth_path_caches.layer_local()
                    .resolve_or_fit(*path, extrusion_reference.flipped(), scaled_resolution)}};
        }
        for (auto it{result.rbegin()}; it != result.rend(); ++it) {
            if (!it->path.empty()) {
                previous_position = InstancePoint{it->path.back().point};
                break;
            }
        }

        return {result, wipe_offset};
    }
};

// Orca GCode::extrusion_role_to_string_for_parser (3.0 has no separate bottom surface role)
static std::string gcode_extrusion_role_to_string_for_parser(GCodeExtrusionRole role)
{
    switch (role) {
    case GCodeExtrusionRole::Perimeter: return "Perimeter";
    case GCodeExtrusionRole::ExternalPerimeter: return "ExternalPerimeter";
    case GCodeExtrusionRole::OverhangPerimeter: return "OverhangPerimeter";
    case GCodeExtrusionRole::InternalInfill: return "InternalInfill";
    case GCodeExtrusionRole::SolidInfill: return "SolidInfill";
    case GCodeExtrusionRole::TopSolidInfill: return "TopSolidInfill";
    case GCodeExtrusionRole::BridgeInfill: return "BridgeInfill";
    case GCodeExtrusionRole::InternalBridgeInfill: return "InternalBridgeInfill";
    case GCodeExtrusionRole::GapFill: return "GapFill";
    case GCodeExtrusionRole::Ironing: return "Ironing";
    case GCodeExtrusionRole::Skirt: return "Skirt";
    case GCodeExtrusionRole::Brim: return "Brim";
    case GCodeExtrusionRole::SupportMaterial: return "SupportMaterial";
    case GCodeExtrusionRole::SupportMaterialInterface: return "SupportMaterialInterface";
    case GCodeExtrusionRole::WipeTower: return "WipeTower";
    default: return "Mixed";
    }
}

std::vector<GCode::ExtrusionOrder::ExtruderExtrusions> GCodeGenerator::get_sorted_extrusions(
    const Print &print,
    const ObjectsLayerToPrint &layers,
    const LayerTools &layer_tools,
    const std::vector<InstanceToPrint> &instances_to_print,
    const GCode::SmoothPathCaches &smooth_path_caches,
    const bool first_layer
) {
    // Orca make_skirt_loops_per_extruder_1st_layer / _other_layers, per skirt / brim group: its skirt on the first
    // layer, then on the layers above up to the skirt height, each print_z once; at the print_z of the raft layers and
    // the object layers, not of the interlaced support layers (the caller asks for the combined skirts on the layers
    // with a skirt only). Returns the layer's skirt height.
    const GCode::ExtrusionOrder::SkirtGroupDueFunction skirt_group_due = [this, &print, &layer_tools, first_layer](size_t group_idx) -> std::optional<double> {
        std::vector<double> &skirt_done = m_skirt_group_done[group_idx];
        if (! print.has_skirt() || print.skirt_brim_groups()[group_idx].skirt.empty())
            return std::nullopt;
        if (first_layer) {
            if (! skirt_done.empty())
                return std::nullopt;
        } else if (! ((skirt_done.size() < size_t(print.config().get<int>("skirt_height")) || print.has_infinite_skirt()) &&
                      ! skirt_done.empty() && skirt_done.back() < layer_tools.print_z - EPSILON))
            return std::nullopt;
        skirt_done.emplace_back(layer_tools.print_z);
        return skirt_done.back() - (skirt_done.size() == 1 ? 0. : skirt_done[skirt_done.size() - 2]);
    };

    const SmoothPathGenerator smooth_path{
        m_seam_placer,
        smooth_path_caches,
        m_scaled_resolution,
        print.config(),
        m_enable_loop_clipping
    };

    using GCode::ExtrusionOrder::ExtruderExtrusions;
    using GCode::ExtrusionOrder::get_extrusions;

    const std::optional<Point> previous_position{
        this->last_position ? std::optional{Point{scaled(this->point_to_gcode(*this->last_position))}} :
                              std::nullopt};
    std::vector<ExtruderExtrusions> extrusions{
        get_extrusions(
            print,
            this->m_wipe_tower.get(),
            layers,
            first_layer,
            layer_tools,
            instances_to_print,
            skirt_group_due,
            m_pending_brims,
            this->m_writer.extruder()->id(),
            smooth_path,
            previous_position
        )
    };

    return extrusions;
}

/**
 * @brief Detects whether the extruder loop of process_layer() will emit a tool change before the
 * first extrusion of the layer.
 */
static bool is_tool_change_before_first_extrusion(
    const GCodeWriter& writer,
    const std::vector<GCode::ExtrusionOrder::ExtruderExtrusions>& extrusions
)
{
    if (!writer.multiple_extruders) {
        return false;
    }

    for (const GCode::ExtrusionOrder::ExtruderExtrusions& extruder_extrusions : extrusions) {
        if (writer.need_toolchange(extruder_extrusions.extruder_id)) {
            return true;
        }

        if (GCode::ExtrusionOrder::get_first_point(extruder_extrusions).has_value()) {
            break;
        }
    }

    return false;
}

// In sequential mode, process_layer is called once per each object and its copy,
// therefore layers will contain a single entry and single_object_instance_idx will point to the copy of the object.
// In non-sequential mode, process_layer is called per each print_z height with all object and support layers accumulated.
// For multi-material prints, this routine minimizes extruder switches by gathering extruder specific extrusion paths
// and performing the extruder specific extrusions together.
LayerResult GCodeGenerator::process_layer(
    const Print                    			&print,
    // Set of object & print layers of the same PrintObject and with the same print_z.
    const ObjectsLayerToPrint           	&layers,
    const LayerTools        		        &layer_tools,
    const GCode::SmoothPathCaches           &smooth_path_caches,
    const bool                               last_layer,
    // Pairs of PrintObject index and its instance index.
    const std::vector<const PrintInstance*> *ordering,
    // If set to size_t(-1), then print all copies of all objects.
    // Otherwise print a single copy of a single object.
    const size_t                     		 single_object_instance_idx)
{
    using Biz::libpgcode::reserved_tag;
    using Biz::libpgcode::Tags;
    using Domain::CustomGCode::Type;

    assert(! layers.empty());
    // Either printing all copies of all objects, or just a single copy of a single object.
    assert(single_object_instance_idx == size_t(-1) || layers.size() == 1);

    // First object, support and raft layer, if available.
    const Layer         *object_layer  = nullptr;
    const SupportLayer  *support_layer = nullptr;
    const SupportLayer  *raft_layer    = nullptr;
    for (const ObjectLayerToPrint &l : layers) {
        if (l.object_layer && ! object_layer)
            object_layer = l.object_layer;
        if (l.support_layer) {
            if (! support_layer)
                support_layer = l.support_layer;
            if (! raft_layer && support_layer->id() < support_layer->object()->slicing_parameters().raft_layers())
                raft_layer = support_layer;
        }
    }
    const Layer  &layer = (object_layer != nullptr) ? *object_layer : *support_layer;
    LayerResult   result { {}, layer.id(), false, last_layer, false};
    if (layer_tools.extruders.empty())
        // Nothing to extrude.
        return result;

    // Extract 1st object_layer and support_layer of this set of layers with an equal print_z.
    double             print_z       = layer.print_z + layer.object()->config().get<double>("z_offset");
    bool                 first_layer   = layer.id() == 0;
    unsigned int         first_extruder_id = layer_tools.extruders.front();

    const std::vector<InstanceToPrint> instances_to_print{sort_print_object_instances(layers, ordering, single_object_instance_idx)};

    // Check whether it is possible to apply the spiral vase logic for this layer.
    // Just a reminder: A spiral vase mode is allowed for a single object, single material print only.
    m_enable_loop_clipping = true;
    if (m_spiral_vase && layers.size() == 1 && support_layer == nullptr) {
        bool enable = (layer.id() > 0 || !print.has_brim()) && (layer.id() >= (size_t)print.config().get<int>("skirt_height") && ! print.has_infinite_skirt());
        if (enable) {
            for (const LayerRegion *layer_region : layer.regions())
                if (size_t(layer_region->region().extruder_config_value<int>("bottom_solid_layers", FlowRole::frSolidInfill)) > layer.id() ||
                    layer_region->perimeters().items_count() > 1u ||
                    layer_region->fills().items_count() > 0) {
                    enable = false;
                    break;
                }
        }
        result.spiral_vase_enable = enable;
        // If we're going to apply spiralvase to this layer, disable loop clipping.
        m_enable_loop_clipping = !enable;
    }

    const float height = first_layer ? static_cast<float>(print_z) : static_cast<float>(print_z) - m_last_layer_z;

    using GCode::ExtrusionOrder::ExtruderExtrusions;
    const std::vector<ExtruderExtrusions> extrusions{
        this->get_sorted_extrusions(print, layers, layer_tools, instances_to_print, smooth_path_caches, first_layer)};

    if (extrusions.empty()) {
        return result;
    }

    const auto optional_first_segment{GCode::ExtrusionOrder::get_first_point(extrusions)};
    if (!optional_first_segment) {
        return result;
    }
    const Geometry::ArcWelder::Segment &first_segment{*optional_first_segment};
    const Vec3crd first_point{to_3d(first_segment.point, scaled(print_z + (first_segment.height_fraction - 1.0) * height))};
    const PrintInstance* first_instance{get_first_instance(extrusions, instances_to_print)};
    m_label_objects.update(first_instance);

    const bool uses_wipe_tower = layer_tools.has_wipe_tower && m_wipe_tower;

    // Without a wipe tower, the travel to the next layer's first point must not happen before a tool change.
    const bool tool_change_before_first_extrusion =
        !uses_wipe_tower && is_tool_change_before_first_extrusion(m_writer, extrusions);

    std::string gcode;

    assert(is_decimal_separator_point()); // for the sprintfs

    // add tag for processor
    gcode += ";" + std::string(reserved_tag(Tags::Layer_Change)) + "\n";
    // export layer z
    gcode += std::string(";Z:") + float_to_string_decimal_point(print_z) + "\n";

    // export layer height
    gcode += std::string(";") + std::string(reserved_tag(Tags::Height))
        + float_to_string_decimal_point(height) + "\n";

    // Orca: the first layer's travels use initial_layer_travel_speed
    m_writer.set_is_first_layer(first_layer);

    // update caches
    const double previous_layer_z{m_last_layer_z};
    m_last_layer_z = static_cast<float>(print_z);
    m_max_layer_z  = std::max(m_max_layer_z, m_last_layer_z);
    m_last_height = height;

    // Set new layer - this will change Z and force a retraction if retract_layer_change is enabled.
    if (!first_layer && ! print.config().get<std::string>("before_layer_gcode").empty()) {
        ParserConfig config;
        config.set("layer_num", m_layer_index + 1);
        config.set("layer_z", print_z);
        config.set("max_layer_z", double{m_max_layer_z});
        gcode += this->placeholder_parser_process("before_layer_gcode",
            print.config().get<std::string>("before_layer_gcode"), m_writer.extruder()->id(), &config)
            + "\n";
    }

    // Initialize avoid crossing perimeters before a layer change.
    if (!instances_to_print.empty() && print.config().get<bool>("avoid_crossing_perimeters")) {
        const InstanceToPrint instance_to_print{instances_to_print.front()};
        this->m_avoid_crossing_perimeters.init_layer(
            *layers[instance_to_print.object_layer_to_print_id].layer());
        this->set_origin(unscale(first_instance->shift()));

        const GCode::PrintObjectInstance next_instance{
            &instances_to_print.front().print_object,
            int(instances_to_print.front().instance_id)
        };
        if (m_current_instance != next_instance) {
            m_avoid_crossing_perimeters.use_external_mp_once = true;
        }
    }

    const PrintObjectConfigView& first_object_config{layer.object()->config()};
    const Biz::Slicing::ExtrudeConfig first_object_extrude_config{first_object_config};

    // With a pending tool change there is no safe travel target before the tool change.
    // In that case, the travel to the first point happens after set_extruder() instead.
    const std::optional<Point> layer_change_first_point = tool_change_before_first_extrusion ?
        std::nullopt :
        std::optional<Point>{first_point.head<2>()};

    gcode += this->change_layer(
        previous_layer_z,
        print_z,
        result.spiral_vase_enable,
        layer_change_first_point,
        first_layer,
        first_object_extrude_config
    ); // this will increase m_layer_index
    m_layer = &layer;

    if (this->line_distancer_is_required(layer_tools.extruders, instances_to_print) && this->m_layer != nullptr && this->m_layer->lower_layer != nullptr)
        m_travel_obstacle_tracker.init_layer(layer, layers);

    m_object_layer_over_raft = false;
    // Orca: the timelapse G-code after the layer change
    if (const std::string& time_lapse_gcode = print.config().get<std::string>("time_lapse_gcode"); !time_lapse_gcode.empty()) {
        ParserConfig config;
        config.set("layer_num", m_layer_index);
        config.set("layer_z", print_z);
        config.set("max_layer_z", double{m_max_layer_z});
        gcode += this->placeholder_parser_process("timelapse_gcode", time_lapse_gcode, m_writer.extruder()->id(), &config) + "\n";
    }
    if (!first_layer && ! print.config().get<std::string>("layer_gcode").empty()) {
        ParserConfig config;
        config.set("layer_num", m_layer_index);
        config.set("layer_z", print_z);
        config.set("max_layer_z", double{m_max_layer_z});
        {
            double accumulated_mass = 0.;
            for (const Extruder& extruder : m_writer.extruders())
                accumulated_mass += extruder.extruded_volume() * extruder.filament_density() * 0.001;
            const auto acceleration_y = print.config().get<std::vector<double>>("machine_max_acceleration_y");
            double y_limit = 1e10;
            for (double value : acceleration_y)
                y_limit = std::min(y_limit, value);
            const double layer_mass = std::max(0., accumulated_mass - m_last_layer_accumulated_mass);
            m_last_layer_accumulated_mass = accumulated_mass;
            config.set("curr_y_acceleration_limit", y_limit);
            config.set("curr_accumulated_mass", accumulated_mass);
            config.set("curr_layer_mass", layer_mass);
        }
        gcode += this->placeholder_parser_process("layer_gcode",
            print.config().get<std::string>("layer_gcode"), m_writer.extruder()->id(), &config)
            + "\n";
    }

    // Orca: the process' junction deviation from the first layer on (Marlin)
    if (first_layer && m_writer.config.gcode_flavor == GCodeFlavor::gcfMarlinFirmware) {
        const double junction_deviation = print.config().get<double>("default_junction_deviation");
        if (junction_deviation > 0.)
            gcode += m_writer.set_junction_deviation(junction_deviation);
    }

    if (! first_layer && ! m_second_layer_things_done) {
        // Orca: power loss recovery from the 2nd layer
        gcode += m_writer.set_power_loss_recovery(print.config().get<Domain::PowerLossRecoveryMode>("enable_power_loss_recovery"));
        // Transition from 1st to 2nd layer. Adjust nozzle temperatures as prescribed by the nozzle dependent
        // first_layer_temperature vs. temperature settings.
        for (const Extruder &extruder : m_writer.extruders()) {
            if (print.config().get<bool>("single_extruder_multi_material") || m_ooze_prevention.enable) {
                // In single extruder multi material mode, set the temperature for the current extruder only.
                // The same applies when ooze prevention is enabled.
                if (extruder.id() != m_writer.extruder()->id())
                    continue;
            }
            int temperature = print.config().get<std::vector<int>>("temperature").at(extruder.id());
            if (temperature > 0 && (temperature != print.config().get<std::vector<int>>("first_layer_temperature").at(extruder.id())))
                gcode += m_writer.set_temperature(temperature, false, extruder.id());
        }

        // Bed temperature for layers from the 2nd layer (GCodeGenerator::bed_temperature).
        gcode += m_writer.set_bed_temperature(this->bed_temperature(print, false, first_extruder_id));

        // Mark the temperature transition from 1st to 2nd layer to be finished.
        m_second_layer_things_done = true;
    }

    if (print.config().get<bool>("avoid_crossing_curled_overhangs")) {
        m_avoid_crossing_curled_overhangs.clear();
        for (const ObjectLayerToPrint &layer_to_print : layers) {
            if (layer_to_print.object() == nullptr)
                continue;
            for (const auto &instance : layer_to_print.object()->instances()) {
                m_avoid_crossing_curled_overhangs.add_obstacles(layer_to_print.object_layer, instance.shift());
                m_avoid_crossing_curled_overhangs.add_obstacles(layer_to_print.support_layer, instance.shift());
            }
        }
    }

    const bool has_custom_gcode_to_emit     = single_object_instance_idx == size_t(-1) && layer_tools.custom_gcode != nullptr;
    const int  extruder_id_for_custom_gcode = int(layer_tools.extruder_needed_for_color_changer) - 1;

    if (has_custom_gcode_to_emit && extruder_id_for_custom_gcode == -1) {
        // Normal (non-sequential) print with some custom code without picking a specific extruder before it.
        // If we don't need to pick a specific extruder before the color change, we can just emit a custom g-code.
        // Otherwise, we will emit the g-code after picking the specific extruder.

        std::string custom_gcode = ProcessLayer::emit_custom_gcode_per_print_z(*this, *layer_tools.custom_gcode, m_writer.extruder()->id(), first_extruder_id, print.config());
        if (layer_tools.custom_gcode->type == CustomGCode::Type::ColorChange) {
            // We have a color change to do on this layer, but we want to do it immediately before the first extrusion instead of now, in order to fix GH #2672.
            m_pending_pre_extrusion_gcode = custom_gcode;
        } else {
            gcode += custom_gcode;
        }
    }

    this->set_origin({0, 0});
    this->m_moved_to_first_layer_point = false;


    // Extrude the skirt, brim, support, perimeters, infill ordered by the extruders.
    for (const ExtruderExtrusions &extruder_extrusions : extrusions)
    {
        gcode += uses_wipe_tower ?
            m_wipe_tower->tool_change(
                *this,
                first_object_config,
                extruder_extrusions.extruder_id,
                extruder_extrusions.extruder_id == layer_tools.extruders.back()
            ) :
            this->set_extruder(extruder_extrusions.extruder_id, print_z, first_object_config);

        // let analyzer tag generator aware of a role type change
        if (uses_wipe_tower) {
            m_last_processor_extrusion_role = GCodeExtrusionRole::WipeTower;
        }

        if (has_custom_gcode_to_emit && extruder_id_for_custom_gcode == int(extruder_extrusions.extruder_id)) {
            assert(m_writer.extruder()->id() == extruder_id_for_custom_gcode);
            assert(m_pending_pre_extrusion_gcode.empty());
            // Now we have picked the right extruder, so we can emit the custom g-code.
            gcode += ProcessLayer::emit_custom_gcode_per_print_z(*this, *layer_tools.custom_gcode, m_writer.extruder()->id(), first_extruder_id, print.config());
        }

        if (!extruder_extrusions.skirt.empty()) {
            gcode += m_label_objects.maybe_stop_instance();
            this->m_label_objects.update(nullptr);
        }

        // An extruder picked just to perform a color change may extrude nothing on this layer.
        // Such an iteration must not travel to the first point before the following tool change.
        const bool extrudes_anything =
            GCode::ExtrusionOrder::get_first_point(extruder_extrusions).has_value();

        if (!this->m_moved_to_first_layer_point && (uses_wipe_tower || extrudes_anything)) {
            const Point shift{first_instance->shift()};
            this->set_origin(unscale(shift));

            const GCode::PrintObjectInstance next_instance{
                &instances_to_print.front().print_object,
                int(instances_to_print.front().instance_id)
            };
            if (m_current_instance != next_instance) {
                m_avoid_crossing_perimeters.use_external_mp_once = true;
            }

            const double writer_z{m_writer.get_position().z()};
            const double previous_z{writer_z - print.config().get<double>("z_offset") <= 10 * std::numeric_limits<double>::epsilon() ? print_z : writer_z};

            gcode += this->travel_to_first_position(first_point - to_3d(shift, 0), previous_z, ExtrusionRole::Mixed, [this]() {
                if (m_writer.multiple_extruders) {
                    return std::string{""};
                }
                return m_label_objects.maybe_change_instance(m_writer);
            }, first_object_extrude_config);
            this->set_origin({0, 0});
        }

        if (!extruder_extrusions.skirt.empty()) {
            this->m_label_objects.update(nullptr);

            m_avoid_crossing_perimeters.use_external_mp();
            Flow layer_skirt_flow = print.skirt_flow().with_height(float(extruder_extrusions.skirt_height));
            double mm3_per_mm = layer_skirt_flow.mm3_per_mm();
            for (const auto&[_, smooth_path] : extruder_extrusions.skirt) {
                // Adjust flow according to this layer's layer height.
                //FIXME using the support_material_speed of the 1st object printed.
                gcode += this->extrude_skirt(smooth_path,
                    // Override of skirt extrusion parameters. extrude_skirt() will fill in the extrusion width.
                    ExtrusionFlow{ mm3_per_mm, 0., layer_skirt_flow.height() },
                    first_object_extrude_config
                );
            }
            m_avoid_crossing_perimeters.use_external_mp(false);
            // Allow a straight travel move to the first object point if this is the first layer (but don't in next layers).
            if (first_layer && extruder_extrusions.skirt.front().first == 0)
                m_avoid_crossing_perimeters.disable_once();
        }

        m_label_objects.update(first_instance);

        if (!extruder_extrusions.overriden_extrusions.empty()) {
            // Extrude wipes.
            size_t gcode_size_old = gcode.size();
            for (std::size_t i{0}; i < instances_to_print.size(); ++i) {
                const InstanceToPrint &instance{instances_to_print[i]};
                using GCode::ExtrusionOrder::OverridenExtrusions;
                const OverridenExtrusions &overriden_extrusions{extruder_extrusions.overriden_extrusions[i]};
                if (is_empty(overriden_extrusions.slices_extrusions)) {
                    continue;
                }
                this->initialize_instance(instance, layers[instance.object_layer_to_print_id], i == 0);
                gcode += this->extrude_slices(
                    instance, layers[instance.object_layer_to_print_id],
                    overriden_extrusions.slices_extrusions
                );
            }
            if (gcode_size_old < gcode.size()) {
                gcode+="; PURGING FINISHED\n";
            }
        }

        // Extrude normal extrusions, by the visits of the instances (Orca print_order).
        for (const GCode::ExtrusionOrder::NormalExtrusions &normal_extrusions : extruder_extrusions.normal_extrusions) {
            const InstanceToPrint &instance{instances_to_print[normal_extrusions.instance_index]};
            using GCode::ExtrusionOrder::SupportPath;
            const std::vector<SupportPath> &support_extrusions{normal_extrusions.support_extrusions};
            const ObjectLayerToPrint &layer_to_print{layers[instance.object_layer_to_print_id]};
            const std::vector<SliceExtrusions> &slices_extrusions{normal_extrusions.slices_extrusions};

            // Orca: the per object skirt and the brims, before the instance (in the print coordinates)
            if (!normal_extrusions.skirt.empty() || !normal_extrusions.brim.empty()) {
                this->set_origin({0, 0});
                gcode += m_label_objects.maybe_stop_instance();
                this->m_label_objects.update(nullptr);
                m_avoid_crossing_perimeters.use_external_mp();
                if (!normal_extrusions.skirt.empty()) {
                    const Flow   layer_skirt_flow = print.skirt_flow().with_height(float(normal_extrusions.skirt_height));
                    const double mm3_per_mm       = layer_skirt_flow.mm3_per_mm();
                    for (const auto&[_, smooth_path] : normal_extrusions.skirt)
                        gcode += this->extrude_skirt(smooth_path, ExtrusionFlow{ mm3_per_mm, 0., layer_skirt_flow.height() }, first_object_extrude_config);
                }
                for (const GCode::ExtrusionOrder::BrimPath &brim_path : normal_extrusions.brim)
                    gcode += this->extrude_smooth_path(
                        brim_path.path,
                        brim_path.is_loop,
                        "brim",
                        first_object_config.get<double>("support_material_speed"),
                        first_object_extrude_config
                    );
                m_avoid_crossing_perimeters.use_external_mp(false);
                // Allow a straight travel move to the first object point.
                m_avoid_crossing_perimeters.disable_once();
            }

            if (support_extrusions.empty() && is_empty(slices_extrusions)) {
                continue;
            }
            this->initialize_instance(instance, layers[instance.object_layer_to_print_id], &normal_extrusions == &extruder_extrusions.normal_extrusions.front());

            if (!support_extrusions.empty()) {
                m_layer = layer_to_print.support_layer;
                m_object_layer_over_raft = false;
                const Biz::Slicing::ExtrudeConfig print_object_config{instance.print_object.config()};
                gcode += this->extrude_support(support_extrusions, print_object_config);
            }

            gcode += this->extrude_slices(
                instance, layer_to_print, slices_extrusions
            );
        }
        this->set_origin(0.0, 0.0);
    }


    SPDLOG_TRACE("Exported layer {} print_z {} {}", layer.id(), print_z, log_memory_info());

    result.gcode = std::move(gcode);
    result.cooling_buffer_flush = object_layer || raft_layer || last_layer;
    return result;
}

static const auto comment_perimeter = "perimeter"sv;

void GCodeGenerator::initialize_instance(
    const InstanceToPrint &print_instance,
    const ObjectLayerToPrint &layer_to_print,
    const bool is_first
) {
    const PrintObject &print_object = print_instance.print_object;
    const Print       &print        = *print_object.print();

    m_layer = layer_to_print.layer();
    const Point offset = print_object.instances()[print_instance.instance_id].shift();
    GCode::PrintObjectInstance next_instance = {&print_object, int(print_instance.instance_id)};

    if (print.config().get<bool>("avoid_crossing_perimeters") && !is_first) {
        m_avoid_crossing_perimeters.init_layer(*m_layer);

        // When starting a new object, use the external motion planner for the first travel move.
        if (m_current_instance != next_instance) {
            m_avoid_crossing_perimeters.use_external_mp_once = true;
        }
    }

    m_current_instance = next_instance;

    this->set_origin(unscale(offset));
    m_label_objects.update(&print_instance.print_object.instances()[print_instance.instance_id]);
}

std::string GCodeGenerator::extrude_slices(
    const InstanceToPrint &print_instance,
    const ObjectLayerToPrint &layer_to_print,
    const std::vector<SliceExtrusions> &slices_extrusions
) {
    const PrintObject &print_object = print_instance.print_object;

    m_layer = layer_to_print.layer();
    // To control print speed of the 1st object layer printed over raft interface.
    m_object_layer_over_raft = layer_to_print.object_layer && layer_to_print.object_layer->id() > 0 &&
        print_object.slicing_parameters().raft_layers() == layer_to_print.object_layer->id();

    std::string gcode;
    for (const SliceExtrusions &slice_extrusions : slices_extrusions) {
        for (const IslandExtrusions &island_extrusions : slice_extrusions.common_extrusions) {
            if (island_extrusions.infill_first) {
                gcode += this->extrude_infill_ranges(island_extrusions.infill_ranges, "infill");
                gcode += this->extrude_perimeters(*island_extrusions.region, island_extrusions.perimeters, print_instance);
            } else {
                // Orca unsupported_wall_last: loops with nothing under them after the infill that anchors them
                const auto after_infill = [](const GCode::ExtrusionOrder::Perimeter &perimeter) {
                    const auto *loop = dynamic_cast<const ExtrusionLoop *>(perimeter.extrusion_entity);
                    return loop != nullptr && loop->print_after_infill;
                };
                if (std::any_of(island_extrusions.perimeters.begin(), island_extrusions.perimeters.end(), after_infill)) {
                    std::vector<GCode::ExtrusionOrder::Perimeter> anchored, deferred;
                    for (const GCode::ExtrusionOrder::Perimeter &perimeter : island_extrusions.perimeters)
                        (after_infill(perimeter) ? deferred : anchored).push_back(perimeter);
                    gcode += this->extrude_perimeters(*island_extrusions.region, anchored, print_instance);
                    gcode += this->extrude_infill_ranges(island_extrusions.infill_ranges, "infill");
                    gcode += this->extrude_perimeters(*island_extrusions.region, deferred, print_instance, &anchored);
                } else {
                    gcode += this->extrude_perimeters(*island_extrusions.region, island_extrusions.perimeters, print_instance);
                    gcode += this->extrude_infill_ranges(island_extrusions.infill_ranges, "infill");
                }
            }
        }

        gcode += this->extrude_infill_ranges(slice_extrusions.ironing_extrusions, "ironing");
    }

    return gcode;
}

void GCodeGenerator::set_extruders(const std::vector<unsigned int> &extruder_ids, const PrintConfigView& config)
{
    m_writer.set_extruders(extruder_ids);
    m_wipe.init(config, extruder_ids);
}

void GCodeGenerator::set_origin(const Vec2d &pointf)
{
    // if origin increases (goes towards right), last_pos decreases because it goes towards left
    const auto offset = scaled(Vec2d{m_origin - pointf});
    if (last_position.has_value())
        *(this->last_position) += offset;

    m_wipe.offset_path(offset);
    m_origin = pointf;
}

std::string GCodeGenerator::preamble(const double z_offset)
{
    std::string gcode = m_writer.preamble();

    /*  Perform a *silent* move to z_offset: we need this to initialize the Z
        position of our writer object so that any initial lift taking place
        before the first layer change will raise the extruder from the correct
        initial Z instead of 0.  */
    m_writer.travel_to_z(z_offset);

    return gcode;
}

// called by GCodeGenerator::process_layer()
std::string GCodeGenerator::change_layer(
    double previous_layer_z,
    double print_z,
    bool vase_mode,
    const std::optional<Point> first_point,
    const bool first_layer,
    const Slicing::ExtrudeConfig& config
)
{
    std::string gcode;
    if (m_layer_count > 0)
        // Increment a progress bar indicator.
        gcode += m_writer.update_progress(++ m_layer_index, m_layer_count);

    if (m_writer.multiple_extruders) {
        gcode += m_label_objects.maybe_change_instance(m_writer);
    }

    const unsigned extruder_id{m_writer.extruder()->id()};
    const bool do_ramping_layer_change =
        (this->last_position
         && first_point
         && !vase_mode
         && print_z > previous_layer_z
         && config.travel_ramping_lift.at(extruder_id)
         && config.travel_slope.at(extruder_id) > 0
         && config.travel_slope.at(extruder_id) < 90);

    const std::vector<double> retract_speed{config.retract_speed};
    const double travel_speed{config.travel_speed};

    if (this->last_position
        && first_point
        && print_z > previous_layer_z
        && !config.retract_layer_change.at(m_writer.extruder()->id()))
    {
        const Vec3d from{to_3d(this->point_to_gcode(*this->last_position), previous_layer_z)};
        const Vec3d to{to_3d(unscaled(*first_point), print_z)};
        const Polyline xy_path{this->get_layer_change_xy_path(from, to, config)};

        if (this->needs_retraction(xy_path, config, ExtrusionRole::Mixed)) {
            gcode += this->retract_and_wipe(retract_speed, travel_speed);
        }
    } else {
        gcode += this->retract_and_wipe(retract_speed, travel_speed);
    }

    if (do_ramping_layer_change) {
        // Must be determined again after possible wipe.
        const Vec3d from{to_3d(this->point_to_gcode(*this->last_position), previous_layer_z)};
        const Vec3d to{to_3d(unscaled(*first_point), print_z)};

        gcode += this->get_ramping_layer_change_gcode(from, to, extruder_id, config);

        this->writer().update_position(to);
        this->last_position = this->gcode_to_point(unscaled(first_point.value()));
    } else {
        if (!first_layer) {
            gcode += this->writer().travel_to_z_force(print_z, "simple layer change");
        } else {
            Vec3d position{this->writer().get_position()};
            position.z() = position.z() + config.z_offset;
            this->writer().update_position(position);
        }
    }

    // forget last wiping path as wiping after raising Z is pointless
    m_wipe.reset_path();

    return gcode;
}

std::string GCodeGenerator::extrude_smooth_path(
    const GCode::SmoothPath &smooth_path,
    const bool is_loop,
    const std::string_view description,
    const double speed,
    const Biz::Slicing::ExtrudeConfig& config,
    const std::size_t wipe_offset
) {
    std::string gcode;

    // Orca adaptive pressure advance: one PA_CHANGE tag for the smooth path, by its length-weighted average flow
    m_apa_path_set           = false;
    m_apa_average_mm3_per_mm = 0.;
    if (m_pa_processor) {
        double weighted_mm3_per_mm = 0., length = 0.;
        for (const GCode::SmoothPathElement &el : smooth_path)
            if (el.path_attributes.mm3_per_mm > 0.) {
                const double el_length = Geometry::ArcWelder::path_length<double>(el.path);
                weighted_mm3_per_mm += el.path_attributes.mm3_per_mm * el_length;
                length += el_length;
            }
        if (length > 0.)
            m_apa_average_mm3_per_mm = weighted_mm3_per_mm / length;
    }

    // Extrude along the smooth path.
    bool          is_bridge_extruded = false;
    EmitModifiers emit_modifiers     = EmitModifiers::create_with_disabled_emits();
    for (auto el_it = smooth_path.begin(); el_it != smooth_path.end(); ++el_it) {
        const auto next_el_it = next(el_it);

        // By default, GCodeGenerator::_extrude() emit markers _BRIDGE_FAN_START, _BRIDGE_FAN_END and _RESET_FAN_SPEED for every extrusion.
        // Together with split extrusions because of different ExtrusionAttributes, this could flood g-code with those markers and then
        // produce an unnecessary number of duplicity M106.
        // To prevent this, we control when each marker should be emitted by EmitModifiers, which allows determining when a bridge starts and ends,
        // even when it is split into several extrusions.
        if (el_it->path_attributes.role.is_bridge()) {
            emit_modifiers.emit_bridge_fan_start = !is_bridge_extruded;
            emit_modifiers.emit_bridge_fan_end   = next_el_it == smooth_path.end() || !next_el_it->path_attributes.role.is_bridge();
            is_bridge_extruded                   = true;
        } else if (is_bridge_extruded) {
            emit_modifiers.emit_bridge_fan_start = false;
            emit_modifiers.emit_bridge_fan_end   = false;
            is_bridge_extruded                   = false;
        }

        // Ensure that just for the last extrusion from the smooth path, the fan speed will be reset back
        // to the value calculated by the CoolingBuffer.
        if (next_el_it == smooth_path.end()) {
            emit_modifiers.emit_fan_speed_reset = true;
        }

        gcode += this->_extrude(el_it->path_attributes, el_it->path, description, speed, config, emit_modifiers);
        m_apa_path_set = true;
    }
    m_apa_path_set           = false;
    m_apa_average_mm3_per_mm = 0.;

    // reset acceleration
    gcode += m_writer.set_print_acceleration(
        fast_round_up<unsigned int>(
            config.default_acceleration.at(m_writer.extruder()->id())
        )
    );

    if (is_loop) {
        GCode::SmoothPath wipe{smooth_path.begin() + wipe_offset, smooth_path.end()};
        m_wipe.set_path(std::move(wipe));
    } else {
        if (wipe_offset > 0) {
            throw std::runtime_error("Wipe offset is not supported for non looped paths!");
        }

        GCode::SmoothPath reversed_smooth_path{smooth_path};
        GCode::reverse(reversed_smooth_path);
        m_wipe.set_path(std::move(reversed_smooth_path));
    }

    return gcode;
}

std::string GCodeGenerator::extrude_skirt(
    GCode::SmoothPath smooth_path,
    const ExtrusionFlow& extrusion_flow_override,
    const Biz::Slicing::ExtrudeConfig& config
)
{
    // Extrude along the smooth path.
    std::string gcode;
    for (GCode::SmoothPathElement &el : smooth_path) {
        // Override extrusion parameters.
        el.path_attributes.mm3_per_mm = extrusion_flow_override.mm3_per_mm;
        el.path_attributes.height = extrusion_flow_override.height;
    }

    gcode += this->extrude_smooth_path(smooth_path, true, "skirt"sv, config.support_material_speed, config);

    return gcode;
}

std::string GCodeGenerator::extrude_infill_ranges(
    const std::vector<InfillRange> &infill_ranges,
    const std::string &comment
) {
    std::string gcode{};
    for (const InfillRange &infill_range : infill_ranges) {
        if (!infill_range.items.empty()) {
            const Biz::Slicing::ExtrudeConfig config{infill_range.region->config()};
            for (const GCode::SmoothPath &path : infill_range.items) {
                gcode += this->extrude_smooth_path(path, false, comment, -1.0, config);
            }
        }
    }
    return gcode;
}

// Whether a smooth path (a loop in print order) runs counter-clockwise.
static bool smooth_path_is_ccw(const GCode::SmoothPath &path)
{
    double area2 = 0.;
    Point  prev  = path.back().path.back().point;
    for (const GCode::SmoothPathElement &el : path)
        for (const Geometry::ArcWelder::Segment &segment : el.path) {
            area2 += double(prev.x()) * double(segment.point.y()) - double(segment.point.x()) * double(prev.y());
            prev = segment.point;
        }
    return area2 > 0.;
}

// Orca Point::ccw_angle: the angle (0, 2π] at center from p1 to p2
static double ccw_angle(const Point &center, const Point &p1, const Point &p2)
{
    const double angle = atan2(double(p1.x() - center.x()), double(p1.y() - center.y())) -
        atan2(double(p2.x() - center.x()), double(p2.y() - center.y()));
    return angle <= 0 ? angle + 2. * M_PI : angle;
}

// Orca Point::rotate around a center
static Point rotated(const Point &p, const double angle, const Point &center)
{
    const Vec2d  d = (p - center).cast<double>();
    const double s = std::sin(angle), c = std::cos(angle);
    return Point(fast_round_up<coord_t>(center.x() + c * d.x() - s * d.y()), fast_round_up<coord_t>(center.y() + s * d.x() + c * d.y()));
}

// Orca wipe_on_loops_destination: a little inside the loop start (the seam), to move to after an external
// perimeter; none for a degenerate loop
static std::optional<Point> wipe_on_loops_destination(const GCode::ExtrusionOrder::Perimeter &perimeter, const double nozzle_diameter_scaled)
{
    const GCode::SmoothPath &path = perimeter.smooth_path;
    const auto *loop = dynamic_cast<const ExtrusionLoop *>(perimeter.extrusion_entity);
    if (loop == nullptr || path.empty() || nozzle_diameter_scaled <= 0.)
        return {};
    // clamped to a quarter of the loop so the forward and backward samples cannot meet
    const double sample_distance = std::min(nozzle_diameter_scaled, GCode::length(path) * 0.25);
    std::optional<Point> a = GCode::sample_path_point_at_distance_from_start(path, sample_distance);
    std::optional<Point> b = GCode::sample_path_point_at_distance_from_end(path, sample_distance);
    const Point seam_start = path.front().path.front().point;
    if (!a || !b || *a == *b || *a == seam_start || *b == seam_start)
        return {};
    const bool reverse_turn = loop->is_clockwise() == smooth_path_is_ccw(path);
    if (reverse_turn)
        std::swap(a, b);
    double angle = ccw_angle(seam_start, *a, *b) / 3;
    // degenerate angles near 0 or 2π
    static constexpr double angle_epsilon = 0.01;
    if (angle < angle_epsilon || angle > 2 * M_PI / 3 - angle_epsilon)
        return {};
    if (reverse_turn)
        angle *= -1;
    const std::optional<Point> pt = GCode::sample_path_point_at_distance_from_start(path, std::min(0.2 * nozzle_diameter_scaled, sample_distance));
    if (!pt)
        return {};
    return rotated(*pt, angle, seam_start);
}

// Orca (SuperSlicer) wipe_before_external_loop: the point slightly inside the wall to de-retract an external
// perimeter loop at, if another perimeter of the island is within a nozzle diameter of it (so it is inside the model)
static std::optional<Point> external_loop_deretract_point(
    const GCode::ExtrusionOrder::Perimeter &perimeter,
    const std::vector<GCode::ExtrusionOrder::Perimeter> &perimeters,
    const double nozzle_diameter
) {
    const GCode::SmoothPath &path = perimeter.smooth_path;
    if (path.empty() || path.front().path.size() < 2 || path.back().path.size() < 2)
        return {};
    const auto *loop = dynamic_cast<const ExtrusionLoop *>(perimeter.extrusion_entity);
    if (loop == nullptr)
        return {};
    const bool is_hole = loop->is_clockwise();
    const bool is_ccw  = smooth_path_is_ccw(path);

    // previous & next are inverted to extrude in the opposite direction: we are "rewinding"
    const Point previous_point = path.front().path[1].point;
    const Point current_point  = path.front().path.front().point;
    Point       next_point     = path.back().path.back().point;
    // can happen if seam_gap is null
    if (next_point == current_point)
        next_point = path.back().path[path.back().path.size() - 2].point;
    Point a = next_point, b = previous_point;
    if (is_hole ? !is_ccw : is_ccw)
        std::swap(a, b);
    double angle = ccw_angle(current_point, a, b) / 3.;
    // turn outwards if contour, inwards if hole
    if (is_hole ? !is_ccw : is_ccw)
        angle *= -1.;

    const Vec2d  current_pos = current_point.cast<double>();
    const Vec2d  vec_dist    = next_point.cast<double>() - current_pos;
    const double vec_norm    = vec_dist.norm();
    if (vec_norm <= 0.)
        return {};
    // half the nozzle diameter or of the perimeter's width, whichever is smaller
    const double dist = std::min(scaled(nozzle_diameter) * 0.5, scaled(double(path.front().path_attributes.width)) * 0.5);
    const Point  pt   = rotated((current_pos + vec_dist * (2 * dist / vec_norm)).cast<coord_t>(), angle, current_point);

    // the de-retraction point must touch this perimeter and another one
    const double radius = scaled(nozzle_diameter);
    int touching = 0;
    for (const GCode::ExtrusionOrder::Perimeter &other : perimeters) {
        Points points;
        other.extrusion_entity->collect_points(points);
        if (other.extrusion_entity->is_loop() && !points.empty())
            points.push_back(points.front());
        for (size_t i = 1; i < points.size(); ++i) {
            const Vec2d  p0 = points[i - 1].cast<double>(), p1 = points[i].cast<double>(), v = p1 - p0;
            const double t  = v.squaredNorm() > 0. ? std::clamp((pt.cast<double>() - p0).dot(v) / v.squaredNorm(), 0., 1.) : 0.;
            if ((p0 + t * v - pt.cast<double>()).norm() <= radius) {
                ++touching;
                break;
            }
        }
        if (touching > 1)
            return pt;
    }
    return {};
}

// Orca wipe_inward works on the points of a path: the arcs discretized
static Polyline smooth_path_to_polyline(GCode::SmoothPath::const_iterator begin, GCode::SmoothPath::const_iterator end, const double resolution)
{
    Polyline out;
    for (auto it = begin; it != end; ++ it)
        for (size_t i = 0; i < it->path.size(); ++ i) {
            const Geometry::ArcWelder::Segment &segment = it->path[i];
            if (i == 0) {
                if (out.empty() || out.points.back() != segment.point)
                    out.points.push_back(segment.point);
            } else if (segment.linear())
                out.points.push_back(segment.point);
            else {
                const Points arc = Geometry::ArcWelder::arc_discretize(it->path[i - 1].point, segment.point, segment.radius, segment.ccw(), resolution);
                out.points.insert(out.points.end(), arc.begin() + 1, arc.end());
            }
        }
    return out;
}

std::string GCodeGenerator::extrude_perimeters(
    const PrintRegion &region,
    const std::vector<GCode::ExtrusionOrder::Perimeter> &perimeters,
    const InstanceToPrint &print_instance,
    const std::vector<GCode::ExtrusionOrder::Perimeter> *printed_before
) {
    std::string gcode{};

    const Biz::Slicing::ExtrudeConfig config{region.config()};

    // Orca wipe_inward: the printed prefix of the region's perimeters, the support of an inward wipe
    const auto wipe_inward_distance = region.config().get<Domain::FloatOrPercentage>("wipe_inward_distance");
    std::optional<WipeInwardSupport> wipe_support;
    if (m_wipe.enabled() && m_wipe_enabled.at(m_writer.extruder()->id()) && region.config().get<bool>("wipe_inward") &&
        ! wipe_inward_distance.is_zero() && scale_(m_wipe.wipe_distance(m_writer.extruder()->id())) > SCALED_EPSILON) {
        wipe_support.emplace();
        if (printed_before != nullptr)
            for (const GCode::ExtrusionOrder::Perimeter &printed : *printed_before)
                wipe_support->append(*printed.extrusion_entity);
    }

    for (const GCode::ExtrusionOrder::Perimeter &perimeter : perimeters) {
        double speed{-1};
        // Apply the small perimeter speed (Orca: SMALL_PERIMETER_LENGTH(small_perimeter_threshold)).
        if (perimeter.extrusion_entity->length()
            <= (m_print->config().get<double>("small_perimeter_threshold") / SCALING_FACTOR) * 2 * PI)
            speed = region
                        .extruder_config_value<Domain::FloatOrPercentage>(
                            "small_perimeter_speed",
                            FlowRole::frExternalPerimeter
                        )
                        .get_abs_value(region.extruder_config_value<double>(
                            "perimeter_speed",
                            FlowRole::frExternalPerimeter
                        ));
        if (region.config().get<bool>("wipe_before_external_loop") && perimeter.extrusion_entity->is_loop() &&
            perimeter.extrusion_entity->role().is_external_perimeter() && perimeters.size() > 1) {
            const double nozzle_diameter = Biz::Slicing::get_nozzle_diameter(region.config().hw_config(), m_writer.extruder()->id());
            if (const std::optional<Point> pt = external_loop_deretract_point(perimeter, perimeters, nozzle_diameter)) {
                // travel to it, de-retract there, then move to the loop start without extruding
                ExtrusionAttributes attributes = perimeter.smooth_path.front().path_attributes;
                attributes.mm3_per_mm          = 0.;
                Geometry::ArcWelder::Path move{{*pt}, {perimeter.smooth_path.front().path.front().point}};
                for (Geometry::ArcWelder::Segment &segment : move)
                    segment.e_fraction = 0.f;
                gcode += this->_extrude(attributes, move, "move inwards before retraction/seam"sv, speed, config,
                                        EmitModifiers::create_with_disabled_emits());
            }
        }
        gcode += this->extrude_smooth_path(
            perimeter.smooth_path,
            perimeter.extrusion_entity->is_loop(),
            comment_perimeter,
            speed,
            config,
            perimeter.wipe_offset
        );
        this->m_travel_obstacle_tracker.mark_extruded(
            perimeter.extrusion_entity, print_instance.object_layer_to_print_id, print_instance.instance_id
        );

        const bool is_extruding{
            !perimeter.smooth_path.empty()
            && !perimeter.smooth_path.front().path.empty()
            && perimeter.smooth_path.front().path.front().e_fraction > 0
        };

        if (
            region.config().get<bool>("wipe_on_loops")
            && perimeter.extrusion_entity->role().is_external_perimeter()
            && m_layer != nullptr
            && region.extruder_config_value<int>("perimeters", FlowRole::frPerimeter) > 1
            && is_extruding
        ) {
            // Orca wipe_on_loops: make a little move inwards before leaving loop.
            if (std::optional<Point> pt = wipe_on_loops_destination(
                    perimeter,
                    scale_(Biz::Slicing::get_nozzle_diameter(region.config().hw_config(), m_writer.extruder()->id())));
                pt)
            {
                // Generate the seam hiding travel move.
                gcode += m_writer.travel_to_xy(this->point_to_gcode(*pt), "move inwards before travel");
                this->last_position = *pt;
            }
        }

        // Orca wipe_inward: the wipe after an external loop moved toward the inner walls printed before it, made
        // right away without a retraction (the travel after it decides on retracting)
        if (wipe_support && is_extruding && perimeter.extrusion_entity->is_loop() && this->last_position &&
            ! wipe_support->inner_lines.empty() &&
            std::any_of(perimeter.smooth_path.begin(), perimeter.smooth_path.end(), [](const GCode::SmoothPathElement &element) {
                return element.path_attributes.role == ExtrusionRole::ExternalPerimeter; })) {
            // the loop in print order from where its wipe starts
            const Polyline wipe_path = smooth_path_to_polyline(
                perimeter.smooth_path.begin() + perimeter.wipe_offset, perimeter.smooth_path.end(), m_scaled_resolution);
            // the actual extrusion width, not the configured one (which may be automatic)
            const double width = perimeter.smooth_path.front().path_attributes.width;
            const double offset_dist = scale_(std::min(wipe_inward_distance.get_abs_value(width), width));
            if (wipe_path.size() >= 2 && offset_dist > SCALED_EPSILON) {
                const auto  *loop            = static_cast<const ExtrusionLoop *>(perimeter.extrusion_entity);
                const double nozzle_diameter = Biz::Slicing::get_nozzle_diameter(region.config().hw_config(), m_writer.extruder()->id());
                Polyline     inward_path     = wipe_path;
                if (offset_wipe_path_toward_support(
                        inward_path, wipe_path.first_point(), perimeter.smooth_path.back().path.back().point, *this->last_position,
                        wipe_offset_direction(smooth_path_is_ccw(perimeter.smooth_path), loop->is_clockwise()), offset_dist,
                        scale_(m_wipe.wipe_distance(m_writer.extruder()->id())), wipe_support->inner_lines,
                        wipe_support->printed_lines, Algorithms::Polyline::to_lines(wipe_path), scale_(std::max(nozzle_diameter, width)))) {
                    // the first point stands for the nozzle position
                    inward_path.points.front() = *this->last_position;
                    Geometry::ArcWelder::Path path;
                    for (const Point &point : inward_path.points)
                        path.push_back({ point });
                    m_wipe.set_path(std::move(path));
                    gcode += m_wipe.wipe_without_retraction(*this, config.travel_speed);
                }
            }
        }
        if (wipe_support)
            wipe_support->append(*perimeter.extrusion_entity);
    }
    return gcode;
};

bool GCodeGenerator::retract_lift_allowed(const Biz::Slicing::ExtrudeConfig& config, unsigned int extruder_id) const
{
    // Orca GCode::retract: top = after a top surface or ironing (gap fill aside), bottom = the first layer
    const bool after_top = m_last_notgapfill_role == ExtrusionRole::TopSolidInfill || m_last_notgapfill_role == ExtrusionRole::Ironing;
    switch (config.retract_lift_enforce.at(extruder_id)) {
    case Domain::RetractLiftEnforce::TopOnly:      return after_top;
    case Domain::RetractLiftEnforce::BottomOnly:   return this->on_first_layer();
    case Domain::RetractLiftEnforce::TopAndBottom: return this->on_first_layer() || after_top;
    default:                                       return true;
    }
}

std::string GCodeGenerator::extrude_support(
    const std::vector<GCode::ExtrusionOrder::SupportPath>& support_extrusions,
    const Biz::Slicing::ExtrudeConfig& config
)
{
    static constexpr const auto support_label            = "support material"sv;
    static constexpr const auto support_interface_label  = "support material interface"sv;

    std::string gcode;
    if (! support_extrusions.empty()) {
        const double  support_speed            = config.support_material_speed;
        const double  support_interface_speed  = config.support_material_interface_speed.get_abs_value(support_speed);
        for (const GCode::ExtrusionOrder::SupportPath &path : support_extrusions) {
            const auto   label = path.is_interface ?  support_interface_label : support_label;
            double       speed = path.is_interface ? support_interface_speed : support_speed;
            // Orca: small support areas (up to 2π × small_support_perimeter_threshold long) at
            // small_support_perimeter_speed, a percentage of the support / interface speed (0: half of it)
            if (!GCode::longer_than(path.path, scale_(config.small_support_perimeter_threshold) * 2. * M_PI))
                speed = config.small_support_perimeter_speed.is_zero() ? speed * 0.5 :
                                                                         config.small_support_perimeter_speed.get_abs_value(speed);
            gcode += this->extrude_smooth_path(path.path, false, label, speed, config);
        }
    }
    return gcode;
}

void GCodeGenerator::GCodeOutputStream::write(const char *what)
{
    if (what != nullptr) {
        //FIXME don't allocate a string, maybe process a batch of lines?
        std::string gcode(m_find_replace ? m_find_replace->process_layer(what) : what);
        m_processor.process_buffer(std::move(gcode));
    }
}

void GCodeGenerator::GCodeOutputStream::writeln(const std::string &what)
{
    if (! what.empty())
        this->write(what.back() == '\n' ? what : what + '\n');
}

void GCodeGenerator::GCodeOutputStream::write_format(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int buflen;
    {
        va_list args2;
        va_copy(args2, args);
        buflen =
    #ifdef _MSC_VER
            ::_vscprintf(format, args2)
    #else
            ::vsnprintf(nullptr, 0, format, args2)
    #endif
            + 1;
        va_end(args2);
    }

    char buffer[1024];
    bool buffer_dynamic = buflen > 1024;
    char *bufptr = buffer_dynamic ? (char*)malloc(buflen) : buffer;
    int res = ::vsnprintf(bufptr, buflen, format, args);
    if (res > 0)
        this->write(bufptr);

    if (buffer_dynamic)
        free(bufptr);

    va_end(args);
}

std::string GCodeGenerator::travel_to_first_position(
    const Vec3crd& point,
    const double from_z,
    const ExtrusionRole role,
    const std::function<std::string()>& insert_gcode,
    const Biz::Slicing::ExtrudeConfig& config
)
{
    std::string gcode;

    const Vec3d gcode_point = to_3d(this->point_to_gcode(point.head<2>()), unscaled(point.z()));

    if (!config.travel_ramping_lift.at(m_writer.extruder()->id()) && this->last_position) {
        const Vec3crd from{to_3d(*this->last_position, scaled(from_z))};
        gcode = this->travel_to(
            from, point, role, "travel to first layer point", insert_gcode, config, EnforceFirstZ::True
        );
    } else {
        double lift{
            config.travel_ramping_lift.at(m_writer.extruder()->id()) ?
                config.travel_max_lift.at(m_writer.extruder()->id()) :
                config.retract_lift.at(m_writer.extruder()->id())
        };
        const double upper_limit = config.retract_lift_below.at(m_writer.extruder()->id());
        const double lower_limit = config.retract_lift_above.at(m_writer.extruder()->id());
        if ((lower_limit > 0 && gcode_point.z() < lower_limit) ||
            (upper_limit > 0 && gcode_point.z() > upper_limit) ||
            !this->retract_lift_allowed(config, m_writer.extruder()->id())) {
            lift = 0.0;
        }

        if (config.retract_length.at(m_writer.extruder()->id()) > 0 && !this->last_position) {
            if (!this->last_position || config.retract_before_travel.at(m_writer.extruder()->id()) < (this->point_to_gcode(*this->last_position) - gcode_point.head<2>()).norm()) {
                gcode += this->writer().retract();
                gcode += this->writer().travel_to_z_force(from_z + lift, "lift");
            }
        }

        const std::string comment{"move to first layer point"};

        gcode += insert_gcode();
        gcode += this->writer().travel_to_xy_force(gcode_point.head<2>(), comment);
        gcode += this->writer().travel_to_z_force(gcode_point.z(), comment);

        this->m_avoid_crossing_perimeters.reset_once_modifiers();
        this->last_position = point.head<2>();
        this->writer().update_position(gcode_point);
    }

    this->m_moved_to_first_layer_point = true;
    return gcode;
}

// Orca GCode::calc_max_volumetric_speed: the max volumetric speed fitted to the layer height and line width,
// from six space separated coefficients (none, or all zero: no limit)
static double fitted_max_volumetric_speed(const double layer_height, const double line_width, const std::string &coefficients)
{
    std::vector<double> cs;
    std::istringstream  ss(coefficients);
    for (std::string token; std::getline(ss, token, ' ');)
        try {
            cs.push_back(std::stod(token));
        } catch (...) {}
    if (cs.size() != 6 || std::all_of(cs.begin(), cs.end(), [](double v) { return v == 0; }))
        return std::numeric_limits<double>::max();
    const double x = layer_height, y = line_width;
    return cs[0] * x * x + cs[1] * y * y + cs[2] * x * y + cs[3] * x + cs[4] * y + cs[5];
}

double cap_speed(
    double speed, const Biz::Slicing::ExtrudeConfig &config, int extruder_id, const ExtrusionAttributes &path_attr
) {
    const double general_volumetric_cap{config.max_volumetric_speed.at(extruder_id)};
    if (general_volumetric_cap > 0) {
        speed = std::min(speed, general_volumetric_cap / path_attr.mm3_per_mm);
    }
    const double filament_volumetric_cap{config.filament_max_volumetric_speed.at(extruder_id)};
    if (filament_volumetric_cap > 0) {
        speed = std::min(speed, filament_volumetric_cap / path_attr.mm3_per_mm);
    }
    if (path_attr.role == ExtrusionRole::InternalInfill) {
        const double infill_cap{
            path_attr.maybe_self_crossing ?
                config.filament_infill_max_crossing_speed.at(extruder_id) :
                config.filament_infill_max_speed.at(extruder_id)};
        if (infill_cap > 0) {
            speed = std::min(speed, infill_cap);
        }
    }

    return speed;
}

std::string GCodeGenerator::_extrude(
    const ExtrusionAttributes& path_attr,
    const Geometry::ArcWelder::Path& path,
    const std::string_view description,
    double speed,
    const Biz::Slicing::ExtrudeConfig& config,
    const EmitModifiers& emit_modifiers
)
{
    using Biz::libpgcode::reserved_tag;
    using Biz::libpgcode::Tags;

    std::string gcode;
    const std::string_view description_bridge = path_attr.role.is_bridge() ? " (bridge)"sv : ""sv;

    const bool has_active_instance{m_label_objects.has_active_instance()};
    if (m_writer.multiple_extruders && has_active_instance) {
        gcode += m_label_objects.maybe_change_instance(m_writer);
    }

    if (!this->last_position) {
        const double z = this->m_last_layer_z;
        const std::string comment{"move to print after unknown position"};
        gcode += this->retract_and_wipe(config.retract_speed, config.travel_speed);
        gcode += m_writer.multiple_extruders ? "" : m_label_objects.maybe_change_instance(m_writer);
        gcode += this->m_writer.travel_to_xy(this->point_to_gcode(path.front().point), comment);
        gcode += this->m_writer.travel_to_z_force(z, comment);
    } else if ( this->last_position != path.front().point) {
        std::string comment = "move to first ";
        comment += description;
        comment += description_bridge;
        comment += " point";
        const Vec3crd from{to_3d(*this->last_position, scaled(this->m_last_layer_z))};
        const Vec3crd to{to_3d(path.front().point, scaled(this->m_last_layer_z + (path.front().height_fraction - 1.0) * path_attr.height))};
        const std::string travel_gcode{this->travel_to(from, to, path_attr.role, comment, [this](){
            return m_writer.multiple_extruders ? "" : m_label_objects.maybe_change_instance(m_writer);
        }, config)};
        gcode += travel_gcode;
    }

    // compensate retraction
    gcode += this->unretract();

    if (m_writer.multiple_extruders && !has_active_instance) {
        gcode += m_label_objects.maybe_change_instance(m_writer);
    }

    if (!m_pending_pre_extrusion_gcode.empty()) {
        // There is G-Code that is due to be inserted before an extrusion starts. Insert it.
        gcode += m_pending_pre_extrusion_gcode;
        m_pending_pre_extrusion_gcode.clear();
    }

    const unsigned extruder_id{m_writer.extruder()->id()};

    // adjust acceleration
    unsigned int acceleration_i = 0;
    if (config.default_acceleration.at(extruder_id) > 0) {
        double acceleration;
        if (this->on_first_layer() && config.first_layer_acceleration.at(extruder_id) > 0) {
            acceleration = config.first_layer_acceleration.at(extruder_id);
        } else if (this->object_layer_over_raft() && config.first_layer_acceleration_over_raft.at(extruder_id) > 0) {
            acceleration = config.first_layer_acceleration_over_raft.at(extruder_id);
        } else if (config.bridge_acceleration.at(extruder_id) > 0 && path_attr.role.is_bridge()) {
            acceleration = config.bridge_acceleration.at(extruder_id);
        } else if (config.top_solid_infill_acceleration.at(extruder_id) > 0 && path_attr.role == ExtrusionRole::TopSolidInfill) {
            acceleration = config.top_solid_infill_acceleration.at(extruder_id);
        } else if (config.solid_infill_acceleration.at(extruder_id) > 0 && path_attr.role.is_solid_infill()) {
            acceleration = config.solid_infill_acceleration.at(extruder_id);
        } else if (config.infill_acceleration.at(extruder_id) > 0 && path_attr.role.is_infill()) {
            acceleration = config.infill_acceleration.at(extruder_id);
        } else if (config.external_perimeter_acceleration.at(extruder_id) > 0 && path_attr.role.is_external_perimeter()) {
            acceleration = config.external_perimeter_acceleration.at(extruder_id);
        } else if (config.perimeter_acceleration.at(extruder_id) > 0 && path_attr.role.is_perimeter()) {
            acceleration = config.perimeter_acceleration.at(extruder_id);
        } else {
            acceleration = config.default_acceleration.at(extruder_id);
        }
        acceleration_i = (unsigned int)floor(acceleration + 0.5);
    }
    // Orca: XY jerk by feature
    double jerk = 0.;
    if (config.default_jerk > 0.) {
        if (this->on_first_layer() && config.initial_layer_jerk > 0.)
            jerk = config.initial_layer_jerk;
        else if (config.outer_wall_jerk > 0. && path_attr.role.is_external_perimeter())
            jerk = config.outer_wall_jerk;
        else if (config.inner_wall_jerk > 0. && path_attr.role.is_perimeter())
            jerk = config.inner_wall_jerk;
        else if (config.top_surface_jerk > 0. && path_attr.role == ExtrusionRole::TopSolidInfill)
            jerk = config.top_surface_jerk;
        else if (config.infill_jerk > 0. && path_attr.role.is_infill())
            jerk = config.infill_jerk;
        else
            jerk = config.default_jerk;
    }
    if (m_writer.config.gcode_flavor == GCodeFlavor::gcfKlipper && (jerk > 0. || m_writer.klipper_velocity_limit())) {
        gcode += m_writer.set_accel_and_jerk(acceleration_i, jerk);
    } else {
        gcode += m_writer.set_print_acceleration(acceleration_i);
        gcode += m_writer.set_jerk_xy(jerk);
    }

    // calculate extrusion length per distance unit
    double e_per_mm = m_writer.extruder()->e_per_mm3() * path_attr.mm3_per_mm;
    {
        // Orca (GCode::_extrude): print flow ratio and the extrusion role's flow ratio (the
        // filament's flow ratio, 3.0's extrusion_multiplier, is in e_per_mm3 already)
        double flow_ratio = config.print_flow_ratio;
        if (path_attr.role == ExtrusionRole::TopSolidInfill)
            flow_ratio *= config.top_solid_infill_flow_ratio;
        else if (path_attr.role == ExtrusionRole::InternalBridgeInfill)
            flow_ratio *= config.internal_bridge_flow;
        else if (path_attr.role == ExtrusionRole::Brim)
            flow_ratio *= config.brim_flow_ratio;
        else if (std::any_of(path.begin(), path.end(), [](const auto& point) { return point.height_fraction < 1.f; }))
            flow_ratio *= config.scarf_joint_flow_ratio;
        if (config.set_other_flow_ratios) {
            if (path_attr.role == ExtrusionRole::ExternalPerimeter)
                flow_ratio *= config.outer_wall_flow_ratio;
            else if (path_attr.role == ExtrusionRole::Perimeter)
                flow_ratio *= config.inner_wall_flow_ratio;
            else if (path_attr.role == ExtrusionRole::OverhangPerimeter)
                flow_ratio *= config.overhang_flow_ratio;
            else if (path_attr.role == ExtrusionRole::InternalInfill)
                flow_ratio *= config.sparse_infill_flow_ratio;
            else if (path_attr.role == ExtrusionRole::SolidInfill)
                flow_ratio *= config.internal_solid_infill_flow_ratio;
            else if (path_attr.role == ExtrusionRole::GapFill)
                flow_ratio *= config.gap_fill_flow_ratio;
            else if (path_attr.role == ExtrusionRole::SupportMaterial)
                flow_ratio *= config.support_flow_ratio;
            else if (path_attr.role == ExtrusionRole::SupportMaterialInterface)
                flow_ratio *= config.support_interface_flow_ratio;
            // the first layer (except brims and skirts)
            if (this->on_first_layer() && path_attr.role != ExtrusionRole::Skirt && path_attr.role != ExtrusionRole::Brim)
                flow_ratio *= config.first_layer_flow_ratio;
        }
        e_per_mm *= flow_ratio;
    }
    if (m_writer.extrusion_axis().empty())
        // gcfNoExtrusion
        e_per_mm = 0;

    // Orca _needSAFC: the small area flow compensation of solid infill in the rectilinear and monotonic patterns
    const bool small_area_flow_compensation = m_small_area_infill_flow_compensator && [&]() {
        const auto supported = [](const Domain::InfillPattern pattern) {
            return pattern == Domain::InfillPattern::ipRectilinear || pattern == Domain::InfillPattern::ipAlignedRectilinear ||
                pattern == Domain::InfillPattern::ipMonotonic || pattern == Domain::InfillPattern::ipMonotonicLines;
        };
        if (path_attr.role != ExtrusionRole::SolidInfill && path_attr.role != ExtrusionRole::TopSolidInfill)
            return false;
        return (this->on_first_layer() && supported(config.bottom_fill_pattern)) ||
            (path_attr.role == ExtrusionRole::SolidInfill && supported(config.internal_solid_infill_pattern)) ||
            (path_attr.role == ExtrusionRole::TopSolidInfill && supported(config.top_fill_pattern));
    }();

    using Domain::FloatOrPercentage;
    const auto perimeter_speed = config.perimeter_speed.at(extruder_id);
    const auto infill_speed = config.infill_speed.at(extruder_id);
    const auto solid_infill_speed = config.solid_infill_speed
                                        .at(extruder_id)
                                        .get_abs_value(infill_speed);
    // set speed
    if (speed == -1) {
        if (path_attr.role == ExtrusionRole::Perimeter) {
            speed = perimeter_speed;
        } else if (path_attr.role == ExtrusionRole::ExternalPerimeter) {
            speed = config.external_perimeter_speed.at(extruder_id).get_abs_value(perimeter_speed);
        } else if (path_attr.role == ExtrusionRole::InternalBridgeInfill) {
            // Orca internal_bridge_speed, a percentage of the bridge speed
            speed = config.internal_bridge_speed.at(extruder_id).get_abs_value(config.bridge_speed.at(extruder_id));
        } else if (path_attr.role.is_bridge()) {
            assert(path_attr.role.is_perimeter() || path_attr.role == ExtrusionRole::BridgeInfill);
            speed = config.bridge_speed.at(extruder_id);
        } else if (path_attr.role == ExtrusionRole::InternalInfill) {
            speed = infill_speed;
        } else if (path_attr.role == ExtrusionRole::SolidInfill) {
            speed = solid_infill_speed;
        } else if (path_attr.role == ExtrusionRole::InfillOverBridge) {
            const double over_bridge_speed{config.over_bridge_speed.at(extruder_id).get_abs_value(solid_infill_speed)};
            if (over_bridge_speed > 0) {
                speed = over_bridge_speed;
            } else {
                speed = solid_infill_speed;
            }
        } else if (path_attr.role == ExtrusionRole::TopSolidInfill) {
            speed = config.top_solid_infill_speed.at(extruder_id).get_abs_value(solid_infill_speed);
        } else if (path_attr.role == ExtrusionRole::Ironing) {
            speed = config.ironing_speed;
        } else if (path_attr.role == ExtrusionRole::GapFill) {
            speed = config.gap_fill_speed.at(extruder_id);
        } else {
            throw Slic3r::InvalidArgument("Invalid speed");
        }
    }
    if (m_volumetric_speed.at(extruder_id) != 0. && speed == 0) {
        double volumetric_speed = m_volumetric_speed.at(extruder_id);
        if (config.filament_adaptive_volumetric_speed.at(extruder_id))
            volumetric_speed = std::min(volumetric_speed, fitted_max_volumetric_speed(
                path_attr.height, path_attr.width, config.volumetric_speed_coefficients.at(extruder_id)));
        speed = volumetric_speed / path_attr.mm3_per_mm;
    }
    if (this->on_first_layer()) {
        if (path_attr.role == ExtrusionRole::InternalInfill) {
            speed = config.first_layer_infill_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::SolidInfill) {
            speed = config.first_layer_solid_infill_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::TopSolidInfill) {
            speed = config.first_layer_top_solid_infill_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::Perimeter) {
            speed = config.first_layer_perimeter_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::ExternalPerimeter) {
            speed = config.first_layer_external_perimeter_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::SupportMaterial) {
            speed = config.first_layer_support_material_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::GapFill) {
            speed = config.first_layer_gap_fill_speed.at(extruder_id).float_value();
        } else if (path_attr.role == ExtrusionRole::Skirt || path_attr.role == ExtrusionRole::Brim) {
            speed = config.first_layer_support_material_speed.at(extruder_id).float_value();
        } else {
            SPDLOG_ERROR("Invalid speed on first layer, using first layer perimeter speed");
            speed = config.first_layer_perimeter_speed.at(extruder_id).float_value();
        }
    }
    else if (this->object_layer_over_raft())
        speed = config.first_layer_speed_over_raft.at(extruder_id).get_abs_value(speed);

    // Orca: the first slow_down_layers layers (counted from the first object layer above a raft)
    // ramp linearly from the first layer speed to the layer's speed
    if (!this->on_first_layer() && !this->object_layer_over_raft() && config.slow_down_layers > 1
        && m_layer != nullptr) {
        const int raft_layers = int(m_layer->object()->slicing_parameters().raft_layers());
        const int layer       = int(m_layer->id()) - raft_layers;
        if (layer > 0 && layer < config.slow_down_layers) {
            const double first_layer_speed = path_attr.role.is_perimeter() ?
                config.first_layer_perimeter_speed.at(extruder_id).float_value() :
                config.first_layer_infill_speed.at(extruder_id).float_value();
            if (first_layer_speed < speed)
                speed = std::min(speed, first_layer_speed + (speed - first_layer_speed) * double(layer) / config.slow_down_layers);
        }
    }

    // Orca: the scarf joint (the sloped part of a seam) at most at scarf_joint_speed (a percentage of the
    // wall's speed)
    const bool sloped = std::any_of(path.begin(), path.end(), [](const auto& point) { return point.height_fraction < 1.f; });
    if (sloped && path_attr.role.is_perimeter())
        speed = std::min(speed, config.scarf_joint_speed.get_abs_value(speed));

    // Orca: override skirt speed if set
    if (path_attr.role == ExtrusionRole::Skirt && config.skirt_speed > 0.)
        speed = config.skirt_speed;

    ExtrusionProcessor::OverhangSpeeds dynamic_print_and_fan_speeds = {-1.f, -1.f};
    if (path_attr.overhang_attributes.has_value()) {
        double external_perimeter_reference_speed =
            config.external_perimeter_speed
                .at(extruder_id)
                .get_abs_value(perimeter_speed);
        if (external_perimeter_reference_speed == 0) {
            external_perimeter_reference_speed = m_volumetric_speed.at(extruder_id) / path_attr.mm3_per_mm;
        }

        external_perimeter_reference_speed = cap_speed(external_perimeter_reference_speed, config, m_writer.extruder()->id(), path_attr);
        dynamic_print_and_fan_speeds       = ExtrusionProcessor::calculate_overhang_speed(path_attr, config, m_writer.extruder()->id(),
                                                                                    float(external_perimeter_reference_speed), float(speed),
                                                                                    m_current_dynamic_fan_speed);
    }

    if (dynamic_print_and_fan_speeds.print_speed > -1) {
        speed = dynamic_print_and_fan_speeds.print_speed;
    }

    // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
    speed = cap_speed(speed, config, m_writer.extruder()->id(), path_attr);

    double F = speed * 60;  // convert mm/sec to mm/min

    const bool apa_role_change = extrusion_role_to_gcode_extrusion_role(path_attr.role) != m_last_processor_extrusion_role;

    // extrude arc or line
    if (m_enable_extrusion_role_markers) {
        if (GCodeExtrusionRole role = extrusion_role_to_gcode_extrusion_role(path_attr.role); role != m_last_extrusion_role) {
            m_last_extrusion_role = role;
            if (m_enable_extrusion_role_markers)
            {
                char buf[32];
                sprintf(buf, ";_EXTRUSION_ROLE:%d\n", int(m_last_extrusion_role));
                gcode += buf;
            }
        }
    }

    // Orca: custom G-code on an extrusion role change (printer, filament, process)
    if (GCodeExtrusionRole role = extrusion_role_to_gcode_extrusion_role(path_attr.role);
        role != m_last_processor_extrusion_role) {
        const unsigned int extruder_id_now = m_writer.extruder()->id();
        const std::string& machine_role_change_gcode =
            m_print->config().get<std::string>("change_extrusion_role_gcode");
        const auto filament_role_change_gcodes =
            m_print->config().get<std::vector<std::string>>("filament_change_extrusion_role_gcode");
        const std::string filament_role_change_gcode = extruder_id_now < filament_role_change_gcodes.size() ?
            filament_role_change_gcodes[extruder_id_now] : std::string{};
        const std::string& process_role_change_gcode =
            m_print->config().get<std::string>("process_change_extrusion_role_gcode");
        if (!machine_role_change_gcode.empty() || !filament_role_change_gcode.empty()
            || !process_role_change_gcode.empty()) {
            ParserConfig cfg;
            cfg.set("extrusion_role", gcode_extrusion_role_to_string_for_parser(role));
            cfg.set("last_extrusion_role", gcode_extrusion_role_to_string_for_parser(m_last_processor_extrusion_role));
            cfg.set("layer_num", m_layer_index + 1);
            cfg.set("layer_z", double(m_layer == nullptr ? m_last_height : m_layer->print_z));
            const auto append_role_gcode = [this, extruder_id_now, &cfg, &gcode](const std::string& key, const std::string& templ)
            {
                if (templ.empty())
                    return;
                gcode += this->placeholder_parser_process(key, templ, extruder_id_now, &cfg) + "\n";
            };
            append_role_gcode("change_extrusion_role_gcode", machine_role_change_gcode);
            append_role_gcode("filament_change_extrusion_role_gcode", filament_role_change_gcode);
            append_role_gcode("process_change_extrusion_role_gcode", process_role_change_gcode);
        }
    }

    // adds processor tags and updates processor tracking data
    // PrusaMultiMaterial::Writer may generate GCodeProcessor::Height_Tag lines without updating m_last_height
    // so, if the last role was GCodeExtrusionRole::WipeTower we force export of GCodeProcessor::Height_Tag lines
    bool last_was_wipe_tower = (m_last_processor_extrusion_role == GCodeExtrusionRole::WipeTower);
    assert(is_decimal_separator_point());

    if (GCodeExtrusionRole role = extrusion_role_to_gcode_extrusion_role(path_attr.role); role != m_last_processor_extrusion_role) {
        m_last_processor_extrusion_role = role;
        char buf[64];
        sprintf(buf, ";%s%s\n", reserved_tag(Tags::Role).data(), gcode_extrusion_role_to_string(m_last_processor_extrusion_role).c_str());
        gcode += buf;
    }

    if (last_was_wipe_tower || m_last_width != path_attr.width) {
        m_last_width = path_attr.width;
        gcode += std::string(";") + std::string(reserved_tag(Tags::Width))
               + float_to_string_decimal_point(m_last_width) + "\n";
    }

    if (last_was_wipe_tower || std::abs(m_last_height - path_attr.height) > EPSILON) {
        m_last_height = path_attr.height;
        gcode += std::string(";") + std::string(reserved_tag(Tags::Height)) + float_to_string_decimal_point(m_last_height) + "\n";
    }

    std::string cooling_marker_setspeed_comments;
    const bool  internal_bridge_own_fan = path_attr.role == ExtrusionRole::InternalBridgeInfill &&
        m_print->config().get<std::vector<int>>("internal_bridge_fan_speed").at(extruder_id) >= 0;
    if (m_enable_cooling_markers) {
        // Orca (append_role_based_fan_marker): a START when a role with its own fan speed begins (again on
        // every layer, the CoolingBuffer works per layer), an END when another role follows
        auto role_fan_marker = [this, &gcode](size_t idx, const char* prefix, bool on) {
            if (on) {
                if (!m_role_fan_on[idx] || m_role_fan_layer[idx] != m_layer_index) {
                    gcode += std::string(";") + prefix + "_FAN_START\n";
                    m_role_fan_on[idx]    = true;
                    m_role_fan_layer[idx] = m_layer_index;
                }
            } else if (m_role_fan_on[idx]) {
                gcode += std::string(";") + prefix + "_FAN_END\n";
                m_role_fan_on[idx]    = false;
                m_role_fan_layer[idx] = -1;
            }
        };
        role_fan_marker(0, "_SUPP_INTERFACE", config.support_material_interface_fan_speed.at(extruder_id) >= 0
                                                  && path_attr.role == ExtrusionRole::SupportMaterialInterface);
        role_fan_marker(1, "_IRONING", config.ironing_fan_speed.at(extruder_id) >= 0
                                           && path_attr.role == ExtrusionRole::Ironing);
        // Orca internal_bridge_fan_speed (-1: the bridge fan)
        role_fan_marker(2, "_INTERNAL_BRIDGE", internal_bridge_own_fan);

        if (path_attr.role.is_bridge() && !internal_bridge_own_fan && emit_modifiers.emit_bridge_fan_start) {
            gcode += ";_BRIDGE_FAN_START\n";
        } else if (!path_attr.role.is_bridge()) {
            cooling_marker_setspeed_comments = ";_EXTRUDE_SET_SPEED";
        }

        if (path_attr.role.is_external_perimeter()) {
            cooling_marker_setspeed_comments += ";_EXTERNAL_PERIMETER";
        } else if (path_attr.role.is_perimeter()) {
            assert(path_attr.perimeter_index.has_value());
            if (path_attr.perimeter_index.has_value()) {
                cooling_marker_setspeed_comments += ";_INTERNAL_PERIMETER" + std::to_string(*path_attr.perimeter_index);
            }
        }
    }

    // Orca adaptive pressure advance: a PA_CHANGE tag for the AdaptivePAProcessor at the start of a smooth
    // path or a role change, and within features (adaptive_pressure_advance_overhangs) where the speed or the
    // flow changes: slowing down takes the overhang rule (the lower speed), speeding up the feature's highest
    if (m_pa_processor && m_adaptive_pa.at(extruder_id)) {
        const auto pa_change_tag = [&](double mm3_per_mm, int role_change, int overhang) {
            char buf[160];
            sprintf(buf, ";%sT%u MM3MM:%g ACCEL:%u BR:%d RC:%d OV:%d\n", reserved_tag(Tags::PA_Change).data(),
                    extruder_id, mm3_per_mm, acceleration_i, int(path_attr.role.is_bridge()), role_change, overhang);
            gcode += buf;
        };
        const bool overhang_perimeter = path_attr.role == ExtrusionRole::OverhangPerimeter;
        if (!m_apa_path_set || apa_role_change) {
            if (m_apa_average_mm3_per_mm > 0.)
                pa_change_tag(m_apa_average_mm3_per_mm, int(apa_role_change), int(overhang_perimeter));
            else if (path_attr.mm3_per_mm > 0.)
                pa_change_tag(path_attr.mm3_per_mm, int(apa_role_change), int(overhang_perimeter));
        } else if (m_adaptive_pa_overhangs.at(extruder_id) && path_attr.mm3_per_mm > 0. &&
                   (std::abs(m_writer.current_speed() - F) > EPSILON ||
                    std::abs(path_attr.mm3_per_mm - m_apa_last_mm3_per_mm) > EPSILON)) {
            pa_change_tag(path_attr.mm3_per_mm, 1, m_writer.current_speed() > F ? 1 : 0);
            m_apa_last_mm3_per_mm = path_attr.mm3_per_mm;
        }
    }

    // F is mm per minute.
    gcode += m_writer.set_speed(F, "", cooling_marker_setspeed_comments);

    if (dynamic_print_and_fan_speeds.fan_speed >= 0) {
        const int fan_speed = int(dynamic_print_and_fan_speeds.fan_speed);
        if (!m_current_dynamic_fan_speed.has_value() || (m_current_dynamic_fan_speed.has_value() && m_current_dynamic_fan_speed != fan_speed)) {
            m_current_dynamic_fan_speed = fan_speed;
            gcode += ";_SET_FAN_SPEED" + std::to_string(fan_speed) + "\n";
        }
    } else if (m_current_dynamic_fan_speed.has_value() && dynamic_print_and_fan_speeds.fan_speed < 0) {
        m_current_dynamic_fan_speed.reset();
        gcode += ";_RESET_FAN_SPEED\n";
    }

    std::string comment;
    if (config.gcode_comments) {
        comment = description;
        comment += description_bridge;
    }
    Vec2d prev_exact = this->point_to_gcode(path.front().point);
    Vec2d prev = GCodeFormatter::quantize(prev_exact);
    auto  it   = path.begin();
    auto  end  = path.end();
    for (++ it; it != end; ++ it) {
        Vec2d p_exact = this->point_to_gcode(it->point);
        Vec2d p = GCodeFormatter::quantize(p_exact);
        //assert(p != prev);
        if (p != prev) {
            // Center of the radius to be emitted into the G-code: Either by radius or by center offset.
            double radius = 0;
            Vec2d  ij;
            if (it->radius != 0) {
                // Extrude an arc.
                assert(config.arc_fitting == Domain::ArcFittingType::EmitCenter);
                radius = unscaled<double>(it->radius);
                {
                    // Calculate quantized IJ circle center offset.
                    ij = GCodeFormatter::quantize(Vec2d(
                            Geometry::ArcWelder::arc_center(prev_exact.cast<double>(), p_exact.cast<double>(), double(radius), it->ccw())
                            - prev));
                    if (ij == Vec2d::Zero())
                        // Don't extrude a degenerated circle.
                        radius = 0;
                }
            }
            if (radius == 0) {
                // Extrude line segment.
                if (const double line_length = (p - prev).norm(); line_length > 0) {
                    double extrusion_amount{e_per_mm * line_length * it->e_fraction};
                    if (small_area_flow_compensation)
                        extrusion_amount = m_small_area_infill_flow_compensator->modify_flow(line_length, extrusion_amount);
                    if (it->height_fraction < 1.0 || std::prev(it)->height_fraction < 1.0) {
                        const Vec3d destination{to_3d(p, this->m_last_layer_z + (it->height_fraction - 1) * m_last_height)};
                        gcode += m_writer.extrude_to_xyz(destination, extrusion_amount);
                    } else {
                        gcode += m_writer.extrude_to_xy(p, extrusion_amount, comment);
                    }
                }
            } else {
                double angle = Geometry::ArcWelder::arc_angle(prev.cast<double>(), p.cast<double>(), double(radius));
                assert(angle > 0);
                const double line_length = angle * std::abs(radius);
                double       dE          = e_per_mm * line_length;
                if (small_area_flow_compensation)
                    dE = m_small_area_infill_flow_compensator->modify_flow(line_length, dE);
                assert(dE > 0);
                gcode += m_writer.extrude_to_xy_G2G3IJ(p, ij, it->ccw(), dE, comment);
            }
            prev = p;
            prev_exact = p_exact;
        }
    }

    if (m_enable_cooling_markers) {
        if (path_attr.role.is_bridge() && !internal_bridge_own_fan && emit_modifiers.emit_bridge_fan_end) {
            gcode += ";_BRIDGE_FAN_END\n";
        } else if (!path_attr.role.is_bridge()) {
            gcode += ";_EXTRUDE_END\n";
        }
    }

    if (path_attr.role != ExtrusionRole::GapFill)
        m_last_notgapfill_role = path_attr.role;

    if (m_current_dynamic_fan_speed.has_value() && emit_modifiers.emit_fan_speed_reset) {
        m_current_dynamic_fan_speed.reset();
        gcode += ";_RESET_FAN_SPEED\n";
    }

    this->last_position = path.back().point;
    return gcode;
}

std::string GCodeGenerator::generate_travel_gcode(
    const Points3& travel,
    const std::string& comment,
    const std::function<std::string()>& insert_gcode,
    const Biz::Slicing::ExtrudeConfig& config,
    const EnforceFirstZ enforce_first_z,
    const std::function<bool()>& use_short_distance_acceleration
) {
    if (travel.empty()) {
        return "";
    }

    unsigned       travel_acceleration                = static_cast<unsigned>(config.travel_acceleration + 0.5);
    const unsigned travel_short_distance_acceleration = static_cast<unsigned>(config.travel_short_distance_acceleration + 0.5);
    // Orca: the first layer's travel acceleration and jerk
    double travel_jerk = 0.;
    if (this->on_first_layer()) {
        const double first_layer = config.initial_layer_travel_acceleration.get_abs_value(config.travel_acceleration);
        if (config.travel_acceleration > 0. && first_layer > 0.)
            travel_acceleration = static_cast<unsigned>(first_layer + 0.5);
        if (config.default_jerk > 0.)
            travel_jerk = config.initial_layer_travel_jerk.get_abs_value(config.travel_jerk);
    } else if (config.default_jerk > 0.) {
        travel_jerk = use_short_distance_acceleration() && config.outer_wall_jerk > 0. ? config.outer_wall_jerk : config.travel_jerk;
    }

    std::string gcode;
    // Generate G-code for the travel move.
    // Use G1 because we rely on paths being straight (G0 may make round paths).
    const unsigned first_acceleration = use_short_distance_acceleration() ? travel_short_distance_acceleration : travel_acceleration;
    if (m_writer.config.gcode_flavor == GCodeFlavor::gcfKlipper && (travel_jerk > 0. || m_writer.klipper_velocity_limit())) {
        gcode += this->m_writer.set_accel_and_jerk(first_acceleration, travel_jerk);
    } else {
        gcode += this->m_writer.set_travel_acceleration(first_acceleration);
        gcode += this->m_writer.set_jerk_xy(travel_jerk);
    }

    bool already_inserted{false};
    for (std::size_t i{0}; i < travel.size(); ++i) {
        const Vec3crd& point{travel[i]};
        const Vec3d gcode_point{this->point_to_gcode(point)};

        if (travel.size() - i <= 2 && !already_inserted) {
            gcode += insert_gcode();
            already_inserted = true;
        }

        if (enforce_first_z == EnforceFirstZ::True && i == 0) {
            if (
                std::abs(gcode_point.x() - m_writer.get_position().x()) < GCodeFormatter::XYZ_EPSILON
                && std::abs(gcode_point.y() - m_writer.get_position().y()) < GCodeFormatter::XYZ_EPSILON
            ) {
                gcode += this->m_writer.travel_to_z_force(gcode_point.z(), comment);
            } else {
                gcode += this->m_writer.travel_to_xyz_force(gcode_point, comment);
            }
        } else {
            gcode += this->m_writer.travel_to_xyz(gcode_point, comment);
        }

        this->last_position = point.head<2>();
    }

    // This is mainly for parts of the G-code export that don't take into account that travel acceleration could change during printing.
    // Those parts of the G-code export always use the travel acceleration that was set last.
    if (use_short_distance_acceleration() && travel_short_distance_acceleration != travel_acceleration) {
        gcode += this->m_writer.set_travel_acceleration(travel_acceleration);
    }

    if (!this->m_writer.supports_separate_travel_acceleration()) {
        // In case that this flavor does not support separate print and travel acceleration,
        // reset acceleration to default.
        // TODO: This doesn't seem to perform what the comment describes.
        gcode += this->m_writer.set_travel_acceleration(travel_acceleration);
    }

    return gcode;
}

bool GCodeGenerator::needs_retraction(
    const Polyline& travel, const Biz::Slicing::ExtrudeConfig& config, ExtrusionRole role
)
{
    const unsigned extruder_id{m_writer.extruder()->id()};

    if (! m_writer.extruder() || travel.length() < scale_(config.retract_before_travel.at(m_writer.extruder()->id()))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }

    if (role == ExtrusionRole::SupportMaterial)
        if (const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(m_layer);
            support_layer != nullptr && ! support_layer->support_islands_bboxes.empty()) {
            BoundingBox bbox_travel = get_extents(travel);
            Polylines   trimmed;
            bool        trimmed_initialized = false;
            for (const BoundingBox &bbox : support_layer->support_islands_bboxes)
                if (bbox.overlap(bbox_travel)) {
                    const auto &island = support_layer->support_islands[&bbox - support_layer->support_islands_bboxes.data()];
                    trimmed = trimmed_initialized ? diff_pl(trimmed, island) : diff_pl(travel, island);
                    trimmed_initialized = true;
                    if (trimmed.empty())
                        // skip retraction if this is a travel move inside a support material island
                        //FIXME not retracting over a long path may cause oozing, which in turn may result in missing material
                        // at the end of the extrusion path!
                        return false;
                    // Not sure whether updating the boudning box isn't too expensive.
                    //bbox_travel = get_extents(trimmed);
                }
        }

    if (config.only_retract_when_crossing_perimeters
        && m_layer != nullptr
        && config.fill_density.at(extruder_id)
            > Domain::Percentage{0}
        && m_retract_when_crossing_perimeters.travel_inside_internal_regions(*m_layer, travel))
        // Skip retraction if travel is contained in an internal slice *and*
        // internal infill is enabled (so that stringing is entirely not visible).
        //FIXME any_internal_region_slice_contains() is potentionally very slow, it shall test for the bounding boxes first.
        return false;

    // retract if only_retract_when_crossing_perimeters is disabled or doesn't apply
    return true;
}

Polyline GCodeGenerator::generate_travel_xy_path(
    const Point& start_point,
    const Point& end_point,
    const bool needs_retraction,
    const Biz::Slicing::ExtrudeConfig& config,
    bool& could_be_wipe_disabled
) {

    const Point scaled_origin{scaled(this->origin())};
    const bool avoid_crossing_perimeters = (
        config.avoid_crossing_perimeters
        && !this->m_avoid_crossing_perimeters.disabled_once()
    );

    Polyline xy_path{start_point, end_point};
    if (config.avoid_crossing_curled_overhangs) {
        if (avoid_crossing_perimeters) {
            SPDLOG_WARN("Option >avoid crossing curled overhangs< is not compatible with avoid crossing perimeters and it will be ignored!");
        } else {
            xy_path = this->m_avoid_crossing_curled_overhangs.find_path(
                start_point + scaled_origin,
                end_point + scaled_origin
            );
            xy_path.translate(-scaled_origin);
        }
    }


    // if a retraction would be needed, try to use avoid_crossing_perimeters to plan a
    // multi-hop travel path inside the configuration space
    if (
        needs_retraction
        && avoid_crossing_perimeters
    ) {
        xy_path = this->m_avoid_crossing_perimeters.travel_to(
            *this,
            end_point,
            config.avoid_crossing_perimeters_max_detour,
            config.travel_max_lift,
            &could_be_wipe_disabled
        );
        Algorithms::Polyline::simplify(xy_path, this->m_scaled_resolution);
    }

    return xy_path;
}

// This method accepts &point in print coordinates.
std::string GCodeGenerator::travel_to(
    const Vec3crd &start_point,
    const Vec3crd &end_point,
    ExtrusionRole role,
    const std::string &comment,
    const std::function<std::string()>& insert_gcode,
    const Biz::Slicing::ExtrudeConfig& config,
    const GCodeGenerator::EnforceFirstZ enforce_first_z
) {
    const double initial_elevation{unscaled(start_point.z())};

    // check whether a straight travel move would need retraction

    bool could_be_wipe_disabled {false};
    bool needs_retraction = this->needs_retraction(Polyline{start_point.head<2>(), end_point.head<2>()}, config, role);

    Polyline xy_path{generate_travel_xy_path(
        start_point.head<2>(), end_point.head<2>(), needs_retraction, config, could_be_wipe_disabled
    )};

    needs_retraction = this->needs_retraction(xy_path, config, role);

    std::string wipe_retract_gcode{};
    if (needs_retraction) {
        if (could_be_wipe_disabled) {
            m_wipe.reset_path();
        }

        Point position_before_wipe{*this->last_position};
        wipe_retract_gcode = this->retract_and_wipe(config.retract_speed, config.travel_speed);

        if (*this->last_position != position_before_wipe) {
            xy_path = generate_travel_xy_path(
                *this->last_position, end_point.head<2>(), needs_retraction, config, could_be_wipe_disabled
            );
        }
    } else {
        m_wipe.reset_path();
    }

    this->m_avoid_crossing_perimeters.reset_once_modifiers();

    const unsigned extruder_id = this->m_writer.extruder()->id();
    const double retract_length = config.retract_length.at(extruder_id);
    bool can_be_flat{!needs_retraction || retract_length == 0};

    const double upper_limit = config.retract_lift_below.at(extruder_id);
    const double lower_limit = config.retract_lift_above.at(extruder_id);
    if ((lower_limit > 0 && initial_elevation < lower_limit) ||
        (upper_limit > 0 && initial_elevation > upper_limit) ||
        !this->retract_lift_allowed(config, extruder_id)) {
        can_be_flat = true;
    }

    Points3 travel = (
        can_be_flat ?
        GCode::Impl::Travels::generate_flat_travel(xy_path.points, initial_elevation) :
        GCode::Impl::Travels::generate_travel_to_extrusion(
            xy_path,
            config,
            extruder_id,
            initial_elevation,
            m_travel_obstacle_tracker,
            scaled(m_origin)
        )
    );
    if (config.scarf_seam_placement != Domain::ScarfSeamPlacement::nowhere &&
        role == ExtrusionRole::ExternalPerimeter && can_be_flat && travel.size() == 2 &&
        scaled(2.0) > xy_path.length()) {

        // Go directly to the outter perimeter.
        travel.pop_back();
    }
    travel.emplace_back(end_point);

    // Short distance travel acceleration must not be applied when the travel acceleration control
    // is disabled (travel_acceleration is zero). Emitting zero acceleration is a no-op, so there
    // would be no way to return from the short distance travel acceleration back to the default one.
    if (config.travel_acceleration > 0. && config.travel_short_distance_acceleration > 0.) {
        return wipe_retract_gcode + generate_travel_gcode(travel, comment, insert_gcode, config, enforce_first_z, [&]() {
                   return role.is_external_perimeter() && xy_path.length() < scaled<double>(config.retract_before_travel.at(m_writer.extruder()->id()));
               });
    }

    return wipe_retract_gcode + generate_travel_gcode(travel, comment, insert_gcode, config, enforce_first_z);
}

std::string GCodeGenerator::retract_and_wipe(
    const std::vector<double>& retract_speed,
    double travel_speed,
    bool toolchange,
    bool reset_e
)
{
    std::string gcode;

    if (m_writer.extruder() == nullptr)
        return gcode;

    // wipe (if it's enabled for this extruder and we have a stored wipe path)
    if (m_wipe_enabled.at(m_writer.extruder()->id()) && m_wipe.has_path()) {
        gcode += toolchange ? m_writer.retract_for_toolchange(true) : m_writer.retract(true);
        gcode += m_wipe.wipe(*this, retract_speed, travel_speed, toolchange);
    }

    /*  The parent class will decide whether we need to perform an actual retraction
        (the extruder might be already retracted fully or partially). We call these
        methods even if we performed wipe, since this will ensure the entire retraction
        length is honored in case wipe path was too short.  */
    gcode += toolchange ? m_writer.retract_for_toolchange() : m_writer.retract();

    if (reset_e) {
        gcode += m_writer.reset_e();
    }

    return gcode;
}

void GCodeGenerator::set_current_extruder_placeholders(unsigned int extruder_id, const Domain::ConfigView& config)
{
    // Orca: the current filament and its extruder / nozzle / hotend (3.0: the material slot and its tool)
    const int tool = int(config.hw_config().tool_of_material_slot(extruder_id));
    this->placeholder_parser().set("current_extruder", int(extruder_id));
    this->placeholder_parser().set("current_filament_id", int(extruder_id));
    this->placeholder_parser().set("current_extruder_id", tool);
    this->placeholder_parser().set("current_nozzle_id", tool);
    this->placeholder_parser().set("current_hotend", tool);
}

std::string GCodeGenerator::set_extruder(unsigned int extruder_id, double print_z, const Domain::ConfigView& config)
{
    if (!m_writer.need_toolchange(extruder_id))
        return "";

    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!m_writer.multiple_extruders) {
        this->set_current_extruder_placeholders(extruder_id, config);

        std::string gcode;
        // Append the filament start G-code.
        const std::string start_filament_gcode = config.get<std::vector<std::string>>("start_filament_gcode").at(extruder_id);
        if (! start_filament_gcode.empty()) {
            // Process the start_filament_gcode for the filament.
            ParserConfig dynamic_config;
            dynamic_config.set("layer_num", m_layer_index);
            dynamic_config.set("layer_z", this->writer().get_position().z() - config.get<double>("z_offset"));
            dynamic_config.set("max_layer_z", double{m_max_layer_z});
            dynamic_config.set("filament_extruder_id", int(extruder_id));
            gcode += this->placeholder_parser_process("start_filament_gcode", start_filament_gcode, extruder_id, &dynamic_config);
            check_add_eol(gcode);
        }

        const PressureAdvance pressure_advance =
            config.get<std::vector<PressureAdvance>>("pressure_advance").at(extruder_id);
        if (pressure_advance != PressureAdvance::Disabled) {
            gcode += m_writer.set_pressure_advance(
                config.get<std::vector<double>>("pressure_advance_value").at(extruder_id),
                this->m_print->config().hw_config().vendor_id
            );

            if (pressure_advance == PressureAdvance::AutomaticCalibration) {
                gcode += m_writer.emit_automatic_pressure_advance_calibration();
            }
        }

        gcode += m_writer.toolchange(extruder_id);
        return gcode;
    }

    std::string gcode{};
    if (!config.get<bool>("complete_objects")) {
        gcode += this->m_label_objects.maybe_stop_instance();
    }

    const std::vector<double> retract_speed{config.get<std::vector<double>>("retract_speed")};
    const double travel_speed{config.get<double>("travel_speed")};
    // prepend retraction on the current extruder
    gcode += this->retract_and_wipe(retract_speed, travel_speed, true);

    // Always reset the extrusion path, even if the tool change retract is set to zero.
    m_wipe.reset_path();

    if (m_writer.extruder() != nullptr) {
        // Process the custom end_filament_gcode.
        unsigned int        old_extruder_id     = m_writer.extruder()->id();
        const std::string  end_filament_gcode  = config.get<std::vector<std::string>>("end_filament_gcode").at(old_extruder_id);
        if (! end_filament_gcode.empty()) {
            ParserConfig dynamic_config;
            dynamic_config.set("layer_num", m_layer_index);
            dynamic_config.set("layer_z", m_writer.get_position().z() - config.get<double>("z_offset"));
            dynamic_config.set("max_layer_z", double{m_max_layer_z});
            dynamic_config.set("filament_extruder_id", int(old_extruder_id));
            gcode += placeholder_parser_process("end_filament_gcode", end_filament_gcode, old_extruder_id, &dynamic_config);
            check_add_eol(gcode);
        }
    }


    // If ooze prevention is enabled, set current extruder to the standby temperature.
    if (m_ooze_prevention.enable && m_writer.extruder() != nullptr)
        gcode += m_ooze_prevention.pre_toolchange(*this, config);

    const std::string& toolchange_gcode = config.get<std::string>("toolchange_gcode");
    std::string toolchange_gcode_parsed;

    const int prev_extruder_id =
        static_cast<int>(m_writer.extruder() != nullptr ? m_writer.extruder()->id() : -1);

    // Process the custom toolchange_gcode. If it is empty, insert just a Tn command.
    // Orca: with manual filament change, not for the first tool (the print starts with it loaded)
    if (!toolchange_gcode.empty() && !(config.get<bool>("manual_filament_change") && prev_extruder_id < 0)) {
        // Do not count the current toolchange.
        if (prev_extruder_id >= 0) {
            ++ m_toolchange_count;
        }
        ParserConfig dynamic_config;
        dynamic_config.set("previous_extruder", prev_extruder_id);
        dynamic_config.set("next_extruder",     int(extruder_id));
        dynamic_config.set("toolchange_count",  int(m_toolchange_count));
        dynamic_config.set("layer_num",         m_layer_index);
        dynamic_config.set("layer_z",           print_z);
        dynamic_config.set("toolchange_z",      print_z);
        dynamic_config.set("max_layer_z",       double{m_max_layer_z});
        // Tool-change variables of OrcaSlicer's change_filament_gcode (ported from Orca GCode.cpp),
        // so the G-code of external multi-material units (purging into their own chute) works.
        {
            const auto temperature = [&](int id) {
                return (m_layer_index <= 0 || std::abs(print_z) < EPSILON ?
                    config.get<std::vector<int>>("first_layer_temperature") :
                    config.get<std::vector<int>>("temperature")).at(id);
            };
            const auto e_feedrate = [&](int id, double filament_area) {
                const int feedrate = int(60.0 * config.get<std::vector<double>>("filament_max_volumetric_speed").at(id) / filament_area);
                return feedrate == 0 ? 100 : feedrate;
            };
            const double filament_area = (M_PI / 4.) * std::pow(config.get<std::vector<double>>("filament_diameter").at(extruder_id), 2);
            double purge_volume = 0.;
            if (prev_extruder_id >= 0) {
                const auto wipe_volumes = WipeTower::extract_wipe_volumes(m_print->config());
                if (size_t(prev_extruder_id) < wipe_volumes.size() && extruder_id < wipe_volumes[prev_extruder_id].size())
                    purge_volume = wipe_volumes[prev_extruder_id][extruder_id];
            }
            dynamic_config.set("old_filament_temp", prev_extruder_id >= 0 ? temperature(prev_extruder_id) : 0);
            dynamic_config.set("new_filament_temp", temperature(int(extruder_id)));
            dynamic_config.set("old_filament_e_feedrate", prev_extruder_id >= 0 ? e_feedrate(prev_extruder_id, filament_area) : 200);
            dynamic_config.set("new_filament_e_feedrate", e_feedrate(int(extruder_id), filament_area));
            dynamic_config.set("flush_length", purge_volume / filament_area);

            // the rest of Orca's change_filament_gcode variables (GCode::set_extruder)
            const Domain::Preset::HwPrinterConfig& hw = config.hw_config();
            const int old_tool = prev_extruder_id >= 0 ? int(hw.tool_of_material_slot(size_t(prev_extruder_id))) : -1;
            const int new_tool = int(hw.tool_of_material_slot(extruder_id));
            dynamic_config.set("current_filament_id", prev_extruder_id);
            dynamic_config.set("next_filament_id", int(extruder_id));
            dynamic_config.set("current_nozzle_id", old_tool);
            dynamic_config.set("next_nozzle_id", new_tool);
            dynamic_config.set("current_hotend", old_tool);
            dynamic_config.set("next_hotend", new_tool);
            dynamic_config.set("outer_wall_volumetric_speed", this->outer_wall_volumetric_speed(*m_print, extruder_id));
            dynamic_config.set("relative_e_axis", config.get<bool>("use_relative_e_distances"));
            dynamic_config.set("fan_speed", 0);
            const auto retract_length            = config.get<std::vector<double>>("retract_length");
            const auto retract_length_toolchange = config.get<std::vector<double>>("retract_length_toolchange");
            dynamic_config.set("old_retract_length", prev_extruder_id >= 0 ? retract_length.at(prev_extruder_id) : 0.);
            dynamic_config.set("new_retract_length", retract_length.at(extruder_id));
            dynamic_config.set("old_retract_length_toolchange", prev_extruder_id >= 0 ? retract_length_toolchange.at(prev_extruder_id) : 0.);
            dynamic_config.set("new_retract_length_toolchange", retract_length_toolchange.at(extruder_id));
            // where the nozzle is before the change
            const Vec3d nozzle_pos = m_writer.get_position();
            dynamic_config.set("x_after_toolchange", nozzle_pos.x());
            dynamic_config.set("y_after_toolchange", nozzle_pos.y());
            dynamic_config.set("z_after_toolchange", nozzle_pos.z());
            // the purge in two halves and in up to four 135 mm3 parts (g_purge_volume_one_time, g_max_flush_count)
            const double flush_length = purge_volume / filament_area;
            dynamic_config.set("first_flush_volume", flush_length / 2.);
            dynamic_config.set("second_flush_volume", flush_length / 2.);
            const int flush_count = std::min(4, int(std::round(purge_volume / 135.)));
            for (int i = 0; i < 4; ++i)
                dynamic_config.set("flush_length_" + std::to_string(i + 1), i < flush_count ? flush_length / flush_count : 0.);
            // Orca get_path_of_change_filament without a cutter path (start_end_points): its fixed points
            dynamic_config.set("travel_point_1_x", 54.);
            dynamic_config.set("travel_point_1_y", 0.);
            dynamic_config.set("travel_point_2_x", 54.);
            dynamic_config.set("travel_point_2_y", 0.);
            dynamic_config.set("travel_point_3_x", 54.);
            dynamic_config.set("travel_point_3_y", 245.);
            dynamic_config.set("wipe_avoid_perimeter", false);
            dynamic_config.set("wipe_avoid_pos_x", 110.);
            dynamic_config.set("is_prime_tower_interface", false);
        }
        toolchange_gcode_parsed = placeholder_parser_process("toolchange_gcode", toolchange_gcode, extruder_id, &dynamic_config);
        // Orca: the fan mover does not move fan commands across custom tool change G-code
        if (m_fan_mover)
            gcode += ";_FAN_MOVER_SKIP_START\n";
        gcode += toolchange_gcode_parsed;
        check_add_eol(gcode);
        if (m_fan_mover)
            gcode += ";_FAN_MOVER_SKIP_END\n";
    }

    // We inform the writer about what is happening, but we may not use the resulting gcode.
    std::string toolchange_command = m_writer.toolchange(extruder_id);
    if (! custom_gcode_changes_tool(toolchange_gcode_parsed, m_writer.toolchange_prefix(), extruder_id))
        gcode += toolchange_command;
    else {
        // user provided his own toolchange gcode, no need to do anything
    }

    // Emit toolchange time annotation for CoolingBuffer.
    const double toolchange_time = config.get<double>("filament_change_time");
    if (toolchange_time > 0.) {
        gcode += ";_TOOLCHANGE_TIME" + std::to_string(toolchange_time) + "\n";
    }

    // Custom toolchange gcode may have changed fan speed via M106/M107 that CoolingBuffer
    // doesn't track. Reset dynamic fan speed and emit ;_TOOLCHANGE_END to force CoolingBuffer
    // to restore the correct fan speed.
    m_current_dynamic_fan_speed.reset();
    gcode += ";_TOOLCHANGE_END\n";

    // Set the temperature if the wipe tower didn't (not needed for non-single extruder MM)
    if (config.get<bool>("single_extruder_multi_material") && !config.get<bool>("wipe_tower")) {
        int temp = (m_layer_index <= 0 ? config.get<std::vector<int>>("first_layer_temperature").at(extruder_id) :
                                         config.get<std::vector<int>>("temperature").at(extruder_id));

        gcode += m_writer.set_temperature(temp, false);
    }

    this->set_current_extruder_placeholders(extruder_id, config);

    // Append the filament start G-code.
    const std::string start_filament_gcode = config.get<std::vector<std::string>>("start_filament_gcode").at(extruder_id);
    if (! start_filament_gcode.empty()) {
        // Process the start_filament_gcode for the new filament.
        ParserConfig dynamic_config;
        dynamic_config.set("layer_num", m_layer_index);
        dynamic_config.set("layer_z",   this->writer().get_position().z() - config.get<double>("z_offset"));
        dynamic_config.set("max_layer_z", double{m_max_layer_z});
        dynamic_config.set("filament_extruder_id", int(extruder_id));
        gcode += this->placeholder_parser_process("start_filament_gcode", start_filament_gcode, extruder_id, &dynamic_config);
        check_add_eol(gcode);
    }

    const PressureAdvance pressure_advance =
        config.get<std::vector<PressureAdvance>>("pressure_advance").at(extruder_id);
    if (pressure_advance != PressureAdvance::Disabled) {
        gcode += m_writer.set_pressure_advance(
            config.get<std::vector<double>>("pressure_advance_value").at(extruder_id),
            this->m_print->config().hw_config().vendor_id
        );

        if (pressure_advance == PressureAdvance::AutomaticCalibration) {
            gcode += m_writer.emit_automatic_pressure_advance_calibration();
        }
    }

    // Set the new extruder to the operating temperature.
    if (m_ooze_prevention.enable)
        gcode += m_ooze_prevention.post_toolchange(*this, config);

    // The position is now known after the tool change.
    this->last_position = std::nullopt;

    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Point GCodeGenerator::gcode_to_point(const Vec2d &point) const
{
    Vec2d pt = point - m_origin;
    if (const Extruder *extruder = m_writer.extruder(); extruder)
        // This function may be called at the very start from toolchange G-code when the extruder is not assigned yet.
        pt += m_extruder_offset.at(extruder->id());
    return scaled<coord_t>(pt);
}

}   // namespace Slic3r
