#include "frontend/Characters.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace frontend {

bool CharacterRoster::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { LOG_WARN("frontend roster: %s missing (character selection has no characters)", path.c_str()); return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    assets::Json j;
    if (!assets::Json::parse(ss.str(), j)) return false;
    for (const auto& [id, c] : j["chassis"].obj) {
        ChassisInfo ci;
        ci.id = id;
        ci.displayName = c["display"]["iconic"]["INT"].asString();
        ci.specialty = c["class"].asString();
        ci.availability = c["availability"].asString();
        const std::string& fac = c["faction"].asString();
        ci.faction = fac == "Autobot" ? 0 : fac == "Decepticon" ? 1 : 3;
        ci.lockedChassis = c["locks"]["LockedChassis"].asBool();
        ci.lockedCharacter = c["locks"]["LockedCharacter"].asBool();
        ci.robotGltf = c["robot"]["gltf"].asString();
        ci.vehicleGltf = c["vehicle"]["gltf"].asString();
        chassis_[id] = ci;
    }
    // Fresh profile: one custom character per specialty, SpecialtyClasses order.
    const assets::Json& classes = j["default_four_classes (MP presets)"];
    for (const char* spec : {"Scout", "Scientist", "Soldier", "Leader"}) {
        const assets::Json& c = classes[spec];
        if (!c.isObject()) continue;
        CharacterPreset p;
        p.name = spec;
        p.specialty = spec;
        p.chassis[0] = c["Autobot"].asString();
        p.chassis[1] = c["Decepticon"].asString();
        for (int fct = 0; fct < 2; ++fct) p.iconicNames[fct] = c["names"][(size_t)fct].asString();
        auto list = [](const assets::Json& a, std::vector<std::string>& out) { for (size_t i = 0; i < a.size(); ++i) out.push_back(a[i].asString()); };
        list(c["weapons"], p.weapons);
        list(c["vehicle_weapons"], p.vehicleWeapons);
        list(c["melee"], p.melee);
        list(c["abilities"], p.abilities);
        presets_.push_back(p);
    }
    characters_ = presets_;
    LOG_INFO("frontend roster: %zu chassis, %zu default characters", chassis_.size(), presets_.size());
    return !presets_.empty();
}

void CharacterRoster::loadAuthored(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    assets::Json j;
    if (!assets::Json::parse(ss.str(), j)) { LOG_WARN("frontend roster: %s missing (no authored colours)", path.c_str()); return; }
    auto color = [](const assets::Json& col, const assets::Json& pal, const assets::Json& xy, CharacterColor& out) {
        out.r = (int)col["R"].asDouble(); out.g = (int)col["G"].asDouble(); out.b = (int)col["B"].asDouble();
        out.a = col["A"].isNumber() ? (int)col["A"].asDouble() : 255;
        out.palette = (int)pal.asDouble();
        out.x = (float)xy["X"].asDouble(); out.y = (float)xy["Y"].asDouble();
    };
    for (CharacterPreset& p : presets_) {
        const assets::Json& d = j["presets"][p.name + "_PCD_MP"];
        if (!d.isObject()) continue;
        for (size_t fct = 0; fct < 2; ++fct) {
            color(d["PrimaryColors"][fct], d["PrimaryColorPalettes"][fct], d["PrimaryColorCoords"][fct], p.primary[fct]);
            color(d["SecondaryColors"][fct], d["SecondaryColorPalettes"][fct], d["SecondaryColorCoords"][fct], p.secondary[fct]);
        }
        p.skills.clear();
        for (size_t i = 0; i < d["Skills"].size(); ++i) p.skills.push_back(d["Skills"][i].asString());
        if (d["MeleeWeapons"].size()) {
            p.melee.clear();
            for (size_t i = 0; i < d["MeleeWeapons"].size(); ++i) p.melee.push_back(d["MeleeWeapons"][i].asString());
        }
    }
    characters_ = presets_;
}

const CharacterPreset* CharacterRoster::find(const std::string& name) const {
    for (const CharacterPreset& p : characters_) if (p.name == name) return &p;
    return nullptr;
}

CharacterPreset* CharacterRoster::findMutable(const std::string& name) {
    for (CharacterPreset& p : characters_) if (p.name == name) return &p;
    return nullptr;
}

void CharacterRoster::reset(const std::string& name) {
    for (size_t i = 0; i < characters_.size() && i < presets_.size(); ++i)
        if (characters_[i].name == name) { characters_[i] = presets_[i]; return; }
}

namespace {
std::string joinList(const std::vector<std::string>& v) {
    std::string o;
    for (const auto& s : v) o += (o.empty() ? "" : ",") + s;
    return o;
}
std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!s.empty()) out.push_back(cur);
    return out;
}
std::string colorText(const CharacterColor& c) {
    char b[96];
    std::snprintf(b, sizeof b, "%d.%d.%d.%d;%d;%g;%g", c.r, c.g, c.b, c.a, c.palette, c.x, c.y);
    return b;
}
bool parseColorText(const std::string& s, CharacterColor& c) {
    return std::sscanf(s.c_str(), "%d.%d.%d.%d;%d;%f;%f", &c.r, &c.g, &c.b, &c.a, &c.palette, &c.x, &c.y) == 7;
}
}

bool CharacterRoster::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "; WFC rebuild custom characters (TnCharacterCustomizationData role; PC original: WriteCustomizationFile).\n";
    for (const CharacterPreset& p : characters_) {
        f << "\n[Character " << p.name << "]\nFriendlyName=" << p.friendlyName << "\nSpecialty=" << p.specialty
          << "\nChassisTypes=" << p.chassis[0] << "," << p.chassis[1] << "\nWeaponTypes=" << joinList(p.weapons)
          << "\nAbilities=" << joinList(p.abilities) << "\nVehicleWeapons=" << joinList(p.vehicleWeapons)
          << "\nSkills=" << joinList(p.skills) << "\n";
        for (int fct = 0; fct < 2; ++fct)
            f << "Primary" << fct << "=" << colorText(p.primary[fct]) << "\nSecondary" << fct << "=" << colorText(p.secondary[fct]) << "\n";
    }
    return true;
}

void CharacterRoster::loadSaved(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    CharacterPreset* cur = nullptr;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("[Character ", 0) == 0 && !line.empty() && line.back() == ']') { cur = findMutable(line.substr(11, line.size() - 12)); continue; }
        size_t eq = line.find('=');
        if (!cur || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "FriendlyName") cur->friendlyName = v;
        else if (k == "Specialty" && !v.empty()) cur->specialty = v;
        else if (k == "ChassisTypes") { auto c = splitList(v); if (c.size() == 2) { cur->chassis[0] = c[0]; cur->chassis[1] = c[1]; } }
        else if (k == "WeaponTypes") cur->weapons = splitList(v);
        else if (k == "Abilities") cur->abilities = splitList(v);
        else if (k == "VehicleWeapons") cur->vehicleWeapons = splitList(v);
        else if (k == "Skills") cur->skills = splitList(v);
        else if (k == "Primary0") parseColorText(v, cur->primary[0]);
        else if (k == "Primary1") parseColorText(v, cur->primary[1]);
        else if (k == "Secondary0") parseColorText(v, cur->secondary[0]);
        else if (k == "Secondary1") parseColorText(v, cur->secondary[1]);
    }
}

const ChassisInfo* CharacterRoster::chassis(const std::string& id) const {
    auto it = chassis_.find(id);
    return it == chassis_.end() ? nullptr : &it->second;
}

} // namespace frontend
