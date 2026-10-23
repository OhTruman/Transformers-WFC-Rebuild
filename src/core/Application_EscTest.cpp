// WFC_ESCTEST (with WFC_MAP=MP_ESC_BrokenHope, headless with WFC_NULLRENDER): the Escalation E1 director through World - the ESC
// launch, PreGame, wave 1's forced MinCount and budget, the spawn finder's occupied-zone score, the horde never respawning, the wave
// end respawning a dead player, and the all-dead loss. One SUMMARY line.
#include "core/Application.h"
#include "core/Log.h"
#include "game/World.h"

#include <cmath>

namespace core {

void Application::runEscalationTest() {
    const float dt = (float)clock_.stepSeconds();
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("ESCTEST %s %s", ok ? "PASS" : "FAIL", what); };
    // guard: the players are kept at full health while the wave rules are checked (the horde would otherwise end the test early)
    bool guard = true, guardAlly = true; game::MatchOpponent* ally = nullptr;
    auto step = [&] {
        platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt);
        if (!guard) return;
        if (!world_.localPlayerDead()) world_.player().pawn().health().reset();
        if (ally && ally->spawned() && guardAlly) ally->pawn().health().reset();
    };
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) step(); };
    using Phase = game::SurvivalState::Phase;
    game::MatchLaunch L;
    check(game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=ESC", L) && L.settings.survival && L.settings.goalScore == 0 &&
          L.settings.timeLimit == 0, "URL: ESC = survival, no score goal, no time limit");
    check(world_.launchMatch(L), "launchMatch(ESC) on MP_ESC_BrokenHope");
    game::Match& m = world_.match();
    const game::SurvivalData& D = world_.survivalData();
    const game::SurvivalState& S = world_.survival();
    check(D.ok && D.waves.size() == 17 && D.enemies.size() == 13 && D.zones.size() == 30 && D.preGameTime == 30.0f && D.timeBetweenWaves == 25.0f,
          "BrokenHope data: 17 waves, 13 enemy entries, 30 zones, PreGame 30 s, between waves 25 s");
    check(S.slots.size() == 46 && S.phase == Phase::PreGame, "46 horde slots (sum of MaxCount), PreGame");
    const int me = world_.localMatchPlayer();
    ally = world_.addMatchOpponent("Ally", false);   // a second human (Remote kind) on the players' team
    m.playerMutable(ally->matchPlayer()).team = D.coopTeam;
    check(m.players()[(size_t)me].team == 0, "the player is on BrokenHope's co-op team (Autobots)");
    run(10.5f);
    int hordeAlive = 0;
    for (const auto& s : S.slots) hordeAlive += m.players()[(size_t)s.player].alive;
    check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead() && ally->spawned() && hordeAlive == 0,
          "match started: players spawned, no horde (director-spawned only)");
    check(world_.hudState().svWave == 1 && world_.hudState().svNextWaveTime > 25, "HUD: wave 1 pending, PreGame countdown");
    run(29.0f);
    check(S.phase == Phase::PreGame, "still PreGame before 30 s");
    run(1.5f);
    check(S.phase == Phase::InWave && S.displayWave == 0 && S.currentWave == 0 && S.spawns >= 1, "wave 1 begins after PreGameTime, first enemy at once");
    run(3.0f);
    bool spidersFirst = S.spawnOrder.size() >= 2 && D.enemies[(size_t)S.spawnOrder[0]].type == "Spider_AICD" && D.enemies[(size_t)S.spawnOrder[1]].type == "Spider_AICD";
    check(spidersFirst, "wave 1: the Spider entry (MinCount 2) is forced first");
    {
        int cost = 0;
        for (const auto& s : S.slots) if (s.inUse) cost += D.enemies[(size_t)s.enemy].cost;
        check(cost <= D.waves[0].budgetAlive + 10 && S.usedThisWave <= D.waves[0].budget + 10, "alive cost stops at BudgetAlive, used within the budget");
        LOG_INFO("ESCTEST wave 1 after 3 s: %d spawns, %d used, alive cost %d, %d failed", S.spawns, S.usedThisWave, cost, S.failedSpawns);
    }
    {   // the spawn finder: a player inside a zone puts -999 on it (UniversalScoreAdjustment)
        int z = -1;
        for (size_t i = 0; i < S.touching.size() && z < 0; ++i) if (!S.touching[i].empty()) z = S.touching[i][0];
        bool ok = z < 0;
        if (z >= 0) ok = std::fabs(S.universal[(size_t)z] + 999.0f * 1.0f) < 0.5f || S.universal[(size_t)z] <= -999.0f;
        check(ok, "occupied zone: UniversalScoreAdjustment -999 per player pawn inside");
        LOG_INFO("ESCTEST touched zone %s (universal %.0f)", z >= 0 ? D.zones[(size_t)z].id.c_str() : "none", z >= 0 ? S.universal[(size_t)z] : 0.0f);
    }
    // Kill the ally: no respawn during the wave.
    guardAlly = false;
    world_.applyMatchDamage(ally->matchPlayer(), -1, 1.0e6f, false, "TransGame.TnDamageTypeIonBlaster");
    run(7.0f);
    check(!m.players()[(size_t)ally->matchPlayer()].alive, "a dead player waits for the wave end (no respawn timer)");
    // Clear the wave: kill every live horde pawn until the budget is spent and none is left.
    int killed = 0;
    for (int k = 0; k < 120 * 60 && S.phase == Phase::InWave; ++k) {
        for (const auto& s : S.slots)
            if (s.inUse && m.players()[(size_t)s.player].alive) { world_.applyMatchDamage(s.player, me, 1.0e6f, false, "TransGame.TnDamageTypeIonBlaster"); ++killed; }
        step();
    }
    bool hordeRespawned = false;
    for (const auto& s : S.slots) hordeRespawned |= m.players()[(size_t)s.player].timeToRespawn >= 0.0f;
    check(S.phase == Phase::BetweenWaves && S.usedThisWave >= D.waves[0].budget, "wave 1 cleared once the budget is spent and the horde is dead");
    check(!hordeRespawned, "the horde never queues a respawn");
    run(0.5f);
    check(ally->spawned() && m.players()[(size_t)ally->matchPlayer()].alive, "the wave end respawns the dead player");
    check(world_.hudState().svNextWaveTime > 20 && world_.hudState().svNextWaveTime <= 25, "HUD: next wave countdown (25 s)");
    LOG_INFO("ESCTEST wave 1: %d horde killed, %d used of %d", killed, S.usedThisWave, D.waves[0].budget);
    run(25.5f);
    check(S.phase == Phase::InWave && S.displayWave == 1 && S.currentWave == 1 && world_.hudState().svWave == 2, "wave 2 after TimeBetweenWaves");
    // Loss: every player dead at once.
    guard = false;
    world_.applyMatchDamage(ally->matchPlayer(), -1, 1.0e6f, false, "TransGame.TnDamageTypeIonBlaster");
    world_.killLocalPlayer(-1, false);
    run(0.5f);
    check(m.state() == game::Match::State::MatchOver && m.endReason() == "AllDead" && S.phase == Phase::GameOver && world_.hudState().svGameOver,
          "all players dead: EndGame AllDead, game over at wave 2");
    LOG_INFO("ESCTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
    exitCode_ = fails ? 1 : 0;
}

} // namespace core
