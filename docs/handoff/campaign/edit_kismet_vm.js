// Campaign runtime step 1: the Kismet VM (src/game/Kismet.h / .cpp) and WFC_KISMETTEST (src/core/Application_KismetTest.cpp), copied
// in from the Gameplay worktree (argv[2]) - nothing else uses the VM yet (no sim change). Run from a tree root.
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
for (const [src, dst] of [['docs/handoff/campaign/Kismet.h', 'src/game/Kismet.h'], ['docs/handoff/campaign/Kismet.cpp', 'src/game/Kismet.cpp'],
                          ['docs/handoff/campaign/Application_KismetTest.cpp', 'src/core/Application_KismetTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);
ed('src/core/Application.h', [['    void runEscalationTest();', '    void runKismetTest();       // WFC_KISMETTEST (Application_KismetTest.cpp)\n    void runEscalationTest();']]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }',
     '    if (std::getenv("WFC_KISMETTEST")) { runKismetTest(); return false; }      // campaign Kismet VM (no map)\n' +
     '    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }'],
]);
ed('src/core/Application_Frontend.cpp', [['"WFC_TDMTEST", "WFC_ESCTEST",', '"WFC_TDMTEST", "WFC_ESCTEST", "WFC_KISMETTEST",']]);
console.log('ok');
