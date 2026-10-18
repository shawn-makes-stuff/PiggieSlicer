#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem/path.hpp>
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostMoonraker.hpp"

#include <map>

namespace fs = boost::filesystem;


template <typename T>
void update_diff(
    Slic3r::Biz::Preset::PresetsSwitchStates& ret,
    Slic3r::Biz::Preset::PresetDiffOperation operation,
    std::optional<std::string> new_name,
    const T& original,
    const T& selected,
    const std::function<const Slic3r::Domain::ConfigBox&(const T&)>& getter,
    Slic3r::Domain::Preset::PresetKind kind,
    std::optional<size_t> slot = std::nullopt
)
{
    auto diff = getter(selected).diff_keys(getter(original));
    for (const auto& key : diff) {
        Slic3r::Biz::Preset::PresetSwitchState dest{
            .operation       = operation,
            .new_preset_name = new_name
        };
        const auto it = getter(selected).find(key);
        if (it.is_override) {
            dest.overrides.insert({key, it.item->value()});
        } else {
            dest.items.insert({key, it.item->value()});
        }
        ret.emplace(Slic3r::Biz::Preset::PresetSwitchKindId{kind, slot}, std::move(dest));
    }
}

class TestPresetDialogManager : public Slic3r::Biz::Preset::IPresetDialogManager
{
public:
    PresetsSwitchStates show_unsaved_changes_dialog(
        const std::string& dialog_name,
        const Slic3r::Domain::ConfigPack& config_original,
        const Slic3r::Domain::ConfigPack& config_selected,
        Slic3r::Domain::ConfigPack* config_new_selected,
        const Slic3r::Biz::Preset::PresetSelectionNames& preset_names,
        const Slic3r::Biz::Preset::PresetSelectionNames& preset_names_new,
        const Slic3r::Biz::Preset::PresetInteractor& preset_interactor,
        bool new_printer_has_multiple_extruders = false
    ) override
    {
        PresetsSwitchStates ret;
        std::visit(
            Slic3r::Domain::overloaded{
                [&](const Slic3r::Domain::ConfigPackFDM& original)
                {
                    const auto& selected = std::get<Slic3r::Domain::ConfigPackFDM>(config_selected);
                    update_diff<Slic3r::Domain::ConfigPackFDM>(
                        ret,
                        operation,
                        new_name,
                        original,
                        selected,
                        [](const auto& p) -> const Slic3r::Domain::ConfigBox& { return p.printer; },
                        Slic3r::Domain::Preset::PresetKind::FdmPrinter
                    );
                    update_diff<Slic3r::Domain::ConfigPackFDM>(
                        ret,
                        operation,
                        new_name,
                        original,
                        selected,
                        [](const auto& p) -> const Slic3r::Domain::ConfigBox& { return p.print; },
                        Slic3r::Domain::Preset::PresetKind::FdmPrint
                    );
                    for (size_t i = 0; i < original.tool.size(); i++) {
                        update_diff<Slic3r::Domain::ConfigPackFDM>(
                            ret,
                            operation,
                            new_name,
                            original,
                            selected,
                            [&](const auto& p) -> const Slic3r::Domain::ConfigBox&
                            { return p.tool[i]; },
                            Slic3r::Domain::Preset::PresetKind::FdmToolPrint, i
                        );
                        update_diff<Slic3r::Domain::ConfigPackFDM>(
                            ret,
                            operation,
                            new_name,
                            original,
                            selected,
                            [&](const auto& p) -> const Slic3r::Domain::ConfigBox&
                            { return p.filament[i]; },
                            Slic3r::Domain::Preset::PresetKind::FdmMaterial, i
                        );
                    }
                },
                [&](const Slic3r::Domain::ConfigPackSLA& original)
                {
                    const auto& selected = std::get<Slic3r::Domain::ConfigPackSLA>(config_selected);
                    update_diff<Slic3r::Domain::ConfigPackSLA>(
                        ret,
                        operation,
                        new_name,
                        original,
                        selected,
                        [](const auto& p) -> const Slic3r::Domain::ConfigBox&
                        { return p.sla_printer_settings; },
                        Slic3r::Domain::Preset::PresetKind::SlaPrinter
                    );
                    update_diff<Slic3r::Domain::ConfigPackSLA>(
                        ret,
                        operation,
                        new_name,
                        original,
                        selected,
                        [](const auto& p) -> const Slic3r::Domain::ConfigBox&
                        { return p.sla_print_settings; },
                        Slic3r::Domain::Preset::PresetKind::SlaPrint
                    );
                    update_diff<Slic3r::Domain::ConfigPackSLA>(
                        ret,
                        operation,
                        new_name,
                        original,
                        selected,
                        [](const auto& p) -> const Slic3r::Domain::ConfigBox&
                        { return p.sla_material_settings; },
                        Slic3r::Domain::Preset::PresetKind::SlaMaterial
                    );
                }
            },
            config_original
        );
        return ret;
    }

