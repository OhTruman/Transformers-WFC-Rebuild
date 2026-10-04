#include "frontend/DataStores.h"
#include "frontend/Catalog.h"
#include "frontend/FlowTrace.h"
#include "frontend/GameFlow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace frontend {

namespace {
const char* gtsName(int v) {
    // EGameTeamStatus (StringToGameTeamStatus inverse) [CONFIRMED values]; 0 = GTS_Unknown.
    switch (v) { case 1: return "GTS_FreeForAllGame"; case 2: return "GTS_SingleTeamGame"; case 3: return "GTS_TeamGame";
                 case 4: return "GTS_CampaignGame"; default: return "GTS_Unknown"; }
}
std::string replaceAll(std::string s, const std::string& a, const std::string& b) {
    for (size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) s.replace(p, a.size(), b);
    return s;
}
} // namespace

std::string DataStores::read(const std::string& markup, bool* known) {
    if (known) *known = true;
    const LobbyState& L = flow_.lobby();
    bool inMatch = flow_.level() == LevelKind::Match;
    const GameSettings* gs = inMatch ? flow_.currentMatch().settings : L.settings;
    std::string tag = inMatch ? flow_.currentMatch().modeTag : L.gameModeTag;
    auto b = [](bool v) { return std::string(v ? "1" : "0"); };
    // [integration] In a match, Gameplay's state (GameFlow::matchValues, from World::hudState) answers the match fields.
    const MatchValues& mv = flow_.matchValues();
    if (inMatch && mv.valid) {
        if (markup == "<CurrentGame:IsCountingDown>") return b(mv.countingDown);
        if (markup == "<CurrentGame:CurrentCountdown>") return std::to_string(mv.countdown);
        if (markup == "<CurrentGame:GoalScore>") return std::to_string(mv.goalScore);
        if (markup == "<PlayerOwner:Score>") return std::to_string(mv.score);
        if (markup == "<PlayerOwner:TeamID>") return std::to_string(mv.myTeam);
        if (markup == "<PlayerOwner:TimeToRespawn>") return std::to_string((int)std::ceil(std::max(0.0f, mv.timeToRespawn)));
    }
    if (markup == "<CurrentGame:GameModeTag>" || markup == "<CurrentGame:MapCompatibilityTag>") return tag;
    // TnVersusGameOverMessage -> GRI.SetGameOverMessage, read by EndGameStats_GFX [RE A5, CONFIRMED]; Gameplay's result.
    if (markup == "<CurrentGame:GameOverMessage>") return flow_.matchValues().gameOverMessage;
    if (markup.rfind("<OnlinePlayerData:ProfileData.", 0) == 0 && markup.size() > 31) {
        std::string field = markup.substr(30, markup.size() - 31);
        if (LocalProfile::isOriginalField(field)) return flow_.profile().get(field);
    }
    if (markup == "<CurrentGame:GameModeFriendlyName>") return tag.empty() ? "" : cat_.modeFriendlyName(tag);
    if (markup == "<CurrentGame:GameModeFriendlyDescription>") return tag.empty() ? "" : cat_.localize("TransGame", "TnOnlineGameSettings" + tag, "Description");
    if (markup == "<CurrentGame:GameModeFriendlyRules>") {
        // RulesFormat with `p (PointsToWin), `t (TimeLimit), `r (Rounds) replaced (FormatedRules) [RE 4].
        if (tag.empty()) return "";
        std::string r = cat_.localize("TransGame", "TnOnlineGameSettings" + tag, "RulesFormat");
        if (gs) {
            r = replaceAll(r, "`p", std::to_string(gs->pointsToWin));
            r = replaceAll(r, "`r", std::to_string(gs->pointsToWin));
            if (!gs->timeLimits.empty()) r = replaceAll(r, "`t", std::to_string(gs->timeLimits[(size_t)gs->timeLimitDefaultIndex] / 60));
        }
        return r;
    }
    if (markup == "<CurrentGame:GameTeamStatus>") return gtsName(inMatch ? flow_.currentMatch().gameTeamStatus : L.gameTeamStatus);
    if (markup == "<CurrentGame:MapSelectionMethod>") return std::to_string(L.mapSelectionMethod);
    if (markup == "<CurrentGame:SelectedMapID>") return std::to_string(inMatch ? flow_.currentMatch().mapId : L.mapId);
    if (markup == "<CurrentGame:AllowMapVeto>") return b(L.allowMapVeto);
    if (markup == "<CurrentGame:MapVetos>") return "0";
    if (markup == "<CurrentGame:RequiredVetos>") return std::to_string(L.numRequiredPlayers / 2 + 1);
    if (markup == "<CurrentGame:IsCountingDown>") return b(L.countingDown);
    if (markup == "<CurrentGame:CurrentCountdown>") return std::to_string(L.countdown);
    if (markup == "<CurrentGame:CanHostRequestGameStart>")
        return b(flow_.level() == LevelKind::GameLobby && L.mapSelectionMethod == 1 && !L.countdownRubicon && L.mapId >= 0);
    if (markup == "<CurrentGame:LobbyStatus>") {
        if (flow_.level() != LevelKind::GameLobby || L.countingDown) return "";
        const char* key = L.lobbyStatus == 3 ? "WaitingForHostToStartStatus" : L.lobbyStatus == 4 ? "WaitingForBalancedTeamsStatus" : "WaitingForPlayersStatus";
        std::string s = cat_.localize("TransGame", "TnGameReplicationInfoGameLobby", key);
        return replaceAll(s, "`p", std::to_string(std::max(0, L.numRequiredPlayers - L.numPlayers)));
    }
    if (markup == "<CurrentGame:NumOnlinePlayers>") return "0";   // no online service
    if (markup == "<CurrentGame:AutobalanceTeams>") return "1";   // setting 0x40000012 default Autobalanced [RE 5.1]
    if (markup == "<CurrentGame:_bIconicMode>") return "0";
    if (markup == "<CurrentGame:SelectedCheckpoint>") return "";
    if (markup == "<CurrentGame:Rounds>") return std::to_string(inMatch ? flow_.currentMatch().rounds : 0);
    if (markup == "<CurrentGame:CurrentRound>") return "1";
    if (markup == "<CurrentGame:ProgressStatusTitle>" || markup == "<CurrentGame:ProgressStatusMessage>") return "";
    if (markup == "<PlayerOwner:PrimeModeActive>") return "0";
    // No profile / gamertag service: the local player's name is the rebuild profile name [PARTIAL].
    if (markup == "<PlayerOwner:PlayerName>") return playerName();
    if (markup == "<PlayerOwner:TeamID>") return std::to_string(inMatch ? flow_.currentMatch().teamIndex : L.localTeam);
    if (markup == "<PlayerOwner:Score>") return "0";
    // TnGameReplicationInfo.MessageOfTheDay is authored in TransGame.int [CONFIRMED data].
    if (markup == "<CurrentGame:MessageOfTheDay>") return cat_.localize("TransGame", "TnGameReplicationInfo", "MessageOfTheDay");
    if (markup == "<OnlinePlayerData:ProfileData.HasAdjustedGamma>") return "1";   // rebuild: no brightness calibration screen pending
    if (markup == "<OnlinePlayerData:ProfileData.UseAllRegionMatchmaking>") return written_.count(markup) ? written_[markup] : "0";
    if (markup.rfind("<TnGameSettings:", 0) == 0 && markup.size() > 17) {
        std::string field = markup.substr(16, markup.size() - 17);
        const GameSettings* cur = flow_.currentSettings();
        if (const SettingField* f = cur ? cur->field(field) : nullptr) {
            int i = flow_.settingIndex(cur, field);
            return i >= 0 && i < (int)f->values.size() ? f->values[(size_t)i] : "";
        }
    }
    auto w = written_.find(markup);
    if (w != written_.end()) return w->second;
    if (known) *known = false;
    return "";
}

