// Campaign gate item 3 (Integration 2026-10-23): roller mines (TnActorFactoryRollerMine -> TnRollerMine, AssetTools spawnables.json
// roller_mine: the factory's DamageRadius / DamageAmount / Health / Fuse / launch speed). On the head + SENTRIES.
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
    ['// A player interact (TnInteract*) and its TnSeqEvent_Interact events.\n',
     '// A launched roller mine (its hit box / draw: a destructible).\n' +
     'struct CampaignMine { int factory = -1, des = -1; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float fuse = 10.0f, damage = 200.0f, radius = 12.8f, speed = 20.0f; bool done = false; };\n\n' +
     '// A player interact (TnInteract*) and its TnSeqEvent_Interact events.\n'],
    ['    std::vector<CampaignSentry> sentries;\n',
     '    std::vector<CampaignSentry> sentries;\n' +
     '    struct MineFactory { float damage = 200.0f, radius = 12.8f, health = 30.0f, fuse = 10.0f, speed = 20.0f; std::vector<core::Vec3> points; };\n' +
     '    std::map<std::string, MineFactory> mineFactories;   // factory action path -> its authored mine values + spawn points\n' +
     '    std::vector<CampaignMine> mines;\n'],
]);
ed('src/game/World.h', [
    ['    bool cpSentryOpActivate(KismetVM& vm, int op, int input);\n',
     '    bool cpSentryOpActivate(KismetVM& vm, int op, int input);\n' +
     '    void cpLoadMineDefs();\n' +
     '    bool cpMineFactoryActivate(KismetVM& vm, int op, int input, CampaignFactory& F);\n' +
     '    void cpTickMines(float dt);\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadSentryDefs();\n', '        cpLoadSentryDefs();\n        cpLoadMineDefs();\n'],
    ['    if (!cp_.gameOver) cpTickSentries(dt);\n', '    if (!cp_.gameOver) cpTickSentries(dt);\n    if (!cp_.gameOver) cpTickMines(dt);\n'],
]);
console.log('ok');