    std::string show_save_dialog(
        Slic3r::Domain::Preset::PresetKind kind,
        const std::string& original_name,
        const Slic3r::Biz::Preset::PresetInteractor& preset_interactor
    ) override
    {
        return new_name;
    }

    NamesPerKindMap show_save_print_tool_dialog (
        const NamesPerKindMap& original_names_per_kind,
        const Slic3r::Biz::Preset::PresetInteractor& preset_interactor
    ) override
    {
        return new_print_tool_names;
    }

    Slic3r::Biz::Preset::PresetDiffOperation operation = Slic3r::Biz::Preset::PresetDiffOperation::Save;
    std::string new_name{"test"};
    NamesPerKindMap new_print_tool_names = {
        {Slic3r::Domain::Preset::PresetKind::FdmPrint, {"print_test"}},
        {Slic3r::Domain::Preset::PresetKind::FdmToolPrint, {"tool1_test", "tool2_test"}}
    };
};



struct BaseProjectInteractorFixture
{
    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher main_thread_dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{main_thread_dispatcher};
    Tests::JobManagerScope job_manager_scope{main_thread_dispatcher};
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Biz::ProjectInteractor project_interactor{workbench, main_thread_dispatcher, thumbnail_image_generator};
    TestPresetDialogManager preset_dialog_manager;

    BaseProjectInteractorFixture()
    {
        project_interactor.preset_interactor().set_dialog_manager(&preset_dialog_manager);
    }

    virtual ~BaseProjectInteractorFixture()
    {
        main_thread_dispatcher.close();
    }
};

struct LoadedProjectInteractorFixture : BaseProjectInteractorFixture
{
    const Slic3r::Biz::Preset::IO::BundlePaths bundle_paths{
        Slic3r::Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
    };

    LoadedProjectInteractorFixture()
    {
        auto& preset_interactor = project_interactor.preset_interactor();
        preset_interactor.set_use_hw_config_short_name(false);
        preset_interactor.load_preset_bundle(bundle_paths);
    }

    ~LoadedProjectInteractorFixture() override
    {
        fs::remove_all(bundle_paths.user_bundle_path);
    }
};

template <typename T>
const T* find_if(const Slic3r::Biz::IObservableList<T>& list, const std::function<bool(const T&)>& predicate)
{
    for (size_t i = 0, n = list.size(); i < n; ++i) {
        if (predicate(list.at(i)))
            return &list.at(i);
    }
    return nullptr;
}

template <typename T>
const T* find_by_name(const Slic3r::Biz::IObservableList<T>& list, const std::string& name)
{
    return find_if<T>(list, [&](const T& item) { return item.name == name; });
}


