#include "Slic3r/App/Preview/LegendWindow.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <Slic3r/App/libvgcode/Types.hpp>
#include <Slic3r/App/libvgcode/FdmViewer.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/PrintStatistics.hpp"
#include "Slic3r/App/Preview/FdmViewerWrapper.hpp"
#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/Time.hpp"

namespace Slic3r::App::Preview {

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

LegendWindow::LegendWindow(libvgcode::FdmViewer* viewer, FdmViewerWrapper* wrapper)
    : CollapsibleWindow(_u8L("Legend"), "LegendWindow")
{
    content()->set_padding(0.f);
    Item* scroll_area = content()->emplace_back<ScrollArea>();
    scroll_area->set_orientation(Orientation::Vertical);
    scroll_area->set_padding(Paddings(20.f, 10.f, 20.f, 0.f));
    m_legend = scroll_area->emplace_back<Legend>(viewer, wrapper);
    m_legend->set_flex_shrink(0.f);

    m_show_time_estimate = content()->emplace_back<ToggleButton>(_u8L("Used filament"));
    m_show_time_estimate->set_padding(Paddings(20.f, 5.f));
    m_show_time_estimate->set_visible(false);
    m_show_time_estimate->set_checked(true);
    m_show_time_estimate->callbacks().checked_changed = [this](bool checked) { 
        m_legend->set_show_time_estimate(checked);
        m_show_time_estimate->set_label(checked ? _u8L("Used filament") : _u8L("Time estimate"));
        m_show_time_estimate->set_tooltip(checked ? _u8L("Switch to show used filament").c_str() : _u8L("Switch to show time estimate"));
    };

    m_settings = content()->emplace_back<Rectangle>();
    m_settings->set_flex_shrink(0.f);
    m_settings->set_fill(
        Imgui::adjust_brightness(m_theme->color_imgui(Platform::Color::WindowBg), 1.25f)
    );
    m_settings->set_flags(ImDrawFlags_RoundCornersBottom);
    m_settings->set_padding(Paddings(20.f, 15.f));
    m_settings->set_gap(10.f);

    // change to combobox later
    m_type_selector = m_settings->emplace_back<ComboBox>("Types");
    m_type_selector->set_flags(ImGuiComboFlags_HeightLargest);
    m_type_selector->set_flex_grow(1.0);
    m_type_selector->callbacks().selection_changed = [this, viewer](int current_index) {
        if (current_index < 0 || current_index >= int(m_type_option_types.size()))
            return;
        viewer->set_view_type(m_type_option_types[current_index]);
        if (m_legend->callbacks().cb_view_type_changed)
            m_legend->callbacks().cb_view_type_changed();
        update_show_time_estimate(*viewer);
    };

    m_detail_view = m_settings->emplace_back<ToggleButton>(_u8L("Detail view"));
    m_detail_view->set_font_type(Render::ImguiFontType::Bold);
    m_detail_view->callbacks().checked_changed = [this, viewer](bool checked) {
        m_legend->set_detail_view(checked);
        update_show_time_estimate(*viewer);
    };
}

LegendCallbacks& LegendWindow::callbacks()
{
    return m_legend->callbacks();
}

void LegendWindow::set_filter(const std::string& filter)
{
    m_legend->set_filter(filter);
}

void LegendWindow::update_type_selector(const std::vector<std::string>& types, const std::vector<libvgcode::ViewType>& option_types, int selection)
{
    m_type_option_types = option_types;
    m_type_selector->set_items(types);
    m_type_selector->set_current_index(selection);
}

void LegendWindow::update_show_time_estimate(const libvgcode::FdmViewer& viewer)
{
    libvgcode::ViewType type = viewer.view_type();
    bool is_visible = m_detail_view->checked() && (type == libvgcode::ViewType::FeatureType ||
        (type == libvgcode::ViewType::ColorPrint && viewer.has_gcode_events_to_show()));
    m_show_time_estimate->set_visible(is_visible);
}

SlicedInfoWindow::SlicedInfoWindow()
    : CollapsibleWindow(_u8L("Sliced Info"), "SlicedInfoWindow")
{
    content()->set_orientation(Orientation::Vertical);
    content()->set_padding(Paddings(20.f, 10.f));
    content()->set_gap(6.f);

    const Row header = add_row(std::string{}, true);
    header.time->set_text(_u8L("Time"));
    header.weight->set_text(_u8L("Weight"));
    header.length->set_text(_u8L("Length"));
    m_model   = add_row(_u8L("Model"), false);
    m_purge   = add_row(_u8L("Purge"), false);
    m_prepare = add_row(_u8L("Prepare"), false);
    m_total   = add_row(_u8L("Total"), true);

    m_cost_row = content()->emplace_back<Item>();
    m_cost_row->set_gap(10.f);
    Text* cost_label = m_cost_row->emplace_back<Text>(_u8L("Cost"));
    cost_label->set_font_type(Render::ImguiFontType::Bold);
    cost_label->set_width(70.f);
    m_cost = m_cost_row->emplace_back<Text>(std::string{});
}

SlicedInfoWindow::Row SlicedInfoWindow::add_row(const std::string& label, bool bold)
{
    Row r;
    r.row = content()->emplace_back<Item>();
    r.row->set_gap(10.f);
    Text* label_text = r.row->emplace_back<Text>(label);
    label_text->set_font_type(Render::ImguiFontType::Bold);
    label_text->set_width(70.f);
    auto value = [&r, bold]()
    {
        Text* text = r.row->emplace_back<Text>(std::string{});
        text->set_font_type(bold ? Render::ImguiFontType::Bold : Render::ImguiFontType::Regular);
        text->set_flex_basis(0.f);
        text->set_flex_grow(1.f);
        return text;
    };
    r.time   = value();
    r.weight = value();
    r.length = value();
    return r;
}

void SlicedInfoWindow::update(const Domain::FullPrintStatistics& stats, const FdmViewerWrapper& viewer)
{
    using Domain::GCodeExtrusionRole;
    // time: the purge is the wipe tower's, the preparation the start / end G-code's (Orca's
    // "Prepare time"); the model is the rest
    const float total_time   = stats.normal_mode_time.time;
    const float purge_time   = viewer.extrusion_role_estimated_time(GCodeExtrusionRole::WipeTower);
    const float prepare_time = viewer.extrusion_role_estimated_time(GCodeExtrusionRole::Custom);
    // filament: the purge is the wipe tower's and the flushes' (per role: metres, grams)
    const float purge_g    = stats.total_used_filament_for_wipe_tower_g + stats.total_used_filament_for_flush_g;
    const float purge_mm   = stats.total_used_filament_for_wipe_tower_mm + stats.total_used_filament_for_flush_mm;
    const auto  custom     = stats.used_filaments_per_role.find(GCodeExtrusionRole::Custom);
    const bool  has_custom = custom != stats.used_filaments_per_role.end();
    const float prepare_g  = has_custom ? custom->second.second : 0.f;
    const float prepare_mm = has_custom ? 1000.f * custom->second.first : 0.f;

    auto set = [](Row& row, float time, float g, float mm)
    {
        row.time->set_text(Utils::get_time_dhms(std::max(0.f, time)));
        row.weight->set_text(format("%1$.2f g", std::max(0.f, g)));
        row.length->set_text(format("%1$.2f m", std::max(0.f, mm) / 1000.f));
    };
    set(m_model,
        total_time - purge_time - prepare_time,
        stats.total_used_filament_g - purge_g - prepare_g,
        stats.total_used_filament_mm - purge_mm - prepare_mm);
    set(m_purge, purge_time, purge_g, purge_mm);
    set(m_prepare, prepare_time, prepare_g, prepare_mm);
    set(m_total, total_time, stats.total_used_filament_g, stats.total_used_filament_mm);
    m_purge.row->set_visible(purge_time > 0.f || purge_mm > 0.f);
    m_cost->set_text(format("%1$.2f", stats.total_filament_cost));
    m_cost_row->set_visible(stats.total_filament_cost > 0.f);
}

} // namespace Slic3r::App::Preview
