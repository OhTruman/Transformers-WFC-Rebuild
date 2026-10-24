// Escalation DLC mechanisms, presentation data for Rendering: the visual actors in the mechanisms JSON (gen_escalation.py) and their
// matinee positions advanced by the sim (SurvivalMech.h / WorldSurvivalMech.cpp, copied from argv[2]) + an ESCTEST check. Run from
// the tree root, then gen_escalation.py.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
const H = path.join(G, 'docs/handoff/escalation');
fs.copyFileSync(path.join(H, 'SurvivalMech.h'), 'src/game/SurvivalMech.h');
fs.copyFileSync(path.join(H, 'WorldSurvivalMech.cpp'), 'src/game/WorldSurvivalMech.cpp');
fs.copyFileSync(path.join(H, 'gen_escalation.py'), 'tools/gameplay/gen_escalation.py');
ed('src/core/Application_EscTest.cpp', [
    ['            if (MD.outage.on) LOG_INFO("ESCTEST %s: power outages so far %d (Round Begins %d)", map.c_str(), MS.outages, S.roundBegins);\n',
     '            if (MD.outage.on) LOG_INFO("ESCTEST %s: power outages so far %d (Round Begins %d)", map.c_str(), MS.outages, S.roundBegins);\n' +
     '            {   // the presentation data (Rendering): every laser set with its 9 emitters / button / switch, every sentry its body\n' +
     '                bool ok = true;\n' +
     '                for (const auto& L : MD.lasers) ok = ok && L.emitters.size() == 9 && !L.buttonActor.empty() && !L.switchActor.empty() && !L.switchInterp.empty();\n' +
     '                for (const auto& T : MD.sentries) ok = ok && !T.body.empty() && !T.templ.empty() && !T.emitters.empty();\n' +
     '                if (MD.outage.on) ok = ok && !MD.outage.ppvOn.empty() && !MD.outage.interp.empty();\n' +
     '                for (const auto& F : MD.floods) ok = ok && !F.waterfallInterp.empty() && !F.materialInterp.empty();\n' +
     '                if (!MD.floods.empty()) ok = ok && !MD.drainInterp.empty() && MD.baseDoors.size() == 2 && !MD.unhideAtDrain.empty();\n' +
     '                LOG_INFO("ESCTEST %s: presentation actors %s (lasers %zu, sentries %zu, floodgates %zu, base doors %zu)", map.c_str(), ok ? "complete" : "MISSING",\n' +
     '                         MD.lasers.size(), MD.sentries.size(), MD.floods.size(), MD.baseDoors.size());\n' +
     '                if (MD.any()) check(ok, "the mechanisms\' presentation actors are all named");\n' +
     '            }\n'],
    ['                check(bought == (int)MD.floods.size() && drained && MS.gatesOpen && zonesOn == (int)MD.zonesOn.size(),\n' +
     '                      "floodgates: three purchases drain the base; its zones on, the base doors open");\n',
     '                check(bought == (int)MD.floods.size() && drained && MS.gatesOpen && zonesOn == (int)MD.zonesOn.size(),\n' +
     '                      "floodgates: three purchases drain the base; its zones on, the base doors open");\n' +
     '                run(MD.doorLength);   // the door matinees play on to their end\n' +
     '                LOG_INFO("ESCTEST %s: drain matinee at %.2f / %.2f s, door matinees at %.2f / %.2f s", map.c_str(), MS.drainT, MD.drainLength, MS.doorPos, MD.doorLength);\n' +
     '                check(std::fabs(MS.drainT - MD.drainLength) < 0.05f && std::fabs(MS.doorPos - MD.doorLength) < 0.05f,\n' +
     '                      "matinee positions: the drain at its end, the base doors played to their end");\n'],
]);
console.log('ok');
