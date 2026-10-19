#pragma once

#include "Slic3r/App/Yoga/CollapsibleWindow.hpp"
#include "Slic3r/App/Preview/Legend.hpp"

namespace Slic3r::Domain {
struct FullPrintStatistics;
}

namespace Slic3r::App::Yoga {
class Text;
class Rectangle;
class ToggleButton;
class ComboBox;
}

namespace Slic3r::App::Preview {

class LegendWindow : public Yoga::CollapsibleWindow
{
public:
    LegendWindow(libvgcode::FdmViewer* viewer, FdmViewerWrapper* wrapper);
    LegendCallbacks& callbacks();
    void set_filter(const std::string& filter);
    void update_type_selector(const std::vector<std::string>& types, const std::vector<libvgcode::ViewType>& option_types, int selection);

private:
    void update_show_time_estimate(const libvgcode::FdmViewer& viewer);

private:
    Legend* m_legend{ nullptr };
    Yoga::ComboBox* m_type_selector{ nullptr };
    // Maps a combo box index to the ViewType it represents; the combo omits LayerTime* entries
    // when no layer times are available, so the index cannot be cast to ViewType directly.
    std::vector<libvgcode::ViewType> m_type_option_types;

    Yoga::ToggleButton* m_show_time_estimate{ nullptr };
    Yoga::ToggleButton* m_detail_view{ nullptr };

    Yoga::Rectangle* m_settings{ nullptr };
};

/// Orca's print summary: time and filament of the model, the purge (wipe tower and flushes) and
/// the start / end G-code, with the totals and the cost
class SlicedInfoWindow : public Yoga::CollapsibleWindow
{
public:
    SlicedInfoWindow();
    void update(const Domain::FullPrintStatistics& stats, const FdmViewerWrapper& viewer);

private:
    struct Row
    {
        Yoga::Item* row{nullptr};
        Yoga::Text* time{nullptr};
        Yoga::Text* weight{nullptr};
        Yoga::Text* length{nullptr};
    };
    Row add_row(const std::string& label, bool bold);

    Row m_model;
    Row m_purge;
    Row m_prepare;
    Row m_total;
    Yoga::Item* m_cost_row{nullptr};
    Yoga::Text* m_cost{nullptr};
    // the time and filament of each feature (extrusion role), coloured as the legend
    Yoga::Item* m_features_header{nullptr};
    Yoga::Item* m_features{nullptr};
};

} // namespace Slic3r::App::Preview
