#include "platform/UiBindings.h"

#include <fstream>
#include <sstream>

namespace platform {

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}

const char* UiBindings::actionName(UiKey k) {
    static const char* n[(int)UiKey::Count] = {"Up", "Down", "Left", "Right", "Accept", "Back", "X", "Y", "Start",
                                               "Select", "LB", "RB", "LT", "RT", "LThumb", "RThumb"};
    return (int)k >= 0 && k < UiKey::Count ? n[(int)k] : "";
}

UiBindings UiBindings::defaults(bool consoleStart) {
    UiBindings b;
    auto set = [&](UiKey k, std::vector<std::string> keys, std::vector<std::string> pad) {
        b.keys[(int)k] = std::move(keys);
        b.pad[(int)k] = std::move(pad);
    };
    set(UiKey::Up, {"Up"}, {"DPadUp", "LStickUp"});
    set(UiKey::Down, {"Down"}, {"DPadDown", "LStickDown"});
    set(UiKey::Left, {"Left"}, {"DPadLeft", "LStickLeft"});
    set(UiKey::Right, {"Right"}, {"DPadRight", "LStickRight"});
    set(UiKey::Accept, {"Enter"}, {"A"});
    set(UiKey::Back, {"Escape"}, {"B"});
    set(UiKey::X, {"F1"}, {"X"});
    set(UiKey::Y, {"F2"}, {"Y"});
    set(UiKey::Start, consoleStart ? std::vector<std::string>{"Space", "F3"} : std::vector<std::string>{"F3"}, {"Start"});
    // Select = pad Back (GFxUI.KeyMap F4) and, in a match, ShowScores (Back / Tab) [RE OVERNIGHT A7].
    set(UiKey::Select, {"F4", "Tab"}, {"Back"});
    set(UiKey::LB, {"PageUp"}, {"LB"});
    set(UiKey::RB, {"PageDown"}, {"RB"});
    set(UiKey::LT, {"Home"}, {"LT"});
    set(UiKey::RT, {"End"}, {"RT"});
    set(UiKey::LThumb, {"F5"}, {"LThumb"});
    set(UiKey::RThumb, {"F6"}, {"RThumb"});
    return b;
}

bool UiBindings::loadIni(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { section = line; continue; }
        if (section != "[UI]") continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string action = trim(line.substr(0, eq));
        bool pad = action.rfind("Pad.", 0) == 0;
        if (pad) action = action.substr(4);
        for (int i = 0; i < (int)UiKey::Count; ++i) {
            if (action != actionName((UiKey)i)) continue;
            std::vector<std::string>& dst = pad ? this->pad[i] : keys[i];
            dst.clear();
            std::stringstream ss(line.substr(eq + 1));
            std::string name;
            while (std::getline(ss, name, ',')) if (!trim(name).empty()) dst.push_back(trim(name));
        }
    }
    return true;
}

} // namespace platform
