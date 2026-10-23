#pragma once
// Escalation (SV, the co-op horde) - E1: the director (TnSurvivalGame), the wave budget (TnSurvivalSpawnList) and the spawn
// finder (TnSurvivalSpawnFinder). Behaviour: RE MP_ESCALATION_RE.md §1-§2 + the spawn-finder answer of 2026-10-23 (C); data:
// tools/gameplay/gen_escalation.py (RE export + AssetTools zone boxes), compiled in as src/game/EscalationData.inc.
// Owned and ticked by World (WorldSurvival.cpp); all sim, drawn from the Match RNG, by match player index.
#include "core/Math.h"

#include <array>
#include <string>
#include <vector>

namespace game {

// ESpawnType order (the SelfModifier / ZoneChanges arrays are indexed by it).
enum SurvivalSpawnType { kSvStandard = 0, kSvShortRange, kSvLongRange, kSvAir, kSvBoss, kSvTypes };

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
    std::vector<std::vector<int>> touching;    // per match player: the zones its pawn touches (human pawns only)
    int spawns = 0, failedSpawns = 0;
    std::vector<int> spawnOrder;               // the enemy entry of every spawn (tests)
};

} // namespace game
