#include "AnycubicDevicePanel.hpp"

#include <thread>
#include <algorithm>
#include <cctype>
#include <cmath>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>

#include <wx/sizer.h>
#include <wx/listbox.h>
#include <wx/vlbox.h>
#include <wx/dc.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/gauge.h>
#include <wx/spinctrl.h>
#include <wx/scrolwin.h>
#include <wx/textdlg.h>
#include <wx/textctrl.h>
#include <wx/filedlg.h>
#include <wx/colordlg.h>
#include <wx/choicdlg.h>
#include <wx/choice.h>
#include <wx/utils.h>
#include <wx/datetime.h>

#include <nlohmann/json.hpp>

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/StateColor.hpp"
#include "Widgets/WebView.hpp"

namespace Slic3r { namespace GUI {

using njson = nlohmann::json;

// --- PiggieSlicer palette (modern card dashboard) ---
static const wxColour PINK(236, 111, 166);      // #EC6FA6 accent
static const wxColour PINK_DK(194, 78, 134);    // #C24E86 accent dark
static const wxColour PINK_HOV(242, 145, 188);  // #F291BC hover
static const wxColour PINK_SOFT(252, 234, 242); // #FCEAF2 tint
static const wxColour PAGE_BG(243, 244, 247);   // app page behind the cards
static const wxColour CARD_BG(255, 255, 255);   // card fill
static const wxColour CARD_BORDER(228, 230, 236);
static const wxColour INK(58, 58, 66);          // primary text
static const wxColour INK_SOFT(124, 126, 134);  // secondary text
static const wxColour OK_GREEN(46, 160, 105);
static const wxColour WARN_GREY(150, 152, 160);
static const wxColour SECTION_TINT(248, 249, 252);
static const wxColour CONSOLE_BG(250, 251, 253);
static const wxColour CONSOLE_HDR(245, 246, 250);
static const wxColour CONSOLE_BORDER(228, 230, 236);
static const wxColour CONSOLE_TEXT(58, 58, 66);
static const wxColour CONSOLE_ACC(67, 118, 184);
static const wxColour CONSOLE_ERR(255, 128, 128);
static const char*    PRINTERS_KEY = "piggie_lan_printers";

// Owner-drawn printer list so the selection highlight is pink (not the system blue).
// Drop-in for the bits of wxListBox the panel uses: Append/Clear/Get/SetSelection +
// wxEVT_LISTBOX. Selection rendering is fully under our control.
class PinkPrinterList : public wxVListBox
{
public:
    PinkPrinterList(wxWindow* parent, const wxSize& size)
        : wxVListBox(parent, wxID_ANY, wxDefaultPosition, size, 0)
    {
        SetBackgroundColour(*wxWHITE);
    }
    void Append(const wxString& s) { m_items.Add(s); SetItemCount(m_items.size()); Refresh(); }
    void Clear()                   { m_items.Clear(); SetItemCount(0); Refresh(); }

protected:
    void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override
    {
        if (n >= m_items.size()) return;
        dc.SetFont(GetFont());
        dc.SetTextForeground(IsSelected(n) ? *wxWHITE : INK);
        wxRect r = rect; r.x += FromDIP(12);
        dc.DrawLabel(m_items[n], r, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
    }
    wxCoord OnMeasureItem(size_t) const override { return FromDIP(36); }
    void OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const override
    {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(IsSelected(n) ? PINK : *wxWHITE));
        dc.DrawRectangle(rect);
    }
private:
    wxArrayString m_items;
};

static wxString format_minutes_short(int minutes)
{
    if (minutes <= 0)
        return _L("Ready");
    const int hours = minutes / 60;
    const int mins  = minutes % 60;
    if (hours > 0)
        return wxString::Format(_L("%d hr %d min"), hours, mins);
    return wxString::Format(_L("%d min"), mins);
}

static wxString format_duration_hms(int total_seconds)
{
    total_seconds = std::max(total_seconds, 0);
    const int hours = total_seconds / 3600;
    const int mins  = (total_seconds % 3600) / 60;
    const int secs  = total_seconds % 60;
    return wxString::Format("%d:%02d:%02d", hours, mins, secs);
}

static int pct_from_gcode_speed(int raw)
{
    if (raw <= 100)
        return std::clamp(raw, 0, 100);
    return std::clamp((int)std::lround(100.0 * double(raw) / 255.0), 0, 100);
}

static int gcode_speed_from_pct(int pct)
{
    return std::clamp((int)std::lround(255.0 * double(std::clamp(pct, 0, 100)) / 100.0), 0, 255);
}

// profile: 0 = Low latency, 1 = Balanced, 2 = Smooth. The printer serves a single fixed-
// resolution FLV stream (no server-side quality switch exists), so these only trade
// player latency vs smoothness — Low latency feels the fastest/most responsive.
static wxString build_anycubic_camera_html(const std::string& ip, int profile)
{
    wxString html = wxString::FromUTF8(R"(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
html, body {
    margin: 0;
    width: 100%;
    height: 100%;
    overflow: hidden;
    background: #000;
    color: #f5f5f5;
    font-family: "Segoe UI", sans-serif;
}
#wrap {
    position: relative;
    width: 100%;
    height: 100%;
    background: #000;
    display: flex;
    align-items: center;
    justify-content: center;
}
video {
    width: auto;
    height: 100%;
    object-fit: contain;
    background: #000;
}
#status {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    text-align: center;
    padding: 20px;
    background: rgba(0, 0, 0, 0.28);
    font-size: 14px;
}
</style>
<script src="https://cdn.jsdelivr.net/npm/flv.js/dist/flv.min.js"></script>
</head>
<body>
<div id="wrap">
  <video id="video" autoplay muted playsinline></video>
  <div id="status">Connecting to camera...</div>
</div>
<script>
(function() {
  const streamUrl = "{{STREAM_URL}}";
  const status = document.getElementById("status");
  const video = document.getElementById("video");
  let player = null;

  function show(msg) {
    status.textContent = msg || "";
    status.style.display = msg ? "flex" : "none";
  }

  function cleanup() {
    if (!player) return;
    try { player.pause(); } catch (e) {}
    try { player.unload(); } catch (e) {}
    try { player.detachMediaElement(); } catch (e) {}
    try { player.destroy(); } catch (e) {}
    player = null;
  }

  window.addEventListener("beforeunload", cleanup);

  if (!window.flvjs || !flvjs.isSupported()) {
    show("Camera stream unavailable in this runtime.");
    return;
  }

  try {
    player = flvjs.createPlayer({
      type: "flv",
      url: streamUrl,
      isLive: true,
      hasAudio: false,
      hasVideo: true
    }, {
      enableStashBuffer: {{STASH_ENABLE}},
      stashInitialSize: {{STASH_SIZE}},
      lazyLoad: false,
      autoCleanupSourceBuffer: true,
      fixAudioTimestampGap: false
    });
    player.attachMediaElement(video);
    player.load();
    const started = player.play();
    if (started && typeof started.catch === "function") started.catch(function(){});
    player.on(flvjs.Events.METADATA_ARRIVED, function() { show(""); });
    player.on(flvjs.Events.ERROR, function() { show("Camera stream unavailable."); });
    video.addEventListener("playing", function(){ show(""); });
    // Keep the picture near the live edge — flv.js buffers, which looks like lag.
    setInterval(function(){
      try {
        if (video.buffered && video.buffered.length) {
          var end = video.buffered.end(video.buffered.length - 1);
          if (end - video.currentTime > {{SEEK_AHEAD}}) video.currentTime = end - 0.1;
        }
      } catch (e) {}
    }, {{SEEK_INTERVAL}});
  } catch (err) {
    cleanup();
    show("Camera stream unavailable.");
  }
})();
</script>
</body>
</html>)");
    html.Replace("{{STREAM_URL}}", from_u8("http://" + ip + ":18088/flv"));
    const char* stash_enable = "false"; const char* stash_size = "16";
    const char* seek_ahead = "0.3";     const char* seek_interval = "500";
    if (profile == 1) { stash_enable = "false"; stash_size = "128"; seek_ahead = "1.0"; seek_interval = "1500"; }
    else if (profile == 2) { stash_enable = "true"; stash_size = "384"; seek_ahead = "4.0"; seek_interval = "4000"; }
    html.Replace("{{STASH_ENABLE}}", stash_enable);
    html.Replace("{{STASH_SIZE}}", stash_size);
    html.Replace("{{SEEK_AHEAD}}", seek_ahead);
    html.Replace("{{SEEK_INTERVAL}}", seek_interval);
    return html;
}

static bool try_parse_int_param(const std::vector<std::string>& tokens, char key, int& out)
{
    const char upper_key = (char)std::toupper((unsigned char)key);
    for (const std::string& token_raw : tokens) {
        std::string token = boost::to_upper_copy(token_raw);
        if (token.size() >= 2 && token[0] == upper_key) {
            try {
                out = std::stoi(token.substr(1));
                return true;
            } catch (...) {
                return false;
            }
        }
    }
    return false;
}

static void style_segment_button(Button* button, bool active)
{
    if (!button) return;
    button->SetCornerRadius(button->FromDIP(8));
    button->SetPaddingSize(wxSize(button->FromDIP(18), button->FromDIP(8)));
    button->SetMinSize(wxSize(button->FromDIP(92), button->FromDIP(34)));
    if (active) {
        button->SetBorderWidth(0);
        button->SetBackgroundColor(StateColor(std::make_pair(PINK_DK, (int)StateColor::Pressed),
                                              std::make_pair(PINK_HOV, (int)StateColor::Hovered),
                                              std::make_pair(PINK, (int)StateColor::Normal)));
        button->SetTextColor(StateColor(*wxWHITE));
    } else {
        button->SetBorderWidth(1);
        button->SetBorderColor(StateColor(std::make_pair(CARD_BORDER, (int)StateColor::Hovered),
                                          std::make_pair(CARD_BORDER, (int)StateColor::Normal)));
        button->SetBackgroundColor(StateColor(std::make_pair(SECTION_TINT, (int)StateColor::Hovered),
                                              std::make_pair(SECTION_TINT, (int)StateColor::Normal)));
        button->SetTextColor(StateColor(INK_SOFT));
    }
}

static void style_control_button(Button* button, const wxSize& min_size, bool accent = false)
{
    if (!button) return;
    button->SetCornerRadius(button->FromDIP(12));
    button->SetPaddingSize(wxSize(button->FromDIP(12), button->FromDIP(10)));
    button->SetMinSize(min_size);
    if (accent) {
        button->SetBorderWidth(0);
        button->SetBackgroundColor(StateColor(std::make_pair(PINK_DK, (int)StateColor::Pressed),
                                              std::make_pair(PINK_HOV, (int)StateColor::Hovered),
                                              std::make_pair(PINK_SOFT, (int)StateColor::Normal)));
        button->SetTextColor(StateColor(PINK_DK));
    } else {
        button->SetBorderWidth(1);
        button->SetBorderColor(StateColor(std::make_pair(CARD_BORDER, (int)StateColor::Hovered),
                                          std::make_pair(CARD_BORDER, (int)StateColor::Normal)));
        button->SetBackgroundColor(StateColor(std::make_pair(*wxWHITE, (int)StateColor::Hovered),
                                              std::make_pair(SECTION_TINT, (int)StateColor::Normal)));
        button->SetTextColor(StateColor(INK));
    }
}

