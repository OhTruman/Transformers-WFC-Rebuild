// Escalation: the original NavigationCell nav opt-in again (WFC_ESCNAV=1; Integration's decision after Experimental's broad pass:
// Remnant 0 horde kills, team bots stranded on it) and, on that nav, no strand suicide for the players' team bots either (the cell nav
// is the horde's; a team bot it doesn't cover must not suicide off it). WorldSurvival.cpp / Application_EscTest.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/escalation/Application_EscTest.cpp'), 'src/core/Application_EscTest.cpp');
ed('src/game/WorldBots.cpp', [
    ['                        if (cpSquadMember(b.player)) b.cpRecover = true; else b.suicideWish = true;',
     '                        if (cpSquadMember(b.player)) b.cpRecover = true; else if (!sv_.escNav) b.suicideWish = true;   // (the Escalation cell nav: no suicide)'],
    ['        else if (b.offMeshRun >= 30.0f && !noSuicide && !b.suicideWish && !match_.players()[(size_t)b.player].directorSpawned) {',
     '        else if (b.offMeshRun >= 30.0f && !noSuicide && !b.suicideWish && !match_.players()[(size_t)b.player].directorSpawned && !sv_.escNav) {'],
]);
console.log('ok');
