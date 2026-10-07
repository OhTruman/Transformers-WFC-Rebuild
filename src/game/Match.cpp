#include "game/Match.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace game {

const char* matchStateName(Match::State s) {
    switch (s) {
        case Match::State::None: return "None"; case Match::State::PendingMatch: return "PendingMatch";
        case Match::State::InProgress: return "InProgress"; case Match::State::MatchOver: return "MatchOver";
        case Match::State::Returned: return "Returned";
    }
    return "?";
}

// TnOnlineGameSettings<tag> defaults [CONF authored.db]: TDM PointsToWin 40, DM 20; TimeLimit index 1 of
// TimeLimits (600, 900, 1200) = 900 s for both.
MatchSettings MatchSettings::forMode(const std::string& tag) {
    MatchSettings s;
    s.modeTag = tag;
    // DOM (Conquest) / KOTH (Power Struggle): PointsToWin 400, TimeLimit 900; rules ScoreKillsMP (no team score per kill),
    // ReportGameProgressTime + Points [CONF authored TnOnlineGameSettings + RE PLAYTEST section 3].
    if (tag == "DM") { s.teamGame = false; s.goalScore = 20; s.timeLimit = 900; s.gameType = "TNGT_DM"; }
    else if (tag == "DOM") { s.teamGame = true; s.goalScore = 400; s.timeLimit = 900; s.gameType = "TNGT_DOM"; s.teamScoreAmount = 0; s.reportKills = false; s.reportPoints = true; }
    // CTF (Code of Power): rounds 2 (2/4/6), TimeLimit per round 300, capture IndividualScore 10 + team 1, ScoreKillsMP.
    // EXT (Countdown to Extinction): PointsToWin 3 (1/3/5), TimeLimit 900, detonation IndividualScore 10 + team 1
    // [CONF RE MILESTONE05_PLAYTEST_RE §3].
    else if (tag == "CTF") { s.teamGame = true; s.goalScore = 1000000; s.timeLimit = 300; s.gameType = "TNGT_CTF"; s.teamScoreAmount = 0;
                             s.reportKills = false; s.reportPoints = false; s.objectiveIndividualScore = 10; s.rounds = 2; s.singleFlagCTF = true; }
    else if (tag == "EXT") { s.teamGame = true; s.goalScore = 3; s.timeLimit = 900; s.gameType = "TNGT_EXT"; s.teamScoreAmount = 0;
                             s.reportKills = false; s.reportPoints = false; s.objectiveIndividualScore = 10; }
    else if (tag == "KOTH") { s.teamGame = true; s.goalScore = 400; s.timeLimit = 900; s.gameType = "TNGT_KOTH"; s.teamScoreAmount = 0; s.reportKills = false; s.reportPoints = true; s.objectiveIndividualScore = 1; }
    else { s.modeTag = "TDM"; s.teamGame = true; s.goalScore = 40; s.timeLimit = 900; s.gameType = "TNGT_TDM"; }
    return s;
}

bool Match::loadSpawnData(const std::string& path) {
    starts_.clear(); clusters_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    assets::Json g;
    if (!assets::Json::parse(ss.str(), g)) return false;
    const assets::Json& ps = g["player_starts"];
    for (size_t i = 0; i < ps.size(); ++i) {
        const assets::Json& L = ps[i]["location_gltf"];
        if (L.size() < 3) continue;
        Start s;
        s.actor = ps[i]["actor"].asString();
        s.cluster = ps[i]["clusters"][0].asString();
        s.ffa = ps[i]["class"].asString() == "TnFreeForAllPlayerStart";
        const std::string& team = ps[i]["effective"]["Team"].asString();
        s.team = s.ffa ? 255 : (team == "TNTG_Decepticons" ? 1 : 0);   // default team = Autobots
        s.pos = {L[0].asFloat(), L[1].asFloat(), L[2].asFloat()};
        float ueYaw = ps[i]["yaw_deg"].asFloat() * 0.01745329252f;
        s.yaw = std::atan2(-std::cos(ueYaw), -std::sin(ueYaw));
        starts_.push_back(s);
    }
    auto shortName = [](const std::string& a) { size_t d = a.rfind('.'); return d == std::string::npos ? a : a.substr(d + 1); };
    const assets::Json& cl = g["spawn_clusters"];
    for (size_t i = 0; i < cl.size(); ++i) {
        const assets::Json& a = cl[i]["authored"];
        // TnSpawnPointManager.Initialize registers a cluster when ActiveGameTypes is empty or contains the game type;
        // every Streets cluster is empty or [TNGT_TDM], so all 12 register in TDM [CONF RE §5.3].
        Cluster c;
        c.actor = cl[i]["actor"].asString();
        const assets::Json& cp = a["_CenterPoint"];
        c.center = {cp["X"].asFloat() * 0.01f, cp["Z"].asFloat() * 0.01f, cp["Y"].asFloat() * 0.01f};
        c.initialSpawn = a["InitialSpawn"].asBool(false);
        for (size_t k = 0; k < a["ActiveGameTypes"].size(); ++k) c.activeGameTypes.push_back(a["ActiveGameTypes"][k].asString());
        for (size_t k = 0; k < a["SpawnPoints"].size(); ++k) {
            std::string sp = shortName(a["SpawnPoints"][k].asString());
            for (size_t j = 0; j < starts_.size(); ++j) if (starts_[j].actor == sp) c.spawnPoints.push_back((int)j);
        }
        if (!c.spawnPoints.empty()) c.faction = starts_[(size_t)c.spawnPoints[0]].team;   // team of its first spawn point
        clusters_.push_back(c);
    }
    LOG_INFO("match: %zu player starts, %zu spawn clusters", starts_.size(), clusters_.size());
    return !starts_.empty();
}