// A pill toggle button whose label carries the on/off state; filled pink when on.
static void set_toggle_button(Button* b, const wxString& base, bool on)
{
    if (!b) return;
    b->SetLabel(base + (on ? wxString::FromUTF8(": On") : wxString::FromUTF8(": Off")));
    if (on) {
        b->SetBorderWidth(0);
        b->SetBackgroundColor(StateColor(std::make_pair(PINK_DK, (int)StateColor::Pressed),
                                         std::make_pair(PINK_HOV, (int)StateColor::Hovered),
                                         std::make_pair(PINK, (int)StateColor::Normal)));
        b->SetTextColor(StateColor(*wxWHITE));
    } else {
        b->SetBorderWidth(1);
        b->SetBorderColor(StateColor(std::make_pair(CARD_BORDER, (int)StateColor::Hovered),
                                     std::make_pair(CARD_BORDER, (int)StateColor::Normal)));
        b->SetBackgroundColor(StateColor(std::make_pair(*wxWHITE, (int)StateColor::Hovered),
                                         std::make_pair(SECTION_TINT, (int)StateColor::Normal)));
        b->SetTextColor(StateColor(INK));
    }
}

AnycubicDevicePanel::AnycubicDevicePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
    , m_poll_timer(this)
{
    m_alive = std::make_shared<std::atomic<bool>>(true);
    SetBackgroundColour(*wxWHITE);
    build_ui();
    load_printers();
    refresh_list();
    Bind(wxEVT_TIMER, &AnycubicDevicePanel::on_poll, this, m_poll_timer.GetId());
    maybe_auto_connect_on_launch();
}

AnycubicDevicePanel::~AnycubicDevicePanel()
{
    if (m_alive) m_alive->store(false);
    if (m_poll_timer.IsRunning()) m_poll_timer.Stop();
}

// ---------------------------------------------------------------- UI ----

wxWindow* AnycubicDevicePanel::make_section(wxWindow* parent, const wxString& title, wxSizer*& body_out)
{
    auto* card = new StaticBox(parent);
    card->SetBackgroundColor(StateColor(CARD_BG));
    card->SetBorderColor(StateColor(CARD_BORDER));
    card->SetBorderWidth(1);
    card->SetCornerRadius(FromDIP(12));
    card->SetBackgroundColour(CARD_BG);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    if (!title.empty()) {
        auto* hdr = new Label(card, Label::Head_14, title);
        hdr->SetForegroundColour(PINK_DK);
        hdr->SetBackgroundColour(CARD_BG);
        outer->Add(hdr, 0, wxLEFT | wxTOP | wxRIGHT, FromDIP(16));
        outer->AddSpacer(FromDIP(10));
    }

    auto* body = new wxBoxSizer(wxVERTICAL);
    outer->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    card->SetSizer(outer);
    body_out = body;
    return card;
}

