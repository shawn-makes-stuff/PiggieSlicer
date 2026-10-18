#pragma once

#include "Types.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include <string>

namespace Slic3r::App::libvgcode {
class FdmViewer;
} // namespace Slic3r::App::libvgcode

namespace Slic3r::App::Preview {

class FdmViewerWrapper;

struct LegendCallbacks
{
    GCodeViewTypeChangedCallback                    cb_view_type_changed{ nullptr };
    ExtrusionRoleVisibilityChangedCallback          cb_extrusion_role_visibility_changed{ nullptr };
};

class Legend : public Yoga::Item {
public:
    LegendCallbacks& callbacks();

    Legend(libvgcode::FdmViewer* viewer, FdmViewerWrapper* wrapper);

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    void set_detail_view(bool detail);
    /// Shows only the items whose name contains the text (case-insensitive; the preview's search)
    void set_filter(const std::string& filter);
    void set_show_time_estimate(bool show);

private:
    Yoga::Vec2f get_item_size() override;

private:
    libvgcode::FdmViewer* m_viewer = nullptr;
    FdmViewerWrapper* m_wrapper = nullptr;

    bool m_detail_view = false;
    std::string m_filter;
    bool m_show_time_estimate = true;
    Yoga::Vec2f m_size = { 0, 150 };

    LegendCallbacks m_callback;
};

} // namespace Slic3r::App::Preview
