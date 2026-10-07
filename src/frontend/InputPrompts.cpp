// Clean-room reconstruction - see InputPrompts.h.
#include "frontend/InputPrompts.h"

#include <cmath>
#include <fstream>

namespace frontend {

namespace {
// The quoted value of name="..." in a struct-literal line ("" if absent).
std::string field(const std::string& line, const std::string& name) {
    const std::string k = name + "=\"";
    size_t at = 0;
    while ((at = line.find(k, at)) != std::string::npos) {
        if (at == 0 || line[at - 1] == '(' || line[at - 1] == ',' || line[at - 1] == ' ') {
            const size_t b = at + k.size(), e = line.find('"', b);
            return e == std::string::npos ? std::string() : line.substr(b, e - b);
        }
        at += k.size();
    }
    return {};
}
// The lines of one [section] of an ini / int file.
std::vector<std::string> sectionLines(const std::string& path, const std::string& section) {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string line;
    bool in = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') { in = line == "[" + section + "]"; continue; }
        if (in) out.push_back(line);
    }
    return out;
}
std::map<std::string, std::string> mapperSection(const std::string& path) {
    std::map<std::string, std::string> m;
    for (const std::string& l : sectionLines(path, "TnInputCommandToBindingMapper"))
        if (l.rfind("KeyToActionScriptBindings", 0) == 0) m[field(l, "Key")] = field(l, "ActionScript");
    return m;
}
bool tokenChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
}  // namespace

const std::map<std::string, std::string>& InputPrompts::pcCommandKeys() {
    // The rebuild's keyboard bindings (Win32Window vkFor) in the PC mapper's key names [PC ADAPTATION]: G grenade,
    // Shift Ability0 / dash (CONF "Ability0 | VehicleSpecialMove"), Ctrl Ability1, R reload, Space jump, F transform,
    // mouse buttons fire / fine aim, Q melee, C / V hover up / down, B kill streak / look at, E interact / pick up.
    static const std::map<std::string, std::string> t = {
        {"^SWITCH_WEAPON", "MouseWheel/PageUp/PageDown"}, {"^THROW_GRENADE", "G"}, {"^USE_ABILITY_0", "Shift"},
        {"^USE_ABILITY_1", "Control"}, {"^RELOAD_WEAPON", "R"}, {"^SPEED_BOOST", "RightMouseButton"}, {"^JUMP", "Space"},
        {"^TRANSFORM", "F"}, {"^MOVE", "Move"}, {"^TURN", "Turn"}, {"^SHOOT", "LeftMouseButton"},
        {"^LOOK_AROUND", "LookAround"}, {"^FINE_AIM", "RightMouseButton"}, {"^MELEE", "Q/MiddleMouseButton"},
        {"^BARRELL_ROLL_DIRECTION", "BarrelRollDirection"}, {"^BARRELL_ROLL_START", "Shift"}, {"^HOVER_UP", "C"},
        {"^HOVER_DOWN", "V"}, {"^POINT_OF_INTEREST", "B"}, {"^USE_INTERACT", "E"}, {"^EXIT_TURRET", "E"},
        {"^DETATCH_TURRET", "G"}, {"^PICKUP", "E"}, {"^REVIVE", "E"}, {"^ADD_TO_GENERATOR", "E"}, {"^USE_KILL_STREAK", "B"}};
    return t;
}

bool InputPrompts::load(const std::string& root) {
    padKeys_.clear();
    for (const std::string& l : sectionLines(root + "/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/Xe-TransInput.ini",
                                             "TransGame.TnInputCommandToBindingMapper"))
        if (l.rfind("InputCommandToKeyBindings", 0) == 0) padKeys_[field(l, "InputCommand")] = field(l, "Key");
    const std::string loc = root + "/config/Coalesced_int/TransGame/Localization/INT/";
    padText_ = mapperSection(loc + "TransGame.int");
    pcText_ = mapperSection(loc + "TransGame_PC.int");
    return !padKeys_.empty() && !padText_.empty() && !pcText_.empty();
}

void InputPrompts::setTables(std::map<std::string, std::string> padKeys, std::map<std::string, std::string> padText,
                             std::map<std::string, std::string> pcText) {
    padKeys_ = std::move(padKeys); padText_ = std::move(padText); pcText_ = std::move(pcText);
}

std::string InputPrompts::pcKeyText(const std::string& key) const {
    auto it = pcText_.find(key);
    return it == pcText_.end() ? std::string() : it->second;
}

std::string InputPrompts::translate(const std::string& text, bool pad) const {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '^' || i + 1 >= text.size() || !tokenChar(text[i + 1])) { out += text[i++]; continue; }
        size_t e = i + 1;
        while (e < text.size() && tokenChar(text[e])) ++e;
        const std::string token = text.substr(i, e - i);
        std::string repl;
        if (pad) {
            auto k = padKeys_.find(token);
            if (k != padKeys_.end()) { auto t = padText_.find(k->second); if (t != padText_.end()) repl = t->second; }
        } else {
            auto k = pcCommandKeys().find(token);
            if (k != pcCommandKeys().end()) repl = pcKeyText(k->second);
        }
        out += repl.empty() ? token : repl;   // unbound: the token stays (as the original)
        i = e;
    }
    return out;
}

void InputDevice::update(bool padActive, bool keyActive, float dx, float dy, float dt) {
    if (padActive) { pad_ = true; travel_ = 0.0f; window_ = 0.0f; return; }
    if (keyActive) { pad_ = false; travel_ = 0.0f; window_ = 0.0f; return; }
    if (!pad_) return;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d <= 0.0f) {
        window_ += dt;
        if (window_ > kMouseWindow) { travel_ = 0.0f; window_ = 0.0f; }
        return;
    }
    if (window_ > kMouseWindow) { travel_ = 0.0f; window_ = 0.0f; }
    travel_ += d;
    window_ += dt;
    if (travel_ >= kMouseTravel) { pad_ = false; travel_ = 0.0f; window_ = 0.0f; }
}

}  // namespace frontend