// Themed pill button used across the panel.
Button* AnycubicDevicePanel::make_button(wxWindow* parent, const wxString& label, bool primary)
{
    auto* b = new Button(parent, label);
    b->SetCornerRadius(FromDIP(6));
    b->SetPaddingSize(wxSize(FromDIP(12), FromDIP(7)));
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

void AnycubicDevicePanel::set_jog_distance(double mm)
{
    m_jog_distance_mm = mm;
    refresh_jog_distance_buttons();
}

void AnycubicDevicePanel::refresh_jog_distance_buttons()
{
    style_segment_button(m_btn_jog_1mm,  std::abs(m_jog_distance_mm - 1.0)  < 0.01);
    style_segment_button(m_btn_jog_15mm, std::abs(m_jog_distance_mm - 15.0) < 0.01);
    style_segment_button(m_btn_jog_50mm, std::abs(m_jog_distance_mm - 50.0) < 0.01);
}

void AnycubicDevicePanel::append_console_line(const wxString& line, const wxColour& colour)
{
    if (!m_console_log) return;
    m_console_log->SetDefaultStyle(wxTextAttr(colour));
    m_console_log->AppendText(wxDateTime::Now().Format("%H:%M  ") + line + "\n");
    // cap the buffer so streamed telemetry can't grow it without bound
    if (m_console_log->GetNumberOfLines() > 600) {
        long cut = m_console_log->XYToPosition(0, 200);
        if (cut > 0) m_console_log->Remove(0, cut);
    }
    m_console_log->ShowPosition(m_console_log->GetLastPosition());
}

void AnycubicDevicePanel::set_camera_placeholder(const wxString& text)
{
    // Show the centered "Not available" panel; stop any running stream.
    if (m_camera_placeholder_label)
        m_camera_placeholder_label->SetLabel(text.empty() ? _L("Not available") : text);
    if (m_camera_placeholder)
        m_camera_placeholder->Show();
    if (m_camera_view)
        m_camera_view->Hide();
    if (!m_camera_stream_ip.empty() && m_camera_view)
        m_camera_view->SetPage("<html><body style='background:#000;'></body></html>", "");
    m_camera_stream_ip.clear();
    if (m_camera_placeholder && m_camera_placeholder->GetParent())
        m_camera_placeholder->GetParent()->Layout();
}

void AnycubicDevicePanel::refresh_camera_preview()
{
    if (!m_camera_view || !m_camera_placeholder)
        return;

    if (!m_camera_enabled) {
        set_camera_placeholder(_L("Camera off"));
        return;
    }

    if (!m_connected || !m_status.has_camera || m_creds.ip.empty()) {
        set_camera_placeholder(wxEmptyString);
        return;
    }

    // Already streaming this printer — leave the live view alone (don't reload per poll).
    if (m_camera_stream_ip == m_creds.ip && m_camera_view->IsShown())
        return;

    m_camera_stream_ip = m_creds.ip;
    m_camera_placeholder->Hide();
    m_camera_view->Show();
    m_camera_view->SetPage(build_anycubic_camera_html(m_creds.ip, m_cam_profile), from_u8("http://" + m_creds.ip + "/"));
    if (m_camera_placeholder->GetParent())
        m_camera_placeholder->GetParent()->Layout();
}

void AnycubicDevicePanel::build_ui()
{
    SetBackgroundColour(PAGE_BG);
    auto* root = new wxBoxSizer(wxHORIZONTAL);

    auto* sidebar = new wxPanel(this, wxID_ANY);
    sidebar->SetBackgroundColour(PAGE_BG);
    auto* left = new wxBoxSizer(wxVERTICAL);

    wxSizer* printer_body = nullptr;
    auto* printer_card = make_section(sidebar, _L("Printers"), printer_body);
    m_list = new PinkPrinterList(printer_card, wxSize(FromDIP(216), -1));
    m_list->SetMinSize(wxSize(FromDIP(216), FromDIP(440)));
    m_list->SetFont(Label::Body_13);
    m_list->Bind(wxEVT_LISTBOX, &AnycubicDevicePanel::on_select, this);
    printer_body->Add(m_list, 1, wxEXPAND | wxBOTTOM, FromDIP(12));

    auto* lb1 = new wxBoxSizer(wxHORIZONTAL);
    auto* b_scan = make_button(printer_card, _L("Scan"), true);
    auto* b_add  = make_button(printer_card, _L("Add IP"), false);
    b_scan->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_discover, this);
    b_add->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_add_ip, this);
    lb1->Add(b_scan, 1, wxRIGHT, FromDIP(5)); lb1->Add(b_add, 1);
    printer_body->Add(lb1, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* lb2 = new wxBoxSizer(wxHORIZONTAL);
    auto* b_ren = make_button(printer_card, _L("Rename"), false);
    auto* b_rem = make_button(printer_card, _L("Remove"), false);
    b_ren->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_rename, this);
    b_rem->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_remove, this);
    lb2->Add(b_ren, 1, wxRIGHT, FromDIP(5)); lb2->Add(b_rem, 1);
    printer_body->Add(lb2, 0, wxEXPAND);
    left->Add(printer_card, 1, wxEXPAND | wxALL, FromDIP(10));
    sidebar->SetSizer(left);
    root->Add(sidebar, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(10));

    m_content = new wxScrolledWindow(this, wxID_ANY);
    m_content->SetScrollRate(FromDIP(12), FromDIP(12));
    m_content->SetBackgroundColour(PAGE_BG);
    auto* col = new wxBoxSizer(wxVERTICAL);

    {
        auto* head = new StaticBox(m_content);
        head->SetBackgroundColor(StateColor(CARD_BG));
        head->SetBorderColor(StateColor(CARD_BORDER));
        head->SetBorderWidth(1);
        head->SetCornerRadius(FromDIP(12));
        head->SetBackgroundColour(CARD_BG);

        auto* hs = new wxBoxSizer(wxVERTICAL);
        auto* title_row = new wxBoxSizer(wxHORIZONTAL);
        m_lbl_name = new Label(head, Label::Head_24, _L("No printer selected"));
        m_lbl_name->SetForegroundColour(INK);
        m_lbl_name->SetBackgroundColour(CARD_BG);
        title_row->Add(m_lbl_name, 0, wxALIGN_CENTER_VERTICAL);
        title_row->AddSpacer(FromDIP(14));
        m_lbl_state = new Label(head, Label::Body_14, wxString::FromUTF8("\xE2\x97\x8F ") + _L("Idle"));
        m_lbl_state->SetForegroundColour(WARN_GREY);
        m_lbl_state->SetBackgroundColour(CARD_BG);
        title_row->Add(m_lbl_state, 0, wxALIGN_CENTER_VERTICAL);
        title_row->AddStretchSpacer();
        hs->Add(title_row, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, FromDIP(16));

        hs->AddSpacer(FromDIP(10));
        m_progress = new ProgressBar(head, wxID_ANY, 100);
        m_progress->ShowNumber(false);
        m_progress->SetProgressForedColour(wxColour(219, 221, 226));
        m_progress->SetProgressBackgroundColour(PINK);
        m_progress->SetHeight(FromDIP(18));
        m_progress->SetMinSize(wxSize(-1, FromDIP(18)));
        hs->Add(m_progress, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        hs->AddSpacer(FromDIP(10));
        auto* meta_row = new wxBoxSizer(wxHORIZONTAL);
        m_lbl_job = new Label(head, Label::Body_14, _L("No active job"));
        m_lbl_job->SetForegroundColour(INK);
        m_lbl_job->SetBackgroundColour(CARD_BG);
        meta_row->Add(m_lbl_job, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
        m_lbl_time = new Label(head, Label::Body_13, "");
        m_lbl_time->SetForegroundColour(INK_SOFT);
        m_lbl_time->SetBackgroundColour(CARD_BG);
        meta_row->Add(m_lbl_time, 0, wxALIGN_CENTER_VERTICAL);
        meta_row->AddStretchSpacer();
        hs->Add(meta_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        hs->AddSpacer(FromDIP(8));
        auto* foot_row = new wxBoxSizer(wxHORIZONTAL);
        m_lbl_ip = new Label(head, Label::Body_12, "");
        m_lbl_ip->SetForegroundColour(INK_SOFT);
        m_lbl_ip->SetBackgroundColour(CARD_BG);
        foot_row->Add(m_lbl_ip, 0, wxALIGN_CENTER_VERTICAL);
        foot_row->AddStretchSpacer();
        m_lbl_msg = new Label(head, Label::Body_12, "");
        m_lbl_msg->SetForegroundColour(PINK_DK);
        m_lbl_msg->SetBackgroundColour(CARD_BG);
        foot_row->Add(m_lbl_msg, 0, wxALIGN_CENTER_VERTICAL);
        hs->Add(foot_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

        head->SetSizer(hs);
        col->Add(head, 0, wxEXPAND | wxALL, FromDIP(10));
    }

    auto* cols = new wxBoxSizer(wxHORIZONTAL);
    auto* colL = new wxBoxSizer(wxVERTICAL);
    auto* colR = new wxBoxSizer(wxVERTICAL);

    {
        wxSizer* body = nullptr;
        auto* sec = make_section(m_content, _L("Controls"), body);
        sec->SetMinSize(wxSize(FromDIP(560), -1));

        auto* step_row = new wxBoxSizer(wxHORIZONTAL);
        m_btn_jog_1mm  = make_button(sec, _L("1mm"), false);
        m_btn_jog_15mm = make_button(sec, _L("15mm"), false);
        m_btn_jog_50mm = make_button(sec, _L("50mm"), false);
        m_btn_jog_1mm->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_jog_distance(1.0); });
        m_btn_jog_15mm->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_jog_distance(15.0); });
        m_btn_jog_50mm->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_jog_distance(50.0); });
        step_row->Add(m_btn_jog_1mm, 0, wxRIGHT, FromDIP(6));
        step_row->Add(m_btn_jog_15mm, 0, wxRIGHT, FromDIP(6));
        step_row->Add(m_btn_jog_50mm, 0);
        body->Add(step_row, 0, wxBOTTOM, FromDIP(12));

        auto* move_wrap = new StaticBox(sec);
        move_wrap->SetCornerRadius(FromDIP(10));
        move_wrap->SetBorderWidth(0);
        move_wrap->SetBorderColor(StateColor(CARD_BORDER));
        move_wrap->SetBackgroundColor(StateColor(CARD_BG));
        move_wrap->SetBackgroundColour(CARD_BG);
        auto* move_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto* home_col = new wxBoxSizer(wxVERTICAL);
        auto* b_home_all = make_button(move_wrap, _L("HOME"), false);
        style_control_button(b_home_all, wxSize(FromDIP(82), FromDIP(76)), true);
        b_home_all->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_home_all, this);
        home_col->AddStretchSpacer();
        home_col->Add(b_home_all, 0, wxBOTTOM, FromDIP(10));
        home_col->AddStretchSpacer();
        move_sizer->Add(home_col, 0, wxRIGHT, FromDIP(14));

        auto* xy_grid = new wxFlexGridSizer(3, 3, FromDIP(8), FromDIP(8));
        xy_grid->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_NONE);
        auto add_spacer = [&]() { xy_grid->AddSpacer(FromDIP(18)); };
        auto add_jog = [&](const wxString& label, int axis, int move_type, bool accent = false) {
            auto* b = make_button(move_wrap, label, false);
            style_control_button(b, wxSize(FromDIP(78), FromDIP(44)), accent);
            b->Bind(wxEVT_BUTTON, [this, axis, move_type](wxCommandEvent&) { on_jog(axis, move_type); });
            xy_grid->Add(b, 0);
        };
        add_spacer();                                      add_jog("Y+", AcLan::AxisY, AcLan::MovePlus);  add_spacer();
        add_jog("X-", AcLan::AxisX, AcLan::MoveMinus);     add_jog(_L("HOME XY"), AcLan::AxisAll, AcLan::MoveHome, true); add_jog("X+", AcLan::AxisX, AcLan::MovePlus);
        add_spacer();                                      add_jog("Y-", AcLan::AxisY, AcLan::MoveMinus); add_spacer();
        move_sizer->Add(xy_grid, 0, wxRIGHT, FromDIP(14));

        auto* z_col = new wxBoxSizer(wxVERTICAL);
        auto* b_z_plus = make_button(move_wrap, "Z+", false);
        auto* b_z_home = make_button(move_wrap, _L("HOME Z"), false);
        auto* b_z_minus = make_button(move_wrap, "Z-", false);
        style_control_button(b_z_plus, wxSize(FromDIP(88), FromDIP(44)));
        style_control_button(b_z_home, wxSize(FromDIP(88), FromDIP(52)), true);
        style_control_button(b_z_minus, wxSize(FromDIP(88), FromDIP(44)));
        b_z_plus->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_jog(AcLan::AxisZ, AcLan::MovePlus); });
        b_z_home->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_jog(AcLan::AxisZ, AcLan::MoveHome); });
        b_z_minus->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_jog(AcLan::AxisZ, AcLan::MoveMinus); });
        z_col->Add(b_z_plus, 0, wxBOTTOM, FromDIP(8));
        z_col->Add(b_z_home, 0, wxBOTTOM, FromDIP(8));
        z_col->Add(b_z_minus, 0);
        move_sizer->Add(z_col, 0);

        move_wrap->SetSizer(move_sizer);
        body->Add(move_wrap, 0, wxBOTTOM, FromDIP(12));

        auto* action_row = new wxBoxSizer(wxHORIZONTAL);
        m_btn_pause  = make_button(sec, _L("Pause"),  false);
        m_btn_resume = make_button(sec, _L("Resume"), false);
        m_btn_stop   = make_button(sec, _L("Stop"),   false);
        m_btn_motors_off = make_button(sec, _L("Motors Off"), false);
        style_control_button(m_btn_pause, wxSize(FromDIP(92), FromDIP(34)));
        style_control_button(m_btn_resume, wxSize(FromDIP(92), FromDIP(34)));
        style_control_button(m_btn_stop, wxSize(FromDIP(92), FromDIP(34)));
        style_control_button(m_btn_motors_off, wxSize(FromDIP(110), FromDIP(34)));
        m_btn_pause->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_pause, this);
        m_btn_resume->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_resume, this);
        m_btn_stop->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_stop, this);
        m_btn_motors_off->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_motors_off, this);
        action_row->Add(m_btn_pause, 0, wxRIGHT, FromDIP(8));
        action_row->Add(m_btn_resume, 0, wxRIGHT, FromDIP(8));
        action_row->Add(m_btn_stop, 0, wxRIGHT, FromDIP(8));
        action_row->Add(m_btn_motors_off, 0);
        m_btn_resume->Hide();   // shown only while paused (Pause/Resume swap by state)
        body->Add(action_row, 0);
        colL->Add(sec, 0, wxEXPAND | wxALL, FromDIP(8));
    }

    {
        wxSizer* body = nullptr;
        auto* sec = make_section(m_content, _L("Temperatures / Fans"), body);
        sec->SetMinSize(wxSize(FromDIP(560), -1));
        auto add_temp = [&](const wxString& lbl, wxStaticText*& out_lbl, wxSpinCtrl*& out_spin, int lo, int hi,
                            void (AnycubicDevicePanel::*handler)(wxCommandEvent&)) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            out_lbl = new Label(sec, Label::Body_13, lbl + wxString::FromUTF8(":  \xE2\x80\x94 / \xE2\x80\x94"));
            out_lbl->SetForegroundColour(INK);
            out_lbl->SetBackgroundColour(CARD_BG);
            out_lbl->SetMinSize(wxSize(FromDIP(180), -1));
            row->Add(out_lbl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
            out_spin = new wxSpinCtrl(sec, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(78), -1));
            out_spin->SetRange(lo, hi);
            row->Add(out_spin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
            auto* set = make_button(sec, _L("Set"), false);
            style_control_button(set, wxSize(FromDIP(68), FromDIP(32)));
            set->Bind(wxEVT_BUTTON, handler, this);
            row->Add(set, 0, wxALIGN_CENTER_VERTICAL);
            body->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
        };
        add_temp(_L("Nozzle"),     m_lbl_nozzle, m_spin_nozzle, 0, 320, &AnycubicDevicePanel::on_set_nozzle);
        add_temp(_L("Bed"),        m_lbl_bed,    m_spin_bed,    0, 120, &AnycubicDevicePanel::on_set_bed);
        add_temp(_L("Part fan"),   m_lbl_fan,    m_spin_fan,    0, 100, &AnycubicDevicePanel::on_set_fan);
        m_lbl_aux = new Label(sec, Label::Body_12, _L("Aux fan:") + wxString::FromUTF8(" \xE2\x80\x94%"));
        m_lbl_aux->SetForegroundColour(INK_SOFT);
        m_lbl_aux->SetBackgroundColour(CARD_BG);
        body->Add(m_lbl_aux, 0, wxTOP, FromDIP(2));
        colL->Add(sec, 0, wxEXPAND | wxALL, FromDIP(8));
    }

    {
        wxSizer* body = nullptr;
        auto* sec = make_section(m_content, _L("ACE / Materials"), body);
        sec->SetMinSize(wxSize(FromDIP(560), -1));
        m_ace_body = new wxBoxSizer(wxHORIZONTAL);
        body->Add(m_ace_body, 0, wxEXPAND | wxBOTTOM, FromDIP(14));
        auto* hint = new Label(sec, Label::Body_11, _L("No materials reported yet."));
        hint->SetForegroundColour(INK_SOFT);
        hint->SetBackgroundColour(CARD_BG);
        m_ace_body->Add(hint, 0, wxBOTTOM, FromDIP(4));

        m_lbl_ace_env = new Label(sec, Label::Body_12, _L("Temperature:  -    Humidity:  -"));
        m_lbl_ace_env->SetForegroundColour(INK_SOFT);
        m_lbl_ace_env->SetBackgroundColour(CARD_BG);
        body->Add(m_lbl_ace_env, 0, wxBOTTOM, FromDIP(12));

        auto* ace_row1 = new wxBoxSizer(wxHORIZONTAL);
        m_btn_ace_refill = make_button(sec, _L("Auto Refill: Off"), false);
        style_control_button(m_btn_ace_refill, wxSize(FromDIP(150), FromDIP(34)));
        m_btn_ace_refill->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_toggle_ace_auto_feed, this);
        m_btn_ace_dry = make_button(sec, _L("Drying: Off"), false);
        style_control_button(m_btn_ace_dry, wxSize(FromDIP(150), FromDIP(34)));
        m_btn_ace_dry->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_toggle_ace_dry, this);
        ace_row1->Add(m_btn_ace_refill, 0, wxRIGHT, FromDIP(8));
        ace_row1->Add(m_btn_ace_dry, 0);
        body->Add(ace_row1, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

        m_lbl_ace_dry_info = new Label(sec, Label::Body_12, _L("Drying temperature: 45C   Drying time: 4:00:00"));
        m_lbl_ace_dry_info->SetForegroundColour(INK_SOFT);
        m_lbl_ace_dry_info->SetBackgroundColour(CARD_BG);
        body->Add(m_lbl_ace_dry_info, 0);
        m_ace_section = sec;
        colL->Add(sec, 0, wxEXPAND | wxALL, FromDIP(8));
    }

    {
        wxSizer* body = nullptr;
        auto* sec = make_section(m_content, _L("Camera"), body);
        sec->SetMinSize(wxSize(FromDIP(520), -1));

        auto* preview = new StaticBox(sec);
        preview->SetCornerRadius(FromDIP(10));
        preview->SetBorderWidth(1);
        preview->SetBorderColor(StateColor(CARD_BORDER));
        preview->SetBackgroundColor(StateColor(*wxBLACK));
        preview->SetBackgroundColour(*wxBLACK);
        preview->SetMinSize(wxSize(FromDIP(520), FromDIP(300)));
        auto* preview_sizer = new wxBoxSizer(wxVERTICAL);
        m_camera_placeholder = new wxPanel(preview, wxID_ANY);
        m_camera_placeholder->SetBackgroundColour(*wxBLACK);
        auto* placeholder_sizer = new wxBoxSizer(wxVERTICAL);
        m_camera_placeholder_label = new Label(m_camera_placeholder, Label::Body_13, _L("Not available"));
        m_camera_placeholder_label->SetForegroundColour(wxColour(170, 172, 178));
        m_camera_placeholder_label->SetBackgroundColour(*wxBLACK);
        placeholder_sizer->AddStretchSpacer();
        placeholder_sizer->Add(m_camera_placeholder_label, 0, wxALIGN_CENTER);
        placeholder_sizer->AddStretchSpacer();
        m_camera_placeholder->SetSizer(placeholder_sizer);
        preview_sizer->Add(m_camera_placeholder, 1, wxEXPAND);
        m_camera_view = WebView::CreateWebView(preview, wxEmptyString);
        m_camera_view->EnableContextMenu(false);
        m_camera_view->Hide();
        preview_sizer->Add(m_camera_view, 1, wxEXPAND);
        preview->SetSizer(preview_sizer);
        body->Add(preview, 1, wxEXPAND | wxBOTTOM, FromDIP(12));

        auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
        m_btn_light = make_button(sec, _L("Head Light: Off"), false);
        style_control_button(m_btn_light, wxSize(FromDIP(150), FromDIP(34)));
        m_btn_light->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_toggle_light, this);
        m_btn_cam_light = make_button(sec, _L("Cam Light: Off"), false);
        style_control_button(m_btn_cam_light, wxSize(FromDIP(150), FromDIP(34)));
        m_btn_cam_light->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_toggle_cam_light, this);
        m_btn_camera_capture = make_button(sec, _L("Camera: On"), false);
        style_control_button(m_btn_camera_capture, wxSize(FromDIP(130), FromDIP(34)));
        m_btn_camera_capture->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_toggle_camera, this);
        toolbar->Add(m_btn_light, 0, wxRIGHT, FromDIP(8));
        toolbar->Add(m_btn_cam_light, 0, wxRIGHT, FromDIP(8));
        toolbar->Add(m_btn_camera_capture, 0, wxRIGHT, FromDIP(8));
        toolbar->AddStretchSpacer();
        auto* qlbl = new Label(sec, Label::Body_12, _L("Stream"));
        qlbl->SetForegroundColour(INK_SOFT);
        qlbl->SetBackgroundColour(CARD_BG);
        toolbar->Add(qlbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        m_cam_quality = new wxChoice(sec, wxID_ANY);
        m_cam_quality->Append(_L("Low latency"));
        m_cam_quality->Append(_L("Balanced"));
        m_cam_quality->Append(_L("Smooth"));
        m_cam_quality->SetSelection(0);
        m_cam_quality->Bind(wxEVT_CHOICE, [this](wxCommandEvent&){
            m_cam_profile = m_cam_quality->GetSelection();
            m_camera_stream_ip.clear();   // force the preview to reload with the new profile
            refresh_camera_preview();
        });
        toolbar->Add(m_cam_quality, 0, wxALIGN_CENTER_VERTICAL);
        body->Add(toolbar, 0, wxEXPAND);
        colR->Add(sec, 1, wxEXPAND | wxALL, FromDIP(8));
    }

    {
        wxSizer* body = nullptr;
        auto* sec = make_section(m_content, _L("Console"), body);

        auto* console_shell = new StaticBox(sec);
        console_shell->SetCornerRadius(FromDIP(10));
        console_shell->SetBorderWidth(1);
        console_shell->SetBorderColor(StateColor(CONSOLE_BORDER));
        console_shell->SetBackgroundColor(StateColor(CONSOLE_BG));
        console_shell->SetBackgroundColour(CONSOLE_BG);

        auto* console_sizer = new wxBoxSizer(wxVERTICAL);
        auto* header = new wxPanel(console_shell, wxID_ANY);
        header->SetBackgroundColour(CONSOLE_HDR);
        auto* header_row = new wxBoxSizer(wxHORIZONTAL);
        auto* header_title = new Label(header, Label::Body_13, _L("Local control"));
        header_title->SetForegroundColour(INK);
        header_title->SetBackgroundColour(CONSOLE_HDR);
        header_row->Add(header_title, 0, wxALIGN_CENTER_VERTICAL);
        header_row->AddStretchSpacer();
        auto make_header_hint = [&](const wxString& text) {
            auto* hint = new Label(header, Label::Body_11, text);
            hint->SetForegroundColour(INK_SOFT);
            hint->SetBackgroundColour(CONSOLE_HDR);
            return hint;
        };
        header_row->Add(make_header_hint(_L("Supported: M104 M140 M106 M107 G28 M18 M84 M355")), 0, wxALIGN_CENTER_VERTICAL);
        header->SetSizer(header_row);
        console_sizer->Add(header, 0, wxEXPAND | wxALL, FromDIP(12));

        auto* input_row = new wxBoxSizer(wxHORIZONTAL);
        m_console_input = new wxTextCtrl(console_shell, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_NONE);
        m_console_input->SetHint(_L("Send code..."));
        m_console_input->SetMinSize(wxSize(-1, FromDIP(34)));
        m_console_input->SetBackgroundColour(*wxWHITE);
        m_console_input->SetForegroundColour(CONSOLE_TEXT);
        m_console_input->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
        m_console_input->Bind(wxEVT_TEXT_ENTER, &AnycubicDevicePanel::on_console_send, this);
        input_row->Add(m_console_input, 1, wxEXPAND | wxRIGHT, FromDIP(8));
        m_btn_console_send = make_button(console_shell, _L("Send"), true);
        style_control_button(m_btn_console_send, wxSize(FromDIP(78), FromDIP(34)), true);
        m_btn_console_send->Bind(wxEVT_BUTTON, &AnycubicDevicePanel::on_console_send, this);
        input_row->Add(m_btn_console_send, 0, wxALIGN_CENTER_VERTICAL);
        console_sizer->Add(input_row, 0, wxEXPAND | wxALL, FromDIP(12));

        m_console_log = new wxTextCtrl(console_shell, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxBORDER_NONE);
        m_console_log->SetMinSize(wxSize(-1, FromDIP(280)));
        m_console_log->SetBackgroundColour(*wxWHITE);
        m_console_log->SetForegroundColour(CONSOLE_TEXT);
        m_console_log->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
        console_sizer->Add(m_console_log, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

        console_shell->SetSizer(console_sizer);
        body->Add(console_shell, 1, wxEXPAND);
        colR->Add(sec, 1, wxEXPAND | wxALL, FromDIP(8));
    }

    cols->Add(colL, 0, wxEXPAND | wxRIGHT, FromDIP(8));
    cols->Add(colR, 1, wxEXPAND);
    col->Add(cols, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(2));

    m_content->SetSizer(col);
    root->Add(m_content, 1, wxEXPAND | wxALL, FromDIP(10));
    SetSizer(root);

    set_jog_distance(1.0);
    set_controls_enabled(false);
    append_console_line(_L("Console ready. Supported: M104, M140, M106, M107, G28, M18, M84, M355."), CONSOLE_TEXT);
}

