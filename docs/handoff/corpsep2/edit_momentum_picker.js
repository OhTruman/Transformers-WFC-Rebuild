// Death-body patch 2, part 4: the full momentum picker. TnRobotDeathPicker's momentum types read TearOffMomentum, the momentum of the
// killing TakeDamage call; its direction is the shot (instant hit), victim - origin (HurtRadius / splash / shockwave) or victim -
// attacker (melee). Each real hit site notes its direction just before applyMatchDamage; applyMatchDamage stores it for the victim on
// every applied call (a call without a direction clears it, as a momentum-less TakeDamage would), and requestCorpse uses it, else the
// interim (away from the killer's position). Presentation only: never read by the sim, not part of GameplayEvent.
// Run from a tree root (af4d0731 or later), after edit_scorch.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}

ed('src/game/World.h', [[
    '        core::Vec3 pos{}, vel{}, fwd{1, 0, 0}, killerPos{}; bool haveKiller = false;',
    '        core::Vec3 pos{}, vel{}, fwd{1, 0, 0}, killerPos{}; bool haveKiller = false;\n' +
    '        core::Vec3 hitDir{}; bool haveHitDir = false;   // TearOffMomentum direction of the killing hit (lastHitDir_)'
], [
    '    int corpsesMade_ = 0;                                  // diagnostics (BOTTEST / WFC_CORPSETEST)\n',
    '    int corpsesMade_ = 0;                                  // diagnostics (BOTTEST / WFC_CORPSETEST)\n' +
    '    // TearOffMomentum direction of each player\'s last applied damage (presentation only: the death picker\'s momentum types).\n' +
    '    // A hit site calls noteHitDir just before applyMatchDamage; the call consumes it (no note = no direction).\n' +
    '    core::Vec3 pendingHitDir_{0, 0, 0}; bool pendingHitDirOk_ = false;\n' +
    '    std::unordered_map<int, core::Vec3> lastHitDir_;\n' +
    '    void noteHitDir(const core::Vec3& d) { const float l = core::length(d); pendingHitDirOk_ = l > 1e-4f; if (pendingHitDirOk_) pendingHitDir_ = d * (1.0f / l); }\n'
]]);

const W = 'src/game/World.cpp';
ed(W, [
    // applyMatchDamage: consume the pending direction on entry; store it once the damage is known to apply.
    ['bool World::applyMatchDamage(int victim, int instigator, float amount, bool aoe, const std::string& damageType) {\n',
     'bool World::applyMatchDamage(int victim, int instigator, float amount, bool aoe, const std::string& damageType) {\n' +
     '    const bool hitDirOk = pendingHitDirOk_; const core::Vec3 hitDir = pendingHitDir_; pendingHitDirOk_ = false;\n'],
    ['    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;\n    Health* h = nullptr;\n',
     '    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;\n' +
     '    if (hitDirOk) lastHitDir_[victim] = hitDir; else lastHitDir_.erase(victim);\n' +
     '    Health* h = nullptr;\n'],
    // Local instant hit: the shot direction.
    ['    if (hitOpp) applyMatchDamage(hitOpp->matchPlayer(), localPlayer_, w.damageAt(dist), false, w.damageType);',
     '    if (hitOpp) { noteHitDir(dir); applyMatchDamage(hitOpp->matchPlayer(), localPlayer_, w.damageAt(dist), false, w.damageType); }'],
    // HurtRadius (bomb / AOE): victim - origin.
    ['                applyMatchDamage(localPlayer_, rd.instigator, rd.damage, true, rd.damageType);\n',
     '                { noteHitDir(pc.actorLocation() - rd.pos); applyMatchDamage(localPlayer_, rd.instigator, rd.damage, true, rd.damageType); }\n'],
    ['                    applyMatchDamage(o->matchPlayer(), rd.instigator, rd.damage, true, rd.damageType);\n',
     '                    { noteHitDir(o->pawn().actorLocation() - rd.pos); applyMatchDamage(o->matchPlayer(), rd.instigator, rd.damage, true, rd.damageType); }\n'],
    // Explosions (projectile splash): victim - the blast point.
    ['        if (k > 0.0f && radiusVisible(at, player_.pawn().actorLocation())) applyMatchDamage(localPlayer_, instigator, damage * k, false, type);',
     '        if (k > 0.0f && radiusVisible(at, player_.pawn().actorLocation())) { noteHitDir(player_.pawn().actorLocation() - at); applyMatchDamage(localPlayer_, instigator, damage * k, false, type); }'],
    ['        if (k > 0.0f && radiusVisible(at, o->pawn().actorLocation())) applyMatchDamage(o->matchPlayer(), instigator, damage * k, false, type);',
     '        if (k > 0.0f && radiusVisible(at, o->pawn().actorLocation())) { noteHitDir(o->pawn().actorLocation() - at); applyMatchDamage(o->matchPlayer(), instigator, damage * k, false, type); }'],
    // Shockwave: victim - self.
    ['        applyMatchDamage(victim, self, 65.0f, false, "TransGame.TnDamageTypeShockwave");\n',
     '        noteHitDir(dir);\n        applyMatchDamage(victim, self, 65.0f, false, "TransGame.TnDamageTypeShockwave");\n'],
    // Melee: victim - attacker (the knockback direction).
    ['            applyMatchDamage(vp, self, damage, false, type);\n            onPawnHitEffect(',
     '            noteHitDir(c - pc.actorLocation());\n            applyMatchDamage(vp, self, damage, false, type);\n            onPawnHitEffect('],
    // The picker: TearOffMomentum when known, else the interim.
    ['        core::Vec3 m = q.haveKiller ? q.pos - q.killerPos : core::Vec3{0, 0, 0}; m.y = 0.0f;\n',
     '        core::Vec3 m = q.haveHitDir ? q.hitDir : q.haveKiller ? q.pos - q.killerPos : core::Vec3{0, 0, 0}; m.y = 0.0f;\n'],
    ['    q.pos = pawn.actorLocation(); q.vel = pawn.velocity(); q.fwd = core::forwardFromYawPitch(pawn.yaw(), 0.0f);\n',
     '    q.pos = pawn.actorLocation(); q.vel = pawn.velocity(); q.fwd = core::forwardFromYawPitch(pawn.yaw(), 0.0f);\n' +
     '    { auto hd = lastHitDir_.find(player); if (hd != lastHitDir_.end()) { q.hitDir = hd->second; q.haveHitDir = true; lastHitDir_.erase(hd); } }\n'],
]);

// Bot instant hit: the shot direction.
ed('src/game/WorldBots.cpp', [[
    '        applyMatchDamage(hitPlayer, instigator, w.damageAt(dist), false, w.damageType ? w.damageType : "");\n',
    '        noteHitDir(dir);\n        applyMatchDamage(hitPlayer, instigator, w.damageAt(dist), false, w.damageType ? w.damageType : "");\n'
]]);
console.log('ok');
