#ifndef slic3r_AcLan_hpp_
#define slic3r_AcLan_hpp_

// Anycubic LAN printing — fully local, no cloud/account.
// Flow (reverse-engineered & verified): SSDP discover -> GET /info -> signed
// POST /ctrl -> AES-CBC decrypt -> {broker, deviceId, username, password} ->
// MQTT(9883) publish print + HTTP gcode upload. See doc/AcLan_protocol.md.

#include <array>
#include <string>
#include <vector>
#include <functional>
#include <boost/filesystem/path.hpp>

namespace Slic3r {

// A printer found on the LAN via SSDP (ST: ac:3dprinter:fdm).
struct AcLanDevice {
    std::string ip;
    std::string model_id;    // e.g. "20024" (from the printer; do not hardcode)
    std::string model_name;  // e.g. "Anycubic Kobra 3"
    std::string cn;          // device serial / cloud number
    std::string usn;         // uuid:fdm:<MAC>
    std::string name;        // deviceName
};

// Local credentials + connection info fetched from a printer on demand.
struct AcLanCreds {
    bool        ok = false;
    std::string error;
    std::string ip;
    std::string broker_host;     // usually == ip
    int         broker_port = 9883;
    std::string printer_id;      // deviceId — used in MQTT topics
    std::string model_id;
    std::string model_name;
    std::string username;        // MQTT username
    std::string password;        // MQTT password
    std::string device_crt;      // per-device TLS cert (PEM, optional)
    std::string device_pk;       // per-device TLS key  (PEM, optional)
    std::string file_upload_url; // from /info (?s=<session>), rotates per fetch
};

class AcLan
{
public:
    // Broadcast SSDP M-SEARCH (ST ac:3dprinter:fdm) and collect replies.
    static std::vector<AcLanDevice> discover(int timeout_ms = 3000);

    // GET http://<ip>:18910/info -> basic device info (no creds).
    static bool get_info(const std::string& ip, AcLanDevice& out, std::string& err);

    // Full local credential fetch: /info -> signed /ctrl -> AES decrypt.
    // Re-fetch before each session (upload session token rotates).
    static AcLanCreds fetch_credentials(const std::string& ip);

    // HTTP multipart upload of a gcode (or .gcode.3mf) file to the printer.
    // file_upload_url comes from a fresh fetch_credentials()/get info.
    static bool upload_file(const std::string& file_upload_url,
                            const std::string& filename,
                            const boost::filesystem::path& file,
                            std::function<void(int /*percent*/)> on_progress,
                            std::string& err);

    // Connect to the printer's MQTT broker and publish a print-start command.
    // filename is the uploaded name (".gcode"); md5 is the uploaded file's md5.
    static bool send_print(const AcLanCreds& creds,
                           const std::string& filename,
                           const std::string& md5,
                           std::string& err);

    // Print control: action = "pause" | "resume" | "stop".
    // taskid MUST be the current job's task id (firmware ignores the command with a
    // wrong/empty id). "-1" is the idle sentinel. Verified against the official app JS.
    static bool print_action(const AcLanCreds& creds, const std::string& action, std::string& err,
                             const std::string& taskid = "-1");

    // --- live controls (schemas verified against Kobra3 firmware, see NOTES.md) ---
    // Temperatures (°C). NOTE: firmware treats target 0 as "no change"; to turn a
    // heater OFF send a low value (e.g. 1). Values are NOT clamped to UI limits.
    // IMPORTANT: the firmware IGNORES the update unless taskid is the current job id
    // (or "-1" when idle) — an empty taskid is silently dropped (verified live).
    static bool set_nozzle_temp(const AcLanCreds& creds, int target_c, std::string& err, const std::string& taskid = "-1");
    static bool set_bed_temp   (const AcLanCreds& creds, int target_c, std::string& err, const std::string& taskid = "-1");
    // Fans, percent 0..100 (0 here IS honoured = off, unlike temps).
    static bool set_fan    (const AcLanCreds& creds, int pct, std::string& err, const std::string& taskid = "-1");
    static bool set_aux_fan(const AcLanCreds& creds, int pct, std::string& err, const std::string& taskid = "-1");

