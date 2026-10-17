#pragma once

#include "Slic3r/App/LeftBarTabs.hpp"
#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

#include <wx/panel.h>
#include <functional>

namespace Slic3r::App::WX::WebView {

class AbstractWebViewPanel : public wxPanel
{
public:
    AbstractWebViewPanel(wxWindow* parent, int id) :
        wxPanel(parent, id, wxDefaultPosition, wxDefaultSize)
    {}

    virtual ~AbstractWebViewPanel() = default;

    virtual void set_next_show_url(const std::string url) {}

    virtual App::Browser::AbstractBrowserLogic* browser_logic() { return nullptr; }

    void set_switch_left_tab_fn(std::function<void(LeftBarTabs, const std::string&)> switch_left_tab_fn)
    {
        m_switch_left_tab_fn = switch_left_tab_fn;
    }

protected:
    std::function<void(LeftBarTabs, const std::string&)> m_switch_left_tab_fn;

};

} // namespace Slic3r::App::WX::WebView
