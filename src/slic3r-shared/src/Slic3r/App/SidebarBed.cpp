#include "Slic3r/App/SidebarBed.hpp"
#include "Slic3r/App/ColorMix/ColorMixDialog.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/PrinterSettingsButton.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/MaterialSelectionDialog.hpp"
#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/FlushingVolumesDialog.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <imgui/imgui_internal.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SidebarBed::SidebarBed(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Item(),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_physical_printer_listener_scope(project_interactor.physical_printer_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    m_printer_add_dialog              = emplace_back<PrinterAddDialog>(m_project_interactor);
    m_logical_printer_settings_dialog = emplace_back<LogicalPrinterSettingsDialog>(
        project_interactor,
        m_printer_add_dialog,
        m_navigator
    );

    m_material_selection_dialog =
        emplace_back<MaterialSelectionDialog>(project_interactor, m_navigator);

    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_flex_shrink(0.f);

    // Printer section (Orca: "Printer" title bar, preset box in the content)
    m_printer_section = emplace_back<CollapsibleWindow>(Biz::_u8L("Printer"), "SidebarPrinter");
    m_printer_section->collapsible_window_callbacks().collapsed_changed = [this](bool)
    { refresh_section_titles(); };
    LayoutButton* connection_button = m_printer_section->emplace_into_header<LayoutButton>(
        std::string{},
        Render::Icon::PhysicalPrinterIcon,
        Biz::_u8L("Network printer: address and access (Mainsail, Fluidd, OctoPrint ...)")
    );
    connection_button->set_content_padding({});
    connection_button->set_width(22);
    connection_button->set_height(22);
    connection_button->set_background_color(Platform::Color::ButtonTransparent);
    connection_button->callbacks().action = [this]
    {
        if (m_connection_action)
            m_connection_action(m_printer_section);
    };
    Item* printer_content = m_printer_section->content();
    printer_content->set_gap(10_fpx);

    // Filament section (Orca: "Project Filaments (N)" ... [-] [+] [sync]; [+] after [-] so it
    // keeps its place when [-] hides). Spacing between title buttons as Orca: 10, then 18.
    m_filament_section = emplace_back<CollapsibleWindow>(std::string{}, "SidebarFilament");
    m_filament_section->collapsible_window_callbacks().collapsed_changed = [this](bool)
    { refresh_section_titles(); };
    auto add_title_button = [this](Render::Icon icon, const std::string& tooltip, float left_margin)
    {
        LayoutButton* button = m_filament_section->emplace_into_header<LayoutButton>(std::string{}, icon, tooltip);
        button->set_content_padding({});
        button->set_width(22);
        button->set_height(22);
        button->set_background_color(Platform::Color::ButtonTransparent);
        button->set_margin({left_margin, 0.f, 0.f, 0.f});
        return button;
    };
    // Orca: "Flushing volumes" left of [-] / [+], shown with several filaments
    m_flushing_volumes_dialog = emplace_back<FlushingVolumesDialog>(m_project_interactor, m_navigator);
    m_flushing_volumes_dialog->attach_to_item(m_filament_section, Position::Right, 40_fpx, AlignU::Start);
    // full-spectrum (virtual) filaments mixed from the physical ones: PrusaSlicer 3.0's colour
    // mix editor, drawn by this item so it opens on its own
    auto& color_mix = m_logical_printer_settings_dialog->color_mix_dialog();
    append(color_mix.parent()->remove(&color_mix));
    color_mix.attach_to_item(m_filament_section, Position::Right, 40_fpx, AlignU::Start);
    m_color_mix_button = add_title_button(Render::Icon::MmSegmentation, Biz::_u8L("Color mix (virtual filaments)"), 0.f);
    m_color_mix_button->callbacks().action = [this, &color_mix]
    { m_navigator.set_opened_dialog(color_mix.opened() ? nullptr : &color_mix); };
    m_flushing_volumes_button = add_title_button(Render::Icon::LegendWipe, Biz::_u8L("Flushing volumes"), 5.f);
    m_flushing_volumes_button->callbacks().action = [this]
    {
        m_navigator.set_opened_dialog(
            m_flushing_volumes_dialog->opened() ? nullptr : m_flushing_volumes_dialog
        );
    };
    m_remove_filament_button = add_title_button(Render::Icon::Minus, Biz::_u8L("Remove last filament"), 5.f);
    m_add_filament_button    = add_title_button(Render::Icon::Plus, Biz::_u8L("Add one filament"), 5.f);
    m_sync_filament_button   = add_title_button(
        Render::Icon::Reload,
        Biz::_u8L("Synchronize filament list from the printer"),
        13.f
    );
    // the generic unit is attached to the first tool (single-nozzle printers, like Orca's sidebar)
    m_remove_filament_button->callbacks().action = [this]
    { m_project_interactor.preset_interactor().change_external_mmu_slots(0, -1, true); };
    m_add_filament_button->callbacks().action = [this]
    { m_project_interactor.preset_interactor().change_external_mmu_slots(0, 1, true); };
    m_sync_filament_button->callbacks().action = [this]
    {
        m_sync_filament_button->set_enabled(false);
        m_project_interactor.sync_filaments_from_selected_printer(
            0,
            [this](std::string error)
            {
                m_sync_filament_button->set_enabled(true);
                m_sync_filament_button->set_tooltip(
                    error.empty() ? Biz::_u8L("Synchronize filament list from the printer") : error
                );
            }
        );
    };
    Item* filament_content = m_filament_section->content();

    m_material_button_group = std::make_shared<ButtonGroup>();
    m_material_button_group->set_always_checked(false);

    m_bed_name = printer_content->emplace_back<Text>("Unkown");
    m_bed_name->set_margin(Paddings(0.f, -5.f, 0.f, 0.f));
    m_bed_name->set_font_type(Render::ImguiFontType::Bold);
    m_bed_name->set_font_size(1.15_rem);

    // printer box, then the plate and the nozzles each on their own row (user: plate under the
    // printer rather than beside it as in Orca's layout_printer)
    m_logical_printer_button = printer_content->emplace_back<PrinterSettingsButton>();

    // plate dropdown across the row, the same width as the nozzle dropdown below
    m_combo_plate = printer_content->emplace_back<ComboBoxListViewSelection<Domain::Preset::HwSheetConfigDef>>();
    m_combo_plate->set_get_name_fn(
        [](const Domain::Preset::HwSheetConfigDef* data) -> std::string { return data->name; }
    );
    m_combo_plate->set_source_list(&m_project_interactor.preset_interactor().sheet_items());
    m_combo_plate->callbacks().selection_changed = [this](int sheet_index)
    {
        if (sheet_index >= 0) {
            m_project_interactor.preset_interactor().select_printer_sheet(
                m_project_interactor.preset_interactor().sheet_items().items().at(sheet_index).id,
                true
            );
        }
    };

    m_nozzle_list_view = printer_content->emplace_back<NozzleListView>(NozzleListView::ViewFactoryType{
        m_project_interactor.preset_interactor(),
        std::function<void(bool)>{}
    });
    m_nozzle_list_view->set_orientation(Orientation::Vertical);
    m_nozzle_list_view->set_gap(5_fpx);
    m_nozzle_list_view->set_source_list(&m_project_interactor.preset_interactor().tool_items());
    m_logical_printer_button->set_visible_cog(true);

    m_logical_printer_settings_dialog
        ->attach_to_item(m_logical_printer_button, Position::Right, 40_fpx, AlignU::Start);
    m_logical_printer_settings_dialog->callbacks().opened = [this]()
    { m_logical_printer_button->set_checked(true); };
    m_logical_printer_settings_dialog->callbacks().closed = [this]()
    { m_logical_printer_button->set_checked(false); };

    auto toggle_logical_printer_settings_dialog = [this]()
    {
        if (m_logical_printer_settings_dialog->opened()) {
            m_navigator.set_opened_dialog(nullptr);
        } else {
            m_navigator.set_opened_dialog(m_logical_printer_settings_dialog);
        }
    };
    m_logical_printer_button->callbacks().action = [toggle_logical_printer_settings_dialog]()
    { toggle_logical_printer_settings_dialog(); };
    // the cog opens the printer's settings; clicking the printer itself opens the printer list.
    // The settings dialog is drawn by this item, not inside the list (a closed popup draws none of
    // its children), so it can open on its own.
    auto& printer_settings = m_logical_printer_settings_dialog->printer_advanced_settings_dialog();
    append(printer_settings.parent()->remove(&printer_settings));
    m_logical_printer_button->on_cog() = [this, &printer_settings]()
    {
        if (printer_settings.opened()) {
            m_navigator.set_opened_dialog(nullptr);
            return;
        }
        printer_settings.attach_to_item(m_logical_printer_button, Position::Right, 40_fpx, AlignU::Start);
        m_navigator.set_opened_dialog(&printer_settings);
    };

    // the filament cog opens the filament's settings alone (Orca's edit button), like the printer's:
    // drawn by this item, not inside the material list
    auto& material_settings = m_material_selection_dialog->material_settings_dialog();
    append(material_settings.parent()->remove(&material_settings));

    m_list_view = filament_content->emplace_back<MaterialListView>(MaterialListViewFactory{
        std::weak_ptr<ButtonGroup>(m_material_button_group),
        [this, &material_settings](size_t index)
        {
            m_material_selection_dialog->set_material_index(index);
            material_settings.set_current_tab(index);
            if (!material_settings.opened()) {
                material_settings.attach_to_item(m_list_view, Position::Right, 40_fpx, AlignU::Start);
                m_navigator.set_opened_dialog(&material_settings);
            }
        },
        [toggle_logical_printer_settings_dialog, this](size_t index)
        {
            toggle_logical_printer_settings_dialog();
            m_logical_printer_settings_dialog->select_page_settings();
        },
        m_project_interactor
    });
    m_list_view->set_source_list(&m_project_interactor.preset_interactor().material_presets());
    // two columns in reading order (Orca: filament i at column i % 2, row i / 2)
    m_list_view->set_orientation(Orientation::Horizontal);
    m_list_view->set_flex_wrap(YGWrapWrap);
    m_list_view->set_justify_content(YGJustifySpaceBetween);
    m_list_view->set_gap(5_fpx);

    m_material_selection_dialog->attach_to_item(m_list_view, Position::Right, 40_fpx, AlignU::Start);
    material_settings.attach_to_item(m_list_view, Position::Right, 40_fpx, AlignU::Start);
    m_material_selection_dialog->callbacks().closed = [this]()
    {
        for (AbstractButton* button : m_material_button_group->buttons()) {
            button->set_checked(false);
        }
    };

    m_material_selection_dialog->material_selection_callbacks().advanced_settings_tab_opened =
        [this](size_t current_index)
    {
        if (m_material_selection_dialog->opened()) {
            dynamic_cast<AbstractButton*>(m_list_view->get_item(current_index))->set_checked(true);
            m_material_selection_dialog->set_material_index(current_index);
        }
    };

    m_material_button_group->callbacks().action = [this](AbstractButton* action_button)
    {
        if (action_button->checked()) {
            m_material_selection_dialog->set_material_index(
                m_list_view->index_of(action_button).value()
            );
            m_material_selection_dialog->material_settings_dialog().set_current_tab(
                m_list_view->index_of(action_button).value()
            );
            if (!m_material_selection_dialog->opened()) {
                m_navigator.set_opened_dialog(m_material_selection_dialog);
            }
        } else {
            m_navigator.set_opened_dialog(nullptr);
        }
    };

    m_project_interactor.preset_interactor()
        .printer_presets()
        .add_listener<Biz::IListSelectionChangedListener>(this);
    on_list_selection_changed(
        m_project_interactor.preset_interactor().printer_presets().selected_index()
    );

    m_project_interactor.scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(
        this
    );
    on_selected_bed_instances_changed(
        m_project_interactor.selected_project_id(),
        m_project_interactor.scene_interactor().bed_selection()
    );
}