void Match::begin(const MatchSettings& s) {
    s_ = s;
    events_.clear();
    gevents_.clear();                 // a new match = a fresh record (serials continue: never reused within a session)
    // AcquiredKillstreaks are cleared at ClientGameEnded [CONF]: a new match starts with none (they leaked into the next match).
    for (MatchPlayer& p : players_) { p.objectiveScore = 0; p.bestKillStreak = 0; p.spawnTime = -1.0f; p.currentKillStreak = 0; p.acquiredKillstreaks.clear(); }
    teamScore_[0] = teamScore_[1] = 0;
    for (MatchPlayer& p : players_) { p.score = p.kills = p.deaths = 0; p.assists = 0.0f; p.alive = false; p.timeToRespawn = -1.0f; }
    tombstones_.clear();
    for (auto& h : damageHistory_) h.clear();
    killHistory_.clear();
    for (float& d : deathTime_) d = -1.0f;
    matchTime_ = 0.0f;
    endReason_.clear();
    winnerPlayer_ = -1;
    // InitGame: GoalScore = PointsToWin, TimeLimit (s); InitGameReplicationInfo: GRI.GoalScore, RemainingTime.
    remainingTime_ = std::max(0, s_.timeLimit);
    elapsedTime_ = 0;
    winnerTeam_ = -1;
    for (bool& a : announced_) a = false;
    for (bool& a : killsAnnounced_) a = false;
    // TnSpawnPointManager.Initialize: InitialSpawn clusters become each faction's first active cluster.
    for (Cluster& c : clusters_) {
        c.iterator = 0;   // fresh level: SpawnIterator 0
        c.registered = c.activeGameTypes.empty() || std::find(c.activeGameTypes.begin(), c.activeGameTypes.end(), s_.gameType) != c.activeGameTypes.end();
    }
    objMods_.clear();
    active_[0] = active_[1] = -1;
    for (size_t i = 0; i < clusters_.size(); ++i)
        if (clusters_[i].registered && clusters_[i].initialSpawn && (clusters_[i].faction == 0 || clusters_[i].faction == 1)) active_[clusters_[i].faction] = (int)i;
    uptime_[0] = uptime_[1] = 0.0f; clusterClock_ = 0.0f; sinceUpdate_ = 0.0f; usingInitialSpawn_ = true;
    // PendingMatch.BeginState: bWaitingToStartMatch, GRI.SetGameStatus(2), GRI.ResetCountdown(true, 10).
    state_ = State::PendingMatch;
    gameStatus_ = 2;
    countdown_ = (int)s_.matchAutoStartCountdown;
    secondAccum_ = 0.0f; stateTime_ = 0.0f;
    emit(MatchEvent::Type::CountdownTick, -1, countdown_);
    LOG_INFO("match: %s begin (goal %d, time limit %d s, countdown %d s)", s_.modeTag.c_str(), s_.goalScore, s_.timeLimit, countdown_);
}

int Match::addPlayer(const std::string& name, int team) {
    const int p = addPlayer(name);
    if (s_.teamGame && (team == 0 || team == 1)) players_[(size_t)p].team = team;
    return p;
}

int Match::addPlayer(const std::string& name) {
    MatchPlayer p;
    p.name = name;
    if (s_.teamGame) {
        // TnTeamHandlerTwoTeams.PickTeam(255): the smaller team; a tie -> RandomInt(2) [CONF].
        int n0 = 0, n1 = 0;
        for (const MatchPlayer& q : players_) { n0 += q.team == 0; n1 += q.team == 1; }
        p.team = n0 < n1 ? 0 : (n1 < n0 ? 1 : randomInt(2));
    }
    p.hasSelectedCharacter = true;   // local default until the frontend selection screen exists (Optimus, iconic "Truck")
    players_.push_back(p);
    locs_.push_back({0, 0, 0});
    spawnAt_.push_back(-1);
    damageHistory_.emplace_back();
    deathTime_.push_back(-1.0f);
    return (int)players_.size() - 1;
}

void Match::tick(float dt) {
    if (state_ == State::None || state_ == State::Returned) return;
    stateTime_ += dt;
    matchTime_ += dt;
    // GameReplicationInfo.Timer / GRI.DecrementCountdown: 1 s repeating timers.
    secondAccum_ += dt;
    while (secondAccum_ >= 1.0f) { secondAccum_ -= 1.0f; secondTimer(); }
    if (state_ == State::InProgress) {
        // TnRespawnHelperDelayed.OnTick: TimeToRespawn -= dt; <= 0 -> Respawn = Game.RestartPlayer.
        for (size_t i = 0; i < players_.size(); ++i) {
            MatchPlayer& p = players_[i];
            if (p.alive || p.timeToRespawn < 0.0f) continue;
            p.timeToRespawn -= dt;
            if (p.timeToRespawn <= 0.0f) restartPlayer((int)i);
        }
        if (s_.teamGame) updateClusters(dt);
        if (s_.rounds > 0) tickRounds(dt);
    } else if (state_ == State::MatchOver && stateTime_ >= s_.matchOverCountdown) {
        // MatchOver.OnCountdownComplete -> TnGame.ReturnToGameLobby (ServerTravel to the lobby URL).
        state_ = State::Returned;
        emit(MatchEvent::Type::ReturnToLobby);
    }
}

