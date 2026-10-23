// Escalation: the horde never suicides (Integration 2026-10-23: the strand-suicide unstick is a versus-bot PC adaptation; the original
// horde never suicides) and, until E2b part 3's Escalation nav covers every authored spawn point, a PROV stand-in: an off-nav point is
// replaced by the zone's next on-nav point (logged). Plus WFC_ESCLOG's launch list of off-nav points. WorldSurvival.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/escalation/WorldSurvival.cpp'), 'src/game/WorldSurvival.cpp');
ed('src/game/WorldBots.cpp', [
    ['                    if (nowS - b.noWaySince >= 30.0f && !noSuicide) b.suicideWish = true;\n',
     '                    // The Escalation horde never suicides (the original\'s AI does not; the unstick is a versus-bot PC adaptation).\n' +
     '                    if (nowS - b.noWaySince >= 30.0f && !noSuicide && !match_.players()[(size_t)b.player].directorSpawned) b.suicideWish = true;\n'],
    ['        if ((b.offMeshRun += dt) >= 30.0f && !noSuicide && !b.suicideWish) {\n',
     '        if ((b.offMeshRun += dt) >= 30.0f && !noSuicide && !b.suicideWish && !match_.players()[(size_t)b.player].directorSpawned) {\n'],
]);
console.log('ok');
