// Clean-room reconstruction — platform-neutral input.
// Gameplay code depends ONLY on this, never on Win32/XInput/etc.
#pragma once

namespace platform {

enum class Button {
    Forward, Back, Left, Right,
    Jump, Transform, Fire, Sprint,
    Reload, CameraToggle, Debug, Quit,
    FineAim,   // WFC: RightMouseButton = "ToggleFineAim | Boost"; pad LeftTrigger = "FineAim | Boost"
    Dash,      // Dash = VehicleSpecialMove (hover dash / nitro): PC Shift, pad RightShoulder [CONF]
    Ascend, Descend,
    NextWeapon, PrevWeapon,
    Melee,     // Q / middle mouse = "Melee" [CONF Controls card]
    Killstreak,  // B = "Look At / Kill Streak" (TriggerKillstreak) [CONF Controls card]
    Ability1,  // Ctrl = Ability1 (Ability0 is Shift = Button::Dash in robot form) [CONF Controls card]   // Swap Weapons: PC PgUp / PgDn (and the mouse wheel) [CONF Controls card]   // jet Hover Up / Down: PC C / V [CONF Controls card, TnPlayerInput.KeyDescriptions]
    DebugNextStart, DebugPrevStart,   // test only (not WFC): F6 / F7 cycle the authored player starts
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
    float padLT = 0;              // left trigger, 0..1

    bool isDown(Button b) const { return down[(int)b]; }
    bool wasPressed(Button b) const { return pressed[(int)b]; }
};

} // namespace platform
