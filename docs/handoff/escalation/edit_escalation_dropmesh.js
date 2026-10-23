// Escalation: the dropped-weapon mesh lookup for Rendering - gen_escalation.py writes each map's weapon id -> its pickup factory's
// PickupMesh (escalation_geometry weapon_pickups inventory_type -> pickup_mesh) as "dropMeshes"; World::svDropMesh(weaponId) gives the
// content glTF (the pickup mesh, else the held weapon's own). Sources copied from argv[2] (the docs / tools / data / generated .inc).
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
for (const [src, dst] of [['docs/handoff/escalation/Survival.h', 'src/game/Survival.h'], ['docs/handoff/escalation/WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'],
                          ['src/game/EscalationData.inc', 'src/game/EscalationData.inc'], ['tools/gameplay/gen_escalation.py', 'tools/gameplay/gen_escalation.py'],
                          ['data/gameplay/escalation_MP_ESC_BrokenHope.json', 'data/gameplay/escalation_MP_ESC_BrokenHope.json'],
                          ['data/gameplay/escalation_MP_ESC_Remnant.json', 'data/gameplay/escalation_MP_ESC_Remnant.json']])
    fs.copyFileSync(path.join(G, src), dst);
ed('src/game/World.h', [
    ['    int svTestSpawn(const std::string& typePrefix);',
     '    std::string svDropMesh(const std::string& weaponId) const;   // Escalation: a dropped weapon\'s mesh (content-relative glTF; Rendering)\n' +
     '    int svTestSpawn(const std::string& typePrefix);'],
]);
console.log('ok');
