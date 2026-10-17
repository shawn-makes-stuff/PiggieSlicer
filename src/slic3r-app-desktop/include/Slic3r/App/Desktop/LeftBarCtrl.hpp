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

protected:
    void on_compact_mode_changed() override;
    void on_rescale() override;

private:
    Button* m_preferences_btn{nullptr};

    int m_action_btn_sz{20};
};

} // namespace Slic3r::App::Desktop