void Match::secondTimer() {
    if (state_ == State::PendingMatch) {
        if (countdown_ > 0) { --countdown_; emit(MatchEvent::Type::CountdownTick, -1, countdown_); }
        if (countdown_ <= 0) startMatch();                       // ReadyToStartMatch = HasCountdownExpired
        return;
    }
    if (state_ != State::InProgress) return;
    ++elapsedTime_;
    if (s_.rounds > 0) return;                                   // RoundsBase: RunGameTimer false, the round timer runs
    if (remainingTime_ > 0) {                                    // bStopCountDown false: rules RunGameTimer
        --remainingTime_;
        // TnGameRules_ReportGameProgressTime: 30 / 60 / 120 s -> switch 0 / 1 / 2; 60 also NotifyGameNearlyComplete.
        const int at[3] = {30, 60, 120};
        for (int k = 0; k < 3; ++k)
            if (remainingTime_ == at[k] && !announced_[k]) {
                announced_[k] = true;
                emit(MatchEvent::Type::TimeAnnouncement, -1, k);
                if (k == 1) emit(MatchEvent::Type::GameNearlyComplete);
            }
        if (remainingTime_ <= 0) endGame(-1, "");                // OnRemainingTimeUpdated -> EndGame(none, "")
    }
}

void Match::startMatch() {
    // TnTeamGame.StartMatch: Reset() pawns / dropped pickups / pickup factories (host), InProgress.BeginState:
    // ElapsedTime 0, GRI.SetGameStatus(3), ResetPlayerStats, UI event 3, GTMT_StartGame.
    state_ = State::InProgress;
    gameStatus_ = 3;
    elapsedTime_ = 0;
    stateTime_ = 0.0f;
    if (s_.rounds > 0) {
        // RoundsBase.MatchStarting -> Active: TimeTillNextReset = TimeLimit; SingleFlagCTF: AttackingTeam = RandomInt(2).
        currentRound_ = 0; betweenRounds_ = false; roundTimeLeft_ = (float)s_.timeLimit;
        if (s_.singleFlagCTF) attackingTeam_ = std::rand() % 2;
        remainingTime_ = s_.timeLimit;
    }
    emit(MatchEvent::Type::MatchStarted);
    recordEvent(GameplayEventType::MatchStart);
    if (s_.rounds > 0) emit(MatchEvent::Type::RoundStarted, -1, attackingTeam_);
    // SpawnHelper: RespawnHelper.InitialSpawn -> Wave TimeToAllowInstantInitialSpawns -1: always immediate.
    spawnAllInitial();
}

// The initial spawn of everyone (match start, CTF round start). Above 16 participants (CUSTOM-GAME EXTENSION; PC ADAPTATION) bots are
// released one per simulation step (63 bots over ~1 s) instead of all in one frame; the local human and the original counts spawn at once.
void Match::spawnAllInitial() {
    const bool stagger = players_.size() > 16;
    int k = 0;
    for (size_t i = 0; i < players_.size(); ++i) {
        if (stagger && players_[i].kind == ParticipantKind::Bot) { players_[i].alive = false; players_[i].timeToRespawn = (float)(++k) / 60.0f; continue; }
        restartPlayer((int)i);
    }
}

