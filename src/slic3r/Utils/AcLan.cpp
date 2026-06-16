#include "AcLan.hpp"
#include "Http.hpp"

#include <array>
#include <chrono>
#include <random>
#include <cstdint>
#include <cmath>
#include <sstream>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>

#include <openssl/evp.h>

namespace Slic3r {

using njson = nlohmann::json;
namespace asio = boost::asio;
using asio::ip::udp;
using asio::ip::tcp;

// ---------- small crypto / util helpers ----------

static std::string md5_hex(const std::string& in)
{
    unsigned char md[EVP_MAX_MD_SIZE]; unsigned int mdlen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, in.data(), in.size());
    EVP_DigestFinal_ex(ctx, md, &mdlen);
    EVP_MD_CTX_free(ctx);
    static const char* hexd = "0123456789abcdef";
    std::string out; out.reserve(mdlen * 2);
    for (unsigned i = 0; i < mdlen; ++i) { out.push_back(hexd[md[i] >> 4]); out.push_back(hexd[md[i] & 0xF]); }
    return out;
}

static std::string base64_decode(const std::string& s)
{
    static int8_t T[256]; static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) T[i] = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(unsigned char)a[i]] = (int8_t)i; init = true; }
    std::string out; int val = 0, bits = -8;
    for (unsigned char c : s) {
        if (c == '=' || T[c] == -1) continue;
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

// AES-128-CBC decrypt with PKCS7 unpadding. key/iv must be 16 bytes.
static bool aes128_cbc_decrypt(const std::string& cipher, const std::string& key,
                               const std::string& iv, std::string& out)
{
    if (key.size() < 16 || iv.size() < 16) return false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = false;
    out.resize(cipher.size() + 16);
    int outl1 = 0, outl2 = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
            (const unsigned char*)key.data(), (const unsigned char*)iv.data()) == 1 &&
        EVP_DecryptUpdate(ctx, (unsigned char*)out.data(), &outl1,
            (const unsigned char*)cipher.data(), (int)cipher.size()) == 1 &&
        EVP_DecryptFinal_ex(ctx, (unsigned char*)out.data() + outl1, &outl2) == 1) {
        out.resize(outl1 + outl2);
        ok = true;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static std::string rand_string(const std::string& charset, int n)
{
    static std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> d(0, (int)charset.size() - 1);
    std::string s; s.reserve(n);
    for (int i = 0; i < n; ++i) s.push_back(charset[d(rng)]);
    return s;
}

static int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// double URL-encode (encodeURIComponent twice), matching the official sign.
static std::string gen_sign(const std::string& token, int64_t ts, const std::string& nonce)
{
    std::string first  = md5_hex(token.substr(0, std::min<size_t>(16, token.size())));
    std::string second = md5_hex(first + std::to_string(ts) + nonce);
    return Http::url_encode(Http::url_encode(second));
}

static std::string qval(const std::string& q, const std::string& key)
{
    // parse value of key in a query string a=b&c=d
    for (auto& kv : [&]{ std::vector<std::string> v; boost::split(v, q, boost::is_any_of("&")); return v; }()) {
        auto eq = kv.find('=');
        if (eq != std::string::npos && kv.substr(0, eq) == key)
            return Http::url_decode(kv.substr(eq + 1));
    }
    return {};
}

// ---------- SSDP discovery ----------

std::vector<AcLanDevice> AcLan::discover(int timeout_ms)
{
    std::map<std::string, AcLanDevice> found;
    try {
        asio::io_context io;
        udp::socket sock(io, udp::endpoint(udp::v4(), 0));
        sock.set_option(asio::socket_base::reuse_address(true));
        sock.set_option(asio::socket_base::broadcast(true));
        const udp::endpoint mcast(asio::ip::make_address("239.255.255.250"), 1900);
        const std::string msearch =
            "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ac:3dprinter:fdm\r\n\r\n";
        boost::system::error_code ec;
        sock.send_to(asio::buffer(msearch), mcast, 0, ec);
        sock.send_to(asio::buffer(msearch), mcast, 0, ec);

        std::array<char, 8192> buf{};
        udp::endpoint sender;
        std::function<void()> do_recv = [&]() {
            sock.async_receive_from(asio::buffer(buf), sender,
                [&](const boost::system::error_code& e, std::size_t n) {
                    if (!e && n > 0) {
                        std::string resp(buf.data(), n);
                        std::string low = boost::algorithm::to_lower_copy(resp);
                        auto lp = low.find("location:");
                        if (lp != std::string::npos && low.find("ac:fdm") != std::string::npos) {
                            size_t s = resp.find_first_not_of(" ", lp + 9);
                            size_t e2 = resp.find_first_of("\r\n", s);
                            std::string loc = resp.substr(s, e2 - s);
                            auto qpos = loc.find('?');
                            std::string q = qpos == std::string::npos ? "" : loc.substr(qpos + 1);
                            AcLanDevice d;
                            d.ip = qval(q, "ip"); if (d.ip.empty()) d.ip = sender.address().to_string();
                            d.model_id = qval(q, "modelId");
                            d.model_name = qval(q, "modelName");
                            d.cn = qval(q, "cn");
                            d.name = qval(q, "deviceName");
                            if (!d.ip.empty()) found[d.ip] = d;
                        }
                    }
                    do_recv();
                });
        };
        do_recv();
        asio::steady_timer timer(io);
        timer.expires_after(std::chrono::milliseconds(timeout_ms));
        timer.async_wait([&](const boost::system::error_code&) { sock.cancel(); io.stop(); });
        io.run();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "AcLan::discover error: " << e.what();
    }
    std::vector<AcLanDevice> out;
    for (auto& kv : found) out.push_back(kv.second);
    return out;
}

// ---------- HTTP /info ----------

bool AcLan::get_info(const std::string& ip, AcLanDevice& out, std::string& err)
{
    std::string body; long status = 0; bool done = false;
    Http::get("http://" + ip + ":18910/info")
        .timeout_connect(5).timeout_max(8)
        .on_complete([&](std::string b, unsigned s) { body = std::move(b); status = s; done = true; })
        .on_error([&](std::string b, std::string e, unsigned s) { err = e.empty() ? b : e; status = s; })
        .perform_sync();
    if (!done || status != 200) { if (err.empty()) err = "no /info response"; return false; }
    try {
        auto j = njson::parse(body);
        out.ip = ip;
        out.model_id = j.value("modelId", "");
        out.model_name = j.value("modelName", "");
        out.cn = j.value("cn", "");
        out.usn = j.value("usn", "");
        out.name = j.value("deviceName", "");
        return true;
    } catch (const std::exception& e) { err = std::string("parse /info: ") + e.what(); return false; }
}

// ---------- local credential fetch ----------

AcLanCreds AcLan::fetch_credentials(const std::string& ip)
{
    AcLanCreds c; c.ip = ip; c.broker_host = ip;
    // 1) GET /info
    std::string info_body; long st = 0;
    Http::get("http://" + ip + ":18910/info").timeout_connect(5).timeout_max(8)
        .on_complete([&](std::string b, unsigned s) { info_body = std::move(b); st = s; })
        .on_error([&](std::string b, std::string e, unsigned s) { c.error = e.empty() ? b : e; })
        .perform_sync();
    if (st != 200) { if (c.error.empty()) c.error = "no /info"; return c; }

    std::string token, ctrl_url;
    try {
        auto j = njson::parse(info_body);
        token = j.value("token", "");
        ctrl_url = j.value("ctrlInfoUrl", "");
        c.model_id = j.value("modelId", "");
        c.model_name = j.value("modelName", "");
        c.file_upload_url = j.value("fileUploadurl", "");
    } catch (const std::exception& e) { c.error = std::string("parse /info: ") + e.what(); return c; }
    if (token.empty() || ctrl_url.empty()) { c.error = "missing token/ctrlInfoUrl"; return c; }

    // 2) signed POST /ctrl
    int64_t ts = now_ms();
    std::string nonce = rand_string("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 6);
    std::string did   = rand_string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 32);
    std::string sign  = gen_sign(token, ts, nonce);
    std::string url = ctrl_url + "?ts=" + std::to_string(ts) + "&nonce=" + nonce +
                      "&sign=" + sign + "&did=" + did;
    std::string ctrl_body; long cst = 0;
    Http::post(url).timeout_connect(5).timeout_max(10)
        .set_post_body(std::string())
        .on_complete([&](std::string b, unsigned s) { ctrl_body = std::move(b); cst = s; })
        .on_error([&](std::string b, std::string e, unsigned s) { c.error = e.empty() ? b : e; })
        .perform_sync();
    if (cst != 200) { if (c.error.empty()) c.error = "no /ctrl"; return c; }

    // 3) decrypt info
    std::string local_token, enc;
    try {
        auto j = njson::parse(ctrl_body);
        if (j.value("code", 0) != 200) { c.error = "ctrl code " + std::to_string(j.value("code", 0)); return c; }
        auto data = j.at("data");
        local_token = data.value("token", "");
        enc = data.value("info", "");
    } catch (const std::exception& e) { c.error = std::string("parse /ctrl: ") + e.what(); return c; }
    if (enc.empty() || local_token.empty()) { c.error = "missing ctrl token/info"; return c; }

    std::string key = token.substr(16, 16);
    std::string iv = local_token;
    if (iv.size() < 16) iv.append(16 - iv.size(), '\0'); else iv = iv.substr(0, 16);
    std::string plain;
    if (!aes128_cbc_decrypt(base64_decode(enc), key, iv, plain)) { c.error = "AES decrypt failed"; return c; }

    try {
        auto pj = njson::parse(plain);
        c.printer_id = pj.value("deviceId", "");
        c.username   = pj.value("username", "");
        c.password   = pj.value("password", "");
        c.device_crt = pj.value("devicecrt", "");
        c.device_pk  = pj.value("devicepk", "");
        if (c.model_id.empty()) c.model_id = pj.value("modeId", "");
        if (c.model_name.empty()) c.model_name = pj.value("modelName", "");
        std::string broker = pj.value("broker", "");  // mqtts://ip:port
        auto p = broker.find("://");
        if (p != std::string::npos) {
            std::string hp = broker.substr(p + 3);
            auto colon = hp.find(':');
            if (colon != std::string::npos) { c.broker_host = hp.substr(0, colon); c.broker_port = std::atoi(hp.c_str() + colon + 1); }
            else c.broker_host = hp;
        }
    } catch (const std::exception& e) { c.error = std::string("parse printer_data: ") + e.what(); return c; }

    if (c.username.empty() || c.password.empty() || c.printer_id.empty()) { c.error = "incomplete creds"; return c; }
    c.ok = true;
    return c;
}

// ---------- gcode upload ----------

std::string AcLan::file_md5_hex(const boost::filesystem::path& file)
{
    boost::filesystem::ifstream f(file, std::ios::binary);
    if (!f) return {};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    std::vector<char> buf(1 << 16);
    while (f) { f.read(buf.data(), buf.size()); std::streamsize n = f.gcount(); if (n > 0) EVP_DigestUpdate(ctx, buf.data(), (size_t)n); }
    unsigned char md[EVP_MAX_MD_SIZE]; unsigned int mdlen = 0;
    EVP_DigestFinal_ex(ctx, md, &mdlen); EVP_MD_CTX_free(ctx);
    static const char* hexd = "0123456789abcdef"; std::string out;
    for (unsigned i = 0; i < mdlen; ++i) { out.push_back(hexd[md[i] >> 4]); out.push_back(hexd[md[i] & 0xF]); }
    return out;
}

bool AcLan::upload_file(const std::string& file_upload_url, const std::string& filename,
                        const boost::filesystem::path& file,
                        std::function<void(int)> on_progress, std::string& err)
{
    bool ok = false; long status = 0;
    Http http = Http::post(file_upload_url);
    http.timeout_connect(10).timeout_max(600)
        .form_add(std::string("filename"), filename)
        .form_add_file(std::string("gcode"), file, filename)
        .on_complete([&](std::string body, unsigned s) {
            status = s;
            try { auto j = njson::parse(body); ok = (s == 200 && j.value("code", 0) == 200); }
            catch (...) { ok = (s == 200); }
            if (!ok) err = "upload http " + std::to_string(s) + ": " + body;
        })
        .on_error([&](std::string body, std::string e, unsigned s) { err = (e.empty() ? body : e); status = s; });
    if (on_progress) {
        http.on_progress([&](Http::Progress p, bool& /*cancel*/) {
            if (p.ultotal > 0) on_progress((int)(100 * p.ulnow / p.ultotal));
        });
    }
    http.perform_sync();
    return ok;
}

// ---------- minimal MQTT 3.1.1 over TLS ----------

namespace {
void mqtt_remlen(std::string& out, size_t len) {
    do { uint8_t b = len % 128; len /= 128; if (len) b |= 0x80; out.push_back((char)b); } while (len);
}
void mqtt_str(std::string& out, const std::string& s) {
    out.push_back((char)((s.size() >> 8) & 0xFF)); out.push_back((char)(s.size() & 0xFF));
    out += s;
}
using SslStream = asio::ssl::stream<tcp::socket>;

// Connect + TLS handshake + MQTT CONNECT/CONNACK on a caller-owned stream.
bool mqtt_open(const AcLanCreds& creds, SslStream& ssl, std::string& err) {
    tcp::resolver resolver(ssl.get_executor());
    auto eps = resolver.resolve(creds.broker_host, std::to_string(creds.broker_port));
    asio::connect(ssl.next_layer(), eps);
    ssl.next_layer().set_option(tcp::no_delay(true));
    ssl.handshake(asio::ssl::stream_base::client);
    std::string client_id = "piggie_" + rand_string("0123456789abcdef", 8);
    std::string vh; mqtt_str(vh, "MQTT"); vh.push_back((char)0x04);
    vh.push_back((char)0xC2);                              // user+pass+clean session
    vh.push_back((char)0x00); vh.push_back((char)0x3C);   // keepalive 60s
    std::string payload; mqtt_str(payload, client_id);
    mqtt_str(payload, creds.username); mqtt_str(payload, creds.password);
    std::string pkt; pkt.push_back((char)0x10);
    mqtt_remlen(pkt, vh.size() + payload.size()); pkt += vh; pkt += payload;
    asio::write(ssl, asio::buffer(pkt));
    std::array<char, 4> ack{};
    asio::read(ssl, asio::buffer(ack, 4));
    if ((uint8_t)ack[0] != 0x20 || (uint8_t)ack[3] != 0x00) {
        err = "MQTT not authorized (connack rc=" + std::to_string((uint8_t)ack[3]) + ")";
        return false;
    }
    return true;
}

void mqtt_publish(SslStream& ssl, const std::string& topic, const std::string& body) {
    std::string vh; mqtt_str(vh, topic);                  // QoS0: no packet id
    std::string pub; pub.push_back((char)0x30);
    mqtt_remlen(pub, vh.size() + body.size()); pub += vh; pub += body;
    asio::write(ssl, asio::buffer(pub));
}

// One-shot: open, publish a single message to a topic, close.
bool mqtt_send_one(const AcLanCreds& creds, const std::string& topic,
                   const std::string& body, std::string& err) {
    try {
        asio::io_context io;
        asio::ssl::context ctx(asio::ssl::context::tls_client);
        ctx.set_verify_mode(asio::ssl::verify_none);
        SslStream ssl(io, ctx);
        if (!mqtt_open(creds, ssl, err)) return false;
        mqtt_publish(ssl, topic, body);
        boost::system::error_code ec; ssl.shutdown(ec);
        return true;
    } catch (const std::exception& e) { err = std::string("MQTT error: ") + e.what(); return false; }
}

std::string web_topic(const AcLanCreds& c, const std::string& sub) {
    return "anycubic/anycubicCloud/v1/web/printer/" + c.model_id + "/" + c.printer_id + "/" + sub;
}
std::string envelope(const std::string& type, const std::string& action, const njson& data) {
    njson j = {{"type", type}, {"action", action}, {"timestamp", now_ms()},
               {"msgid", rand_string("0123456789abcdef", 8) + "-piggie"}, {"data", data}};
    return j.dump();
}
} // namespace

bool AcLan::send_print(const AcLanCreds& creds, const std::string& filename,
                       const std::string& md5, std::string& err)
{
    njson data = {{"taskid", "-1"}, {"url", "https://anycubic.com/store/aaa.gcode"},
                  {"filename", filename}, {"md5", md5}, {"filepath", nullptr}, {"filetype", 1}};
    std::string topic = "anycubic/anycubicCloud/v1/slicer/printer/" + creds.model_id +
                        "/" + creds.printer_id + "/print";
    return mqtt_send_one(creds, topic, envelope("print", "start", data), err);
}

bool AcLan::print_action(const AcLanCreds& creds, const std::string& action, std::string& err,
                         const std::string& taskid)
{
    // Verified against the official app: pause/resume/stop carry data:{taskid}; the
    // firmware drops the command if the id is missing/empty.
    njson data = {{"taskid", taskid}};
    return mqtt_send_one(creds, web_topic(creds, "print"), envelope("print", action, data), err);
}

bool AcLan::set_light(const AcLanCreds& creds, bool on, int brightness, std::string& err, int type)
{
    njson data = {{"type", type}, {"status", on ? 1 : 0}, {"brightness", brightness}};
    return mqtt_send_one(creds, web_topic(creds, "light"), envelope("light", "control", data), err);
}

bool AcLan::video_capture(const AcLanCreds& creds, bool start, std::string& err)
{
    // web .../video {type:video, action:"startCapture"|"stopCapture", data:null}
    return mqtt_send_one(creds, web_topic(creds, "video"),
                         envelope("video", start ? "startCapture" : "stopCapture", nullptr), err);
}

// --- temperatures / fans: type:"print", action:"update", settings:{...} on /print
static bool send_print_setting(const AcLanCreds& creds, const std::string& key, int value,
                               const std::string& taskid, std::string& err)
{
    njson data = {{"taskid", taskid}, {"settings", {{key, value}}}};
    return mqtt_send_one(creds, web_topic(creds, "print"), envelope("print", "update", data), err);
}

bool AcLan::set_nozzle_temp(const AcLanCreds& creds, int target_c, std::string& err, const std::string& taskid)
{ return send_print_setting(creds, "target_nozzle_temp", target_c, taskid, err); }

bool AcLan::set_bed_temp(const AcLanCreds& creds, int target_c, std::string& err, const std::string& taskid)
{ return send_print_setting(creds, "target_hotbed_temp", target_c, taskid, err); }

bool AcLan::set_fan(const AcLanCreds& creds, int pct, std::string& err, const std::string& taskid)
{ return send_print_setting(creds, "fan_speed_pct", pct, taskid, err); }

bool AcLan::set_aux_fan(const AcLanCreds& creds, int pct, std::string& err, const std::string& taskid)
{ return send_print_setting(creds, "aux_fan_speed_pct", pct, taskid, err); }

// --- axis: type:"axis" on /axis
bool AcLan::move_axis(const AcLanCreds& creds, int axis, int move_type, double distance_mm, std::string& err)
{
    njson data = {{"axis", axis}, {"move_type", move_type}, {"distance", distance_mm}};
    return mqtt_send_one(creds, web_topic(creds, "axis"), envelope("axis", "move", data), err);
}

bool AcLan::home_axis(const AcLanCreds& creds, int axis, std::string& err)
{
    njson data = {{"axis", axis}, {"move_type", (int)MoveHome}, {"distance", 0}};
    return mqtt_send_one(creds, web_topic(creds, "axis"), envelope("axis", "move", data), err);
}

bool AcLan::motors_off(const AcLanCreds& creds, std::string& err)
{
    return mqtt_send_one(creds, web_topic(creds, "axis"), envelope("axis", "turnOff", nullptr), err);
}

// --- ACE / multiColorBox
bool AcLan::ace_set_auto_feed(const AcLanCreds& creds, int box_id, bool on, std::string& err)
{
    njson data = {{"multi_color_box", njson::array({ {{"id", box_id}, {"auto_feed", on ? 1 : 0}} })}};
    return mqtt_send_one(creds, web_topic(creds, "multiColorBox"), envelope("multiColorBox", "setAutoFeed", data), err);
}

bool AcLan::ace_set_dry(const AcLanCreds& creds, int box_id, bool on, int target_temp_c, int minutes, std::string& err)
{
    njson dry = on ? njson{{"status", 1}, {"target_temp", target_temp_c}, {"duration", minutes * 60}}
                   : njson{{"status", 0}};
    njson data = {{"multi_color_box", njson::array({ {{"id", box_id}, {"drying_status", dry}} })}};
    return mqtt_send_one(creds, web_topic(creds, "multiColorBox"), envelope("multiColorBox", "setDry", data), err);
}

bool AcLan::ace_feed(const AcLanCreds& creds, int box_id, int slot_index, const std::string& type, std::string& err)
{
    njson feed = {{"slot_index", slot_index}, {"type", type}};
    njson data = {{"multi_color_box", njson::array({ {{"id", box_id}, {"feed_status", feed}} })}};
    return mqtt_send_one(creds, web_topic(creds, "multiColorBox"), envelope("multiColorBox", "feedFilament", data), err);
}

bool AcLan::ace_set_slot(const AcLanCreds& creds, int box_id, int slot_index,
                         const std::string& type, int r, int g, int b, std::string& err)
{
    // web .../multiColorBox setInfo: preserve the richer slot shape expected by ACE firmware.
    njson slot = {
        {"index", slot_index},
        {"type", type},
        {"sku", ""},
        {"color", njson::array({ r, g, b })},
        {"color_group", njson::array({ njson::array({ r, g, b, 255 }) })}
    };
    njson data = {{"multi_color_box", njson::array({ {{"id", box_id}, {"slots", njson::array({ slot })}} })}};
    return mqtt_send_one(creds, web_topic(creds, "multiColorBox"), envelope("multiColorBox", "setInfo", data), err);
}

bool AcLan::ace_set_slots(const AcLanCreds& creds, int box_id, const std::vector<AceSlot>& slots, std::string& err)
{
    njson slot_array = njson::array();
    for (const AceSlot& s : slots) {
        if (s.index < 0)
            continue;
        njson color_group = njson::array();
        for (const auto& c : s.color_group)
            color_group.push_back(njson::array({ c[0], c[1], c[2], c[3] }));
        if (color_group.empty())
            color_group.push_back(njson::array({ s.r, s.g, s.b, 255 }));

        njson slot = {
            {"index", s.index},
            {"type", s.material},
            {"sku", s.sku},
            {"color", njson::array({ s.r, s.g, s.b })},
            {"color_group", color_group}
        };
        if (s.status != 0)
            slot["status"] = s.status;
        slot_array.push_back(std::move(slot));
    }
    if (slot_array.empty()) {
        err = "no ACE slots to update";
        return false;
    }
    njson data = {{"multi_color_box", njson::array({ {{"id", box_id}, {"slots", slot_array}} })}};
    return mqtt_send_one(creds, web_topic(creds, "multiColorBox"), envelope("multiColorBox", "setInfo", data), err);
}

// ---------- status query (collect reports for a window) ----------

static int json_to_int(const njson& value, int fallback = 0)
{
    try {
        if (value.is_number_integer() || value.is_number_unsigned())
            return value.get<int>();
        if (value.is_number_float())
            return (int)std::lround(value.get<double>());
        if (value.is_boolean())
            return value.get<bool>() ? 1 : 0;
        if (value.is_string())
            return std::stoi(value.get<std::string>());
    } catch (...) {}
    return fallback;
}

static std::string json_to_string(const njson& value)
{
    try {
        if (value.is_string())
            return value.get<std::string>();
        if (value.is_number_integer() || value.is_number_unsigned())
            return std::to_string(value.get<int>());
    } catch (...) {}
    return {};
}

static int json_field_int(const njson& obj, std::initializer_list<const char*> keys, int fallback = 0)
{
    for (const char* key : keys) {
        if (obj.contains(key))
            return json_to_int(obj[key], fallback);
    }
    return fallback;
}

static std::string json_field_string(const njson& obj, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        if (obj.contains(key)) {
            std::string value = json_to_string(obj[key]);
            if (!value.empty())
                return value;
        }
    }
    return {};
}

