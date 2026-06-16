#include "MixedFilamentDialog.hpp"

#include <algorithm>

#include <wx/sizer.h>
#include <wx/scrolwin.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/stattext.h>

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GLCanvas3D.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/MixedFilament.hpp"
#include "libslic3r/Config.hpp"

namespace Slic3r { namespace GUI {

static const wxColour PINK(236, 111, 166);
static const wxColour PINK_DK(194, 78, 134);
static const wxColour PINK_HOV(242, 145, 188);
static const wxColour PINK_SOFT(252, 234, 242);
static const wxColour PAGE_BG(243, 244, 247);
static const wxColour CARD_BG(255, 255, 255);
static const wxColour CARD_BORDER(228, 230, 236);
static const wxColour INK(58, 58, 66);
static const wxColour INK_SOFT(124, 126, 134);

static wxColour hex_to_colour(const std::string& hex)
{
    if (hex.size() >= 7 && hex[0] == '#') {
        auto h2 = [&](int i) { return (unsigned char) strtol(hex.substr(i, 2).c_str(), nullptr, 16); };
        return wxColour(h2(1), h2(3), h2(5));
    }
    return wxColour(200, 200, 200);
}

static Button* make_btn(wxWindow* parent, const wxString& label, bool primary)
{
    auto* b = new Button(parent, label);
    b->SetCornerRadius(parent->FromDIP(6));
    b->SetPaddingSize(wxSize(parent->FromDIP(12), parent->FromDIP(7)));
    b->SetFont(Label::Body_13);
    if (primary) {
        b->SetBorderWidth(0);
        b->SetBackgroundColor(StateColor(std::make_pair(PINK_DK, (int) StateColor::Pressed),
                                         std::make_pair(PINK_HOV, (int) StateColor::Hovered),
                                         std::make_pair(PINK, (int) StateColor::Normal)));
        b->SetTextColor(StateColor(*wxWHITE));
    } else {
        b->SetBorderWidth(1);
        b->SetBorderColor(StateColor(std::make_pair(PINK, (int) StateColor::Hovered),
                                     std::make_pair(CARD_BORDER, (int) StateColor::Normal)));
        b->SetBackgroundColor(StateColor(std::make_pair(PINK_SOFT, (int) StateColor::Hovered),
                                         std::make_pair(CARD_BG, (int) StateColor::Normal)));
        b->SetTextColor(StateColor(PINK_DK));
    }
    return b;
}

static wxWindow* make_swatch(wxWindow* parent, const wxColour& c, int dip)
{
    auto* sw = new StaticBox(parent, wxID_ANY, wxDefaultPosition, wxSize(parent->FromDIP(dip), parent->FromDIP(dip)));
    sw->SetCornerRadius(parent->FromDIP(6));
    sw->SetBorderColor(StateColor(CARD_BORDER));
    sw->SetBorderWidth(1);
    sw->SetBackgroundColour(CARD_BG);
    sw->SetBackgroundColor(StateColor(c));
    return sw;
}

MixedFilamentDialog::MixedFilamentDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Mixed Filaments"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(PAGE_BG);
    SetMinSize(wxSize(FromDIP(520), FromDIP(520)));

    // Sync the manager from the current colours + saved definitions.
    auto* pb = wxGetApp().preset_bundle;
    const std::vector<std::string> colors = physical_colors();
    if (pb) {
        std::string serialized;
        if (auto* opt = pb->project_config.option<ConfigOptionString>("mixed_filament_definitions"))
            serialized = opt->value;
        pb->mixed_filaments.auto_generate(colors);
        pb->mixed_filaments.load_custom_entries(serialized, colors);
    }

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* intro = new Label(this, Label::Body_13,
        _L("Create new colors by alternating layers between two physical filaments. "
           "On a single-nozzle ACE printer each alternation is a tool change + purge."));
    intro->Wrap(FromDIP(490));
    intro->SetForegroundColour(INK_SOFT);
    intro->SetBackgroundColour(PAGE_BG);
    root->Add(intro, 0, wxALL, FromDIP(14));

