#include "Slic3r/App/Preview/DoubleSliderForGCode.hpp"

#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/Yoga/LambdaItem.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App::Preview {

static constexpr float SLIDER_GCODE_HEIGHT = 40.0f;
using FuncCommandExtraOpts                 = Platform::FuncCommandExtraOpts;

// ColorStack's viewer: times the print time
static constexpr std::array<float, 4> PLAY_SPEEDS{60.f, 300.f, 1200.f, 5000.f};
static constexpr float PLAY_WIDTH = 92.f;

DoubleSliderForGcode::DoubleSliderForGcode() :
    Imgui::DoubleSlider::Manager<unsigned int>(
        std::string("GCodeSlider"),
        Biz::L("Steps"),
        Yoga::Orientation::Horizontal
    )
{
    set_min_width(100);
    set_min_height(SLIDER_GCODE_HEIGHT);
    set_flex_shrink(0);
    m_ctrl->callbacks().value_changed       = [this]() { process_thumb_move(); };
    m_ctrl->callbacks().request_extra_frame = [this]() { process_request_extra_frames(); };

    // the play button and the speed before the slider
    auto play = std::make_unique<Yoga::LambdaItem>([this](Domain::Vec2f pos, Domain::Vec2f size) { render_play(pos, size); });
    play->set_width(PLAY_WIDTH);
    play->set_flex_shrink(0);
    insert(std::move(play), 0);
}

void DoubleSliderForGcode::set_play_callbacks(std::function<void()> start, std::function<bool(float)> tick)
{
    m_play_start = std::move(start);
    m_play_tick  = std::move(tick);
}

void DoubleSliderForGcode::toggle_play()
{
    m_playing = !m_playing;
    if (m_playing && m_play_start)
        m_play_start();
    process_request_extra_frames();
}

void DoubleSliderForGcode::render_play(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    const float d      = std::min(size.y(), 26.f * m_scale);
    const ImVec2 button(pos.x(), pos.y() + (size.y() - d) / 2.f);
    ImDrawList* draw   = ImGui::GetWindowDrawList();

    ImGui::SetCursorScreenPos(button);
    if (ImGui::InvisibleButton("##gcode_play", ImVec2(d, d)))
        toggle_play();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s", m_playing ? Biz::L("Pause").c_str() : Biz::L("Play").c_str());
    const ImVec2 c(button.x + d / 2.f, button.y + d / 2.f);
    draw->AddCircleFilled(c, d / 2.f, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
    const ImU32 glyph = ImGui::GetColorU32(ImGuiCol_Text);
    const float g     = d * 0.2f;
    if (m_playing) {
        draw->AddRectFilled(ImVec2(c.x - g, c.y - g), ImVec2(c.x - g * 0.3f, c.y + g), glyph);
        draw->AddRectFilled(ImVec2(c.x + g * 0.3f, c.y - g), ImVec2(c.x + g, c.y + g), glyph);
    } else
        draw->AddTriangleFilled(ImVec2(c.x - g * 0.7f, c.y - g), ImVec2(c.x - g * 0.7f, c.y + g), ImVec2(c.x + g, c.y), glyph);

    // the speed: a click takes the next one
    const std::string speed = std::to_string(int(PLAY_SPEEDS[m_speed])) + "x";
    const ImVec2 text_size  = ImGui::CalcTextSize(speed.c_str());
    const ImVec2 text_pos(button.x + d + 6.f * m_scale, pos.y() + (size.y() - text_size.y) / 2.f);
    ImGui::SetCursorScreenPos(text_pos);
    if (ImGui::InvisibleButton("##gcode_play_speed", text_size))
        m_speed = (m_speed + 1) % PLAY_SPEEDS.size();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Biz::L("Playback speed").c_str());
    draw->AddText(text_pos, ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled), speed.c_str());

    if (m_playing) {
        if (!m_play_tick || !m_play_tick(ImGui::GetIO().DeltaTime * PLAY_SPEEDS[m_speed]))
            m_playing = false;
        process_request_extra_frames();
    }
}

void DoubleSliderForGcode::register_commands(
    MenuManager& menu_manager,
    CommandBindingManager& command_binding_manager
)
{
    command_binding_manager.main_command_registry()
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-increase-slow",
                [this]() { move_current_thumb(1); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Right}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-decrease-slow",
                [this]() { move_current_thumb(-1); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Left}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-increase-medium",
                [this]() { move_current_thumb(5); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::Right
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-decrease-medium",
                [this]() { move_current_thumb(-5); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Shift),
                        Platform::KeyCode::Left
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-increase-fast",
                [this]() { move_current_thumb(10); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Right
                    }}
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "slider-gcode-decrease-fast",
                [this]() { move_current_thumb(-10); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{Platform::KeyboardShortcut{
                        Platform::KeyModifiers(Platform::KeyModifier::Ctrl),
                        Platform::KeyCode::Left
                    }}
                }
            )
        );

    command_binding_manager.main_command_registry().register_command(
        std::make_unique<Platform::FuncCommand>("slider-gcode-play", [this]() { toggle_play(); })
    );

    if (!m_menu_manager) {
        m_menu_manager = &menu_manager;
    }

    if (!m_command_binding_manager) {
        m_command_binding_manager = &command_binding_manager;
    }
}

} // namespace Slic3r::App::Preview