static bool parse_rgb(const njson& color_node, int& r, int& g, int& b)
{
    try {
        if (color_node.is_array() && color_node.size() >= 3) {
            r = json_to_int(color_node[0], r);
            g = json_to_int(color_node[1], g);
            b = json_to_int(color_node[2], b);
            return true;
        }
        if (color_node.is_object()) {
            r = json_field_int(color_node, {"r", "red"}, r);
            g = json_field_int(color_node, {"g", "green"}, g);
            b = json_field_int(color_node, {"b", "blue"}, b);
            return true;
        }
        if (color_node.is_string()) {
            std::string hex = color_node.get<std::string>();
            if (!hex.empty() && hex[0] == '#')
                hex.erase(hex.begin());
            if (hex.size() == 6) {
                r = std::stoi(hex.substr(0, 2), nullptr, 16);
                g = std::stoi(hex.substr(2, 2), nullptr, 16);
                b = std::stoi(hex.substr(4, 2), nullptr, 16);
                return true;
            }
        }
    } catch (...) {}
    return false;
}

static void apply_report(AcLan::Status& st, const std::string& topic, const std::string& body)
{
    njson j;
    try { j = njson::parse(body); } catch (...) { return; }
    std::string type = j.value("type", "");

    // Capture for the console telemetry view (skip bare {"msgid":..} acks). The leaf is
    // the topic past .../public/<model>/<printer>/ .
    if (!type.empty()) {
        std::string leaf = topic;
        size_t pub = topic.find("/public/");
        if (pub != std::string::npos) {
            size_t p = pub + 8;
            for (int seg = 0; seg < 2 && p != std::string::npos && p <= topic.size(); ++seg) {
                size_t slash = topic.find('/', p);
                p = (slash == std::string::npos) ? std::string::npos : slash + 1;
            }
            if (p != std::string::npos && p <= topic.size()) leaf = topic.substr(p);
        }
        st.reports.push_back(leaf + ": " + body);
        if (st.reports.size() > 60) st.reports.erase(st.reports.begin(), st.reports.begin() + (st.reports.size() - 60));
    }

    if (!j.contains("data") || j["data"].is_null()) {
        if (j.contains("state")) st.state = j.value("state", st.state);
        return;
    }
    const njson& d = j["data"];
    if (type == "tempature") {
        st.nozzle_temp = d.value("curr_nozzle_temp", st.nozzle_temp);
        st.nozzle_target = d.value("target_nozzle_temp", st.nozzle_target);
        st.bed_temp = d.value("curr_hotbed_temp", st.bed_temp);
        st.bed_target = d.value("target_hotbed_temp", st.bed_target);
    } else if (type == "print") {
        st.cur_layer = d.value("curr_layer", st.cur_layer);
        st.total_layers = d.value("total_layers", st.total_layers);
        st.progress = d.value("progress", st.progress);
        st.remain_min = d.value("remain_time", st.remain_min);
        st.print_min = d.value("print_time", st.print_min);
        st.filename = d.value("filename", st.filename);
        if (j.contains("state")) st.state = j.value("state", st.state);
    } else if (type == "status") {
        if (j.contains("state")) st.state = j.value("state", st.state);
    } else if (type == "fan") {
        st.fan_speed_pct = d.value("fan_speed_pct", st.fan_speed_pct);
        st.aux_fan_speed_pct = d.value("aux_fan_speed_pct", st.aux_fan_speed_pct);
    } else if (type == "light") {
        const njson* lights = nullptr;
        if (d.contains("lights") && d["lights"].is_array())
            lights = &d["lights"];
        if (lights != nullptr) {
            for (const auto& l : *lights) {
                const int light_type = json_field_int(l, {"type", "light_type"}, 0);
                if (light_type == 1) {
                    st.light_on = json_field_int(l, {"status", "on"}, 0) != 0;
                    st.light_brightness = json_field_int(l, {"brightness"}, 0);
                } else if (light_type == 2) {
                    st.camera_light_on = json_field_int(l, {"status", "on"}, 0) != 0;
                    st.camera_light_brightness = json_field_int(l, {"brightness"}, 0);
                }
            }
        } else if (d.is_object()) {
            const int light_type = json_field_int(d, {"type", "light_type"}, 0);
            if (light_type == 1) {
                st.light_on = json_field_int(d, {"status", "on"}, 0) != 0;
                st.light_brightness = json_field_int(d, {"brightness"}, 0);
            } else if (light_type == 2) {
                st.camera_light_on = json_field_int(d, {"status", "on"}, 0) != 0;
                st.camera_light_brightness = json_field_int(d, {"brightness"}, 0);
            }
        }
    } else if (type == "peripherie") {
        st.has_camera = json_field_int(d, {"camera", "has_camera"}, 0) != 0;
        st.has_ace = json_field_int(d, {"multiColorBox", "multi_color_box", "has_ace"}, 0) != 0;
    } else if (type == "info") {
        st.version = d.value("version", st.version);
        if (d.contains("state")) st.state = d.value("state", st.state);
        if (d.contains("project") && d["project"].is_object()) {
            const njson& p = d["project"];
            st.progress = p.value("progress", st.progress);
            st.cur_layer = p.value("curr_layer", st.cur_layer);
            st.total_layers = p.value("total_layers", st.total_layers);
            st.remain_min = p.value("remain_time", st.remain_min);
            st.print_min = p.value("print_time", st.print_min);
            st.filename = p.value("filename", st.filename);
            st.state = p.value("state", st.state);
            // task_id arrives as the NUMBER -1 when idle; reading it with a string
            // default throws and aborts the whole parse (breaks temp/fan + ACE). Robust:
            if (p.contains("task_id")) { std::string t = json_to_string(p["task_id"]); if (!t.empty()) st.task_id = t; }
        }
        if (d.contains("temp") && d["temp"].is_object()) {
            const njson& t = d["temp"];
            st.nozzle_temp = t.value("curr_nozzle_temp", st.nozzle_temp);
            st.nozzle_target = t.value("target_nozzle_temp", st.nozzle_target);
            st.bed_temp = t.value("curr_hotbed_temp", st.bed_temp);
            st.bed_target = t.value("target_hotbed_temp", st.bed_target);
        }
    } else if (type == "multiColorBox") {
        const njson* boxes_node = nullptr;
        if (d.contains("multi_color_box") && d["multi_color_box"].is_array())
            boxes_node = &d["multi_color_box"];
        else if (d.contains("multiColorBox") && d["multiColorBox"].is_array())
            boxes_node = &d["multiColorBox"];
        else if (d.contains("boxes") && d["boxes"].is_array())
            boxes_node = &d["boxes"];
        else if (d.is_array())
            boxes_node = &d;

        if (boxes_node != nullptr) {
            st.has_ace = true;
            st.ace.clear();
            st.boxes.clear();
            for (const auto& box : *boxes_node) {
                AcLan::AceBox b;
                b.id = json_field_int(box, {"id", "box_id"}, 0);
                b.auto_feed = json_field_int(box, {"auto_feed", "autoFeed"}, 0);
                b.loaded_slot = json_field_int(box, {"loaded_slot", "current_slot", "slot_index"}, -1);
                b.temp = json_field_int(box, {"temp", "temperature"}, 0);
                if (box.contains("drying_status") && box["drying_status"].is_object()) {
                    const njson& ds = box["drying_status"];
                    b.dry_status = json_field_int(ds, {"status"}, 0);
                    b.dry_target = json_field_int(ds, {"target_temp", "temperature"}, 0);
                    b.dry_duration = json_field_int(ds, {"duration", "total_time"}, 0);
                    b.dry_remain = json_field_int(ds, {"remain_time", "remaining_time"}, 0);
                } else if (box.contains("drying") && box["drying"].is_object()) {
                    const njson& ds = box["drying"];
                    b.dry_status = json_field_int(ds, {"status", "enabled"}, 0);
                    b.dry_target = json_field_int(ds, {"target_temp", "temperature"}, 0);
                    b.dry_duration = json_field_int(ds, {"duration", "total_time"}, 0);
                    b.dry_remain = json_field_int(ds, {"remain_time", "remaining_time"}, 0);
                }
                const njson* slots_node = nullptr;
                if (box.contains("slots") && box["slots"].is_array())
                    slots_node = &box["slots"];
                else if (box.contains("filaments") && box["filaments"].is_array())
                    slots_node = &box["filaments"];
                else if (box.contains("trays") && box["trays"].is_array())
                    slots_node = &box["trays"];
                if (slots_node != nullptr) {
                    for (const auto& s : *slots_node) {
                        AcLan::AceSlot slot;
                        slot.index = json_field_int(s, {"index", "slot_index", "id"}, -1);
                        slot.material = json_field_string(s, {"type", "material", "filament_type", "name"});
                        slot.sku = json_field_string(s, {"sku", "filament_id"});
                        slot.status = json_field_int(s, {"status", "state"}, 0);
                        if (slot.material.empty() && s.contains("material") && s["material"].is_object())
                            slot.material = json_field_string(s["material"], {"type", "name", "material", "filament_type"});
                        if (s.contains("color"))
                            parse_rgb(s["color"], slot.r, slot.g, slot.b);
                        else if (s.contains("colour"))
                            parse_rgb(s["colour"], slot.r, slot.g, slot.b);
                        else if (s.contains("material") && s["material"].is_object() && s["material"].contains("color")) {
                            parse_rgb(s["material"]["color"], slot.r, slot.g, slot.b);
                        }
                        if (s.contains("color_group") && s["color_group"].is_array()) {
                            for (const auto& cg : s["color_group"]) {
                                if (!cg.is_array() || cg.size() < 3)
                                    continue;
                                const int cr = json_to_int(cg[0], slot.r);
                                const int cg_val = json_to_int(cg[1], slot.g);
                                const int cb = json_to_int(cg[2], slot.b);
                                const int ca = cg.size() > 3 ? json_to_int(cg[3], 255) : 255;
                                slot.color_group.push_back({ cr, cg_val, cb, ca });
                            }
                        }
                        b.slots.push_back(slot);
                        st.ace.push_back(slot);
                    }
                }
                st.boxes.push_back(std::move(b));
            }
        }
    }
}

