// Clean-room reconstruction — platform-neutral input.
// Gameplay code depends ONLY on this, never on Win32/XInput/etc.
#pragma once

namespace platform {

enum class Button {
    Forward, Back, Left, Right,
    Jump, Transform, Fire, Sprint,
    Reload, CameraToggle, Debug, Quit,
    // Vehicle DASH / ram action. [CONF] TnTruckForm.Driving.UpdateNitro starts the nitro ram on the
    // dash input while driving on wheels (boosting). Final control mapping belongs to Gameplay.
    Dash,
    Count
};

struct InputFrame {
    bool  down[(int)Button::Count]    = {};   // currently held
    bool  pressed[(int)Button::Count] = {};   // rising edge this frame
    float mouseDX = 0.0f;
    float mouseDY = 0.0f;
    // Gamepad (optional; zeroed if none connected)
    bool  padConnected = false;
    float padLX = 0, padLY = 0;   // left stick, -1..1
    float padRX = 0, padRY = 0;   // right stick, -1..1

    bool isDown(Button b) const { return down[(int)b]; }
    bool wasPressed(Button b) const { return pressed[(int)b]; }
};

} // namespace platform
