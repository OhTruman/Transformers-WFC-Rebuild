// Diagnostics for Systems' FX-count row (WFC_CORPSETEST or WFC_DEATHFXLOG): one DEATHFX line per gib and per PlayEffect, the table
// decision and what actually spawned (actual / planned: a null-render run spawns no effects, chunks or ragdolls, so the planned
// values are the check there):
//   DEATHFX p<player> <chassis> entry <entry> gib: fx [<tmpl>@<PositionSocket|pos>, ...] chunks <actual>/<planned> skel <actual>/<planned> decal <planned>
//   DEATHFX p<player> entry <entry> play: [<tmpl>@<socket|fallback>, ...]
// Logging only. Run from a tree root after edit_death_sockets_shake.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
const W = 'src/game/World.cpp';
ed(W, [
    ['bool World::Corpse::deathSocketWorld(const char* socket, core::Vec3& out) const {\n',
     'static bool deathFxLogOn() { static const bool on = std::getenv("WFC_CORPSETEST") || std::getenv("WFC_DEATHFXLOG"); return on; }\n\n' +
     'bool World::Corpse::deathSocketWorld(const char* socket, core::Vec3& out) const {\n'],
    ['    if (c.noGibs) return;   // Melee: the Defrag dissolve alone\n',
     '    if (c.noGibs) {   // Melee: the Defrag dissolve alone\n' +
     '        if (deathFxLogOn()) LOG_INFO("DEATHFX p%d %s entry %s gib: fx [] chunks 0/0 skel 0/0 decal 0", c.player, c.chassis.c_str(), c.entry);\n' +
     '        return;\n' +
     '    }\n' +
     '    std::string fxList;   // DEATHFX: the gibs blueprint\'s effects as spawned (template @ PositionSocket / pos)\n'],
    ['        core::Vec3 ps = c.pos; c.deathSocketWorld("PositionSocket", ps);\n' +
     '        for (int i = 0; i < fxr->nGibFx && renderer_; ++i)\n',
     '        core::Vec3 ps = c.pos; const bool psOk = c.deathSocketWorld("PositionSocket", ps);\n' +
     '        for (int i = 0; i < fxr->nGibFx; ++i) fxList += std::string(fxList.empty() ? "" : ", ") + fxr->gibFx[i].tmpl + (psOk ? "@PositionSocket" : "@pos");\n' +
     '        for (int i = 0; i < fxr->nGibFx && renderer_; ++i)\n'],
    ['        if (renderer_) fxStepSpawn(fx, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n    }\n    if (!gibMeshesTried_ && renderer_) {\n',
     '        fxList = std::string(fx) + "@pos";\n' +
     '        if (renderer_) fxStepSpawn(fx, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n    }\n    if (!gibMeshesTried_ && renderer_) {\n'],
    ['                c.hullDefragDelay = c.jet ? 0.0f : 0.5f + 0.5f * c.frand();\n                c.hullDefragTime = c.jet ? 6.0f : 4.5f;\n            }\n        }\n    }\n}\n',
     '                c.hullDefragDelay = c.jet ? 0.0f : 0.5f + 0.5f * c.frand();\n                c.hullDefragTime = c.jet ? 6.0f : 4.5f;\n            }\n        }\n    }\n' +
     '    if (deathFxLogOn())\n' +
     '        LOG_INFO("DEATHFX p%d %s entry %s gib: fx [%s] chunks %d/%d skel %d/%d decal %d", c.player, c.chassis.c_str(), c.entry, fxList.c_str(),\n' +
     '                 (int)c.chunks.size(), (!fxr || fxr->chunks) ? 4 : 0, c.skelGib ? 1 : 0, (!c.vehicle && !c.noSkelGib) ? 1 : 0, (!c.vehicle && !c.noDecal) ? 1 : 0);\n' +
     '}\n'],
    // PlayEffect: each effect with its socket or the fallback placement.
    ['                if (renderer_ && c.fxRow >= 0) {\n                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n',
     '                if (deathFxLogOn() && c.fxRow >= 0) {\n' +
     '                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n' +
     '                    std::string pl;\n' +
     '                    for (int i = 0; i < R.nPlay; ++i) { core::Vec3 tmp; pl += std::string(pl.empty() ? "" : ", ") + R.play[i].tmpl + "@" + (c.deathSocketWorld(R.play[i].socket, tmp) ? R.play[i].socket : "fallback"); }\n' +
     '                    LOG_INFO("DEATHFX p%d entry %s play: [%s]", c.player, c.entry, pl.c_str());\n' +
     '                }\n' +
     '                if (renderer_ && c.fxRow >= 0) {\n                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n'],
]);
console.log('ok');
