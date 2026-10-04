#include "frontend/Catalog.h"
#include "assets/Json.h"
#include "core/Config.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace frontend {

namespace {

bool readJson(const std::string& path, assets::Json& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return assets::Json::parse(ss.str(), out);
}

bool fileExists(const std::string& path) { std::ifstream f(path, std::ios::binary); return (bool)f; }

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

bool iendsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && lower(s.substr(s.size() - suffix.size())) == lower(suffix);
}

std::string stripPackage(const std::string& s) {   // "TransGame.TnGameRules_X" -> keep (rules keep their package)
    return s;
}

// Fields the frontend manifests do not carry yet (RE MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md section 4 table,
// CONFIRMED authored defaults). HANDOFF: AssetTools to add LobbyGameClass / TeamType / score defaults to
// frontend_modes.json; the manifest value wins whenever present.
struct ModeDefaults { const char* tag; const char* lobbyGameClass; const char* teamType; int teamValue; int points; const char* scoreOpt; };
const ModeDefaults kModeDefaults[] = {
    {"TDM", "TransContent.TnGameLobbyGameTeam", "GTS_TeamGame", 3, 40, "PointsToWin"},
    {"DM", "TransContent.TnGameLobbyGameFreeForAll", "GTS_FreeForAllGame", 1, 20, "PointsToWin"},
    {"CTF", "TransContent.TnGameLobbyGameTeam", "GTS_TeamGame", 3, 2, "Rounds"},
    {"KOTH", "TransContent.TnGameLobbyGameTeam", "GTS_TeamGame", 3, 400, "PointsToWin"},
    {"DOM", "TransContent.TnGameLobbyGameTeam", "GTS_TeamGame", 3, 400, "PointsToWin"},
    {"EXT", "TransContent.TnGameLobbyGameTeam", "GTS_TeamGame", 3, 3, "PointsToWin"},
    {"SV", "TransContent.TnGameLobbyGameSurvival", "GTS_SingleTeamGame", 2, -1, "PointsToWin"},
    {"CP", "TransContent.TnGameLobbyGameCoop", "GTS_CampaignGame", 4, -1, "PointsToWin"},
    {"CCP", "TransContent.TnGameLobbyGameCoop", "GTS_CampaignGame", 4, -1, "PointsToWin"},
};

} // namespace

bool MapInfo::compatibleWith(const std::string& tag) const {
    return std::find(compatibleGameTypes.begin(), compatibleGameTypes.end(), tag) != compatibleGameTypes.end();
}

std::string Catalog::defaultManifestRoot() {
    if (const char* e = std::getenv("WFC_FRONTEND_MANIFESTS")) return e;
    return "F:/Transformers Rebuild/AssetTools/manifests";
}

std::string Catalog::defaultExtractedRoot() {
    if (const char* e = std::getenv("WFC_EXTRACTED")) return e;
    std::string vs = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    size_t s = vs.find_last_of("/\\");
    return s == std::string::npos ? vs : vs.substr(0, s);   // ExtractedAssets (parent of VerticalSlice)
}

