// The guided missile per seat (RE MP_GUIDED_MISSILE_RE.md): Remote seats and the online local seat, simulated on every peer in match
// player order (tickSeatMissiles, the step prefix after the seat commands). The offline local player keeps the singleton missile_.
// - Trigger (ability or the Omega Missile killstreak): Skill_GuidedMissile, Delay 1.0 s, then the spawn along the seat's control rotation
//   (Pitch clamped to [700, 16384] rotator units) at ReactionSocket_Chest + (200, 0, 0) rotated [chest ~ the eye height, PROV].
// - Guiding: the owner stands still (ClearInputs / StopMovement, no fire or abilities); the command's move axes carry the missile input
//   (moveRight = LeftRight, moveForward = UpDown, +-1); Acceleration = (LR x right + UD x up) x ControlStrength 2500 UU/s^2, speed held at
//   2000 UU/s; either ability button detonates.
// - Detonation: world / pawn (any but the owner's) hit, fuse 30 s, kill Z, the owner's death, or the button: HurtRadius 10000 / 4500 UU.
// - The cooldown waits for it (missileAlive_). The online local seat drives the missile camera + HUD from its seat missile.
// Offline identical (no seats). Run after edit_seat_iter.js (integration 0df2a0fa has the scripts before it).
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
    ['    GuidedMissile missile_;\n',
     '    GuidedMissile missile_;\n' +
     '    // Per-seat guided missiles (Remote seats + the online local seat), index = match player.\n' +
     '    struct SeatMissile { float delay = -1.0f; bool alive = false, detonate = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0};\n' +
     '                         float life = 0.0f, lr = 0.0f, ud = 0.0f, yaw = 0.0f, pitch = 0.0f; };\n' +
     '    std::vector<SeatMissile> seatMissiles_;\n' +
     '    SeatMissile* seatMissileMut(int p) { return p >= 0 && (size_t)p < seatMissiles_.size() ? &seatMissiles_[(size_t)p] : nullptr; }\n' +
     '    bool seatGuiding(int p) const { const SeatMissile* m = seatMissile(p); return m && (m->alive || m->delay >= 0.0f); }\n' +
     '    void startSeatMissile(int p, float yaw, float pitch);\n' +
     '    void tickSeatMissiles(float dt);\n' +
     '    void detonateSeatMissile(int p, const core::Vec3& at);\n'],
    ['    bool guidedMissileAlive() const { return missile_.alive; }\n',
     '    bool guidedMissileAlive() const { return missile_.alive; }\n' +
     '    const SeatMissile* seatMissile(int p) const { return p >= 0 && (size_t)p < seatMissiles_.size() ? &seatMissiles_[(size_t)p] : nullptr; }\n'],
]);
ed('src/game/World.cpp', [
    // The prefix: the seat missiles after the seat commands, by match player index.
    ['    tickSlotSwaps(dt);   // session slot handovers at their agreed tick (main thread, before any bot / pawn work)\n',
     '    tickSlotSwaps(dt);   // session slot handovers at their agreed tick (main thread, before any bot / pawn work)\n' +
     '    tickSeatMissiles(dt);   // the seats\' guided missiles (every peer, by match player index)\n'],
    ['    tickGuidedMissile(dt);   // the local player\'s missile (its controls are the local input)\n',
     '    if (!onlineSeats_) tickGuidedMissile(dt);   // the local player\'s missile (offline; online the local seat\'s is a seat missile)\n'],
    ['    missile_ = GuidedMissile{}; missileDelay_ = -1.0f;\n',
     '    missile_ = GuidedMissile{}; missileDelay_ = -1.0f; seatMissiles_.clear();\n'],
    // The seat path: guiding freezes the owner and reads the missile input; the launch takes the latest control rotation.
    ['        if (pc.inputLocked()) { s.intent = MoveIntent{}; return; }   // TnBuffDecoyTrap: no movement / buttons\n',
     '        if (pc.inputLocked()) { s.intent = MoveIntent{}; return; }   // TnBuffDecoyTrap: no movement / buttons\n' +
     '        if (SeatMissile* gm = seatMissileMut(p); gm && gm->alive) {   // GuidingMissile: ClearInputs / StopMovement; the axes steer\n' +
     '            gm->lr = core::clampf(c.moveRight / 127.0f, -1.0f, 1.0f); gm->ud = core::clampf(c.moveForward / 127.0f, -1.0f, 1.0f);\n' +
     '            if (down(platform::Button::Dash) || down(platform::Button::Ability1)) gm->detonate = true;   // the ability button again\n' +
     '            MoveIntent z; z.faceYaw = pc.yaw(); z.viewPitch = c.aimPitch; s.intent = z; s.pressesPickup = false;\n' +
     '            return;\n' +
     '        }\n'],
    ['        if (robot && down(platform::Button::Ability1)) remoteTriggerAbility(s, 1, c.aimYaw, aimPt, in);\n',
     '        if (robot && down(platform::Button::Ability1)) remoteTriggerAbility(s, 1, c.aimYaw, aimPt, in);\n' +
     '        if (SeatMissile* gm = seatMissileMut(p); gm && gm->delay >= 0.0f) { gm->yaw = c.aimYaw; gm->pitch = c.aimPitch; }   // spawn rotation\n'],
    ['    else return false;   // [PARTIAL] the aimed abilities (HardLock / jammer / disruptor / guided missile) for Remote seats come later\n',
     '    else if (a.id == "GuidedMissile") { if (seatGuiding(p)) return false; startSeatMissile(p, aimYaw, 0.0f); }\n' +
     '    else return false;\n'],
    // The Omega Missile killstreak for seats (bots never took this path; they still do not).
    ['    if (player != localPlayer_ && (id == "GuidedMissileStreak" || id == "MinePooperStreak")) return "";\n',
     '    if (id == "GuidedMissileStreak" && seatHuman(player) && (player != localPlayer_ || onlineSeats_)) {   // the seat\'s guided missile\n' +
     '        if (seatGuiding(player)) return "";\n' +
     '        mp.acquiredKillstreaks.pop_back();\n' +
     '        startSeatMissile(player, pc.yaw(), 0.0f);\n' +
     '        LOG_INFO("killstreak %s triggered (p%d)", id.c_str(), player);\n' +
     '        return id;\n' +
     '    }\n' +
     '    if (player != localPlayer_ && (id == "GuidedMissileStreak" || id == "MinePooperStreak")) return "";\n'],
    // HUD and audio.
    ['    h.guidedMissile = missile_.alive; h.guidedMissilePos = missile_.pos; h.guidedMissileFuse = missile_.life;\n',
     '    h.guidedMissile = missile_.alive; h.guidedMissilePos = missile_.pos; h.guidedMissileFuse = missile_.life;\n' +
     '    if (const SeatMissile* sm = seatMissile(localPlayer_); sm && sm->alive) { h.guidedMissile = true; h.guidedMissilePos = sm->pos; h.guidedMissileFuse = sm->life; }\n'],
    ['        { AbilityActorState st; st.alive = missile_.alive; st.pos = missile_.pos;\n' +
     '          setAbilityActorAudio(AbilityActor::GuidedMissile, localPlayer_, true, st); }\n',
     '        if (!onlineSeats_) { AbilityActorState st; st.alive = missile_.alive; st.pos = missile_.pos;\n' +
     '          setAbilityActorAudio(AbilityActor::GuidedMissile, localPlayer_, true, st); }\n' +
     '        for (size_t q = 0; q < seatMissiles_.size(); ++q)   // the seats\' missiles (the flight loop per owner)\n' +
     '            if (seatMissiles_[q].alive) { AbilityActorState st; st.alive = true; st.pos = seatMissiles_[q].pos;\n' +
     '                                          setAbilityActorAudio(AbilityActor::GuidedMissile, (int)q, (int)q == localPlayer_, st); }\n'],
    // The local seat's command: while its missile flies, the axes carry the controller's missile input.
    ['    c.moveForward = q8(mi.moveForward); c.moveRight = q8(mi.moveRight);\n',
     '    c.moveForward = q8(mi.moveForward); c.moveRight = q8(mi.moveRight);\n' +
     '    if (const SeatMissile* gm = seatMissile(localPlayer_); gm && gm->alive) { c.moveForward = q8(ctl.guideUD()); c.moveRight = q8(ctl.guideLR()); }\n'],
    // The seat missile functions.
    ['void World::clearAllParticipants() {\n',
     'void World::startSeatMissile(int p, float yaw, float pitch) {\n' +
     '    Character* pc = participantPawnMutable(p);\n' +
     '    if (!pc) return;\n' +
     '    if (seatMissiles_.size() <= (size_t)p) seatMissiles_.resize((size_t)p + 1);\n' +
     '    SeatMissile& m = seatMissiles_[(size_t)p];\n' +
     '    m = SeatMissile{}; m.delay = 1.0f; m.yaw = yaw; m.pitch = pitch;   // Delay 1.0 s -> SpawnGuidedMissile\n' +
     '    pc->playAction("Skill_GuidedMissile", false);\n' +
     '    pc->missileAlive_ = true;\n' +
     '}\n\n' +
     'void World::detonateSeatMissile(int p, const core::Vec3& at) {\n' +
     '    SeatMissile* m = seatMissileMut(p);\n' +
     '    if (!m || !m->alive) return;\n' +
     '    m->alive = false; m->detonate = false;\n' +
     '    onAbilityActorExploded(AbilityActor::GuidedMissile, p, p == localPlayer_, at);   // [Systems] the explosion sound\n' +
     '    radiusDamage(at, 10000.0f, 45.0f, p, "TransGame.TnDamageTypeGuidedMissile");   // MP 10000 / 4500 UU\n' +
     '    LOG_INFO("guided missile (p%d) detonated at (%.1f %.1f %.1f)", p, at.x, at.y, at.z);\n' +
     '}\n\n' +
     'void World::tickSeatMissiles(float dt) {\n' +
     '    for (size_t i = 0; i < seatMissiles_.size(); ++i) {\n' +
     '        const int p = (int)i;\n' +
     '        SeatMissile& m = seatMissiles_[i];\n' +
     '        if (m.delay < 0.0f && !m.alive) continue;\n' +
     '        Character* pc = participantPawnMutable(p);\n' +
     '        const bool local = p == localPlayer_;\n' +
     '        PlayerController& ctl = player_.controller();\n' +
     '        if (m.delay >= 0.0f) {\n' +
     '            if (!pc) { m = SeatMissile{}; continue; }   // the owner died before the launch\n' +
     '            m.delay -= dt;\n' +
     '            if (m.delay < 0.0f) {\n' +
     '                const float pitch = core::clampf(m.pitch, 700.0f * 6.2831853f / 65536.0f, 1.5707963f);\n' +
     '                const core::Vec3 dir = core::forwardFromYawPitch(m.yaw, pitch);\n' +
     '                m.alive = true; m.life = 30.0f; m.vel = dir * 20.0f;   // FuseTime 30 s (MP); InitialSpeed 2000\n' +
     '                m.pos = pc->actorLocation() + core::Vec3{0, pc->robotParams().eyeHeight, 0} + dir * 2.0f;\n' +
     '                if (local) { ctl.setGuiding(true); if (renderer_) renderer_->notifyCameraCut("missile cam"); }\n' +
     '                LOG_INFO("guided missile (p%d) launched (pitch %.1f deg)", p, pitch * 57.2958f);\n' +
     '            }\n' +
     '        }\n' +
     '        if (m.alive && (!pc || m.detonate)) detonateSeatMissile(p, m.pos);   // the owner died / the button\n' +
     '        else if (m.alive) {\n' +
     '            const core::Vec3 f = core::normalize(m.vel);\n' +
     '            core::Vec3 r = core::cross(f, core::Vec3{0, 1, 0});\n' +
     '            if (core::length(r) < 1e-4f) r = core::cross(core::forwardFromYawPitch(m.yaw, 0.0f), core::Vec3{0, 1, 0});   // straight up\n' +
     '            r = core::normalize(r);\n' +
     '            const core::Vec3 u = core::cross(r, f);\n' +
     '            m.vel = m.vel + (r * m.lr + u * m.ud) * (25.0f * dt);   // ControlStrength 2500\n' +
     '            m.vel = core::normalize(m.vel) * 20.0f;                  // MaxSpeed 2000\n' +
     '            const core::Vec3 next = m.pos + m.vel * dt;\n' +
     '            float t; core::Vec3 n;\n' +
     '            const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);\n' +
     '            bool hit = line && line->segmentHit(m.pos, next, t, n);\n' +
     '            core::Vec3 at = hit ? m.pos + (next - m.pos) * t : next;\n' +
     '            const core::Vec3 d = next - m.pos; const float len = core::length(d);\n' +
     '            float best = hit ? t * len : len;\n' +
     '            if (len > 1e-5f)\n' +
     '                for (size_t q = 0; q < match_.players().size(); ++q) {   // any pawn but the owner\'s, by match player index\n' +
     '                    const Character* c = (int)q == p ? nullptr : participantPawn((int)q);\n' +
     '                    float th;\n' +
     '                    if (c && MatchOpponent::pawnRayHit(*c, m.pos, d * (1.0f / len), len, th) && th < best) { best = th; hit = true; at = m.pos + d * (th / len); }\n' +
     '                }\n' +
     '            m.life -= dt;\n' +
     '            if (hit || m.life <= 0.0f || m.pos.y < killZ_) detonateSeatMissile(p, at);\n' +
     '            else m.pos = next;\n' +
     '        }\n' +
     '        if (local) {   // the local seat\'s missile camera (presentation)\n' +
     '            if (m.alive) {\n' +
     '                const core::Vec3 f = core::normalize(m.vel);\n' +
     '                const float yaw = std::atan2(-f.x, -f.z), pitch = std::asin(core::clampf(f.y, -1.0f, 1.0f));\n' +
     '                core::Vec3 r = core::cross(f, core::Vec3{0, 1, 0});\n' +
     '                r = core::length(r) < 1e-4f ? core::Vec3{1, 0, 0} : core::normalize(r);\n' +
     '                const core::Vec3 u = core::cross(r, f);\n' +
     '                ctl.setSpectatorView(m.pos + f * 1.35f + u * 1.25f, yaw, pitch, 120.0f);   // LocalOffset (135, 0, 125), FOV 120\n' +
     '            } else if (ctl.guiding() && m.delay < 0.0f) {\n' +
     '                ctl.setGuiding(false);\n' +
     '                ctl.clearSpectatorView();\n' +
     '                if (renderer_) renderer_->notifyCameraCut("missile cam end");\n' +
     '            }\n' +
     '        }\n' +
     '        if (pc) pc->missileAlive_ = m.alive || m.delay >= 0.0f;   // TnAbilityCooldownConditionGuidedMissile\n' +
     '    }\n' +
     '}\n\n' +
     'void World::clearAllParticipants() {\n'],
]);
ed('src/core/Application.cpp', [[
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n',
    '    partialCase("remote guided missile: launch, owner frozen, steer, detonate", game::Specialty::Leader, {"AssaultRifle", "HomingRocket", "FlakGrenades"}, {"Hover", "GuidedMissile"},\n' +
    '        [&](int q, game::MatchOpponent* T, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            const game::Character* me = world_.participantPawn(q);\n' +
    '            if (!me || me->abilities_[1].id != "GuidedMissile") { LOG_INFO("SESSIONTEST guided missile: slot 1 is %s", me ? me->abilities_[1].id.c_str() : "-"); return false; }\n' +
    '            aimAt(); cmd.pressed = 1u << (int)platform::Button::Ability1; stepCmd();\n' +
    '            for (int i = 0; i < 70; ++i) { aimAt(); stepCmd(); }   // Delay 1.0 s\n' +
    '            const auto* gm = world_.seatMissile(q);\n' +
    '            const bool launched = gm && gm->alive;\n' +
    '            const core::Vec3 p0 = world_.participantPawn(q) ? world_.participantPawn(q)->position() : core::Vec3{0, 0, 0};\n' +
    '            const core::Vec3 m0 = launched ? gm->pos : core::Vec3{0, 0, 0}, v0 = launched ? gm->vel : core::Vec3{0, 0, 0};\n' +
    '            cmd.moveRight = 127; cmd.moveForward = 0;   // guiding: the axes steer (full right)\n' +
    '            for (int i = 0; i < 20; ++i) stepCmd();\n' +
    '            gm = world_.seatMissile(q);\n' +
    '            const game::Character* me2 = world_.participantPawn(q);\n' +
    '            const bool frozen = me2 && core::length(me2->position() - p0) < 0.05f;\n' +
    '            const bool steered = gm && gm->alive && core::dot(core::normalize(gm->vel), core::normalize(v0)) < 0.99f && std::fabs(core::length(gm->vel) - 20.0f) < 0.01f;\n' +
    '            const bool cooling = me2 && me2->abilities_[1].cooldown <= 0.0f && me2->missileAlive_;   // the cooldown waits for the missile\n' +
    '            const game::Character* tp = world_.participantPawn(T->matchPlayer());\n' +
    '            const float h0 = tp ? tp->health().current : 0.0f;\n' +
    '            cmd.moveRight = 0; cmd.pressed = 1u << (int)platform::Button::Ability1; stepCmd();   // the ability button again: detonate\n' +
    '            for (int i = 0; i < 3; ++i) stepCmd();\n' +
    '            gm = world_.seatMissile(q);\n' +
    '            tp = world_.participantPawn(T->matchPlayer());\n' +
    '            const bool boom = gm && !gm->alive && (!tp || tp->health().current < h0);\n' +
    '            LOG_INFO("SESSIONTEST guided missile: launched %d (at %.1f %.1f %.1f) frozen %d steered %d cooldown held %d detonated + target hit %d",\n' +
    '                     (int)launched, m0.x, m0.y, m0.z, (int)frozen, (int)steered, (int)cooling, (int)boom);\n' +
    '            return launched && frozen && steered && cooling && boom;\n' +
    '        });\n' +
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n'
]]);
console.log('ok');
