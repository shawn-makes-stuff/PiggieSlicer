#ifndef slic3r_AnycubicDevicePanel_hpp_
#define slic3r_AnycubicDevicePanel_hpp_

// PiggieSlicer: native LAN control panel for Anycubic printers.
// Fresh implementation (not the old flat panel): a printer sidebar + a sectioned
// dashboard (status / temperatures+fans / ACE materials / motion+light / camera),
// driven by the fully-local AcLan protocol. All device I/O runs on a guarded
// worker thread; the UI is only touched back on the main thread via CallAfter.

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <wx/panel.h>
#include <wx/timer.h>

#include "slic3r/Utils/AcLan.hpp"

class wxListBox;
class wxButton;
class wxStaticText;
class wxScrolledWindow;
class wxGauge;
class wxSpinCtrl;
class wxSizer;
class wxBoxSizer;
class wxTextCtrl;
class wxChoice;
class wxWindow;
class wxWebView;

// PiggieSlicer themed widgets (global namespace)
class Button;
class ProgressBar;
class SwitchButton;
class StaticBox;

namespace Slic3r { namespace GUI {

class PinkPrinterList;   // owner-drawn list with a pink selection highlight (defined in .cpp)

class AnycubicDevicePanel : public wxPanel
{
public:
    explicit AnycubicDevicePanel(wxWindow* parent);
    ~AnycubicDevicePanel() override;

private:
    struct Printer {
        std::string name;       // user nickname (defaults to model name)
        std::string ip;
        std::string printer_id; // from creds/discovery (for dedup, optional)
        std::string usn;        // SSDP USN (uuid:fdm:<mac>) for dedup
        std::string model;      // model name reported by the printer
    };

    // --- construction ---
    void       build_ui();
    wxWindow*  make_section(wxWindow* parent, const wxString& title, wxSizer*& body_out);
    Button*    make_button(wxWindow* parent, const wxString& label, bool primary);
    void       set_jog_distance(double mm);
    void       refresh_jog_distance_buttons();
    void       append_console_line(const wxString& line, const wxColour& colour);
    void       refresh_camera_preview();
    void       set_camera_placeholder(const wxString& text);
    void       on_console_send(wxCommandEvent&);

    // --- printer store (our own list; nicknames live here) ---
    void load_printers();
    void save_printers();
    void refresh_list();
    int  find_printer(const std::string& usn, const std::string& ip) const;
    void maybe_auto_connect_on_launch();

    // --- sidebar actions ---
    void on_select (wxCommandEvent&);
    void on_discover(wxCommandEvent&);
    void on_add_ip (wxCommandEvent&);
    void on_rename (wxCommandEvent&);
    void on_remove (wxCommandEvent&);

    // --- connection + polling ---
    void connect_selected();
    void on_poll(wxTimerEvent&);
    void poll_status();
    void update_status_ui(const AcLan::Status& s);
    void set_controls_enabled(bool on);
    void set_msg(const wxString& m);

    // --- control handlers ---
    void on_pause (wxCommandEvent&);
    void on_resume(wxCommandEvent&);
    void on_stop  (wxCommandEvent&);
    void on_set_nozzle(wxCommandEvent&);
    void on_set_bed   (wxCommandEvent&);
    void on_set_fan   (wxCommandEvent&);
    void on_light_on  (wxCommandEvent&);
    void on_light_off (wxCommandEvent&);
    void on_toggle_light(wxCommandEvent&);
    void on_toggle_cam_light(wxCommandEvent&);
    void on_toggle_camera(wxCommandEvent&);
    void on_motors_off(wxCommandEvent&);
    void clear_motors_off_state();
    void on_jog       (int axis, int move_type);
    void on_home_all  (wxCommandEvent&);
    void on_ace_dry   (wxCommandEvent&);
    void on_ace_refill(wxCommandEvent&);
    void on_toggle_ace_auto_feed(wxCommandEvent&);
    void on_toggle_ace_dry(wxCommandEvent&);
    void on_edit_slot (int box_id, int slot_index, const std::string& cur_type, int r, int g, int b);
    void on_send_file (wxCommandEvent&);

