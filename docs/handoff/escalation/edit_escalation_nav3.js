// Escalation nav v3 support (AssetTools' typed links + merged versus cells): BotNav takes "walk" links (a plain move), marks long
// downward R_JUMP links (span > 6 m, landing below the foot) as leaps (waypoint action 4: a ballistic launch at the foot) and reads
// flags.versus_only; a director-spawned (horde) agent costs versus-only cells x10 so the horde keeps to the original NavigationCells.
// The versus bot_nav.json files carry none of these (no walk links, no versus_only, no long jump-downs): the versus nav is unchanged.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/BotNav.h', [
    ['    struct Link { int from, to; bool jump; bool robot, vehicle; core::Vec3 fromPos, toPos; bool doubleJump = false; };\n',
     '    // walk: an R_WALK reach spec (a plain move); leap: a long R_JUMP down (span > 6 m, landing below), launched ballistically at the foot\n' +
     '    struct Link { int from, to; bool jump; bool robot, vehicle; core::Vec3 fromPos, toPos; bool doubleJump = false; bool walk = false, leap = false; };\n'],
    ['        bool vehicle = false;\n        std::vector<Portal> portals;',
     '        bool vehicle = false;\n        bool versusOnly = false;   // Escalation nav: a versus cell merged where the original has no NavigationCell\n        std::vector<Portal> portals;'],
    ['    struct Agent { float radius = 1.75f; bool vehicle = false; ',
     '    struct Agent { float radius = 1.75f; bool vehicle = false; bool originalOnly = false;   // originalOnly: versus-only cells cost x10 (the horde)\n                   '],
]);
ed('src/game/BotNav.cpp', [
    ['        o.vehicle = c["flags"]["vehicle_capable"].asBool();\n',
     '        o.vehicle = c["flags"]["vehicle_capable"].asBool();\n        o.versusOnly = c["flags"]["versus_only"].asBool(false);\n'],
    ['        if (kind != "jump_up" && kind != "drop_down" && kind != "double_jump_up") continue;\n',
     '        if (kind != "jump_up" && kind != "drop_down" && kind != "double_jump_up" && kind != "walk") continue;\n'],
    ['        L.doubleJump = kind == "double_jump_up";\n',
     '        L.doubleJump = kind == "double_jump_up";\n        L.walk = kind == "walk";\n'],
    ['        L.fromPos = v3(l["from_gltf"]); L.toPos = v3(l["to_gltf"]);\n',
     '        L.fromPos = v3(l["from_gltf"]); L.toPos = v3(l["to_gltf"]);\n' +
     '        {   // a long jump down off a perch (AssetTools v3: an authored R_JUMP with a negative rise)\n' +
     '            const float dx = L.toPos.x - L.fromPos.x, dz = L.toPos.z - L.fromPos.z;\n' +
     '            L.leap = L.jump && !L.doubleJump && L.toPos.y < L.fromPos.y - 1.0f && dx * dx + dz * dz > 36.0f;\n' +
     '        }\n'],
    ['            out.push_back({l.toPos, l.doubleJump ? 3 : (l.jump ? 1 : 2), c1});\n',
     '            out.push_back({l.toPos, l.leap ? 4 : l.doubleJump ? 3 : l.jump ? 1 : l.walk ? 0 : 2, c1});\n'],
    ['            if (a.avoid && std::find(a.avoid->begin(), a.avoid->end(), p.to) != a.avoid->end()) tight *= 10.0f;\n',
     '            if (a.avoid && std::find(a.avoid->begin(), a.avoid->end(), p.to) != a.avoid->end()) tight *= 10.0f;\n' +
     '            if (a.originalOnly && cells_[(size_t)p.to].versusOnly) tight *= 10.0f;\n'],
    ['            if (a.cost && (size_t)l.to < a.cost->size()) ng += (ng - gsAt(c)) * a.costW * (*a.cost)[(size_t)l.to];\n',
     '            if (a.cost && (size_t)l.to < a.cost->size()) ng += (ng - gsAt(c)) * a.costW * (*a.cost)[(size_t)l.to];\n' +
     '            if (a.originalOnly && cells_[(size_t)l.to].versusOnly) ng += (ng - gsAt(c)) * 9.0f;\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['    BotNav::Agent ag; ag.radius = pc.cylinderRadius(Form::Robot); ag.vehicle = vehicle && !jetForm;\n    b.repathTimer -= dt;\n',
     '    BotNav::Agent ag; ag.radius = pc.cylinderRadius(Form::Robot); ag.vehicle = vehicle && !jetForm;\n' +
     '    ag.originalOnly = match_.players()[(size_t)b.player].directorSpawned;   // the horde keeps to the original NavigationCells\n' +
     '    b.repathTimer -= dt;\n'],
    ['            if (w.action == 1 || w.action == 3) {   // jump link: jump at its foot, keep pushing toward the top\n',
     '            if (w.action == 4) {   // a leap down (R_JUMP off a perch) [H: UE3 SuggestJumpVelocity]: at the foot, jump with the horizontal\n' +
     '                                    // speed that lands on the spec\'s end (x1.1 for the falling air-control drag), then steer to it\n' +
     '                const core::Vec3 foot = b.wp > 0 ? b.path[b.wp - 1].pos : pos;\n' +
     '                if (pc.onGround() && hdist(pos, foot) < 3.0f && dl > 1e-3f) {\n' +
     '                    const float vy = pc.robotParams().jumpSpeed(), g = core::config::kGravity, dy = w.pos.y - pos.y;\n' +
     '                    const float t = (vy + std::sqrt(std::max(0.0f, vy * vy - 2.0f * g * dy))) / g;\n' +
     '                    const float vh = t > 1e-3f ? dl / t * 1.1f : 0.0f;\n' +
     '                    pc.velocity().x = d.x / dl * vh; pc.velocity().z = d.z / dl * vh;\n' +
     '                    jump = true; ++b.jumps;\n' +
     '                }\n' +
     '                if (dl < 2.0f || (pc.onGround() && pos.y < w.pos.y + 2.0f && dl < 8.0f)) { ++b.wp; b.bestDist = 1e9f; }\n' +
     '            }\n' +
     '            if (w.action == 1 || w.action == 3) {   // jump link: jump at its foot, keep pushing toward the top\n'],
]);
console.log('ok');
