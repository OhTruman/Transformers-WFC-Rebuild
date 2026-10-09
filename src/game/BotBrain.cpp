// Clean-room reconstruction — bot AI data tables (see BotBrain.h).
#include "game/BotBrain.h"
#include <algorithm>
#include <cstring>

namespace game {

AiWeaponData aiWeaponData(const char* weaponId, int clip) {
    // *_AI_WEPDATA BurstRanges {Burst Min / Max shots, PauseDuration Min / Max s} and DesiredFiringRange [CONF RE data].
    struct Row { const char* id; AiWeaponData d; };
    static const Row rows[] = {
        {"AssaultRifle",    {{35, 45, 0.5f, 1.0f}, {30, 40, 1.0f, 1.5f}, {25, 35, 2.0f, 3.0f}, AiRange::Medium, true}},
        {"BurstRifle",      {{15, 25, 0.5f, 1.5f}, {20, 30, 0.5f, 2.0f}, {20, 25, 2.0f, 3.5f}, AiRange::Medium, true}},
        {"EmpShotgun",      {{4, 8, 0.5f, 1.0f},   {3, 6, 1.5f, 2.0f},   {2, 5, 2.0f, 2.5f},   AiRange::Close, true}},
        {"HeavyPistol",     {{3, 6, 0.2f, 0.8f},   {3, 9, 0.5f, 1.5f},   {4, 8, 1.0f, 3.0f},   AiRange::Medium, true}},
        {"SniperRifle",     {{4, 7, 0.5f, 1.0f},   {3, 6, 1.0f, 2.0f},   {3, 6, 1.5f, 2.0f},   AiRange::Retreated, true}},
        {"RocketLauncher",  {{4, 5, 0.5f, 1.0f},   {3, 5, 1.0f, 2.0f},   {2, 5, 2.5f, 3.5f},   AiRange::Retreated, true}},
        {"GrenadeLauncher", {{3, 3, 2.0f, 2.0f},   {3, 3, 2.0f, 2.0f},   {3, 3, 2.0f, 2.0f},   AiRange::Medium, true}},
        {"AssaultRifleVehicle", {{15, 20, 1.0f, 2.0f}, {10, 20, 2.0f, 3.0f}, {15, 25, 2.5f, 3.5f}, AiRange::Far, true}},
        {"PlaneMachineGun", {{10, 15, 1.0f, 1.5f}, {12, 20, 1.0f, 2.0f}, {20, 30, 1.5f, 3.0f}, AiRange::Far, true}},
        {"TankCannon",      {{1, 3, 1.5f, 2.5f},   {1, 3, 2.0f, 3.0f},   {1, 3, 2.0f, 3.5f},   AiRange::Retreated, true}},   // TankVehicleCannonHeavy bursts
        {"HomingRocketVehicle", {{2, 4, 1.0f, 2.0f}, {2, 4, 1.0f, 2.0f}, {2, 4, 1.5f, 2.5f}, AiRange::Far, false}},   // band CONF, burst PROV
        {"RocketVehicle",   {{2, 4, 1.0f, 2.0f},   {2, 4, 1.0f, 2.0f},   {2, 4, 1.5f, 2.5f},   AiRange::Retreated, false}},  // band CONF, burst PROV
        {"HeavyMG",         {{30, 40, 1.0f, 1.5f}, {25, 35, 1.0f, 2.0f}, {20, 30, 2.0f, 3.0f}, AiRange::Far, false}},       // band CONF, burst PROV
    };
    for (const Row& r : rows) if (weaponId && std::strcmp(weaponId, r.id) == 0) return r.d;
    // No AI weapon data (IonBlaster, Shotgun, PlasmaCannon, RepairRay, ...): PROVISIONAL bursts of about a clip.
    // (built per call: a static 4-slot ring shared by the steering workers raced - two bots' provisional rows overwrote each other)
    AiWeaponData d;
    const int c = std::max(1, clip);
    const bool shotgun = weaponId && std::strcmp(weaponId, "Shotgun") == 0;
    d.shortR = {std::max(1, c / 2), std::max(1, c), 0.4f, 0.9f};
    d.mediumR = {std::max(1, c / 3), std::max(1, c * 2 / 3), 0.8f, 1.4f};
    d.longR = {std::max(1, c / 4), std::max(1, c / 2), 1.5f, 2.5f};
    d.desired = shotgun ? AiRange::Close : AiRange::Medium;
    d.authored = false;
    return d;
}

const BotSkill& botSkill(int difficulty) {
    //                          react turn  err  fov   sight mem  strafe burst pause
    // EXPERT (PC EXTENSION, user decision 2026-10-09): HARD pushed further on every knob - reaction 0.22 -> 0.15 s, turn 620 -> 800
    // deg/s, aim error 2.0 -> 1.2 deg, FOV 160 -> 175 deg, sight 70 -> 80 m, memory 6 -> 8 s, pause between bursts x0.85.
    static const BotSkill s[4] = {{0.80f, 220.0f, 7.0f, 110.0f, 50.0f, 2.0f, 0.45f, 0.6f, 1.5f},    // EASY
                                  {0.45f, 380.0f, 4.0f, 130.0f, 60.0f, 4.0f, 0.75f, 0.85f, 1.15f},  // MEDIUM
                                  {0.22f, 620.0f, 2.0f, 160.0f, 70.0f, 6.0f, 1.0f, 1.0f, 1.0f},     // HARD
                                  {0.15f, 800.0f, 1.2f, 175.0f, 80.0f, 8.0f, 1.0f, 1.0f, 0.85f}};   // EXPERT (PC EXTENSION)
    return s[std::clamp(difficulty, 0, kMaxBotDifficulty)];
}

const char* botGoalName(BotGoalKind k) {
    static const char* n[] = {"Roam", "Attack", "Defend", "Capture", "Hold", "Contest", "Retrieve", "Return", "Support"};
    return n[(int)k];
}

} // namespace game
