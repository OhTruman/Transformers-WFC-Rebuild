// Campaign runtime step 5 (anchor edits; on RUNTIME4): Matinee (WorldCampaignMatinee.cpp, a new file) - SeqAct_Interp playback, event
// keys, InterpActor movers (moving collision + the renderer's actor transform + pawns carried), the director's camera / FOV / fade.
// Renderer.h gains setMapActorTransform (default no-op; Rendering implements it). WorldCampaignMatinee.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignMatinee.cpp'), 'src/game/WorldCampaignMatinee.cpp');
ed('src/game/Campaign.h', [
    ['struct CampaignFactory {\n',
     '// A matinee (InterpData) of the chapter: the tracks the runtime plays (movement, event keys, director cuts, fade, FOVAngle).\n' +
     'struct CampaignMatinee {\n' +
     '    struct Key { float t = 0.0f; float v[3] = {0, 0, 0}, ai[3] = {0, 0, 0}, lo[3] = {0, 0, 0}; int mode = 0; };   // 0 linear 1 constant 2 curve\n' +
     '    struct Track { int kind = 0; bool relative = false; std::vector<Key> pos, euler, curve; std::vector<std::pair<float, std::string>> events; };\n' +
     '    // kind 0 move, 1 event keys, 2 director cuts (camera group names), 3 fade, 4 FOVAngle\n' +
     '    struct Group { std::string name; bool director = false; std::map<std::string, std::vector<std::string>> actors; std::vector<Track> tracks; };\n' +
     '    float length = 0.0f;\n' +
     '    std::vector<Group> groups;\n' +
     '};\n' +
     '// An InterpActor of the chapter moved by a matinee: its moving collision set and its pose (glTF metres).\n' +
     'struct CampaignMover {\n' +
     '    std::string name;\n' +
     '    core::Vec3 pivot{0, 0, 0}, offset{0, 0, 0}, lastOffset{0, 0, 0}, lo{0, 0, 0}, hi{0, 0, 0};\n' +
     '    std::vector<core::Vec3> tris;   // pivot-relative\n' +
     '    int pawnSet = -1;\n' +
     '};\n\n' +
     'struct CampaignFactory {\n'],
    ['    std::string humanChassis;',
     '    std::vector<CampaignMatinee> matinees;    // step 5: Matinee\n' +
     '    struct Interp { int matinee = -1, op = -1; std::string action; float rate = 1.0f, dir = 1.0f, pos = 0.0f; bool looping = false, rewindOnPlay = false, playing = false; };\n' +
     '    std::vector<Interp> interps;              // one per SeqAct_Interp op\n' +
     '    int cineOp = -1, cineViewOp = -1;         // the op driving the local camera / the CinematicView\n' +
     '    std::string cineCamGroup;\n' +
     '    std::string humanChassis;'],
]);
ed('src/game/World.h', [
    ['    void cpSyncPicks();\n',
     '    void cpSyncPicks();\n' +
     '    void cpCollectMovers();                       // WorldCampaignMatinee.cpp\n' +
     '    void cpSplitMovers(const render::MeshData& in, render::MeshData& out);\n' +
     '    void cpRegisterMovers();\n' +
     '    void cpLoadMatinees();\n' +
     '    bool cpInterpActivate(KismetVM& vm, int op, int input);\n' +
     '    void cpTickMatinees(float dt);\n' +
     '    void cpMatineeApply(CampaignState::Interp& in, float from, float to);\n' +
     '    void cpMatineeEnd(CampaignState::Interp& in);\n' +
     '    std::vector<CampaignMover> cpMovers_;         // the chapter\'s InterpActors (kept across launches: loaded with the map)\n'],
]);
ed('src/game/World.cpp', [
    ['        splitMovers(colMesh, pawnStatic, pawnMovers);\n        collision_.build(pawnStatic);\n',
     '        if (campaignMap_) { cpCollectMovers(); cpSplitMovers(colMesh, pawnStatic); }   // campaign: the chapter\'s InterpActors\n' +
     '        else splitMovers(colMesh, pawnStatic, pawnMovers);\n' +
     '        collision_.build(pawnStatic);\n' +
     '        if (campaignMap_) cpRegisterMovers();\n'],
]);
ed('src/render/Renderer.h', [
    ['    virtual void setActorHidden(const std::string& actor, bool hidden) { (void)actor; (void)hidden; }\n',
     '    virtual void setActorHidden(const std::string& actor, bool hidden) { (void)actor; (void)hidden; }\n' +
     '    // Gameplay-authoritative pose of an authored map actor moved by a campaign matinee (an InterpActor: world.glb actor name), UE\n' +
     '    // units / rotator degrees (pitch, yaw, roll), absolute. Actors never sent keep their authored pose. [Gameplay; Rendering implements]\n' +
     '    virtual void setMapActorTransform(const std::string& actor, const core::Vec3& posUE, const core::Vec3& rotUEdeg) { (void)actor; (void)posUE; (void)rotUEdeg; }\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadActors();\n', '        cpLoadActors();\n        cpLoadMatinees();\n'],
    ['    if (cpAiOpActivate(vm, op, input)) return true;',
     '    if (cpInterpActivate(vm, op, input)) return true;   // SeqAct_Interp: Matinee\n    if (cpAiOpActivate(vm, op, input)) return true;'],
    ['    if (c == "SeqAct_Interp" || c == "SeqAct_MoviePlayer" ||', '    if (c == "SeqAct_Interp" || c == "SeqAct_MoviePlayer" ||'],
    ['    if (!cp_.gameOver) cpTickFactories(dt);\n', '    if (!cp_.gameOver) cpTickFactories(dt);\n    cpTickMatinees(dt);\n'],
]);
console.log('ok');
