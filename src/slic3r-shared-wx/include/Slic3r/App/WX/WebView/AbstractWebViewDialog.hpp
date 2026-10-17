#pragma once

#include <wx/dialog.h>

namespace Slic3r::App::WX::WebView {

class AbstractWebViewDialog : public wxDialog
{
public:
    AbstractWebViewDialog(
        wxWindow* parent,
        wxWindowID id,
        const wxString& title,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style         = wxDEFAULT_DIALOG_STYLE
    ) :
        wxDialog(parent, id, title, pos, size, style)
    {}

    virtual ~AbstractWebViewDialog() = default;
};
} // namespace Slic3r::App::WX::WebView
