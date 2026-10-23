// Campaign SeqAct_ToggleHidden / SeqAct_Destroy on world actors (Integration 2026-10-23): the targets' visibility through
// setActorHidden (the authored bHidden as the start state), Destroy = hidden + collision off for movers / transforming actors,
// destructibles removed, sentries killed. WorldCampaignRuntime.cpp copied from argv[2]; anchor edits on the head.
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
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignRuntime.cpp'), 'src/game/WorldCampaignRuntime.cpp');
ed('src/game/Campaign.h', [
    ['    std::vector<CampaignMine> mines;\n',
     '    std::vector<CampaignMine> mines;\n' +
     '    std::map<std::string, bool> actorHidden;   // world actor -> hidden (authored bHidden, then ToggleHidden / Destroy)\n' +
     '    std::set<std::string> actorDestroyed;      // SeqAct_Destroy\'d world actors (they stay hidden)\n'],
]);
ed('src/game/World.h', [
    ['    void cpTickMines(float dt);\n', '    void cpTickMines(float dt);\n    bool cpHideOpActivate(KismetVM& vm, int op, int input);\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    if (cpSentryOpActivate(vm, op, input)) return true;     // sentry force target / dormant\n',
     '    if (cpSentryOpActivate(vm, op, input)) return true;     // sentry force target / dormant\n' +
     '    if (cpHideOpActivate(vm, op, input)) return true;       // SeqAct_ToggleHidden / SeqAct_Destroy on world actors\n'],
]);
let h = fs.readFileSync('src/game/Campaign.h', 'utf8');
if (!/#include <set>/.test(h)) {
    h = h.replace(/#include <map>(\r?\n)/, (m, nl) => m + '#include <set>' + nl);
    if (!/#include <set>/.test(h)) throw new Error('Campaign.h: no #include <map> to put <set> after');
    fs.writeFileSync('src/game/Campaign.h', h);
}
console.log('ok');