// ------------------------------------------------------ printer store ----

void AnycubicDevicePanel::load_printers()
{
    m_printers.clear();
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;
    std::string raw = cfg->get(PRINTERS_KEY);
    if (raw.empty()) return;
    try {
        njson j = njson::parse(raw);
        for (auto& e : j) {
            Printer p;
            p.name       = e.value("name", "");
            p.ip         = e.value("ip", "");
            p.printer_id = e.value("printer_id", "");
            p.usn        = e.value("usn", "");
            p.model      = e.value("model", "");
            if (!p.ip.empty()) m_printers.push_back(std::move(p));
        }
    } catch (...) {}
}

void AnycubicDevicePanel::save_printers()
{
    auto* cfg = wxGetApp().app_config;
    if (!cfg) return;
    njson j = njson::array();
    for (auto& p : m_printers)
        j.push_back({{"name", p.name}, {"ip", p.ip}, {"printer_id", p.printer_id}, {"usn", p.usn}, {"model", p.model}});
    cfg->set(PRINTERS_KEY, j.dump());
    cfg->save();
}

void AnycubicDevicePanel::refresh_list()
{
    m_list->Clear();
    for (auto& p : m_printers) m_list->Append(from_u8(p.name.empty() ? p.ip : p.name));
    if (m_selected >= 0 && m_selected < (int)m_printers.size())
        m_list->SetSelection(m_selected);
}

int AnycubicDevicePanel::find_printer(const std::string& usn, const std::string& ip) const
{
    for (int i = 0; i < (int)m_printers.size(); ++i)
        if ((!usn.empty() && m_printers[i].usn == usn) || (!ip.empty() && m_printers[i].ip == ip))
            return i;
    return -1;
}

void AnycubicDevicePanel::maybe_auto_connect_on_launch()
{
    static bool attempted_this_session = false;
    if (attempted_this_session || m_printers.empty())
        return;
    attempted_this_session = true;

    auto found = std::make_shared<std::vector<AcLanDevice>>();
    run_async(
        [found]() {
            try { *found = AcLan::discover(2500); } catch (...) {}
        },
        [this, found]() {
            if (m_selected >= 0 || m_connected || !found || found->empty())
                return;

            for (const AcLanDevice& d : *found) {
                for (int i = 0; i < (int)m_printers.size(); ++i) {
                    const bool same_usn = !d.usn.empty() && !m_printers[i].usn.empty() && d.usn == m_printers[i].usn;
                    const bool same_ip  = !d.ip.empty() && d.ip == m_printers[i].ip;
                    if (!same_usn && !same_ip)
                        continue;

                    m_printers[i].ip = d.ip.empty() ? m_printers[i].ip : d.ip;
                    if (!d.usn.empty()) m_printers[i].usn = d.usn;
                    if (!d.model_name.empty()) {
                        m_printers[i].model = d.model_name;
                        if (m_printers[i].name.empty() || m_printers[i].name == m_printers[i].ip)
                            m_printers[i].name = d.model_name;
                    }
                    save_printers();
                    m_selected = i;
                    refresh_list();
                    append_console_line(_L("Auto-connecting to saved Anycubic printer: ") + from_u8(m_printers[i].ip), CONSOLE_TEXT);
                    connect_selected();
                    return;
                }
            }
        });
}

