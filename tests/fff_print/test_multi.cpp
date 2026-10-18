#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>
#include <sstream>

#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/FlushVolCalc.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "test_data.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"

#include "boost/algorithm/string.hpp"

using namespace Slic3r;
using namespace std::literals;
using Catch::Approx;
using Biz::GCodeReader::GCodeReader;
using Test::TestConfig;
using Domain::VolumeSettings;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Biz::Algorithms::ModelObject::add_volume;
using Biz::Algorithms::ModelObject::ensure_on_bed;
using Biz::Algorithms::ModelVolume::translate;
using Biz::Slicing::SerializedConfig;
using Domain::Preset::HwPrinterConfig;

SCENARIO("Basic tests", "[Multi]")
{
    WHEN("Slicing multi-material print with non-consecutive extruders") {

        Test::TestConfig config{4, 0.6};
        config.print.items.opt("support_material_extruder").set(0);
        config.print.items.opt("perimeter_extruder").set(2);
        config.print.items.opt("solid_infill_extruder").set(2);
        config.print.items.opt("infill_extruder").set(4);
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("Sliced successfully") {
            REQUIRE(! gcode.empty());
        }
        THEN("T3 toolchange command found") {
            bool T1_found = gcode.find("\nT3\n") != gcode.npos;
            REQUIRE(T1_found);
        }
    }

    WHEN("Slicing with multiple skirts with a single, non-zero extruder") {
        Test::TestConfig config{4, 0.6};
        config.print.items.opt("support_material_interface_extruder").set(2);
        config.print.items.opt("support_material_extruder").set(0);
        config.print.items.opt("perimeter_extruder").set(2);
        config.print.items.opt("solid_infill_extruder").set(2);
        config.print.items.opt("infill_extruder").set(4);

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("Sliced successfully") {
            REQUIRE(! gcode.empty());
        }
    }
}

TEST_CASE("Ooze prevention", "[Multi]")
{
    TestConfig config{4, 0.6};

    config.print.items.opt("support_material_extruder").set(4);
    config.print.items.opt("raft_layers").set(2);
    config.print.items.opt("infill_extruder").set(2);
    config.print.items.opt("solid_infill_extruder").set(3);
    config.print.items.opt("ooze_prevention").set(true);
    // the standby temperatures alone: Orca's preheating drops a standby when the tool is heated again soon
    config.print.items.opt("preheat_time").set(0.);

    config.printer.items.opt("extruder_offset")
        .set(std::vector{Vec2d{0, 0}, Vec2d{20, 0}, Vec2d{0, 20}, Vec2d{20, 20}});
    config.filament.at(0).items.opt("temperature").set(200);
    config.filament.at(1).items.opt("temperature").set(180);
    config.filament.at(2).items.opt("temperature").set(170);
    config.filament.at(3).items.opt("temperature").set(160);
    config.filament.at(0).items.opt("first_layer_temperature").set(206);
    config.filament.at(1).items.opt("first_layer_temperature").set(186);
    config.filament.at(2).items.opt("first_layer_temperature").set(166);
    config.filament.at(3).items.opt("first_layer_temperature").set(156);
    // test that it doesn't crash when this is supplied
    config.printer.items.opt("toolchange_gcode").set("T[next_extruder] ;toolchange" );

    // Since July 2019, PrusaSlicer only emits automatic Tn command in case that the toolchange_gcode is empty
    // The "T[next_extruder]" is therefore needed in this test.

    std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

    GCodeReader parser;
    int         tool = -1;
    int         tool_temp[] = { 0, 0, 0, 0};
    Points      toolchange_points;
    Points      extrusion_points;
    parser.parse_buffer(gcode, [&tool, &tool_temp, &toolchange_points, &extrusion_points, &config]
        (GCodeReader &self, const GCodeReader::GCodeLine &line)
    {
        // if the command is a T command, set the the current tool
        if (boost::starts_with(line.cmd(), "T")) {
            // Ignore initial toolchange.
            if (tool != -1) {
                int expected_temp = is_approx<double>(self.z(), config.print.items.opt("first_layer_height").get<FloatOrPercentage>().get_abs_value(1.0) + config.printer.items.opt("z_offset").get<double>()) ?
                    config.filament.at(tool).items.opt("first_layer_temperature").get<int>() :
                    config.filament.at(tool).items.opt("temperature").get<int>();
                if (tool_temp[tool] != expected_temp + config.print.items.opt("standby_temperature_delta").get<int>())
                    throw std::runtime_error("Standby temperature was not set before toolchange.");
                toolchange_points.emplace_back(self.xy_scaled());
            }
            tool = atoi(line.cmd().data() + 1);
        } else if (line.cmd_is("M104") || line.cmd_is("M109")) {
            // May not be defined on this line.
            int t = tool;
            line.has_value('T', t);
            // Should be available on this line.
            int s;
            if (! line.has_value('S', s))
                throw std::runtime_error("M104 or M109 without S");

            // Following is obsolete. The first printing extruder is newly set to its first layer temperature immediately, not to the standby.
            //if (tool_temp[t] == 0 && s != print_config.first_layer_temperature.get_at(t) + print_config.standby_temperature_delta)
            //    throw std::runtime_error("initial temperature is not equal to first layer temperature + standby delta");

            tool_temp[t] = s;
        } else if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0) {
            extrusion_points.emplace_back(
                line.new_XY_scaled(self)
                + scaled<coord_t>(
                    config.printer.items.opt("extruder_offset").get<std::vector<Vec2d>>().at(tool)
                )
            );
        }
    });

    Polygon convex_hull = Geometry::convex_hull(extrusion_points);
    
    // THEN("all nozzles are outside skirt at toolchange") {
    //     Points t;
    //     sort_remove_duplicates(toolchange_points);
    //     size_t inside = 0;
    //     for (const auto &point : toolchange_points)
    //         for (const Vec2d &offset : print_config.extruder_offset.values) {
    //             Point p = point + scaled<coord_t>(offset);
    //             if (convex_hull.contains(p))
    //                 ++ inside;
    //         }
    //     REQUIRE(inside == 0);
    // }