void Match::killed(int killer, int victim, bool suicide, const std::string& damageType, const KillContext* ctx) {
    if (state_ != State::InProgress || victim < 0 || (size_t)victim >= players_.size()) return;   // MatchOver: no-ops
    if (killer >= (int)players_.size()) killer = -1;
    MatchPlayer& V = players_[(size_t)victim];
    if (!V.alive) return;
    // Snapshots BEFORE the streaks change (EndKillStreak tests the victim's streak; KillStreak the killer's new count).
    const ParticipantSnapshot victimSnap = snapshot(victim);
    const ParticipantSnapshot killerSnap = killer >= 0 ? snapshot(killer) : ParticipantSnapshot{};
    const int scoreBefore = killer >= 0 ? players_[(size_t)killer].score : 0;
    const int teamBefore = killer >= 0 ? teamScore(players_[(size_t)killer].team) : 0;
    std::string streakEarned;
    V.currentKillStreak = 0;                                      // AddDeaths -> KillStreakEnded
    // TnMultiplayerGame.Killed: TnTombstone at the victim (team set) - a spawn modifier.
    tombstones_.push_back({locs_[(size_t)victim], V.team});
    const bool killedSelf = killer == victim;
    // ScoreKills(TDM|DM).ScoreKill: not for DmgType_Suicided; needs a killer; AddScore(self ? 0 : 1, self ? 0 : TeamScoreAmount).
    // ShouldScoreKill: the original never scores an AI victim; botVictimsScore (PC ADAPTATION) lets bots score as players.
    const bool victimScores = V.kind != ParticipantKind::Bot || s_.botVictimsScore;
    if (!suicide && killer >= 0 && victimScores) {
        MatchPlayer& K = players_[(size_t)killer];
        if (!killedSelf) {
            K.score += 1;
            if (s_.teamGame && s_.teamScoreAmount > 0 && (K.team == 0 || K.team == 1)) teamScore_[K.team] += 1;   // Team.AddScore(1)
            K.kills += 1;                                         // TrackKillsMP: +1 -> AddKills
            // AddKills: _CurrentKillStreak += 1; TnKillStreakAnnouncer -> UpdateKillstreakRewards(count) -> AcquireKillstreak
            // (no duplicates) [CONF script].
            K.currentKillStreak += 1;
            if (const KillstreakDef* ks = findKillstreak(K.specialty, K.currentKillStreak))
                if (std::find(K.acquiredKillstreaks.begin(), K.acquiredKillstreaks.end(), ks->id) == K.acquiredKillstreaks.end()) {
                    K.acquiredKillstreaks.push_back(ks->id);
                    streakEarned = ks->id;
                }
            K.bestKillStreak = std::max(K.bestKillStreak, K.currentKillStreak);
        }
    }
    // TrackKillsMP.ScoreAssists: the first damager in the victim's DamageHistory that is neither killer nor victim
    // gets AddAssist(damage / HealthMax).
    int assister = -1; float assistFrac = 0.0f;
    for (const auto& [who, dmg] : damageHistory_[(size_t)victim])
        if (who >= 0 && who != killer && who != victim) { assistFrac = dmg / V.healthMax; players_[(size_t)who].assists += assistFrac; assister = who; break; }
    damageHistory_[(size_t)victim].clear();
    V.deaths += 1;                                                // PRI.AddDeaths(1)
    V.alive = false;
    {
        KillFeedEntry k;
        k.time = matchTime_;
        k.messageSwitch = (killer < 0 || killer == victim || suicide) ? 1 : 0;
        k.killer = k.messageSwitch == 0 ? killer : -1;
        k.victim = victim;
        k.killerTeam = killer >= 0 ? players_[(size_t)killer].team : 255;
        k.victimTeam = V.team;
        k.damageType = suicide ? "Engine.DmgType_Suicided" : (damageType.empty() ? (killer < 0 ? "Engine.DmgType_Fell" : "") : damageType);
        killHistory_.push_back(k);
        deathTime_[(size_t)victim] = matchTime_;
    }
    emit(MatchEvent::Type::PlayerKilled, victim, 0, suicide ? "suicide" : (killer < 0 ? "environment" : ""), killer);
    {
        // The authoritative death record (one per death) + the assist + an acquired killstreak.
        const GameplayEventType t = (suicide || killedSelf) ? GameplayEventType::Suicide
                                  : killer < 0 ? GameplayEventType::EnvironmentDeath : GameplayEventType::Kill;
        GameplayEvent& e = recordEvent(t, killer, victim);
        e.instigatorState = killerSnap; e.victimState = victimSnap;
        e.damageType = suicide ? "Engine.DmgType_Suicided" : (damageType.empty() ? (killer < 0 ? "Engine.DmgType_Fell" : "") : damageType);
        if (ctx) {
            e.weapon = ctx->weapon; e.melee = ctx->melee; e.headshot = ctx->headshot; e.backstab = ctx->backstab;
            e.ability = ctx->ability; e.killAfterDeath = ctx->killAfterDeath; e.distanceUU = ctx->distanceUU;
        }
        if (killer >= 0) {
            e.personalScore = players_[(size_t)killer].score - scoreBefore;
            e.teamScore = teamScore(players_[(size_t)killer].team) - teamBefore;
        }
        if (t == GameplayEventType::Kill && assister >= 0) {
            GameplayEvent& a = recordEvent(GameplayEventType::Assist, assister, victim);
            a.instigatorState = snapshot(assister); a.victimState = victimSnap;
            a.assistFraction = assistFrac; a.damageType = e.damageType;
        }
        if (!streakEarned.empty()) {
            GameplayEvent& k = recordEvent(GameplayEventType::KillstreakEarned, killer);
            k.text = streakEarned; k.value = players_[(size_t)killer].currentKillStreak;
        }
    }
    if (s_.reportKills && killer >= 0 && !suicide && !killedSelf) {
        // ReportGameProgressKills.HandleProgress: NumScoresLeft = GoalScore - TeamScore (1/3/5 -> switch 5/6/7).
        const MatchPlayer& K = players_[(size_t)killer];
        int score = s_.teamGame ? teamScore(K.team) : K.score;
        int left = s_.goalScore - score;
        const int at[3] = {1, 3, 5};
        for (int k = 0; k < 3; ++k)
            if (left == at[k]) { emit(MatchEvent::Type::KillsLeftAnnouncement, killer, 5 + k); if (k == 2) emit(MatchEvent::Type::GameNearlyComplete); }
    }
    // CheckScore (killer present): TnTeamGame.GetCurrentScore = PRI.Team.GetScore(); >= GoalScore -> EndGame("Score").
    if (killer >= 0) {
        const MatchPlayer& K = players_[(size_t)killer];
        int score = s_.teamGame ? teamScore(K.team) : K.score;
        if (s_.goalScore > 0 && score >= s_.goalScore) { endGame(killer, "Score"); }
    }
    // RespawnHelper.Died -> OnDeath: TimeToRespawn = ShouldInstantRespawn ? 0 : WaveRespawnTime (5 s).
    if (state_ == State::InProgress) V.timeToRespawn = s_.waveRespawnTime;
}

std::vector<KillFeedEntry> Match::killFeed() const {
    std::vector<KillFeedEntry> v;
    for (const KillFeedEntry& k : killHistory_) if (matchTime_ - k.time < KillFeedEntry::kLifetime + KillFeedEntry::kFade) v.push_back(k);
    return v;
}

