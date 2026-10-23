// Escalation E2b part 1: BrokenHope's Wall_Crashers map Kismet run as data (crash walls, round gates, the terminal window; RE E2b
// answers 45a2a08) and the ESCTEST checks for it and for the item generator's Taken path (G1). WorldSurvival.cpp / Survival.h /
// EscalationData.inc / Application_EscTest.cpp are copied in from the Gameplay worktree (argv[2]). Run from a tree root (29f253cd+).
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
                          ['src/game/EscalationData.inc', 'src/game/EscalationData.inc'], ['docs/handoff/escalation/Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);
ed('src/game/World.h', [
    ['    void svFireGenerator(int generator, bool levelDrop = false);\n',
     '    void svFireGenerator(int generator, bool levelDrop = false);\n    void svTickKismet();   // Wall_Crashers (map Kismet as data)\n'],
]);
console.log('ok');