void SidebarBed::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    const auto& preset_interactor = m_project_interactor.preset_interactor();
    const Biz::Preset::PresetItem& preset_item =
        preset_interactor.printer_presets().items().at(new_selection);

    const bool is_modified_preset = preset_interactor.is_printer_preset_selected_and_dirty(
        preset_item.hw_printer_config_id,
        preset_item.id
    );

    m_selected_printer_preset_name = preset_item.ui_preset_name();
    m_logical_printer_button->set_printer_name(m_selected_printer_preset_name, is_modified_preset);
    m_logical_printer_button->set_preset_name(preset_item.ui_hw_config_name());
    m_logical_printer_button->set_tooltip(
        preset_item.ui_preset_name() + "\n" + preset_item.ui_hw_config_name()
    );

    const Domain::Preset::HwPrinterConfig& printer_config =
        preset_interactor.current_printer_config();

    if (printer_config.visual.thumbnail.has_value()) {
        const std::string image_path =
            printer_config.relative_path_to_assets() + printer_config.visual.thumbnail.value();

        m_logical_printer_button->set_image(image_path);
    }
    refresh_section_titles();
    refresh_filament_buttons();
}

void SidebarBed::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::BedSelection& bed_selection
)
{
    if (project_id == Domain::INVALID_ID
        || m_project_interactor.selected_project_id() != project_id)
    {
        return;
    }

    Domain::BedInstance* bed_instance =
        m_project_interactor.selected_project().find_bed_instance_by_id(
            bed_selection.last_selected_bed().instance_id
        );

    ASSERT(bed_instance);

    m_bed_name->set_text(bed_instance->name());
}

