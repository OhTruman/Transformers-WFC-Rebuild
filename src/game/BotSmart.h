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
    // Metrics (WFC_AIMETRICS).
    int seen = 0, heard = 0, callouts = 0, hitBy = 0, posted = 0;
    float rand01() { rng = rng * 1664525U + 1013904223U; return (float)(rng >> 8) * (1.0f / 16777216.0f); }
};

// A sound bots can hear: gunfire, an explosion. Collected once per step (serial) from the previous step's shots / blasts.
struct SmartNoise { core::Vec3 pos; int player; uint8_t kind; float time; };   // kind: 1 gunfire, 2 explosion

// A team call-out (one per enemy per team, newest wins): what a teammate saw, and when.
struct SmartSighting { int enemy = -1, by = -1; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float time = -1e9f; };

}  // namespace game
