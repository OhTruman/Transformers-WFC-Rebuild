#include "frontend/Profile.h"

#include <algorithm>
#include "assets/Json.h"
#include "core/Log.h"
#include "frontend/DataPaths.h"
#include "frontend/FlowTrace.h"
#include "frontend/OpenPath.h"

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
    std::ifstream f(openPath(frontendDataFile("profile_defaults.json")), std::ios::binary);
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
    std::ifstream f(core::userFilePath(file_.c_str()));
    if (loadFrom(f)) save();
}

bool LocalProfile::loadFrom(std::istream& f) {
    std::string line, section;
    bool factionKeys = false, oldKeys = false, hdUserSetKey = false;
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
            else if (k == "Upscaling") display.upscaling = std::clamp(std::atoi(v.c_str()), 0, Display::kUpscalingMax);
            else if (k == "HDTextures") display.hdTextures = std::clamp(std::atoi(v.c_str()), 0, Display::kHdTexturesMax);
            else if (k == "Anisotropy") display.anisotropy = Display::clampAnisotropy(std::atoi(v.c_str()));
            else if (k == "FrameGeneration") display.frameGeneration = std::atoi(v.c_str()) != 0 ? 1 : 0;
            else if (k == "ShowFps") display.showFps = v == "1";
            else if (k == "VolumetricLighting") display.volumetricLighting = v != "0";
            else if (k == "LensFlares") display.lensFlares = std::clamp(std::atoi(v.c_str()), 0, 2);
            else if (k == "Language" && !v.empty()) display.language = v;
            else if (k == "SplitLayout") display.splitLayout = std::clamp(std::atoi(v.c_str()), 1, 2);
            else if (k == "Smoke") display.smoke = std::clamp(std::atoi(v.c_str()), 0, 1);
            else if (k == "Water") display.water = std::clamp(std::atoi(v.c_str()), 0, 1);
            else if (k == "RayTracing") display.rayTracing = std::clamp(std::atoi(v.c_str()), 0, Display::kRayTracingMax);
            else if (k == "HDTexturesUserSet") { display.hdTexturesUserSet = v == "1"; hdUserSetKey = true; }
            else if (k == "HDPackHintShown") display.hdPackHintShown = v == "1";
            else if (k == "BotsFriendly") { bots.friendly = std::max(0, std::atoi(v.c_str())); oldKeys = true; }
            else if (k == "BotsEnemy") { bots.enemy = std::max(0, std::atoi(v.c_str())); oldKeys = true; }
            else if (k == "BotDifficulty") bots.difficulty = std::clamp(std::atoi(v.c_str()), 0, 3);   // 3 = Expert (PC EXTENSION)
            else if (k == "BotsAutobot") { bots.autobot = std::max(0, std::atoi(v.c_str())); factionKeys = true; }
            else if (k == "BotsDecepticon") { bots.decepticon = std::max(0, std::atoi(v.c_str())); factionKeys = true; }
            else if (k == "BotsExtended") bots.extended = v == "1";
            else if (k == "BotsEdited") bots.editedSinceMap = v == "1";
            else if (k == "IconicCharacters") bots.iconic = v == "1";
            else if (k == "IdleKick") bots.idleKick = v == "1";
            else if (k == "OriginalChassisLocks") originalChassisLocks = v == "1";
        } else if (section == "[Voice]") {
            if (k == "Mode") voice.mode = std::clamp(std::atoi(v.c_str()), 0, 2);
            else if (k == "Audience") voice.audience = std::clamp(std::atoi(v.c_str()), 0, 3);
            else if (k == "Device") voice.device = v;
            else if (k == "PushToTalkKey") voice.pttKey = v;
            else if (k == "VoiceChannelKey" && !v.empty()) voice.channelKey = v;
            else if (k == "Volume") voice.volume = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "ProximityVolume") voice.proximityVolume = std::clamp(std::atoi(v.c_str()), 0, 100);
            else if (k == "Muted" || k == "Blocked") {
                std::vector<std::string>& list = k == "Muted" ? voice.muted : voice.blocked;
                list.clear();
                std::stringstream ss(v);
                for (std::string item; std::getline(ss, item, ',');) if (!item.empty()) list.push_back(item);
            }
        } else if (section == "[Lobby]") {
            if (k == "LastMode") lobbyLastMode = v;
        } else if (section == "[RecentPlayers]") {
            if (k == "Player") {   // fingerprint|lastSeen|name
                const size_t a = v.find('|'), b = a == std::string::npos ? a : v.find('|', a + 1);
                if (b != std::string::npos && recentPlayers.size() < 50)
                    recentPlayers.push_back({v.substr(0, a), v.substr(b + 1), std::atoll(v.substr(a + 1, b - a - 1).c_str())});
            }
        } else if (section == "[Online]") {
            if (k == "FirstRunShown") onlineFirstRunShown = v == "1";
            else if (k == "LobbyService") lobbyService = v;
        } else if (section == "[Campaign]") {
            auto ints = [&](int* out, int n) {
                std::stringstream ss(v); std::string t; int i = 0;
                while (i < n && std::getline(ss, t, ',')) out[i++] = std::atoi(t.c_str());
            };
            int tmp[Campaign::kChapters] = {};
            if (k == "Difficulty") campaign.difficulty = std::clamp(std::atoi(v.c_str()), 0, 2);
            else if (k == "Available") { ints(tmp, Campaign::kChapters); for (int i = 0; i < Campaign::kChapters; ++i) campaign.available[i] = tmp[i] != 0; }
            else if (k == "Complete") { ints(tmp, Campaign::kChapters); for (int i = 0; i < Campaign::kChapters; ++i) campaign.complete[i] = tmp[i] != 0; }
            else if (k == "BestDifficulty") ints(campaign.bestDifficulty, Campaign::kChapters);
            else if (k == "Checkpoint") ints(campaign.checkpoint, Campaign::kChapters);
            else if (k == "Resume") ints(campaign.resumeMap, 2);
        } else if (section == "[Tutorials]") {
            if (k == "Shown") {
                tutorialsShown.clear();
                size_t a = 0;
                while (a <= v.size()) {
                    size_t b = v.find(',', a);
                    if (b == std::string::npos) b = v.size();
                    if (b > a) tutorialsShown.push_back(v.substr(a, b - a));
                    a = b + 1;
                }
            }
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
    // Profiles saved before HDTexturesUserSet (2026-10-12): an HD level that is on was the user's own choice (the row
    // defaulted Off then); written with the key at the next save.
    if (!hdUserSetKey && display.hdTextures > 0) display.hdTexturesUserSet = true;
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
    std::ofstream f(core::userFilePath(file_.c_str()));
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
      << "\nFrameLimit=" << display.frameLimit << "\nUpscaling=" << display.upscaling << "\nHDTextures=" << display.hdTextures
      << "\nHDTexturesUserSet=" << (display.hdTexturesUserSet ? 1 : 0) << "\nHDPackHintShown=" << (display.hdPackHintShown ? 1 : 0)
      << "\nAnisotropy=" << display.anisotropy
      << "\nFrameGeneration=" << display.frameGeneration << "\nRayTracing=" << display.rayTracing
      << "\nShowFps=" << (display.showFps ? 1 : 0) << "\nVolumetricLighting=" << (display.volumetricLighting ? 1 : 0)
      << (display.language != "INT" ? "\nLanguage=" + display.language : std::string())   // only when not the default
      << (display.splitLayout != 1 ? "\nSplitLayout=" + std::to_string(display.splitLayout) : std::string())
      << (display.lensFlares != 2 ? "\nLensFlares=" + std::to_string(display.lensFlares) : std::string())   // only when not the default
      << (display.smoke != 0 ? "\nSmoke=" + std::to_string(display.smoke) : std::string())
      << (display.water != 0 ? "\nWater=" + std::to_string(display.water) : std::string())
      << "\nBotsFriendly=" << bots.friendly << "\nBotsEnemy=" << bots.enemy
      << "\nBotDifficulty=" << bots.difficulty << "\nBotsAutobot=" << bots.autobot << "\nBotsDecepticon=" << bots.decepticon
      << "\nAutoDetectGpu=" << autoDetectGpu
      << "\nBotsExtended=" << (bots.extended ? 1 : 0) << "\nBotsEdited=" << (bots.editedSinceMap ? 1 : 0) << "\nOriginalChassisLocks=" << (originalChassisLocks ? 1 : 0) << "\n";
    if (bots.iconic) f << "IconicCharacters=1\n";   // only when on: existing profiles stay byte-identical
    if (bots.idleKick) f << "IdleKick=1\n";
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
    if (!voice.isDefault()) {   // voice chat settings / mutes: only when changed (offline profiles unchanged)
        f << "\n[Voice]\nMode=" << voice.mode << "\nAudience=" << voice.audience << "\nDevice=" << voice.device
          << "\nPushToTalkKey=" << voice.pttKey << "\nVoiceChannelKey=" << voice.channelKey << "\nVolume=" << voice.volume << "\nProximityVolume=" << voice.proximityVolume << "\nMuted=";
        for (size_t i = 0; i < voice.muted.size(); ++i) f << (i ? "," : "") << voice.muted[i];
        f << "\n";
        if (!voice.blocked.empty()) {
            f << "Blocked=";
            for (size_t i = 0; i < voice.blocked.size(); ++i) f << (i ? "," : "") << voice.blocked[i];
            f << "\n";
        }
    }
    if (onlineFirstRunShown || !lobbyService.empty()) {   // only when set (offline profiles unchanged)
        f << "\n[Online]\n";
        if (onlineFirstRunShown) f << "FirstRunShown=1\n";
        if (!lobbyService.empty()) f << "LobbyService=" << lobbyService << "\n";
    }
    if (!recentPlayers.empty()) {
        f << "\n[RecentPlayers]\n";
        for (const RecentPlayer& r : recentPlayers) f << "Player=" << r.fingerprint << "|" << r.lastSeen << "|" << r.name << "\n";
    }
    if (!lobbyLastMode.empty()) f << "\n[Lobby]\nLastMode=" << lobbyLastMode << "\n";   // only once a mode was chosen   // only when set (offline profiles unchanged)
    if (!campaign.isDefault()) {   // only once the campaign changed something (versus profiles unchanged)
        auto list = [&](const auto* a, int n) { for (int i = 0; i < n; ++i) f << (i ? "," : "") << (int)a[i]; f << "\n"; };
        f << "\n[Campaign]\nDifficulty=" << campaign.difficulty << "\nAvailable=";
        list(campaign.available, Campaign::kChapters);
        f << "Complete="; list(campaign.complete, Campaign::kChapters);
        f << "BestDifficulty="; list(campaign.bestDifficulty, Campaign::kChapters);
        f << "Checkpoint="; list(campaign.checkpoint, Campaign::kChapters);
        f << "Resume="; list(campaign.resumeMap, 2);
    }
    if (!tutorialsShown.empty()) {   // CAMPAIGN_HUD_PLAN.md E5: only when not empty (versus profiles unchanged)
        f << "\n[Tutorials]\nShown=";
        for (size_t i = 0; i < tutorialsShown.size(); ++i) f << (i ? "," : "") << tutorialsShown[i];
        f << "\n";
    }
}