// ---------------------------------------------------- sidebar actions ----

void AnycubicDevicePanel::on_select(wxCommandEvent&)
{
    int sel = m_list->GetSelection();
    if (sel == wxNOT_FOUND) return;
    m_selected = sel;
    connect_selected();
}

void AnycubicDevicePanel::on_discover(wxCommandEvent&)
{
    set_msg(_L("Scanning the network..."));
    append_console_line(_L("Scanning local network for Anycubic printers."), CONSOLE_TEXT);
    run_async(
        [this]() {
            std::vector<AcLanDevice> found;
            try { found = AcLan::discover(3000); } catch (...) {}
            // stash on a member-safe path: capture by copy into done via shared_ptr
            m_scan_result = std::make_shared<std::vector<AcLanDevice>>(std::move(found));
        },
        [this]() {
            int added = 0;
            if (m_scan_result) {
                for (auto& d : *m_scan_result) {
                    if (find_printer(d.usn, d.ip) >= 0) continue;
                    Printer p; p.ip = d.ip; p.usn = d.usn; p.model = d.model_name;
                    p.name = d.model_name.empty() ? d.ip : d.model_name;
                    p.printer_id = d.model_id;
                    m_printers.push_back(std::move(p));
                    ++added;
                }
                m_scan_result.reset();
            }
            if (added > 0) { save_printers(); refresh_list(); }
            append_console_line(wxString::Format(_L("Scan complete. %d printer(s) added."), added), CONSOLE_TEXT);
            set_msg(wxString::Format(_L("Scan complete: %d new printer(s)."), added));
        });
}

void AnycubicDevicePanel::on_add_ip(wxCommandEvent&)
{
    wxTextEntryDialog dlg(this, _L("Printer IP address:"), _L("Add Anycubic printer"));
    if (dlg.ShowModal() != wxID_OK) return;
    std::string ip = into_u8(dlg.GetValue());
    boost::trim(ip);
    if (ip.empty()) return;
    if (find_printer("", ip) >= 0) { set_msg(_L("That printer is already in the list.")); return; }
    Printer p; p.ip = ip; p.name = ip;
    m_printers.push_back(p);
    save_printers();
    refresh_list();
    append_console_line(_L("Added printer IP: ") + from_u8(ip), CONSOLE_TEXT);
    set_msg(_L("Added. Select it to connect."));
}

void AnycubicDevicePanel::on_rename(wxCommandEvent&)
{
    if (m_selected < 0 || m_selected >= (int)m_printers.size()) return;
    wxTextEntryDialog dlg(this, _L("Nickname:"), _L("Rename printer"), from_u8(m_printers[m_selected].name));
    if (dlg.ShowModal() != wxID_OK) return;
    std::string nm = into_u8(dlg.GetValue());
    boost::trim(nm);
    if (nm.empty()) return;
    m_printers[m_selected].name = nm;
    save_printers();
    refresh_list();
    append_console_line(_L("Renamed printer to: ") + from_u8(nm), CONSOLE_TEXT);
}

void AnycubicDevicePanel::on_remove(wxCommandEvent&)
{
    if (m_selected < 0 || m_selected >= (int)m_printers.size()) return;
    const wxString DEG = wxString::FromUTF8(" \xC2\xB0""C");
    m_printers.erase(m_printers.begin() + m_selected);
    m_selected = -1;
    m_connected = false;
    if (m_poll_timer.IsRunning()) m_poll_timer.Stop();
    save_printers();
    refresh_list();
    append_console_line(_L("Removed printer from the local list."), CONSOLE_TEXT);
    m_lbl_name->SetLabel(_L("No printer selected"));
    m_lbl_state->SetForegroundColour(WARN_GREY);
    m_lbl_state->SetLabel(wxString::FromUTF8("\xE2\x97\x8F ") + _L("Idle"));
    m_lbl_ip->SetLabel("");
    m_lbl_job->SetLabel(_L("No active job"));
    m_lbl_time->SetLabel(_L("Ready"));
    m_progress->SetProgress(0);
    m_lbl_nozzle->SetLabel(_L("Nozzle:  - / -") + DEG);
    m_lbl_bed->SetLabel(_L("Bed:  - / -") + DEG);
    m_lbl_fan->SetLabel(_L("Part fan:  0%"));
    m_lbl_aux->SetLabel(_L("Aux fan: 0%"));
    m_lbl_ace_env->SetLabel(_L("Temperature:  -    Humidity:  -"));
    m_lbl_ace_dry_info->SetLabel(_L("Drying temperature: 45C   Drying time: 4:00:00"));
    if (m_ace_body) {
        m_ace_body->Clear(true);
        auto* hint = new Label((wxWindow*)m_ace_section, Label::Body_11, _L("No materials reported yet."));
        hint->SetForegroundColour(INK_SOFT);
        hint->SetBackgroundColour(CARD_BG);
        m_ace_body->Add(hint, 0, wxBOTTOM, FromDIP(4));
    }
    m_ace_sig.clear();
    m_light_on = m_cam_light_on = m_ace_auto_feed = m_ace_dry = false;
    m_camera_enabled = true;
    set_toggle_button(m_btn_light, _L("Head Light"), false);
    set_toggle_button(m_btn_cam_light, _L("Cam Light"), false);
    if (m_btn_camera_capture) m_btn_camera_capture->SetLabel(_L("Camera: On"));
    set_toggle_button(m_btn_ace_refill, _L("Auto Refill"), false);
    set_toggle_button(m_btn_ace_dry, _L("Drying"), false);
    m_status = AcLan::Status();
    set_camera_placeholder(wxEmptyString);
    set_msg(wxEmptyString);
    set_controls_enabled(false);
}

// ------------------------------------------------ connect + polling ----

void AnycubicDevicePanel::connect_selected()
{
    if (m_selected < 0 || m_selected >= (int)m_printers.size()) return;
    const wxString DEG = wxString::FromUTF8(" \xC2\xB0""C");
    if (m_poll_timer.IsRunning()) m_poll_timer.Stop();
    m_connected = false;
    set_controls_enabled(false);
    const Printer p = m_printers[m_selected];
    m_lbl_name->SetLabel(from_u8(p.name));
    m_lbl_ip->SetLabel(from_u8("IP  " + p.ip));
    m_lbl_state->SetForegroundColour(WARN_GREY);
    m_lbl_state->SetLabel(wxString::FromUTF8("\xE2\x97\x8F ") + _L("Connecting..."));
    m_lbl_job->SetLabel(_L("No active job"));
    m_lbl_time->SetLabel(_L("Ready"));
    m_progress->SetProgress(0);
    m_lbl_nozzle->SetLabel(_L("Nozzle:  - / -") + DEG);
    m_lbl_bed->SetLabel(_L("Bed:  - / -") + DEG);
    m_lbl_fan->SetLabel(_L("Part fan:  0%"));
    m_lbl_aux->SetLabel(_L("Aux fan: 0%"));
    m_lbl_ace_env->SetLabel(_L("Temperature:  -    Humidity:  -"));
    m_lbl_ace_dry_info->SetLabel(_L("Drying temperature: 45C   Drying time: 4:00:00"));
    if (m_ace_body) {
        m_ace_body->Clear(true);
        auto* hint = new Label((wxWindow*)m_ace_section, Label::Body_11, _L("No materials reported yet."));
        hint->SetForegroundColour(INK_SOFT);
        hint->SetBackgroundColour(CARD_BG);
        m_ace_body->Add(hint, 0, wxBOTTOM, FromDIP(4));
    }
    m_ace_sig.clear();
    m_status = AcLan::Status();
    m_light_on = m_cam_light_on = m_ace_auto_feed = m_ace_dry = false;
    m_camera_enabled = true;
    set_toggle_button(m_btn_light, _L("Head Light"), false);
    set_toggle_button(m_btn_cam_light, _L("Cam Light"), false);
    if (m_btn_camera_capture) m_btn_camera_capture->SetLabel(_L("Camera: On"));
    set_toggle_button(m_btn_ace_refill, _L("Auto Refill"), false);
    set_toggle_button(m_btn_ace_dry, _L("Drying"), false);
    set_camera_placeholder(wxEmptyString);
    append_console_line(_L("Connecting to ") + from_u8(p.ip) + _L("..."), CONSOLE_TEXT);
    set_msg(_L("Connecting..."));
    const std::string ip = p.ip;

    auto creds = std::make_shared<AcLanCreds>();
    run_async(
        [ip, creds]() { try { *creds = AcLan::fetch_credentials(ip); } catch (...) { creds->ok = false; creds->error = "exception"; } },
        [this, creds, ip]() {
            if (m_selected < 0 || m_selected >= (int)m_printers.size() || m_printers[m_selected].ip != ip)
                return; // selection changed while connecting
            m_creds = *creds;
            if (!m_creds.ok) {
                m_connected = false;
                m_lbl_state->SetForegroundColour(wxColour(214, 69, 69));
                m_lbl_state->SetLabel(wxString::FromUTF8("\xE2\x97\x8F ") + _L("Offline"));
                append_console_line(_L("Connect failed: ") + from_u8(m_creds.error), CONSOLE_ERR);
                set_msg(_L("Could not connect: ") + from_u8(m_creds.error));
                return;
            }
            if (!m_creds.printer_id.empty()) { m_printers[m_selected].printer_id = m_creds.printer_id; save_printers(); }
            m_connected = true;
            set_controls_enabled(true);
            append_console_line(_L("Connected to ") + from_u8(ip), CONSOLE_TEXT);
            set_msg(_L("Connected."));
            poll_status();
            m_poll_timer.Start(3500);
        });
}

void AnycubicDevicePanel::on_poll(wxTimerEvent&) { poll_status(); }

void AnycubicDevicePanel::poll_status()
{
    if (!m_connected || m_polling) return;
    m_polling = true;
    AcLanCreds c = m_creds;
    auto st = std::make_shared<AcLan::Status>();
    run_async(
        [c, st]() { try { *st = AcLan::query_status(c, 2500); } catch (...) {} },
        [this, st]() { m_polling = false; if (m_connected) update_status_ui(*st); });
}

