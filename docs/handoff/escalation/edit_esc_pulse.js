// Pulse Power_Generator_Action (RE DLC_ESC_MECHANISMS_RE.md section 8): the Zone_Manager door matinees open their doors' collision
// (per-name switchable blockers), on top of edit_esc_mech_visuals.js (run it first; this script re-copies SurvivalMech.h /
// WorldSurvivalMech.cpp / gen_escalation.py from argv[2]). Then run gen_escalation.py. Run from the tree root.
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
ed('src/game/World.h', [
    ['    void svMechAddBlockers();' + NL + '    void svMechSetBlockers(bool on);' + NL,
     '    void svMechAddBlockers();' + NL + '    void svMechSetBlockers(bool on);' + NL +
     '    bool svMechSetBlocker(const std::string& name, bool on);   // one blocking actor (a Pulse door, a Tranquillity gate)' + NL +
     '    std::vector<uint8_t> escBlockerOn_;    // per escBlockerSets_ entry' + NL],
]);
ed('src/game/World.h', [
    ['    const std::vector<HazardVolume>& hazardVolumes() const { return hazards_; }' + NL,
     '    const std::vector<HazardVolume>& hazardVolumes() const { return hazards_; }' + NL +
     '    bool svMechBlockerOn(const std::string& name) const;   // an Escalation mechanism blocker: its collision on (blocking)' + NL],
]);
ed('src/core/Application_EscTest.cpp', [
    ['            if (MD.outage.on) LOG_INFO("ESCTEST %s: power outages so far %d (Round Begins %d)", map.c_str(), MS.outages, S.roundBegins);' + NL,
     '            if (MD.outage.on) LOG_INFO("ESCTEST %s: power outages so far %d (Round Begins %d)", map.c_str(), MS.outages, S.roundBegins);' + NL +
     '            if (!MD.managers.empty()) {   // Pulse: a Zone_Manager door generator opens its zone and, after its matinee, its doors' + NL +
     '                int mk = -1;' + NL +
     '                for (size_t k = 0; k < MD.managers.size() && mk < 0; ++k)' + NL +
     '                    if (!MD.managers[k].blockers.empty() && !MD.managers[k].g.empty() && !S.generatorDone[(size_t)MD.managers[k].g[0]])' + NL +
     '                        for (const auto& a : D.generators[(size_t)MD.managers[k].g[0]].actions) if (a.op == "on" && a.cls == "TnSurvivalZoneVolume") { mk = (int)k; break; }' + NL +
     '                if (mk >= 0) {' + NL +
     '                    const auto& MG = MD.managers[(size_t)mk];' + NL +
     '                    const bool closedBefore = world_.svMechBlockerOn(MG.blockers[0]);' + NL +
     '                    const bool bought = buyGen(MG.g[0]);' + NL +
     '                    run(MG.delay + 1.0f);' + NL +
     '                    int open = 0; for (const auto& b : MG.blockers) open += !world_.svMechBlockerOn(b);' + NL +
     '                    int zonesOn = 0, zones = 0;' + NL +
     '                    for (const auto& a : D.generators[(size_t)MG.g[0]].actions)' + NL +
     '                        if (a.op == "on" && a.cls == "TnSurvivalZoneVolume") for (const auto& Z : D.zones) if ("TnSurvivalZoneVolume_" + Z.id == a.target) { ++zones; zonesOn += Z.active; }' + NL +
     '                    LOG_INFO("ESCTEST %s: %s via generator %s: bought %d, doors closed before %d, open %d / %zu after %.2f s, zones %d / %d on", map.c_str(), MG.seq.c_str(),' + NL +
     '                             MG.gens[0].c_str(), (int)bought, (int)closedBefore, open, MG.blockers.size(), MG.delay, zonesOn, zones);' + NL +
     '                    check(bought && closedBefore && open == (int)MG.blockers.size() && zonesOn == zones && zones > 0,' + NL +
     '                          "Power_Generator_Action: a door generator enables its zone and opens its doors after the matinee");' + NL +
     '                } else check(false, "Power_Generator_Action: a manager door generator to buy");' + NL +
     '            }' + NL],
]);
console.log('ok');
