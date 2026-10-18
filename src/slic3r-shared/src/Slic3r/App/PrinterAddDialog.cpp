#include "Slic3r/App/PrinterAddDialog.hpp"

#include "Slic3r/App/AddPrinterPanel.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrinterAddDialog::PrinterAddDialog(Biz::ProjectInteractor& project_interactor) :
    Dialog({"Add printer"}, "PrinterAddDialog"),
    m_project_interactor(project_interactor)
{
    content_item()->set_width(100_ww);
    content_item()->set_height(100_wh);
    content_item()->set_max_width(800_fpx);
    content_item()->set_max_height(600_fpx);
    content_item()->set_padding(0);
    m_add_printer_panel = content()->emplace_back<AddPrinterPanel>(
        m_project_interactor,
        nullptr,
        [this](const AddPrinterPanel::Printer& printer)
        {
            AppSettingsAdvanced& settings =
                AppServices::instance().app_config().app_settings_advanced();
            auto& preset_interactor = m_project_interactor.preset_interactor();

            // build and evaluate just this printer (the picker lists unbuilt templates), list it
            // (favourite, as Orca lists the installed printers) and remember it for the next start
            const Domain::Preset::HwPrinterConfig& config = printer.default_config;
            preset_interactor.update_changed_printer_configs({config});
            const std::vector<std::string> preset_ids = preset_interactor.system_printer_preset_ids(config.id);
            for (const std::string& preset_id : preset_ids)
                settings.printer_favorite_presets.insert(preset_id);
            if (!settings.added_printers)
                settings.added_printers.emplace();
            settings.added_printers->insert(
                Biz::Preset::PresetInteractor::added_printer_key(config.vendor_id, printer.template_id)
            );
            if (!preset_ids.empty())
                preset_interactor.select_printer_preset(config.id, preset_ids.front(), true);
            AppServices::instance().app_config().save();
            if (m_callbacks.printer_added) {
                m_callbacks.printer_added();
                close();
            }
        }
    );
    m_add_printer_panel->set_flex_grow(1);
}

PrinterAddDialog::Callbacks& PrinterAddDialog::callbacks()
{
    return m_callbacks;
}
} // namespace Slic3r::App
