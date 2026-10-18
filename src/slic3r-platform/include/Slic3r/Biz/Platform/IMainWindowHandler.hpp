#pragma once

namespace Slic3r::Biz::Platform {

/**
 * @brief Interface for handling platform-specific window operations
 */
class IMainWindowHandler
{
public:
    virtual ~IMainWindowHandler() = default;

    virtual bool has_fullscreen() const  = 0;
    virtual bool is_fullscreen() const   = 0;
    virtual void set_fullscreen(bool on) = 0;

    virtual void close_application() = 0;

    /// The app draws the window's caption itself (its top bar carries the window buttons and
    /// moves the window); otherwise the system title bar is kept (Linux, macOS).
    virtual bool has_custom_caption() const = 0;
    virtual void minimize_window() = 0;
    virtual void toggle_maximize_window() = 0;
    virtual bool is_window_maximized() const = 0;
    /// Moves the window with the mouse (call on a mouse press in the caption area).
    virtual void start_window_drag() = 0;
};

} // namespace Slic3r::Biz::Platform
