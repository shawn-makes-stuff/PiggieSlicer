#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

#include <optional>
#include <string>
#include <vector>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

/// One filament lane (slot) of a multi-material unit, as the printer reports it.
struct FilamentLane
{
    int index{0};          ///< 0-based lane (tool number)
    std::string material;  ///< e.g. "PLA", as reported
    std::string color;     ///< "#RRGGBB", empty when unknown
    int nozzle_temp{0};
    int bed_temp{0};
};

/// Parses Moonraker's `server/database/item?namespace=lane_data` response (AFC, recent Happy Hare).
std::optional<std::vector<FilamentLane>> parse_moonraker_lane_data(const std::string& body);
/// Parses Happy Hare's `printer/objects/query?mmu` response (available gates only).
std::optional<std::vector<FilamentLane>> parse_happy_hare_gates(const std::string& body);
/// Orca's colour normalization: "RRGGBBAA" upper case, "00000000" when invalid.
std::string normalize_lane_color(const std::string& color);
/// Generic filament family ("PLA", "PETG", "PLA Silk", ...) of a reported material, empty if unknown.
std::string generic_filament_family(const std::string& material);

class PrintHostMoonraker : public IPrintHost {

public:
    PrintHostMoonraker(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)) {}
    
    PrintHostMoonraker(const PrintHostMoonraker&) = delete;
    PrintHostMoonraker& operator=(const PrintHostMoonraker&) = delete;
    PrintHostMoonraker(PrintHostMoonraker&& other) noexcept = default;
    PrintHostMoonraker& operator=(PrintHostMoonraker&& other) noexcept = default;
   
    ~PrintHostMoonraker() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "Moonraker"; }
    bool test(std::string& msg, ProgressFn progress_fn, RetryFn retry_fn) const override;

    /// Reads the lanes of the printer's multi-material unit: Moonraker lane data first, then Happy Hare.
    std::optional<std::vector<FilamentLane>> fetch_filament_lanes(std::string& msg, RetryFn retry_fn) const;
private:
    std::string make_url(const std::string& path) const;
    std::optional<std::string> get(const std::string& path, std::string& msg, RetryFn retry_fn) const;
    void set_auth(Network::IHttp* http) const;
};
/**
 * @brief Reads the multi-material unit lanes of a physical printer (synchronous, run it in a job).
 * Supported for Moonraker hosts; other hosts return nullopt with a message.
 */
std::optional<std::vector<FilamentLane>> fetch_filament_lanes(
    const PhysicalPrinter::PhysicalPrinterConfig& printer,
    std::string& msg,
    IPrintHost::RetryFn retry_fn = [](Network::IHttp::Retry, bool&) {}
);

} // namespace Slic3r::Biz::PrintHost