#if 0
    require "Slic3r/SVG.pm";
    Slic3r::SVG::output(
        "ooze_prevention_test.svg",
        no_arrows   => 1,
        polygons    => [$convex_hull],
        red_points  => \@t,
        points      => \@toolchange_points,
    );
#endif
    
    THEN("all toolchanges happen within expected area") {
        // offset the skirt by the maximum displacement between extruders plus a safety extra margin
        const float delta = scaled<float>(20. * sqrt(2.) + 1.);
        Polygon outer_convex_hull = expand(convex_hull, delta).front();
        size_t inside = std::count_if(toolchange_points.begin(), toolchange_points.end(), [&outer_convex_hull](const Point &p){ return Slic3r::Biz::Algorithms::Polygon::contains(outer_convex_hull, p); });
        REQUIRE(inside == toolchange_points.size());
    }
}

std::string slice_model(Domain::Model &model, const TestConfig &config)
{
    Domain::Bed model_bed;
    Domain::BedInstance bed_instance{model_bed};
    for (const Domain::ModelObject* object : model.objects) {
        for (Domain::ModelInstance* instance : object->instances) {
            bed_instance.model_instances.push_back(instance);
        }
    }
    Print print;
    auto preset_metadata = Test::create_dummy_selected_preset_metadata(
        Test::create_dummy_hw_config(config.tool.size())
    );
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);
    auto status{print.update(
        model,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    )};
    REQUIRE(std::holds_alternative<Biz::Slicing::ApplyStatus::Changed>(status));
    print.validate();
    return Test::gcode(print);
}

std::string slice_stacked_cubes(const TestConfig &config, const VolumeSettings &volume1config, const VolumeSettings &volume2config)
{
    Domain::Model        model;
    Domain::ModelObject* object = model.add_object();
    object->name = "object.stl";
    Domain::ModelVolume *v1 = add_volume(object, Test::mesh(Test::TestMesh::cube_20x20x20));
    v1->volume_settings = volume1config;
    Domain::ModelVolume *v2 = add_volume(object, Test::mesh(Test::TestMesh::cube_20x20x20));
    translate(*v2, 0., 0., 20.);
    v2->volume_settings = volume2config;
    object->add_instance();
    ensure_on_bed(*object);

    return slice_model(model, config);
}

