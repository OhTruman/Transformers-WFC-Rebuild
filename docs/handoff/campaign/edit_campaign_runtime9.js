// Campaign runtime step 9 (anchor edits; on RUNTIME8): destructibles (WorldCampaignDestructibles.cpp, a new file) - the decoded blueprint
// state graphs for D5's Omega battery factories (out/campaign_characters/destructibles) and the placed ones (<chapter>/destructibles.json
// when AssetTools delivers it): hitscan + radius damage, timers, notification links, SeqEvent_HmDestructibleStateNotification, the state
// meshes drawn; plus the touch loader reading <chapter>/physics.json (the levels without a render unit). Sources copied from argv[2].
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
for (const f of ['WorldCampaignDestructibles.cpp', 'WorldCampaignRuntime.cpp', 'WorldCampaignMatinee.cpp'])
    fs.copyFileSync(path.join(G, 'docs/handoff/campaign', f), 'src/game/' + f);
ed('src/game/Campaign.h', [
    ['#include "game/Kismet.h"\n', '#include "game/Kismet.h"\n#include "render/Mesh.h"\n'],
    ['struct CampaignFactory {\n',
     '// A campaign destructible (an HmDestructible blueprint instance): its pieces\' current states, health and timers, its hit box.\n' +
     'struct CampaignDestructible {\n' +
     '    struct State { int handle = 0; std::string name, mesh; float health = 0.0f, autoTime = 0.0f; std::vector<std::pair<int, std::string>> links; };\n' +
     '    struct Piece { std::string name; std::vector<State> states; int cur = 0; float health = 0.0f, timer = 0.0f; State* state(); };\n' +
     '    std::string name;\n' +
     '    core::Vec3 pos{0, 0, 0}, lo{0, 0, 0}, hi{0, 0, 0};\n' +
     '    float yawDeg = 0.0f;\n' +
     '    std::vector<Piece> pieces;\n' +
     '    int lastHitter = -1;\n' +
     '    std::vector<int> events;   // its Kismet notification events (op indices)\n' +
     '};\n' +
     'struct CampaignMeshAsset { bool ok = false; render::MeshData mesh; render::MeshHandle handle = -1; };\n\n' +
     'struct CampaignFactory {\n'],
    ['    std::map<std::string, std::vector<int>> bodyPools;',
     '    std::vector<CampaignDestructible> destructibles;   // step 9\n' +
     '    std::map<std::string, std::vector<int>> bodyPools;'],
]);
ed('src/game/World.h', [
    ['    void cpLoadTouches();\n',
     '    void cpLoadTouches();\n' +
     '    int cpSpawnDestructible(const std::string& blueprint, const std::string& name, const core::Vec3& at, float yawDeg);   // WorldCampaignDestructibles.cpp\n' +
     '    void cpDesBounds(int i);\n' +
     '    void cpDesTransition(int i, size_t piece, int to);\n' +
     '    void cpDamageDes(int i, float amount, int instigator, const std::string& type);\n' +
     '    int cpDesRayHit(const core::Vec3& o, const core::Vec3& dir, float range, float& t) const;\n' +
     '    void cpRadiusDes(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type);\n' +
     '    void cpTickDestructibles(float dt);\n' +
     '    void cpDrawDestructibles(render::IRenderer& r) const;\n' +
     '    void cpBindDesEvents(int i);\n' +
     '    void cpLoadPlacedDestructibles();\n' +
     '    std::map<std::string, CampaignMeshAsset> cpDesMeshes_;   // destructible state meshes (by content glTF)\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadTouches();\n', '        cpLoadTouches();\n        cpLoadPlacedDestructibles();\n'],
    ['    if (!cp_.gameOver) cpTickTouches();\n', '    if (!cp_.gameOver) cpTickTouches();\n    cpTickDestructibles(dt);\n'],
]);
ed('src/game/World.cpp', [
    ['    float dist = (hitTarget || hitDes || hitOpp || hitBarrier) ? targetDist : bestDist;\n',
     '    int cpHit = -1;   // campaign destructibles (in front of any closer hit)\n' +
     '    if (cp_.active) { float tc; const int ci = cpDesRayHit(origin, dir, range, tc); if (ci >= 0 && tc < targetDist) { targetDist = tc; cpHit = ci; hitTarget = nullptr; hitDes = nullptr; hitOpp = nullptr; hitBarrier = false; } }\n' +
     '    if (cpHit >= 0) cpDamageDes(cpHit, w.damageAt(targetDist), localPlayer_, w.damageType ? w.damageType : "");\n' +
     '    float dist = (hitTarget || hitDes || hitOpp || hitBarrier || cpHit >= 0) ? targetDist : bestDist;\n'],
    ['    svRadiusHit_ = true;\n', '    svRadiusHit_ = true;\n    if (cp_.active) cpRadiusDes(at, damage, radius, instigator, type);   // campaign destructibles\n'],
    ['    for (const BarrierState& bs : barriers_) if (bs.alive', '    if (cp_.active) cpDrawDestructibles(r);   // campaign destructibles\n    for (const BarrierState& bs : barriers_) if (bs.alive'],
]);
ed('src/game/Kismet.cpp', [
    ['            const int inc = o.authored["IncrementAmount"].asInt(o.authored["bIncrement"].asBool(false) ? 1 : 0);',
     '            const int inc = o.authored["IncrementAmount"].asInt(1);   // Engine.Default__SeqAct_Switch IncrementAmount 1 [C, RE]'],
]);
ed('src/game/Kismet.cpp', [
    ['    fireEvents("SeqEvent_GameplayStarted", bound);   // queued: runs with the first normal tick [C]\n',
     '    // A chapter start (no launch CP): the pre-gameplay pass made a checkpoint active (A1: Checkpoint_14845, D5: 4578), and that\n' +
     '    // checkpoint\'s bound GameplayStarted fires (A1 GameplayStarted_7189 -> LevelBegin) [C data, RE A1_OPENING; PROV rule].\n' +
     '    const std::string gsCp = checkpoint.empty() ? activeCheckpoint : checkpoint;\n' +
     '    fireEvents("SeqEvent_GameplayStarted", [&](const Op& o) {\n' +
     '        const std::string org = o.authored["Originator"].asString();\n' +
     '        bool fire;\n' +
     '        if (org.find("Checkpoint") != std::string::npos) fire = !gsCp.empty() && endsWith(org, gsCp);\n' +
     '        else fire = checkpoint.empty() || o.authored["bFireForAllCheckpoints"].asBool(false);\n' +
     '        if (fire) ops_[(size_t)find(o.path)].triggerCount = 0;\n' +
     '        return fire;\n' +
     '    });   // queued: runs with the first normal tick [C]\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    if (cpAiOpActivate(vm, op, input)) return true;',
     '    if (c == "SeqAct_ToggleCinematicMode") {   // Enable / Disable / Toggle: the local player\'s input and HUD [H CDO: HUD off, movement / turning / input off]\n' +
     '        cp_.cinematicMode = input == 0 ? true : input == 1 ? false : !cp_.cinematicMode;\n' +
     '        LOG_INFO("campaign: cinematic mode %s (%s)", cp_.cinematicMode ? "on" : "off", o.path.substr(o.path.find_last_of(\'.\') + 1).c_str());\n' +
     '        if (!o.outputs.empty()) vm.output(op, o.outputs.front().first);\n' +
     '        return true;\n' +
     '    }\n' +
     '    if (cpAiOpActivate(vm, op, input)) return true;'],
]);
ed('src/game/Campaign.h', [
    ['    std::vector<CampaignDestructible> destructibles;   // step 9\n',
     '    std::vector<CampaignDestructible> destructibles;   // step 9\n    bool cinematicMode = false;               // SeqAct_ToggleCinematicMode: the local player\'s input + HUD off\n'],
]);
ed('src/game/World.cpp', [
    ['    player_.controller().handleInput(in, dt);   // Dash (Shift) is latched by PlayerController\n',
     '    player_.controller().handleInput(cp_.active && cp_.cinematicMode ? platform::InputFrame{} : in, dt);   // Dash (Shift) is latched by PlayerController; campaign cinematic mode: no input\n'],
    ['    h.cpActive = cp_.active;', '    h.cpCinematic = cp_.active && cp_.cinematicMode;\n    h.cpActive = cp_.active;'],
]);
ed('src/game/World.h', [
    ['    bool cpActive = false, cpGameOver = false, cpMissionComplete = false; std::string cpCheckpoint;\n',
     '    bool cpActive = false, cpGameOver = false, cpMissionComplete = false; std::string cpCheckpoint;\n    bool cpCinematic = false;   // SeqAct_ToggleCinematicMode on: the HUD hides\n'],
]);
ed('src/game/Campaign.h', [
    ['bool looping = false, rewindOnPlay = false, playing = false; };', 'bool looping = false, rewindOnPlay = false, playing = false; bool viewing = false; float lastFade = 0.0f; };'],
    ['    int cineOp = -1, cineViewOp = -1;', '    bool viewOn = false;                      // the CinematicView sent active\n    int cineOp = -1, cineViewOp = -1;'],
]);
ed('src/game/World.h', [
    ['    void cpMatineeEnd(CampaignState::Interp& in);\n', '    void cpMatineeEnd(CampaignState::Interp& in);\n    void cpFlushCinematicView();\n'],
]);
console.log('ok');
