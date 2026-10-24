#pragma once
// Escalation DLC map mechanisms the director runs as data (RE work/handoff/DLC_ESC_MECHANISMS_RE.md, 1104a86; data written by
// tools/gameplay/gen_escalation.py as escalation_<map>.json "mechanisms"): District's laser defense grid, sentries, power outage and
// detpacks; Tranquillity's floodgate drain and base doors. Hazards here are World::hazards_ (AssetTools hazard_volumes.json) by actor.
#include "core/Math.h"

#include <string>
#include <vector>

namespace assets { class Json; }

namespace game {

struct SvMech {
    std::vector<std::string> hazardsOff;   // pain volumes off at PreGameplaySetup
    // LaserDefenseGridGenerator: bought -> the damage volume on for `time`; the button (within buttonRange, from armDelay) ends it
    // early; at the end the volume goes off and the generator is back at 0 (re-buyable).
    struct Laser { std::string gen, hazard; core::Vec3 button{0, 0, 0}; float time = 45.0f, armDelay = 3.0f, buttonRange = 3.0f; int g = -1; };
    std::vector<Laser> lasers;
    // TnActorFactorySentryAI at a SkeletalMeshActor: spawned dormant + god mode (never removed); its generator wakes it for `time`,
    // then dormant again and the generator reset. uses > 0: after that many expiries the generator is off for good.
    struct Sentry { std::string seq, gen; core::Vec3 pos{0, 0, 0}; float yaw = 0.0f; bool repair = false; float time = 45.0f; int uses = 0; int g = -1; };
    std::vector<Sentry> sentries;
    // TnInteractDetpack: a player Interact within range (no cost, single use) -> after the countdown, damage per destructible
    struct Hit { std::string wall; float delay = 0.0f, damage = 1000.0f; };
    struct Detpack { core::Vec3 pos{0, 0, 0}; float range = 5.0f, countdown = 3.0f; std::vector<Hit> hits; };
    std::vector<Detpack> detpacks;
    // Power_Outage: from Round Begin `fromRound`, each Round Begin -> `delay` s -> `chance` (lateChance from lateRound); while out
    // (duration) no generator can be bought; at most `max` per match; lockout after each (lateLockout from lateRound).
    struct Outage { bool on = false; int fromRound = 4, lateRound = 10, max = 3; float chance = 0.25f, lateChance = 0.5f, delay = 5.0f,
                    duration = 60.0f, lockout = 180.0f, lateLockout = 300.0f; };
    Outage outage;
    // Floodgates: each purchase resumes the drain to its next pause (drainTimes); after the last: Drain Completed (zones on, the
    // energon pain volume off, the base doors open: blockers off after gateOpenDelay).
    struct Flood { std::string gen, trigger, hazard; int g = -1; };
    std::vector<Flood> floods;
    std::vector<float> drainTimes;
    std::vector<std::string> zonesOn, blockers;
    std::string energon;
    float gateOpenDelay = 3.1f;
    bool any() const { return !hazardsOff.empty() || !lasers.empty() || !sentries.empty() || !detpacks.empty() || outage.on || !floods.empty(); }
    static void parse(const assets::Json& j, SvMech& out);
};

struct SvMechState {
    struct Laser { bool active = false; float t = 0.0f; };
    struct Sentry { bool active = false; float t = 0.0f, yaw = 0.0f, cool = 0.0f; int expiries = 0; int target = -1; };
    struct Detpack { bool used = false; float t = -1.0f; size_t next = 0; int by = -1; };
    std::vector<Laser> lasers;
    std::vector<Sentry> sentries;
    std::vector<Detpack> detpacks;
    std::vector<uint8_t> prevInteract;     // per match player (the detpack / button press edge)
    int outages = 0, roundsSeen = 0;
    float outageLeft = 0.0f, lockLeft = 0.0f, rollIn = -1.0f;
    int floodBought = 0;
    float drainT = 0.0f, gateT = -1.0f;
    bool drained = false, gatesOpen = false;
    int laserRuns = 0, sentryRuns = 0, detonations = 0;   // test counters (WFC_ESCTEST)
};

} // namespace game