void AnycubicDevicePanel::update_status_ui(const AcLan::Status& s)
{
    static const wxString DOT = wxString::FromUTF8("\xE2\x97\x8F ");
    m_status = s;
    if (!s.ok && s.state.empty()) {
        m_lbl_state->SetForegroundColour(WARN_GREY);
        m_lbl_state->SetLabel(DOT + _L("Unknown"));
        set_msg(s.raw_error.empty() ? wxEmptyString : from_u8(s.raw_error));
        refresh_camera_preview();
        return;
    }
    set_msg(s.raw_error.empty() ? wxEmptyString : from_u8(s.raw_error));
    // colored status indicator: show the live state only while active; otherwise "Connected"
    {
        std::string sl = boost::to_lower_copy(s.state);
        const bool paused  = sl.find("paus") != std::string::npos;
        const bool active  = sl.find("print") != std::string::npos || sl.find("run") != std::string::npos ||
                             sl.find("busy") != std::string::npos  || sl.find("heat") != std::string::npos;
        const bool offline = sl.find("offline") != std::string::npos || sl.find("error") != std::string::npos;
        wxColour sc; wxString txt;
        if      (paused)  { sc = wxColour(225, 158, 40); txt = _L("Paused"); }
        else if (active)  { sc = PINK_DK;                txt = from_u8(s.state); }
        else if (offline) { sc = wxColour(214, 69, 69);  txt = from_u8(s.state); }
        else              { sc = OK_GREEN;               txt = _L("Connected"); }
        m_lbl_state->SetForegroundColour(sc);
        m_lbl_state->SetLabel(DOT + txt);
    }
    const wxString DEG = wxString::FromUTF8(" \xC2\xB0""C");
    m_lbl_nozzle->SetLabel(wxString::Format(_L("Nozzle:  %d / %d"), (int)s.nozzle_temp, (int)s.nozzle_target) + DEG);
    m_lbl_bed->SetLabel(wxString::Format(_L("Bed:  %d / %d"), (int)s.bed_temp, (int)s.bed_target) + DEG);
    m_lbl_fan->SetLabel(wxString::Format(_L("Part fan:  %d%%"), (int)s.fan_speed_pct));
    m_lbl_aux->SetLabel(wxString::Format(_L("Aux fan: %d%%"), (int)s.aux_fan_speed_pct));
    m_light_on = s.light_on; m_cam_light_on = s.camera_light_on;
    set_toggle_button(m_btn_light, _L("Head Light"), m_light_on);
    set_toggle_button(m_btn_cam_light, _L("Cam Light"), m_cam_light_on);
    if (m_btn_cam_light) m_btn_cam_light->Enable(m_connected && s.has_camera);
    if (m_btn_camera_capture) {
        m_btn_camera_capture->SetLabel(m_camera_enabled ? _L("Camera: On") : _L("Camera: Off"));
        m_btn_camera_capture->Enable(m_connected && s.has_camera);
    }
    // Job line: only show the file while actually printing (the printer keeps reporting the
    // last filename when idle). Format: "<file.gcode>  -  <total time>  -  <percent>".
    {
        std::string sl = boost::to_lower_copy(s.state);
        const bool printing = sl.find("paus") != std::string::npos || sl.find("print") != std::string::npos ||
                              sl.find("run") != std::string::npos  || sl.find("busy") != std::string::npos ||
                              sl.find("heat") != std::string::npos;
        if (printing && !s.filename.empty()) {
            m_lbl_job->SetLabel(from_u8(s.filename));
            const wxString SEP = wxString::FromUTF8("   \xE2\x80\xA2   ");
            const int total_min = s.print_min + s.remain_min;   // total = elapsed + remaining
            wxString meta = format_minutes_short(total_min);
            meta += SEP + wxString::Format(_L("%d%%"), std::max(0, std::min(100, s.progress)));
            m_lbl_time->SetLabel(meta);
            m_progress->SetProgress(std::max(0, std::min(100, s.progress)));
        } else {
            m_lbl_job->SetLabel(_L("No active job"));
            m_lbl_time->SetLabel(wxEmptyString);
            m_progress->SetProgress(0);
        }
    }
    refresh_camera_preview();

    std::vector<AcLan::AceBox> ace_boxes = s.boxes;
    if (ace_boxes.empty() && !s.ace.empty()) {
        AcLan::AceBox fallback;
        fallback.slots = s.ace;
        ace_boxes.push_back(std::move(fallback));
    }
    const bool ace_ready = s.has_ace || !ace_boxes.empty();
    if (m_btn_ace_refill) m_btn_ace_refill->Enable(m_connected && ace_ready);
    if (m_btn_ace_dry)    m_btn_ace_dry->Enable(m_connected && ace_ready);
    if (m_lbl_ace_env) {
        if (!ace_boxes.empty())
            m_lbl_ace_env->SetLabel(wxString::Format(_L("Temperature:  %dC    Humidity:  -"), ace_boxes.front().temp));
        else
            m_lbl_ace_env->SetLabel(_L("Temperature:  -    Humidity:  -"));
    }
    if (m_lbl_ace_dry_info) {
        if (!ace_boxes.empty() && ace_boxes.front().dry_target > 0) {
            const auto& box = ace_boxes.front();
            const int duration = box.dry_duration > 0 ? box.dry_duration : 4 * 3600;
            m_lbl_ace_dry_info->SetLabel(
                wxString::Format(_L("Drying temperature: %dC   Drying time: "), box.dry_target) + format_duration_hms(duration));
        } else {
            m_lbl_ace_dry_info->SetLabel(_L("Drying temperature: 45C   Drying time: 4:00:00"));
        }
    }
    {
        bool auto_feed = false, drying = false;
        for (const auto& box : ace_boxes) {
            auto_feed = auto_feed || box.auto_feed != 0;
            drying    = drying    || box.dry_status != 0;
        }
        m_ace_auto_feed = auto_feed; m_ace_dry = drying;
        set_toggle_button(m_btn_ace_refill, _L("Auto Refill"), auto_feed);
        set_toggle_button(m_btn_ace_dry, _L("Drying"), drying);
    }

    // Pause/Resume swap with state; Stop only while a job is active.
    {
        std::string sl = boost::to_lower_copy(s.state);
        const bool paused = sl.find("paus") != std::string::npos;
        const bool active = paused || sl.find("print") != std::string::npos ||
                            sl.find("run") != std::string::npos || sl.find("busy") != std::string::npos ||
                            sl.find("heat") != std::string::npos;
        if (m_btn_pause)  { m_btn_pause->Show(!paused);  m_btn_pause->Enable(m_connected && active && !paused); }
        if (m_btn_resume) { m_btn_resume->Show(paused);  m_btn_resume->Enable(m_connected && paused); }
        if (m_btn_stop)     m_btn_stop->Enable(m_connected && active);
        if (m_btn_pause && m_btn_pause->GetParent()) m_btn_pause->GetParent()->Layout();
    }

    // Telemetry stream into the console (compact, deduped so it doesn't flood).
    {
        wxString tel = wxString::Format("%s  N%d/%d  B%d/%d  fan%d%%",
            from_u8(s.state.empty() ? "-" : s.state), (int)s.nozzle_temp, (int)s.nozzle_target,
            (int)s.bed_temp, (int)s.bed_target, (int)s.fan_speed_pct);
        if (s.total_layers > 0 || s.progress > 0)
            tel += wxString::Format("  L%d/%d  %d%%", s.cur_layer, s.total_layers, s.progress);
        if (tel != m_last_telemetry) {
            m_last_telemetry = tel;
            append_console_line(wxString::FromUTF8("\xE2\x80\xA2 ") + tel, INK_SOFT);
        }
    }

    // ACE material chips — only rebuild when the slot data actually changes (stops the
    // per-poll flashing). Signature folds in box id, loaded slot, and each slot's
    // index/colour/material/status.
    {
        std::string sig;
        for (const auto& box : ace_boxes) {
            sig += "B" + std::to_string(box.id) + "L" + std::to_string(box.loaded_slot) + ":";
            for (const auto& sl : box.slots)
                sig += std::to_string(sl.index) + "," + std::to_string(sl.r) + "," + std::to_string(sl.g)
                     + "," + std::to_string(sl.b) + "," + sl.material + "," + std::to_string(sl.status) + ";";
        }
        if (sig != m_ace_sig) {
            m_ace_sig = sig;
            m_ace_body->Clear(true);
            if (ace_boxes.empty()) {
                auto* hint = new Label((wxWindow*)m_ace_section, Label::Body_11, _L("No materials reported yet."));
                hint->SetForegroundColour(INK_SOFT);
                hint->SetBackgroundColour(CARD_BG);
                m_ace_body->Add(hint, 0, wxBOTTOM, FromDIP(4));
            }
            for (const auto& box : ace_boxes) {
                auto* row = new wxBoxSizer(wxHORIZONTAL);
                for (const auto& sl : box.slots) {
                    auto* slot_col = new wxBoxSizer(wxVERTICAL);
                    const bool has_color = sl.status != 0 || sl.r || sl.g || sl.b;
                    wxColour spool_color = has_color ? wxColour(sl.r, sl.g, sl.b) : wxColour(236, 236, 240);

                    auto* spool = new StaticBox((wxWindow*)m_ace_section, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(74), FromDIP(74)));
                    spool->SetCornerRadius(FromDIP(37));
                    spool->SetBorderWidth(box.loaded_slot == sl.index ? 3 : 2);
                    spool->SetBorderColor(StateColor(box.loaded_slot == sl.index ? PINK : CARD_BORDER));
                    spool->SetBackgroundColor(StateColor(spool_color));
                    spool->SetBackgroundColour(CARD_BG);   // corners blend with the card -> colour stays in the circle

                    auto* spool_sizer = new wxBoxSizer(wxVERTICAL);
                    auto* hole = new StaticBox(spool, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
                    hole->SetCornerRadius(FromDIP(12));
                    hole->SetBorderWidth(2);
                    hole->SetBorderColor(StateColor(wxColour(230, 230, 234)));
                    hole->SetBackgroundColor(StateColor(*wxWHITE));
                    hole->SetBackgroundColour(spool_color);
                    spool_sizer->AddStretchSpacer();
                    spool_sizer->Add(hole, 0, wxALIGN_CENTER);
                    spool_sizer->AddStretchSpacer();
                    spool->SetSizer(spool_sizer);
                    spool->SetToolTip(from_u8(sl.material.empty() ? "empty" : sl.material));
                    slot_col->Add(spool, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(8));

                    // material name + edit button
                    auto* mat_row = new wxBoxSizer(wxHORIZONTAL);
                    wxString material = sl.material.empty() ? _L("Empty") : from_u8(sl.material);
                    auto* mat_lbl = new Label((wxWindow*)m_ace_section, Label::Body_12, material);
                    mat_lbl->SetForegroundColour(INK_SOFT);
                    mat_lbl->SetBackgroundColour(CARD_BG);
                    mat_row->Add(mat_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
                    auto* edit_btn = new Button((wxWindow*)m_ace_section, wxString::FromUTF8("\xE2\x9C\x8E"));  // pencil
                    edit_btn->SetCornerRadius(FromDIP(6));
                    edit_btn->SetPaddingSize(wxSize(FromDIP(4), FromDIP(2)));
                    edit_btn->SetBorderWidth(1);
                    edit_btn->SetBorderColor(StateColor(CARD_BORDER));
                    edit_btn->SetBackgroundColor(StateColor(std::make_pair(PINK_SOFT, (int)StateColor::Hovered),
                                                            std::make_pair(*wxWHITE, (int)StateColor::Normal)));
                    edit_btn->SetTextColor(StateColor(PINK_DK));
                    edit_btn->SetToolTip(_L("Edit material / color"));
                    { const int bid = box.id, sidx = sl.index, rr = sl.r, gg = sl.g, bb = sl.b; const std::string mt = sl.material;
                      edit_btn->Bind(wxEVT_BUTTON, [this, bid, sidx, mt, rr, gg, bb](wxCommandEvent&){ on_edit_slot(bid, sidx, mt, rr, gg, bb); }); }
                    mat_row->Add(edit_btn, 0, wxALIGN_CENTER_VERTICAL);
                    slot_col->Add(mat_row, 0, wxALIGN_CENTER);

                    if (box.loaded_slot == sl.index) {
                        auto* active_lbl = new Label((wxWindow*)m_ace_section, Label::Body_11, _L("Loaded"));
                        active_lbl->SetForegroundColour(PINK_DK);
                        active_lbl->SetBackgroundColour(CARD_BG);
                        slot_col->Add(active_lbl, 0, wxALIGN_CENTER | wxTOP, FromDIP(4));
                    }
                    row->Add(slot_col, 0, wxRIGHT, FromDIP(16));
                }
                m_ace_body->Add(row, 0, wxBOTTOM, FromDIP(10));
            }
            m_content->FitInside();
            m_content->Layout();
        }
    }
}

void AnycubicDevicePanel::set_controls_enabled(bool on)
{
    for (Button* b : { m_btn_pause, m_btn_resume, m_btn_stop, m_btn_motors_off, m_btn_console_send,
                       m_btn_jog_1mm, m_btn_jog_15mm, m_btn_jog_50mm })
        if (b) b->Enable(on);
    if (m_spin_nozzle) m_spin_nozzle->Enable(on);
    if (m_spin_bed)    m_spin_bed->Enable(on);
    if (m_spin_fan)    m_spin_fan->Enable(on);
    if (m_btn_light) m_btn_light->Enable(on);
    if (m_btn_cam_light) m_btn_cam_light->Enable(on && m_status.has_camera);
    if (m_btn_camera_capture) m_btn_camera_capture->Enable(on && m_status.has_camera);
    const bool ace_ready = m_status.has_ace || !m_status.boxes.empty() || !m_status.ace.empty();
    if (m_btn_ace_refill) m_btn_ace_refill->Enable(on && ace_ready);
    if (m_btn_ace_dry) m_btn_ace_dry->Enable(on && ace_ready);
    if (m_console_input) m_console_input->Enable(on);
}

void AnycubicDevicePanel::set_msg(const wxString& m) { if (m_lbl_msg) { m_lbl_msg->SetLabel(m); m_content->Layout(); } }

// ----------------------------------------------------- control cmds ----

void AnycubicDevicePanel::run_cmd(const wxString& ok_msg, const wxString& console_line, std::function<bool(const AcLanCreds&, std::string&)> fn)
{
    if (!m_connected || !m_creds.ok) {
        if (!console_line.empty())
            append_console_line(_L("> ") + console_line, CONSOLE_ACC);
        append_console_line(_L("Not connected."), CONSOLE_ERR);
        set_msg(_L("Not connected."));
        return;
    }
    AcLanCreds c = m_creds;
    auto err = std::make_shared<std::string>();
    auto ok  = std::make_shared<bool>(false);
    if (!console_line.empty())
        append_console_line(_L("> ") + console_line, CONSOLE_ACC);
    run_async(
        [c, fn, err, ok]() { try { *ok = fn(c, *err); } catch (...) { *err = "exception"; } },
        [this, ok_msg, console_line, err, ok]() {
            if (*ok) {
                append_console_line(ok_msg, CONSOLE_TEXT);
                set_msg(ok_msg);
                poll_status();
            } else {
                const wxString msg = _L("Failed: ") + from_u8(*err);
                append_console_line(msg + (console_line.empty() ? wxString() : (_L(" [") + console_line + "]")), CONSOLE_ERR);
                set_msg(msg);
            }
        });
}

// pause/resume/stop carry the live task id (firmware drops the command otherwise).
void AnycubicDevicePanel::on_pause(wxCommandEvent&)  { std::string tid=m_status.task_id; run_cmd(_L("Paused"),  _L("pause print"),  [tid](const AcLanCreds& c, std::string& e){ return AcLan::print_action(c, "pause",  e, tid); }); }
void AnycubicDevicePanel::on_resume(wxCommandEvent&) { std::string tid=m_status.task_id; run_cmd(_L("Resumed"), _L("resume print"), [tid](const AcLanCreds& c, std::string& e){ return AcLan::print_action(c, "resume", e, tid); }); }
void AnycubicDevicePanel::on_stop(wxCommandEvent&)   { std::string tid=m_status.task_id; run_cmd(_L("Stopped"), _L("stop print"),   [tid](const AcLanCreds& c, std::string& e){ return AcLan::print_action(c, "stop",   e, tid); }); }

// temp/fan: the firmware ignores the update unless taskid is the live job id ("-1" idle).
void AnycubicDevicePanel::on_set_nozzle(wxCommandEvent&) { int t = m_spin_nozzle->GetValue(); std::string tid=m_status.task_id; run_cmd(_L("Nozzle target set"), wxString::Format(_L("M104 S%d"), t), [t,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_nozzle_temp(c, t, e, tid); }); }
void AnycubicDevicePanel::on_set_bed(wxCommandEvent&)    { int t = m_spin_bed->GetValue();    std::string tid=m_status.task_id; run_cmd(_L("Bed target set"),    wxString::Format(_L("M140 S%d"), t), [t,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_bed_temp(c, t, e, tid); }); }
void AnycubicDevicePanel::on_set_fan(wxCommandEvent&)    { int t = m_spin_fan->GetValue();    std::string tid=m_status.task_id; run_cmd(_L("Fan set"),           wxString::Format(_L("M106 S%d"), gcode_speed_from_pct(t)), [t,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_fan(c, t, e, tid); }); }

void AnycubicDevicePanel::on_light_on(wxCommandEvent&)  { run_cmd(_L("Light on"),  _L("M355 S1"), [](const AcLanCreds& c, std::string& e){ return AcLan::set_light(c, true,  100, e); }); }
void AnycubicDevicePanel::on_light_off(wxCommandEvent&) { run_cmd(_L("Light off"), _L("M355 S0"), [](const AcLanCreds& c, std::string& e){ return AcLan::set_light(c, false, 0,   e); }); }
void AnycubicDevicePanel::on_toggle_light(wxCommandEvent&)
{
    const bool on = !m_light_on;                 // toggle from last known state
    m_light_on = on;
    set_toggle_button(m_btn_light, _L("Head Light"), on);
    run_cmd(on ? _L("Light on") : _L("Light off"), on ? _L("M355 S1") : _L("M355 S0"),
            [on](const AcLanCreds& c, std::string& e){ return AcLan::set_light(c, on, on ? 100 : 0, e); });
}
void AnycubicDevicePanel::on_toggle_cam_light(wxCommandEvent&)
{
    const bool on = !m_cam_light_on;
    m_cam_light_on = on;
    set_toggle_button(m_btn_cam_light, _L("Cam Light"), on);
    run_cmd(on ? _L("Cam light on") : _L("Cam light off"),
            on ? _L("camera light on") : _L("camera light off"),
            [on](const AcLanCreds& c, std::string& e){ return AcLan::set_light(c, on, on ? 100 : 0, e, 2); });
}

void AnycubicDevicePanel::on_toggle_camera(wxCommandEvent&)
{
    if (!m_connected || !m_creds.ok) {
        set_msg(_L("Not connected."));
        return;
    }
    if (!m_status.has_camera) {
        set_msg(_L("Camera not supported."));
        return;
    }

    const bool on = !m_camera_enabled;
    m_camera_enabled = on;
    if (m_btn_camera_capture)
        m_btn_camera_capture->SetLabel(on ? _L("Camera: On") : _L("Camera: Off"));
    if (!on)
        set_camera_placeholder(_L("Camera off"));
    else
        refresh_camera_preview();

    run_cmd(on ? _L("Camera on") : _L("Camera off"),
            on ? _L("camera on") : _L("camera off"),
            [on](const AcLanCreds& c, std::string& e){ return AcLan::video_capture(c, on, e); });
}

// Toggle: disable steppers, or note that they re-engage on the next motion (the
// network API has no explicit "enable" command).
void AnycubicDevicePanel::on_motors_off(wxCommandEvent&)
{
    if (!m_motors_off) {
        m_motors_off = true;
        if (m_btn_motors_off) m_btn_motors_off->SetLabel(_L("Motors On"));
        run_cmd(_L("Motors disabled"), _L("M84"), [](const AcLanCreds& c, std::string& e){ return AcLan::motors_off(c, e); });
    } else {
        m_motors_off = false;
        if (m_btn_motors_off) m_btn_motors_off->SetLabel(_L("Motors Off"));
        append_console_line(_L("Motors re-engage on the next move, home, or print."), CONSOLE_TEXT);
        set_msg(_L("Motors re-engage on the next move, home, or print."));
    }
}
void AnycubicDevicePanel::on_home_all(wxCommandEvent&)  { clear_motors_off_state(); run_cmd(_L("Homing"), _L("G28"), [](const AcLanCreds& c, std::string& e){ return AcLan::home_axis(c, AcLan::AxisAll, e); }); }

// Any motion re-energizes the steppers, so the "disabled" state no longer holds.
void AnycubicDevicePanel::clear_motors_off_state()
{
    if (m_motors_off) {
        m_motors_off = false;
        if (m_btn_motors_off) m_btn_motors_off->SetLabel(_L("Motors Off"));
    }
}

void AnycubicDevicePanel::on_jog(int axis, int move_type)
{
    clear_motors_off_state();
    if (move_type == AcLan::MoveHome) {
        const wxString axis_cmd = axis == AcLan::AxisZ ? _L("G28 Z") : _L("G28 X Y");
        run_cmd(_L("Homing"), axis_cmd,
            [axis](const AcLanCreds& c, std::string& e) {
                const int home_axis = axis == AcLan::AxisZ ? AcLan::AxisZ : AcLan::AxisXY;
                return AcLan::home_axis(c, home_axis, e);
            });
        return;
    }

    wxString axis_name = axis == AcLan::AxisX ? _L("X") : axis == AcLan::AxisY ? _L("Y") : _L("Z");
    wxString dir = move_type == AcLan::MovePlus ? _L("+") : _L("-");
    run_cmd(_L("Move sent"), _L("Jog ") + axis_name + dir + wxString::Format(_L(" %.0fmm"), m_jog_distance_mm),
        [axis, move_type, distance = m_jog_distance_mm](const AcLanCreds& c, std::string& e) {
            return AcLan::move_axis(c, axis, move_type, distance, e);
        });
}

void AnycubicDevicePanel::on_ace_dry(wxCommandEvent& evt)    { on_toggle_ace_dry(evt); }
void AnycubicDevicePanel::on_ace_refill(wxCommandEvent& evt) { on_toggle_ace_auto_feed(evt); }

void AnycubicDevicePanel::on_toggle_ace_auto_feed(wxCommandEvent&)
{
    const bool on = !m_ace_auto_feed;            // toggle from last known state
    m_ace_auto_feed = on;
    set_toggle_button(m_btn_ace_refill, _L("Auto Refill"), on);
    const std::vector<AcLan::AceBox> boxes = m_status.boxes;
    run_cmd(on ? _L("Auto refill enabled") : _L("Auto refill disabled"),
            on ? _L("ACE auto refill on") : _L("ACE auto refill off"),
            [boxes, on](const AcLanCreds& c, std::string& e) {
                if (boxes.empty())
                    return AcLan::ace_set_auto_feed(c, 0, on, e);
                for (const auto& box : boxes) {
                    if (!AcLan::ace_set_auto_feed(c, box.id, on, e))
                        return false;
                }
                return true;
            });
}

void AnycubicDevicePanel::on_toggle_ace_dry(wxCommandEvent&)
{
    const bool on = !m_ace_dry;                   // toggle from last known state
    m_ace_dry = on;
    set_toggle_button(m_btn_ace_dry, _L("Drying"), on);
    const std::vector<AcLan::AceBox> boxes = m_status.boxes;
    run_cmd(on ? _L("Drying enabled") : _L("Drying disabled"),
            on ? _L("ACE drying on") : _L("ACE drying off"),
            [boxes, on](const AcLanCreds& c, std::string& e) {
                const int target_temp = 45;
                const int duration_min = 240;
                if (boxes.empty())
                    return AcLan::ace_set_dry(c, 0, on, target_temp, duration_min, e);
                for (const auto& box : boxes) {
                    if (!AcLan::ace_set_dry(c, box.id, on, box.dry_target > 0 ? box.dry_target : target_temp,
                                            box.dry_duration > 0 ? box.dry_duration / 60 : duration_min, e))
                        return false;
                }
                return true;
            });
}

void AnycubicDevicePanel::on_console_send(wxCommandEvent&)
{
    if (!m_console_input)
        return;

    wxString raw = m_console_input->GetValue();
    raw.Trim(true);
    raw.Trim(false);
    if (raw.empty())
        return;

    m_console_input->Clear();

    const int comment_pos = raw.Find(';');
    if (comment_pos != wxNOT_FOUND)
        raw = raw.Left(comment_pos);
    raw.Trim(true);
    raw.Trim(false);
    if (raw.empty())
        return;

    const std::string raw_utf8 = into_u8(raw);
    std::vector<std::string> tokens;
    boost::split(tokens, raw_utf8, boost::is_any_of(" \t"), boost::token_compress_on);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const std::string& token) { return token.empty(); }), tokens.end());
    if (tokens.empty())
        return;

    const std::string cmd = boost::to_upper_copy(tokens.front());
    if (cmd == "M104") {
        int temp = 0;
        if (!try_parse_int_param(tokens, 'S', temp)) {
            append_console_line(_L("> ") + raw, CONSOLE_ACC);
            append_console_line(_L("M104 requires S<temp>."), CONSOLE_ERR);
            set_msg(_L("M104 requires S<temp>."));
            return;
        }
        std::string tid=m_status.task_id;
        run_cmd(_L("Nozzle target set"), raw, [temp,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_nozzle_temp(c, temp, e, tid); });
        return;
    }
    if (cmd == "M140") {
        int temp = 0;
        if (!try_parse_int_param(tokens, 'S', temp)) {
            append_console_line(_L("> ") + raw, CONSOLE_ACC);
            append_console_line(_L("M140 requires S<temp>."), CONSOLE_ERR);
            set_msg(_L("M140 requires S<temp>."));
            return;
        }
        std::string tid=m_status.task_id;
        run_cmd(_L("Bed target set"), raw, [temp,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_bed_temp(c, temp, e, tid); });
        return;
    }
    if (cmd == "M106") {
        int raw_speed = 255;
        int fan_index = 0;
        (void) try_parse_int_param(tokens, 'S', raw_speed);
        (void) try_parse_int_param(tokens, 'P', fan_index);
        const int pct = pct_from_gcode_speed(raw_speed);
        std::string tid=m_status.task_id;
        if (fan_index == 2) {
            run_cmd(_L("Aux fan set"), raw, [pct,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_aux_fan(c, pct, e, tid); });
        } else {
            run_cmd(_L("Fan set"), raw, [pct,tid](const AcLanCreds& c, std::string& e){ return AcLan::set_fan(c, pct, e, tid); });
        }
        return;
    }
    if (cmd == "M107") {
        std::string tid=m_status.task_id;
        run_cmd(_L("Fan set"), raw, [tid](const AcLanCreds& c, std::string& e){ return AcLan::set_fan(c, 0, e, tid); });
        return;
    }
    if (cmd == "M18" || cmd == "M84") {
        run_cmd(_L("Motors disabled"), raw, [](const AcLanCreds& c, std::string& e){ return AcLan::motors_off(c, e); });
        return;
    }
    if (cmd == "M355") {
        int light_state = 1;
        (void) try_parse_int_param(tokens, 'S', light_state);
        const bool on = light_state != 0;
        run_cmd(on ? _L("Light on") : _L("Light off"), raw,
                [on](const AcLanCreds& c, std::string& e){ return AcLan::set_light(c, on, on ? 100 : 0, e); });
        return;
    }
    if (cmd == "G28") {
        bool has_x = false, has_y = false, has_z = false;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i].empty())
                continue;
            const char axis = (char)std::toupper((unsigned char)tokens[i][0]);
            has_x = has_x || axis == 'X';
            has_y = has_y || axis == 'Y';
            has_z = has_z || axis == 'Z';
        }
        int axis = AcLan::AxisAll;
        if (has_z && !has_x && !has_y) axis = AcLan::AxisZ;
        else if (!has_z && (has_x || has_y)) axis = AcLan::AxisXY;
        run_cmd(_L("Homing"), raw, [axis](const AcLanCreds& c, std::string& e){ return AcLan::home_axis(c, axis, e); });
        return;
    }

    append_console_line(_L("> ") + raw, CONSOLE_ACC);
    append_console_line(_L("Unsupported command. Supported: M104, M140, M106, M107, G28, M18, M84, M355."), CONSOLE_ERR);
    set_msg(_L("Unsupported console command."));
}

