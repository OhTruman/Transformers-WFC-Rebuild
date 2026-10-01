// Clean-room reconstruction — window/platform surface interface.
#pragma once
#include "platform/Input.h"

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
};

// Factory — implemented per platform. Returns nullptr on failure.
IWindow* createWindow(int width, int height, const char* title);

} // namespace platform