bool Catalog::load(const std::string& manifestRoot, const std::string& extractedRoot, const std::string& runtimeMapRoot) {
    manifestRoot_ = manifestRoot;
    extractedRoot_ = extractedRoot;
    maps_.clear(); playlists_.clear(); modes_.clear(); settings_.clear(); loc_.clear(); engageTexts_.clear();

    // ---- localization first (FriendlyNames of settings classes come from it) ----
    assets::Json loc;
    if (readJson(manifestRoot + "/frontend_localization.json", loc)) {
        const assets::Json& sec = loc["sections"];
        for (const auto& [file, sections] : sec.obj)
            for (const auto& [section, keys] : sections.obj)
                for (const auto& [key, val] : keys.obj)
                    if (val.isString()) loc_[file + "." + section + "." + key] = val.asString();
        for (const auto& [section, keys] : loc["transgame_frontend_sections"].obj)
            for (const auto& [key, val] : keys.obj)
                if (val.isString()) loc_["TransGame." + section + "." + key] = val.asString();
        for (const auto& [key, val] : loc["gfx_keys"].obj)
            if (val["INT"].isString() && key.size() > 1) loc_[key.substr(1)] = val["INT"].asString();
    } else {
        LOG_WARN("FRONTEND catalog: no frontend_localization.json under %s", manifestRoot.c_str());
    }
    // The shipped INT localization files themselves fill what the manifest does not carry (e.g. the HUD's
    // TnDamageType* DeathString templates, TnMessageTextColors-coloured kill feed, game-type messages).
    for (const char* file : {"TransGame", "UIText"}) {
        std::ifstream f(extractedRoot + "/config/Coalesced_int/TransGame/Localization/INT/" + file + ".int");
        std::string line, section;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == ';') continue;
            if (line[0] == '[') { section = line.substr(1, line.find(']') - 1); continue; }
            size_t eq = line.find('=');
            if (eq == std::string::npos || section.empty()) continue;
            std::string v = line.substr(eq + 1);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            loc_.emplace(std::string(file) + "." + section + "." + line.substr(0, eq), v);   // manifest entries win
        }
    }

    loadProviders(extractedRoot);

    // ---- maps ----
    assets::Json mj;
    if (!readJson(manifestRoot + "/frontend_maps.json", mj)) {
        LOG_ERROR("FRONTEND catalog: cannot read %s/frontend_maps.json", manifestRoot.c_str());
        return false;
    }
    for (size_t i = 0; i < mj["maps"].size(); ++i) {
        const assets::Json& m = mj["maps"][i];
        MapInfo mi;
        mi.iniOrder = m["ini_order"].asInt();
        mi.mapFilename = m["MapFilename"].asString();
        mi.mapId = std::atoi(m["MapId"].asString().c_str());
        mi.presenceId = std::atoi(m["PresenceId"].asString().c_str());
        mi.faction = std::atoi(m["Faction"].asString().c_str());
        for (size_t k = 0; k < m["CompatibleGameTypes"].size(); ++k) mi.compatibleGameTypes.push_back(m["CompatibleGameTypes"][k].asString());
        mi.imagePath = m["ImagePath"].asString();
        mi.thumbnailPng = m["thumbnail_png"].asString();
        mi.friendlyName = m["FriendlyName"]["INT"].asString();
        mi.friendlyNameFra = m["FriendlyName"]["FRA"].asString();
        mi.cooked = m["cooked_package_present"].isString();
        // Rebuild runtime directory: the map's package base name without the "_Base_m" suffix
        // (MP_IAC_Streets_Base_m -> Maps/MP_IAC_Streets, the AssetTools VerticalSlice layout).
        std::string dir = mi.mapFilename;
        if (iendsWith(dir, "_Base_m")) dir = dir.substr(0, dir.size() - 7);
        mi.runtimeDir = dir;
        // [integration M05] Selectable = cooked + the runtime world AND AssetTools' render export (render_index.json,
        // read by the renderer): a map whose presentation has not been exported is listed disabled, as a map without
        // HasRequiredAssets is. Data-driven: a map becomes selectable when AssetTools exports it (no map names here).
        // [integration M06] ... or the AssetTools production-pipeline index (manifests/maps/<map>/render_index_generic.json,
        // written only for a map that passed the generic pipeline and its structure audit; the renderer reads it through
        // tools/render/build_render_index.py). The uncooked registry maps (Fortress / Havoc / Tranquillity) stay
        // disabled by `cooked`.
        mi.hasRequiredAssets = mi.cooked && fileExists(runtimeMapRoot + "/" + dir + "/world.glb")
                               && (fileExists(runtimeMapRoot + "/" + dir + "/render_index.json")
                                   || fileExists(manifestRoot + "/maps/" + dir + "/render_index_generic.json"));
        maps_.push_back(mi);
    }
    std::stable_sort(maps_.begin(), maps_.end(), [](const MapInfo& a, const MapInfo& b) { return a.iniOrder < b.iniOrder; });

    // ---- modes / playlists / settings ----
    assets::Json md;
    if (!readJson(manifestRoot + "/frontend_modes.json", md)) {
        LOG_ERROR("FRONTEND catalog: cannot read %s/frontend_modes.json", manifestRoot.c_str());
        return false;
    }
    for (const auto& [cls, v] : md["game_mode_info (TnDataProvider_GameModeInfo: Xe-TransGame.ini + TransGame.int)"].obj) {
        if (cls == "TnDataProvider_GameModeInfo") continue;
        GameModeInfo gi;
        gi.gameClassShort = cls;
        gi.settingsConfigName = v["SettingsConfigName"].asString();
        gi.friendlyName = v["FriendlyName"].asString();
        gi.friendlyNameFra = v["FriendlyName_FRA"].asString();
        modes_.push_back(gi);
    }
    for (size_t i = 0; i < md["playlists"].size(); ++i) {
        const assets::Json& p = md["playlists"][i];
        Playlist pl;
        pl.id = std::atoi(p["PlaylistId"].asString().c_str());
        pl.section = p["section"].asString();
        pl.tag = p["GameModeTag"].asString();
        pl.visibleInMenu = p["VisibleInMenu"].asString() == "true";
        pl.displayName = p["DisplayName"]["INT"].asString();
        pl.displayNameFra = p["DisplayName"]["FRA"].asString();
        playlists_.push_back(pl);
    }
    // TnOnlinePlaylistManager.Playlists order + names + settings classes.
    const assets::Json& pm = md["playlist_manager"];
    defaultQuickmatchPlaylist_ = std::atoi(pm["DefaultQuickmatchPlaylistId"].asString().c_str());
    std::vector<Playlist> ordered;
    for (size_t i = 0; i < pm["Playlists"].size(); ++i) {
        const std::string& s = pm["Playlists"][i].asString();
        auto field = [&](const char* key) {
            size_t a = s.find(key);
            if (a == std::string::npos) return std::string();
            a += std::string(key).size();
            if (s[a] == '"') { size_t b = s.find('"', a + 1); return s.substr(a + 1, b - a - 1); }
            size_t b = s.find_first_of(",)", a);
            return s.substr(a, b - a);
        };
        int id = std::atoi(field("PlaylistId=").c_str());
        for (const Playlist& p : playlists_)
            if (p.id == id) {
                Playlist q = p;
                q.name = field("Name=");
                std::string sc = field("GameSettingsClassName=");
                size_t dot = sc.rfind('.');
                q.settingsClass = dot == std::string::npos ? sc : sc.substr(dot + 1);
                ordered.push_back(q);
            }
    }
    if (ordered.size() == playlists_.size()) playlists_ = ordered;

    for (const auto& [cls, v] : md["online_game_settings_classes"].obj) {
        GameSettings gs;
        gs.className = cls;
        gs.tag = v["GameModeTag"].asString();
        gs.gameClass = v["GameClass"].asString();
        for (size_t k = 0; k < v["Rules"].size(); ++k) gs.rules.push_back(stripPackage(v["Rules"][k].asString()));
        for (size_t k = 0; k < v["TimeLimits"].size(); ++k) gs.timeLimits.push_back(v["TimeLimits"][k].asInt());
        gs.numPublicConnections = v["NumPublicConnections"].asInt(0);
        gs.numPrivateConnections = v["NumPrivateConnections"].asInt(0);
        gs.isPrivate = iendsWith(cls, "Private");
        gs.isPublic = iendsWith(cls, "Public");
        for (const ModeDefaults& d : kModeDefaults)
            if (gs.tag == d.tag) {
                gs.lobbyGameClass = d.lobbyGameClass; gs.teamType = d.teamType; gs.teamTypeValue = d.teamValue;
                gs.pointsToWin = d.points; gs.scoreOptionName = d.scoreOpt;
            }
        if (v.has("LobbyGameClass")) gs.lobbyGameClass = v["LobbyGameClass"].asString();
        if (v.has("PointsToWin")) gs.pointsToWin = v["PointsToWin"].asInt();
        // [RE note 4] Public/Private pairs use 10 connections (SV 4, CP/CCP 3); Private = host's choice.
        int conns = gs.tag == "SV" ? 4 : (gs.tag == "CP" || gs.tag == "CCP") ? 3 : 10;
        if (gs.isPrivate && gs.numPrivateConnections == 0) gs.numPrivateConnections = conns;
        if (gs.isPublic && gs.numPublicConnections == 0) gs.numPublicConnections = conns;
        gs.mapSelectionMethod = gs.isPrivate ? 1 : 0;
        if (gs.tag == "SV" || gs.tag == "CP" || gs.tag == "CCP") gs.lobbyMapName = "UI_CampaignLobby_m";
        settings_[cls] = gs;
    }

    // ---- authored settings class defaults (tools/frontend/export_game_settings.py; future: an AssetTools manifest) ----
    loadSettingsDefaults(manifestRoot);

    // ---- loading ----
    assets::Json lj;
    if (readJson(manifestRoot + "/frontend_loading.json", lj)) {
        const assets::Json& c = lj["config (Xe-TransGame.ini [LoadingMovie])"];
        loadingDefault_ = c["DefaultFileName"].asString();
        loadingInitial_ = c["InitialStartupFileName"].asString();
    }
    for (int i = 0; i < 64; ++i) {
        std::string t = localize("TransGame", "TnOnlineGameSettingsBase", "EngageText[" + std::to_string(i) + "]");
        if (t.empty()) break;
        engageTexts_.push_back(t);
    }

    int enabled = 0;
    for (const MapInfo& m : maps_) enabled += m.hasRequiredAssets;
    LOG_INFO("FRONTEND catalog: %zu map providers (%d with rebuild runtime data), %zu playlists, %zu settings classes, %zu loc strings, %zu engage texts",
             maps_.size(), enabled, playlists_.size(), settings_.size(), loc_.size(), engageTexts_.size());
    for (const MapInfo& m : maps_)
        LOG_INFO("FRONTEND catalog map %d %-26s \"%s\" cooked=%d runtime=%s modes=%zu", m.mapId, m.mapFilename.c_str(), m.friendlyName.c_str(),
                 (int)m.cooked, m.hasRequiredAssets ? m.runtimeDir.c_str() : "-", m.compatibleGameTypes.size());
    return true;
}