TEST_CASE("Stacked cubes", "[Multi]")
{
    VolumeSettings lower_config;

    lower_config.overrides.set("extruder", 1);
    lower_config.overrides.set("bottom_solid_layers", 0);
    lower_config.overrides.set("top_solid_layers", 1);

    VolumeSettings upper_config;

    upper_config.overrides.set("extruder", 2);
    upper_config.overrides.set("bottom_solid_layers", 1);
    upper_config.overrides.set("top_solid_layers", 0);

    static constexpr const double solid_infill_speed = 99;
    TestConfig config{4, 0.6};

    config.print.items.opt("fill_density").set(Percentage{0});
    config.print.items.opt("solid_infill_speed").set(FloatOrPercentage{solid_infill_speed});
    config.print.items.opt("top_solid_infill_speed").set(FloatOrPercentage{solid_infill_speed});
    // for preventing speeds from being altered
    config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});

    // for preventing speeds from being altered
    for (auto& filament_settings : config.filament) {
        filament_settings.items.opt("cooling").set(false);
    }


    auto test_shells = [](const std::string &gcode) {
        GCodeReader       parser;
        int               tool = -1;
        // Scaled Z heights.
        std::set<coord_t> T0_shells, T1_shells;
        parser.parse_buffer(gcode, [&tool, &T0_shells, &T1_shells]
            (GCodeReader &self, const GCodeReader::GCodeLine &line)
        {
            if (boost::starts_with(line.cmd(), "T")) {
                tool = atoi(line.cmd().data() + 1);
            } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                if (is_approx<double>(line.new_F(self), solid_infill_speed * 60.) && (tool == 0 || tool == 1))
                    (tool == 0 ? T0_shells : T1_shells).insert(scaled<coord_t>(self.z()));
            }
        });
        return std::make_pair(T0_shells, T1_shells);
    };

    WHEN("Interface shells disabled") {
        std::string gcode = slice_stacked_cubes(config, lower_config, upper_config);
        auto [t0, t1] = test_shells(gcode);
        THEN("no interface shells") {
            REQUIRE(t0.empty());
            REQUIRE(t1.empty());
        }
    }
    WHEN("Interface shells enabled") {
        config.print.items.opt("interface_shells").set(true);
        std::string gcode = slice_stacked_cubes(config, lower_config, upper_config);
        auto [t0, t1] = test_shells(gcode);
        THEN("top interface shells") {
            REQUIRE(t0.size() == lower_config.overrides.get("top_solid_layers")->get<int>());
        }
        THEN("bottom interface shells") {
            REQUIRE(t1.size() == upper_config.overrides.get("bottom_solid_layers")->get<int>());
        }
    }
    WHEN("Slicing with auto-assigned extruders") {
        TestConfig config{4, 0.6};
        config.print.items.opt("layer_height").set(0.4);
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.4});
        config.print.items.opt("skirts").set(0);
        std::string gcode = slice_stacked_cubes(config, VolumeSettings{}, VolumeSettings{});
        GCodeReader       parser;
        int               tool = -1;
        // Scaled Z heights.
        std::set<coord_t> T0_shells, T1_shells;
        parser.parse_buffer(gcode, [&tool, &T0_shells, &T1_shells](GCodeReader &self, const GCodeReader::GCodeLine &line)
        {
            if (boost::starts_with(line.cmd(), "T")) {
                tool = atoi(line.cmd().data() + 1);
            } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                if (tool == 0 && self.z() > 20)
                    // Layers incorrectly extruded with T0 at the top object.
                    T0_shells.insert(scaled<coord_t>(self.z()));
                else if (tool == 1 && self.z() < 20)
                    // Layers incorrectly extruded with T1 at the bottom object.
                    T1_shells.insert(scaled<coord_t>(self.z()));
            }
        });
        THEN("T0 is never used for upper object") {
            REQUIRE(T0_shells.empty());
        }
        THEN("T0 is never used for lower object") {
            REQUIRE(T1_shells.empty());
        }
    }
}

