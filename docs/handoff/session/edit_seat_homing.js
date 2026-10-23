// The homing lock per seat (an online-local-only path the seat path missed): TnWeaponHoming's lock-on (candidate inside
// max(atan(minR / d), min(4 deg, atan(7 m / d))) of the aim, LockOnTime to lock, HoldLockOnTime to keep it) for every Remote seat and the
// online local seat, from the seat's eye toward its command's aim point, candidates by match player index; a seat's homing projectile
// takes its locked target. Online the local HUD lock mirrors the local seat's lock (the camera-ray lock is the offline path).
// Offline identical. Run from a tree root (integration dd28a723+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', [
    ['    void clearAllParticipants();\n',
     '    void clearAllParticipants();\n' +
     '    struct SeatLock { int candidate = -1, target = -1; float timer = 0.0f, hold = 0.0f; bool locked = false; };\n' +
     '    std::vector<SeatLock> seatLocks_;   // per seat (match player index): the homing lock\n' +
     '    void tickSeatLock(int p, const Character& pc, const core::Vec3& eye, const core::Vec3& fwd, float dt);\n'],
    ['    bool guidedMissileAlive() const { return missile_.alive; }\n',
     '    bool guidedMissileAlive() const { return missile_.alive; }\n' +
     '    int seatLockTarget(int p) const { return p >= 0 && (size_t)p < seatLocks_.size() && seatLocks_[(size_t)p].locked ? seatLocks_[(size_t)p].target : -1; }\n'],
]);
ed('src/game/World.cpp', [
    ['        Character& pc = *s.pc;\n        pc.setAimPitch(c.aimPitch);\n',
     '        Character& pc = *s.pc;\n        pc.setAimPitch(c.aimPitch);\n' +
     '        {   // the homing lock from the eye toward the command\'s aim point\n' +
     '            const Weapon* hw = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();\n' +
     '            const core::Vec3 eye = botEye(pc), at = seatAimPoint(pc, c, hw && hw->rangeM > 0.0f ? hw->rangeM : 300.0f);\n' +
     '            const core::Vec3 d = at - eye;\n' +
     '            tickSeatLock(p, pc, eye, core::length(d) > 1e-4f ? core::normalize(d) : core::forwardFromYawPitch(c.aimYaw, c.aimPitch), dt);\n' +
     '        }\n'],
    ['            spawnProjectile(origin, core::normalize(aimPt - origin) * w.projSpeed, w, p);\n',
     '            spawnProjectile(origin, core::normalize(aimPt - origin) * w.projSpeed, w, p);\n' +
     '            if (w.projHoming && seatLockTarget(p) >= 0) projectiles_.back().target = seatLockTarget(p);   // SetTarget(locked ? target : none)\n'],
    ['        spawnProjectile(muzzle, d * w.projSpeed, w, p);\n',
     '        spawnProjectile(muzzle, d * w.projSpeed, w, p);\n' +
     '        if (w.projHoming && seatLockTarget(p) >= 0) projectiles_.back().target = seatLockTarget(p);   // SetTarget(locked ? target : none)\n'],
    ['    if (!onlineSeats_) tickLocalMelee(dt);   // online: the seat ticks\n    tickHomingLock(dt);\n',
     '    if (!onlineSeats_) tickLocalMelee(dt);   // online: the seat ticks\n' +
     '    if (!onlineSeats_) tickHomingLock(dt);\n' +
     '    else {   // online: the HUD lock is the local seat\'s (applySeat ticks it on every peer)\n' +
     '        const SeatLock* sl = localPlayer_ >= 0 && (size_t)localPlayer_ < seatLocks_.size() ? &seatLocks_[(size_t)localPlayer_] : nullptr;\n' +
     '        locked_ = sl && sl->locked; lockTarget_ = sl ? sl->target : -1; lockCandidate_ = sl ? sl->candidate : -1; lockTimer_ = sl ? sl->timer : 0.0f;\n' +
     '    }\n'],
    ['    bots_.clear(); smart_.clear(); anySmart_ = false; remoteIdle_.clear();',
     '    bots_.clear(); smart_.clear(); anySmart_ = false; remoteIdle_.clear(); seatLocks_.clear();'],
    ['void World::clearAllParticipants() {\n',
     'void World::tickSeatLock(int p, const Character& pc, const core::Vec3& eye, const core::Vec3& fwd, float dt) {\n' +
     '    if (seatLocks_.size() <= (size_t)p) seatLocks_.resize((size_t)p + 1);\n' +
     '    SeatLock& L = seatLocks_[(size_t)p];\n' +
     '    const Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();\n' +
     '    if (!matchActive_ || pc.isTransforming() || !w || !w->projHoming || w->lockOnTime <= 0.0f) { L = SeatLock{}; return; }\n' +
     '    const float deg4 = 4.0f * 0.0174533f;\n' +
     '    int best = -1; float bestAng = 1e9f;\n' +
     '    for (size_t i = 0; i < match_.players().size(); ++i) {   // by match player index\n' +
     '        const int q = (int)i;\n' +
     '        if (q == p || match_.sameTeam(q, p)) continue;\n' +
     '        const Character* t = participantPawn(q);\n' +
     '        if (!t) continue;\n' +
     '        const bool robot = t->moveForm() == Form::Robot;\n' +
     '        if (robot && !w->lockRobots) continue;\n' +
     '        const core::Vec3 d = t->actorLocation() - eye;\n' +
     '        const float dist = core::length(d);\n' +
     '        if (dist < 1e-3f || dist > w->rangeM) continue;\n' +
     '        const float minR = robot ? 6.0f : 5.0f, maxR = 7.0f;\n' +
     '        float half = std::max(std::atan(minR / dist), std::min(deg4, std::atan(maxR / dist)));\n' +
     '        half = std::min(half, std::atan(maxR / dist));\n' +
     '        const float ang = std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f));\n' +
     '        if (ang <= half && ang < bestAng) { bestAng = ang; best = q; }\n' +
     '    }\n' +
     '    if (best >= 0) {\n' +
     '        L.hold = 0.0f;\n' +
     '        if (best != L.candidate) { L.candidate = best; L.timer = 0.0f; if (best != L.target) L.locked = false; }\n' +
     '        L.timer += dt;\n' +
     '        if (L.timer >= w->lockOnTime && (w->ammo > 0 || w->magSize <= 0)) { L.locked = true; L.target = best; }\n' +
     '    } else {\n' +
     '        L.candidate = -1; L.timer = 0.0f;\n' +
     '        if (L.locked) {\n' +
     '            L.hold += dt;\n' +
     '            if (L.hold >= w->holdLockOnTime || !participantPawn(L.target)) { L.locked = false; L.target = -1; L.hold = 0.0f; }\n' +
     '        }\n' +
     '    }\n' +
     '}\n\n' +
     'void World::clearAllParticipants() {\n'],
]);
ed('src/core/Application.cpp', [[
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n',
    '    partialCase("remote homing lock: the aimed enemy locked, the rocket takes it", game::Specialty::Soldier, {"HomingRocket", "AssaultRifle", "FlakGrenades"}, {"Hover", "Cloaking"},\n' +
    '        [&](int q, game::MatchOpponent* T, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            const game::Character* me = world_.participantPawn(q);\n' +
    '            if (!me || !me->weapon().projHoming) { LOG_INFO("SESSIONTEST homing: held weapon %s", me && me->weapon().def ? me->weapon().def->id : "-"); return false; }\n' +
    '            if (!me->weapon().lockRobots) {   // Thermo rockets lock onto vehicles only (authored): the target transforms first\n' +
    '                T->pawn().beginTransform();\n' +
    '                for (int i = 0; i < 150; ++i) { aimAt(); stepCmd(); }\n' +
    '            }\n' +
    '            int lockedAt = -1;\n' +
    '            for (int i = 0; i < 180 && lockedAt < 0; ++i) { aimAt(); stepCmd(); if (world_.seatLockTarget(q) == T->matchPlayer()) lockedAt = i; }\n' +
    '            LOG_INFO("SESSIONTEST homing: lock on p%d after %d steps (LockOnTime %.2f s)", T->matchPlayer(), lockedAt, me->weapon().lockOnTime);\n' +
    '            return lockedAt >= 0;\n' +
    '        });\n' +
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n'
]]);
console.log('ok');
