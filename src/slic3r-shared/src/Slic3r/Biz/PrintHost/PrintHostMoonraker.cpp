#include "Slic3r/Biz/PrintHost/PrintHostMoonraker.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

bool PrintHostMoonraker::perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    // POST /server/files/upload

    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const char* name              = get_name();
    const auto upload_filename    = m_upload_data.dest_path.filename();
    const auto upload_parent_path = m_upload_data.dest_path.parent_path();

    // If test fails, test_msg_or_host_ip contains the error message.
    std::string test_msg_or_host_ip;
    if (!test(test_msg_or_host_ip, progress_fn, retry_fn)) {
        error_fn(std::move(test_msg_or_host_ip));
        return false;
    }

    std::string url;
    bool res = true;

#ifdef WIN32
    // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
    if (m_print_host_config.host.find("https://") == 0 || test_msg_or_host_ip.empty())
#endif // _WIN32
    {
        // If https is entered we assume signed certificate is being used
        // IP resolving will not happen - it could resolve into address not being specified in cert
        url = make_url("server/files/upload");
    }
#ifdef WIN32
    else
    {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Curl uses easy_getinfo to get ip address of last successful transaction.
        // If it got the address use it instead of the stored in "host" variable.
        // This new address returns in "test_msg_or_host_ip" variable.
        // Solves troubles of uploads failing with name address.
        // in original address (m_host) replace host for resolved ip
        info_fn(PrintHostJobInfoTag::Resolve, test_msg_or_host_ip);
        url = Network::IHttp::substitute_host(make_url("server/files/upload"), test_msg_or_host_ip);
        SPDLOG_INFO("Upload address after ip resolve: " + url);
    }
#endif // _WIN32

    SPDLOG_INFO(
        "{}: Uploading file at {}, filename: {}, path: {}, print: {}",
        name,
        url,
        upload_filename.string(),
        upload_parent_path.string(),
        (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false")
    );
    /*
    The file must be uploaded in the request's body multipart/form-data (ie: <input type="file">). The following arguments may also be added to the form-data:
    root: The root location in which to upload the file.Currently this may be gcodes or config.If not specified the default is gcodes.
    path : This argument may contain a path(relative to the root) indicating a subdirectory to which the file is written.If a path is present the server will attempt to create any subdirectories that do not exist.
    checksum : A SHA256 hex digest calculated by the client for the uploaded file.If this argument is supplied the server will compare it to its own checksum calculation after the upload has completed.A checksum mismatch will result in a 422 error.
    Arguments available only for the gcodes root :
    print: If set to "true", Klippy will attempt to start the print after uploading.Note that this value should be a string type, not boolean.This provides compatibility with OctoPrint's upload API.
    */
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    set_auth(http.get());

    http->form_add("root", "gcodes");
    if (!upload_parent_path.empty())
        http->form_add("path", upload_parent_path.string());
    if (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint)
        http->form_add("print", "true");

    http->form_add_file("file", m_upload_data.source_path, upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File uploaded: HTTP {}: {}", name, status, body);
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error uploading file: {}, HTTP {}, body: `{}`", name, error, status, body);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Network::IHttp::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                SPDLOG_INFO("Moonraker: Upload canceled");
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

bool PrintHostMoonraker::test(std::string& msg, ProgressFn progress_fn, RetryFn retry_fn) const
{
    // GET /server/info

    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const char* name = get_name();

    bool res = true;
    auto url = make_url("server/info");

    SPDLOG_INFO("{}: Get version at: {}", name, url);

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url),
        retry_fn
    );
    set_auth(http.get());
    http->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error getting version: {}, HTTP {}, body: `{}`", name, error, status, body);
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got server/info: {}", name, body);

            try {
                // All successful HTTP requests will return a json encoded object in the form of :
                // {result: <response data>}
                nlohmann::json json = nlohmann::json::parse(body);
                if (!json.contains("result") || !json["result"].is_structured()) {
                    msg = "Could not parse server response";
                    res = false;
                    return;
                }
                if (!json["result"].contains("moonraker_version")
                    || !json["result"]["moonraker_version"].is_string())
                {
                    msg = "Could not parse server response";
                    res = false;
                    return;
                }
                SPDLOG_INFO("{}: Got version: {}", name, json["result"]["moonraker_version"].dump());
            } catch (const nlohmann::json::exception&) {
                res = false;
                msg = "Could not parse server response";
            }
        })
#ifdef _WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = address;
        })
#endif // _WIN32
        .on_progress(progress_fn)
        .perform_sync();

    return res;
}

std::string PrintHostMoonraker::make_url(const std::string& path) const
{
    if (m_print_host_config.host.find("http://") == 0
        || m_print_host_config.host.find("https://") == 0)
    {
        if (m_print_host_config.host.back() == '/') {
            return fmt::format("{}{}", m_print_host_config.host, path);
        } else {
            return fmt::format("{}/{}", m_print_host_config.host, path);
        }
    } else {
        return fmt::format("http://{}/{}", m_print_host_config.host, path);
    }
}

