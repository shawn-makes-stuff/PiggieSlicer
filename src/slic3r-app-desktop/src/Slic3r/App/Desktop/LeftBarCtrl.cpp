#include "Slic3r/App/Desktop/LeftBarCtrl.hpp"
#include "Slic3r/App/Desktop/TabsBarMenus.hpp"
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/App/WX/StringConversions.hpp>

#include "Slic3r/App/WX/I18N.hpp"

#include <wx/sizer.h>

namespace Slic3r::App::Desktop {

using namespace Slic3r::App::WX;

LeftBarCtrl::LeftBarCtrl(wxWindow* parent, int orient, TabsBarMenus* menus)
: TabsBarCtrl(parent, orient, menus)
{
    auto add_btn = [this](Button* btn) -> void {
        m_second_sizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, m_btn_margin);
        btn->set_margin(w_config()->em_unit(this));
    };

    m_preferences_btn = new Button(this, { wxEmptyString, "cog_wx", m_action_btn_sz, wxVERTICAL });
    m_preferences_btn->SetToolTip(from_u8(Biz::_u8L("Preferences")));
    add_btn(m_preferences_btn);

    m_preferences_btn->set_compact_mode(compact_mode(), m_action_btn_sz, false);
    // Preferences is reached from Menu; the left bar has no shortcut for it
    m_preferences_btn->Hide();

    // Prepare / Preview above the page buttons, looking like them (TabsBarCtrl::InsertPage)
    m_prepare_btn = new Button(this, {from_u8(Biz::_u8L("Prepare")), "lb_slicing", 22, m_orient});
    m_preview_btn = new Button(this, {from_u8(Biz::_u8L("Preview")), "preview", 22, m_orient});
    wxBoxSizer* mode_sizer = new wxBoxSizer(m_orient);
    for (Button* btn : {m_prepare_btn, m_preview_btn}) {
        btn->set_compact_mode(compact_mode(), 22);
        mode_sizer->Add(
            btn,
            0,
            (m_orient == wxVERTICAL ? wxALIGN_CENTER_HORIZONTAL | wxBOTTOM : wxALIGN_CENTER_VERTICAL | wxRIGHT),
            m_btn_margin
        );
    }
    m_first_sizer->Insert(0, mode_sizer, 0, m_orient == wxVERTICAL ? wxALIGN_CENTER_HORIZONTAL | wxTOP : wxALIGN_CENTER_VERTICAL, m_btn_margin);

    // The removed account button used to trigger this initial layout.
    this->Layout();
}

void LeftBarCtrl::OnColorsChanged()
{
    TabsBarCtrl::OnColorsChanged();

    m_preferences_btn->sys_color_changed();
    m_prepare_btn->sys_color_changed();
    m_preview_btn->sys_color_changed();
}

void LeftBarCtrl::UnselectPopupButtons()
{
}

void LeftBarCtrl::on_rescale()
{
    int margin = w_config()->em_unit(this);
    m_preferences_btn->set_margin(margin);
}

TabsBarCtrl::Button* LeftBarCtrl::preferences_btn() const
{
    return m_preferences_btn;
}

TabsBarCtrl::Button* LeftBarCtrl::prepare_btn() const
{
    return m_prepare_btn;
}

TabsBarCtrl::Button* LeftBarCtrl::preview_btn() const
{
    return m_preview_btn;
}

void LeftBarCtrl::on_compact_mode_changed()
{
    const bool compact = compact_mode();
    m_preferences_btn->set_compact_mode(compact, m_action_btn_sz, false);
    m_prepare_btn->set_compact_mode(compact, 22);
    m_preview_btn->set_compact_mode(compact, 22);
    m_first_sizer->Layout();
    for (int sizer_item_id = 0; sizer_item_id < m_second_sizer->GetItemCount(); sizer_item_id++) {
        m_second_sizer->GetItem(sizer_item_id)
            ->SetFlag(wxALIGN_CENTER_HORIZONTAL | (m_compact_mode ? wxTOP : wxALL));
    }
    m_second_sizer->Layout();
    Refresh();
}

} // namespace Slic3r::App::Desktop