TEST_CASE_METHOD(LoadedProjectInteractorFixture, "Preset Interactor Tests", "[PresetInteractor][preset]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();
    REQUIRE(printer_items.items().size() > 0);

    auto switch_printer_and_verify = [&](const std::string& printer_name)     {
        std::optional<size_t> printer_idx;
        for (size_t i = 0, n = printer_items.items().size(); i < n; ++i) {
            const auto& item = printer_items.items().at(i);
            if (item.hw_printer_config_name == printer_name) {
                printer_idx = i;
                break;
            }
        }
        REQUIRE(printer_idx.has_value() == true);
        const auto& selected_printer = printer_items.items().at(printer_idx.value());
        preset_interactor.select_printer_preset(selected_printer.hw_printer_config_id, selected_printer.id);

        const auto selected_printer_idx = printer_items.selected_index();
        REQUIRE(selected_printer_idx != Slic3r::Domain::INVALID_ID);
        REQUIRE(printer_items.items().at(selected_printer_idx).hw_printer_config_name == printer_name);
    };

    auto switch_print_and_verify = [&](const std::string& print_name) -> const auto&
    {
        const auto& p_ptr = find_by_name(preset_interactor.print_presets().items(), print_name);
        REQUIRE(p_ptr != nullptr);
        const auto& p = *p_ptr;
        REQUIRE(p.id.empty() == false);
        preset_interactor.select_print_preset(p.id);
        const auto& selected_idx = preset_interactor.print_presets().selected_index();
        REQUIRE(preset_interactor.print_presets().items().at(selected_idx).name == print_name);
        return p.id;
    };


    // Validate precondition: selected printer
    {
        const auto selected_printer_idx = printer_items.selected_index();
        REQUIRE(selected_printer_idx != Slic3r::Domain::INVALID_ID);
        REQUIRE(printer_items.items().at(selected_printer_idx).hw_printer_config_name.starts_with("Prusa CORE One"));
    }

    // Make sure the printer is switched to Prusa CORE One 0.4 HF
    switch_printer_and_verify("Prusa CORE One 0.4 HF");

    // Switch print to 0.20mm
    switch_print_and_verify("0.20mm");

    // Switch printer to Prusa CORE One 0.6 HF
    switch_printer_and_verify("Prusa CORE One 0.6 HF");

    // Verify that printer preset stays 0.20mm
    {
        const auto& print_presets = preset_interactor.print_presets();
        const auto print_preset_idx = print_presets.selected_index();
        REQUIRE(print_presets.items().at(print_preset_idx).name == "0.20mm");
    }

    {
        const auto user_preset_name = preset_dialog_manager.new_name;

        auto user_preset = find_if<Slic3r::Biz::Preset::PresetItem>(
            preset_interactor.print_presets().items(),
            [&](const auto& p) -> bool { return p.name == user_preset_name; }
        );
        REQUIRE(user_preset == nullptr);

        preset_interactor.set_preset_value(
            Slic3r::Domain::FDMConfigLocation::Print,
            0,
            "brim_width",
            [](auto& p) { p.set(2.0); }
        );
        const auto* p20 = find_by_name(preset_interactor.print_presets().items(), "0.20mm");
        REQUIRE(p20 != nullptr);

        bool can_select = Slic3r::Biz::Preset::PresetSelectionCheck::can_select_print_preset(
            preset_interactor,
            p20->id
        );
        REQUIRE(can_select);
        const auto& hw_config = preset_interactor.current_printer_config();
        preset_interactor.select_print_preset(p20->id);
        fs::path saved_preset_path = Slic3r::Biz::Preset::IO::preset_path(
            bundle_paths,
            Slic3r::Domain::Preset::PresetKind::FdmPrint,
            preset_dialog_manager.new_name,
            hw_config.vendor_id,
            hw_config.repo_id
        );
        REQUIRE(fs::exists(saved_preset_path));

        user_preset = find_if<Slic3r::Biz::Preset::PresetItem>(
            preset_interactor.print_presets().items(),
            [&](const auto& p) -> bool { return p.name == user_preset_name; }
        );
        REQUIRE(user_preset != nullptr);

        preset_interactor.load_preset_bundle(bundle_paths);
        switch_printer_and_verify("Prusa CORE One 0.6 HF");
        user_preset = find_if<Slic3r::Biz::Preset::PresetItem>(
            preset_interactor.print_presets().items(),
            [&](const auto& p) -> bool { return p.name == user_preset_name; }
        );
        INFO(preset_interactor.current_printer_config().name);
        REQUIRE(user_preset != nullptr);
    }
}