AcLan::Status AcLan::query_status(const AcLanCreds& creds, int timeout_ms)
{
    Status st;
    try {
        asio::io_context io;
        asio::ssl::context ctx(asio::ssl::context::tls_client);
        ctx.set_verify_mode(asio::ssl::verify_none);
        SslStream ssl(io, ctx);
        std::string err;
        if (!mqtt_open(creds, ssl, err)) { st.raw_error = err; return st; }

        // SUBSCRIBE to public/<model>/<printer>/#
        std::string filter = "anycubic/anycubicCloud/v1/printer/public/" + creds.model_id + "/" + creds.printer_id + "/#";
        std::string svh; svh.push_back((char)0x00); svh.push_back((char)0x01); // packet id 1
        std::string sp; mqtt_str(sp, filter); sp.push_back((char)0x00);        // QoS 0
        std::string sub; sub.push_back((char)0x82); mqtt_remlen(sub, svh.size() + sp.size());
        sub += svh; sub += sp;
        asio::write(ssl, asio::buffer(sub));

        // ask for the main subsystems
        const std::pair<std::string, std::string> queries[] = {
            {"info", "query"}, {"status", "query"}, {"tempature", "query"}, {"fan", "query"},
            {"light", "query"}, {"peripherie", "query"}, {"multiColorBox", "getInfo"} };
        for (const auto& q : queries)
            mqtt_publish(ssl, web_topic(creds, q.first), envelope(q.first, q.second, nullptr));

        // collect for timeout_ms
        std::string buf;
        std::array<char, 4096> rb{};
        std::function<void()> do_read = [&]() {
            ssl.async_read_some(asio::buffer(rb), [&](const boost::system::error_code& e, std::size_t n) {
                if (e) return;
                buf.append(rb.data(), n);
                // frame MQTT packets
                size_t pos = 0;
                while (pos + 2 <= buf.size()) {
                    uint8_t type = (uint8_t)buf[pos];
                    size_t mult = 1, rl = 0, i = pos + 1; bool ok = false;
                    for (; i < buf.size() && i < pos + 5; ++i) {
                        uint8_t b = (uint8_t)buf[i]; rl += (b & 0x7F) * mult; mult *= 128;
                        if (!(b & 0x80)) { ok = true; ++i; break; }
                    }
                    if (!ok) break;
                    if (i + rl > buf.size()) break;          // wait for more
                    if ((type & 0xF0) == 0x30) {             // PUBLISH
                        size_t p = i;
                        uint16_t tl = ((uint8_t)buf[p] << 8) | (uint8_t)buf[p + 1]; p += 2;
                        std::string topic = buf.substr(p, tl); p += tl;
                        if (type & 0x06) p += 2;             // QoS>0 packet id
                        std::string msg = buf.substr(p, (i + rl) - p);
                        apply_report(st, topic, msg);
                    }
                    pos = i + rl;
                }
                if (pos) buf.erase(0, pos);
                do_read();
            });
        };
        do_read();
        asio::steady_timer timer(io);
        timer.expires_after(std::chrono::milliseconds(timeout_ms));
        timer.async_wait([&](const boost::system::error_code&) {
            boost::system::error_code ec; ssl.next_layer().cancel(ec); io.stop();
        });
        io.run();
        boost::system::error_code ec; ssl.shutdown(ec);
        st.ok = true;
    } catch (const std::exception& e) {
        st.raw_error = std::string("MQTT error: ") + e.what();
    }
    return st;
}

} // namespace Slic3r