    // --- list of mixed filaments ---
    m_list = new wxScrolledWindow(this, wxID_ANY);
    m_list->SetScrollRate(0, FromDIP(10));
    m_list->SetBackgroundColour(PAGE_BG);
    m_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_list->SetSizer(m_list_sizer);
    root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(14));

    // --- add-row card ---
    auto* add_card = new StaticBox(this);
    add_card->SetBackgroundColor(StateColor(CARD_BG));
    add_card->SetBorderColor(StateColor(CARD_BORDER));
    add_card->SetCornerRadius(FromDIP(10));
    add_card->SetBackgroundColour(CARD_BG);
    auto* ac = new wxBoxSizer(wxVERTICAL);
    auto* ahdr = new Label(add_card, Label::Head_14, _L("Add a mixed color"));
    ahdr->SetForegroundColour(PINK_DK);
    ahdr->SetBackgroundColour(CARD_BG);
    ac->Add(ahdr, 0, wxLEFT | wxTOP | wxRIGHT, FromDIP(14));

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    m_choice_a = new wxChoice(add_card, wxID_ANY);
    m_choice_b = new wxChoice(add_card, wxID_ANY);
    for (size_t i = 0; i < colors.size(); ++i) {
        wxString item = wxString::Format(_L("Filament %d"), int(i + 1));
        m_choice_a->Append(item);
        m_choice_b->Append(item);
    }
    if (colors.size() >= 1) m_choice_a->SetSelection(0);
    if (colors.size() >= 2) m_choice_b->SetSelection(1); else m_choice_b->SetSelection(0);
    row->Add(new Label(add_card, Label::Body_13, "A"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    row->Add(m_choice_a, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    row->Add(new Label(add_card, Label::Body_13, "B"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    row->Add(m_choice_b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    m_preview_sw = make_swatch(add_card, *wxWHITE, 30);
    row->Add(m_preview_sw, 0, wxALIGN_CENTER_VERTICAL);
    ac->Add(row, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    auto* mixrow = new wxBoxSizer(wxHORIZONTAL);
    m_lbl_mix = new Label(add_card, Label::Body_12, _L("Mix: 50% A / 50% B"));
    m_lbl_mix->SetForegroundColour(INK);
    m_lbl_mix->SetBackgroundColour(CARD_BG);
    m_slider_mix = new wxSlider(add_card, wxID_ANY, 50, 0, 100, wxDefaultPosition, wxSize(FromDIP(200), -1));
    mixrow->Add(m_lbl_mix, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    mixrow->Add(m_slider_mix, 1, wxALIGN_CENTER_VERTICAL);
    ac->Add(mixrow, 0, wxEXPAND | wxALL, FromDIP(14));

    m_add_btn = make_btn(add_card, _L("Add mixed color"), true);
    ac->Add(m_add_btn, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));
    add_card->SetSizer(ac);
    root->Add(add_card, 0, wxEXPAND | wxALL, FromDIP(14));

    // bottom buttons
    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    bottom->AddStretchSpacer();
    auto* close = make_btn(this, _L("Close"), false);
    bottom->Add(close, 0);
    root->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

    SetSizerAndFit(root);

    m_choice_a->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { on_preview_changed(); });
    m_choice_b->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { on_preview_changed(); });
    m_slider_mix->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) { on_preview_changed(); });
    m_add_btn->Bind(wxEVT_BUTTON, &MixedFilamentDialog::on_add, this);
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });

    // PiggieSlicer: the rounded StaticBox cards custom-paint their background, so the
    // area exposed when the dialog grows must be invalidated explicitly or it shows
    // garbage. Repaint the whole dialog (and the scroll list) on every resize.
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        if (m_list) { m_list->Refresh(); m_list->Layout(); }
        Refresh();
        e.Skip();
    });

    rebuild_rows();
    on_preview_changed();
}