TEST_CASE_METHOD(LoadedProjectInteractorFixture, "External multi-material unit", "[PresetInteractor][preset][ExternalMMU]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();

    auto select_printer = [&](const std::string& printer_name)
    {
        for (size_t i = 0, n = printer_items.items().size(); i < n; ++i) {
            const auto& item = printer_items.items().at(i);
            if (item.hw_printer_config_name == printer_name) {
                preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
                return true;
            }
        }
        return false;
    };
    auto printer_value = [&](const std::string& key)
    {
        return preset_interactor.selected_printer_preset().printer.config_box().items.opt(key).get<bool>();
    };

    REQUIRE(select_printer("Prusa CORE One 0.4 HF"));
    REQUIRE(preset_interactor.can_attach_external_mmu(0));
    REQUIRE(!preset_interactor.external_mmu_slot_count(0).has_value());
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == 1);
    REQUIRE(printer_value("enable_filament_ramming"));

    // attach a 4-slot unit
    REQUIRE(preset_interactor.set_external_mmu(0, 4));
    REQUIRE(preset_interactor.external_mmu_slot_count(0) == 4u);
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == 4);
    REQUIRE(preset_interactor.selected_printer_preset().materials.size() == 4);
    REQUIRE(preset_interactor.material_presets().size() == 4);
    REQUIRE(printer_value("single_extruder_multi_material"));
    REQUIRE(!printer_value("enable_filament_ramming"));
    REQUIRE(printer_value("wiping_volumes_by_colour"));
    REQUIRE(preset_interactor.selected_printer_preset().print.config_box().items.opt("wipe_tower").get<bool>());

    // change the slot count: 16 slots (four 4-slot units), and clamping to 2..64
    REQUIRE(preset_interactor.set_external_mmu(0, 16));
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == 16);
    REQUIRE(preset_interactor.selected_printer_preset().materials.size() == 16);
    REQUIRE(preset_interactor.set_external_mmu(0, 100));
    REQUIRE(preset_interactor.external_mmu_slot_count(0) == 64u);
    REQUIRE(preset_interactor.set_external_mmu(0, 1));
    REQUIRE(preset_interactor.external_mmu_slot_count(0) == 2u);

    // edit and save the printer preset: the user's own values win over the unit's (Orca)
    preset_interactor.set_preset_value(
        Slic3r::Domain::FDMConfigLocation::Printer,
        0,
        "enable_filament_ramming",
        [](auto& p) { p.set(true); }
    );
    REQUIRE(printer_value("enable_filament_ramming"));
    preset_interactor.save_user_preset(Slic3r::Domain::Preset::PresetKind::FdmPrinter, 0);
    auto check_user_preset = [&]
    {
        const auto& item = printer_items.items().at(printer_items.selected_index());
        REQUIRE(item.name == preset_dialog_manager.new_name);
        REQUIRE(preset_interactor.external_mmu_slot_count(0) == 2u);
        REQUIRE(printer_value("enable_filament_ramming"));
        REQUIRE(printer_value("single_extruder_multi_material"));
    };
    check_user_preset();

    // switch to another printer and back to the saved preset, also after a reload
    auto select_user_preset = [&]
    {
        for (size_t i = 0, n = printer_items.items().size(); i < n; ++i) {
            const auto& item = printer_items.items().at(i);
            if (item.hw_printer_config_name == "Prusa CORE One 0.4 HF" && item.name == preset_dialog_manager.new_name) {
                preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
                return true;
            }
        }
        return false;
    };
    REQUIRE(select_printer("Prusa CORE One 0.6 HF"));
    REQUIRE(select_user_preset());
    check_user_preset();
    preset_interactor.load_preset_bundle(bundle_paths);
    REQUIRE(select_user_preset());
    check_user_preset();

    // detach
    REQUIRE(preset_interactor.set_external_mmu(0, std::nullopt));
    REQUIRE(!preset_interactor.external_mmu_slot_count(0).has_value());
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == 1);
    REQUIRE(preset_interactor.selected_printer_preset().materials.size() == 1);
    REQUIRE(printer_value("enable_filament_ramming"));

    // the sidebar's filament [+]/[-]: attach with 2 slots, step, detach below 2
    REQUIRE(!preset_interactor.change_external_mmu_slots(0, -1));
    REQUIRE(preset_interactor.change_external_mmu_slots(0, 1));
    REQUIRE(preset_interactor.external_mmu_slot_count(0) == 2u);
    REQUIRE(preset_interactor.change_external_mmu_slots(0, 1));
    REQUIRE(preset_interactor.external_mmu_slot_count(0) == 3u);
    REQUIRE(preset_interactor.change_external_mmu_slots(0, -1));
    REQUIRE(preset_interactor.change_external_mmu_slots(0, -1));
    REQUIRE(!preset_interactor.external_mmu_slot_count(0).has_value());
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == 1);
}

