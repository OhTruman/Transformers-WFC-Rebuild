// SCRATCH diagnostic (playtest #18 bots floating): WFC_FLOATLOG=1. Per spawned opponent pawn per step:
// (A) robot form, not on ground, not hovering / transforming, |vel.y| < 0.2 m/s for 30 steps -> "FLOAT stall";
// (B) on ground, but the render geometry (collision.glb) under the feet is > 0.6 m lower for 60 steps -> "FLOAT above-visual".
const fs = require('fs');
function ed(f, a, b) { let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n'); if (s.split(a).length !== 2) throw new Error(f + ' anchor ' + a.slice(0, 60)); s = s.replace(a, b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s); }
ed('src/game/WorldBots.cpp', 'void World::tickBots(float dt) {\n',
   'void World::tickBots(float dt) {\n' +
   '    static const bool floatLog = std::getenv("WFC_FLOATLOG") != nullptr;\n' +
   '    if (floatLog && matchActive_) {\n' +
   '        static CollisionWorld visual; static std::string visualMap; static std::map<int, std::pair<int, int>> run;\n' +
   '        if (visualMap != mapName_) { render::MeshData m; visual = CollisionWorld{}; if (assets::loadGlb(mapDir() + "collision.glb", m)) visual.build(m); visualMap = mapName_; run.clear(); }\n' +
   '        for (MatchOpponent* o : opponents_) {\n' +
   '            if (!o->spawned()) { run.erase(o->matchPlayer()); continue; }\n' +
   '            const Character& c = o->pawn(); auto& r = run[o->matchPlayer()];\n' +
   '            const core::Vec3 p = c.position(), v = c.velocity();\n' +
   '            const bool stall = c.moveForm() == Form::Robot && !c.onGround() && c.hoverState_ == 0 && !c.isTransforming() && std::fabs(v.y) < 0.2f;\n' +
   '            r.first = stall ? r.first + 1 : 0;\n' +
   '            if (r.first == 30) LOG_INFO("FLOAT stall p%d %s t %.2f at (%.2f %.2f %.2f) vel (%.2f %.2f %.2f) groundY %.2f", o->matchPlayer(), c.chassis().id.c_str(), match_.matchTime(), p.x, p.y, p.z, v.x, v.y, v.z, c.groundY);\n' +
   '            float gy = 0.0f; core::Vec3 gn; bool above = false;\n' +
   '            if (c.onGround() && visual.valid() && visual.groundHeight(p.x, p.z, p.y + 0.3f, 0.3f, gy, gn)) above = p.y - gy > 0.6f;\n' +
   '            r.second = above ? r.second + 1 : 0;\n' +
   '            if (r.second == 60) LOG_INFO("FLOAT above-visual p%d %s %s t %.2f at (%.2f %.2f %.2f) visual ground %.2f (%.2f m below)", o->matchPlayer(), c.chassis().id.c_str(), c.moveForm() == Form::Robot ? "robot" : "vehicle", match_.matchTime(), p.x, p.y, p.z, gy, p.y - gy);\n' +
   '        }\n' +
   '    }\n');
let s = fs.readFileSync('src/game/WorldBots.cpp', 'utf8');
if (!s.includes('#include "assets/GltfLoader.h"') && !s.includes('assets::loadGlb')) console.log('note: check loadGlb include');
console.log('ok');
