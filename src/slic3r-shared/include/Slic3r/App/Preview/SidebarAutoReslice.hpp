#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"

namespace Slic3r::App::Yoga {
    class ToggleButton;
}

namespace Slic3r::Biz {
    class ProjectInteractor;
}

namespace Slic3r::App::Preview {

/// The "Auto-reslice" toggle (Prepare's action area): while on, the selected bed is sliced again
/// after any change, in Prepare and in Preview
class SidebarAutoReslice : public Yoga::Item
{
public:
    SidebarAutoReslice(Biz::ProjectInteractor& project_interactor);

    bool is_enabled() const;
    /// Auto slices the selected bed when the toggle is on (call when the selected bed changes)
    void apply();

public:
    Biz::ProjectInteractor& m_project_interactor;
    Yoga::ToggleButton* m_auto_reslice_chb { nullptr };
    AppConfig& m_app_config{AppServices::instance().app_config()};
};

} // namespace Slic3r::App::Preview
