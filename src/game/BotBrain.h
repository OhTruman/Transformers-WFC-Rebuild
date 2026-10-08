// Clean-room reconstruction — offline multiplayer bot controller state (PC ADAPTATION: the shipped versus game had no bots).
// What IS original and used here:
//  - range bands: TnAiController.RangeSet / TnAiRangeDetector.ETnRange in UU (Touch 0-175, Striking -600, Close -1500,
//    Medium -3000, Far -5000, Retreated -7000, OutOfRange beyond) [CONF RE addendum 7];
//  - fire pacing: the AI weapon data's BurstRanges (TnWeapon.GetBurstDataForRangeInternal: Striking / Close -> ShortRange,
//    Medium -> MediumRange, everything else -> LongRange) and DesiredFiringRange (an ETnRange band) [CONF data + RE];
//  - aim: the AI aims at the target's TargetableLocation; inaccuracy comes from weapon spread and burst pacing [CONF RE].
// What is a PC ADAPTATION: the existence of bots, perception limits, reaction delay, aim tracking / error, difficulty
// scaling, roaming and navigation (BotNav over AssetTools' reconstructed cells), weapon / form choices.
// The decisions run at a low rate (think, staggered per bot); per-step work is steering and aim easing only.
#pragma once
#include "core/Math.h"
#include "game/BotNav.h"
#include <map>
#include <string>
#include <vector>