bool Match::spectating(int p) const {
    if (p < 0 || (size_t)p >= players_.size() || players_[(size_t)p].alive || deathTime_[(size_t)p] < 0.0f) return false;
    return matchTime_ - deathTime_[(size_t)p] >= kMinRespawnDelay;
}

void Match::checkScore(int player, int team) {
    if (state_ != State::InProgress) return;
    int score = s_.teamGame ? teamScore(team) : (player >= 0 ? players_[(size_t)player].score : 0);
    if (s_.goalScore > 0 && score >= s_.goalScore) endGame(player, "Score");
}

void Match::reportPoints(int player) {
    // ReportGameProgressPoints.HandleProgress (on ScoreObjective): NumScoresLeft = GoalScore - team score;
    // 50 -> switch 4, 25 -> switch 3 + NotifyGameNearlyComplete [CONF authored Sounds + bytecode].
    if (!s_.reportPoints || player < 0) return;
    int left = s_.goalScore - (s_.teamGame ? teamScore(players_[(size_t)player].team) : players_[(size_t)player].score);
    if (left == 50) emit(MatchEvent::Type::PointsLeftAnnouncement, player, 4);
    if (left == 25) { emit(MatchEvent::Type::PointsLeftAnnouncement, player, 3); emit(MatchEvent::Type::GameNearlyComplete); }
}

GameplayEvent& Match::recordEvent(GameplayEventType t, int instigator, int victim) {
    GameplayEvent e;
    e.serial = nextEventSerial_++;
    e.type = t; e.time = matchTime_; e.instigator = instigator; e.victim = victim;
    if (instigator >= 0 && (size_t)instigator < players_.size()) e.instigatorState = snapshot(instigator);
    if (victim >= 0 && (size_t)victim < players_.size()) e.victimState = snapshot(victim);
    gevents_.push_back(std::move(e));
    return gevents_.back();
}

ParticipantSnapshot Match::snapshot(int player) const {
    ParticipantSnapshot s;
    if (snapshot_) s = snapshot_(player);
    if (player < 0 || (size_t)player >= players_.size()) return s;
    const MatchPlayer& P = players_[(size_t)player];
    s.player = player; s.team = P.team; s.kind = P.kind; s.alive = P.alive; s.killStreak = P.currentKillStreak;
    s.spawnTime = P.spawnTime; s.level = P.level;
    if (s.specialty.empty()) s.specialty = P.specialty;
    if (s.chassis.empty()) s.chassis = P.chassis;
    return s;
}

void Match::recordObjective(const std::string& kind, int player, int team, int value) {
    GameplayEvent& e = recordEvent(GameplayEventType::Objective, player);
    e.objective = kind; e.value = value;
    if (team != 255) e.instigatorState.team = team;
    // The score each action carried (TnGameRules_ScoreObjectives / ScoreDomination; applied just before by the host).
    if (kind == "FlagCapture" || kind == "BombDetonate") { e.personalScore = s_.objectiveIndividualScore; e.teamScore = 1; }
    else if (kind == "NodeCapture") e.personalScore = 2;
}

void Match::scoreObjective(int player, int score) {
    if (state_ != State::InProgress || player < 0 || (size_t)player >= players_.size()) return;
    MatchPlayer& P = players_[(size_t)player];
    P.score += s_.objectiveIndividualScore;                 // Scorer.AddScore(IndividualScore, Score)
    P.objectiveScore += s_.objectiveIndividualScore;
    if (s_.teamGame && score > 0 && (P.team == 0 || P.team == 1)) {
        teamScore_[P.team] += score;
        LOG_INFO("MATCH score team=%d score=%d reason=objective player=%d", P.team, teamScore_[P.team], player);   // audit line (objective)
    }
    reportPoints(player);
    checkScore(player, P.team);
}

void Match::scoreTeamObjective(int team, int amount) {
    if (state_ != State::InProgress || (team != 0 && team != 1)) return;
    teamScore_[team] += amount;   // [HIGH: the TnTeamGame override is not in the decompiled set; RE section 3 C: +1 team / 3 s]
    LOG_INFO("MATCH score team=%d score=%d reason=%s", team, teamScore_[team], s_.modeTag == "KOTH" ? "zone" : "node");   // audit line (objective tick)
    if (s_.goalScore > 0 && teamScore_[team] >= s_.goalScore) endGame(-1, "Score");
}

void Match::addPersonalScore(int player, int amount) {
    if (state_ != State::InProgress || player < 0 || (size_t)player >= players_.size()) return;
    players_[(size_t)player].score += amount;
    players_[(size_t)player].objectiveScore += amount;
}

void Match::recordDamage(int victim, int instigator, float amount) {
    if (victim >= 0) { if (lastDamagedAt_.size() <= (size_t)victim) lastDamagedAt_.resize((size_t)victim + 1, -100.0f); lastDamagedAt_[(size_t)victim] = matchTime_; }
    if (victim < 0 || (size_t)victim >= players_.size() || amount <= 0.0f) return;
    auto& h = damageHistory_[(size_t)victim];
    for (auto& e : h) if (e.first == instigator) { e.second += amount; return; }
    h.push_back({instigator, amount});
}

