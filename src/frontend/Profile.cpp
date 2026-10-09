#include "frontend/Profile.h"

#include <algorithm>
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
    if (!loggedInAccount.empty()) return loggedInAccount;
    if (!identityName.empty()) return identityName;
    static const char* const envName = std::getenv("WFC_PLAYERNAME");   // read once (a getenv per collection row before)
    if (envName && *envName) return envName;
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
    if (loadFrom(f)) save();
}

bool LocalProfile::loadFrom(std::istream& f) {
    std::string line, section;
    bool factionKeys = false, oldKeys = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') { section = line; continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "HasWatchedIntroMovie") continue;   // written by earlier rebuilds; not a profile field in the original
        else if (section == "[Identity]" && k == "Name") identityName = v;
        else if (section == "[Accounts]" && k == "Account" && !v.empty()) accounts.push_back(v);
        else if (section == "[Accounts]" && k == "SignedIn") loggedInAccount = v;
        else if (section == "[PCSettings]") {
            pcSettingsLoaded = true;
            if (k == "AutoDetectGpu") autoDetectGpu = v;
            else if (k == "Width") display.width = std::atoi(v.c_str());
            else if (k == "Height") display.height = std::atoi(v.c_str());
            else if (k == "Fullscreen") display.fullscreen = v == "1";
            else if (k == "TextureQuality") display.textureQuality = std::atoi(v.c_str());
            else if (k == "VSync") display.vsync = v == "1";
            else if (k == "FrameLimit") display.frameLimit = std::max(0, std::atoi(v.c_str()));
            else if (k == "Upscaling") display.upscaling = std::clamp(std::atoi(v.c_str()), 0, 3);
            else if (k == "HDTextures") display.hdTextures = v == "1";
            else if (k == "Anisotropy") display.anisotropy = Display::clampAnisotropy(std::atoi(v.c_str()));
            else if (k == "FrameGeneration") display.frameGeneration = std::max(0, std::atoi(v.c_str()));
            else if (k == "RayTracing") display.rayTracing = v == "1";
            else if (k == "BotsFriendly") { bots.friendly = std::max(0, std::atoi(v.c_str())); oldKeys = true; }
            else if (k == "BotsEnemy") { bots.enemy = std::max(0, std::atoi(v.c_str())); oldKeys = true; }
            else if (k == "BotDifficulty") bots.difficulty = std::clamp(std::atoi(v.c_str()), 0, 3);   // 3 = Expert (PC EXTENSION)
            else if (k == "BotAI") bots.ai = v == "Smart" ? 1 : v == "Classic" ? 0 : -1;
            else if (k == "BotsAutobot") { bots.autobot = std::max(0, std::atoi(v.c_str())); factionKeys = true; }
            else if (k == "BotsDecepticon") { bots.decepticon = std::max(0, std::atoi(v.c_str())); factionKeys = true; }
            else if (k == "BotsExtended") bots.extended = v == "1";
            else if (k == "BotsEdited") bots.editedSinceMap = v == "1";
            else if (k == "OriginalChassisLocks") originalChassisLocks = v == "1";
        } else if (section == "[Progression]") {
            const int sp = progression::specialtyIndex(k.size() > 2 && k.rfind("Xp", 0) == 0 ? k.substr(2) : std::string());
            const int lm = progression::specialtyIndex(k.size() > 9 && k.rfind("LastMatch", 0) == 0 ? k.substr(9) : std::string());
            if (sp >= 0) progression.xp[(size_t)sp] = std::clamp(std::atol(v.c_str()), 0L, progression::kXpCap);
            else if (lm >= 0) progression.lastMatchXp[(size_t)lm] = std::max(0L, std::atol(v.c_str()));
            else if (k == "Prime") progression.prime = v == "1";
            else if (k == "NewlyUnlocked") {
                progression.newlyUnlocked.clear();
                std::stringstream ss(v);
                for (std::string item; std::getline(ss, item, ',');) if (!item.empty()) progression.newlyUnlocked.push_back(item);
            }
            else if (k.rfind("Tier.", 0) == 0) progression.tiers[std::atoi(k.c_str() + 5)] = std::clamp(std::atoi(v.c_str()), 0, 3);
            else if (k.rfind("Stat.", 0) == 0) progression.stats[std::atoi(k.c_str() + 5)] = std::atol(v.c_str());
        } else if (section == "[ProfileData]") values_[k] = v;
    }
    // Profiles saved before the faction bot counts (09b, 8c2b6e3) hold only BotsFriendly / BotsEnemy: the team-mode
    // Bot Settings would read 0 / 0. Migrated once, as 09b launched them (no faction is known before the match: the
    // human on the Autobot side), within the original 5 v 5 limits (the human's side 4 bots, the other 5).
    if (oldKeys && !factionKeys) {
        bots.autobot = std::min(bots.friendly, 4);
        bots.decepticon = std::min(bots.enemy, 5);
        return true;
    }
    return false;
}