TEST_CASE("Moonraker filament lanes", "[ExternalMMU]")
{
    using namespace Slic3r::Biz::PrintHost;

    // as AFC publishes it (AFC_lane.py: "lane" is the T map index as a string)
    const std::string lane_data = R"({"result": {"namespace": "lane_data", "value": {
        "lane1": {"color": "#ff0000", "material": "PLA", "bed_temp": 60, "nozzle_temp": 210, "lane": "0"},
        "lane2": {"color": "00FF00", "material": "petg", "bed_temp": 80, "nozzle_temp": 240, "lane": "1"},
        "lane4": {"color": "", "material": "", "bed_temp": "", "nozzle_temp": "", "lane": "3"},
        "bad":   {"color": "#0000FF", "material": "ABS", "lane": ""}
    }}})";
    const auto lanes = parse_moonraker_lane_data(lane_data);
    REQUIRE(lanes.has_value());
    REQUIRE(lanes->size() == 3);
    std::map<int, FilamentLane> by_index;
    for (const auto& lane : *lanes)
        by_index[lane.index] = lane;
    REQUIRE(by_index.at(0).color == "#FF0000");
    REQUIRE(by_index.at(0).material == "PLA");
    REQUIRE(by_index.at(0).nozzle_temp == 210);
    REQUIRE(by_index.at(1).color == "#00FF00");
    REQUIRE(by_index.at(3).color.empty());
    REQUIRE(by_index.at(3).nozzle_temp == 0);

    REQUIRE(!parse_moonraker_lane_data(R"({"result": {"value": {}}})").has_value());
    REQUIRE(!parse_moonraker_lane_data("not json").has_value());

    const std::string happy_hare = R"({"result": {"status": {"mmu": {
        "num_gates": 4,
        "gate_status": [1, 0, 2, -1],
        "gate_material": ["PLA", "PLA", "TPU", "ABS"],
        "gate_color": ["ffffff", "000000", "#123456", "ff0000"],
        "gate_temperature": [200, 200, 230, 250]
    }}}})";
    const auto gates = parse_happy_hare_gates(happy_hare);
    REQUIRE(gates.has_value());
    REQUIRE(gates->size() == 2);
    REQUIRE(gates->at(0).index == 0);
    REQUIRE(gates->at(0).color == "#FFFFFF");
    REQUIRE(gates->at(1).index == 2);
    REQUIRE(gates->at(1).material == "TPU");
    REQUIRE(gates->at(1).nozzle_temp == 230);
    REQUIRE(!parse_happy_hare_gates(R"({"result": {"status": {"mmu": {}}}})").has_value());

    REQUIRE(normalize_lane_color("0x12ab34") == "12AB34FF");
    REQUIRE(normalize_lane_color("bad") == "00000000");
    REQUIRE(generic_filament_family(" pla-silk ") == "PLA Silk");
    REQUIRE(generic_filament_family("PET") == "PETG");
    REQUIRE(generic_filament_family("unobtainium").empty());
}

// The application's own preset bundle (resources/presets), which has generic filaments.
struct AppBundleProjectInteractorFixture : BaseProjectInteractorFixture
{
    const Slic3r::Biz::Preset::IO::BundlePaths bundle_paths{[] {
        auto paths = Slic3r::Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir());
        paths.app_bundle_path =
            (Tests::get_datadir() / ".." / ".." / ".." / ".." / "resources" / "presets").string();
        return paths;
    }()};

    AppBundleProjectInteractorFixture()
    {
        auto& preset_interactor = project_interactor.preset_interactor();
        preset_interactor.set_use_hw_config_short_name(false);
        preset_interactor.load_preset_bundle(bundle_paths);
    }

    ~AppBundleProjectInteractorFixture() override
    {
        fs::remove_all(bundle_paths.user_bundle_path);
    }
};

TEST_CASE_METHOD(AppBundleProjectInteractorFixture, "Flushing volumes", "[PresetInteractor][preset][ExternalMMU]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();
    bool found = false;
    for (size_t i = 0, n = printer_items.items().size(); i < n && !found; ++i) {
        const auto& item = printer_items.items().at(i);
        if (item.hw_printer_config_name.find("CORE One") == std::string::npos)
            continue;
        preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
        const auto& hw = preset_interactor.current_printer_config();
        found = hw.tool_count == 1 && hw.feeders.empty();
    }
    REQUIRE(found);
    REQUIRE(preset_interactor.set_external_mmu(0, 4));

    auto& settings          = project_interactor.project_settings_interactor();
    const auto cc_id        = project_interactor.selected_config_container_id();
    const auto by_colour    = settings.wiping_volumes_from_colours(cc_id);
    REQUIRE(by_colour.size() == 16);
    // the unit purges by colour (external unit built-in values), so the volumes in use are those
    REQUIRE(settings.wiping_volumes(cc_id) == by_colour);

    // a custom matrix is stored and used
    std::vector<double> custom(16, 100.);
    for (size_t i = 0; i < 4; ++i)
        custom[i * 4 + i] = 0.;
    settings.set_wiping_volumes(cc_id, custom);
    REQUIRE(settings.wiping_volumes(cc_id) == custom);

    // Orca: a new colour for slot 1 recalculates its row and column, the other pairs are kept
    settings.set_color_from_user(cc_id, 1, "#FF0000");
    const auto after = settings.wiping_volumes(cc_id);
    const auto fresh = settings.wiping_volumes_from_colours(cc_id);
    for (size_t from = 0; from < 4; ++from) {
        for (size_t to = 0; to < 4; ++to) {
            if (from == 1 || to == 1)
                REQUIRE(after[from * 4 + to] == fresh[from * 4 + to]);
            else
                REQUIRE(after[from * 4 + to] == custom[from * 4 + to]);
        }
    }
}