void Match::endGame(int winnerPlayer, const std::string& reason) {
    if (state_ != State::InProgress) return;
    int winner = -1;
    if (s_.teamGame) {
        // TnVersusGame.PickWinningTeam: Autobots if A > D or D empty; Decepticons if D > A or A empty; else none (tie).
        int n0 = 0, n1 = 0;
        for (const MatchPlayer& p : players_) { n0 += p.team == 0; n1 += p.team == 1; }
        if (teamScore_[0] > teamScore_[1] || n1 == 0) winner = 0;
        else if (teamScore_[1] > teamScore_[0] || n0 == 0) winner = 1;
    } else if (winnerPlayer < 0) {
        // GetWinningPRI: highest score [HIGH: stock first-highest; tie handling PARTIAL].
        // GetWinningPRI: the top score; an equal top score is a draw (completion type 2) [CONF RE PLAYTEST §3].
        int best = -1; bool tie = false;
        for (size_t i = 0; i < players_.size(); ++i) {
            if (best < 0 || players_[i].score > players_[(size_t)best].score) { best = (int)i; tie = false; }
            else if (players_[i].score == players_[(size_t)best].score) tie = true;
        }
        winnerPlayer = tie ? -1 : best;
    }
    winnerTeam_ = winner;
    winnerPlayer_ = s_.teamGame ? -1 : winnerPlayer;
    endReason_ = reason;
    state_ = State::MatchOver;                                    // GotoState('MatchOver'): status 5, UI event 9
    gameStatus_ = 5;
    stateTime_ = 0.0f;
    emit(MatchEvent::Type::MatchEnded, winnerPlayer, winner, reason);
    {
        GameplayEvent& e = recordEvent(GameplayEventType::MatchEnd, winnerPlayer);
        e.value = winner; e.text = reason; e.winnerPlayer = winnerPlayer;
        // Completion per player (stats): 0 win, 1 loss, 2 draw. Team games: by the winning team (none = draw);
        // FFA: the winner wins, an equal top score draws [CONF RE PLAYTEST §3 / MP_PROGRESSION §1].
        int top = -1; for (const MatchPlayer& p : players_) top = std::max(top, p.score);
        int topCount = 0; for (const MatchPlayer& p : players_) topCount += p.score == top;
        e.completion.resize(players_.size(), 1);
        for (size_t i = 0; i < players_.size(); ++i) {
            if (s_.teamGame) e.completion[i] = winner < 0 ? 2 : (players_[i].team == winner ? 0 : 1);
            else e.completion[i] = (winnerPlayer < 0 && players_[i].score == top) ? 2 : ((int)i == winnerPlayer ? 0 : 1);
        }
        // GameWin XP quirk (TnVersusGame.CheckEndGame): every player on Winner.Team, where Winner is the PRI passed to EndGame
        // - on a time-limit end GetWinningPRI (top individual score, first found), even on a team tie. DM: no win/lose XP.
        if (s_.teamGame) {
            int w = winnerPlayer;
            if (w < 0) for (size_t i = 0; i < players_.size(); ++i) if (w < 0 || players_[i].score > players_[(size_t)w].score) w = (int)i;
            e.xpWinTeam = w >= 0 ? players_[(size_t)w].team : -1;
        }
        // MVP (achievement 37): every PRI tied for the top personal score, more than one player (versus: both teams present).
        int n0 = 0, n1 = 0; for (const MatchPlayer& p : players_) { n0 += p.team == 0; n1 += p.team == 1; }
        if (players_.size() > 1 && (!s_.teamGame || (n0 > 0 && n1 > 0)))
            for (size_t i = 0; i < players_.size(); ++i) if (players_[i].score == top) e.mvp.push_back((int)i);
    }
    LOG_INFO("match: EndGame reason \"%s\" score %d-%d winner team %d player %d", reason.c_str(), teamScore_[0], teamScore_[1], winner, winnerPlayer);
}

void Match::restartPlayer(int p) {
    MatchPlayer& P = players_[(size_t)p];
    if (!P.hasSelectedCharacter) { P.timeToRespawn = 0.1f; return; }   // CheckReadySpawn: no selection, no spawn (retry)
    P.chassis = resolveChassis(P.selection, faction(p));
    // CustomCharacterOptions.FindChassis: the chassis must resolve to a buildable body; no substitute exists.
    if (chassisCheck_) {
        std::string err;
        if (!chassisCheck_(P.chassis, err)) {
            if (P.spawnError != err) LOG_ERROR("MATCH spawn refused for %s: chassis %s unavailable (%s)", P.name.c_str(), P.chassis.c_str(), err.c_str());
            P.spawnError = err;
            P.timeToRespawn = 1.0f;
            return;
        }
    }
    P.spawnError.clear();
    int st = findPlayerStart(p);
    // Extended matches: no free point this step -> retry shortly instead of a placeless spawn (generated points normally cover it).
    if (st < 0 && s_.extendedSlots) { P.alive = false; P.timeToRespawn = 0.25f; return; }
    spawnAt_[(size_t)p] = st;
    P.alive = true;
    P.timeToRespawn = -1.0f;
    damageHistory_[(size_t)p].clear();
    if (st >= 0) { locs_[(size_t)p] = starts_[(size_t)st].pos; if ((size_t)p < radii_.size()) radii_[(size_t)p] = 2.0f; }
    P.spawnTime = matchTime_;
    emit(MatchEvent::Type::PlayerSpawned, p, st, st >= 0 ? starts_[(size_t)st].actor : std::string());
}