void Catalog::loadSettingsDefaults(const std::string& manifestRoot) {
    assets::Json j;
    std::string used;
    for (std::string path : {manifestRoot + "/frontend_game_settings.json", std::string(WFC_SOURCE_DIR) + "/data/frontend/game_settings.json"})
        if (readJson(path, j)) { used = path; break; }
    if (used.empty()) { LOG_WARN("FRONTEND catalog: no game_settings.json; host options unavailable, RE-table defaults used"); return; }
    int n = 0;
    for (auto& [cls, gs] : settings_) {
        const assets::Json& c = j["classes"][cls];
        if (!c.isObject()) continue;
        ++n;
        if (c.has("LobbyGameClass")) gs.lobbyGameClass = c["LobbyGameClass"].asString();
        if (c.has("LobbyMapName")) gs.lobbyMapName = c["LobbyMapName"].asString();
        if (c.has("NumRequiredPlayers")) gs.numRequiredPlayers = c["NumRequiredPlayers"].asInt();
        if (c.has("NumPrivateConnections")) gs.numPrivateConnections = c["NumPrivateConnections"].asInt();
        if (c.has("NumPublicConnections")) gs.numPublicConnections = c["NumPublicConnections"].asInt();
        if (c.has("TeamType")) {
            gs.teamType = c["TeamType"].asString();
            gs.teamTypeValue = gs.teamType == "GTS_FreeForAllGame" ? 1 : gs.teamType == "GTS_SingleTeamGame" ? 2
                             : gs.teamType == "GTS_TeamGame" ? 3 : gs.teamType == "GTS_CampaignGame" ? 4 : 0;
        }
        std::map<int, int> ctxDefault;
        for (size_t i = 0; i < c["LocalizedSettings"].size(); ++i)
            ctxDefault[c["LocalizedSettings"][i]["Id"].asInt()] = c["LocalizedSettings"][i]["ValueIndex"].asInt();
        std::map<int, double> propDefault;
        for (size_t i = 0; i < c["Properties"].size(); ++i)
            propDefault[c["Properties"][i]["PropertyId"].asInt()] = c["Properties"][i]["Data"]["Value1"].asDouble();
        gs.fields.clear();
        for (size_t i = 0; i < c["LocalizedSettingsMappings"].size(); ++i) {
            const assets::Json& m = c["LocalizedSettingsMappings"][i];
            SettingField f;
            f.name = m["Name"].asString(); f.header = m["ColumnHeaderText"].asString(); f.id = m["Id"].asInt();
            for (size_t k = 0; k < m["ValueMappings"].size(); ++k) f.values.push_back(m["ValueMappings"][k]["Name"].asString());
            int def = ctxDefault.count(f.id) ? ctxDefault[f.id] : 0;
            // LocalizedSettings ValueIndex is the value Id; the mapping's position of that Id is the list index.
            for (size_t k = 0; k < m["ValueMappings"].size(); ++k) if (m["ValueMappings"][k]["Id"].asInt() == def) f.defaultIndex = (int)k;
            gs.fields.push_back(f);
        }
        for (size_t i = 0; i < c["PropertyMappings"].size(); ++i) {
            const assets::Json& m = c["PropertyMappings"][i];
            SettingField f;
            f.name = m["Name"].asString(); f.header = m["ColumnHeaderText"].asString(); f.id = m["Id"].asInt(); f.property = true;
            double def = propDefault.count(f.id) ? propDefault[f.id] : 0;
            for (size_t k = 0; k < m["PredefinedValues"].size(); ++k) {
                double v = m["PredefinedValues"][k]["Value1"].asDouble();
                f.numeric.push_back(v);
                f.values.push_back(std::to_string((long long)v));
                if (v == def) f.defaultIndex = (int)k;
            }
            if (f.name == "PointsToWin" || f.name == "Rounds") { gs.pointsToWin = (int)def; gs.scoreOptionName = f.name; }
            gs.fields.push_back(f);
        }
        if (const SettingField* t = gs.field("TimeLimit")) gs.timeLimitDefaultIndex = t->defaultIndex;
        if (const SettingField* ms = gs.field("MapSelectionMethod")) gs.mapSelectionMethod = ms->defaultIndex;
    }
    LOG_INFO("FRONTEND catalog: authored settings defaults for %d classes (%s)", n, used.c_str());
}