bool LocalProfile::Campaign::isDefault() const {
    const Campaign d;
    if (difficulty != d.difficulty || resumeMap[0] != -1 || resumeMap[1] != -1) return false;
    for (int i = 0; i < kChapters; ++i)
        if (available[i] != d.available[i] || complete[i] || bestDifficulty[i] != -1 || checkpoint[i] != 0) return false;
    return true;
}

void LocalProfile::Campaign::completeMission(int mapId, int difficulty) {
    const int i = index(mapId);
    if (i < 0) return;
    complete[i] = true;
    bestDifficulty[i] = std::max(bestDifficulty[i], std::clamp(difficulty, 0, 2));
    const int faction = i < 5 ? 0 : 1, next = faction * 5 + (i % 5 + 1) % 5;
    available[next] = true;
    resumeMap[faction] = kFirstMapId + next;
}

void LocalProfile::Campaign::reachedCheckpoint(int mapId, int index_) {
    const int i = index(mapId);
    if (i < 0) return;
    checkpoint[i] = std::max(checkpoint[i], index_);
    resumeMap[i < 5 ? 0 : 1] = -1;
}

void LocalProfile::noteRecentPlayer(const std::string& fingerprint, const std::string& name, long long now) {
    if (fingerprint.empty()) return;
    recentPlayers.erase(std::remove_if(recentPlayers.begin(), recentPlayers.end(), [&](const RecentPlayer& r) { return r.fingerprint == fingerprint; }),
                        recentPlayers.end());
    recentPlayers.insert(recentPlayers.begin(), RecentPlayer{fingerprint, name, now});
    if (recentPlayers.size() > 50) recentPlayers.resize(50);
}

int LocalProfile::Campaign::nextAvailableChapter(int mapId) const {
    const int i = index(mapId);
    if (i < 0) return mapId;
    const int next = (i < 5 ? 0 : 5) + (i % 5 + 1) % 5;
    return available[next] ? kFirstMapId + next : mapId;
}

bool LocalProfile::campaignField(const std::string& field, std::string& value) const {
    for (int i = 0; i < Campaign::kChapters; ++i) {
        const std::string k = Campaign::key(i);
        if (field.rfind(k, 0) != 0) continue;
        const std::string rest = field.substr(k.size());
        if (rest == "Available") { value = campaign.available[i] ? "1" : "0"; return true; }
        if (rest == "Difficulty") { value = std::to_string(campaign.bestDifficulty[i]); return true; }
        if (rest == "Checkpoint") { value = std::to_string(campaign.checkpoint[i]); return true; }
    }
    return false;
}

} // namespace frontend