void LocalProfile::save() const {
    std::ofstream f(kFile);
    f << "; WFC rebuild local profile (TnProfileSettings role). Original fields under [ProfileData]; the PC SKU's\n"
         "; display settings under [PCSettings].\n";
    f << "\n[ProfileData]\n";
    for (const auto& [k, v] : values_) f << k << "=" << v << "\n";
    if (!identityName.empty()) f << "\n[Identity]\nName=" << identityName << "\n";
    if (!accounts.empty()) {
        f << "\n[Accounts]\n";
        for (const std::string& a : accounts) f << "Account=" << a << "\n";
        if (!loggedInAccount.empty()) f << "SignedIn=" << loggedInAccount << "\n";
    }
    f << "\n[PCSettings]\nWidth=" << display.width << "\nHeight=" << display.height << "\nFullscreen=" << (display.fullscreen ? 1 : 0)
      << "\nTextureQuality=" << display.textureQuality << "\nVSync=" << (display.vsync ? 1 : 0)
      << "\nFrameLimit=" << display.frameLimit << "\nUpscaling=" << display.upscaling << "\nHDTextures=" << (display.hdTextures ? 1 : 0)
      << "\nAnisotropy=" << display.anisotropy
      << "\nFrameGeneration=" << display.frameGeneration << "\nRayTracing=" << (display.rayTracing ? 1 : 0) << "\nBotsFriendly=" << bots.friendly << "\nBotsEnemy=" << bots.enemy
      << "\nBotDifficulty=" << bots.difficulty << "\nBotsAutobot=" << bots.autobot << "\nBotsDecepticon=" << bots.decepticon
      << "\nAutoDetectGpu=" << autoDetectGpu << (bots.ai >= 0 ? std::string("\nBotAI=") + (bots.ai ? "Smart" : "Classic") : std::string())
      << "\nBotsExtended=" << (bots.extended ? 1 : 0) << "\nBotsEdited=" << (bots.editedSinceMap ? 1 : 0) << "\nOriginalChassisLocks=" << (originalChassisLocks ? 1 : 0) << "\n";
    f << "\n[Progression]\n";
    for (int i = 0; i < 4; ++i) f << "Xp" << progression::specialtyName(i) << "=" << progression.xp[(size_t)i] << "\n";
    for (int i = 0; i < 4; ++i) f << "LastMatch" << progression::specialtyName(i) << "=" << progression.lastMatchXp[(size_t)i] << "\n";
    f << "Prime=" << (progression.prime ? 1 : 0) << "\n";
    if (!progression.newlyUnlocked.empty()) {
        f << "NewlyUnlocked=";
        for (size_t i = 0; i < progression.newlyUnlocked.size(); ++i) f << (i ? "," : "") << progression.newlyUnlocked[i];
        f << "\n";
    }
    for (const auto& [id, t] : progression.tiers) if (t > 0) f << "Tier." << id << "=" << t << "\n";
    for (const auto& [id, v] : progression.stats) if (v != 0) f << "Stat." << id << "=" << v << "\n";
}

} // namespace frontend