const MapInfo* Catalog::mapById(int id) const {
    for (const MapInfo& m : maps_) if (m.mapId == id) return &m;
    return nullptr;
}

const MapInfo* Catalog::mapByFilename(const std::string& f) const {
    std::string lf = lower(f);
    for (const MapInfo& m : maps_) if (lower(m.mapFilename) == lf) return &m;
    return nullptr;
}

const MapInfo* Catalog::mapByRuntimeDir(const std::string& dir) const {
    std::string ld = lower(dir);
    for (const MapInfo& m : maps_) if (lower(m.runtimeDir) == ld) return &m;
    return nullptr;
}

const GameSettings* Catalog::settings(const std::string& className) const {
    auto it = settings_.find(className);
    return it == settings_.end() ? nullptr : &it->second;
}

const GameSettings* Catalog::settingsForTag(const std::string& tag, bool privateMatch) const {
    if (const GameSettings* s = settings("TnOnlineGameSettings" + tag + (privateMatch ? "Private" : "Public"))) return s;
    return settings("TnOnlineGameSettings" + tag);
}

const Playlist* Catalog::playlistById(int id) const {
    for (const Playlist& p : playlists_) if (p.id == id) return &p;
    return nullptr;
}

std::vector<const MapInfo*> Catalog::compatibleMaps(const std::string& tag, bool skipDisabled) const {
    std::vector<const MapInfo*> out;
    for (const MapInfo& m : maps_)
        if (m.compatibleWith(tag) && (!skipDisabled || m.hasRequiredAssets)) out.push_back(&m);
    return out;
}