    // Axis move. axis: 1=X 2=Y 3=Z 4=XY 5=all; move_type: 0=minus 1=plus 2=home.
    enum Axis { AxisX = 1, AxisY = 2, AxisZ = 3, AxisXY = 4, AxisAll = 5 };
    enum MoveType { MoveMinus = 0, MovePlus = 1, MoveHome = 2 };
    static bool move_axis (const AcLanCreds& creds, int axis, int move_type, double distance_mm, std::string& err);
    static bool home_axis (const AcLanCreds& creds, int axis, std::string& err); // axis 3/4/5
    static bool motors_off(const AcLanCreds& creds, std::string& err);           // disable steppers

    // Chamber/work light. brightness 0..100 (ignored when off). type 1 = Kobra3 LED.
    static bool set_light(const AcLanCreds& creds, bool on, int brightness, std::string& err, int type = 1);

    // Camera/video stream control. action = "startCapture" | "stopCapture".
    // Only meaningful when the printer reports a camera (peripherie.camera != 0).
    static bool video_capture(const AcLanCreds& creds, bool start, std::string& err);

    // ACE / multi-color box controls.
    static bool ace_set_auto_feed(const AcLanCreds& creds, int box_id, bool on, std::string& err);
    static bool ace_set_dry(const AcLanCreds& creds, int box_id, bool on, int target_temp_c, int minutes, std::string& err);
    static bool ace_feed(const AcLanCreds& creds, int box_id, int slot_index, const std::string& type, std::string& err);
    // Set a slot's material type + color manually (multiColorBox setInfo). r/g/b 0..255.
    static bool ace_set_slot(const AcLanCreds& creds, int box_id, int slot_index,
                             const std::string& type, int r, int g, int b, std::string& err);

    // One-shot status query: connect, subscribe, ask, collect reports for
    // ~timeout_ms, return parsed snapshot.
    struct AceSlot {
        int index = -1;
        std::string material;
        std::string sku;
        int r = 0, g = 0, b = 0;
        int status = 0;
        std::vector<std::array<int, 4>> color_group;
    };
    struct AceBox {
        int id = 0;
        int auto_feed = 0;        // auto-refill on/off
        int loaded_slot = -1;     // currently fed slot (-1 none)
        int temp = 0;             // box internal temp
        int dry_status = 0;       // drying on/off
        int dry_target = 0;       // drying target °C
        int dry_duration = 0;     // total seconds
        int dry_remain = 0;       // remaining seconds
        std::vector<AceSlot> slots;
    };
    static bool ace_set_slots(const AcLanCreds& creds, int box_id,
                              const std::vector<AceSlot>& slots, std::string& err);
    struct Status {
        bool ok = false;
        std::string state;          // free / busy / printing / preheating / ...
        double nozzle_temp = 0, nozzle_target = 0;
        double bed_temp = 0, bed_target = 0;
        double fan_speed_pct = 0, aux_fan_speed_pct = 0;
        bool   light_on = false; int light_brightness = 0;
        bool   camera_light_on = false; int camera_light_brightness = 0;
        int progress = 0, cur_layer = 0, total_layers = 0, remain_min = 0;
        int print_min = 0;          // elapsed print minutes (total = print_min + remain_min)
        std::string filename;
        std::string version;
        std::string task_id = "-1";  // "-1" = idle sentinel (firmware needs this, not "")
        bool has_camera = false, has_ace = false;
        std::vector<AceSlot> ace;   // flat list of all slots (back-compat)
        std::vector<AceBox>  boxes; // per-box detail
        std::string raw_error;
        // Raw printer->app reports captured during the query window, for the console
        // telemetry view. Each entry is "<leaf-topic>: <json>". Capped to the latest few.
        std::vector<std::string> reports;
    };
    static Status query_status(const AcLanCreds& creds, int timeout_ms = 3000);

    // md5 hex of a file (matches what the print command expects).
    static std::string file_md5_hex(const boost::filesystem::path& file);
};

} // namespace Slic3r

#endif
