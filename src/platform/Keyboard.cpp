#include "platform/Keyboard.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace platform {

uint16_t virtualKeyForName(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return (uint16_t)name[0];
    if (name == "Caps Lock") return 0x14;   // VK_CAPITAL
    if (name == "Mouse 4") return 0x05;     // VK_XBUTTON1
    if (name == "Mouse 5") return 0x06;     // VK_XBUTTON2
    return 0;
}

bool isKeyHeld(uint16_t vk) {
#ifdef _WIN32
    if (vk == 0) return false;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg || !GetWindowThreadProcessId(fg, &pid) || pid != GetCurrentProcessId()) return false;
    return (GetAsyncKeyState((int)vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

} // namespace platform