std::string Catalog::Provider::get(const std::string& k) const {
    for (const auto& f : fields) if (f.first == k) return f.second;
    return std::string();
}

const std::vector<Catalog::Provider>& Catalog::providers(const std::string& kind) const {
    static const std::vector<Provider> none;
    auto it = providers_.find(kind);
    return it == providers_.end() ? none : it->second;
}

void Catalog::loadProviders(const std::string& extractedRoot) {
    providers_.clear();
    const std::string cfg = extractedRoot + "/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/";
    for (const char* file : {"TransCustomization.ini", "TransWeapons.ini", "TransChallenges.ini"}) {
        std::ifstream f(cfg + file);
        std::string line;
        Provider* cur = nullptr;
        std::string curKind;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line[0] == '[') {
                cur = nullptr;
                size_t sp = line.find(" TnDataProvider_"), end = line.find(']');
                if (sp != std::string::npos && end != std::string::npos) {
                    curKind = line.substr(sp + 16, end - sp - 16);
                    providers_[curKind].push_back(Provider{line.substr(1, sp - 1), {}});
                    cur = &providers_[curKind].back();
                    for (const char* lk : {"FriendlyName", "Description", "FriendlyIconicName", "ChallengeName"}) {   // localized fields
                        std::string v = localize("TransGame", cur->name + " TnDataProvider_" + curKind, lk);
                        if (!v.empty()) cur->fields.push_back({lk, v});
                    }
                }
                continue;
            }
            size_t eq = line.find('=');
            if (!cur || eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            bool merged = false;
            for (auto& fv : cur->fields) if (fv.first == k) { fv.second += "," + v; merged = true; }
            if (!merged) cur->fields.push_back({k, v});
        }
    }
    size_t n = 0;
    for (const auto& [k, v] : providers_) n += v.size();
    LOG_INFO("FRONTEND catalog: %zu authored TnDataProvider objects (%zu kinds)", n, providers_.size());
}

std::string Catalog::localize(const std::string& file, const std::string& section, const std::string& key) const {
    auto it = loc_.find(file + "." + section + "." + key);
    return it == loc_.end() ? std::string() : it->second;
}

std::string Catalog::localizeKey(const std::string& dollarKey) const {
    std::string k = !dollarKey.empty() && dollarKey[0] == '$' ? dollarKey.substr(1) : dollarKey;
    auto it = loc_.find(k);
    return it == loc_.end() ? std::string() : it->second;
}

std::string Catalog::modeFriendlyName(const std::string& tag) const {
    std::string s = localize("TransGame", "TnOnlineGameSettings" + tag, "FriendlyName");
    if (!s.empty()) return s;
    for (const GameModeInfo& g : modes_) if (g.settingsConfigName == tag) return g.friendlyName;
    return tag;
}

} // namespace frontend
