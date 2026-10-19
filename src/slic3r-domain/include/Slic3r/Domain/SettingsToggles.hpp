#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/GCodeFlavor.hpp"

namespace Slic3r::Domain {

/// What the print settings rules need to know of the printer.
struct PrinterKind
{
    size_t      nozzles{1};   ///< tools (hotends)
    size_t      filaments{1}; ///< material slots of all the tools (an external unit's slots too)
    bool        single_extruder_multi_material{false};
    bool        purge_in_prime_tower{false};
    GCodeFlavor gcode_flavor{GCodeFlavor::gcfMarlinFirmware};
    bool        junction_deviation{false}; ///< Marlin with machine_max_junction_deviation > 0
};

/// The print settings a rule greys out (their value has no effect now) or hides (they cannot apply).
struct SettingsToggles
{
    std::unordered_set<std::string> disabled;
    std::unordered_set<std::string> hidden;

    bool operator==(const SettingsToggles&) const = default;
};

/// The current value of a print setting, nullptr if there is none.
using ConfigValueGetter = std::function<const ConfigValue*(const std::string& key)>;

/// Prusa's (3.0 ConfigManipulation::toggle_print_fff_options) and Orca's (ConfigManipulation::
/// toggle_print_fff_options, v2.5.0-alpha) rules on the print settings, with Orca's keys translated to ours,
/// and the printer's: the multi-material settings only with more than one filament, the multi-nozzle ones only
/// with more than one nozzle.
SettingsToggles print_settings_toggles(const ConfigValueGetter& value_of, const PrinterKind& printer);

/// The print settings to change after `changed_key` was set, so that no two conflicting settings are on (Orca's
/// ConfigManipulation::update_print_fff_config / toggle_print_fff_options value changes, Prusa's exclusive travel
/// options); each value has the type of the setting's current value.
std::vector<std::pair<std::string, ConfigValue>> print_settings_fixes(const std::string& changed_key, const ConfigValueGetter& value_of);

} // namespace Slic3r::Domain