    // --- async plumbing (guarded against panel destruction) ---
    void run_async(std::function<void()> work, std::function<void()> done);
    // run a one-shot AcLan command on the worker, report ok/err on the main thread.
    void run_cmd(const wxString& ok_msg, const wxString& console_line, std::function<bool(const AcLanCreds&, std::string&)> fn);

    // --- state ---
    std::vector<Printer>               m_printers;
    int                                m_selected = -1;
    AcLanCreds                         m_creds;     // valid creds for the selected printer (main thread)
    AcLan::Status                      m_status;
    bool                               m_connected = false;
    std::shared_ptr<std::atomic<bool>> m_alive;     // false once the panel is destroyed
    wxTimer                            m_poll_timer;
    bool                               m_polling = false;
    std::shared_ptr<std::vector<AcLanDevice>> m_scan_result; // discovery results handed worker->main
    double                             m_jog_distance_mm = 1.0;
    bool                               m_motors_off = false;     // we last disabled the steppers
    wxString                           m_last_telemetry;         // dedup console telemetry stream

    // --- widgets ---
    PinkPrinterList* m_list        = nullptr;
    wxScrolledWindow* m_content    = nullptr;
    wxStaticText*    m_lbl_name    = nullptr;
    wxStaticText*    m_lbl_state   = nullptr;
    wxStaticText*    m_lbl_ip      = nullptr;
    wxStaticText*    m_lbl_msg     = nullptr;
    // temps
    wxStaticText*    m_lbl_nozzle  = nullptr;
    wxStaticText*    m_lbl_bed     = nullptr;
    wxStaticText*    m_lbl_fan     = nullptr;
    wxStaticText*    m_lbl_aux     = nullptr;
    wxSpinCtrl*      m_spin_nozzle = nullptr;
    wxSpinCtrl*      m_spin_bed    = nullptr;
    wxSpinCtrl*      m_spin_fan    = nullptr;
    wxStaticText*    m_lbl_ace_env = nullptr;
    wxStaticText*    m_lbl_ace_dry_info = nullptr;
    // job
    wxStaticText*    m_lbl_job     = nullptr;
    ProgressBar*     m_progress    = nullptr;
    wxStaticText*    m_lbl_time    = nullptr;
    Button*          m_btn_pause   = nullptr;
    Button*          m_btn_resume  = nullptr;
    Button*          m_btn_stop    = nullptr;
    Button*          m_btn_motors_off = nullptr;
    Button*          m_btn_console_send = nullptr;
    Button*          m_btn_jog_1mm = nullptr;
    Button*          m_btn_jog_15mm = nullptr;
    Button*          m_btn_jog_50mm = nullptr;
    // ACE
    wxSizer*         m_ace_body    = nullptr;
    wxWindow*        m_ace_section = nullptr;
    wxWindow*        m_camera_placeholder = nullptr;
    wxStaticText*    m_camera_placeholder_label = nullptr;
    wxWebView*       m_camera_view = nullptr;
    std::string      m_camera_stream_ip;
    wxChoice*        m_cam_quality = nullptr;       // stream latency profile selector
    int              m_cam_profile = 0;             // 0 low-latency, 1 balanced, 2 smooth
    // controls
    Button*          m_btn_light    = nullptr;       // toggle buttons (state in label)
    Button*          m_btn_cam_light = nullptr;
    Button*          m_btn_camera_capture = nullptr;
    Button*          m_btn_ace_refill = nullptr;
    Button*          m_btn_ace_dry = nullptr;
    wxTextCtrl*      m_console_input = nullptr;
    wxTextCtrl*      m_console_log = nullptr;
    // live toggle state (mirrors telemetry; drives the toggle buttons)
    bool             m_light_on = false;
    bool             m_cam_light_on = false;
    bool             m_camera_enabled = true;
    bool             m_ace_auto_feed = false;
    bool             m_ace_dry = false;
    std::string      m_ace_sig;                       // only rebuild ACE chips when this changes
};

}} // namespace Slic3r::GUI

#endif
