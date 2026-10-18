#include "Slic3r/App/SidebarPrint.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/PrintSettingsDialog.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"

#include "Slic3r/App/Config/PrintTabListView.hpp"
#include "Slic3r/App/Config/PrintToolFavoritesItem.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"

#include <Slic3r/Log.hpp>

#include <imgui/imgui.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Render;

namespace Slic3r::App {

namespace {

// Global | Objects switch and the tab strip: the settings dialog's tab shape
LayoutButton* add_tab_button(Item* parent, ButtonGroup& group, const std::string& label, Render::Icon icon)
{
    LayoutButton* button = parent->emplace_back<LayoutButton>(label, icon);
    button->set_checkable(true);
    button->set_content_padding({10, 3});
    button->set_background_color(Platform::Color::WindowBgAlternate);
    // selected: the buttons' default checked colour, the subdued pink of the left bar's tabs
    button->set_flex_shrink(0);
    group.insert_button(button);
    return button;
}

LayoutButton* add_icon_button(Item* parent, Render::Icon icon, const std::string& tooltip)
{
    LayoutButton* button = parent->emplace_back<LayoutButton>(std::string{}, icon, tooltip);
    button->set_content_padding({});
    button->set_width(24);
    button->set_height(24);
    button->set_self_align(YGAlignCenter);
    button->set_flex_shrink(0);
    button->set_background_color(Platform::Color::ButtonTransparent);
    return button;
}

} // namespace

SidebarPrint::SidebarPrint(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Item(),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    // kept for "navigate to setting" (search); the settings are edited inline
    m_print_settings_dialog = emplace_back<PrintSettingsDialog>(project_interactor, m_navigator);

    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_flex_grow(1);

    // Orca: [icon] Process 18 [Global | Objects] ...... (ParamsPanel.cpp 437-446)
    m_section = emplace_back<CollapsibleWindow>(Biz::_u8L("Process"), "SidebarPrint");
    m_section->collapsible_window_callbacks().collapsed_changed = [this](bool) { update_visibility(); };
    m_section->set_flex_grow(1);
    // the Objects tab's object list and object settings are windows of their own drawn over this
    // one: a click here (the tabs) must not bring it in front of them
    m_section->set_flags(m_section->flags() | ImGuiWindowFlags_NoBringToFrontOnFocus);
    m_print_settings_dialog->attach_to_item(m_section, Position::Right, 20);

    Item* mode_switch = m_section->emplace_after_label<Item>();
    mode_switch->set_orientation(Orientation::Horizontal);
    mode_switch->set_margin({13.f, 0.f, 0.f, 0.f}); // 5 header gap + 13 = Orca's wide spacing 18
    m_global_button  = add_tab_button(mode_switch, m_mode_group, Biz::_u8L("Global"), Render::Icon::None);
    m_objects_button = add_tab_button(mode_switch, m_mode_group, Biz::_u8L("Objects"), Render::Icon::None);
    m_global_button->set_draw_flags(ImDrawFlags_RoundCornersLeft);
    m_objects_button->set_draw_flags(ImDrawFlags_RoundCornersRight);
    m_global_button->set_checked(true);
    m_mode_group.set_always_checked(true);
    m_mode_group.callbacks().action = [this](AbstractButton* button)
    { set_objects_mode(button == m_objects_button); };

    // Orca: [advanced] 5 [mode] in the Process title (Tab.cpp: "Cycle settings visibility")
    m_settings_mode_button = mode_switch->emplace_back<LayoutButton>(
        std::string{}, Render::Icon::None, Biz::_u8L("Cycle settings visibility")
    );
    m_settings_mode_button->set_content_padding({10, 3});
    m_settings_mode_button->set_margin({13.f, 0.f, 0.f, 0.f});
    m_settings_mode_button->set_flex_shrink(0);
    m_settings_mode_button->callbacks().action = [this]
    {
        using Mode = Domain::ConfigItemDef::Mode;
        set_settings_mode(Mode((int(print_settings_mode()) + 1) % (int(Mode::Expert) + 1)));
    };

    m_global_content = m_section->content()->emplace_back<Item>();
    m_global_content->set_orientation(Orientation::Vertical);
    m_global_content->set_gap(9_fpx); // Orca: content vertical margin
    m_global_content->set_flex_grow(1);
    m_section->content()->set_flex_grow(1);

    // Orca preset row: [print preset combo ......] [undo-to-system] 10 [save] (Tab.cpp 457-476);
    // compare is in Orca's Process title, here next to save
    Item* preset_row = m_global_content->emplace_back<Item>();
    preset_row->set_orientation(Orientation::Horizontal);
    preset_row->set_gap(5_fpx);
    preset_row->set_flex_shrink(0);

    m_combo_print = preset_row->emplace_back<ComboBoxListViewSelection<Biz::Preset::PresetItem>>();
    m_combo_print->set_get_name_fn(
        [](const Biz::Preset::PresetItem* item) -> std::string { return item->ui_preset_name(); }
    );
    m_combo_print->set_source_list(&m_project_interactor.preset_interactor().print_presets());
    m_combo_print->set_flex_grow(1);
    m_combo_print->callbacks().selection_changed = [this](int print_index)
    {
        if (print_index >= 0) {
            auto& preset_interactor = m_project_interactor.preset_interactor();
            const std::string& preset_id =
                preset_interactor.print_presets().items().at(print_index).id;
            if (Biz::Preset::PresetSelectionCheck::can_select_print_preset(
                    preset_interactor,
                    preset_id
                ))
            {
                preset_interactor.select_print_preset(preset_id, true);
                m_last_selected_index = print_index;
            } else {
                m_combo_print->set_current_index(m_last_selected_index);
            }
        }
    };

    m_revert_button = add_icon_button(preset_row, Render::Icon::UndoGizmo, Biz::_u8L("Revert changes"));
    m_revert_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->callbacks().action = [this]
    {
        auto& preset_interactor = m_project_interactor.preset_interactor();
        preset_interactor.discard_selected_print_preset_changes();
        size_t tool_index = preset_interactor.tool_items().size();
        while (tool_index > 0)
            preset_interactor.discard_selected_tool_print_preset_changes(--tool_index);
    };

    LayoutButton* save_button =
        add_icon_button(preset_row, Render::Icon::ExportToLocal, Biz::_u8L("Save preset"));
    save_button->set_margin({5.f, 0.f, 0.f, 0.f}); // 5 gap + 5 = Orca's icon spacing 10
    save_button->callbacks().action = [this]
    { m_project_interactor.preset_interactor().save_user_tool_print_presets(); };

    // Orca's delete button (Tab.cpp: shown for the user's own presets)
    m_delete_button = add_icon_button(preset_row, Render::Icon::DeleteBtnIcon, Biz::_u8L("Delete preset"));
    m_delete_button->set_margin({5.f, 0.f, 0.f, 0.f});
    m_delete_button->callbacks().action = [this]
    {
        App::AppServices::instance().dialog_manager().show_yesno_dialog(
            Biz::_u8L("Delete Preset"),
            Biz::_u8L("Are you sure you want to delete the selected preset?"),
            [this](bool yes)
            {
                if (yes)
                    m_project_interactor.preset_interactor().delete_selected_user_preset(
                        Domain::Preset::PresetKind::FdmPrint
                    );
            }
        );
    };

    LayoutButton* compare_button =
        add_icon_button(preset_row, Render::Icon::Compare, Biz::_u8L("Compare presets"));
    compare_button->set_margin({5.f, 0.f, 0.f, 0.f});
    compare_button->callbacks().action = [this]
    {
        App::AppServices::instance().dialog_manager().show_diff_dialog(
            m_project_interactor.preset_interactor(),
            Domain::Preset::PresetKind::FdmPrint
        );
    };
    refresh_print_combobox_label_color();

    // per-tool print presets of multi-tool printers (PrusaSlicer 3.0)
    m_tool_head_list_view = m_global_content->emplace_back<ToolHeadListView>(
        Yoga::ViewFactory<
            SidebarToolHeadRow,
            Biz::Preset::PresetItemObservableList,
            Biz::ProjectInteractor&>{m_project_interactor}
    );
    m_tool_head_list_view->set_orientation(Orientation::Vertical);
    m_tool_head_list_view->set_gap(5_fpx);
    m_tool_head_list_view->set_flex_shrink(0);
    m_tool_head_list_view->set_source_list(&m_project_interactor.preset_interactor().tool_presets());

    // Orca: tab strip (Quality | Strength | Speed | Support | Multimaterial | Others) on top of the
    // page, one row across the width, in the settings dialog's tab look (Dialog::append_tab)
    Item* tabs_and_page = m_global_content->emplace_back<Item>();
    tabs_and_page->set_orientation(Orientation::Vertical);
    tabs_and_page->set_gap(8_fpx); // settings and scrollbar clear of the tab row
    tabs_and_page->set_flex_grow(1);

    m_tab_strip = tabs_and_page->emplace_back<Item>();
    m_tab_strip->set_orientation(Orientation::Horizontal);
    m_tab_strip->set_gap(0);
    m_tab_strip->set_flex_shrink(0);

    m_tab_pages = tabs_and_page->emplace_back<StackLayout>();
    m_tab_pages->set_orientation(Orientation::Vertical);
    m_tab_pages->set_flex_grow(1);

    m_tab_group.set_always_checked(true);
    m_tab_group.callbacks().action = [this](AbstractButton* button)
    {
        const auto it = std::ranges::find(m_tab_buttons, button);
        if (it != m_tab_buttons.end())
            select_tab(size_t(it - m_tab_buttons.begin()));
    };
    create_tabs();

    update_tools_visibility();
    refresh_tools_comboboxes_label_colors();
    update_visibility();
}

void SidebarPrint::create_tabs()
{
    for (LayoutButton* button : m_tab_buttons) {
        m_tab_group.remove_button(button);
        m_tab_strip->remove(button);
    }
    m_tab_buttons.clear();
    while (m_tab_pages->object_count() > 0)
        m_tab_pages->remove(m_tab_pages->get_item(0));

    m_tabs_technology = m_project_interactor.selected_config_container().print_technology();
    auto& preset_interactor = m_project_interactor.preset_interactor();
    // first tab: the settings starred as favourites (PrusaSlicer 3.0's sidebar list; not in Orca)
    LayoutButton* favorites_button = add_tab_button(m_tab_strip, m_tab_group, std::string{}, Render::Icon::StarSolid);
    favorites_button->set_tooltip(Biz::_u8L("Favorites"));
    favorites_button->set_content_padding({8, 4});
    favorites_button->set_draw_flags(ImDrawFlags_RoundCornersLeft);
    m_tab_buttons.push_back(favorites_button);
    ScrollArea* favorites_page = m_tab_pages->emplace_back<ScrollArea>();
    favorites_page->set_orientation(Orientation::Vertical);
    favorites_page->set_flex_grow(1);
    favorites_page->emplace_back<PrintToolFavoritesItem>(m_project_interactor);

    const std::vector<PrintTab> tabs = print_tabs(m_tabs_technology);
    for (const PrintTab& tab : tabs) {
        LayoutButton* button = add_tab_button(m_tab_strip, m_tab_group, tab.name, Render::Icon::None);
        button->set_flex_grow(1);
        button->set_flex_shrink(1);
        button->set_content_padding({6, 4});
        if (m_tab_buttons.size() == tabs.size())
            button->set_draw_flags(ImDrawFlags_RoundCornersRight);
        else
            button->set_rounding(0);
        m_tab_buttons.push_back(button);
        m_tab_pages->emplace_back<PrintTabListView>(
            tab,
            preset_interactor.print_tool_cbi(),
            preset_interactor,
            m_project_interactor
        );
    }
    select_tab(0);
    set_settings_mode(print_settings_mode());
}

void SidebarPrint::set_settings_mode(Domain::ConfigItemDef::Mode mode)
{
    using Mode = Domain::ConfigItemDef::Mode;
    if (mode != print_settings_mode())
        set_print_settings_mode(mode);
    m_settings_mode_button->set_label(
        mode == Mode::Simple ? Biz::_u8L("Simple") : mode == Mode::Advanced ? Biz::_u8L("Advanced") : Biz::_u8L("Expert")
    );
    for (size_t index = 1; index < m_tab_pages->object_count(); ++index) {
        if (auto* page = dynamic_cast<PrintTabListView*>(m_tab_pages->get_item(index)))
            page->refresh_mode();
    }
}

void SidebarPrint::select_tab(size_t index)
{
    m_tab_buttons.at(index)->set_checked(true);
    m_tab_pages->set_current_index(index);
}

bool SidebarPrint::show_item(const Domain::ConfigItem* config_item)
{
    // page 0 is Favorites, the category tabs follow
    for (size_t index = 1; index < m_tab_pages->object_count(); ++index) {
        auto* page = dynamic_cast<PrintTabListView*>(m_tab_pages->get_item(index));
        if (page != nullptr && page->contains(config_item->def())) {
            // Orca's search: a setting the visibility hides is shown by raising it
            if (!is_shown_in_mode(config_item->def()))
                set_settings_mode(config_item->def().mode);
            set_objects_mode(false);
            m_section->set_collapsed(false);
            select_tab(index);
            page->navigate_to_item(config_item);
            return true;
        }
    }
    return false;
}

void SidebarPrint::clear_navigation()
{
    for (size_t index = 1; index < m_tab_pages->object_count(); ++index) {
        if (auto* page = dynamic_cast<PrintTabListView*>(m_tab_pages->get_item(index)))
            page->clear_navigation();
    }
}

void SidebarPrint::set_objects_items(ItemPtr object_list, ItemPtr object_settings)
{
    m_object_list     = object_list.get();
    m_object_settings = object_settings.get();
    // Orca: Objects is a tab of the Process panel, its page the object list and the selected
    // object's settings (no panel of its own)
    m_section->content()->append(std::move(object_list));
    m_section->content()->append(std::move(object_settings));
    // Orca's sidebar: object list 2 parts, object settings 3 parts (Plater.cpp sizer_params / params_panel)
    if (auto* window = dynamic_cast<Yoga::CollapsibleWindow*>(m_object_list)) {
        window->set_header_visible(false);
        window->set_collapsed(false);
        window->set_flex_basis(0.f);
        window->set_flex_grow(2);
    }
    m_object_settings->set_flex_basis(0.f);
    m_object_settings->set_flex_grow(3);
    update_visibility();
}

void SidebarPrint::set_objects_mode(bool objects)
{
    m_objects_mode = objects;
    (objects ? m_objects_button : m_global_button)->set_checked(true);
    update_visibility();
}

void SidebarPrint::set_object_selected(bool selected)
{
    m_object_selected = selected;
    update_visibility();
}

void SidebarPrint::update_visibility()
{
    // Orca: Global shows the print tab; Objects shows the object list (Sidebar::show_object_list)
    // and the selected item's settings
    // Global / Objects are the Process panel's tabs: one page or the other in its content
    m_global_content->set_visible(!m_objects_mode);
    m_section->content()->set_visible(!m_section->collapsed());
    m_section->set_flex_grow(m_section->collapsed() ? 0.f : 1.f);
    if (m_object_list != nullptr) {
        m_object_list->set_visible(m_objects_mode);
        m_object_settings->set_visible(m_objects_mode && m_object_selected);
    }
}

void SidebarPrint::update_tools_visibility()
{
    const bool is_multi_extruder =
        Domain::Preset::get_feature<bool>(
            m_project_interactor.preset_interactor().selected_printer_preset().hw_config.features,
            "multi_extruder"
        )
            .value_or(false);

    m_tool_head_list_view->set_visible(is_multi_extruder);
}

void SidebarPrint::refresh_print_combobox_label_color()
{
    const auto list = m_project_interactor.preset_interactor().print_tool_cbi().observable_list().lock();
    m_combo_print->set_label_color(m_theme->color_imgui(
        list->is_dirty_print() ? Platform::Color::AccentTertiary : Platform::Color::Text
    ));
    m_revert_button->set_visible(list->is_dirty());
    m_delete_button->set_visible(
        m_project_interactor.preset_interactor().selected_printer_preset().print.origin
        == Domain::Preset::PresetOrigin::User
    );
}

void SidebarPrint::refresh_tools_comboboxes_label_colors()
{
    const size_t tool_cnt = m_project_interactor.preset_interactor().tool_presets().size();
    for (size_t tool_index{}; tool_index < tool_cnt; tool_index++) {
        bool is_modified_preset = m_project_interactor.preset_interactor()
                                      .print_tool_cbi()
                                      .observable_list()
                                      .lock()
                                      ->is_dirty_tool(tool_index);
        m_tool_head_list_view->item_at(tool_index)
            ->combo_box()
            ->set_label_color(m_theme->color_imgui(
                is_modified_preset ? Platform::Color::AccentTertiary : Platform::Color::Text
            ));
    }
}

PrintSettingsDialog& SidebarPrint::print_settings_dialog()
{
    return *m_print_settings_dialog;
}

void SidebarPrint::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id)
    {
        switch (type) {
        case Biz::Preset::PresetItemType::PrinterPreset:
            update_tools_visibility();
            if (m_project_interactor.selected_config_container().print_technology() != m_tabs_technology)
                create_tabs();
            break;
        case Biz::Preset::PresetItemType::PrintPreset:
            refresh_print_combobox_label_color();
            break;
        case Biz::Preset::PresetItemType::ToolPrintPreset:
            refresh_tools_comboboxes_label_colors();
            break;
        case Biz::Preset::PresetItemType::MaterialPreset:
        default:
            break;
        }
    }
}

void SidebarPrint::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if (location == Domain::FDMConfigLocation::Print) {
            refresh_print_combobox_label_color();
        } else if (location == Domain::FDMConfigLocation::Tool) {
            refresh_tools_comboboxes_label_colors();
            refresh_print_combobox_label_color();
        }
    } else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{std::get<Domain::SLAConfigLocation>(item.location())};
        if (location == Domain::SLAConfigLocation::Print) {
            refresh_print_combobox_label_color();
        }
    }
}

void SidebarPrint::on_preset_bundles_loaded()
{
    refresh_print_combobox_label_color();
    refresh_tools_comboboxes_label_colors();
}

void SidebarPrint::on_config_container_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id)
    {
        update_tools_visibility();
        if (m_project_interactor.selected_config_container().print_technology() != m_tabs_technology)
            create_tabs();
    }
}

} // namespace Slic3r::App