bool DataStores::collection(const std::string& markup, Collection& c) {
    if (markup == "<TnMenuItems:Maps>") {
        // TnDataProvider_MapInfo providers in section order; disabled = !HasRequiredAssets [RE 3.2].
        c.columns = {"MapId", "MapFilename", "FriendlyName", "ImagePath", "CompatibleGameTypes", "Faction", "PresenceId"};
        for (const MapInfo& m : cat_.maps()) {
            std::string types;
            for (const std::string& t : m.compatibleGameTypes) types += (types.empty() ? "" : ",") + t;
            c.rows.push_back({std::to_string(m.mapId), m.mapFilename, m.friendlyName, m.imagePath, types, std::to_string(m.faction),
                              std::to_string(m.presenceId)});
            c.enabled.push_back(m.hasRequiredAssets);
        }
        return true;
    }
    if (markup == "<CurrentGame:Players>") {
        // The local player is the only PRI (no online party / session) [offline reduction].
        // Profile progression (levels / characters) is not modelled yet: those columns read empty [PARTIAL].
        c.columns = {"PlayerName", "TeamID", "TeamName", "Score", "Kills", "Deaths", "IsDead", "HeadsetState", "PlayerIconicCharacterName",
                     "_CurrentPower", "IsConnecting", "PrimeModeActive", "CurrentCharacterString", "SelectedCharacterString",
                     "PlayerLevelLeader", "PlayerLevelScientist", "PlayerLevelScout", "PlayerLevelSoldier", "PlayerLevel"};
        bool inMatch = flow_.level() == LevelKind::Match;
        const MatchValues& mv = flow_.matchValues();
        const bool live = inMatch && mv.valid;   // [integration] Gameplay's PRI values in a match
        int team = live ? mv.myTeam : inMatch ? flow_.currentMatch().teamIndex : flow_.lobby().localTeam;
        std::string teamName = team == 0 ? cat_.localize("TransGame", "TnFactionTeamAutobots", "TeamName")
                             : team == 1 ? cat_.localize("TransGame", "TnFactionTeamDecepticons", "TeamName") : "";
        // A fresh profile is level 1 in every specialty (no XP / progression service yet) [PARTIAL].
        auto teamNameOf = [&](int t) {
            return t == 0 ? cat_.localize("TransGame", "TnFactionTeamAutobots", "TeamName")
                 : t == 1 ? cat_.localize("TransGame", "TnFactionTeamDecepticons", "TeamName") : std::string();
        };
        if (live && !mv.players.empty()) {
            // Gameplay's match roster (local player first as in GRI order of joining).
            for (const MatchValues::Player& p : mv.players) {
                c.rows.push_back({p.local ? playerName() : p.name, std::to_string(p.team), teamNameOf(p.team), std::to_string(p.score),
                                  std::to_string(p.kills), std::to_string(p.deaths), p.dead ? "1" : "0",
                                  "0", "", "0", "0", "0", "", "", "1", "1", "1", "1", "1"});
                c.enabled.push_back(true);
            }
            return true;
        }
        c.rows.push_back({playerName(), std::to_string(team), teamName, std::to_string(live ? mv.score : 0),
                          std::to_string(live ? mv.kills : 0), std::to_string(live ? mv.deaths : 0), live && mv.dead ? "1" : "0",
                          "0", "", "0", "0", "0", "", "", "1", "1", "1", "1", "1"});
        c.enabled.push_back(true);
        return true;
    }
    if (markup == "<CurrentGame:Teams>") {
        // Team game: Teams[0] TnFactionTeamAutobots, Teams[1] TnFactionTeamDecepticons [RE 5.3].
        c.columns = {"TeamName", "TeamID", "TeamIndex", "Score"};
        bool inMatch = flow_.level() == LevelKind::Match;
        int gts = inMatch ? flow_.currentMatch().gameTeamStatus : flow_.lobby().gameTeamStatus;
        if (gts == 3) {
            const MatchValues& mv = flow_.matchValues();
            const bool live = inMatch && mv.valid;   // [integration] Gameplay's TeamInfo.Score in a match
            c.rows.push_back({cat_.localize("TransGame", "TnFactionTeamAutobots", "TeamName"), "0", "0", std::to_string(live ? mv.teamScore[0] : 0)});
            c.rows.push_back({cat_.localize("TransGame", "TnFactionTeamDecepticons", "TeamName"), "1", "1", std::to_string(live ? mv.teamScore[1] : 0)});
            c.enabled = {true, true};
        }
        return true;
    }
    if (markup.rfind("<TnGameSettings:", 0) == 0 && markup.size() > 17) {
        std::string field = markup.substr(16, markup.size() - 17);
        const GameSettings* cur = flow_.currentSettings();
        const SettingField* f = cur ? cur->field(field) : nullptr;
        if (!f) return false;
        c.columns = {f->name};
        c.headers = {f->header};
        for (const std::string& v : f->values) { c.rows.push_back({v}); c.enabled.push_back(true); }
        return true;
    }
    if (markup == "<TnMenuItems:Playlists>") {
        // DisplayName: what PartyLobby_GFX's playlist list reads (same localized playlist name).
        c.columns = {"PlaylistId", "FriendlyName", "GameModeTag", "OnlinePlayers", "DisplayName"};
        for (const Playlist& p : cat_.playlists()) {
            if (!p.visibleInMenu) continue;
            c.rows.push_back({std::to_string(p.id), p.displayName, p.tag, "0", p.displayName});
            c.enabled.push_back(true);
        }
        return true;
    }
    // TnMenuItems provider collections from the authored TnDataProvider objects (Catalog::providers), columns = every
    // authored / localized field.
    {
        static const std::map<std::string, std::string> kKinds = {{"<TnMenuItems:Weapons>", "Weapon"}, {"<TnMenuItems:Abilities>", "Ability"},
                                                                 {"<TnMenuItems:Skills>", "Skill"}, {"<TnMenuItems:Chassis>", "Chassis"},
                                                                 {"<TnMenuItems:Killstreaks>", "Killstreak"},
                                                                 {"<TnMenuItems:Challenges>", "Challenge"}};   // TransChallenges.ini (local content; progress needs the stats service)
        auto k = kKinds.find(markup);
        if (k != kKinds.end()) {
            const auto& list = cat_.providers(k->second);
            for (const auto& p : list)
                for (const auto& f : p.fields)
                    if (std::find(c.columns.begin(), c.columns.end(), f.first) == c.columns.end()) c.columns.push_back(f.first);
            for (const auto& p : list) {
                std::vector<std::string> row;
                for (const std::string& col : c.columns) row.push_back(p.get(col));
                c.rows.push_back(row);
                c.enabled.push_back(true);
            }
            return true;
        }
    }
    if (markup == "<TnMenuItems:Specialties>") {
        // TnDataProvider_Specialty providers, TransCustomization.ini order; names from TransGame.int [CONFIRMED].
        c.columns = {"UniqueId", "FriendlyName"};
        for (const char* id : {"Soldier", "Leader", "Scientist", "Scout"}) {
            c.rows.push_back({id, cat_.localize("TransGame", std::string(id) + " TnDataProvider_Specialty", "FriendlyName")});
            c.enabled.push_back(true);
        }
        return true;
    }
    if (markup == "<TnMenuItems:GameModes>") {
        // Game modes offered for private matches: the menu-visible playlists' modes, in playlist order [HIGH].
        c.columns = {"UniqueId", "FriendlyName", "Description", "SettingsConfigName"};
        for (const Playlist& p : cat_.playlists()) {
            if (!p.visibleInMenu) continue;
            c.rows.push_back({p.tag, cat_.modeFriendlyName(p.tag), cat_.localize("TransGame", "TnOnlineGameSettings" + p.tag, "Description"), p.tag});
            c.enabled.push_back(true);
        }
        return true;
    }
    return false;
}