TEST_CASE_METHOD(AppBundleProjectInteractorFixture, "Default materials of an imported printer", "[PresetInteractor][preset]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();
    bool found = false;
    for (size_t i = 0, n = printer_items.items().size(); i < n && !found; ++i) {
        const auto& item = printer_items.items().at(i);
        if (item.hw_printer_config_name.find("Anycubic Kobra 3") == std::string::npos)
            continue;
        preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
        found = true;
    }
    if (!found)
        SKIP("the Orca import (resources/presets/orca-import) is not present");
    INFO(preset_interactor.current_printer_config().name);
    const auto names = preset_interactor.default_material_names();
    REQUIRE(!names.empty());
    // the names are filament presets offered for this printer
    const auto& materials = preset_interactor.material_presets().at(0).items();
    bool any_offered = false;
    for (size_t i = 0, n = materials.size(); i < n; ++i)
        any_offered |= std::ranges::find(names, materials.at(i).name) != names.end();
    REQUIRE(any_offered);
}

TEST_CASE_METHOD(AppBundleProjectInteractorFixture, "Filament sync from printer lanes", "[PresetInteractor][preset][ExternalMMU]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();
    // a single-tool printer without a feeder (e.g. Prusa CORE One)
    bool found = false;
    for (size_t i = 0, n = printer_items.items().size(); i < n && !found; ++i) {
        const auto& item = printer_items.items().at(i);
        if (item.hw_printer_config_name.find("CORE One") == std::string::npos)
            continue;
        preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
        const auto& hw = preset_interactor.current_printer_config();
        found = hw.tool_count == 1 && hw.feeders.empty();
    }
    REQUIRE(found);
    INFO(preset_interactor.current_printer_config().name);

    using Slic3r::Biz::PrintHost::FilamentLane;
    const std::vector<FilamentLane> lanes{
        {.index = 0, .material = "PETG", .color = "#FF0000"},
        {.index = 2, .material = "PLA", .color = "#00FF00"},
    };
    REQUIRE(project_interactor.sync_filaments_from_lanes(0, lanes));
    REQUIRE(preset_interactor.external_mmu_slot_count(0) == 3u);
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == 3);

    auto selected_material_name = [&](size_t slot)
    {
        const auto& list = preset_interactor.material_presets().at(slot);
        return list.items().at(list.selected_index()).name;
    };
    REQUIRE(selected_material_name(0) == "Generic PETG");
    REQUIRE(selected_material_name(2) == "Generic PLA");

    const auto colors = project_interactor.project_settings_interactor().get_colors(
        project_interactor.selected_config_container_id()
    );
    REQUIRE(colors.size() == 3);
    REQUIRE(colors[0].r() == 1.f);
    REQUIRE(colors[0].g() == 0.f);
    REQUIRE(colors[2].g() == 1.f);
    REQUIRE(colors[2].r() == 0.f);

    REQUIRE(!project_interactor.sync_filaments_from_lanes(0, {}));
}

TEST_CASE_METHOD(AppBundleProjectInteractorFixture, "External multi-material unit does not pick up Prusa MMU presets", "[PresetInteractor][preset][ExternalMMU]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();
    // Prusa presets select MMU variants with feeder.base_model=~/.*MMU.*/ (e.g. "*PLA* @MK3.5 0.4 MMU")
    bool found = false;
    for (size_t i = 0, n = printer_items.items().size(); i < n && !found; ++i) {
        const auto& item = printer_items.items().at(i);
        if (item.hw_printer_config_name.find("MK3.5") == std::string::npos ||
            item.hw_printer_config_name.find("0.4") == std::string::npos)
            continue;
        preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
        const auto& hw = preset_interactor.current_printer_config();
        found = hw.tool_count == 1 && hw.feeders.empty();
    }
    REQUIRE(found);
    INFO(preset_interactor.current_printer_config().name);

    auto material_speed = [&]
    {
        return preset_interactor.selected_printer_preset().materials.at(0).config_box().items
            .opt("filament_max_volumetric_speed").get<double>();
    };
    const double speed = material_speed();
    REQUIRE(preset_interactor.set_external_mmu(0, 4));
    REQUIRE(material_speed() == speed);
}

