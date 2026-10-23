// Campaign runtime step 11 (anchor edits; on RUNTIME10): matinee AnimControl on skeletal actors - InterpTrackAnimControl keys, the
// actors from AssetTools <chapter>/matinee_skeletal.json (mesh + anim files), the clip sampled at the matinee time, CPU-skinned and drawn.
// WorldCampaignMatinee.cpp copied from argv[2].
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
    ['#include "render/Mesh.h"\n', '#include "render/Mesh.h"\n#include "assets/SkinnedModel.h"\n'],
    ['    struct Track { int kind = 0; bool relative = false; std::vector<Key> pos, euler, curve; std::vector<std::pair<float, std::string>> events; };',
     '    struct Track { int kind = 0; bool relative = false; std::vector<Key> pos, euler, curve; std::vector<std::pair<float, std::string>> events;\n' +
     '                   std::vector<float> animOffset, animRate; std::vector<uint8_t> animLoop; };   // kind 5 AnimControl: events = (t, clip)'],
    ['struct CampaignFactory {\n',
     '// A matinee-animated skeletal actor (AnimControl): its mesh / anim files, placement, the clip and time the matinee sets.\n' +
     'struct CampaignSkel {\n' +
     '    std::string mesh, clip;\n' +
     '    std::vector<std::string> animFiles;\n' +
     '    core::Vec3 pos{0, 0, 0};\n' +
     '    float yawDeg = 0.0f, time = 0.0f;\n' +
     '    bool loop = false, active = false, tried = false, ok = false;\n' +
     '    assets::SkinnedModel model;\n' +
     '    render::MeshData skinned;\n' +
     '    std::vector<core::Mat4> scratch;\n' +
     '};\n\n' +
     'struct CampaignFactory {\n'],
]);
ed('src/game/World.h', [
    ['    void cpFlushCinematicView();\n',
     '    void cpFlushCinematicView();\n' +
     '    void cpLoadSkeletal();\n' +
     '    void cpDrawSkeletal(render::IRenderer& r) const;\n' +
     '    mutable std::map<std::string, CampaignSkel> cpSkel_;   // matinee skeletal actors (lazily loaded at their first draw)\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadMatinees();\n', '        cpLoadMatinees();\n        cpLoadSkeletal();\n'],
]);
ed('src/game/World.cpp', [
    ['    if (cp_.active) cpDrawDestructibles(r);   // campaign destructibles\n',
     '    if (cp_.active) cpDrawDestructibles(r);   // campaign destructibles\n    if (cp_.active) cpDrawSkeletal(r);        // campaign matinee skeletal actors\n'],
]);
console.log('ok');
