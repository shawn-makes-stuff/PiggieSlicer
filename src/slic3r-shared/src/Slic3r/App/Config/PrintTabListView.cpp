#include "Slic3r/App/Config/PrintTabListView.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <Slic3r/Domain/OrcaLayout.hpp>

#include <algorithm>
#include <optional>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

using Mode = Domain::ConfigItemDef::Mode;

namespace {
// Read once from the app config, then kept here: the filters ask for every row.
std::optional<Mode> current_mode;
} // namespace

Mode print_settings_mode()
{
    if (!current_mode) {
        const int saved = AppServices::instance().app_config().get<int>("print_settings_mode");
        current_mode    = Mode(std::clamp(saved, int(Mode::Simple), int(Mode::Expert)));
    }
    return *current_mode;
}

void set_print_settings_mode(Mode mode)
{
    current_mode = mode;
    AppServices::instance().app_config().set<int>("print_settings_mode", int(mode));
}

bool is_shown_in_mode(const Domain::ConfigItemDef& def)
{
    return def.mode <= print_settings_mode();
}

std::vector<PrintTab> print_tabs(Domain::PrinterTechnology technology)
{
    using Category = Domain::ConfigItemDef::Category;
    if (technology == Domain::PrinterTechnology::SLA) {
        // Orca has no SLA: one tab per category
        std::vector<PrintTab> tabs;
        for (Category category :
             {Category::Print_LayersSurfaces, Category::Print_Supports, Category::Print_Pad,
              Category::Print_Hollowing, Category::Print_OutputOptions, Category::Print_Notes})
        {
            tabs.push_back(
                {Biz::_u8(Domain::ConfigItemDef::translate_category(category, technology)), {category}}
            );
        }
        return tabs;
    }
    // Orca's pages, with its groups and order (Domain/OrcaLayout.hpp)
    std::vector<PrintTab> tabs;
    const std::span<const std::string_view> pages = Domain::orca_layout_pages();
    for (size_t page = 0; page < pages.size(); ++page)
        tabs.push_back({Biz::_u8(std::string(pages[page])), {}, int(page)});
    return tabs;
}

PrintTabListView::PrintTabListView(
    const PrintTab& tab,
    Biz::PrintToolConfigBoxInteractor& cbi,
    Biz::IConfigBoxSetter& cbi_setter,
    Biz::ProjectInteractor& project_interactor
) :
    ListView<PrintToolSubcategoryItem, Biz::PrintToolItem, PrintTabGroupFactory, ScrollArea>(
        PrintTabGroupFactory{cbi, cbi_setter, project_interactor, tab.orca_page >= 0}
    ),
    m_categories(tab.categories),
    m_orca_page(tab.orca_page),
    m_filter(std::make_shared<Biz::ObservableListSortFilter<Biz::PrintToolItem, uint32_t>>())
{
    set_object_name("PrintTabListView");
    set_orientation(Orientation::Vertical);
    set_gap(0);
    set_flex_grow(1);
    set_min_height(100);

    // in Orca's layout a group whose rows are all hidden by the visibility disappears with them
    m_filter->set_filter_fn(
        [this](const Biz::PrintToolItem& item)
        {
            const Domain::ConfigItemDef& def = item.print_item->def();
            return contains(def) && (m_orca_page < 0 || is_shown_in_mode(def));
        }
    );
    m_filter->set_group_by_fn(
        [this](const Biz::PrintToolItem& item, std::unordered_set<uint32_t>& seen_keys)
        { return !seen_keys.insert(group_key(item.print_item->def())).second; }
    );
    m_filter->set_sort_fn(
        [this](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs) -> int
        {
            const Domain::ConfigItemDef& l = lhs.print_item->def();
            const Domain::ConfigItemDef& r = rhs.print_item->def();
            if (m_orca_page >= 0)
                return l.orca_group < r.orca_group;
            const size_t lc = category_order(l.category);
            const size_t rc = category_order(r.category);
            if (lc != rc)
                return lc < rc;
            return l.option_group < r.option_group;
        }
    );
    m_filter->set_source_model(cbi.observable_list());

    set_source_list(m_filter.get());
}

bool PrintTabListView::contains(const Domain::ConfigItemDef& def) const
{
    if (m_orca_page >= 0)
        return def.orca_group != 0 && Domain::orca_layout_groups()[def.orca_group].page == m_orca_page;
    return std::ranges::find(m_categories, def.category) != m_categories.end();
}

uint32_t PrintTabListView::group_key(const Domain::ConfigItemDef& def) const
{
    if (m_orca_page >= 0)
        return def.orca_group;
    // as PrintToolSubcategoryItem filters on both
    return (uint32_t(def.category) << 16) | uint32_t(def.option_group);
}

void PrintTabListView::navigate_to_item(const Domain::ConfigItem* config_item)
{
    const uint32_t key = group_key(config_item->def());
    for (size_t row_index = 0; row_index < m_filter->size(); ++row_index) {
        if (group_key(m_filter->at(row_index).print_item->def()) == key) {
            PrintToolSubcategoryItem* found_item = item_at(row_index);
            found_item->navigate_to_item(config_item);
            scroll_at_item(found_item);
            break;
        }
    }
}

void PrintTabListView::clear_navigation()
{
    for (size_t row_index = 0; row_index < m_filter->size(); ++row_index)
        item_at(row_index)->clear_navigation();
}

void PrintTabListView::refresh_mode()
{
    m_filter->invalidate();
    for (size_t row_index = 0; row_index < m_filter->size(); ++row_index)
        item_at(row_index)->refresh_mode();
}

size_t PrintTabListView::category_order(Domain::ConfigItemDef::Category category) const
{
    return size_t(std::ranges::find(m_categories, category) - m_categories.begin());
}

} // namespace Slic3r::App
