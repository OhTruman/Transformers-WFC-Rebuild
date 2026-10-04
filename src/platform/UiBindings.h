// Clean-room reconstruction — logical UI commands (UiKey) bound to device inputs, data-driven so the PC build can
// remap them. Platform-neutral: keys and pad buttons are named; each platform resolves the names.
//
// Provenance: the logical commands are the movies' KeyListener contract (navigate*, buttonA/B/X/Y, buttonStart / Back,
// LB/RB/LT/RT, clickL/RStick) [CONFIRMED AS2]. The pad bindings follow the Xbox 360 controller [CONFIRMED]. The keyboard
// bindings are a PC adaptation, except Enter = buttonA and Escape = buttonB, which are the key codes the shipped
// KeyListener itself maps (13 / 27) [CONFIRMED AS2]. Overrides: wfc_input.ini, section [UI], e.g. "Accept=Enter,Space".
#pragma once
#include <string>
#include <vector>

#include "platform/Input.h"

namespace platform {

struct UiBindings {
    std::vector<std::string> keys[(int)UiKey::Count];   // key names: "Enter", "Escape", "Up", "F3", "PageUp", "A".."Z", ...
    std::vector<std::string> pad[(int)UiKey::Count];    // pad names: "A", "B", "X", "Y", "Start", "Back", "DPadUp", ...

    static UiBindings defaults(bool consoleStart);      // consoleStart: also bind Space to Start (console presentation)
    // Applies "Action=Name,Name" lines of an ini file's [UI] section; returns false when the file is absent.
    bool loadIni(const std::string& path);
    static const char* actionName(UiKey k);
};

} // namespace platform
