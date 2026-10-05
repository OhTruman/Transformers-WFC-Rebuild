// Clean-room reconstruction — local versus match state machine (Gameplay-owned), launch-independent.
// Provenance: RE MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md §5 (TnMultiplayerGame / TnTeamGame / TnVersusGame,
// TnGameRules_ScoreKills*, TrackKillsMP, ReportGameProgress*, TnRespawnHelperWave, TnSpawnPointManager) and the
// authored TnOnlineGameSettings{TDM,DM} defaults (authored.db). Everything tagged CONF there unless marked here.
//
//   PendingMatch (10 s countdown, nobody spawns) -> StartMatch -> InProgress (clock, kills/score, 5 s respawn)
//   -> EndGame (score limit / clock / forfeit) -> MatchOver (15 s) -> ReturnToGameLobby handoff.
//
// No bots / networking: players are registered by the host (the local player; a test harness may add more and
// drive deaths through killed()). Presentation (HUD movies, announcer) consumes events().
#pragma once
#include "core/Math.h"
#include "game/CharacterRoster.h"

#include <algorithm>
#include <string>
#include <vector>

namespace game {

struct MatchSettings {
    std::string modeTag = "TDM";       // TDM (TnVersusGame, team) or DM (TnFreeForAllGame, FFA)
    bool teamGame = true;
    int goalScore = 40;                // ?PointsToWin (TDM default 40 of 20/30/40/50; DM 20 of 10/20/30)
    int timeLimit = 900;               // ?TimeLimit seconds (TimeLimits[1] of 600/900/1200)
    int teamScoreAmount = 1;           // ScoreKills.TeamScoreAmount: TDM / DM 1; ScoreKillsMP (DOM, KOTH, CTF, EXT) unset = 0
    std::string gameType = "TNGT_TDM"; // ETnGameType: cluster ActiveGameTypes filter
    bool reportKills = true;           // ReportGameProgressKills (TDM / DM)
    bool reportPoints = false;         // ReportGameProgressPoints (DOM / KOTH): 50 / 25 left on ScoreObjective
    int objectiveIndividualScore = 0;  // ScoreObjectives.IndividualScore (KOTH 1; CTF / EXT 10)
    float matchAutoStartCountdown = 10.0f;   // TnMultiplayerGame.MatchAutoStartCountdown
    float matchOverCountdown = 15.0f;        // TnMultiplayerGame.MatchOverCountdown
    float waveRespawnTime = 5.0f;            // TnRespawnHelperWave.WaveRespawnTime
    float forfeitDelay = 1.5f;               // TnVersusGame.ForfeitDelay
    static MatchSettings forMode(const std::string& tag);   // authored defaults per TnOnlineGameSettings<tag>
};

struct MatchEvent {
    enum class Type {
        CountdownTick,        // PendingMatch countdown value changed (PreGameCountdown <CurrentGame:CurrentCountdown>)
        MatchStarted,         // InProgress.BeginState: SendUIEventToControllers(3), GTMT_StartGame message
        PlayerSpawned,        // RestartPlayer at a start (spawnActor)
        PlayerKilled,         // Killed(): score already applied
        GameNearlyComplete,   // NotifyGameNearlyComplete (60 s left or 5 kills left)
        TimeAnnouncement,     // TnGameProgressAnnouncementMessage switch 0/1/2 (30/60/120 s left)
        KillsLeftAnnouncement,// switch 5/6/7 (1/3/5 scores left)
        PointsLeftAnnouncement,// ReportGameProgressPoints switch 4 / 3 (50 / 25 left; DOM / KOTH)
        MatchEnded,           // EndGame -> MatchOver: winner (team index / player index / -1 tie), reason
        ReturnToLobby,        // MatchOver.OnCountdownComplete -> TnGame.ReturnToGameLobby (host handoff)
    };
    Type type;
    int player = -1, other = -1;       // victim / killer, spawned player, winning player (FFA)
    int value = 0;                     // countdown, announcement switch, winning team (-1 = none / tie)
    std::string text;                  // EndGame reason ("Score", "", "Forfeit"), spawn start actor
};

// One TnDeathMessage broadcast (GameInfo.BroadcastDeathMessage -> BroadcastLocalized(DeathMessageClass, switch, Killer.PRI,
// Victim.PRI, DamageType)) [CONF TnDeathMessage.GetColoredString; HIGH stock BroadcastDeathMessage]: switch 1 (suicide or
// no killer) -> DamageType.SuicideMessage(victim), else DamageType.DeathMessage(killer, victim); `k / `o names coloured
// friendly / enemy relative to the viewer (TnMessageHelpers.GetColorForPRI). Text and colour belong to presentation.
struct KillFeedEntry {
    float time = 0.0f;                 // match time (s since begin) of the broadcast
    int messageSwitch = 0;             // 0 kill, 1 suicide / environmental (no killer)
    int killer = -1, victim = -1;      // player indices (-1 none)
    int killerTeam = 255, victimTeam = 255;
    std::string damageType;            // DamageType class, e.g. "TransGame.TnDamageTypeIonBlaster"
    // Hud_GFX feed rows: live 5.0 s, then fade over 1.0 s; newest at slot 0, at most 5 rows [CONF RE OVERNIGHT A2].
    static constexpr float kLifetime = 5.0f, kFade = 1.0f;
};

struct MatchPlayer {
    std::string name;
    int team = 255;                    // 0 Autobots, 1 Decepticons, 255 none (FFA)
    int score = 0, kills = 0, deaths = 0;
    float assists = 0.0f;
    bool alive = false;
    float timeToRespawn = -1.0f;       // >= 0 while queued in the respawn helper (PRI.TimeToRespawn)
    bool hasSelectedCharacter = false; // PRI.HasSelectedCharacter: spawning waits for it [CONF]
    CharacterSelection selection;
    std::string chassis;               // body resolved at the last spawn (faction from the team)
    std::string drawnChassis;          // body actually drawn: the selection when its pawn resources load, else "Truck"
    bool chassisFallback = false;      // true when drawnChassis != chassis [RECONSTRUCTION FALLBACK, logged]
};

class Match {
public:
    enum class State { None, PendingMatch, InProgress, MatchOver, Returned };

