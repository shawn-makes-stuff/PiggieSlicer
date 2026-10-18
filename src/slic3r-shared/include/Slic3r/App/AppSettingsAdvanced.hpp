#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::App {

struct AppSettingsAdvanced
{
    // This should be set of pairs, but due to hw_printer_config_id being unique each run, we are only storing id
    // using PrinterFavoritePresets  = std::set<std::pair<std::string, std::string>>;
    using PrinterFavoritePresets  = std::set<std::string>;
    using MaterialFavoritePresets = std::set<std::string>;
    using RecentProjects          = std::vector<std::string>;

    void toggle_printer_favorite_preset(const std::string& id, const std::string& hw_config_id);
    void toggle_material_favorite_preset(const std::string& id);
    void push_recent_project(const std::string& recent_project);

    bool
    contains_printer_favorite_preset(const std::string& id, const std::string& hw_config_id) const;
    bool contains_material_favorite_preset(const std::string& id) const;

    PrinterFavoritePresets printer_favorite_presets;
    /// The printers the user added (PresetInteractor::added_printer_key: vendor / template); only
    /// these are built at start-up. Not set in settings from before the list existed (migration).
    std::optional<std::set<std::string>> added_printers;
    MaterialFavoritePresets material_favorite_presets;
    RecentProjects recent_projects;
};

void to_json(
    nlohmann::ordered_json& json_value,
    const Slic3r::App::AppSettingsAdvanced& app_settings_advanced
);
void from_json(
    const nlohmann::ordered_json& json_value,
    Slic3r::App::AppSettingsAdvanced& app_settings_advanced
);

} // namespace Slic3r::App
