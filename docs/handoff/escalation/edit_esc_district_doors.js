// District doors (DLC): the versus / Escalation transforming-actor loader falls back to AssetTools' per-map
// maps/<map>/transforming_actors_gameplay.json (the DLC maps' format) when mp_transforming_actors.json has no rows for the map.
// gen_escalation.py (copied from argv[2]) flattens the generators' Zone_Manager / Zone subsequence variables into actions; run it
// after this script to regenerate data/gameplay + EscalationData.inc. Run from the tree root.
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
fs.copyFileSync(path.join(G, 'docs/handoff/escalation/gen_escalation.py'), 'tools/gameplay/gen_escalation.py');
ed('src/game/TransformingActors.cpp', [
    ['    campaign_ = false;\n    return loadFile(manifestRoot + "/maps/mp_transforming_actors.json", mapName, manifestRoot + "/..", pawn, weapon);\n',
     '    campaign_ = false;\n' +
     '    if (loadFile(manifestRoot + "/maps/mp_transforming_actors.json", mapName, manifestRoot + "/..", pawn, weapon) && !actors_.empty()) return true;\n' +
     '    // the DLC maps (District): AssetTools writes their rows to a per-map file in the campaign format instead\n' +
     '    return loadFile(manifestRoot + "/maps/" + mapName + "/transforming_actors_gameplay.json", mapName, manifestRoot + "/..", pawn, weapon);\n'],
]);
ed('src/game/TransformingActors.h', [
    ['    // Loads this map\'s actors (none: an empty list, nothing changes) and adds their collision sets to pawn / weapon.\n',
     '    // Loads this map\'s actors (none: an empty list, nothing changes) and adds their collision sets to pawn / weapon. Rows from\n' +
     '    // mp_transforming_actors.json, else the map\'s own maps/<map>/transforming_actors_gameplay.json (the DLC maps).\n'],
]);
{   // WFC_ESCTEST DLC branch: a door generator check (esc_door_check.cpp.txt) after the generator purchase
    const anchor = '        check(gi >= 0 && S.generatorDone[(size_t)gi], "a generator is bought (stand in its volume, hold Interact)");\n';
    const block = fs.readFileSync(path.join(G, 'docs/handoff/escalation/esc_door_check.cpp.txt'), 'utf8').replace(/\r\n/g, '\n');
    ed('src/core/Application_EscTest.cpp', [[anchor, anchor + block]]);
}
console.log('ok');