void SidebarBed::refresh_printer_label_color()
{
    bool is_modified_preset =
        m_project_interactor.preset_interactor().printer_cbi().config_box_list().lock()->is_dirty();
    m_logical_printer_button->set_printer_name(m_selected_printer_preset_name, is_modified_preset);
}

void SidebarBed::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{ std::get<Domain::FDMConfigLocation>(item.location()) };
        if (location != Domain::FDMConfigLocation::Printer) {
            return;
        }
    }
    else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{ std::get<Domain::SLAConfigLocation>(item.location()) };
        if (location != Domain::SLAConfigLocation::Printer) {
            return;
        }
    }

    refresh_printer_label_color();
}

void SidebarBed::on_preset_bundles_loaded()
{
    refresh_printer_label_color();
}

void SidebarBed::on_hw_item_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::HwItemType type
)
{
    if (type == Biz::Preset::HwItemType::FeederItem) {
        refresh_section_titles();
        refresh_filament_buttons();
    }
}

void SidebarBed::on_selected_physical_printer_changed()
{
    refresh_filament_buttons();
}

void SidebarBed::refresh_section_titles()
{
    // Orca: a folded Printer title shows the printer name
    m_printer_section->set_label(
        m_printer_section->collapsed() ? Biz::_u8L("Printer") + "  |  " + m_selected_printer_preset_name :
                                         Biz::_u8L("Printer")
    );
    m_filament_section->set_label(
        Biz::_u8L("Project Filaments") + " ("
        + std::to_string(m_project_interactor.preset_interactor().material_presets().size()) + ")"
    );
}

