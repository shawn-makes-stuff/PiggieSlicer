#ifndef slic3r_AnycubicHost_hpp_
#define slic3r_AnycubicHost_hpp_

// PiggieSlicer: Anycubic LAN print host. Wraps the fully-local AcLan protocol
// (SSDP discover / GET info / signed ctrl + AES creds / HTTP upload / MQTT print)
// behind Orca's standard PrintHost interface, so "Send to printer" works through
// the normal physical-printer plumbing. No cloud, no account, no proprietary DLL.
//
// Modeled on ElegooLink (another brand-specific LAN host). Registered in
// PrintHost::get_print_host() under host_type == htAnycubicLan.

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"

namespace Slic3r {

class DynamicPrintConfig;

class AnycubicHost : public PrintHost
{
public:
    explicit AnycubicHost(DynamicPrintConfig *config);
    ~AnycubicHost() override = default;

    const char* get_name() const override;

    bool     test(wxString &curl_msg) const override;
    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;

    bool upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    bool                       has_auto_discovery() const override { return false; } // SSDP handled in the device panel, not Bonjour
    bool                       can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string                get_host() const override { return m_host; }

private:
    std::string m_host; // printer IP (parsed from the "print_host" config field)
};

} // namespace Slic3r

#endif
