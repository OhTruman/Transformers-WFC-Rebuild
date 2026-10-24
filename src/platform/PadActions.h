// [Systems] A controller's buttons -> the match actions (platform::Button), the original's multiplayer pad layout as a pure
// function (testable without a controller). Source: the authored Engine.PlayerInput bindings (XboxTypeS_*), with the profile's
// per-pad remaps (TnGameViewportClient.MapInputKeyForController, in game only - RE SPLITSCREEN_RE section 1):
//   SwitchRightThumbstickAndB: B <-> RightThumbstick;
//   UseAlternateControlScheme: Y -> LeftThumbstick, LeftThumbstick -> none, DPad_Up -> Y.
// The sticks, the left trigger's analog FineAim / Boost and Start / Back stay where they are read today (InputFrame pad fields, UiKey).
#pragma once
#include <cstdint>

#include "platform/Input.h"

namespace platform {

// XInput wButtons bit values (InputFrame::padButtons).
constexpr uint32_t kPadStart = 0x0010, kPadBack = 0x0020, kPadLThumb = 0x0040, kPadRThumb = 0x0080, kPadLB = 0x0100, kPadRB = 0x0200,
                   kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000;

// The profile's remaps: physical buttons -> the logical keys the bindings name.
inline uint32_t remapPad(uint32_t phys, bool alternateScheme, bool swapRightStickAndB) {
    uint32_t out = phys & ~(kPadB | kPadRThumb | kPadY | kPadLThumb | kPadDPadUp);
    auto key = [&](uint32_t physBit) {
        if (!(phys & physBit)) return;
        uint32_t k = physBit;
        if (swapRightStickAndB && k == kPadB) k = kPadRThumb;
        else if (swapRightStickAndB && k == kPadRThumb) k = kPadB;
        if (alternateScheme) {
            if (k == kPadY) k = kPadLThumb;
            else if (k == kPadLThumb) k = 0;
            else if (k == kPadDPadUp) k = kPadY;
        }
        out |= k;
    };
    for (uint32_t b : {kPadB, kPadRThumb, kPadY, kPadLThumb, kPadDPadUp}) key(b);
    return out;
}

// Logical keys (+ the right trigger) -> the actions held, bit (1 << Button). The authored commands:
//   A PressJump | Ascend; B TossGrenade | InteractCommand1 | Descend; X XButton (Interact + Reload, below);
//   Y NextWeapon; LeftThumbstick Transform; RightThumbstick Melee; RightTrigger Fire | WeaponMelee;
//   LeftShoulder Ability1 | SwitchSpectatorTarget 1; RightShoulder Ability0 | VehicleSpecialMove | SwitchSpectatorTarget -1;
//   DPad_Left TriggerKillstreak. (DPad_Down Focus has no action of its own yet; DPad_Right is the Voice Channel key.)
inline uint32_t padActions(uint32_t keys, bool rightTrigger) {
    uint32_t a = 0;
    auto on = [&a](Button b) { a |= 1u << (int)b; };
    if (keys & kPadA) { on(Button::Jump); on(Button::Ascend); }
    if (keys & kPadB) { on(Button::Grenade); on(Button::Descend); }
    if (keys & kPadX) { on(Button::Interact); on(Button::Reload); }   // XButton: tap = TryInteract, else ReloadWeapon (the Reload tap fires on
                                                                    // release < 0.3 s); hold 0.3 s = TryRevive / TryPickup (RE, C)
    if (keys & kPadY) on(Button::NextWeapon);
    if (keys & kPadLThumb) on(Button::Transform);
    if (keys & kPadRThumb) on(Button::Melee);
    if (rightTrigger) on(Button::Fire);
    if (keys & kPadLB) { on(Button::Ability1); on(Button::SpectateNext); }
    if (keys & kPadRB) { on(Button::Dash); on(Button::SpectatePrev); }
    if (keys & kPadDPadLeft) on(Button::Killstreak);
    return a;
}

}  // namespace platform
