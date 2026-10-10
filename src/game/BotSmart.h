#pragma once
// Smart bot AI (user decision 2026-10-09: "I want intelligent AI not bots that are fixed to a path"). The faithful AI stays as
// "Classic" (lobby "Bot AI: Smart / Classic"); Classic's decision code is never edited, and a Classic match is step-for-step
// identical to the build before Smart (WFC_SIMHASH gate). Smart state lives here, apart from BotBrain. Design:
// Rebuild-Gameplay/work/pass29/smart-ai-design.md. Everything is deterministic: per-bot seeded rng, shared state written only in
// the serial bot pass, fixed iteration order.
#include <cstdint>
#include <vector>
#include "core/Math.h"

namespace game {

enum class BotAi : int { Classic = 0, Smart = 1 };
const char* botAiName(BotAi a);
BotAi botAiFromString(const char* s, BotAi fallback);   // "Smart" / "Classic" / "1" / "0" (case-insensitive)

// Difficulty as data: one row per BotLaunch difficulty (0 Easy, 1 Normal, 2 Hard); a further tier is one more row.
struct SmartSkill {
    const char* name;
    float memorySeconds;     // a perceived enemy's confidence decays to 0 over this time
    float calloutDelay;      // a teammate's call-out reaches this bot after this delay (s)
    float hearGunfireM;      // gunfire is heard through walls within this range (user decision)
    float hearExplosionM;    // explosions likewise
    float hearFootstepsM;    // footsteps / running robots only this close (user decision: <= 15 m)
    float hearEngineM;       // a moving vehicle's engine
    float hitAwareness;      // 0..1: how precisely a hit tells the bot where the shooter is (0 = direction only, 1 = exact)
};
const SmartSkill& smartSkill(int difficulty);
int smartSkillCount();

// Smart decision weights (one table; the shipped values are the defaults). WFC_SMARTTUNE=key=value,... overrides them for tuning
// sweeps (WFC_AIDUEL) without a rebuild. Keys are the member names.
struct SmartTune {
    float coverExposure = 2.0f;     // seek cover when this many known enemies can see the bot's cell
    float coverMaxExposure = 1.0f;  // a cover spot may be seen by at most this many
    float coverChance = 0.35f;      // chance to seek cover when it applies: coverChance + coverChancePerDiff * difficulty
    float coverChancePerDiff = 0.3f;
    float retreatHp = 0.25f;        // retreat below this health fraction while fighting
    float huntConfidence = 0.35f;   // hunt a remembered enemy at least this certain
    float routeExposureW = 0.6f;    // route cost x (1 + w * exposure)
    float focusHurtW = 12.0f;       // target score: metres per missing health fraction
    float focusMatesW = 6.0f;       // target score: metres per teammate already on that target (up to 3)
    float squadSize = 4.0f;         // Smart bots of a team form squads of this many (by player order); 0 / 1 = no squads
    float squadFollow = 1.0f;
    float squadRegroupM = 15.0f;    // a member farther than this from its leader regroups on it
    float squadWaitM = 1e9f;        // a leader waits while its members are this far away on average (off: a member stuck elsewhere kept
                                    // its leader idle - BOTTEST)       // members take the leader's goal / target when not fighting (0 = off)
    float outnumberMargin = 2.0f;   // fall back to the nearest ally when known enemies near >= allies near + this ...
    float outnumberHp = 0.6f;       // ... and health is below this fraction
    float segBreak = 0.4f;          // in a fight with the current health segment below this fraction: break sight 2+ s to regenerate
                                    // it (regen refills only the current segment after 2 s undamaged) (0 = off)
    float pickupSegments = 1.0f;    // not fighting and this many segments down: fetch a health pickup within 40 m (0 = off)
    float vehicle = 0.0f;           // 1 = Smart also decides in vehicle form (hunt / squad / focus / retreat / pickups). Off: in duels it
                                    // lost fights (55 % vs 65 % wins, more Smart deaths from behind while driving to hunts)
    float flank = 1.0f;             // hunt a moving enemy from its side / back: a firing spot 10-20 m away, > 100 deg off its heading (0 = off)
    float preAim = 1.0f;            // with no target in sight, look where a remembered enemy (>= 0.4 sure, <= 45 m) should appear (0 = off)
};
const SmartTune& smartTune();

// Where the bot believes an enemy is. source: 1 sight, 2 hearing, 3 team call-out, 4 hit by it.
struct SmartMemory {
    core::Vec3 pos{0, 0, 0}, vel{0, 0, 0};
    float time = -1e9f;      // match time of the last update
    float confidence = 0.0f; // 1 = seen now; decays with smartSkill().memorySeconds; hearing / call-outs start lower
    uint8_t source = 0;
};

struct SmartBot {
    bool active = false;                 // this participant is driven by Smart (else Classic)
    unsigned rng = 0x9e3779b9U;          // Smart's own sequence (BotBrain's rng belongs to Classic)
    std::vector<SmartMemory> mem;        // by match player
    float lastHealth = -1.0f;
    float lastPerceive = -1e9f;          // match time of the last perception pass (noises newer than this are heard)
    // S2 decision state: the Smart action layered over the Classic goal.
    enum Action : uint8_t { None = 0, Hunt = 1, Retreat = 2, TakeCover = 3, HoldCover = 4 };
    uint8_t action = None;
    core::Vec3 actionPos{0, 0, 0};
    int actionTarget = -1;
    float actionSince = 0.0f, lastHitAt = -1e9f, lostTargetAt = -1e9f;
    bool ownMission = false;
    float waitSince = -1.0f, waitCooldownUntil = -1e9f;
    core::Vec3 skipPickup{0, 0, 0}; float skipPickupUntil = -1e9f;   // a health pickup this bot could not reach / use: skipped for 30 s   // squad leader waiting for its members (at most 6 s, then 10 s without waiting)
    bool flankSet = false; core::Vec3 flankPos{0, 0, 0}, flankAnchor{0, 0, 0};   // the hunt's flanking spot and the enemy spot it was chosen for             // Smart set BotBrain::mission (follow the path while fighting): Smart clears it again
    // Metrics (WFC_AIMETRICS).
    int seen = 0, heard = 0, callouts = 0, hitBy = 0, posted = 0;
    int hunts = 0, retreats = 0, covers = 0, flanks = 0, huntsHeard = 0, pickupTrips = 0, regenBreaks = 0;
    double cohesionSum = 0.0; long cohesionN = 0;   // squad members: distance to the leader, sampled each think
    float coverSeconds = 0.0f, engagedSeconds = 0.0f;
    float rand01() { rng = rng * 1664525U + 1013904223U; return (float)(rng >> 8) * (1.0f / 16777216.0f); }
};

// A sound bots can hear: gunfire, an explosion. Collected once per step (serial) from the previous step's shots / blasts.
struct SmartNoise { core::Vec3 pos; int player; uint8_t kind; float time; };   // kind: 1 gunfire, 2 explosion

// A team call-out (one per enemy per team, newest wins): what a teammate saw, and when.
struct SmartSighting { int enemy = -1, by = -1; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float time = -1e9f; };

}  // namespace game