void DataStores::forgetMovie(const std::string& movie) {
    for (size_t i = regs_.size(); i-- > 0;) if (regs_[i].movie == movie) regs_.erase(regs_.begin() + (long)i);
}

std::string DataStores::playerName() const { return flow_.profile().playerName(); }   // LocalProfile identity

BridgeValue DataStores::call(const std::string& fn, const std::vector<std::string>& args, const std::string& movie) {
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    const std::string m = arg(0);
    if (fn == "ReadValue" || fn == "GetFieldValue") {
        bool known;
        std::string v = read(m, &known);
        if (!known) FlowTrace::emit("datastore.unhandled", {{"fn", fn}, {"markup", m}});
        return BridgeValue(v);
    }
    if (fn == "ReadBoolValue") {
        bool known;
        std::string v = read(m, &known);
        if (!known) FlowTrace::emit("datastore.unhandled", {{"fn", fn}, {"markup", m}});
        return BridgeValue(v == "1" || v == "true" || v == "True");
    }
    // <OnlinePlayerData:ProfileData.Field>: the local profile (LocalProfile; original fields and defaults).
    if (fn == "WriteValue" && m.rfind("<OnlinePlayerData:ProfileData.", 0) == 0 && m.size() > 31) {
        flow_.profile().set(m.substr(30, m.size() - 31), arg(1));
        return {};
    }
    if (fn == "WriteValue" && m.rfind("<TnGameSettings:", 0) == 0 && m.size() > 17 &&
        flow_.setSettingValue(m.substr(16, m.size() - 17), arg(1))) return {};
    if (fn == "WriteValue") { written_[m] = arg(1); FlowTrace::emit("datastore.write", {{"markup", m}, {"value", arg(1)}}); return {}; }
    if (fn == "RegisterValueChangedCallback" || fn == "RegisterPendingValueChangedCallback") {
        for (const Reg& r : regs_) if (r.movie == movie && r.markup == m && r.callback == arg(1)) return {};
        regs_.push_back({movie, m, arg(1), read(m)});
        return {};
    }
    if (fn == "UnregisterValueChangedCallback" || fn == "UnregisterPendingValueChangedCallback") {
        for (size_t i = 0; i < regs_.size(); ++i)
            if (regs_[i].movie == movie && regs_[i].markup == m && regs_[i].callback == arg(1)) { regs_.erase(regs_.begin() + (long)i); break; }
        return {};
    }
    Collection c;
    bool isColl = collection(m, c);
    if (fn == "GetCollectionRowCount") {
        if (!isColl) FlowTrace::emit("datastore.unhandled", {{"fn", fn}, {"markup", m}});
        return BridgeValue((double)c.rows.size());
    }
    if (fn == "GetCollectionColumnCount") return BridgeValue((double)c.columns.size());
    if (fn == "GetCollectionColumnHeaderByTag") {
        // The providers' localized column headers are not in any shipped .int file [UNKNOWN]: empty.
        FlowTrace::emit("datastore.unknownHeader", {{"markup", m}, {"tag", arg(1)}});
        return BridgeValue(std::string());
    }
    if (fn == "GetCollectionColumnTag" || fn == "GetCollectionColumnHeader") {
        size_t i = (size_t)std::atoi(arg(1).c_str());
        if (fn == "GetCollectionColumnHeader" && !c.headers.empty()) return BridgeValue(i < c.headers.size() ? c.headers[i] : std::string());
        return BridgeValue(i < c.columns.size() ? c.columns[i] : std::string());
    }
    if (fn == "ReadCollectionValue" || fn == "ReadCollectionBoolValue") {
        std::string col = arg(1);
        size_t row = (size_t)std::atoi(arg(2).c_str());
        for (size_t i = 0; i < c.columns.size(); ++i)
            if (c.columns[i] == col && row < c.rows.size()) {
                if (fn == "ReadCollectionBoolValue") return BridgeValue(c.rows[row][i] == "1" || c.rows[row][i] == "true");
                return BridgeValue(c.rows[row][i]);
            }
        if (isColl) FlowTrace::emit("datastore.unhandled", {{"fn", fn}, {"markup", m}, {"column", col}});
        return BridgeValue(std::string());
    }
    if (fn == "IsCollectionValueEnabled") {
        size_t row = (size_t)std::atoi(arg(1).c_str());
        return BridgeValue(row < c.enabled.size() && c.enabled[row]);
    }
    if (fn == "GetDataStoreFields") {
        // TnGameSettings: the current settings object's fields (the host-options menu shows those with a header).
        std::string out;
        if (m == "TnGameSettings" && flow_.currentSettings())
            for (const SettingField& f : flow_.currentSettings()->fields) out += (out.empty() ? "" : ",") + f.name;
        return BridgeValue(out);
    }
    FlowTrace::emit("datastore.unhandled", {{"fn", fn}, {"markup", m}});
    return {};
}

std::vector<DataStores::Change> DataStores::poll() {
    std::vector<Change> out;
    for (Reg& r : regs_) {
        std::string v = read(r.markup);
        if (v != r.last) { r.last = v; out.push_back({r.movie, r.markup, r.callback, v}); }
    }
    return out;
}

} // namespace frontend