std::vector<std::string> MixedFilamentDialog::physical_colors() const
{
    std::vector<std::string> colors;
    auto* pb = wxGetApp().preset_bundle;
    if (pb) {
        if (auto* opt = pb->project_config.option<ConfigOptionStrings>("filament_colour"))
            colors = opt->values;
    }
    if (colors.empty()) colors.push_back("#EC6FA6");
    return colors;
}

void MixedFilamentDialog::on_preview_changed()
{
    if (!m_preview_sw || !m_choice_a || !m_choice_b) return;
    const std::vector<std::string> colors = physical_colors();
    int ia = m_choice_a->GetSelection();
    int ib = m_choice_b->GetSelection();
    int mix = m_slider_mix->GetValue();
    if (m_lbl_mix)
        m_lbl_mix->SetLabel(wxString::Format(_L("Mix: %d%% A / %d%% B"), 100 - mix, mix));
    if (ia < 0 || ib < 0 || ia >= int(colors.size()) || ib >= int(colors.size())) return;
    std::string blended = MixedFilamentManager::blend_color(colors[ia], colors[ib], 100 - mix, mix);
    auto* sb = dynamic_cast<StaticBox*>(m_preview_sw);
    if (sb) { sb->SetBackgroundColor(StateColor(hex_to_colour(blended))); sb->Refresh(); }
}

void MixedFilamentDialog::on_add(wxCommandEvent&)
{
    auto* pb = wxGetApp().preset_bundle;
    if (!pb) return;
    const std::vector<std::string> colors = physical_colors();
    int ia = m_choice_a->GetSelection();
    int ib = m_choice_b->GetSelection();
    if (ia < 0 || ib < 0 || ia == ib) return;
    auto& rows = pb->mixed_filaments.mixed_filaments();
    if (m_editing_mixed_index < rows.size()) {
        MixedFilament& mf = rows[m_editing_mixed_index];
        mf.component_a = (unsigned int)(ia + 1);
        mf.component_b = (unsigned int)(ib + 1);
        mf.mix_b_percent = std::clamp(m_slider_mix->GetValue(), 0, 100);
        mf.enabled = true;
        mf.deleted = false;
        mf.manual_pattern.clear();
        mf.gradient_component_ids.clear();
        mf.gradient_component_weights.clear();
        mf.pointillism_all_filaments = false;
        mf.distribution_mode = int(MixedFilament::Simple);
        mf.local_z_max_sublayers = 0;
        mf.component_a_surface_offset = 0.f;
        mf.component_b_surface_offset = 0.f;
        m_editing_mixed_index = size_t(-1);
        if (m_add_btn)
            m_add_btn->SetLabel(_L("Add mixed color"));
    } else {
        pb->mixed_filaments.add_custom_filament((unsigned int)(ia + 1), (unsigned int)(ib + 1),
                                                m_slider_mix->GetValue(), colors);
    }
    persist();
    rebuild_rows();
}

void MixedFilamentDialog::on_remove_custom(size_t mixed_index)
{
    auto* pb = wxGetApp().preset_bundle;
    if (!pb) return;
    auto& rows = pb->mixed_filaments.mixed_filaments();
    if (mixed_index >= rows.size()) return;
    if (rows[mixed_index].custom)
        rows.erase(rows.begin() + mixed_index);
    else {
        rows[mixed_index].deleted = true;
        rows[mixed_index].enabled = false;
    }
    m_editing_mixed_index = size_t(-1);
    if (m_add_btn)
        m_add_btn->SetLabel(_L("Add mixed color"));
    persist();
    rebuild_rows();
}

void MixedFilamentDialog::on_edit(size_t mixed_index)
{
    auto* pb = wxGetApp().preset_bundle;
    if (!pb) return;
    auto& rows = pb->mixed_filaments.mixed_filaments();
    if (mixed_index >= rows.size()) return;
    const MixedFilament& mf = rows[mixed_index];
    if (mf.deleted || !mf.enabled) return;

    const int a = int(mf.component_a) - 1;
    const int b = int(mf.component_b) - 1;
    if (a >= 0 && a < int(m_choice_a->GetCount()))
        m_choice_a->SetSelection(a);
    if (b >= 0 && b < int(m_choice_b->GetCount()))
        m_choice_b->SetSelection(b);
    m_slider_mix->SetValue(std::clamp(mf.mix_b_percent, 0, 100));
    m_editing_mixed_index = mixed_index;
    if (m_add_btn)
        m_add_btn->SetLabel(_L("Update mixed color"));
    on_preview_changed();
}

