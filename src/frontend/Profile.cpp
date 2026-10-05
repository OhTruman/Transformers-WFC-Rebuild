#include "frontend/Profile.h"
#include "assets/Json.h"
#include "core/Log.h"
#include "frontend/FlowTrace.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace frontend {

namespace {
// Every original profile field and its fresh-profile value: TransGame.Default__TnProfileSettings DefaultSettings +
// ProfileMappings (74 fields), exported by tools/frontend/export_profile_defaults.py [CONFIRMED ORIGINAL, authored
// data]. E.g. the campaign progress fields A1Difficulty ... D5Difficulty are -1 (not completed), which keeps the
// Extras movies that unlock with them locked on a fresh profile.
std::map<std::string, std::string> loadDefaults() {
    std::map<std::string, std::string> d;
    std::ifstream f(std::string(WFC_SOURCE_DIR) + "/data/frontend/profile_defaults.json", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    assets::Json j;
    if (assets::Json::parse(ss.str(), j))
        for (size_t i = 0; i < j["fields"].size(); ++i)
            d[j["fields"][i]["name"].asString()] = j["fields"][i]["default"].asString();
    if (d.empty()) {
        LOG_WARN("profile: data/frontend/profile_defaults.json missing; settings defaults only");
        d = {{"FX Volume", "80"}, {"Dialogue Volume", "80"}, {"Music Volume", "80"}, {"Subtitles", "False"},
             {"Controller Vibration", "True"}, {"UseAlternateControlScheme", "False"}, {"InvertY_Car", "False"},
             {"InvertY_Plane", "False"}, {"InvertY_Tank", "False"}, {"InvertY_Robot", "False"}, {"CameraSensitivity", "30"},
             {"GammaSetting", "50"}, {"HasAdjustedGamma", "False"}};
    }
    return d;
}
const std::map<std::string, std::string>& defaults() {
    static const std::map<std::string, std::string> d = loadDefaults();
    return d;
}
}

bool LocalProfile::isOriginalField(const std::string& field) { return defaults().count(field) > 0; }

std::string LocalProfile::playerName() const {
    if (!identityName.empty()) return identityName;
    if (const char* n = std::getenv("WFC_PLAYERNAME")) if (*n) return n;
    return "Player";
}

std::string LocalProfile::get(const std::string& field) const {
    auto it = values_.find(field);
    if (it != values_.end()) return it->second;
    auto d = defaults().find(field);
    return d == defaults().end() ? std::string() : d->second;
}

int LocalProfile::getInt(const std::string& field) const { return std::atoi(get(field).c_str()); }

void LocalProfile::set(const std::string& field, const std::string& value) {
    values_[field] = value;
    FlowTrace::emit("profile.set", {{"field", field}, {"value", value}, {"original", FlowTrace::boolean(isOriginalField(field))}});
}

void LocalProfile::load() {
    std::ifstream f(kFile);
    std::string line, section;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') { section = line; continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "HasWatchedIntroMovie") continue;   // written by earlier rebuilds; not a profile field in the original
        else if (section == "[Identity]" && k == "Name") identityName = v;
        else if (section == "[PCSettings]") {
            if (k == "Width") display.width = std::atoi(v.c_str());
            else if (k == "Height") display.height = std::atoi(v.c_str());
            else if (k == "Fullscreen") display.fullscreen = v == "1";
            else if (k == "TextureQuality") display.textureQuality = std::atoi(v.c_str());
            else if (k == "VSync") display.vsync = v == "1";
        } else if (section == "[ProfileData]") values_[k] = v;
    }
}

void LocalProfile::save() const {
    std::ofstream f(kFile);
    f << "; WFC rebuild local profile (TnProfileSettings role). Original fields under [ProfileData]; the PC SKU's\n"
         "; display settings under [PCSettings].\n";
    f << "\n[ProfileData]\n";
    for (const auto& [k, v] : values_) f << k << "=" << v << "\n";
    if (!identityName.empty()) f << "\n[Identity]\nName=" << identityName << "\n";
    f << "\n[PCSettings]\nWidth=" << display.width << "\nHeight=" << display.height << "\nFullscreen=" << (display.fullscreen ? 1 : 0)
      << "\nTextureQuality=" << display.textureQuality << "\nVSync=" << (display.vsync ? 1 : 0) << "\n";
}

} // namespace frontend
