// Clean-room reconstruction - the authoritative multiplayer gameplay event record.
//
// Every scoring-relevant occurrence of a match (spawn, kill, suicide, environmental death, assist, killstreak, objective
// action, class selection, match start / end) is recorded ONCE by Match, in the order it happened, with the context the
// original rules need: TnGameRules_ScoreKills / TrackKillsMP / ScoreObjectives (score), TnKillAwardManager's 28 kill-award
// rules and the XP event types (RE MP_PROGRESSION_SCORING_AI_2026-10-06 + notes/data/mp_*.json), challenges and medals
// (the XP popups). Score is applied by Match when the event is recorded; HUD score, scoreboard, kill feed, XP, challenges,
// medals and bots read these records instead of keeping their own counters.
//
// Consumers read Match::gameplayEvents() and remember the last serial they handled (serials are unique and increasing
// within a match; the list is cleared at Match::begin), so no event is consumed twice across respawns or menu paths.
// Gameplay records facts only; award evaluation (XP amounts, medal / challenge conditions) belongs to progression.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/Math.h"

namespace game {

// Who drives a participant. All kinds use the same match rules; the kind only matters where the ORIGINAL treats AI
// differently (ShouldScoreKill: MP kills only score on TnPlayerController victims) - see MatchSettings::botVictimsScore.
enum class ParticipantKind { Local, Remote, Bot };

// A participant's state at the moment of an event (the facts the kill-award rules test).
struct ParticipantSnapshot {
    int player = -1;                   // match player index
    int team = 255;
    ParticipantKind kind = ParticipantKind::Local;
    std::string specialty;             // Scout / Scientist / Leader / Soldier (the CURRENT specialty: XP goes to it)
    std::string chassis;               // resolved body
    bool alive = false;
    bool vehicleForm = false;          // KillerInVehicle / KilledForm (VFT)
    bool transforming = false;
    int vehicleType = -1;              // VehicleFormType of the body (car / truck / tank / jet), -1 robot
    bool flying = false;               // jet in Flying
    float health = 0.0f, healthMax = 0.0f;   // KillerLowHealth (< 50 HP)
    bool downed = false;               // KillerDowned
    bool fineAim = false;              // FineAim award
    bool meleeing = false;
    bool hovering = false;             // the Hover ability's lift (KillHoverMelee, KilledHover)
    int killStreak = 0;                // PRI._CurrentKillStreak BEFORE this event
    float spawnTime = -1.0f;           // match time of the last spawn (Lifetime)
    int level = 0;                     // displayed level (HigherLevel); 0 until progression supplies it
    int carrying = -1;                 // 0 flag, 1 bomb, -1 none (FlagCarrier / BombCarrier kills)
    bool inActiveZone = false;         // KOTH active zone (ZoneKill)
    bool inEnemyNode = false;          // DOM node held by the OTHER team (DominationPointKill)
    core::Vec3 pos{0, 0, 0};           // metres (rebuild space)
    // Active buffs as original class names ("TransGame.TnBuffWarcryIncreaseDamage", ...), with who applied each (match
    // player, -1 self / unknown) for the KilledOffensiveBuffCheck assist-to-debuffer rule.
    struct Buff { std::string cls; int instigator = -1; };
    std::vector<Buff> buffs;
};

enum class GameplayEventType {
    MatchStart,         // InProgress.BeginState
    Spawn,              // RestartPlayer: participant, specialty, chassis
    CharacterSelected,  // PRI._SelectedCharacter changed (applies at the next spawn)
    Kill,               // a kill of a participant by another participant (the victim's death record)
    Suicide,            // DmgType_Suicided or a self-kill (death record, no score)
    EnvironmentDeath,   // no killer (KillZ, hazard, fall): death record, no score
    Assist,             // TrackKillsMP.ScoreAssists: one per kill at most (the first other damager)
    KillstreakEarned,   // UpdateKillstreakRewards acquired a streak reward (text = killstreak id, value = streak count)
    Objective,          // objective action (objective = kind below)
    MatchEnd,           // EndGame
};

struct GameplayEvent {
    uint32_t serial = 0;               // unique, increasing within a match
    GameplayEventType type = GameplayEventType::Kill;
    float time = 0.0f;                 // match time (s since Match::begin)
    int instigator = -1;               // killer / scorer / assister / spawned participant
    int victim = -1;                   // killed participant
    ParticipantSnapshot instigatorState, victimState;
    // Kill context.
    std::string damageType;            // DamageType class, e.g. "TransGame.TnDamageTypeIonBlaster"
    std::string weapon;                // instigator's weapon provider id at the kill ("" = none / unknown)
    bool melee = false;                // melee damage (AllMelee, KillHoverMelee)
    bool headshot = false;             // Headshot (TnKillAwardRuleHeadshot) - set when the hit location is known [PARTIAL]
    bool backstab = false;             // TnBuffIncreaseMeleeDamageFromBehind effective [PARTIAL: not simulated]
    bool ability = false;              // ability damage (Whirlwind, Shockwave, sentry, mines, ...)
    bool killAfterDeath = false;       // the killer was already dead (projectile in flight)
    float distanceUU = 0.0f;           // killer -> victim (LongRange > 8000 UU)
    float assistFraction = 0.0f;       // Assist: damage / victim HealthMax (> 0.5 Assist XP, > 0.25 WeakAssist)
    // Objective context: FlagTaken, FlagCapture, FlagReturn, FlagDropped, BombTaken, BombPlant, BombDefuse, BombDetonate,
    // NodeCapture, ZoneHold (value = points scored in the zone stay).
    std::string objective;
    // Score applied by this event (PRI.AddScore personal / Team.AddScore).
    int personalScore = 0, teamScore = 0;
    int value = 0;                     // killstreak count, zone points, MatchEnd winning team (-1 tie / none)
    std::string text;                  // killstreak id, MatchEnd reason ("Score", "", "Forfeit")
    // MatchEnd: per-player completion (index = match player; 0 win, 1 loss, 2 draw), the team that gets GameWin XP (RE quirk:
    // the team of the winning PRI passed to EndGame; on time-limit ends the top individual scorer's), MVP players.
    std::vector<int> completion;
    int xpWinTeam = -1;
    int winnerPlayer = -1;
    std::vector<int> mvp;
};

}  // namespace game