    struct Start { std::string actor, cluster; int team = 255; bool ffa = false; core::Vec3 pos; float yaw = 0.0f; };
    struct Cluster {
        std::string actor; core::Vec3 center; bool initialSpawn = false; int faction = 255;
        std::vector<std::string> activeGameTypes;   // empty = every game type
        bool registered = true;                     // TnSpawnPointManager.Initialize for the current game type
        std::vector<int> spawnPoints;  // indices into starts
        int iterator = 0;              // round-robin SpawnIterator
    };

    // Starts / clusters from gameplay.json (positions glTF metres). Returns false when the data is unavailable.
    bool loadSpawnData(const std::string& gameplayJson);
    void begin(const MatchSettings& s);          // InitGame + InitGameReplicationInfo + PendingMatch.BeginState
    int addPlayer(const std::string& name);      // PostLogin: team via TnTeamHandlerTwoTeams.PickTeam (team games)
    // TnPlayerController.SelectCharacter -> ReplicateCharacterData -> PRI._SelectedCharacter (applies at the next spawn).
    void selectCharacter(int p, const CharacterSelection& s) { if (p >= 0 && (size_t)p < players_.size()) { players_[(size_t)p].selection = s; players_[(size_t)p].hasSelectedCharacter = true; } }
    // [integration M06] A player whose character comes from the frontend's selection screen: no default selection,
    // so CheckReadySpawn waits for selectCharacter (addPlayer pre-selects Optimus only for the direct boot / harnesses).
    void requireCharacterSelection(int p) { if (p >= 0 && (size_t)p < players_.size()) players_[(size_t)p].hasSelectedCharacter = false; }
    void tick(float dt);
    // GameInfo.Killed. killer < 0: environmental. suicide: DmgType_Suicided or killer == victim.
    void killed(int killer, int victim, bool suicide = false, const std::string& damageType = std::string());
    // Kill feed: entries still within their LocalMessage lifetime (oldest first), and the whole match history.
    std::vector<KillFeedEntry> killFeed() const;
    const std::vector<KillFeedEntry>& killHistory() const { return killHistory_; }
    float matchTime() const { return matchTime_; }
    const std::string& endReason() const { return endReason_; }
    int winnerPlayer() const { return winnerPlayer_; }
    float matchOverTimeLeft() const { return state_ == State::MatchOver ? std::max(0.0f, s_.matchOverCountdown - stateTime_) : 0.0f; }
    // Faction the player's character resolves to (TnGame.GetResolvedCharacterFaction): team for team games; FFA = 1
    // (every DM player is a Decepticon body) [CONF RE MILESTONE05_PLAYTEST_RE §3 / §7].
    int faction(int p) const { return (p < 0 || (size_t)p >= players_.size()) ? 255 : (s_.teamGame ? players_[(size_t)p].team : 1); }
    // Dead player past TnPlayerController.MinRespawnDelay (3.0 s): PlayerSpectating + respawn UI [CONF RE E7].
    bool spectating(int p) const;
    static constexpr float kMinRespawnDelay = 3.0f;
    // Damage that reached the victim (after team filtering): the victim's DamageHistory, used for TrackKillsMP.ScoreAssists.
    void recordDamage(int victim, int instigator, float amount);
    // Objective scoring (rules chain): Game.ScoreObjective(PRI, score) -> ScoreObjectives AddScore(IndividualScore, score) +
    // ReportGameProgressPoints; TnGame.ScoreTeamObjective(team, amount) (DOM); PRI.AddScore(amount) without team. Each
    // reaches the score check.
    void scoreObjective(int player, int score);
    void scoreTeamObjective(int team, int amount);
    void addPersonalScore(int player, int amount);
    // Spawn modifiers owned by objectives (TnSpawnModifierComponent): factor, cutoff (UU, <0 none), team rule.
    struct SpawnModifier { core::Vec3 pos; float factor; float cutoffUU; int rule; int team; };   // rule 0 All, 1 Friend, 2 Enemy
    void setObjectiveSpawnModifiers(const std::vector<SpawnModifier>& m) { objMods_ = m; }
    bool sameTeam(int a, int b) const {
        return s_.teamGame && a >= 0 && b >= 0 && (size_t)a < players_.size() && (size_t)b < players_.size() &&
               players_[(size_t)a].team == players_[(size_t)b].team && players_[(size_t)a].team < 2;
    }
    static constexpr float kHealthMax = 550.0f;   // TR_Health_p.SharedHealth (assist = damage / HealthMax)
    // TnSpawnModifierComponent owners other than player pawns (positions in metres).
    void setPlayerLocation(int p, const core::Vec3& pos) { if (p >= 0 && (size_t)p < players_.size()) locs_[(size_t)p] = pos; }
    core::Vec3 playerLocation(int p) const { return (p >= 0 && (size_t)p < locs_.size()) ? locs_[(size_t)p] : core::Vec3{0, 0, 0}; }