void Match::setGeneratedStarts(const std::vector<Start>& extra) {
    starts_.erase(std::remove_if(starts_.begin(), starts_.end(), [](const Start& s) { return s.generated; }), starts_.end());
    spawnAt_.assign(spawnAt_.size(), -1);
    for (Start s : extra) { s.generated = true; starts_.push_back(s); }
}

int Match::findPlayerStart(int p) {
    const MatchPlayer& P = players_[(size_t)p];
    // LocationValidator.IsSafeSpawnLocation is native [PARTIAL]: approximated as no other live player within 4 m. Extended matches
    // test the pawn capsule instead (both cylinders + 1 m horizontally while the 4 m tall cylinders overlap in height) [PC ADAPTATION].
    auto safe = [&](int si) {
        const core::Vec3& sp = starts_[(size_t)si].pos;
        for (size_t i = 0; i < players_.size(); ++i)
            if ((int)i != p && players_[i].alive) {
                core::Vec3 d = locs_[i] - sp;
                if (s_.extendedSlots) {
                    const float r = i < radii_.size() ? radii_[i] : 2.0f;   // the other pawn's current cylinder
                    // 1 m margin: neighbours keep moving during the spawn step (vehicles ~0.3 m per step)
                    if (std::fabs(d.y) < 4.5f && std::sqrt(d.x * d.x + d.z * d.z) < 2.0f + r + 1.0f) return false;
                } else if (core::length(d) < 4.0f) return false;
            }
        return true;
    };
    if (s_.teamGame && (P.team == 0 || P.team == 1) && active_[P.team] >= 0) {
        // FindSpawnPoint: the team's active cluster, up to MaxSpawnPointsToConsider 4 round-robin picks.
        Cluster& c = clusters_[(size_t)active_[P.team]];
        for (int k = 0; k < 4 && !c.spawnPoints.empty(); ++k) {
            int si = c.spawnPoints[(size_t)(c.iterator % (int)c.spawnPoints.size())];
            c.iterator = (c.iterator + 1) % (int)c.spawnPoints.size();
            if (safe(si)) return si;
        }
    }
    // Fallback (stock FindPlayerStart + TnTeamGame.RatePlayerStart: team starts of the player's team only; FFA starts
    // for the FFA game) [HIGH; the FFA spawn choice is not traced: first safe FFA start, PROV].
    for (size_t i = 0; i < starts_.size(); ++i) {
        const Start& s = starts_[i];
        if (s.generated) continue;
        bool ok = s_.teamGame ? (!s.ffa && s.team == P.team) : s.ffa;
        if (ok && safe((int)i)) return (int)i;
    }
    // CUSTOM-GAME EXTENSION (33 participants): FFA maps author 10-27 FFA starts (MaxPlayers 10), so once they are all occupied any
    // authored start serves (AssetTools spawn_capacity: 50-120 per map) [PC ADAPTATION].
    if (!s_.teamGame && s_.extendedSlots)
        for (size_t i = 0; i < starts_.size(); ++i) if (!starts_[i].generated && safe((int)i)) return (int)i;
    // Extended team games (32 per side; maps author >= 20 team starts per side): the FFA starts next, never the other team's;
    if (s_.teamGame && s_.extendedSlots)
        for (size_t i = 0; i < starts_.size(); ++i) if (!starts_[i].generated && starts_[i].ffa && safe((int)i)) return (int)i;
    // Then the generated spawn points (own team's near its start area; FFA: any), each kept 4 m (two pawn radii) clear of live pawns.
    if (s_.extendedSlots)
        for (size_t i = 0; i < starts_.size(); ++i)
            if (starts_[i].generated && (!s_.teamGame || starts_[i].team == P.team) && safe((int)i)) return (int)i;
    return -1;
}

// TnSpawnPointManager.UpdateClusters (UpdateInterval 0.1 s): initial clusters locked for InitialSpawnTime 15 s; then
// each faction scores every cluster (TDM is FactionNeutral: all clusters) and switches when TopScore - CurrentScore
// >= EarlyCutoffScoreDifference 5 or its Uptime >= ActiveClusterTime 10 s.
void Match::updateClusters(float dt) {
    clusterClock_ += dt;
    uptime_[0] += dt; uptime_[1] += dt;
    sinceUpdate_ += dt;
    if (sinceUpdate_ < 0.1f) return;
    if (usingInitialSpawn_) {
        if (clusterClock_ < 15.0f) return;
        usingInitialSpawn_ = false;
    }
    sinceUpdate_ = 0.0f;
    for (int f = 0; f < 2; ++f) {
        int top = -1; float topScore = -1e30f;
        for (size_t i = 0; i < clusters_.size(); ++i) {
            if (!clusters_[i].registered) continue;
            float sc = scoreCluster(clusters_[i], f);
            if (sc > topScore) { topScore = sc; top = (int)i; }
        }
        if (top < 0) continue;
        float cur = active_[f] >= 0 ? scoreCluster(clusters_[(size_t)active_[f]], f) : -1e30f;
        if (topScore - cur >= 5.0f || uptime_[f] >= 10.0f) {
            if (top != active_[f]) LOG_INFO("match: faction %d active cluster %s -> %s", f, active_[f] >= 0 ? clusters_[(size_t)active_[f]].actor.c_str() : "-", clusters_[(size_t)top].actor.c_str());
            active_[f] = top; uptime_[f] = 0.0f;
        }
    }
}