namespace {

const constexpr std::string_view tool_change_marker{"TEST_TOOLCHANGE"};
const constexpr std::string_view layer_change_marker{"LAYER_CHANGE"};

struct LayerStartToolChange
{
    Point position_at_tool_change;
    Point previous_layer_last_extrusion;
};

std::vector<LayerStartToolChange> collect_layer_start_tool_changes(const std::string& gcode)
{
    std::vector<LayerStartToolChange> tool_changes;

    GCodeReader parser;
    Point last_extrusion_position{0, 0};
    bool anything_extruded{false};
    bool is_layer_change_pending{false};

    parser.parse_buffer(
        gcode,
        [&](GCodeReader& self, const GCodeReader::GCodeLine& line)
        {
            const std::string_view comment{line.comment()};

            if (comment.find(tool_change_marker) != std::string_view::npos) {
                // Skip the initial tool selection, there is nothing printed before it.
                if (is_layer_change_pending && anything_extruded) {
                    tool_changes.push_back({self.xy_scaled(), last_extrusion_position});
                }

                return;
            }

            if (comment.find(layer_change_marker) != std::string_view::npos) {
                is_layer_change_pending = true;
                return;
            }

            if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0.f) {
                last_extrusion_position = line.new_XY_scaled(self);
                anything_extruded       = true;
                is_layer_change_pending = false;
            }
        }
    );

    return tool_changes;
}

std::string slice_two_cubes_with_different_extruders(const TestConfig& config)
{
    Domain::Model model;

    for (int object_index = 0; object_index < 2; ++object_index) {
        Domain::ModelObject* object = model.add_object();
        object->name                = "object.stl";
        Domain::ModelVolume* volume = add_volume(object, Test::mesh(Test::TestMesh::cube_20x20x20));
        translate(*volume, object_index * 40., 0., 0.);
        VolumeSettings volume_settings;
        volume_settings.overrides.set("extruder", object_index + 1);
        volume->volume_settings = volume_settings;
        object->add_instance();
        ensure_on_bed(*object);
    }

    return slice_model(model, config);
}

} // namespace

TEST_CASE(
    "Tool change without a wipe tower is not preceded by a travel to another object",
    "[Multi]"
)
{
    TestConfig config{2};

    config.print.items.opt("wipe_tower").set(false);
    config.print.items.opt("travel_ramping_lift").set(true);
    config.print.items.opt("travel_slope").set(30.);
    config.print.items.opt("skirts").set(0);
    config.print.items.opt("layer_height").set(0.2);
    config.printer.items.opt("toolchange_gcode")
        .set("T[next_extruder] ; " + std::string{tool_change_marker});
    config.print.items.opt("toolchange_ordering").set(Domain::ToolChangeOrderingType::Cyclic);

    const std::string gcode{slice_two_cubes_with_different_extruders(config)};
    const std::vector<LayerStartToolChange> tool_changes{collect_layer_start_tool_changes(gcode)};

    REQUIRE(!tool_changes.empty());

    for (const LayerStartToolChange& tool_change : tool_changes) {
        REQUIRE(
            tool_change.position_at_tool_change.x() == tool_change.previous_layer_last_extrusion.x()
        );
        REQUIRE(
            tool_change.position_at_tool_change.y() == tool_change.previous_layer_last_extrusion.y()
        );
    }
}

namespace {

// One tool fed by a 4-slot multi-material unit (single extruder multi material), as the
// Prusa MMU and the generic external unit both are.
TestConfig semm_config(const unsigned slot_count)
{
    TestConfig config{1};
    config.filament.resize(slot_count);
    config.hw_config.feeders[Domain::Preset::Address{0}] = Domain::Preset::HwFeederConfig{
        .id         = "test_feeder",
        .type       = Domain::Preset::FeederType::MMU,
        .model      = {.model = "TEST", .base_model = "TEST"},
        .slot_count = slot_count
    };
    config.printer.items.opt("single_extruder_multi_material").set(true);
    config.print.items.opt("wipe_tower").set(true);
    config.print.items.opt("skirts").set(0);
    // required by the wipe tower
    config.printer.items.opt("use_relative_e_distances").set(true);
    config.print.items.opt("support_material_extruder").set(0);
    config.print.items.opt("support_material_interface_extruder").set(0);
    return config;
}

std::string slice_semm_stacked_cubes(const TestConfig& config)
{
    Domain::Model model;
    Domain::ModelObject* object = model.add_object();
    object->name = "object.stl";
    for (int i = 0; i < 2; ++i) {
        Domain::ModelVolume* volume = add_volume(object, Test::mesh(Test::TestMesh::cube_20x20x20));
        translate(*volume, 0., 0., i * 20.);
        VolumeSettings volume_settings;
        volume_settings.overrides.set("extruder", i + 1);
        volume->volume_settings = volume_settings;
    }
    object->add_instance();
    ensure_on_bed(*object);

    Domain::Bed model_bed;
    Domain::BedInstance bed_instance{model_bed};
    for (Domain::ModelInstance* instance : object->instances)
        bed_instance.model_instances.push_back(instance);
    Print print;
    auto preset_metadata = Test::create_dummy_selected_preset_metadata(config.hw_config);
    auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);
    auto status{print.update(
        model,
        config,
        bed_instance,
        preset_metadata,
        Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
    )};
    if (const auto* invalid = std::get_if<Biz::Slicing::ApplyStatus::InvalidData>(&status)) {
        for (const auto& error : invalid->errors) {
            UNSCOPED_INFO("error " << int(error.code));
            for (const auto& key : error.item_keys)
                UNSCOPED_INFO("  key " << key);
        }
    }
    REQUIRE(std::holds_alternative<Biz::Slicing::ApplyStatus::Changed>(status));
    print.validate();
    return Test::gcode(print);
}

