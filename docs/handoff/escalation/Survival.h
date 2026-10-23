#pragma once
// Escalation (SV, the co-op horde) - E1: the director (TnSurvivalGame), the wave budget (TnSurvivalSpawnList) and the spawn
// finder (TnSurvivalSpawnFinder). Behaviour: RE MP_ESCALATION_RE.md §1-§2 + the spawn-finder answer of 2026-10-23 (C); data:
// tools/gameplay/gen_escalation.py (RE export + AssetTools zone boxes), compiled in as src/game/EscalationData.inc.
// Owned and ticked by World (WorldSurvival.cpp); all sim, drawn from the Match RNG, by match player index.
#include "core/Math.h"
#include "game/Weapon.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace game {

// ESpawnType order (the SelfModifier / ZoneChanges arrays are indexed by it).
enum SurvivalSpawnType { kSvStandard = 0, kSvShortRange, kSvLongRange, kSvAir, kSvBoss, kSvTypes };
// TnSurvivalGeneratorBase.DrainRate (no subclass override), for a generator authored without one (BrokenHope 12583) [CONF RE E2 §2].
constexpr float kSvDefaultDrainRate = 10.0f;
// The 360 ticked at 30 Hz: the per-tick drain int(max(1, dt x DrainRate)) is emulated on that tick (the clamp matters) [RE E2 §3].
constexpr float kSvDrainTick = 1.0f / 30.0f;

struct SurvivalData {
    bool ok = false;
    std::string map;
    int coopTeam = 0;                    // the players' team (CoopInfo faction); the horde is the other
    float preGameTime = 30.0f, timeBetweenWaves = 25.0f, fillWaveReCheckTime = 0.5f;
    struct Wave { int budget = 0, budgetAlive = 0, deficit = 0; };
    std::vector<Wave> waves;
    struct Enemy { std::string type; int minWave = 0, maxWave = 0, minCount = 0, maxCount = 0, cost = 0, spawnType = 0; float power = 0.0f; };
    std::vector<Enemy> enemies;
    struct Zone {
        std::string id;
        bool active = true;
        int points = 0;                  // PossibleSpawnPoints count
        std::array<float, kSvTypes> self{};
        struct Change { int zone = -1; std::array<float, kSvTypes> mod{}; std::array<bool, kSvTypes> named{}; };
        std::vector<Change> changes;
        core::Vec3 mn{0, 0, 0}, mx{0, 0, 0};
        std::vector<std::array<float, 4>> spawnPoints;   // x, y, z (glTF m), yaw; empty until AssetTools' positions land (PROV)
    };
    std::vector<Zone> zones;
    // TnSurvivalGenerator (E2): cost (MP: one power level), DrainRate (<= 0: the CDO's), the ActivationVolume (outward planes: inside
    // where n.p <= d, plus its box), the New Power Level actions AssetTools resolved (op: unhide / hide / transform / untransform /
    // collide / on / off / toggle; class + target actor).
    struct Generator {
        std::string id, nameKey, volume;
        int cost = 0;
        float drainRate = 0.0f;
        core::Vec3 location{0, 0, 0}, mn{0, 0, 0}, mx{0, 0, 0};
        std::vector<std::array<float, 4>> planes;
        struct Action { std::string op, cls, target; };
        std::vector<Action> actions;
        bool contains(const core::Vec3& p) const {
            if (p.x < mn.x || p.y < mn.y || p.z < mn.z || p.x > mx.x || p.y > mx.y || p.z > mx.z) return false;
            for (const auto& q : planes) if (q[0] * p.x + q[1] * p.y + q[2] * p.z > q[3] + 1e-3f) return false;
            return true;
        }
    };
    std::vector<Generator> generators;
    // Map Kismet as data (BrokenHope Wall_Crashers, RE E2b §4): trigger volumes by name; crash walls (any pawn's first touch: 200 on
    // the destructible); round gates (from Round Begin count `round` a player touch turns the zone on, the untouch off); the window
    // (a player touching `enable` arms an AI-only touch of `volume`: 200 on `wall`).
    struct Shape {
        core::Vec3 mn{0, 0, 0}, mx{0, 0, 0};
        std::vector<std::array<float, 4>> planes;
        bool contains(const core::Vec3& p) const {
            if (p.x < mn.x || p.y < mn.y || p.z < mn.z || p.x > mx.x || p.y > mx.y || p.z > mx.z) return false;
            for (const auto& q : planes) if (q[0] * p.x + q[1] * p.y + q[2] * p.z > q[3] + 1e-3f) return false;
            return true;
        }
    };
    std::vector<std::pair<std::string, Shape>> volumes;
    struct CrashWall { int volume = -1; std::string wall; };
    std::vector<CrashWall> crashWalls;
    struct RoundGate { int volume = -1; int zone = -1; int round = 0; };
    std::vector<RoundGate> roundGates;
    struct Window { int enable = -1, volume = -1; std::string wall; float damage = 0.0f; };
    Window window;
    // TnWeaponPickupFactory (AssetTools weapon_pickups): the weapon id (our WeaponDef id), RespawnTime, location / yaw.
    struct WeaponPickup { std::string actor, weapon; float respawn = 2.0f; core::Vec3 pos{0, 0, 0}; float yaw = 0.0f; };
    std::vector<WeaponPickup> weaponPickups;
    // RE's initial sorted order per type (the original binary's sort of the starting Zones list): ESCTEST's test vector.
    std::array<std::vector<std::string>, kSvTypes> initialSorted;
    static bool load(const std::string& map, SurvivalData& out);
};