// Edit a slot's material type (dropdown) + color (picker) -> multiColorBox setInfo.
void AnycubicDevicePanel::on_edit_slot(int box_id, int slot_index, const std::string& cur_type, int r, int g, int b)
{
    if (!m_connected || !m_creds.ok) { set_msg(_L("Not connected.")); return; }

    wxArrayString mats;
    for (const char* m : { "PLA", "PLA+", "PLA-CF", "PETG", "PETG-CF", "ABS", "ASA", "TPU",
                           "PA (Nylon)", "PA-CF", "PC", "HIPS", "PVA" })
        mats.Add(m);
    int sel = mats.Index(from_u8(cur_type), false);
    if (sel == wxNOT_FOUND) { if (!cur_type.empty()) { mats.Insert(from_u8(cur_type), 0); sel = 0; } else sel = 0; }
    wxSingleChoiceDialog mat_dlg(this, _L("Material type:"), _L("Edit material"), mats);
    mat_dlg.SetSelection(sel);
    if (mat_dlg.ShowModal() != wxID_OK) return;
    std::string type = into_u8(mat_dlg.GetStringSelection());
    if (type.empty()) return;

    wxColourData cd; cd.SetColour(wxColour(r, g, b));
    wxColourDialog col_dlg(this, &cd);
    col_dlg.SetTitle(_L("Pick filament color"));
    if (col_dlg.ShowModal() != wxID_OK) return;
    wxColour chosen = col_dlg.GetColourData().GetColour();
    const int nr = chosen.Red(), ng = chosen.Green(), nb = chosen.Blue();

    // Optimistically update the spool now so the change is visible immediately.
    std::vector<AcLan::AceSlot> updated_slots;
    for (auto& box : m_status.boxes)
        if (box.id == box_id) {
            for (auto& sl : box.slots)
                if (sl.index == slot_index) {
                    sl.material = type;
                    sl.r = nr;
                    sl.g = ng;
                    sl.b = nb;
                    sl.color_group.clear();
                    sl.color_group.push_back({ nr, ng, nb, 255 });
                }
            updated_slots = box.slots;
        }
    m_ace_sig.clear();
    auto alive = m_alive;
    wxGetApp().CallAfter([this, alive]() {
        if (!alive || !alive->load())
            return;
        AcLan::Status snap = m_status;
        update_status_ui(snap);
    });

    run_cmd(_L("Material updated"), wxString::Format(_L("set slot %d -> %s"), slot_index + 1, from_u8(type)),
            [box_id, slot_index, type, nr, ng, nb, updated_slots](const AcLanCreds& c, std::string& e){
                if (!updated_slots.empty())
                    return AcLan::ace_set_slots(c, box_id, updated_slots, e);
                return AcLan::ace_set_slot(c, box_id, slot_index, type, nr, ng, nb, e);
            });
}

