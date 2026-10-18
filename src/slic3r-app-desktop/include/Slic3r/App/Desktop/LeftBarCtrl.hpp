#pragma once

#include "TabsBarCtrl.hpp"

namespace Slic3r::App::Desktop {

class LeftBarCtrl : public TabsBarCtrl
{
public:
    LeftBarCtrl(wxWindow* parent, int orient, TabsBarMenus* menus = nullptr);
    ~LeftBarCtrl() = default;

    void OnColorsChanged() override;
    void UnselectPopupButtons() override;

    Button* preferences_btn() const;
    /// Orca's Prepare / Preview tabs: both show the Slicing page, in Plater or Preview mode.
    Button* prepare_btn() const;
    Button* preview_btn() const;

protected:
    void on_compact_mode_changed() override;
    void on_rescale() override;

private:
    Button* m_preferences_btn{nullptr};
    Button* m_prepare_btn{nullptr};
    Button* m_preview_btn{nullptr};

    int m_action_btn_sz{20};
};

} // namespace Slic3r::App::Desktop
