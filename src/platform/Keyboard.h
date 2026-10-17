// Held-key state for one virtual key (PC EXTENSION): voice chat push-to-talk (ONLINE_MP_FLOWS.md 3.12). The input frame
// carries key presses only; push-to-talk needs "held now", and only while this game's window is in front.
#pragma once
#include <cstdint>
#include <string>

namespace platform {

// True while the Win32 virtual key is held and a window of this process is the foreground window.
bool isKeyHeld(uint16_t vk);
// A key's row name ("V", "F1", "Caps Lock", "Left Shift", "Mouse 4", "Numpad 5", ...) -> the virtual key (0 = unknown).
uint16_t virtualKeyForName(const std::string& name);
// The reverse, for the push-to-talk key capture; "" for keys push-to-talk does not take (left / right mouse button).
std::string nameForVirtualKey(uint16_t vk);
// The first held key push-to-talk can take (keyboard keys, mouse buttons 3-5), 0 = none; only while a window of this
// process is in front.
uint16_t firstKeyDown();

} // namespace platform
