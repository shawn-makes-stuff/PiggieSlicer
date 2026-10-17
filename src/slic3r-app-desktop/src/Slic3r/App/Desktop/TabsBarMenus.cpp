#include "Slic3r/App/Desktop/TabsBarMenus.hpp"
#include "Slic3r/App/Desktop/TabsBarCtrl.hpp"

#include "Slic3r/App/WX/MenuManaging.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/Assert.hpp>

//#include "GUI_App.hpp"
// #include "GUI_Factories.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"

namespace Slic3r::App::Desktop {

using namespace Slic3r::App::WX;

TabsBarMenus::TabsBarMenus()
{
    BindEvtClose();
}

void TabsBarMenus::Popup(TabsBarCtrl* popup_ctrl, wxMenu* menu, wxPoint pos)
{
    m_popup_ctrl = popup_ctrl;
    m_popup_ctrl->PopupMenu(menu, pos);
}

void TabsBarMenus::BindEvtClose()
{
    auto close_fn = [this]() {
        if (m_popup_ctrl)
            m_popup_ctrl->UnselectPopupButtons();
        m_popup_ctrl = nullptr;
    };

    main.        Bind(wxEVT_MENU_CLOSE, [close_fn](wxMenuEvent&) { close_fn(); });
    workspaces.  Bind(wxEVT_MENU_CLOSE, [close_fn](wxMenuEvent&) { close_fn(); });
}

} // Slic3r::App::Desktop