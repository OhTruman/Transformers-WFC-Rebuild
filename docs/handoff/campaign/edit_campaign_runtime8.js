// Campaign runtime step 8 (anchor edits; on RUNTIME7): SeqEvent_Touch for every touch event the level audio does not host
// (TriggerVolume brushes from the units' physics.json, Trigger cylinders from actors.json; Touched / UnTouched / Empty).
// WorldCampaignRuntime.cpp copied from argv[2].
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
    ['#include <map>\n', '#include <map>\n#include <set>\n'],
    ['    std::map<std::string, std::vector<int>> bodyPools;',
     '    struct Touch { int op = -1; bool playerOnly = true, cylinder = false; std::vector<std::vector<core::Vec3>> polys; core::Vec3 lo{0, 0, 0}, hi{0, 0, 0}, center{0, 0, 0};\n' +
     '                   float radius = 0.0f, halfHeight = 0.0f; std::set<int> inside; };\n' +
     '    std::vector<Touch> touches;               // step 8: SeqEvent_Touch\n' +
     '    std::map<std::string, std::vector<int>> bodyPools;'],
]);
ed('src/game/World.h', [
    ['    void cpTickForms();\n', '    void cpTickForms();\n    void cpLoadTouches();\n    void cpTickTouches();\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpBindMoverNav();', '        cpLoadTouches();\n        cpBindMoverNav();'],
    ['    cpTickForms();\n', '    cpTickForms();\n    if (!cp_.gameOver) cpTickTouches();\n'],
]);
console.log('ok');
