#include "Slic3r/App/FlushingVolumesDialog.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include <cmath>
#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

namespace {

constexpr float CELL_WIDTH = 52.f;
constexpr float SWATCH_SIZE = 18.f;

Item* add_swatch(Item* parent, const Domain::ColorRGB& color, const std::string& number)
{
    Item* cell = parent->emplace_back<Item>();
    cell->set_width(CELL_WIDTH);
    cell->set_flex_shrink(0);
    cell->set_align_items(YGAlignCenter);
    cell->set_justify_content(YGJustifyCenter);
    cell->set_gap(4);
    Rectangle* swatch = cell->emplace_back<Rectangle>();
    swatch->set_width(SWATCH_SIZE);
    swatch->set_height(SWATCH_SIZE);
    swatch->set_rounding(SWATCH_SIZE / 2);
    swatch->set_fill(ImColor(color.r(), color.g(), color.b()));
    cell->emplace_back<Text>(number);
    return cell;
}

} // namespace

FlushingVolumesDialog::FlushingVolumesDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Dialog("FlushingVolumesDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this)
{
    set_closable(true);
    append_tab(Biz::_u8L("Flushing volumes"));

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(10);
    content()->set_padding(10);

    m_grid = content()->emplace_back<ScrollArea>();
    m_grid->set_orientation(Orientation::Vertical);
    m_grid->set_gap(4);
    m_grid->set_max_height(400);
    m_grid->set_max_width(600);

    Item* buttons = content()->emplace_back<Item>();
    buttons->set_orientation(Orientation::Horizontal);
    buttons->set_gap(10);
    const Paddings button_padding(15.f, 5.f);

    LayoutButton* auto_button = buttons->emplace_back<LayoutButton>(Biz::_u8L("Auto-calculate"));
    auto_button->set_content_padding(button_padding);
    auto_button->set_tooltip(Biz::_u8L("Calculate the volumes from the filament colours"));
    auto_button->callbacks().action = [this]()
    {
        rebuild(m_project_interactor.project_settings_interactor().wiping_volumes_from_colours(
            m_project_interactor.selected_config_container_id()
        ));
    };

    Item* spacer = buttons->emplace_back<Item>();
    spacer->set_flex_grow(1);

    LayoutButton* ok_button = buttons->emplace_back<LayoutButton>(Biz::_u8L("OK"));
    ok_button->set_content_padding(button_padding);
    ok_button->callbacks().action = [this]()
    {
        apply();
        close_action();
    };

    LayoutButton* cancel_button = buttons->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_button->set_content_padding(button_padding);
    cancel_button->callbacks().action = [this]() { close_action(); };

    callbacks().opened = [this]()
    {
        rebuild(m_project_interactor.project_settings_interactor().wiping_volumes(
            m_project_interactor.selected_config_container_id()
        ));
    };
}

void FlushingVolumesDialog::rebuild(const std::vector<double>& volumes)
{
    while (m_grid->object_count() > 0)
        m_grid->remove(m_grid->get_item(0));
    m_cells.clear();

    const std::vector<Domain::ColorRGB> colors =
        m_project_interactor.project_settings_interactor().get_colors(
            m_project_interactor.selected_config_container_id()
        );
    m_slot_count = size_t(std::lround(std::sqrt(double(volumes.size()))));

    // header row: the "to" filaments
    Item* header = m_grid->emplace_back<Item>();
    header->set_orientation(Orientation::Horizontal);
    header->set_gap(4);
    header->set_flex_shrink(0);
    Item* corner = header->emplace_back<Item>();
    corner->set_width(CELL_WIDTH);
    corner->set_flex_shrink(0);
    corner->set_align_items(YGAlignCenter);
    corner->set_justify_content(YGJustifyFlexEnd);
    corner->emplace_back<Text>(Biz::_u8L("mm³"));
    for (size_t to = 0; to < m_slot_count; ++to)
        add_swatch(header, to < colors.size() ? colors[to] : Domain::ColorRGB::GRAY(), std::to_string(to + 1));

    for (size_t from = 0; from < m_slot_count; ++from) {
        Item* row = m_grid->emplace_back<Item>();
        row->set_orientation(Orientation::Horizontal);
        row->set_align_items(YGAlignCenter);
        row->set_gap(4);
        row->set_flex_shrink(0);
        add_swatch(row, from < colors.size() ? colors[from] : Domain::ColorRGB::GRAY(), std::to_string(from + 1));
        for (size_t to = 0; to < m_slot_count; ++to) {
            InputTextField* cell = row->emplace_back<InputTextField>();
            cell->set_width(CELL_WIDTH);
            cell->set_flex_shrink(0);
            cell->set_validator(std::make_unique<DoubleValidator>(0., 10000.));
            cell->set_text(fmt::format("{:g}", volumes[from * m_slot_count + to]));
            cell->set_enabled(from != to);
            cell->set_tooltip(fmt::format("{} → {}, mm³", from + 1, to + 1));
            m_cells.push_back(cell);
        }
    }
}

std::vector<double> FlushingVolumesDialog::cell_values() const
{
    std::vector<double> volumes(m_cells.size(), 0.);
    for (size_t i = 0; i < m_cells.size(); ++i) {
        try {
            volumes[i] = std::stod(m_cells[i]->text());
        } catch (const std::exception&) {
            volumes[i] = 0.;
        }
    }
    return volumes;
}

void FlushingVolumesDialog::refresh_keeping_edits()
{
    if (!opened())
        return;
    const std::vector<double> edited = cell_values();
    const size_t old_n = m_slot_count;
    std::vector<double> volumes = m_project_interactor.project_settings_interactor().wiping_volumes(
        m_project_interactor.selected_config_container_id()
    );
    const size_t n = size_t(std::lround(std::sqrt(double(volumes.size()))));
    // the pairs that were already in the table keep what is typed there (Orca adds the new
    // filament's row and column and keeps the rest)
    for (size_t from = 0; from < std::min(n, old_n); ++from)
        for (size_t to = 0; to < std::min(n, old_n); ++to)
            volumes[from * n + to] = edited[from * old_n + to];
    rebuild(volumes);
}

void FlushingVolumesDialog::on_preset_selection_changed(
    Domain::SelectionId, Domain::SelectionId, Biz::Preset::PresetItemType type
)
{
    if (type == Biz::Preset::PresetItemType::PrinterPreset
        || type == Biz::Preset::PresetItemType::MaterialPreset)
        refresh_keeping_edits();
}

void FlushingVolumesDialog::on_hw_item_selection_changed(
    Domain::SelectionId, Domain::SelectionId, Biz::Preset::HwItemType
)
{
    refresh_keeping_edits();
}

void FlushingVolumesDialog::apply()
{
    std::vector<double> volumes = cell_values();
    for (size_t i = 0; i < m_slot_count; ++i)
        volumes[i * m_slot_count + i] = 0.;
    m_project_interactor.project_settings_interactor().set_wiping_volumes(
        m_project_interactor.selected_config_container_id(),
        std::move(volumes)
    );
}

void FlushingVolumesDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

} // namespace Slic3r::App
