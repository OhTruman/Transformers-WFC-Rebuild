// WFC_ESCTEST on the DLC Escalation maps (Tranquillity / District / Pulse / Static): the generated data loads, waves advance (the
// guarded players kill each spawned horde member), every enemy type due by the reached wave spawns (District: the Autobot jet
// soldier), and a generator is bought. Run from the tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application_EscTest.cpp', [
    ['    check(game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=ESC", L) && L.settings.survival && L.settings.goalScore == 0 &&\n',
     '    if (world_.mapName() != "MP_ESC_BrokenHope") {   // the DLC maps (generated data): waves advance, the due types spawn, a generator works\n' +
     '        const std::string map = world_.mapName();\n' +
     '        check(game::MatchLaunch::fromURL(map + "?GameModeTag=ESC", L) && world_.launchMatch(L), "launchMatch(ESC) on the map");\n' +
     '        const game::SurvivalData& D = world_.survivalData();\n' +
     '        const game::SurvivalState& S = world_.survival();\n' +
     '        check(D.ok && !D.waves.empty() && !D.enemies.empty() && !D.zones.empty() && !D.generators.empty(), "the generated data loads");\n' +
     '        LOG_INFO("ESCTEST %s: %zu waves, %zu enemy entries, %zu zones, %zu generators, %zu horde slots, players team %d", map.c_str(),\n' +
     '                 D.waves.size(), D.enemies.size(), D.zones.size(), D.generators.size(), S.slots.size(),\n' +
     '                 world_.match().players()[(size_t)world_.localMatchPlayer()].team);\n' +
     '        const int me = world_.localMatchPlayer();\n' +
     '        std::vector<int> spawnedByEntry(D.enemies.size(), 0);\n' +
     '        std::vector<uint8_t> seen(S.slots.size(), 0);\n' +
     '        const int target = 6;   // waves reached (CurrentWave index): past District\'s first jet row (MinWave 3)\n' +
     '        float t = 0.0f, killEvery = 0.0f;\n' +
     '        while (t < 1500.0f && S.currentWave < target && S.phase != Phase::GameOver) {\n' +
     '            step(); t += dt; killEvery += dt;\n' +
     '            for (size_t k = 0; k < S.slots.size(); ++k) {\n' +
     '                const auto& sl = S.slots[k];\n' +
     '                const bool live = sl.inUse && sl.player >= 0 && world_.participantPawn(sl.player);\n' +
     '                if (live && !seen[k] && sl.enemy >= 0 && (size_t)sl.enemy < spawnedByEntry.size()) ++spawnedByEntry[(size_t)sl.enemy];\n' +
     '                seen[k] = live;\n' +
     '            }\n' +
     '            if (killEvery >= 2.0f) {   // the guarded players clear the horde every 2 s (so the waves advance)\n' +
     '                killEvery = 0.0f;\n' +
     '                for (const auto& sl : S.slots)\n' +
     '                    if (sl.inUse && sl.player >= 0 && world_.participantPawn(sl.player)) world_.applyMatchDamage(sl.player, me, 1.0e7f, false, "TnDamageType");\n' +
     '            }\n' +
     '        }\n' +
     '        LOG_INFO("ESCTEST %s: wave index %d after %.0f s, %d spawns, %d failed, power %.0f", map.c_str(), S.currentWave, t, S.spawns, S.failedSpawns,\n' +
     '                 (size_t)me < S.power.size() ? S.power[(size_t)me] : -1.0f);\n' +
     '        check(S.currentWave >= target, "the waves advance (6 waves cleared)");\n' +
     '        check(S.spawns > 0 && S.failedSpawns * 10 <= S.spawns, "the horde spawns (failed spawns under 10 %)");\n' +
     '        bool due = true;\n' +
     '        for (size_t e = 0; e < D.enemies.size(); ++e) {\n' +
     '            LOG_INFO("ESCTEST %s: %s (waves %d-%d) spawned %d", map.c_str(), D.enemies[e].type.c_str(), D.enemies[e].minWave, D.enemies[e].maxWave, spawnedByEntry[e]);\n' +
     '            if (D.enemies[e].minWave < S.currentWave - 1 && D.enemies[e].maxCount > 0 && spawnedByEntry[e] == 0) due = false;\n' +
     '        }\n' +
     '        check(due, "every enemy row due by the reached wave spawned");\n' +
     '        if (map == "MP_ESC_District") {\n' +
     '            int jets = 0; for (size_t e = 0; e < D.enemies.size(); ++e) if (D.enemies[e].type.find("Soldier_Jet_Autobot") != std::string::npos) jets += spawnedByEntry[e];\n' +
     '            check(jets > 0, "District: the Autobot jet soldier (AiEscalation_Soldier_Jet_Autobot_AICD) spawns");\n' +
     '        }\n' +
     '        int gi = -1;   // the cheapest generator the player can afford\n' +
     '        for (size_t g = 0; g < D.generators.size(); ++g)\n' +
     '            if ((size_t)me < S.power.size() && D.generators[g].cost <= S.power[(size_t)me] && (gi < 0 || D.generators[g].cost < D.generators[(size_t)gi].cost)) gi = (int)g;\n' +
     '        if (gi >= 0) {\n' +
     '            const game::SurvivalData::Generator& G = D.generators[(size_t)gi];\n' +
     '            static core::Vec3 at; at = (G.mn + G.mx) * 0.5f; at.y = G.mn.y + 0.5f;\n' +
     '            pin = &at; interact = false; step(); step(); interact = true;\n' +
     '            float b = 0.0f; while (b < 20.0f && !S.generatorDone[(size_t)gi]) { step(); b += dt; }\n' +
     '            interact = false; step(); pin = nullptr;\n' +
     '            LOG_INFO("ESCTEST %s: generator %s (cost %d) bought in %.2f s", map.c_str(), G.id.c_str(), G.cost, S.generatorDone[(size_t)gi] ? b : -1.0f);\n' +
     '        }\n' +
     '        check(gi >= 0 && S.generatorDone[(size_t)gi], "a generator is bought (stand in its volume, hold Interact)");\n' +
     '        LOG_INFO("ESCTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n' +
     '        exitCode_ = fails ? 1 : 0;\n' +
     '        return;\n' +
     '    }\n' +
     '    check(game::MatchLaunch::fromURL("MP_ESC_BrokenHope_Base_m?GameModeTag=ESC", L) && L.settings.survival && L.settings.goalScore == 0 &&\n'],
]);
console.log('ok');
