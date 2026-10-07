// Clean-room reconstruction — platform-neutral input.
// Gameplay code depends ONLY on this, never on Win32/XInput/etc.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

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
    Grenade,   // G = "Throw Grenade / Detach Turret" (TossGrenade) [CONF Controls card]
    Interact,  // E = "Interact / Pick Up / Revive" (TryPickup) [CONF Controls card]
    Killstreak,  // B = "Look At / Kill Streak" (TriggerKillstreak) [CONF Controls card]
    Ability1,  // Ctrl = Ability1 (Ability0 is Shift = Button::Dash in robot form) [CONF Controls card]   // Swap Weapons: PC PgUp / PgDn (and the mouse wheel) [CONF Controls card]   // jet Hover Up / Down: PC C / V [CONF Controls card, TnPlayerInput.KeyDescriptions]
    DebugNextStart, DebugPrevStart,   // test only (not WFC): F6 / F7 cycle the authored player starts
    Count
};

// Menu / UI keys (frontend movies): keyboard keys and gamepad buttons, platform-neutral.
enum class UiKey { Up, Down, Left, Right, Accept, Back, X, Y, Start, Select, LB, RB, LT, RT, LThumb, RThumb, Count };

struct InputFrame {
    uint32_t uiDown = 0;          // bit (1 << UiKey) held (keyboard or pad)
    bool  down[(int)Button::Count]    = {};   // currently held
    bool  pressed[(int)Button::Count] = {};   // rising edge this frame
    float mouseDX = 0.0f;
    float mouseDY = 0.0f;
    // Gamepad (optional; zeroed if none connected)
    bool  padConnected = false;
    float padLX = 0, padLY = 0;   // left stick, -1..1
    float padRX = 0, padRY = 0;   // right stick, -1..1
    float padLT = 0;              // left trigger, 0..1
    // Absolute pointer (UI): client pixels, -1 when the cursor is outside the client area or the mouse is captured.
    int   mouseX = -1, mouseY = -1;
    bool  mouseLeft = false, mouseRight = false;
    float mouseWheel = 0.0f;      // notches this frame (+ = away from the user)
    // Device activity this frame (the last-used device for button prompts): any pad button / stick / trigger, any bound
    // key or mouse button.
    bool  padActive = false;
    bool  keyActive = false;
    // Text entry (UI input fields): characters typed this frame (WM_CHAR, UTF-32) and raw key presses including
    // auto-repeat (Win32 virtual-key codes), in order.
    std::u32string text;
    std::vector<uint16_t> keyPresses;

    bool isDown(Button b) const { return down[(int)b]; }
    bool wasPressed(Button b) const { return pressed[(int)b]; }
    bool uiIsDown(UiKey k) const { return (uiDown >> (int)k) & 1u; }
};

} // namespace platform
