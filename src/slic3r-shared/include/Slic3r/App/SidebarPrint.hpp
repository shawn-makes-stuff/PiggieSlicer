#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/App/SidebarToolHeadRow.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class PrintSettingsDialog;
class PrintTabListView;
class Navigator;

namespace Yoga {
class LayoutButton;
class CollapsibleWindow;
class StackLayout;
} // namespace Yoga

/**
 * @brief Process section of the Orca-style sidebar (docs/design/ORCA_SIDEBAR.md): title with the
 * Global | Objects switch; Global shows the print preset row, the tab strip and the selected
 * tab's settings inline; Objects shows the object list and the selected object's settings.
 */
class SidebarPrint : public Yoga::Item, public Biz::Preset::IPresetChangedListener
{
public:
    explicit SidebarPrint(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    PrintSettingsDialog& print_settings_dialog();

    /// Objects mode content, shown below the Process title (owned by this item).
    void set_objects_items(Yoga::ItemPtr object_list, Yoga::ItemPtr object_settings);
    void set_objects_mode(bool objects);
    void set_object_selected(bool selected);

    /// Search: opens the tab holding the item and scrolls to it; false when no tab shows it.
    bool show_item(const Domain::ConfigItem* config_item);
    void clear_navigation();

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

    void on_preset_bundles_loaded() override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

private:
    void create_tabs();
    void select_tab(size_t index);
    void update_visibility();
    // Orca's settings visibility: shows it on the button and re-filters the tabs
    void set_settings_mode(Domain::ConfigItemDef::Mode mode);
    void update_tools_visibility();
    void refresh_print_combobox_label_color();
    void refresh_tools_comboboxes_label_colors();

private:
    using ToolHeadListView = Yoga::ListView<
        SidebarToolHeadRow,
        Biz::Preset::PresetItemObservableList,
        Yoga::ViewFactory<
            SidebarToolHeadRow,
            Biz::Preset::PresetItemObservableList,
            Biz::ProjectInteractor&>>;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarPrint>
        m_preset_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::CollapsibleWindow* m_section{nullptr};
    Yoga::ButtonGroup m_mode_group;
    Yoga::LayoutButton* m_global_button{nullptr};
    Yoga::LayoutButton* m_objects_button{nullptr};
    Yoga::LayoutButton* m_settings_mode_button{nullptr};

    Yoga::Item* m_global_content{nullptr};
    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_combo_print{nullptr};
    Yoga::LayoutButton* m_revert_button{nullptr};
    Yoga::LayoutButton* m_delete_button{nullptr};
    ToolHeadListView* m_tool_head_list_view{nullptr};

    Yoga::Item* m_tab_strip{nullptr};
    Yoga::ButtonGroup m_tab_group;
    std::vector<Yoga::LayoutButton*> m_tab_buttons;
    Yoga::StackLayout* m_tab_pages{nullptr};
    Domain::PrinterTechnology m_tabs_technology{Domain::PrinterTechnology::FFF};

    Yoga::Item* m_object_list{nullptr};
    Yoga::Item* m_object_settings{nullptr};
    bool m_objects_mode{false};
    bool m_object_selected{false};

    PrintSettingsDialog* m_print_settings_dialog{nullptr};

    int m_last_selected_index{-1};
};

} // namespace Slic3r::App
