// Clean-room reconstruction — window/platform surface interface.
#pragma once
#include <vector>

#include "platform/Input.h"
#include "platform/UiBindings.h"

namespace platform {

class IWindow {
public:
    virtual ~IWindow() = default;

    // Pump OS events and refresh `input`. Returns false when the window wants to close.
    virtual bool pump(InputFrame& input) = 0;

    // Present the current back buffer (swap).
    virtual void present() = 0;

    virtual int  width() const = 0;
    virtual int  height() const = 0;
    virtual bool focused() const = 0;

    // Update the OS window title (used for the debug HUD readout).
    virtual void setTitle(const char* title) = 0;

    // Capture/hide the cursor for mouse-look.
    virtual void setMouseCaptured(bool captured) = 0;

    // Logical UI command bindings (InputFrame::uiDown). Unknown names are ignored with a warning.
    virtual void setUiBindings(const UiBindings& b) = 0;
    // Hide the OS cursor over the client area (the UI draws its own: Cursor_GFX).
    virtual void setOsCursorHidden(bool hidden) = 0;

    // Display settings (the PC SKU's PCSettings): the resolutions the primary display offers (width, height),
    // windowed size, or fullscreen at that resolution (the monitor is switched to it: a display mode change, restored on
    // windowed / alt-tab / exit), and VSync (swap interval).
    struct Mode { int width, height; };
    virtual std::vector<Mode> displayModes() const = 0;
    // The window's monitor at its desktop (registry) mode - native size and refresh, unaffected by a fullscreen mode switch.
    virtual bool desktopMode(int& w, int& h, int& hz) const { (void)w; (void)h; (void)hz; return false; }
    virtual void setDisplayMode(int width, int height, bool fullscreen) = 0;
    virtual bool fullscreen() const = 0;
    virtual void setVSync(bool on) = 0;
    // PC EXTENSION (not in the original): cap presented frames per second (0 = no cap). Waits after the swap only, so
    // fixed-step simulation is unaffected.
    virtual void setFrameLimit(int fps) { (void)fps; }
    // DEV TOOL (scripted pad coverage): hold / release a pad button by its binding name ("A", "DPadDown", "LStickUp", ...)
    // as if read from XInput; it is ORed into the pad state, so it reaches the same name -> UiKey bindings as a real pad.
    virtual void injectPad(const std::string& button, bool down) { (void)button; (void)down; }
    virtual bool vsync() const = 0;
};

// Factory — implemented per platform. Returns nullptr on failure.
IWindow* createWindow(int width, int height, const char* title);

} // namespace platform