// ScoreCluster: sum of TnSpawnModifierComponent rules Factor x InverseDistance (1 / |center - owner| in UU, 1 at 0),
// 0 beyond CutoffDistance. Player pawns: friend +1, enemy -3; TnTombstone: all -5 within 5000 UU [CONF authored].
// Objective-factory / KOTH / Domination modifiers belong to their modes (not registered in TDM [PARTIAL]).
float Match::scoreCluster(const Cluster& c, int faction) const {
    auto inv = [&](const core::Vec3& p) { float d = core::length(c.center - p) * 100.0f; return d > 0.0f ? 1.0f / d : 1.0f; };
    float s = 0.0f;
    for (size_t i = 0; i < players_.size(); ++i) {
        if (!players_[i].alive) continue;
        s += (players_[i].team == faction ? 1.0f : -3.0f) * inv(locs_[i]);
    }
    for (const Tombstone& t : tombstones_)
        if (core::length(c.center - t.pos) * 100.0f <= 5000.0f) s += -5.0f * inv(t.pos);
    // Objective modifiers: KOTH active zone All -50 within 5000; DOM point Friend +1 (owner team) [CONF authored].
    for (const SpawnModifier& m : objMods_) {
        if (m.cutoffUU >= 0.0f && core::length(c.center - m.pos) * 100.0f > m.cutoffUU) continue;
        if (m.rule == 1 && m.team != faction) continue;
        if (m.rule == 2 && (m.team == faction || m.team > 1)) continue;
        s += m.factor * inv(m.pos);
    }
    return s;
}

// TnGameRules_RoundsBase [CONF RE MILESTONE05_GAMEPLAY_UNKNOWNS §4]: Active ticks TimeTillNextReset (mirrored to the GRI
// countdown = HUD clock); at 0 -> EndRound: CurrentRound++, EndGame(none, "Score") after the last round, else
// TnRoundBasedGameMessage 2 -> BetweenRounds (TimeBetweenRounds 5 s, RoundEnded) -> RestartRound (SoftReset: everyone
// respawns, message 3) -> Active. SingleFlagCTF: SetNextAttackingTeam on BetweenRounds entry; CheckMercyRule each
// Active tick on the last round: the team that attacks last already leads -> EndGame(none, "Score"). A capture does not
// end the round.
void Match::tickRounds(float dt) {
    if (betweenRounds_) {
        betweenRoundsLeft_ -= dt;
        if (betweenRoundsLeft_ <= 0.0f) restartRound();
        return;
    }
    roundTimeLeft_ -= dt;
    remainingTime_ = std::max(0, (int)std::ceil(roundTimeLeft_));
    if (s_.singleFlagCTF && currentRound_ == s_.rounds - 1 && attackingTeam_ <= 1) {
        int lastAttacker = attackingTeam_;                        // the team attacking in the last round
        int other = 1 - lastAttacker;
        if (teamScore_[lastAttacker] > teamScore_[other]) { endGame(-1, "Score"); return; }
    }
    if (roundTimeLeft_ > 0.0f) return;
    ++currentRound_;
    if (currentRound_ >= s_.rounds) { endGame(-1, "Score"); return; }
    betweenRounds_ = true;
    betweenRoundsLeft_ = s_.timeBetweenRounds;
    if (s_.singleFlagCTF && attackingTeam_ <= 1) attackingTeam_ = 1 - attackingTeam_;
    emit(MatchEvent::Type::RoundEnded, -1, currentRound_);
}

void Match::restartRound() {
    betweenRounds_ = false;
    roundTimeLeft_ = (float)s_.timeLimit;
    remainingTime_ = s_.timeLimit;
    // TnGame.RestartRound: SoftReset - every player respawns (no death counted).
    for (MatchPlayer& p : players_) p.alive = false;
    spawnAllInitial();
    emit(MatchEvent::Type::RoundStarted, -1, attackingTeam_);
}

namespace {
const KillstreakDef kKillstreaks[] = {
    {"OrbitalReconStreak", 3, "Scout", "Orbital Beacon", true},
    {"HealthRegenStreak", 5, "Scout", "Energon Recharger", true},
    {"ImprovedOrbitalReconStreak", 7, "Scout", "Orbital Beacon 2.0", true},
    {"FastAbilityCooldownStreak", 3, "Leader", "Intercooler", true},
    {"PokeStreak", 5, "Leader", "P.O.K.E. 2.0", true},
    {"MinePooperStreak", 7, "Leader", "Thermo Mine Re-Spawner", true},
    {"FriendlyKillHealthBonusStreak", 3, "Scientist", "Health Matrix 2.0", true},
    {"OverShieldStreak", 5, "Scientist", "Overshield Matrix", true},
    {"SpawnRocketTurretStreak", 7, "Scientist", "Nucleon Shock Cannon", true},
    {"RefillAmmoStreak", 3, "Soldier", "Ammo Matrix", true},
    {"TeamAbilityJammerStreak", 5, "Soldier", "Electromagnetic Pulse", true},
    {"GuidedMissileStreak", 7, "Soldier", "Omega Missile", true},
};
}
const KillstreakDef* findKillstreak(const std::string& specialty, int kills) {
    for (const KillstreakDef& k : kKillstreaks) if (specialty == k.specialty && kills == k.kills) return &k;
    return nullptr;
}
const KillstreakDef* killstreakById(const std::string& id) {
    for (const KillstreakDef& k : kKillstreaks) if (id == k.id) return &k;
    return nullptr;
}

} // namespace game
