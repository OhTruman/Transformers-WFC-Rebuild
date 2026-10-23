// WFC_ESCTEST (with WFC_MAP=MP_ESC_BrokenHope, headless with WFC_NULLRENDER): the Escalation E1 director through World - the ESC
// launch, PreGame, wave 1's forced MinCount and budget, the spawn finder's occupied-zone score, the horde never respawning, the wave
// end respawning a dead player, and the all-dead loss. One SUMMARY line.
#include "core/Application.h"
#include "core/Log.h"
#include "game/World.h"
#include "game/PickupFactory.h"
#include "game/Destructible.h"

#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace core {

void Application::runEscalationTest() {
    const float dt = (float)clock_.stepSeconds();
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) ++fails; LOG_INFO("ESCTEST %s %s", ok ? "PASS" : "FAIL", what); };
    // guard: the players are kept at full health while the wave rules are checked (the horde would otherwise end the test early)
    bool guard = true, guardAlly = true; game::MatchOpponent* ally = nullptr;
    bool interact = false, wasInteract = false;   // the local player's Interact button (generator purchases)
    const core::Vec3* pin = nullptr;              // keep the local pawn here (standing in a generator's volume)
    auto step = [&] {
        platform::InputFrame in;
        in.down[(int)platform::Button::Interact] = interact;
        in.pressed[(int)platform::Button::Interact] = interact && !wasInteract;
        wasInteract = interact;
        if (pin && !world_.localPlayerDead()) { world_.player().pawn().setPosition(*pin); world_.player().pawn().velocity() = {0, 0, 0}; }
        world_.handleInput(in, dt); world_.tick(dt);
        if (!guard) return;
        if (!world_.localPlayerDead()) world_.player().pawn().health().reset();
        if (ally && ally->spawned() && guardAlly) ally->pawn().health().reset();
    };
    auto run = [&](float secs) { for (int k = 0; k < (int)(secs / dt); ++k) step(); };
    using Phase = game::SurvivalState::Phase;
    auto sortedMatches = [&](const game::SurvivalData& D, const game::SurvivalState& S) {   // RE's initial order per type (the binary's qsort)
        for (int t = 0; t < game::kSvTypes; ++t) {
            if (S.sorted[(size_t)t].size() != D.initialSorted[(size_t)t].size()) return false;
            for (size_t i = 0; i < S.sorted[(size_t)t].size(); ++i) if (D.zones[(size_t)S.sorted[(size_t)t][i]].id != D.initialSorted[(size_t)t][i]) return false;
        }
        return true;
    };
    game::MatchLaunch L;
    if (world_.mapName() == "MP_ESC_Remnant") {   // Remnant: the zone order and the first wave (all ShortRange scores tie at 0)
        check(game::MatchLaunch::fromURL("MP_ESC_Remnant_BASE_m?GameModeTag=ESC", L) && world_.launchMatch(L), "launchMatch(ESC) on MP_ESC_Remnant");
        const game::SurvivalData& D = world_.survivalData();
        const game::SurvivalState& S = world_.survival();
        check(D.ok && D.zones.size() == 52 && sortedMatches(D, S), "Remnant: the initial sorted zone order of every type equals RE's (actor order + MSVC qsort)");
        check(!S.sorted[game::kSvShortRange].empty() && D.zones[(size_t)S.sorted[game::kSvShortRange][0]].id == "5771", "Remnant: ShortRange top zone 5771 (4 points)");
        run(15.5f);
        check(world_.match().state() == game::Match::State::InProgress && S.phase == Phase::InWave && S.spawns >= 1,
              "Remnant: PreGame 10 s from level start, so wave 1 begins with the match (15 s countdown) and spawns");
        check(!world_.localPlayerDead() && world_.match().players()[(size_t)world_.localMatchPlayer()].team == 1,
              "Remnant: the player (Decepticons) spawned at a team start (the starts carry no team: all serve the players)");
        run(7.0f);   // Remnant FillWaveReCheckTime 3.0 s: spawns at 0, 3, 6 s
        check(S.failedSpawns == 0 && S.spawns >= 3, "Remnant: wave 1 keeps spawning every 3 s recheck (no failed spawn)");
        LOG_INFO("ESCTEST Remnant: %d spawns, %d failed", S.spawns, S.failedSpawns);
        {   // E3b: the Brute backpack (RE ESCALATION_HORDE_AI sections 2 / 6)
            const int b = world_.svTestSpawn("Brute_");
            run(0.5f);
            game::Character* bc = world_.participantPawnMutable(b);
            const game::SurvivalState::Slot* sl = nullptr;
            for (const auto& x : S.slots) if (x.player == b) sl = &x;
            check(b >= 0 && bc && sl && sl->bpStage == 0 && sl->bpHealth == 25.0f && bc->health().max == 100000.0f,
                  "Brute: spawned with the backpack (25) and the chassis' 100,000 health");
            const int me = world_.localMatchPlayer();
            game::Match& m = world_.match();
            auto hitFrom = [&](float side, float amount, const char* type) {   // side +1: in front of the Brute, -1: behind it
                if (!bc) return;
                bc->setYaw(0.0f);
                const core::Vec3 at = bc->position() + core::forwardFromYawPitch(0.0f, 0.0f) * (6.0f * side);
                m.setPlayerLocation(me, at, 1.0f);
                world_.applyMatchDamage(b, me, amount, false, type);
            };
            const float h0 = bc ? bc->health().current : 0.0f;
            hitFrom(1.0f, 100.0f, "TransGame.TnDamageTypeAssaultRifle");
            check(sl && sl->bpStage == 0 && sl->bpHealth == 25.0f && bc && bc->health().current < h0,
                  "Brute: a hit from the front lands on the body (DamageAngle 100: the backpack is not hit)");
            const float h1 = bc ? bc->health().current : 0.0f;
            hitFrom(-1.0f, 30.0f, "TransGame.TnDamageTypeAssaultRifle");
            check(sl && sl->bpStage == 5 && sl->bpHealth == 50.0f && bc && bc->health().current == h1,
                  "Brute: from behind the backpack takes it (25 -> stage 5 at 50, no overflow) and the pawn nothing");
            hitFrom(-1.0f, 40.0f, "TransGame.TnDamageTypeMeleeDefault");
            check(sl && sl->bpStage == 5 && sl->bpHealth == 30.0f, "Brute: melee x0.5 on the backpack (40 -> 20)");
            hitFrom(-1.0f, 30.0f, "TransGame.TnDamageTypeAssaultRifle");
            hitFrom(-1.0f, 80.0f, "TransGame.TnDamageTypeAssaultRifle");
            check(sl && sl->bpStage == 1 && m.players()[(size_t)b].alive, "Brute: 50 then 75 broken -> critical, still alive");
            const size_t kills = m.killHistory().size();
            run(2.5f);
            check(m.players()[(size_t)b].alive, "Brute: alive 2.5 s into critical");
            run(0.7f);
            bool byPack = false;
            for (size_t k = kills; k < m.killHistory().size(); ++k)
                if (m.killHistory()[k].victim == b && m.killHistory()[k].killer == me && m.killHistory()[k].damageType.find("BruteBackpack") != std::string::npos) byPack = true;
            check(!m.players()[(size_t)b].alive && byPack && S.bpExplosions == 1,
                  "Brute: the backpack explodes 3 s after critical and kills it (TnDamageTypeBruteBackpack, credited to the last hitter)");
            LOG_INFO("ESCTEST backpack: %d hits, %d refused (front), %d explosions", S.bpHits, S.bpRefused, S.bpExplosions);
            check(S.lootDropped >= 8, "Loot: the Brute's row (BruteAut) drops 7 + 1 TnAbilityResourcePickups");
            if (!S.loot.empty()) {
                const core::Vec3 at = S.loot.front().pos;
                pin = &at; run(0.3f); pin = nullptr;
            }
            check(S.lootTaken >= 1 && (size_t)me < S.abilityResource.size() && S.abilityResource[(size_t)me] >= 3.0f,
                  "Loot: touching a pickup gives the player +3 ability resource");
        }
        LOG_INFO("ESCTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
        exitCode_ = fails ? 1 : 0;
        return;
    }
    check(game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=ESC", L) && L.settings.survival && L.settings.goalScore == 0 &&
          L.settings.timeLimit == 0, "URL: ESC = survival, no score goal, no time limit");
    check(world_.launchMatch(L), "launchMatch(ESC) on MP_ESC_BrokenHope");
    game::Match& m = world_.match();
    const game::SurvivalData& D = world_.survivalData();
    const game::SurvivalState& S = world_.survival();
    check(D.ok && D.waves.size() == 17 && D.enemies.size() == 13 && D.zones.size() == 30 && D.preGameTime == 30.0f && D.timeBetweenWaves == 25.0f,
          "BrokenHope data: 17 waves, 13 enemy entries, 30 zones, PreGame 30 s, between waves 25 s");
    check(S.slots.size() == 46 && S.phase == Phase::PreGame, "46 horde slots (sum of MaxCount), PreGame");
    check(sortedMatches(D, S), "the initial sorted zone order of every type equals RE's (actor order + MSVC qsort)");
    check(D.generators.size() == 35 && m.settings().matchAutoStartCountdown == 15, "35 generators; the 15 s auto-start countdown");
    {
        int hidden = 0; for (const game::PickupFactory* f : world_.pickupFactories()) hidden += f->hidden();
        check(hidden == 6, "the 6 bought pickup factories start hidden");
    }
    const int me = world_.localMatchPlayer();
    ally = world_.addMatchOpponent("Ally", false);   // a second human (Remote kind) on the players' team
    m.playerMutable(ally->matchPlayer()).team = D.coopTeam;
    check(m.players()[(size_t)me].team == 0, "the player is on BrokenHope's co-op team (Autobots)");
    check(D.weaponPickups.size() == 17, "17 weapon pickup factories (hidden until bought)");
    const bool escNavOn = std::getenv("WFC_ESCNAV") && std::atoi(std::getenv("WFC_ESCNAV")) != 0;
    if (escNavOn) {
        size_t original = 0; for (const auto& c : world_.botNav().cells()) original += !c.versusOnly;   // v3: + merged versus cells after them
        check(S.escNav && original == 469 && S.navBlockedAtStart == 32 && S.generatorCells.size() == 12,
              "the Escalation nav (WFC_ESCNAV=1): 469 original NavigationCells, 32 blocked at start, 12 generators open cells");
    }
    else check(!S.escNav, "the versus nav by default (the Escalation nav with WFC_ESCNAV=1)");
    check(D.enemies[0].chassis == "ESC_Spider" && D.enemies[0].health == 20.0f && D.waves[9].decreaseTaken == 0 && D.waves[13].increaseDealt == 0,
          "the horde table: Spider body ESC_Spider 20 HP; wave 10 DecreaseDamageTaken L0, wave 14 IncreaseDamage L0");
    run(15.5f);
    int hordeAlive = 0;
    for (const auto& s : S.slots) hordeAlive += m.players()[(size_t)s.player].alive;
    {   // SetPlayerDefaults (SV): the AssaultRifle replaces the primary and secondary
        const auto& pc = world_.player().pawn();
        int robot = 0; bool ar = false;
        for (int i = 0; i < pc.weaponCount(); ++i)
            if (pc.weaponAt(i).fireType != game::WeaponFire::Grenade) { ++robot; ar = ar || (pc.weaponAt(i).def && std::string(pc.weaponAt(i).def->id) == "AssaultRifle"); }
        check(robot == 1 && ar, "the SV spawn loadout: the AssaultRifle only (plus the grenade bag)");
    }
    check(m.state() == game::Match::State::InProgress && !world_.localPlayerDead() && ally->spawned() && hordeAlive == 0,
          "match started: players spawned, no horde (director-spawned only)");
    check(world_.hudState().svWave == 1 && world_.hudState().svNextWaveTime > 10 && world_.hudState().svNextWaveTime <= 15,
          "HUD: wave 1 pending, PreGame countdown (30 s from level start)");
    run(14.0f);
    check(S.phase == Phase::PreGame, "still PreGame before 30 s from level start");
    run(1.0f);
    check(S.phase == Phase::InWave && S.displayWave == 0 && S.currentWave == 0 && S.spawns >= 1, "wave 1 begins 30 s after level start (F1), first enemy at once");
    run(3.0f);
    {   // the first spawn is a Spider in its own body with 20 HP
        const game::Character* sp = nullptr;
        for (const auto& s : S.slots) if (s.inUse && D.enemies[(size_t)s.enemy].type == "Spider_AICD") sp = world_.participantPawn(s.player);
        check(sp && sp->chassis().id == "ESC_Spider" && sp->health().max == 20.0f, "a Spider spawns in the ESC_Spider body with 20 HP");
    }
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
    {   // power: the killer gains each enemy's PowerReward
        float expect = 0.0f;
        for (int e : S.spawnOrder) expect += D.enemies[(size_t)e].power;
        check(std::fabs(S.power[(size_t)me] - std::min(9999.0f, expect)) < 0.5f, "power = the PowerReward of every horde kill");
        LOG_INFO("ESCTEST power %.0f (expected %.0f)", S.power[(size_t)me], expect);
    }
    auto buy = [&](const char* id, float maxSecs) {   // stand in the generator's volume, press and hold Interact
        int gi = -1;
        for (size_t g = 0; g < D.generators.size(); ++g) if (D.generators[g].id == id) gi = (int)g;
        if (gi < 0) return -1.0f;
        const game::SurvivalData::Generator& G = D.generators[(size_t)gi];
        static core::Vec3 at; at = (G.mn + G.mx) * 0.5f; at.y = G.mn.y + 0.5f;
        pin = &at; interact = false; step(); step();
        interact = true;
        float t = 0.0f;
        while (t < maxSecs && !S.generatorDone[(size_t)gi]) { step(); t += dt; }
        interact = false; step(); pin = nullptr;
        return S.generatorDone[(size_t)gi] ? t : -1.0f;
    };
    {   // an item generator: Ammo 11568 (50 at DrainRate 25 -> 1 per 30 Hz tick = 50 ticks = 1.67 s); its factory unhidden, its volume off
        const float p0 = S.power[(size_t)me];
        const float t = buy("11568", 5.0f);
        int gi = -1; for (size_t g = 0; g < D.generators.size(); ++g) if (D.generators[g].id == "11568") gi = (int)g;
        bool shown = false; for (const game::PickupFactory* f : world_.pickupFactories()) if (f->name() == "TnAmmoCratePickupFactory_13346") shown = !f->hidden();
        check(t > 1.5f && t < 1.85f && std::fabs(p0 - S.power[(size_t)me] - 50.0f) < 0.5f && shown && gi >= 0 && S.generatorOff[(size_t)gi],
              "Ammo generator: 50 power over ~1.67 s (1 per 30 Hz tick), its factory unhidden, its volume off");
        LOG_INFO("ESCTEST ammo bought in %.2f s, power %.0f -> %.0f", t, p0, S.power[(size_t)me]);
        // G1: the Taken chain. Take the crate (reserve emptied): the level drop re-applies UnHide + the volume off, so it stays out;
        // the volume reopens 2 s later. A second take at level 0 only hides it.
        const game::PickupFactory* crate = nullptr;
        for (const game::PickupFactory* f : world_.pickupFactories()) if (f->name() == "TnAmmoCratePickupFactory_13346") crate = f;
        auto take = [&] {
            static core::Vec3 at, away; at = crate->position(); away = at + core::Vec3{6.0f, 0.0f, 0.0f};
            pin = &away; step(); step();   // a fresh touch (the factory reacts to an overlap beginning)
            world_.player().pawn().weapon().reserve = 0;
            pin = &at; for (int k = 0; k < 20 && crate->available(); ++k) step(); pin = nullptr;
            step(); step();   // the director sees the Taken edge on the next step
        };
        if (crate && gi >= 0) {
            take();
            check(!crate->hidden() && S.pool[(size_t)gi] == 0.0f && !S.generatorDone[(size_t)gi] && S.generatorOff[(size_t)gi],
                  "G1: the first take drops the level and re-fires New Power Level: the crate stays out, the volume stays off");
            run(2.2f);
            check(!S.generatorOff[(size_t)gi] && crate->available(), "G1: 2 s later the volume is back; the crate respawned (2 s)");
            take();
            check(crate->hidden(), "G1: a take at level 0 only hides it (until the generator is bought again)");
        }
    }
    run(25.5f);
    check(S.phase == Phase::InWave && S.displayWave == 1 && S.currentWave == 1 && world_.hudState().svWave == 2 && S.roundBegins == 2 && S.roundEnds == 1,
          "wave 2 after TimeBetweenWaves (Round Begin x2, Round End x1)");
    for (int k = 0; k < 120 * 60 && S.phase == Phase::InWave; ++k) {   // clear wave 2 for the door's price
        for (const auto& s : S.slots)
            if (s.inUse && m.players()[(size_t)s.player].alive) world_.applyMatchDamage(s.player, me, 1.0e6f, false, "TransGame.TnDamageTypeIonBlaster");
        step();
    }
    {   // a door generator: 4199 (300 at DrainRate 100 -> 3 per tick = 100 ticks = 3.33 s): doors 11450 / 9615 transform, zone 10523 on
        const float t = buy("4199", 8.0f);
        bool doors = true;
        for (const char* d : {"TnTransformingWorldActor_11450", "TnTransformingWorldActor_9615"}) {
            const int i = world_.transformingActors().find(d);
            doors = doors && i >= 0 && world_.transformingActors().actors()[(size_t)i].desired == 1;
        }
        bool zone = false; for (const auto& z : D.zones) if (z.id == "10523") zone = z.active;
        check(t > 3.2f && t < 3.5f && doors && zone, "Door generator 4199: ~3.33 s (3 per tick), doors 11450 / 9615 transform, zone 10523 activated");
        bool open = false;
        for (const auto& gc : S.generatorCells) if (gc.first == "TnSurvivalGenerator_4199") { open = !gc.second.empty(); for (int c : gc.second) open = open && !world_.botNav().isBlocked(c); }
        if (escNavOn) check(open, "Door generator 4199: its NavigationCells are released (the original's Turn On)");
        LOG_INFO("ESCTEST door bought in %.2f s, power %.0f, %d purchases", t, S.power[(size_t)me], S.purchases);
    }
    {   // a weapon generator: FlakGrenade 1835 (75) unhides its factory; the press takes the bag (a different bag is tossed)
        const float t = buy("1835", 6.0f);
        int k = -1; for (size_t i = 0; i < D.weaponPickups.size(); ++i) if (D.weaponPickups[i].actor == "TnWeaponPickupFactory_10216") k = (int)i;   // 1835 unhides _10216
        check(t > 0.0f && k >= 0 && !S.weaponPicks[(size_t)k].hidden, "FlakGrenade generator: its weapon factory unhidden");
        if (k >= 0) {
            static core::Vec3 at; at = D.weaponPickups[(size_t)k].pos;
            const auto& pc = world_.player().pawn();
            bool hadOther = false;
            for (int i = 0; i < pc.weaponCount(); ++i)
                if (pc.weaponAt(i).fireType == game::WeaponFire::Grenade && pc.weaponAt(i).def && std::string(pc.weaponAt(i).def->id) != "FlakGrenades" &&
                    pc.weaponAt(i).ammo + pc.weaponAt(i).reserve > 0) hadOther = true;
            pin = &at; step(); step(); interact = true; step(); interact = false; step(); pin = nullptr;
            bool flak = false;
            for (int i = 0; i < pc.weaponCount(); ++i) flak = flak || (pc.weaponAt(i).def && std::string(pc.weaponAt(i).def->id) == "FlakGrenades");
            check(flak && !S.weaponPicks[(size_t)k].available && (!hadOther || S.drops.size() == 1),
                  "the press takes the Flak grenade bag; a different bag is tossed as a dropped pickup");
            LOG_INFO("ESCTEST flak taken: had another bag %d, drops %zu, takes %d", (int)hadOther, S.drops.size(), S.weaponTakes);
        }
    }
    {   // Wall_Crashers: a crash wall breaks on a pawn's first touch
        const game::SurvivalData::CrashWall& cw = D.crashWalls[0];
        const game::SurvivalData::Shape& v = D.volumes[(size_t)cw.volume].second;
        static core::Vec3 at; at = (v.mn + v.mx) * 0.5f;
        pin = &at; step(); step(); pin = nullptr;
        float hp = 1.0f;
        for (const game::Destructible* d : world_.destructibles())
            if (d->name().size() >= cw.wall.size() && d->name().compare(d->name().size() - cw.wall.size(), cw.wall.size(), cw.wall) == 0) hp = d->health();
        check(D.crashWalls.size() == 4 && S.crashDone[0] && hp <= 0.0f, "Wall_Crashers: a pawn's first touch breaks the crash wall (200 damage)");
    }
    run(25.5f);
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