void PrintHostMoonraker::set_auth(Network::IHttp* http) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    switch (auth->auth_type) {
    case Domain::PrintHostAuthType::ApiKey:
        http->header("X-Api-Key", auth->api_key);
        break;
    case Domain::PrintHostAuthType::Digest:
        http->auth_digest(auth->username, auth->password);
        break;
    default:
        break;
    }

    if (!auth->ca_file.empty()) {
        http->ca_file(auth->ca_file);
    }
}

// ---- Filament lanes of a multi-material unit ----
// Ported from OrcaSlicer v2.5.0-alpha src/slic3r/Utils/MoonrakerPrinterAgent.cpp
// (fetch_moonraker_filament_data, fetch_hh_filament_info, normalize_color_value,
// map_filament_type_to_generic_id). Copyright (c) the OrcaSlicer authors, AGPL-3.0.

namespace {

std::string safe_json_string(const nlohmann::json& obj, const char* key)
{
    auto it = obj.find(key);
    if (it != obj.end() && it->is_string())
        return it->get<std::string>();
    return "";
}

int safe_json_int(const nlohmann::json& obj, const char* key)
{
    auto it = obj.find(key);
    if (it != obj.end() && it->is_number())
        return it->get<int>();
    return 0;
}

std::string safe_array_string(const nlohmann::json& arr, int idx)
{
    if (arr.is_array() && idx >= 0 && idx < static_cast<int>(arr.size()) && arr[idx].is_string())
        return arr[idx].get<std::string>();
    return "";
}

int safe_array_int(const nlohmann::json& arr, int idx)
{
    if (arr.is_array() && idx >= 0 && idx < static_cast<int>(arr.size()) && arr[idx].is_number())
        return arr[idx].get<int>();
    return 0;
}

// Orca stores "RRGGBBAA"; the slicer uses "#RRGGBB" (empty when unknown).
std::string to_slicer_color(const std::string& color)
{
    const std::string normalized = normalize_lane_color(color);
    if (normalized == "00000000")
        return {};
    return "#" + normalized.substr(0, 6);
}

} // namespace

std::string normalize_lane_color(const std::string& color)
{
    std::string value = color;
    boost::trim(value);

    // Remove 0x or 0X prefix if present
    if (value.size() >= 2 && (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)) {
        value = value.substr(2);
    }
    // Remove # prefix if present
    if (!value.empty() && value[0] == '#') {
        value = value.substr(1);
    }

    // Extract only hex digits
    std::string normalized;
    for (char c : value) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }

    // If 6 hex digits, add FF alpha
    if (normalized.size() == 6) {
        normalized += "FF";
    }

    // Validate length - return default if invalid
    if (normalized.size() != 8) {
        return "00000000";
    }

    return normalized;
}

std::string generic_filament_family(const std::string& material)
{
    std::string upper = material;
    boost::trim(upper);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    // Normalize reported material names (trimmed, uppercased) to a generic family.
    static const std::map<std::string, std::string> type_to_family = {
        // PLA variants
        {"PLA", "PLA"},
        {"PLA-CF", "PLA-CF"},
        {"PLA SILK", "PLA Silk"},
        {"PLA-SILK", "PLA Silk"},
        {"PLA HIGH SPEED", "PLA High Speed"},
        {"PLA-HS", "PLA High Speed"},
        {"PLA HS", "PLA High Speed"},

        // ABS/ASA variants
        {"ABS", "ABS"},
        {"ASA", "ASA"},

        // PETG/PET variants
        {"PETG", "PETG"},
        {"PET", "PETG"},
        {"PCTG", "PCTG"},

        // PA/Nylon variants
        {"PA", "PA"},
        {"NYLON", "PA"},
        {"PA-CF", "PA-CF"},
        {"PPA", "PPA-CF"},
        {"PPA-CF", "PPA-CF"},
        {"PPA-GF", "PPA-GF"},

        // PC variants
        {"PC", "PC"},

        // PP/PE variants
        {"PE", "PE"},
        {"PP", "PP"},

        // Support materials
        {"PVA", "PVA"},
        {"HIPS", "HIPS"},
        {"BVOH", "BVOH"},

        // TPU variants
        {"TPU", "TPU"},

        // Other materials
        {"EVA", "EVA"},
        {"PHA", "PHA"},
        {"COPE", "CoPE"},
        {"SBS", "SBS"},
    };

    auto it = type_to_family.find(upper);
    return it == type_to_family.end() ? std::string{} : it->second;
}

