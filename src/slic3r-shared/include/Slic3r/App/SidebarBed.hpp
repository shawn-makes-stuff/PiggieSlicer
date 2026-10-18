#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/MaterialSettingsButton.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/App/PrinterNozzleRow.hpp"

#include "Slic3r/Biz/ObservableListWithSelection.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;
class LogicalPrinterSettingsDialog;
class PrinterAddDialog;
class MaterialSelectionDialog;
class FlushingVolumesDialog;
class MaterialListView;

namespace Yoga {
class Text;
class AbstractButton;
class LayoutButton;
class CollapsibleWindow;
class PrinterSettingsButton;
} // namespace Yoga

// Printer and Filament sections of the Orca-style sidebar (docs/design/ORCA_SIDEBAR.md)
class SidebarBed :
    public Yoga::Item,
    public Biz::IListSelectionChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener
{
public:
    explicit SidebarBed(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;
    void on_preset_bundles_loaded() override;
    // [sync] needs a network printer to read from (Orca shows its sync with a connected printer)
    void on_selected_physical_printer_changed() override;
    void on_hw_item_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::HwItemType type
    ) override;

    LogicalPrinterSettingsDialog& logical_printer_settings_dialog();
    PrinterAddDialog& printer_add_dialog();
    MaterialSelectionDialog& material_selection_dialog();
    FlushingVolumesDialog& flushing_volumes_dialog();
    /// The Printer title's connection button (Orca): opens the network printer editor
    /// (Mainsail / Fluidd / OctoPrint host, API key), which the destination block owns,
    /// next to the given item (the Printer section).
    void set_connection_action(std::function<void(Yoga::Item* anchor)> action);

private:
    void refresh_printer_label_color();
    void refresh_section_titles();
    void refresh_filament_buttons();

private:
    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarBed>
        m_preset_changed_listener_scope;
    Biz::ListenerScope<
        Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor,
        SidebarBed>
        m_physical_printer_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::CollapsibleWindow* m_printer_section{nullptr};
    Yoga::CollapsibleWindow* m_filament_section{nullptr};
    Yoga::LayoutButton* m_remove_filament_button{nullptr};
    Yoga::LayoutButton* m_add_filament_button{nullptr};
    Yoga::LayoutButton* m_sync_filament_button{nullptr};
    Yoga::LayoutButton* m_flushing_volumes_button{nullptr};
    Yoga::LayoutButton* m_color_mix_button{nullptr};
    FlushingVolumesDialog* m_flushing_volumes_dialog{nullptr};
    std::function<void(Yoga::Item*)> m_connection_action;

    Yoga::Text* m_bed_name{nullptr};
    Yoga::PrinterSettingsButton* m_logical_printer_button{nullptr};

    std::shared_ptr<Yoga::ButtonGroup> m_material_button_group;
    using MaterialListViewFactory = Yoga::ViewFactory<
        Yoga::MaterialSettingsButton,
        Biz::Preset::PresetItemObservableList,
        std::weak_ptr<Yoga::ButtonGroup>,
        Yoga::MaterialSettingsButton::FnIndexClicked,
        Yoga::MaterialSettingsButton::FnIndexClicked,
        Biz::ProjectInteractor&>;
    using MaterialListView = Yoga::ListView<
        Yoga::MaterialSettingsButton,
        Biz::Preset::PresetItemObservableList,
        MaterialListViewFactory>;

    using NozzleListView = Yoga::ListView<
        PrinterNozzleRow,
        Biz::Preset::ToolConfigItemObservableList,
        Yoga::ViewFactory<
            PrinterNozzleRow,
            Biz::Preset::ToolConfigItemObservableList,
            Biz::Preset::PresetInteractor&,
            std::function<void(bool)>>>;

    Yoga::ComboBoxListViewSelection<Domain::Preset::HwSheetConfigDef>* m_combo_plate{nullptr};
    NozzleListView* m_nozzle_list_view{nullptr};
    MaterialListView* m_list_view{nullptr};
    LogicalPrinterSettingsDialog* m_logical_printer_settings_dialog{nullptr};
    PrinterAddDialog* m_printer_add_dialog{nullptr};
    MaterialSelectionDialog* m_material_selection_dialog{nullptr};

    std::string m_selected_printer_preset_name{};

};

} // namespace Slic3r::App
