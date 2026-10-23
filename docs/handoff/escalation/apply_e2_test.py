# One-off source edit: ESCTEST for E2 (docs/handoff/escalation/Application_EscTest.cpp): the 15 s countdown + F1 (wave 1 at 30 s from
# level start), RE's initial zone order test vector, a Remnant branch (wave 1 spawns at 5771), power from kills, an item generator
# purchase (drain timing at 30 Hz, the factory unhidden, the volume off) and a door generator (doors transform, zone 10523 on).
import sys
p = 'docs/handoff/escalation/Application_EscTest.cpp'
s = open(p, encoding='utf-8').read()

def rep(a, b):
    global s
    if s.count(a) != 1: sys.exit('anchor: ' + a[:80])
    s = s.replace(a, b)

rep("""    bool guard = true, guardAlly = true; game::MatchOpponent* ally = nullptr;
    auto step = [&] {
        platform::InputFrame none; world_.handleInput(none, dt); world_.tick(dt);""",
"""    bool guard = true, guardAlly = true; game::MatchOpponent* ally = nullptr;
    bool interact = false, wasInteract = false;   // the local player's Interact button (generator purchases)
    const core::Vec3* pin = nullptr;              // keep the local pawn here (standing in a generator's volume)
    auto step = [&] {
        platform::InputFrame in;
        in.down[(int)platform::Button::Interact] = interact;
        in.pressed[(int)platform::Button::Interact] = interact && !wasInteract;
        wasInteract = interact;
        if (pin && !world_.localPlayerDead()) { world_.player().pawn().setPosition(*pin); world_.player().pawn().velocity() = {0, 0, 0}; }
        world_.handleInput(in, dt); world_.tick(dt);""")

rep("""    game::MatchLaunch L;
    check(game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=ESC", L)""",
"""    auto sortedMatches = [&](const game::SurvivalData& D, const game::SurvivalState& S) {   // RE's initial order per type (the binary's qsort)
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
        run(5.0f);
        check(S.failedSpawns == 0 && S.spawns >= 3, "Remnant: wave 1 keeps spawning (no failed spawn)");
        LOG_INFO("ESCTEST Remnant: %d spawns, %d failed", S.spawns, S.failedSpawns);
        LOG_INFO("ESCTEST SUMMARY: %d/%d checks passed", checks - fails, checks);
        exitCode_ = fails ? 1 : 0;
        return;
    }
    check(game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=ESC", L)""")

rep("""    check(S.slots.size() == 46 && S.phase == Phase::PreGame, "46 horde slots (sum of MaxCount), PreGame");""",
"""    check(S.slots.size() == 46 && S.phase == Phase::PreGame, "46 horde slots (sum of MaxCount), PreGame");
    check(sortedMatches(D, S), "the initial sorted zone order of every type equals RE's (actor order + MSVC qsort)");
    check(D.generators.size() == 35 && m.settings().matchAutoStartCountdown == 15, "35 generators; the 15 s auto-start countdown");
    {
        int hidden = 0; for (const game::PickupFactory* f : world_.pickupFactories()) hidden += f->hidden();
        check(hidden == 6, "the 6 bought pickup factories start hidden");
    }""")

rep("""    run(10.5f);
    int hordeAlive = 0;""", """    run(15.5f);
    int hordeAlive = 0;""")
rep("""    check(world_.hudState().svWave == 1 && world_.hudState().svNextWaveTime > 25, "HUD: wave 1 pending, PreGame countdown");
    run(29.0f);
    check(S.phase == Phase::PreGame, "still PreGame before 30 s");
    run(1.5f);
    check(S.phase == Phase::InWave && S.displayWave == 0 && S.currentWave == 0 && S.spawns >= 1, "wave 1 begins after PreGameTime, first enemy at once");""",
"""    check(world_.hudState().svWave == 1 && world_.hudState().svNextWaveTime > 10 && world_.hudState().svNextWaveTime <= 15,
          "HUD: wave 1 pending, PreGame countdown (30 s from level start)");
    run(14.0f);
    check(S.phase == Phase::PreGame, "still PreGame before 30 s from level start");
    run(1.0f);
    check(S.phase == Phase::InWave && S.displayWave == 0 && S.currentWave == 0 && S.spawns >= 1, "wave 1 begins 30 s after level start (F1), first enemy at once");""")

rep("""    LOG_INFO("ESCTEST wave 1: %d horde killed, %d used of %d", killed, S.usedThisWave, D.waves[0].budget);""",
"""    LOG_INFO("ESCTEST wave 1: %d horde killed, %d used of %d", killed, S.usedThisWave, D.waves[0].budget);
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
    }""")

rep("""    check(S.phase == Phase::InWave && S.displayWave == 1 && S.currentWave == 1 && world_.hudState().svWave == 2, "wave 2 after TimeBetweenWaves");""",
"""    check(S.phase == Phase::InWave && S.displayWave == 1 && S.currentWave == 1 && world_.hudState().svWave == 2 && S.roundBegins == 2 && S.roundEnds == 1,
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
        LOG_INFO("ESCTEST door bought in %.2f s, power %.0f, %d purchases", t, S.power[(size_t)me], S.purchases);
    }
    run(25.5f);""")
s = s.replace('#include <cmath>\n', '#include <algorithm>\n#include <cmath>\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('ok')
