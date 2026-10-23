// Campaign gate item 2 (Integration 2026-10-23): the pawn factories' death outputs (Single / All Pawns Killed; Spawned per pawn +
// the Spawned variable) and the sentries (TnActorFactorySentryAI + TURRETDEF from AssetTools out/campaign_characters/spawnables.json:
// a stationary AI turret per spawn point, its HP from the sentry DES, its weapon fire block, SeqAct_TnAiSentryForceTarget / SetDormant).
// WorldCampaignRuntime.cpp / WorldCampaignDestructibles.cpp copied from argv[2]; anchor edits on the head.
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
for (const f of ['WorldCampaignRuntime.cpp', 'WorldCampaignDestructibles.cpp'])
    fs.copyFileSync(path.join(G, 'docs/handoff/campaign', f), path.join('src/game', f));
ed('src/game/Campaign.h', [
    ['    std::vector<int> spawnedDes;   // a destructible factory\'s spawned actors (Kill All)\n',
     '    std::vector<int> spawnedDes;   // a destructible factory\'s spawned actors (Kill All)\n' +
     '    std::vector<int> live;         // its spawned pawns still alive (Single / All Pawns Killed)\n' +
     '    std::vector<int> liveSentries; // a sentry factory\'s living sentries (CampaignState::sentries indices)\n'],
    ['    std::vector<int> events;   // its Kismet notification events (op indices)\n',
     '    std::vector<int> events;   // its Kismet notification events (op indices)\n' +
     '    bool fixedBounds = false;  // the hit box is set by its owner (a sentry), not from the state meshes\n'],
    ['// A player interact (TnInteract*) and its TnSeqEvent_Interact events.\n',
     '// A sentry TURRETDEF (AssetTools spawnables.json) and a spawned sentry (a stationary AI turret on its placed body).\n' +
     'struct CampaignSentryDef { std::string turretdef, weapon; float range = 80.0f, sight = 100.0f, lagDeg = 90.0f, aimDeg = 3.0f, deathLife = 1.0f, hp = 125.0f; };\n' +
     'struct CampaignSentry {\n' +
     '    int def = -1, factory = -1, des = -1, forced = -1;\n' +
     '    std::string body;                 // the placed turret body actor (hidden after its death)\n' +
     '    core::Vec3 pos{0, 0, 0};\n' +
     '    float yaw = 0.0f, cool = 0.0f, deadT = 0.0f;\n' +
     '    bool dead = false, dormant = false;\n' +
     '};\n\n' +
     '// A player interact (TnInteract*) and its TnSeqEvent_Interact events.\n'],
    ['    CampaignOmega omega;\n',
     '    CampaignOmega omega;\n' +
     '    size_t factoryKillsSeen = 0;               // the kill history read by the factory death outputs\n' +
     '    struct SentryFactory { int def = -1, count = 1; std::vector<core::Vec3> points; std::vector<std::string> bodies; };\n' +
     '    std::vector<CampaignSentryDef> sentryDefs;\n' +
     '    std::map<std::string, SentryFactory> sentryFactories;   // factory action path -> its TURRETDEF + spawn points\n' +
     '    std::vector<CampaignSentry> sentries;\n'],
]);
ed('src/game/World.h', [
    ['    void cpTickTransformOps();\n',
     '    void cpTickTransformOps();\n' +
     '    void cpTickFactoryKills();\n' +
     '    void cpFactoryPawnDied(CampaignFactory& F, int victim);\n' +
     '    void cpLoadSentryDefs();\n' +
     '    bool cpSentryFactoryActivate(KismetVM& vm, int op, int input, CampaignFactory& F);\n' +
     '    void cpTickSentries(float dt);\n' +
     '    bool cpSentryOpActivate(KismetVM& vm, int op, int input);\n'],
]);
ed('src/core/Application_CampaignTest.cpp', [
    ['    check(hdist(world_.player().pawn().position(), C.playerStart.pos) < 3.0f, "the human starts at TnCampaignPlayerStart_16313");\n',
     '    check(hdist(world_.player().pawn().position(), C.checkpoints.front().pos) < 3.0f, "the human starts at the chapter\'s first checkpoint (Stage 1, 4578)");\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    const CampaignChapter::Checkpoint* cp = cpChapter_.checkpoint(checkpointName);\n',
     '    const CampaignChapter::Checkpoint* cp = cpChapter_.checkpoint(checkpointName);\n' +
     '    // the chapter start: the first checkpoint in DisplayOrder (the one the pre-gameplay pass makes active; A2\'s TnCampaignPlayerStart\n' +
     '    // sits in the Soundwave arena, a leftover); the player start only for a chapter without checkpoints\n' +
     '    if (!cp && checkpointName.empty() && !cpChapter_.checkpoints.empty()) cp = &cpChapter_.checkpoints.front();\n'],
    ['        cpLoadInteracts();\n', '        cpLoadInteracts();\n        cpLoadSentryDefs();\n'],
    ['    if (cpTransformWorldProp(vm, op, input)) return true;   // doors / pods\n',
     '    if (cpTransformWorldProp(vm, op, input)) return true;   // doors / pods\n    if (cpSentryOpActivate(vm, op, input)) return true;     // sentry force target / dormant\n'],
    ['    cpTickTransformOps();\n', '    cpTickTransformOps();\n    cpTickFactoryKills();\n    if (!cp_.gameOver) cpTickSentries(dt);\n'],
]);
console.log('ok');
