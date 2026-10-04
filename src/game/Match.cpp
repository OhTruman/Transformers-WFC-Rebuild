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
    if (tag == "DM") { s.teamGame = false; s.goalScore = 20; s.timeLimit = 900; }
    else { s.modeTag = "TDM"; s.teamGame = true; s.goalScore = 40; s.timeLimit = 900; }
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
    for (Cluster& c : clusters_) c.iterator = 0;   // fresh level: SpawnIterator 0
    active_[0] = active_[1] = -1;
    for (size_t i = 0; i < clusters_.size(); ++i)
        if (clusters_[i].initialSpawn && (clusters_[i].faction == 0 || clusters_[i].faction == 1)) active_[clusters_[i].faction] = (int)i;
    uptime_[0] = uptime_[1] = 0.0f; clusterClock_ = 0.0f; sinceUpdate_ = 0.0f; usingInitialSpawn_ = true;
    // PendingMatch.BeginState: bWaitingToStartMatch, GRI.SetGameStatus(2), GRI.ResetCountdown(true, 10).
    state_ = State::PendingMatch;
    gameStatus_ = 2;
    countdown_ = (int)s_.matchAutoStartCountdown;
    secondAccum_ = 0.0f; stateTime_ = 0.0f;
    emit(MatchEvent::Type::CountdownTick, -1, countdown_);
    LOG_INFO("match: %s begin (goal %d, time limit %d s, countdown %d s)", s_.modeTag.c_str(), s_.goalScore, s_.timeLimit, countdown_);
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
    emit(MatchEvent::Type::MatchStarted);
    // SpawnHelper: RespawnHelper.InitialSpawn -> Wave TimeToAllowInstantInitialSpawns -1: always immediate.
    for (size_t i = 0; i < players_.size(); ++i) restartPlayer((int)i);
}

void Match::killed(int killer, int victim, bool suicide, const std::string& damageType) {
    if (state_ != State::InProgress || victim < 0 || (size_t)victim >= players_.size()) return;   // MatchOver: no-ops
    if (killer >= (int)players_.size()) killer = -1;
    MatchPlayer& V = players_[(size_t)victim];
    if (!V.alive) return;
    // TnMultiplayerGame.Killed: TnTombstone at the victim (team set) - a spawn modifier.
    tombstones_.push_back({locs_[(size_t)victim], V.team});
    const bool killedSelf = killer == victim;
    // ScoreKills(TDM|DM).ScoreKill: not for DmgType_Suicided; needs a killer; AddScore(self ? 0 : 1, self ? 0 : TeamScoreAmount).
    if (!suicide && killer >= 0) {
        MatchPlayer& K = players_[(size_t)killer];
        if (!killedSelf) {
            K.score += 1;
            if (s_.teamGame && s_.teamScoreAmount > 0 && (K.team == 0 || K.team == 1)) teamScore_[K.team] += 1;   // Team.AddScore(1)
            K.kills += 1;                                         // TrackKillsMP: +1 -> AddKills
        }
    }
    // TrackKillsMP.ScoreAssists: the first damager in the victim's DamageHistory that is neither killer nor victim
    // gets AddAssist(damage / HealthMax).
    for (const auto& [who, dmg] : damageHistory_[(size_t)victim])
        if (who >= 0 && who != killer && who != victim) { players_[(size_t)who].assists += dmg / kHealthMax; break; }
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
    if (killer >= 0 && !suicide && !killedSelf) {
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
    for (const KillFeedEntry& k : killHistory_) if (matchTime_ - k.time < KillFeedEntry::kLifetime) v.push_back(k);
    return v;
}

bool Match::spectating(int p) const {
    if (p < 0 || (size_t)p >= players_.size() || players_[(size_t)p].alive || deathTime_[(size_t)p] < 0.0f) return false;
    return matchTime_ - deathTime_[(size_t)p] >= kMinRespawnDelay;
}

void Match::recordDamage(int victim, int instigator, float amount) {
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
    LOG_INFO("match: EndGame reason \"%s\" score %d-%d winner team %d player %d", reason.c_str(), teamScore_[0], teamScore_[1], winner, winnerPlayer);
}

void Match::restartPlayer(int p) {
    MatchPlayer& P = players_[(size_t)p];
    int st = findPlayerStart(p);
    spawnAt_[(size_t)p] = st;
    P.alive = true;
    P.timeToRespawn = -1.0f;
    damageHistory_[(size_t)p].clear();
    if (st >= 0) locs_[(size_t)p] = starts_[(size_t)st].pos;
    emit(MatchEvent::Type::PlayerSpawned, p, st, st >= 0 ? starts_[(size_t)st].actor : std::string());
}

int Match::findPlayerStart(int p) {
    const MatchPlayer& P = players_[(size_t)p];
    // LocationValidator.IsSafeSpawnLocation is native [PARTIAL]: approximated as no other live player within 4 m.
    auto safe = [&](int si) {
        for (size_t i = 0; i < players_.size(); ++i)
            if ((int)i != p && players_[i].alive) {
                core::Vec3 d = locs_[i] - starts_[(size_t)si].pos;
                if (core::length(d) < 4.0f) return false;
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
        bool ok = s_.teamGame ? (!s.ffa && s.team == P.team) : s.ffa;
        if (ok && safe((int)i)) return (int)i;
    }
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
    return s;
}

} // namespace game
