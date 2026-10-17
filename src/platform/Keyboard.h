// Held-key state for one virtual key (PC EXTENSION): voice chat push-to-talk (ONLINE_MP_FLOWS.md 3.12). The input frame
// carries key presses only; push-to-talk needs "held now", and only while this game's window is in front.
#pragma once
#include <cstdint>
#include <string>

namespace platform {

// True while the Win32 virtual key is held and a window of this process is the foreground window.
bool isKeyHeld(uint16_t vk);
// "V" / "T" / "B" / "Caps Lock" / "Mouse 4" / "Mouse 5" -> the virtual key (0 = unknown).
uint16_t virtualKeyForName(const std::string& name);

} // namespace platform
