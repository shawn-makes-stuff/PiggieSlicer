#pragma once

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"

#include "Slic3r/App/Config/PrintToolSubcategoryListView.hpp"

#include <string>
#include <vector>

#include "Slic3r/Domain/SettingsToggles.hpp"

namespace Slic3r::App {

/// One tab of the sidebar's Process section: a name and what it shows, Orca's page (FDM,
/// Domain/OrcaLayout.hpp) or, with orca_page -1, the print categories (SLA).
struct PrintTab
{
    std::string name;
    std::vector<Domain::ConfigItemDef::Category> categories;
    int orca_page{-1};
};

/// The settings visibility, Simple or Advanced (all, Orca's Expert settings too), saved in the app config.
Domain::ConfigItemDef::Mode print_settings_mode();
void set_print_settings_mode(Domain::ConfigItemDef::Mode mode);
/// Whether the current visibility shows the setting.
bool is_shown_in_mode(const Domain::ConfigItemDef& def);

/// The print settings the current values and the printer grey out / hide (Biz PresetInteractor::
/// print_settings_toggles), shared by the sidebars. Returns whether they changed.
bool set_settings_toggles(Domain::SettingsToggles toggles);
/// Counts the changes, for the sidebars to refresh once each.
size_t settings_toggles_version();
bool is_setting_hidden(const std::string& key);
bool is_setting_disabled(const std::string& key);

/**
 * @brief Orca's print tabs (Tab.cpp TabPrint::build: Quality, Strength, Speed, Support,
 * Multimaterial, Others); SLA keeps one tab per print category.
 */
std::vector<PrintTab> print_tabs(Domain::PrinterTechnology technology);

/**
 * @brief All groups of a tab in one scroll area: Orca's groups of its page in Orca's order, or the
 * option groups of its categories in category order (the same group items as
 * PrintToolSubcategoryListView, which shows one category).
 */
class PrintTabListView :
    public Yoga::ListView<
        PrintToolSubcategoryItem,
        Biz::PrintToolItem,
        PrintTabGroupFactory,
        Yoga::ScrollArea>
{
public:
    PrintTabListView(
        const PrintTab& tab,
        Biz::PrintToolConfigBoxInteractor& cbi,
        Biz::IConfigBoxSetter& cbi_setter,
        Biz::ProjectInteractor& project_interactor
    );

    bool contains(const Domain::ConfigItemDef& def) const;
    /// Scrolls to and highlights the item (as PrintToolSubcategoryListView does in the dialog).
    void navigate_to_item(const Domain::ConfigItem* config_item);
    void clear_navigation();
    /// Re-filters the rows after the settings visibility changed.
    void refresh_mode();
    /// No setting shown (Orca hides such a page).
    bool is_empty() const { return m_filter->size() == 0; }

private:
    /// The group a setting is shown in: Orca's group, or its category and option group.
    uint32_t group_key(const Domain::ConfigItemDef& def) const;
    size_t category_order(Domain::ConfigItemDef::Category category) const;

    std::vector<Domain::ConfigItemDef::Category> m_categories;
    int m_orca_page{-1};
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::PrintToolItem, uint32_t>> m_filter;
};

} // namespace Slic3r::App