// Writes the G-code to $PIGGIE_GCODE_DUMP_DIR/<name>.gcode when that variable is set, so the
// output can be compared byte for byte before and after a change.
void dump_gcode(const std::string& name, const std::string& gcode)
{
    if (const char* dir = std::getenv("PIGGIE_GCODE_DUMP_DIR")) {
        std::ofstream out{std::string{dir} + "/" + name + ".gcode", std::ios::binary};
        out << gcode;
    }
}

} // namespace

TEST_CASE("Single extruder multi material wipe tower G-code", "[Multi][SEMM]")
{
    const std::string gcode{slice_semm_stacked_cubes(semm_config(4))};
    dump_gcode("semm_wipe_tower", gcode);
    REQUIRE(gcode.find("\nT1\n") != std::string::npos);
    REQUIRE(gcode.find("; CP TOOLCHANGE UNLOAD") != std::string::npos);
}

TEST_CASE("Single material G-code", "[Multi][SEMM]")
{
    const std::string gcode{Slic3r::Test::slice({Slic3r::Test::TestMesh::cube_20x20x20}, TestConfig{1})};
    dump_gcode("single_material", gcode);
    REQUIRE(!gcode.empty());
}

namespace {

// True when the wipe tower's unload sections (from "; CP TOOLCHANGE UNLOAD" to the tool change)
// move the extruder, i.e. ram, retract into the cooling tube, cool or park the filament.
bool unload_moves_extruder(const std::string& gcode)
{
    std::istringstream in{gcode};
    std::string line;
    bool in_unload = false;
    while (std::getline(in, line)) {
        if (line.starts_with("; CP TOOLCHANGE UNLOAD"))
            in_unload = true;
        else if (line.starts_with("T") || line.starts_with("; CP TOOLCHANGE END"))
            in_unload = false;
        // Ramming and cooling are X moves with E, the cooling tube retraction and parking are
        // E moves written by the wipe tower (4 decimals); the plain tool change retraction
        // written by the G-code generator after the unload ("G1 E-10") is not counted.
        else if (in_unload && line.starts_with("G1") && line.find(" E") != std::string::npos
                 && (line.starts_with("G1 X") || line.find('.') != std::string::npos))
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("External multi-material unit: no ramming, cooling or parking", "[Multi][SEMM][ExternalMMU]")
{
    TestConfig config{semm_config(4)};
    REQUIRE(unload_moves_extruder(slice_semm_stacked_cubes(config)));

    // as the built-in external unit settings set them
    config.printer.items.opt("enable_filament_ramming").set(false);
    config.printer.items.opt("cooling_tube_length").set(0.);
    config.printer.items.opt("cooling_tube_retraction").set(0.);
    config.printer.items.opt("parking_pos_retraction").set(0.);
    config.printer.items.opt("extra_loading_move").set(0.);
    const std::string gcode{slice_semm_stacked_cubes(config)};
    dump_gcode("semm_no_ramming", gcode);
    REQUIRE(gcode.find("\nT1\n") != std::string::npos);
    REQUIRE(gcode.find("; CP TOOLCHANGE WIPE") != std::string::npos);
    REQUIRE(gcode.find("; CP TOOLCHANGE LOAD") == std::string::npos);
    REQUIRE(!unload_moves_extruder(gcode));
}

TEST_CASE("Purge volumes from filament colours (Orca)", "[Multi][SEMM][ExternalMMU]")
{
    using Slic3r::FlushVolCalc::calc_flush_volume;
    const Domain::ColorRGBA black{0.f, 0.f, 0.f, 1.f};
    const Domain::ColorRGBA white{1.f, 1.f, 1.f, 1.f};
    // Orca's formula: luminance step 0 -> 1 gives 1^0.7 * 560, 1 -> 0 gives 1 * 80
    REQUIRE(calc_flush_volume(black, white) == 560);
    REQUIRE(calc_flush_volume(white, black) == 80);
    // Orca's measured data (flush_data_standard.txt): #000000 -> #545454 = 236, #C12E1F -> #000000 = 26
    REQUIRE(calc_flush_volume(black, Domain::ColorRGBA{(unsigned char)0x54, (unsigned char)0x54, (unsigned char)0x54, (unsigned char)255}) == 236);
    REQUIRE(calc_flush_volume(Domain::ColorRGBA{(unsigned char)0xC1, (unsigned char)0x2E, (unsigned char)0x1F, (unsigned char)255}, black) == 26);
    REQUIRE(calc_flush_volume(black, white, 100) == 660);

    TestConfig config{semm_config(2)};
    config.project.items.opt("extruder_colour").set(std::vector<std::string>{"#000000", "#FFFFFF"});
    config.filament[1].items.opt("filament_purge_multiplier").set(Percentage{50.});
    {
        const auto volumes = WipeTower::extract_wipe_volumes(config.get_view());
        // 'Purging volume' (default 140) when not by colour
        REQUIRE(volumes[0][1] == Approx(70.f));
        REQUIRE(volumes[1][0] == Approx(140.f));
    }
    config.printer.items.opt("wiping_volumes_by_colour").set(true);
    {
        const auto volumes = WipeTower::extract_wipe_volumes(config.get_view());
        REQUIRE(volumes[0][1] == Approx(280.f)); // 560 * 50 %
        REQUIRE(volumes[1][0] == Approx(80.f));
    }
    // a slot count other than the 5x5 project matrix (e.g. a 16-slot unit) is handled
    TestConfig config16{semm_config(16)};
    const auto volumes16 = WipeTower::extract_wipe_volumes(config16.get_view());
    REQUIRE(volumes16.size() == 16);
    REQUIRE(volumes16[15][0] == Approx(140.f));

    // Orca's purge_in_prime_tower off (e.g. Kobra 3 + ACE: the unit purges into its chute):
    // the tower only primes prime_volume (Orca Print.cpp _make_wipe_tower)
    config.printer.items.opt("purge_in_prime_tower").set(false);
    config.print.items.opt("prime_volume").set(45.);
    {
        const auto volumes = WipeTower::extract_wipe_volumes(config.get_view());
        REQUIRE(volumes[0][1] == Approx(45.f));
        REQUIRE(volumes[1][0] == Approx(45.f));
        REQUIRE(volumes[0][0] == Approx(0.f).margin(15.f)); // only the minimal purge on the diagonal
    }
}

TEST_CASE("Per-tool values follow the tool feeding each slot", "[Multi][ExternalMMU]")
{
    // two tools, a 3-slot unit on the second one: slots 0 | 1 2 3 (Orca: filament_map)
    TestConfig config{2};
    config.filament.resize(4);
    config.hw_config.feeders[Domain::Preset::Address{1}] = Domain::Preset::HwFeederConfig{
        .id         = "test_feeder",
        .type       = Domain::Preset::FeederType::MMU,
        .model      = {.model = "TEST", .base_model = "TEST"},
        .slot_count = 3
    };
    config.printer.items.opt("extruder_offset").set(std::vector<Vec2d>{Vec2d{0., 0.}, Vec2d{10., 0.}});
    const auto offsets = config.get_view().get<std::vector<Vec2d>>("extruder_offset");
    REQUIRE(offsets.size() == 4);
    REQUIRE(offsets[0] == Vec2d{0., 0.});
    REQUIRE(offsets[1] == Vec2d{10., 0.});
    REQUIRE(offsets[3] == Vec2d{10., 0.});
}