TEST_CASE_METHOD(AppBundleProjectInteractorFixture, "External multi-material unit next to a vendor feeder", "[PresetInteractor][preset][ExternalMMU]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& printer_items = preset_interactor.printer_presets();

    // a tool with a vendor feeder (e.g. a Prusa printer with MMU3 on tool 0) is left as
    // it is; the other tools of that printer can still get a unit
    bool mmu_printer_found = false;
    for (size_t i = 0, n = printer_items.items().size(); i < n && !mmu_printer_found; ++i) {
        const auto& item = printer_items.items().at(i);
        preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
        mmu_printer_found = !preset_interactor.current_printer_config().feeders.empty();
    }
    REQUIRE(mmu_printer_found);
    const size_t mmu_slots = preset_interactor.current_printer_config().material_slot_count();
    REQUIRE(!preset_interactor.can_attach_external_mmu(0));
    REQUIRE(!preset_interactor.set_external_mmu(0, 4));
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == mmu_slots);
    if (preset_interactor.current_printer_config().tool_count > 1) {
        REQUIRE(preset_interactor.set_external_mmu(1, 4));
        REQUIRE(preset_interactor.current_printer_config().material_slot_count() == mmu_slots + 3);
        REQUIRE(preset_interactor.set_external_mmu(1, std::nullopt));
    }


    // a unit on one tool of a multi-tool printer, next to tools without a feeder
    bool multi_tool_found = false;
    for (size_t i = 0, n = printer_items.items().size(); i < n && !multi_tool_found; ++i) {
        const auto& item = printer_items.items().at(i);
        preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id);
        const auto& hw = preset_interactor.current_printer_config();
        multi_tool_found = hw.tool_count > 1 && hw.feeders.empty();
    }
    REQUIRE(multi_tool_found);
    const size_t tool_count = preset_interactor.current_printer_config().tool_count;
    REQUIRE(preset_interactor.can_attach_external_mmu(1));
    REQUIRE(!preset_interactor.can_attach_external_mmu(tool_count));
    REQUIRE(preset_interactor.set_external_mmu(1, 4));
    REQUIRE(preset_interactor.external_mmu_slot_count(1) == 4u);
    REQUIRE(!preset_interactor.external_mmu_slot_count(0).has_value());
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == tool_count + 3);
    REQUIRE(preset_interactor.selected_printer_preset().materials.size() == tool_count + 3);
    // slots 1..4 are fed by tool 1
    const auto& hw = preset_interactor.current_printer_config();
    for (size_t slot = 0; slot < hw.material_slot_count(); ++slot) {
        const size_t expected_tool = slot < 1 ? slot : (slot < 5 ? 1 : slot - 3);
        REQUIRE(Slic3r::Domain::Preset::MaterialIterator::from_slot_index(hw, slot).tool_index() == expected_tool);
    }
    REQUIRE(preset_interactor.set_external_mmu(1, std::nullopt));
    REQUIRE(preset_interactor.current_printer_config().material_slot_count() == tool_count);
}