struct SurvivalState {
    enum class Phase { Off, PreGame, InWave, BetweenWaves, GameOver };
    Phase phase = Phase::Off;
    int currentWave = -1, displayWave = -1;   // CurrentWave clamps to the last row; the HUD shows DisplayWave + 1
    float phaseTime = 0.0f;                    // s in PreGame (vs PreGameTime) / BetweenWaves (vs TimeBetweenWaves)
    int usedThisWave = 0;                      // TotalUsedThisWave
    bool filling = false;
    float fillTimer = 0.0f;
    struct Slot { int player = -1; int enemy = -1; bool inUse = false; };   // the horde participants, one per entry x MaxCount
    std::vector<Slot> slots;
    // Spawn finder: per zone UniversalScoreAdjustment, CurrentScore[type], the round-robin SpawnPointIndex; per type the sorted
    // zone order (re-sorted only for the types a touch names: the original's stale-order behaviour, RE C).
    std::vector<float> universal;
    std::vector<std::array<float, kSvTypes>> current;
    std::vector<int> pointIndex;
    std::array<std::vector<int>, kSvTypes> sorted;
    // TnSurvivalSpawnFinder.Zones: the active zones in level actor order at Initialize, then NotifyZoneActivated appends (AddItem) and
    // NotifyZoneDeactivated removes (RemoveItem); every sort starts from this order [RE C].
    std::vector<int> zonesList;
    std::vector<std::vector<int>> touching;    // per match player: the zones its pawn touches (human pawns only)
    // Economy (E2): _CurrentPower per match player (0..9999, kills only), each generator's shared pool and state, the HUD prompt.
    std::vector<float> power;
    std::vector<float> pool;
    std::vector<uint8_t> generatorDone, generatorOff;   // full (its one level reached) / its ActivationVolume turned off
    std::vector<int> nearGenerator;                     // per match player: the usable generator its pawn stands in (-1 none)
    std::vector<uint8_t> drainFailed;                   // per match player: draining failed this step (no power: the fail sound)
    std::vector<uint8_t> allowDrain, prevInteract;      // per match player: _AllowDraining (a press inside; reset on entering) / last step's held bit
    std::vector<float> drainClock;                      // per match player: time toward the next 30 Hz drain tick
    std::vector<int> itemFactory;                       // per generator: the pickup factory it unhides (-1: a door / zone generator)
    std::vector<float> reofferTimer;                    // per generator: > 0 while the 2 s Delay before its volume turns back on runs
    std::vector<uint8_t> factoryWasAvailable;           // per generator: its factory's available() last step (the Taken edge)
    size_t killsSeen = 0;                               // Match::killHistory() entries already paid out
    int purchases = 0;
    // Round Begin / Round End (TnSeqEvent_SurvivalEvents 1 / 0) for the map's sequence and the announcer: counters + the last one.
    int roundBegins = 0, roundEnds = 0;
    std::vector<uint8_t> crashDone, gateOn;               // per crash wall: fired; per round gate: its touch enabled
    std::vector<std::vector<uint8_t>> gateInside;         // per round gate, per match player: inside last step
    bool windowArmed = false, windowDone = false;
    std::vector<uint8_t> windowInside;                    // per match player: inside the arming volume last step
    // Weapon pickups: per factory hidden (until bought) / available / respawn timer; dropped weapons (TnDroppedPickupWeapon) with the
    // tossed weapon's state; per match player the Interact press edge for the contextual pickup.
    struct WeaponPick { bool hidden = true, available = true; float respawn = 0.0f; };
    std::vector<WeaponPick> weaponPicks;
    struct Drop { Weapon w; core::Vec3 pos{0, 0, 0}; float life = 0.0f; };
    std::vector<Drop> drops;
    std::vector<uint8_t> pickupPress;
    int weaponTakes = 0, ammoTakes = 0, tosses = 0;
    int spawns = 0, failedSpawns = 0;
    std::vector<int> spawnOrder;               // the enemy entry of every spawn (tests)
};

} // namespace game