void SidebarBed::refresh_filament_buttons()
{
    const auto& preset_interactor = m_project_interactor.preset_interactor();
    const bool can_attach         = preset_interactor.can_attach_external_mmu(0);
    const auto slots              = preset_interactor.external_mmu_slot_count(0);
    m_add_filament_button->set_visible(can_attach);
    m_add_filament_button->set_enabled(!slots || *slots < Domain::Preset::EXTERNAL_MMU_MAX_SLOTS);
    m_remove_filament_button->set_visible(slots.has_value());
    m_flushing_volumes_button->set_visible(preset_interactor.current_printer_config().material_slot_count() > 1);
    // as LogicalPrinterSettingsDialog::update_color_mix_visibility: FFF with several filaments
    m_color_mix_button->set_visible(
        preset_interactor.selected_printer_preset().technology() == Domain::PrinterTechnology::FFF
        && preset_interactor.current_printer_config().material_slot_count() > 1
    );
    m_sync_filament_button->set_visible(
        can_attach && m_project_interactor.physical_printer_interactor().is_printer_upload_selected()
    );
}

LogicalPrinterSettingsDialog& SidebarBed::logical_printer_settings_dialog()
{
    return *m_logical_printer_settings_dialog;
}

PrinterAddDialog& SidebarBed::printer_add_dialog()
{
    return *m_printer_add_dialog;
}

void SidebarBed::set_connection_action(std::function<void(Yoga::Item*)> action)
{
    m_connection_action = std::move(action);
}

FlushingVolumesDialog& SidebarBed::flushing_volumes_dialog()
{
    return *m_flushing_volumes_dialog;
}

MaterialSelectionDialog& SidebarBed::material_selection_dialog()
{
    return *m_material_selection_dialog;
}

} // namespace Slic3r::App
