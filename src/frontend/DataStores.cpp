#include "frontend/DataStores.h"
#include "core/FrameProfile.h"
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
        if (markup == "<CurrentGame:GoalScore>" && mv.goalScore > 0) return std::to_string(mv.goalScore);
        if (markup == "<CurrentGame:AttackingTeamIndex>") return std::to_string(mv.attackingTeamIndex);
        if (markup == "<CurrentGame:CurrentObjectiveCountdown>") return std::to_string(mv.currentObjectiveCountdown);
        if (markup == "<CurrentGame:CompetitiveScoreEnabled>") return b(mv.competitiveScoreEnabled);
        if (markup == "<PlayerOwner:Score>") return std::to_string(mv.score);
        if (markup == "<PlayerOwner:TeamID>") return std::to_string(mv.myTeam);
        if (markup == "<PlayerOwner:TimeToRespawn>") return std::to_string((int)std::ceil(std::max(0.0f, mv.timeToRespawn)));
    }
    if (markup == "<CurrentGame:GameModeTag>" || markup == "<CurrentGame:MapCompatibilityTag>") return tag;
    // TnVersusGameOverMessage -> GRI.SetGameOverMessage, read by EndGameStats_GFX [RE A5, CONFIRMED]; Gameplay's result.
    if (markup == "<CurrentGame:GameOverMessage>") {
        // TnVersusGameOverMessage [RE A5]: a forfeit (Systems 10.5 / Gameplay's 1.5 s team-forfeit check: a team has no
        // players left) shows the winners the original ForfeitMessage "Enemy team forfeits"; otherwise Gameplay's line.
        const MatchValues& mv = flow_.matchValues();
        // Escalation's loss (every player dead): TnSurvivalGameOverMessage "You survived until wave `r" with the wave
        // reached (DisplayWave + 1) [MP_ESCALATION_RE 8.4, RE end-of-match check]; EndGameStats shows it as its header.
        if (mv.endReason == "AllDead" && mv.svWave > 0) {
            const std::string m = cat_.localize("TransContent", "TnSurvivalGameOverMessage", "Message");
            if (!m.empty()) return replaceAll(m, "`r", std::to_string(mv.svWave));
        }
        if (mv.endReason == "Forfeit" && mv.winnerTeam >= 0 && mv.winnerTeam == mv.myTeam) {
            const std::string f = cat_.localize("TransContent", "TnVersusGameOverMessage", "ForfeitMessage");
            if (!f.empty()) return f;
        }
        return mv.gameOverMessage;
    }
    // GRI defaults before Gameplay's match values (Default__TnGameReplicationInfoMultiplayer: AttackingTeamIndex -1,
    // CurrentObjectiveCountdown -1 [CONF]; CompetitiveScoreEnabled engine default 0 [HIGH]). ActiveObjectives is the
    // pause menu's objective text (empty -> no objective clip): none in versus [HIGH].
    if (markup == "<CurrentGame:AttackingTeamIndex>" || markup == "<CurrentGame:CurrentObjectiveCountdown>") return "-1";
    if (markup == "<CurrentGame:CompetitiveScoreEnabled>") return "0";
    if (markup == "<CurrentGame:ActiveObjectives>") return flow_.campaign() ? flow_.objectives().activeText() : "";   // E1
    // Escalation (mode SV) GRI only: a versus GRI has no wave properties, so the read is empty and Hud_GFX keeps its wave
    // clip hidden (it shows "NEW WAVE IN n" for any value; "0" did so in TDM - Integration M08c).
    // In an Escalation match the values come from Gameplay (HudGameState svWave = DisplayWave + 1, svNextWaveTime; MP_ESCALATION_RE
    // 8.1); Gameplay launches the mode as "ESC".
    if (markup == "<CurrentGame:_CurrentWave>" || markup == "<CurrentGame:_NextWaveTime>") {
        if (tag != "SV" && tag != "ESC") return "";
        if (flow_.level() != LevelKind::Match) return "0";
        const MatchValues& sv = flow_.matchValues();
        return std::to_string(markup == "<CurrentGame:_CurrentWave>" ? sv.svWave : sv.svNextWaveTime);
    }
    // GRI.GoalScore = PointsToWin (TnMultiplayerGame.InitGame). Hud_GFX reads it once when it loads and falls back to 10
    // when it is not > 0, which fills the TDM (40) / DM (20) team bars at 10 points [Hud_GFX AS, Experimental audit P1-2].
    if (markup == "<CurrentGame:GoalScore>") return std::to_string(inMatch ? flow_.currentMatch().goalScore : (L.settings && L.settings->pointsToWin > 0 ? L.settings->pointsToWin : 0));
    // PC ADAPTATION: the frame-rate limit entry the frontend adds to the PC graphics menu (Hz, 0 = unlimited).
    // <Strings:File.Section.Key>: a localized string by its INT path (Hud_GFX's downed screen reads
    // <Strings:TransGame.ContextualCommands.DownedSuicide>; CharacterSelect_GFX / CustomTransformers_GFX read UIText
    // ones: the rename prompt's NameTrans / NameDesc, Claimed / Unclaimed). Answered in versus too (Integration
    // 2026-10-17: the original filled those menus from this store, so "" was a fidelity gap). DownedSuicide has no entry
    // in the shipped TransGame.int and stays "", as the data says.
    if (markup.rfind("<Strings:", 0) == 0 && markup.size() > 10 && markup.back() == '>') {
        const std::string path = markup.substr(9, markup.size() - 10);
        const size_t a = path.find('.'), b = path.rfind('.');
        if (a != std::string::npos && b != std::string::npos && b > a)
            return cat_.localize(path.substr(0, a), path.substr(a + 1, b - a - 1), path.substr(b + 1));
        return "";
    }
    if (markup == "<PCSettings:FrameLimit>") return std::to_string(flow_.profile().display.frameLimit);
    if (markup == "<PCSettings:Upscaling>") return std::to_string(flow_.profile().display.upscaling);    // PC EXTENSION
    if (markup == "<PCSettings:HDTextures>") return std::to_string(flow_.profile().display.hdTextures);   // PC EXTENSION
    if (markup == "<PCSettings:FrameGeneration>") return flow_.profile().display.frameGeneration ? "1" : "0";   // PC EXTENSION
    if (markup == "<PCSettings:RayTracing>") return std::to_string(flow_.profile().display.rayTracing);        // PC EXTENSION
    if (markup == "<PCSettings:ShowFps>") return flow_.profile().display.showFps ? "1" : "0";                // PC EXTENSION
    if (markup == "<PCSettings:LensFlares>") return std::to_string(flow_.profile().display.lensFlares);     // PC EXTENSION (user)
    if (markup == "<PCSettings:Smoke>") return std::to_string(flow_.profile().display.smoke);               // PC EXTENSION (user)
    if (markup == "<PCSettings:Water>") return std::to_string(flow_.profile().display.water);               // PC EXTENSION (user #14)
    if (markup == "<WfcLobby:GameMode>") return flow_.lobby().gameModeTag;   // lobby flow v2: the Game Settings mode row
    if (markup == "<PCSettings:VolumetricLighting>") return flow_.profile().display.volumetricLighting ? "1" : "0";   // PC EXTENSION
    if (markup == "<PCSettings:VoiceMode>") return std::to_string(flow_.profile().voice.mode);              // PC EXTENSION (voice, 3.12)
    if (markup == "<PCSettings:VoiceAudience>") return std::to_string(flow_.profile().voice.audience);
    if (markup == "<PCSettings:VoiceDevice>") return flow_.profile().voice.device;
    if (markup == "<PCSettings:VoicePushToTalkKey>") return flow_.profile().voice.pttKey;
    if (markup == "<PCSettings:VoiceVolume>") return std::to_string(flow_.profile().voice.volume);
    if (markup == "<PCSettings:ProximityVolume>") return std::to_string(flow_.profile().voice.proximityVolume);
    if (markup == "<PCSettings:VoiceMicTest>") return "0";   // the meter row (its value text is the live level)
    if (markup == "<PCSettings:Anisotropy>") return std::to_string(flow_.profile().display.anisotropy);  // PC EXTENSION
    if (markup.rfind("<OnlinePlayerData:ProfileData.", 0) == 0 && markup.size() > 31) {
        std::string field = markup.substr(30, markup.size() - 31);
        std::string cv;
        if (flow_.profile().campaignField(field, cv)) return cv;   // CampaignSelect's chapter unlock / difficulty / checkpoint
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
    if (markup == "<CurrentGame:SelectedCheckpoint>") return L.checkpoint;
    if (markup == "<CurrentGame:SelectedMapID>") return std::to_string(inMatch ? flow_.currentMatch().mapId : L.mapId);
    // a voting lobby (ONLINE_LOBBY_FLOW_V2 4.1) replaces the original rotate-and-veto: no Veto button hint
    if (markup == "<CurrentGame:AllowMapVeto>") return b(L.allowMapVeto && flow_.mapVote().round == 0);
    if (markup == "<CurrentGame:MapVetos>") return "0";
    if (markup == "<CurrentGame:RequiredVetos>") return std::to_string(L.numRequiredPlayers / 2 + 1);
    if (markup == "<CurrentGame:IsCountingDown>") return b(L.countingDown);
    if (markup == "<CurrentGame:CurrentCountdown>") return std::to_string(L.countdown);
    if (markup == "<CurrentGame:CanHostRequestGameStart>")
        return b(flow_.level() == LevelKind::GameLobby && L.mapSelectionMethod == 1 && !L.countdownRubicon && L.mapId >= 0 &&
                 flow_.sessionRole() != GameFlow::SessionRole::Member &&     // an online member never starts the game
                 flow_.mapVote().round == 0 &&   // CanHostRequestGameStart = IsPrivateMatch: a public lobby starts by itself [RE]
                 (flow_.sessionRole() == GameFlow::SessionRole::Offline || L.lobbyStatus == 3 ||
                  GameFlow::isCampaignTag(L.gameModeTag)));   // online: the original gate (D4); TnGameLobbyGameCampaign: always
    if (markup == "<CurrentGame:LobbyStatus>") {
        if (flow_.level() != LevelKind::GameLobby || L.countingDown) return "";
        if (flow_.matchInProgressForMember()) return cat_.localizeKey("$WfcPC.Online.MatchInProgressStatus");   // D6
        // waiting for players: the original's singular line for exactly one missing (WaitingForOnePlayerStatus, RE
        // ONLINE_RULES_CHECK_RE), else the plural one
        const int missing = std::max(0, L.numRequiredPlayers - L.numPlayers);
        const char* key = L.lobbyStatus == 3 ? "WaitingForHostToStartStatus" : L.lobbyStatus == 4 ? "WaitingForBalancedTeamsStatus"
                        : L.lobbyStatus == 6 ? "WaitingForCharacterChoiceStatus" : missing == 1 ? "WaitingForOnePlayerStatus" : "WaitingForPlayersStatus";
        std::string s = cat_.localize("TransGame", "TnGameReplicationInfoGameLobby", key);
        return replaceAll(s, "`p", std::to_string(missing));
    }
    if (markup == "<CurrentGame:NumOnlinePlayers>") return "0";   // no online service
    if (markup == "<CurrentGame:AutobalanceTeams>") return "1";   // setting 0x40000012 default Autobalanced [RE 5.1]
    if (markup == "<CurrentGame:_bIconicMode>") return flow_.iconicMode() ? "1" : "0";   // GRI._bIconicMode
    if (markup == "<CurrentGame:ClaimedIconicCharacters>") {   // CharacterSelect_GFX UpdateClaimedCharacters (split(','))
        std::string o;
        for (const std::string& n : flow_.claimedIconicCharacters()) o += (o.empty() ? "" : ",") + n;
        return o;
    }
    if (markup == "<CurrentGame:SelectedCheckpoint>") return "";
    if (markup == "<CurrentGame:Rounds>") return std::to_string(inMatch ? flow_.currentMatch().rounds : 0);
    if (markup == "<CurrentGame:CurrentRound>") return "1";
    if (markup == "<CurrentGame:ProgressStatusTitle>" || markup == "<CurrentGame:ProgressStatusMessage>") return "";
    if (markup == "<PlayerOwner:PrimeModeActive>") return flow_.profile().progression.prime ? "1" : "0";
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
        if (const GameFlow::BotOption* o = GameFlow::botOption(field))   // PC ADAPTATION: bot rows of Game Mode Options
            return flow_.botChoiceLabel(o->field, flow_.botValue(o->field));
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
    core::prof::Scope prof("ds.collection");
    if (markup == "<TnMenuItems:Checkpoints>") {   // CampaignSelect's thumbs: TnDataProvider_Checkpoint in data order
        c.columns = {"MapId", "FriendlyName", "ImagePath", "CheckpointPath"};
        for (const CheckpointInfo& cp : cat_.checkpoints()) {
            c.rows.push_back({std::to_string(cp.mapId), cp.friendlyName, cp.imagePath, cp.checkpointPath});
            c.enabled.push_back(true);
        }
        return true;
    }
    if (markup == "<TnMenuItems:Maps>") {
        // TnDataProvider_MapInfo providers in section order; disabled = !HasRequiredAssets [RE 3.2].
        c.columns = {"MapId", "MapFilename", "FriendlyName", "ImagePath", "CompatibleGameTypes", "Faction", "PresenceId", "Description"};
        for (const MapInfo& m : cat_.maps()) {
            std::string types;
            for (const std::string& t : m.compatibleGameTypes) types += (types.empty() ? "" : ",") + t;
            // Description: CampaignSelect's chapter text (TransGame.int [<map> TnDataProvider_MapInfo] Description)
            c.rows.push_back({std::to_string(m.mapId), m.mapFilename, m.friendlyName, m.imagePath, types, std::to_string(m.faction),
                              std::to_string(m.presenceId), cat_.localize("TransGame", m.mapFilename + " TnDataProvider_MapInfo", "Description")});
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
        // PlayerLevel<Specialty> / PlayerLevel (the sum): the local player's from the profile's progression; other
        // participants' levels are not provided yet (0) [RE MP_PROGRESSION s4].
        const ProgressionState& pr = flow_.profile().progression;
        const std::string prime = pr.prime ? "1" : "0";
        const bool online = flow_.sessionRole() != GameFlow::SessionRole::Offline;
        const std::string pcTag = online ? cat_.localizeKey("$WfcPC.Online.PlatformPC") : std::string();
        // An online human's row (D11 platform tag; HeadsetState, the original column, 3.12: a muted member shows at most
        // "voice on"; a proximity speaker gets the "Near" tag, which Hud_GFX's talker list shows).
        auto onlineRow = [&](const GameFlow::OnlineMember& m, std::string& headset) {
            const std::string nm = (m.info.displayName.empty() ? std::string("Player") : m.info.displayName) + "  " +
                                   (m.info.platform == SessionPlatform::Web ? cat_.localizeKey("$WfcPC.Online.PlatformWeb") : pcTag);
            const GameFlow::VoiceState vs = flow_.voiceStateOf(m.info.id);
            const int hs = flow_.isMuted(m.info.id) ? std::min(vs.headsetState, 1) : vs.headsetState;
            headset = std::to_string(hs);
            return hs == 2 && vs.via == 2 ? nm + "  " + cat_.localizeKey("$WfcPC.Voice.VoiceNearTag") : nm;
        };
        const std::string localName = online ? playerName() + "  " + pcTag : playerName();
        const std::string localHeadset = online ? std::to_string(flow_.localHeadsetState()) : std::string("0");
        if (live && !mv.players.empty()) {
            // Gameplay's match roster (local player first as in GRI order of joining).
            for (const MatchValues::Player& p : mv.players) {
                if (p.directorSpawned) continue;   // Escalation's horde slots are not players (the original lists PRIs only)
                const GameFlow::OnlineMember* om = online && p.remote ? flow_.memberForPlayer(p) : nullptr;
                if (om) {   // another peer's human: its row as in the lobby, with the match's score
                    std::string headset;
                    const std::string nm = onlineRow(*om, headset);
                    c.rows.push_back({nm, std::to_string(p.team), teamNameOf(p.team), std::to_string(p.score), std::to_string(p.kills),
                                      std::to_string(p.deaths), p.dead ? "1" : "0", headset, "", std::to_string(p.power), "0", "0", "", "", "0", "0", "0", "0",
                                      std::to_string(p.level ? p.level : om->info.level)});
                } else if (p.local)
                    c.rows.push_back({localName, std::to_string(p.team), teamNameOf(p.team), std::to_string(p.score),
                                      std::to_string(p.kills), std::to_string(p.deaths), p.dead ? "1" : "0", localHeadset, "", std::to_string(p.power), "0", prime, "", "",
            std::to_string(progression::levelForXp(pr.xp[3])), std::to_string(progression::levelForXp(pr.xp[1])),
                          std::to_string(progression::levelForXp(pr.xp[0])), std::to_string(progression::levelForXp(pr.xp[2])),
                          std::to_string(progression::playerLevel(pr))});
                else {
                    // Bots (PC ADAPTATION: the original's scoreboard provider hides bBot PRIs): Gameplay's generated level
                    // is the summed player level; the played specialty's column shows a quarter of it (0-25).
                    std::string lv[4] = {"0", "0", "0", "0"};   // Leader, Scientist, Scout, Soldier (column order)
                    const int sp = progression::specialtyIndex(p.specialty);   // 0 Scout 1 Scientist 2 Soldier 3 Leader
                    static const int kCol[4] = {2, 1, 3, 0};
                    if (sp >= 0) lv[kCol[sp]] = std::to_string(std::clamp((p.level + 2) / 4, 0, 25));
                    c.rows.push_back({p.name, std::to_string(p.team), teamNameOf(p.team), std::to_string(p.score),
                                      std::to_string(p.kills), std::to_string(p.deaths), p.dead ? "1" : "0",
                                      "0", "", std::to_string(p.power), "0", "0", "", "", lv[0], lv[1], lv[2], lv[3], std::to_string(p.level)});
                }
                c.enabled.push_back(true);
            }
            return true;
        }
        c.rows.push_back({localName, std::to_string(team), teamName, std::to_string(live ? mv.score : 0),
                          std::to_string(live ? mv.kills : 0), std::to_string(live ? mv.deaths : 0), live && mv.dead ? "1" : "0",
                          localHeadset, "", "0", "0", prime, "", "",
            std::to_string(progression::levelForXp(pr.xp[3])), std::to_string(progression::levelForXp(pr.xp[1])),
                          std::to_string(progression::levelForXp(pr.xp[0])), std::to_string(progression::levelForXp(pr.xp[2])),
                          std::to_string(progression::playerLevel(pr))});
        c.enabled.push_back(true);
        if (online) {   // online lobby: the other humans
            for (const GameFlow::OnlineMember& m : flow_.onlineMembers()) {
                std::string headset;
                const std::string shown = onlineRow(m, headset);
                c.rows.push_back({shown, std::to_string(m.team), teamNameOf(m.team), "0", "0", "0", "0", headset, "", "0",
                                  m.connecting ? "1" : "0", "0", "", "", "0", "0", "0", "0", std::to_string(m.info.level)});
                c.enabled.push_back(true);
            }
        }
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
        if (const GameFlow::BotOption* o = GameFlow::botOption(field)) {   // PC ADAPTATION: values 0..max in order
            c.columns = {o->name};
            c.headers = {o->header};
            for (int v = 0; v <= std::max(0, flow_.botMax(o->field)); ++v) { c.rows.push_back({flow_.botChoiceLabel(o->field, v)}); c.enabled.push_back(true); }
            return true;
        }
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
            // PC ADAPTATION (user decision, Milestone E): the originally unnamed LockedChassis get names - Car8 / Car9 /
            // Car10 the campaign role name AICharacterNames.SoldierMG "Machine Gunner" (their ObjectPath is SoldierCarMG*),
            // the minions their FriendlyIconicName - and the neutral Car10 (FactionRestriction 3, which the original picker's
            // faction filter never matches) is listed once per faction. Inert until a body is unlocked (XP table).
            const bool chassis = k->second == "Chassis";
            const auto colOf = [&](const char* name) { auto it = std::find(c.columns.begin(), c.columns.end(), name); return it == c.columns.end() ? -1 : (int)(it - c.columns.begin()); };
            const int cId = colOf("UniqueId"), cName = colOf("FriendlyName"), cFaction = colOf("FactionRestriction");
            for (const auto& p : list) {
                std::vector<std::string> row;
                for (const std::string& col : c.columns) row.push_back(p.get(col));
                if (chassis && cId >= 0 && cName >= 0 && row[(size_t)cName].empty()) {
                    const std::string& id = row[(size_t)cId];
                    if (id == "Car8" || id == "Car9" || id == "Car10")
                        row[(size_t)cName] = cat_.localize("TransGame", "AICharacterNames", "SoldierMG");
                    else if (id.rfind("Minion", 0) == 0)
                        row[(size_t)cName] = p.get("FriendlyIconicName");
                }
                if (chassis && cId >= 0 && cFaction >= 0 && row[(size_t)cId] == "Car10" && row[(size_t)cFaction] == "3") {
                    for (const char* f : {"0", "1"}) { row[(size_t)cFaction] = f; c.rows.push_back(row); c.enabled.push_back(true); }
                    continue;
                }
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
    core::prof::Scope prof("ds.call");
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
    if (fn == "WriteValue" && m == "<PCSettings:Upscaling>") {   // PC EXTENSION; applied / saved by Game.ApplyProfileSettings
        flow_.profile().display.upscaling = std::clamp(std::atoi(arg(1).c_str()), 0, LocalProfile::Display::kUpscalingMax);
        FlowTrace::emit("settings.upscaling", {{"mode", std::to_string(flow_.profile().display.upscaling)}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:Anisotropy>") {   // PC EXTENSION
        flow_.profile().display.anisotropy = LocalProfile::Display::clampAnisotropy(std::atoi(arg(1).c_str()));
        FlowTrace::emit("settings.anisotropy", {{"x", std::to_string(flow_.profile().display.anisotropy)}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:FrameGeneration>") {   // PC EXTENSION; saved now, applied at the next launch
        flow_.profile().display.frameGeneration = arg(1) == "1" || arg(1) == "true" ? 1 : 0;
        FlowTrace::emit("settings.frameGeneration", {{"on", FlowTrace::boolean(flow_.profile().display.frameGeneration != 0)},
                                                     {"applies", "next launch"}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:VoiceMicTest>") return {};   // read-only meter
    if (fn == "WriteValue" && m.rfind("<PCSettings:Voice", 0) == 0 || (fn == "WriteValue" && m == "<PCSettings:ProximityVolume>")) {   // voice rows
        LocalProfile::Voice& v = flow_.profile().voice;
        const std::string a = arg(1);
        if (m == "<PCSettings:VoiceMode>") v.mode = std::clamp(std::atoi(a.c_str()), 0, 2);
        else if (m == "<PCSettings:VoiceAudience>") v.audience = std::clamp(std::atoi(a.c_str()), 0, 3);
        else if (m == "<PCSettings:VoiceDevice>") v.device = a;
        else if (m == "<PCSettings:VoicePushToTalkKey>") { if (a == "__custom") flow_.beginPttCapture(); else v.pttKey = a; }
        else if (m == "<PCSettings:VoiceVolume>") v.volume = std::clamp(std::atoi(a.c_str()), 0, 100);
        else if (m == "<PCSettings:ProximityVolume>") v.proximityVolume = std::clamp(std::atoi(a.c_str()), 0, 100);
        FlowTrace::emit("settings.voice", {{"markup", m}, {"value", a}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:VolumetricLighting>") {   // PC EXTENSION; applied by Game.ApplyProfileSettings
        flow_.profile().display.volumetricLighting = arg(1) == "1" || arg(1) == "true";
        FlowTrace::emit("settings.volumetricLighting", {{"on", FlowTrace::boolean(flow_.profile().display.volumetricLighting)},
                                                        {"provenance", "PC ADAPTATION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<WfcLobby:GameMode>") {   // lobby flow v2: the host changes the mode in the lobby
        const bool ok = flow_.changeLobbyMode(arg(1));
        FlowTrace::emit("lobby.gameModeRow", {{"mode", arg(1)}, {"applied", FlowTrace::boolean(ok)}});
        invalidate();
        return {};
    }
    if (fn == "WriteValue" && (m == "<PCSettings:Smoke>" || m == "<PCSettings:Water>")) {   // PC EXTENSION: Original / Enhanced
        int& v = m == "<PCSettings:Smoke>" ? flow_.profile().display.smoke : flow_.profile().display.water;
        v = std::clamp(std::atoi(arg(1).c_str()), 0, 1);
        FlowTrace::emit(m == "<PCSettings:Smoke>" ? "settings.smoke" : "settings.water", {{"mode", std::to_string(v)}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:LensFlares>") {   // PC EXTENSION (user): Off / Subtle / Original; the renderer follows
        flow_.profile().display.lensFlares = std::clamp(std::atoi(arg(1).c_str()), 0, 2);
        FlowTrace::emit("settings.lensFlares", {{"mode", std::to_string(flow_.profile().display.lensFlares)}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:ShowFps>") {   // PC EXTENSION; the corner readout follows at once
        flow_.profile().display.showFps = arg(1) == "1" || arg(1) == "true";
        FlowTrace::emit("settings.showFps", {{"on", FlowTrace::boolean(flow_.profile().display.showFps)}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:RayTracing>") {   // PC EXTENSION (the row shows only where it can run)
        flow_.profile().display.rayTracing = std::clamp(std::atoi(arg(1).c_str()), 0, LocalProfile::Display::kRayTracingMax);
        FlowTrace::emit("settings.rayTracing", {{"mode", std::to_string(flow_.profile().display.rayTracing)}, {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:HDTextures>") {   // PC EXTENSION (the row offers only the installed levels)
        // Commit writes every row: only a changed value counts as the user's choice.
        LocalProfile::Display& d = flow_.profile().display;
        const int level = arg(1) == "true" ? 1 : std::clamp(std::atoi(arg(1).c_str()), 0, LocalProfile::Display::kHdTexturesMax);
        if (level != d.hdTextures) d.hdTexturesUserSet = true;
        d.hdTextures = level;
        FlowTrace::emit("settings.hdTextures", {{"level", std::to_string(d.hdTextures)}, {"userSet", FlowTrace::boolean(d.hdTexturesUserSet)},
                                                {"provenance", "PC EXTENSION"}});
        return {};
    }
    if (fn == "WriteValue" && m == "<PCSettings:FrameLimit>") {   // applied (and saved) by Game.ApplyProfileSettings
        flow_.profile().display.frameLimit = std::max(0, std::atoi(arg(1).c_str()));
        FlowTrace::emit("settings.frameLimit", {{"hz", std::to_string(flow_.profile().display.frameLimit)}, {"provenance", "PC ADAPTATION"}});
        return {};
    }
    if (fn == "WriteValue" && m.rfind("<OnlinePlayerData:ProfileData.", 0) == 0 && m.size() > 31) {
        flow_.profile().set(m.substr(30, m.size() - 31), arg(1));
        return {};
    }
    if (fn == "WriteValue" && m.rfind("<TnGameSettings:", 0) == 0 && flow_.sessionRole() == GameFlow::SessionRole::Member) {
        FlowTrace::emit("online.memberIgnored", {{"fn", fn}, {"markup", m}});   // the host's options (online member)
        return {};
    }
    if (fn == "WriteValue" && m.rfind("<TnGameSettings:", 0) == 0 && m.size() > 17)
        if (const GameFlow::BotOption* o = GameFlow::botOption(m.substr(16, m.size() - 17))) {
            // Create Game writes every row's label: only a changed value is applied (an unchanged count must not count
            // as the player's edit, which would stop the map-aware Extended counts).
            for (int v = 0; v <= std::max(0, flow_.botMax(o->field)); ++v)
                if (flow_.botChoiceLabel(o->field, v) == arg(1)) {
                    if (v != flow_.botValue(o->field)) flow_.setBotSetting(o->field, v);
                    break;
                }
            invalidate();
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
    if (fn.rfind("Write", 0) == 0 || fn.rfind("Set", 0) == 0) invalidate();
    bool isColl = false;
    const Collection& c = cachedCollection(m, isColl);
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
        // PC ADAPTATION (user playtest 2026-10-13): the Private Match bot settings follow the mode's own options.
        if (m == "TnGameSettings" && flow_.currentSettings())
            for (const GameFlow::BotOption& o : flow_.botOptions()) out += (out.empty() ? "" : ",") + std::string(o.name);
        return BridgeValue(out);
    }
    FlowTrace::emit("datastore.unhandled", {{"fn", fn}, {"markup", m}});
    return {};
}

BridgeValue DataStores::readCell(const std::string& m, const std::string& col, const std::string& rowStr, bool asBool) {
    core::prof::Scope prof("ds.call");
    bool isColl = false;
    const Collection& c = cachedCollection(m, isColl);
    const size_t row = (size_t)std::atoi(rowStr.c_str());
    for (size_t i = 0; i < c.columns.size(); ++i)
        if (c.columns[i] == col && row < c.rows.size()) {
            if (asBool) return BridgeValue(c.rows[row][i] == "1" || c.rows[row][i] == "true");
            return BridgeValue(c.rows[row][i]);
        }
    if (isColl) FlowTrace::emit("datastore.unhandled", {{"fn", asBool ? "ReadCollectionBoolValue" : "ReadCollectionValue"}, {"markup", m}, {"column", col}});
    return BridgeValue(std::string());
}

const DataStores::Collection& DataStores::cachedCollection(const std::string& markup, bool& ok) {
    // std::map nodes are stable: the last entry is reused while the same markup is read (cells of one collection).
    CachedCollection& e = lastColl_ && markup == lastCollMarkup_ ? *lastColl_ : collCache_[markup];
    lastColl_ = &e;
    if (lastCollMarkup_ != markup) lastCollMarkup_ = markup;
    if (e.gen != frameGen_) { e.c = Collection(); e.ok = collection(markup, e.c); e.gen = frameGen_; }
    ok = e.ok;
    return e.c;
}

std::vector<DataStores::Change> DataStores::poll() {
    ++frameGen_;   // a new frame: collections are rebuilt on their next read
    std::vector<Change> out;
    for (Reg& r : regs_) {
        std::string v = read(r.markup);
        if (v != r.last) { r.last = v; out.push_back({r.movie, r.markup, r.callback, v}); }
    }
    return out;
}

} // namespace frontend
