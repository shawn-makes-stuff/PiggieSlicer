#pragma once

#include "Slic3r/App/Yoga/Popup.hpp"

#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

struct PrinterToAdd;

class PrinterScreen;

class WelcomeDialog : public Yoga::Popup
{
public:
    WelcomeDialog(Biz::ProjectInteractor& project_interactor);

private:
    Biz::ProjectInteractor& m_project_interactor;

    Yoga::Item* m_top_bar{nullptr};
    Yoga::Item* m_content{nullptr};
    const Yoga::Item* m_current_screen{nullptr};

    Yoga::Item* m_changelog_screen{nullptr};
    PrinterScreen* m_printer_screen{nullptr};

    void finalize(const std::vector<PrinterToAdd>& printers);

    void reload_top_bar();

    void go_to_screen(const Yoga::Item* screen);

    void sync_config() const;
};
} // namespace Slic3r::App::Plater