void MixedFilamentDialog::persist()
{
    auto* pb = wxGetApp().preset_bundle;
    if (!pb) return;
    const std::string serialized = pb->mixed_filaments.serialize_custom_entries();
    if (auto* opt = pb->project_config.option<ConfigOptionString>("mixed_filament_definitions", true))
        opt->value = serialized;
    // The value lives in project_config, which is serialized into the .3mf on save.
    if (auto* plater = wxGetApp().plater()) {
        plater->on_config_change(pb->full_config());
        plater->schedule_background_process();
        if (auto* canvas = plater->canvas3D())
            canvas->set_as_dirty();
    }
}

void MixedFilamentDialog::rebuild_rows()
{
    if (!m_list_sizer) return;
    m_list_sizer->Clear(true);
    auto* pb = wxGetApp().preset_bundle;
    if (!pb) { m_list->Layout(); return; }
    const auto& rows = pb->mixed_filaments.mixed_filaments();
    const std::vector<std::string> colors = physical_colors();

    size_t shown = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const MixedFilament& mf = rows[i];
        if (mf.deleted || !mf.enabled) continue;
        ++shown;
        auto* card = new StaticBox(m_list);
        card->SetBackgroundColor(StateColor(CARD_BG));
        card->SetBorderColor(StateColor(CARD_BORDER));
        card->SetCornerRadius(FromDIP(8));
        card->SetBackgroundColour(CARD_BG);
        auto* hs = new wxBoxSizer(wxHORIZONTAL);

        wxColour blended = mf.display_color.empty() ? wxColour(200, 200, 200) : hex_to_colour(mf.display_color);
        hs->Add(make_swatch(card, blended, 26), 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(8));

        wxColour ca = (mf.component_a >= 1 && mf.component_a <= colors.size()) ? hex_to_colour(colors[mf.component_a - 1]) : wxColour(180,180,180);
        wxColour cb = (mf.component_b >= 1 && mf.component_b <= colors.size()) ? hex_to_colour(colors[mf.component_b - 1]) : wxColour(180,180,180);
        hs->Add(make_swatch(card, ca, 16), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(3));
        auto* plus = new Label(card, Label::Body_12, "+");
        plus->SetForegroundColour(INK_SOFT); plus->SetBackgroundColour(CARD_BG);
        hs->Add(plus, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(3));
        hs->Add(make_swatch(card, cb, 16), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

        auto* lbl = new Label(card, Label::Body_13,
            wxString::Format(_L("Filament %d + Filament %d   (%d%% B)"),
                             int(mf.component_a), int(mf.component_b), int(mf.mix_b_percent)));
        lbl->SetForegroundColour(INK); lbl->SetBackgroundColour(CARD_BG);
        hs->Add(lbl, 1, wxALIGN_CENTER_VERTICAL);

        auto* edit = make_btn(card, _L("Edit"), false);
        edit->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { on_edit(i); });
        hs->Add(edit, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(6));

        auto* rm = make_btn(card, _L("Remove"), false);
        rm->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { on_remove_custom(i); });
        hs->Add(rm, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));
        card->SetSizer(hs);
        m_list_sizer->Add(card, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    }

    if (shown == 0) {
        auto* empty = new Label(m_list, Label::Body_12, _L("No mixed colors yet. Add one below (needs at least 2 physical filaments)."));
        empty->SetForegroundColour(INK_SOFT);
        empty->SetBackgroundColour(PAGE_BG);
        m_list_sizer->Add(empty, 0, wxALL, FromDIP(8));
    }

    m_list->FitInside();
    m_list->Layout();
    Layout();
}

}} // namespace Slic3r::GUI
