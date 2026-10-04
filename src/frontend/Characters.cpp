#include "frontend/Characters.h"
#include "assets/Json.h"
#include "core/Log.h"

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
    LOG_INFO("frontend roster: %zu chassis, %zu default characters", chassis_.size(), presets_.size());
    return !presets_.empty();
}

const CharacterPreset* CharacterRoster::find(const std::string& name) const {
    for (const CharacterPreset& p : presets_) if (p.name == name) return &p;
    return nullptr;
}

const ChassisInfo* CharacterRoster::chassis(const std::string& id) const {
    auto it = chassis_.find(id);
    return it == chassis_.end() ? nullptr : &it->second;
}

} // namespace frontend