    State state() const { return state_; }
    const MatchSettings& settings() const { return s_; }
    const std::vector<MatchPlayer>& players() const { return players_; }
    int teamScore(int t) const { return (t == 0 || t == 1) ? teamScore_[t] : 0; }
    int remainingTime() const { return remainingTime_; }      // GRI.RemainingTime (s)
    int elapsedTime() const { return elapsedTime_; }          // GRI.ElapsedTime (s)
    int countdown() const { return countdown_; }              // <CurrentGame:CurrentCountdown>
    int gameStatus() const { return gameStatus_; }            // GRI.SetGameStatus: 2 pending, 3 in progress, 5 over
    int winnerTeam() const { return winnerTeam_; }            // GRI.Winner (-1 = none / tie)
    const std::vector<MatchEvent>& events() const { return events_; }   // pending until the host consumes them (clearEvents)
    void clearEvents() { events_.clear(); }
    // Spawn choice for player p (FindPlayerStart): start index or -1. Exposed for the host to place the pawn.
    // GameInfo.Login -> FindPlayerStart for a joining controller (consumes the cluster SpawnIterator like the original).
    int loginStart(int p) { return (p >= 0 && (size_t)p < players_.size()) ? findPlayerStart(p) : -1; }
    int lastSpawnStart(int p) const { return (p >= 0 && (size_t)p < spawnAt_.size()) ? spawnAt_[(size_t)p] : -1; }
    const std::vector<Start>& starts() const { return starts_; }
    const std::vector<Cluster>& clusters() const { return clusters_; }
    int activeCluster(int faction) const { return (faction == 0 || faction == 1) ? active_[faction] : -1; }

private:
    MatchSettings s_;
    State state_ = State::None;
    std::vector<MatchPlayer> players_;
    std::vector<core::Vec3> locs_;
    std::vector<int> spawnAt_;
    std::vector<MatchEvent> events_;
    std::vector<Start> starts_;
    std::vector<Cluster> clusters_;
    int teamScore_[2] = {0, 0};
    int remainingTime_ = 0, elapsedTime_ = 0, countdown_ = 0, gameStatus_ = 0, winnerTeam_ = -1;
    float secondAccum_ = 0.0f, stateTime_ = 0.0f;
    bool announced_[3] = {false, false, false}, killsAnnounced_[3] = {false, false, false};
    // Spawn point manager.
    int active_[2] = {-1, -1};
    float uptime_[2] = {0.0f, 0.0f}, clusterClock_ = 0.0f, sinceUpdate_ = 0.0f;
    bool usingInitialSpawn_ = true;
    std::vector<std::vector<std::pair<int, float>>> damageHistory_;
    std::vector<KillFeedEntry> killHistory_;
    std::vector<SpawnModifier> objMods_;
    void checkScore(int player, int team);
    void reportPoints(int player);
    std::vector<float> deathTime_;
    float matchTime_ = 0.0f;
    std::string endReason_;
    int winnerPlayer_ = -1;   // per victim: (instigator, damage) in first-hit order
    struct Tombstone { core::Vec3 pos; int team; };
    std::vector<Tombstone> tombstones_;
    unsigned rng_ = 0x1234ABCDu;
    int randomInt(int n) { rng_ = rng_ * 1664525u + 1013904223u; return n > 0 ? (int)((rng_ >> 8) % (unsigned)n) : 0; }

    void startMatch();
    void endGame(int winnerPlayer, const std::string& reason);
    void restartPlayer(int p);
    int findPlayerStart(int p);
    void updateClusters(float dt);
    float scoreCluster(const Cluster& c, int faction) const;
    void secondTimer();
    void emit(MatchEvent::Type t, int player = -1, int value = 0, const std::string& text = std::string(), int other = -1) {
        events_.push_back({t, player, other, value, text});
    }
};

const char* matchStateName(Match::State s);

} // namespace game
