#include "platform/Keyboard.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace platform {

namespace {
struct Named { uint16_t vk; const char* name; };
// Win32 virtual keys with their row names (letters, digits, F1-F24 and the numpad are computed).
const Named kNamed[] = {
    {0x04, "Mouse 3"}, {0x05, "Mouse 4"}, {0x06, "Mouse 5"},
    {0x08, "Backspace"}, {0x09, "Tab"}, {0x0D, "Enter"}, {0x13, "Pause"}, {0x14, "Caps Lock"}, {0x1B, "Esc"},
    {0x20, "Space"}, {0x21, "Page Up"}, {0x22, "Page Down"}, {0x23, "End"}, {0x24, "Home"},
    {0x25, "Left"}, {0x26, "Up"}, {0x27, "Right"}, {0x28, "Down"}, {0x2D, "Insert"}, {0x2E, "Delete"},
    {0x6A, "Numpad *"}, {0x6B, "Numpad +"}, {0x6D, "Numpad -"}, {0x6E, "Numpad ."}, {0x6F, "Numpad /"},
    {0xA0, "Left Shift"}, {0xA1, "Right Shift"}, {0xA2, "Left Ctrl"}, {0xA3, "Right Ctrl"}, {0xA4, "Left Alt"},
    {0xA5, "Right Alt"},
    {0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, "-"}, {0xBE, "."}, {0xBF, "/"}, {0xC0, "`"}, {0xDB, "["},
    {0xDC, "Backslash"}, {0xDD, "]"}, {0xDE, "'"},
};
}

std::string nameForVirtualKey(uint16_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x6F);          // VK_F1..VK_F24
    if (vk >= 0x60 && vk <= 0x69) return "Numpad " + std::to_string(vk - 0x60);    // VK_NUMPAD0..9
    for (const Named& n : kNamed) if (n.vk == vk) return n.name;
    return {};
}

uint16_t virtualKeyForName(const std::string& name) {
    if (name.size() == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9'))) return (uint16_t)name[0];
    for (uint16_t vk = 0x60; vk <= 0x87; ++vk)
        if (nameForVirtualKey(vk) == name) return vk;
    for (const Named& n : kNamed) if (name == n.name) return n.vk;
    return 0;
}

#ifdef _WIN32
namespace {
bool foreground() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    return fg && GetWindowThreadProcessId(fg, &pid) && pid == GetCurrentProcessId();
}
}
#endif

bool isKeyHeld(uint16_t vk) {
#ifdef _WIN32
    if (vk == 0 || !foreground()) return false;
    return (GetAsyncKeyState((int)vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

uint16_t firstKeyDown() {
#ifdef _WIN32
    if (!foreground()) return 0;
    for (int vk = 0x03; vk <= 0xFE; ++vk) {
        if (vk >= 0x10 && vk <= 0x12) continue;   // generic Shift / Ctrl / Alt: the left / right keys report instead
        if ((GetAsyncKeyState(vk) & 0x8000) && !nameForVirtualKey((uint16_t)vk).empty()) return (uint16_t)vk;
    }
#endif
    return 0;
}

} // namespace platform