std::optional<std::vector<FilamentLane>> parse_moonraker_lane_data(const std::string& body)
{
    auto json = nlohmann::json::parse(body, nullptr, false, true);
    if (json.is_discarded())
        return std::nullopt;

    // Expected structure: { "result": { "namespace": "lane_data", "value": { "lane1": {...}, ... } } }
    if (!json.contains("result") || !json["result"].contains("value") || !json["result"]["value"].is_object())
        return std::nullopt;

    std::vector<FilamentLane> lanes;
    for (const auto& [lane_key, lane_obj] : json["result"]["value"].items()) {
        if (!lane_obj.is_object())
            continue;

        // Extract lane index from the "lane" field (tool number, 0-based)
        const std::string lane_str = safe_json_string(lane_obj, "lane");
        int lane_index = -1;
        if (!lane_str.empty()) {
            try {
                lane_index = std::stoi(lane_str);
            } catch (...) {
                lane_index = -1;
            }
        }
        if (lane_index < 0)
            continue;

        FilamentLane lane;
        lane.index       = lane_index;
        lane.material    = safe_json_string(lane_obj, "material");
        lane.color       = to_slicer_color(safe_json_string(lane_obj, "color"));
        lane.nozzle_temp = safe_json_int(lane_obj, "nozzle_temp");
        lane.bed_temp    = safe_json_int(lane_obj, "bed_temp");
        lanes.push_back(lane);
    }

    if (lanes.empty())
        return std::nullopt;
    return lanes;
}

std::optional<std::vector<FilamentLane>> parse_happy_hare_gates(const std::string& body)
{
    auto json = nlohmann::json::parse(body, nullptr, false, true);
    if (json.is_discarded())
        return std::nullopt;

    // Expected structure: { "result": { "status": { "mmu": { ... } } } }
    if (!json.contains("result") || !json["result"].contains("status")
        || !json["result"]["status"].contains("mmu") || !json["result"]["status"]["mmu"].is_object())
        return std::nullopt;

    const auto& mmu = json["result"]["status"]["mmu"];
    // Empty mmu object means Happy Hare is not installed
    if (mmu.empty() || !mmu.contains("num_gates") || !mmu["num_gates"].is_number())
        return std::nullopt;

    const int num_gates = mmu["num_gates"].get<int>();
    if (num_gates <= 0)
        return std::nullopt;

    const auto empty = nlohmann::json::array();
    const auto& gate_status      = mmu.contains("gate_status") ? mmu["gate_status"] : empty;
    const auto& gate_material    = mmu.contains("gate_material") ? mmu["gate_material"] : empty;
    const auto& gate_color       = mmu.contains("gate_color") ? mmu["gate_color"] : empty;
    const auto& gate_temperature = mmu.contains("gate_temperature") ? mmu["gate_temperature"] : empty;
    if (!gate_status.is_array() || !gate_material.is_array() || !gate_color.is_array()
        || !gate_temperature.is_array())
        return std::nullopt;

    std::vector<FilamentLane> lanes;
    for (int gate_idx = 0; gate_idx < num_gates; ++gate_idx) {
        // gate_status: -1 = unknown, 0 = empty, 1 or 2 = available
        if (safe_array_int(gate_status, gate_idx) <= 0)
            continue;
        FilamentLane lane;
        lane.index       = gate_idx;
        lane.material    = safe_array_string(gate_material, gate_idx);
        // Skip if no material type (empty gate)
        if (lane.material.empty())
            continue;
        lane.color       = to_slicer_color(safe_array_string(gate_color, gate_idx));
        lane.nozzle_temp = safe_array_int(gate_temperature, gate_idx);
        lanes.push_back(lane);
    }

    if (lanes.empty())
        return std::nullopt;
    return lanes;
}

std::optional<std::string> PrintHostMoonraker::get(const std::string& path, std::string& msg, RetryFn retry_fn) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    std::optional<std::string> result;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        make_url(path),
        retry_fn
    );
    set_auth(http.get());
    http->timeout_connection(5)
        .timeout_total(10)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200)
                result = std::move(body);
            else
                msg = format_error(body, "", status);
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            msg = format_error(body, error, status);
        })
#ifdef _WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
#endif // _WIN32
        .perform_sync();
    return result;
}

std::optional<std::vector<FilamentLane>> PrintHostMoonraker::fetch_filament_lanes(std::string& msg, RetryFn retry_fn) const
{
    // Moonraker lane data first (AFC and recent Happy Hare publish it), then Happy Hare's own status.
    if (auto body = get("server/database/item?namespace=lane_data", msg, retry_fn)) {
        if (auto lanes = parse_moonraker_lane_data(*body))
            return lanes;
    }
    if (auto body = get("printer/objects/query?mmu", msg, retry_fn)) {
        if (auto lanes = parse_happy_hare_gates(*body))
            return lanes;
    }
    if (msg.empty())
        msg = _u8L("The printer reports no multi-material unit lanes.");
    return std::nullopt;
}

std::optional<std::vector<FilamentLane>> fetch_filament_lanes(
    const PhysicalPrinter::PhysicalPrinterConfig& printer,
    std::string& msg,
    IPrintHost::RetryFn retry_fn
)
{
    const auto* upload = std::get_if<PhysicalPrinter::PrinterUpload>(&printer.payload);
    if (upload == nullptr || upload->type != Domain::PrintHostType::Moonraker) {
        msg = _u8L("Filament sync needs a printer connected through Moonraker.");
        return std::nullopt;
    }
    const PrintHostMoonraker host{
        printer,
        PrintHostJobData{std::monostate{}, {}, PrintHostExportFormat::Undefined}
    };
    return host.fetch_filament_lanes(msg, std::move(retry_fn));
}

} // namespace Slic3r::Biz::PrintHost
