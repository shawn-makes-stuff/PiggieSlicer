#pragma once

#include "Slic3r/App/Imgui/DoubleSlider.hpp"

#include <functional>

namespace Slic3r::App::Preview {

class DoubleSliderForGcode : public Imgui::DoubleSlider::Manager<unsigned int>
{
public:
    explicit DoubleSliderForGcode();

    void set_render_as_disabled(bool value) { m_render_as_disabled = value; }
    bool is_rendering_as_disabled() const { return m_render_as_disabled; }

    void register_commands(
        MenuManager& menu_manager,
        CommandBindingManager& command_binding_manager
    ) override;

    // Playback (as ColorStack's viewer): start() when play is pressed; tick(seconds of print time) every frame while
    // playing, false when the end is reached
    void set_play_callbacks(std::function<void()> start, std::function<bool(float)> tick);
    void toggle_play();

private:
    void render_play(const ImRect& line);

    bool m_render_as_disabled{false};
    bool m_playing{false};
    size_t m_speed{1}; // index into the playback speeds
    std::function<void()> m_play_start;
    std::function<bool(float)> m_play_tick;
};

} //namespace Slic3r::App::Preview
