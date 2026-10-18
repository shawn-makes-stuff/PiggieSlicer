#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include <span>
#include <string_view>

namespace Slic3r::Domain {

// Orca's Process tab layout (Tab.cpp TabPrint::build, v2.5.0-alpha) for the sidebar's print settings.
// Generated with its table in OrcaLayout.cpp by tools/orca_import/gen_orca_layout.py.

// A sidebar group: its page (index into orca_layout_pages()) and Orca's group name, or, for the
// settings Orca does not show on its Process tab, 3.0's option group (its name is used).
struct OrcaLayoutGroup
{
    int page;
    std::string_view label;
    ConfigItemDef::OptionGroup option_group;
};

// Orca's page names, untranslated: Quality, Strength, Speed, Support, Multimaterial, Others.
std::span<const std::string_view> orca_layout_pages();

// The groups in display order; index 0 is unused (ConfigItemDef::orca_group 0 = not placed).
std::span<const OrcaLayoutGroup> orca_layout_groups();

// Sets orca_group, orca_order and mode of the FDM print settings.
void apply_orca_layout(ConfigDefinitions& defs);

} // namespace Slic3r::Domain
