#include "AnycubicHost.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "AcLan.hpp"
#include "Http.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"

namespace Slic3r {

// Extract a bare host/IP from the user's print_host field, which may be a plain
// IP ("192.168.1.208"), a host:port, or a URL ("http://192.168.1.208:18910/").
static std::string bare_host(std::string h)
{
    boost::trim(h);
    const auto scheme = h.find("://");
    if (scheme != std::string::npos)
        h = h.substr(scheme + 3);
    h = h.substr(0, h.find('/'));      // drop path
    h = h.substr(0, h.find(':'));      // drop :port (AcLan uses fixed ports)
    return h;
}

AnycubicHost::AnycubicHost(DynamicPrintConfig *config)
    : m_host(config->opt_string("print_host"))
{}

const char* AnycubicHost::get_name() const { return "AnycubicLan"; }

bool AnycubicHost::test(wxString &curl_msg) const
{
    AcLanDevice info;
    std::string err;
    if (AcLan::get_info(bare_host(m_host), info, err))
        return true;
    curl_msg = wxString::FromUTF8(err.c_str());
    return false;
}

wxString AnycubicHost::get_test_ok_msg() const
{
    return _(L("Connected to the Anycubic printer over LAN."));
}

wxString AnycubicHost::get_test_failed_msg(wxString &msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
        % _utf8(L("Could not reach the Anycubic printer"))
        % std::string(msg.ToUTF8())).str());
}

bool AnycubicHost::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const std::string ip = bare_host(m_host);
    const std::string filename = upload_data.upload_path.filename().string();

    // 1) Fetch fresh local credentials (upload session token rotates per call).
    AcLanCreds creds = AcLan::fetch_credentials(ip);
    if (!creds.ok) {
        error_fn(wxString::FromUTF8(creds.error.c_str()));
        return false;
    }

    // 2) HTTP multipart upload of the sliced file to the printer.
    std::string err;
    bool cancelled = false;
    const bool uploaded = AcLan::upload_file(
        creds.file_upload_url, filename, upload_data.source_path,
        [&](int percent) {
            Http::Progress p(0, 0, 100, (size_t) percent, std::string());
            bool cancel = false;
            progress_fn(std::move(p), cancel);
            if (cancel) cancelled = true;
        },
        err);
    if (cancelled) return false;
    if (!uploaded) {
        error_fn(wxString::FromUTF8(err.c_str()));
        return false;
    }

    // 3) Optionally start the print over MQTT.
    if (upload_data.post_action == PrintHostPostUploadAction::StartPrint) {
        const std::string md5 = AcLan::file_md5_hex(upload_data.source_path);
        if (!AcLan::send_print(creds, filename, md5, err)) {
            error_fn(wxString::FromUTF8(err.c_str()));
            return false;
        }
    }

    (void) info_fn;
    return true;
}

} // namespace Slic3r