TEST_CASE_METHOD(LoadedProjectInteractorFixture, "Delete a user print preset", "[PresetInteractor][preset]")
{
    project_interactor.new_project();
    auto& preset_interactor = project_interactor.preset_interactor();
    const auto& prints = preset_interactor.print_presets();
    auto selected_print = [&]() -> const auto& { return prints.items().at(prints.selected_index()); };
    auto has_print = [&](const std::string& name) { return find_by_name(prints.items(), name) != nullptr; };

    // a system preset is not deleted
    REQUIRE(selected_print().origin != Slic3r::Domain::Preset::PresetOrigin::User);
    REQUIRE(!preset_interactor.delete_selected_user_preset(Slic3r::Domain::Preset::PresetKind::FdmPrint));

    // save a user preset, then delete it (Orca's "Delete preset")
    preset_dialog_manager.new_name = "deleted print";
    preset_interactor.set_preset_value(
        Slic3r::Domain::FDMConfigLocation::Print, 0, "perimeters", [](auto& p) { p.set(7); }
    );
    preset_interactor.save_user_preset(Slic3r::Domain::Preset::PresetKind::FdmPrint, 0);
    REQUIRE(selected_print().name == "deleted print");
    const auto path = preset_interactor.selected_user_preset_path(
        Slic3r::Domain::Preset::PresetKind::FdmPrint, "deleted print"
    );
    REQUIRE(fs::exists(path));

    REQUIRE(preset_interactor.delete_selected_user_preset(Slic3r::Domain::Preset::PresetKind::FdmPrint));
    REQUIRE(!fs::exists(path));
    REQUIRE(!has_print("deleted print"));
    REQUIRE(selected_print().origin != Slic3r::Domain::Preset::PresetOrigin::User);
    REQUIRE(preset_interactor.selected_printer_preset().print.origin != Slic3r::Domain::Preset::PresetOrigin::User);

    // and it stays deleted after a reload
    preset_interactor.load_preset_bundle(bundle_paths);
    REQUIRE(!has_print("deleted print"));

    // filament and printer presets the same way
    using Slic3r::Domain::Preset::PresetKind;
    using Slic3r::Domain::Preset::PresetOrigin;
    preset_dialog_manager.new_name = "deleted filament";
    preset_interactor.set_preset_value(
        Slic3r::Domain::FDMConfigLocation::Filament, 0, "filament_notes",
        [](auto& p) { p.set(std::string{"x"}); }
    );
    preset_interactor.save_user_preset(PresetKind::FdmMaterial, 0);
    REQUIRE(preset_interactor.selected_printer_preset().materials[0].origin == PresetOrigin::User);
    REQUIRE(preset_interactor.delete_selected_user_preset(PresetKind::FdmMaterial, 0));
    REQUIRE(preset_interactor.selected_printer_preset().materials[0].origin != PresetOrigin::User);
    REQUIRE(find_by_name(preset_interactor.material_presets().at(0).items(), std::string{"deleted filament"}) == nullptr);

    preset_dialog_manager.new_name = "deleted printer";
    preset_interactor.set_preset_value(
        Slic3r::Domain::FDMConfigLocation::Printer, 0, "printer_notes",
        [](auto& p) { p.set(std::string{"x"}); }
    );
    preset_interactor.save_user_preset(PresetKind::FdmPrinter, 0);
    REQUIRE(preset_interactor.selected_printer_preset().printer.origin == PresetOrigin::User);
    REQUIRE(preset_interactor.delete_selected_user_preset(PresetKind::FdmPrinter));
    REQUIRE(preset_interactor.selected_printer_preset().printer.origin != PresetOrigin::User);
    REQUIRE(find_by_name(preset_interactor.printer_presets().items(), std::string{"deleted printer"}) == nullptr);
}

TEST_CASE_METHOD(BaseProjectInteractorFixture, "Only added printers are built", "[PresetInteractor][preset]")
{
    const Slic3r::Biz::Preset::IO::BundlePaths bundle_paths{
        Slic3r::Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
    };
    auto& preset_interactor = project_interactor.preset_interactor();
    preset_interactor.set_use_hw_config_short_name(false);

    // everything (nullopt): every template of every vendor
    preset_interactor.load_preset_bundle(bundle_paths);
    const auto& bundle = project_interactor.workbench().preset_bundle();
    size_t templates = 0;
    for (const auto& vendor_bundle : bundle.vendor_bundles | std::views::values)
        templates += vendor_bundle.vendor_data.printer_configs.size();
    REQUIRE(templates > 1);
    REQUIRE(bundle.printer_configs.size() == templates);

    // one added printer: only it is built and evaluated
    // copies: the reload replaces the bundle
    const std::string vendor_id = bundle.vendor_bundles.begin()->first;
    const std::string template_id = bundle.vendor_bundles.begin()->second.vendor_data.printer_configs.front().id;
    preset_interactor.set_added_printers(std::set<std::string>{
        Slic3r::Biz::Preset::PresetInteractor::added_printer_key(vendor_id, template_id)});
    preset_interactor.load_preset_bundle(bundle_paths);
    const auto& bundle2 = project_interactor.workbench().preset_bundle();
    REQUIRE(bundle2.printer_configs.size() == 1);
    const auto& config = bundle2.printer_configs.begin()->second;
    REQUIRE(config.template_id == template_id);
    REQUIRE(!preset_interactor.system_printer_preset_ids(config.id).empty());
    REQUIRE(preset_interactor.added_printers_with_presets(
                {preset_interactor.system_printer_preset_ids(config.id).front()})
            == std::set<std::string>{Slic3r::Biz::Preset::PresetInteractor::added_printer_key(vendor_id, template_id)});

    // nothing added (first start): one printer so a project has one
    preset_interactor.set_added_printers(std::set<std::string>{});
    preset_interactor.load_preset_bundle(bundle_paths);
    REQUIRE(project_interactor.workbench().preset_bundle().printer_configs.size() == 1);
}
