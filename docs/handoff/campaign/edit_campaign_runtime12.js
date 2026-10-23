// Campaign runtime step 12 (anchor edits; on RUNTIME11): destructible collision per state (a static-mesh state of a blocking actor is a
// moving-collision set, enabled while current) and the full UE placement (ueActorMatrix: location + pitch / yaw / roll) for the
// destructibles' bounds, collision and draw. WorldCampaignDestructibles.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignDestructibles.cpp'), 'src/game/WorldCampaignDestructibles.cpp');
ed('src/game/Campaign.h', [
    ['    struct State { int handle = 0; std::string name, mesh; float health = 0.0f, autoTime = 0.0f; std::vector<std::pair<int, std::string>> links; };',
     '    struct State { int handle = 0; std::string name, mesh; float health = 0.0f, autoTime = 0.0f; std::vector<std::pair<int, std::string>> links;\n' +
     '                   bool blocks = false; int colSet = -1; };   // a static-mesh state blocks; its collision set (built once)'],
    ['    float yawDeg = 0.0f;\n    std::vector<Piece> pieces;',
     '    float yawDeg = 0.0f;\n    core::Vec3 posUE{0, 0, 0}, rotDeg{0, 0, 0};   // the UE placement (location UU, pitch / yaw / roll degrees)\n    bool block = true;                            // the actor blocks (collision_flags.bBlockActors)\n    std::vector<Piece> pieces;'],
]);
ed('src/game/World.h', [
    ['    void cpDesBounds(int i);\n', '    void cpDesBounds(int i);\n    void cpDesCollide(int i);\n'],
]);
console.log('ok');