void AnycubicDevicePanel::on_send_file(wxCommandEvent&)
{
    if (m_selected < 0 || m_selected >= (int)m_printers.size()) { set_msg(_L("Select a printer first.")); return; }
    wxFileDialog dlg(this, _L("Choose a G-code file"), "", "", "G-code (*.gcode;*.gcode.3mf)|*.gcode;*.gcode.3mf",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    const std::string path = into_u8(dlg.GetPath());
    const std::string ip   = m_printers[m_selected].ip;
    const std::string filename = boost::filesystem::path(path).filename().string();
    append_console_line(_L("Uploading ") + from_u8(filename) + _L("..."), CONSOLE_TEXT);
    set_msg(_L("Uploading..."));
    auto res = std::make_shared<std::string>();
    run_async(
        [ip, path, res]() {
            try {
                AcLanCreds c = AcLan::fetch_credentials(ip);
                if (!c.ok) { *res = "connect failed: " + c.error; return; }
                boost::filesystem::path fp(path);
                std::string name = fp.filename().string();
                std::string err;
                if (!AcLan::upload_file(c.file_upload_url, name, fp, [](int){}, err)) { *res = "upload failed: " + err; return; }
                std::string md5 = AcLan::file_md5_hex(fp);
                if (!AcLan::send_print(c, name, md5, err)) { *res = "start failed: " + err; return; }
                *res = "ok";
            } catch (...) { *res = "exception"; }
        },
        [this, res, filename]() {
            if (*res == "ok") {
                append_console_line(_L("Print started: ") + from_u8(filename), CONSOLE_TEXT);
                set_msg(_L("Print started."));
            } else {
                append_console_line(_L("Upload failed: ") + from_u8(*res), CONSOLE_ERR);
                set_msg(_L("Failed: ") + from_u8(*res));
            }
        });
}

// --------------------------------------------------------- async ----

void AnycubicDevicePanel::run_async(std::function<void()> work, std::function<void()> done)
{
    auto alive = m_alive;
    std::thread([work, done, alive]() {
        try { work(); } catch (...) {}
        wxGetApp().CallAfter([done, alive]() { if (alive && alive->load()) done(); });
    }).detach();
}

}} // namespace Slic3r::GUI