namespace game {

// TnAiRangeDetector.ETnRange.
enum class AiRange { Invalid = 0, Touch, Striking, Close, Medium, Far, Retreated, OutOfRange };
inline AiRange aiRangeBand(float distM) {
    const float uu = distM * 100.0f;
    if (uu < 0.0f) return AiRange::Invalid;
    if (uu <= 175.0f) return AiRange::Touch;
    if (uu <= 600.0f) return AiRange::Striking;
    if (uu <= 1500.0f) return AiRange::Close;
    if (uu <= 3000.0f) return AiRange::Medium;
    if (uu <= 5000.0f) return AiRange::Far;
    if (uu <= 7000.0f) return AiRange::Retreated;
    return AiRange::OutOfRange;
}
inline float aiRangeMinM(AiRange r) { static const float m[8] = {0, 0, 1.76f, 6.01f, 15.01f, 30.01f, 50.01f, 70.01f}; return m[(int)r]; }
inline float aiRangeMaxM(AiRange r) { static const float m[8] = {0, 1.75f, 6.0f, 15.0f, 30.0f, 50.0f, 70.0f, 1e9f}; return m[(int)r]; }

// *_AI_WEPDATA BurstRanges + DesiredFiringRange for the MP weapons [CONF RE notes/data/ai_weapon_data.json]; weapons with no AI
// data get a PROVISIONAL pacing derived from their clip (flagged `authored = false`).
struct AiWeaponData {
    struct Burst { int minShots, maxShots; float minPause, maxPause; };
    Burst shortR, mediumR, longR;
    AiRange desired = AiRange::Medium;
    bool authored = false;
};
// By value: called from the parallel steering pass (a shared scratch ring raced between workers - the Debug determinism flake).
AiWeaponData aiWeaponData(const char* weaponId, int clip);

// Difficulty dimensions (PC ADAPTATION; the original MP had none). 0 EASY, 1 MEDIUM, 2 HARD.
struct BotSkill {
    float reaction;          // s from first sight to the first shot
    float turnRateDeg;       // aim turn rate (deg / s)
    float aimErrorDeg;       // aim offset cone while tracking (shrinks to 40 % with sustained tracking)
    float fovDeg;            // perception cone
    float sightM;            // perception range
    float memory;            // s a lost target is still pursued
    float strafe;            // strafe input magnitude in combat
    float burstScale;        // multiplier on the authored burst length
    float pauseScale;        // multiplier on the authored burst pause
};
const BotSkill& botSkill(int difficulty);

// Objective goal layer shared by every mode (TDM uses Roam / Attack): what a bot is trying to do and where.
enum class BotGoalKind { Roam, Attack, Defend, Capture, Hold, Contest, Retrieve, Return, Support };
const char* botGoalName(BotGoalKind k);
// pos: where the corridor goes (the nav approach); touch: the objective itself (flag / bomb / point) the bot must reach in person.
struct BotGoal { BotGoalKind kind = BotGoalKind::Roam; core::Vec3 pos{0, 0, 0}; float radius = 3.0f; int target = -1; bool hasTouch = false; core::Vec3 touch{0, 0, 0}; };

struct BotBrain {
    int player = -1;
    int difficulty = 1;
    bool wasSpawned = false;
    float thinkTimer = 0.0f;
    float life = 0.0f;                 // s since spawn
    // Perception / target
    struct Seen { core::Vec3 pos{0, 0, 0}; float time = -100.0f; bool visible = false; };
    std::map<int, Seen> seen;
    int target = -1;
    float targetVisibleFor = 0.0f;
    float lastDamageTime = -100.0f; int lastAttacker = -1;
    // Goal + path
    BotGoal goal;
    bool hasGoal = false;
    float goalTime = 0.0f;
    std::vector<BotNav::Waypoint> path;
    size_t wp = 0;
    bool wantRepath = true;
    float repathTimer = 0.0f;
    bool vehiclePath = false;
    float bestDist = 1e9f, progressTimer = 0.0f; int stuckLevel = 0;
    float offMesh = 0.0f;
    // Off-mesh rejoin (a pocket / prop top whose path leg runs through a wall): a point on the mesh in a clear straight line.
    bool hasRejoin = false;
    core::Vec3 rejoin{0, 0, 0}; float rejoinUntil = 0.0f;
    bool fireWish = false;
    unsigned lastShotSerial = 0;
    int dbgCands = 0, dbgFov = 0, dbgLos = 0, dbgVis = 0;   // WFC_PLAYERBOTLOG: the last think's perception funnel    // WFC_PLAYERBOT: the burst counts the local weapon's real shots, not frames
    core::Vec3 watchPos{0, 0, 0}; float watchT = 0.0f; bool watchLogged = false;   // WFC_STUCKWATCH          // WFC_PLAYERBOT: the brain would fire this step (the local player fires through the controller)
    core::Vec3 rejoinFrom{0, 0, 0}; float rejoinStall = 0.0f;   // no progress toward the rejoin point (s)
    core::Vec3 unwedge{0, 0, 0};                                 // this step's slide (applied in the serial pass)
    core::Vec3 stuckPos{0, 0, 0}; float stuckT = 0.0f;   // displacement-based stuck detection
    size_t progressWp = (size_t)-1; float progressBest = 1e9f, progressT = 0.0f;   // waypoint-progress stuck detection
    std::vector<int> avoidCells; float avoidUntil = 0.0f;   // cells where this bot got wedged (A* cost x10 for 30 s)
    float ignoreSightingsUntil = 0.0f;                  // a team sighting proved unreachable
    // Aim / fire
    float yaw = 0.0f, pitch = 0.0f;
    core::Vec3 aimErr{0, 0, 0}; float aimErrTimer = 0.0f;
    float reactionLeft = 0.0f;
    int burstLeft = 0; float burstPause = 0.0f;
    // Movement
    float strafeDir = 1.0f, strafeTimer = 0.0f;
    float switchHold = 0.0f;
    float meleeCooldown = 0.0f;                          // between melee attacks
    float rushUntil = 0.0f;                              // closing in for a melee attack
    int healTarget = -1;                                 // a wounded teammate this bot repairs with the Repair Ray
    int pendingDodge = 0;                                // TnAcrobaticsManager dodge request for the next step (1 left, 2 right)
    bool pendingHover = false;                           // PlayerController.Hover request for the next step
    float grenadeCooldown = 0.0f, grenadeDelay = -1.0f;  // TnGrenadeThrower: TossDelay 0.4 s, then the release
    core::Vec3 grenadeTarget{0, 0, 0};
    float transformCooldown = 0.0f;
    bool wantVehicle = false;
    float objectiveBlockedUntil = 0.0f;   // the objective goal had no path: hunt meanwhile
    bool mission = false;               // the goal is an objective errand that combat must not interrupt (carrying, defusing, ...)
    float wanderT = 0.0f; core::Vec3 wanderPos{0, 0, 0};   // holding a point: small moves inside its radius
    float noVehicleUntil = 0.0f;
    float vehicleFightUntil = 0.0f;   // fighting in vehicle form with the vehicle weapon
    // Diagnostics
    unsigned rng = 1;
    float streakDelay = -1.0f;
    int streaks = 0, vehicleShots = 0, abilities = 0, heals = 0, rushes = 0, melees = 0, grenades = 0, hits = 0, noPaths = 0, shots = 0, repaths = 0, stucks = 0, jumps = 0, transforms = 0, switches = 0, reloads = 0;
    float frand() { rng = rng * 1664525U + 1013904223U; return (float)((rng >> 8) & 0xFFFFFF) / 16777216.0f; }
    float frange(float a, float b) { return a + (b - a) * frand(); }
};

} // namespace game
