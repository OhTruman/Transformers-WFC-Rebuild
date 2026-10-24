// Static (RE DLC_ESC_MECHANISMS_RE.md section 9): the DoorGenerators are root Toggle chains (the flattened generator actions, no new
// code); the fire-jet trap generators are a timed mechanism (SurvivalMech.h Trap). Copies SurvivalMech.h / WorldSurvivalMech.cpp /
// gen_escalation.py from argv[2] and adds ESCTEST's trap check. Then run gen_escalation.py. Run from the tree root.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
const H = path.join(G, 'docs/handoff/escalation');
fs.copyFileSync(path.join(H, 'SurvivalMech.h'), 'src/game/SurvivalMech.h');
fs.copyFileSync(path.join(H, 'WorldSurvivalMech.cpp'), 'src/game/WorldSurvivalMech.cpp');
fs.copyFileSync(path.join(H, 'gen_escalation.py'), 'tools/gameplay/gen_escalation.py');
ed('src/core/Application_EscTest.cpp', [
    ['            if (MD.outage.on) LOG_INFO("ESCTEST %s: power outages so far %d (Round Begins %d)", map.c_str(), MS.outages, S.roundBegins);' + NL,
     '            if (MD.outage.on) LOG_INFO("ESCTEST %s: power outages so far %d (Round Begins %d)", map.c_str(), MS.outages, S.roundBegins);' + NL +
     '            if (!MD.traps.empty()) {   // Static: a fire-jet trap - its pain volumes off at start, on when bought, off after its matinee' + NL +
     '                const auto& T = MD.traps[0];' + NL +
     '                const int before = hazardOn(T.hazards.front());' + NL +
     '                const bool bought = buyGen(T.g);' + NL +
     '                const int on = hazardOn(T.hazards.front());' + NL +
     '                run(T.time + 1.0f);' + NL +
     '                const int after = hazardOn(T.hazards.front());' + NL +
     '                run(T.cooldown + 1.0f);' + NL +
     '                const bool again = T.g >= 0 && !S.generatorDone[(size_t)T.g] && !S.generatorOff[(size_t)T.g];' + NL +
     '                LOG_INFO("ESCTEST %s: trap %s (%zu volumes): off at start %d, bought %d, on %d, off after %.1f s %d, re-buyable after %.1f s %d", map.c_str(), T.gen.c_str(),' + NL +
     '                         T.hazards.size(), (int)(before == 0), (int)bought, on, T.time, (int)(after == 0), T.cooldown, (int)again);' + NL +
     '                check(before == 0 && bought && on == 1 && after == 0 && again, "fire-jet trap: off at start, on when bought, off after its matinee, re-buyable");' + NL +
     '            }' + NL],
]);
console.log('ok');
