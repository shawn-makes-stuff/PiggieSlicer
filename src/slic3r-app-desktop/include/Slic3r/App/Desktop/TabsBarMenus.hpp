#pragma once

#include <wx/menu.h>
#include <boost/filesystem.hpp>

class wxString;

namespace Slic3r::App::Desktop {

class TabsBarCtrl;

class TabsBarMenus
{
private:
    TabsBarCtrl*            m_popup_ctrl        { nullptr };

    std::function<int()>            m_cb_get_mode                   { nullptr };
    std::function<void(int)>        m_cb_save_mode                  { nullptr };
    std::function<std::string(int)> m_cb_get_mode_btn_color         { nullptr };

public:
    wxMenu          main;
    wxMenu          workspaces;

    TabsBarMenus();
    ~TabsBarMenus() = default;

    void Popup(TabsBarCtrl* popup_ctrl, wxMenu* menu, wxPoint pos);
    void BindEvtClose();
};

} // namespace Slic3r::App::Desktop
