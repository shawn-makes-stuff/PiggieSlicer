#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;

namespace Yoga {
class InputTextField;
class ScrollArea;
} // namespace Yoga

/**
 * @brief Orca's "Flushing volumes" dialog: the purging volume matrix (mm³, row = from, column = to)
 * of the selected bed's filament slots, editable, with "Auto-calculate" from the filament colours.
 * Values and storage are Biz::ProjectSettingsInteractor's (wiping_volumes / set_wiping_volumes).
 */
class FlushingVolumesDialog : public Yoga::Dialog, public Biz::Preset::IPresetChangedListener
{
public:
    FlushingVolumesDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    // a filament added / removed / changed while open: the table follows (edits kept)
    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;
    void on_hw_item_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::HwItemType type
    ) override;

protected:
    void close_action() override;

private:
    void rebuild(const std::vector<double>& volumes);
    void refresh_keeping_edits();
    std::vector<double> cell_values() const;
    void apply();

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        FlushingVolumesDialog>
        m_preset_changed_listener_scope;

    Yoga::ScrollArea* m_grid{nullptr};
    std::vector<Yoga::InputTextField*> m_cells;
    size_t m_slot_count{0};
};

} // namespace Slic3r::App
