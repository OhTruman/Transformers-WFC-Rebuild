// Remote seats, part 3 (Integration 2026-10-17): the PARTIAL actions + the agreed absolute aim point (REMOTE_COMMAND_CONTRACT.md AMENDED).
// - RemoteCommand.aimPoint (world, decoded from 3 x int24 at 1 cm), present with kRemoteBitAimPoint (bit 31): shots, beams, grenades and
//   aimed abilities go from the pawn's eye toward it; without it eye + dir(aimYaw, aimPitch) x range. Bit 31 is masked out of the idle test.
// - Killstreak press -> triggerKillstreakFor (robot form).
// - Plasma Cannon (TnChargeWeapon): the local charge state machine (hold -> charge states, drain at full charge, release fires the level's
//   projectile / fizzles at level 1, reload on empty).
// - Beam weapons (TnWeaponRepair): a participant beam (any shooter, every participant a target: the Repair picker cone, heal team mates /
//   damage enemies at RepairAmount 60 / s per tick) [the beam ribbon / squib presentation for participants: PARTIAL].
// - Aimed abilities: HardLock / MarkTarget (homing-lock pick from the seat's eye toward the aim, 10 s), AbilityJammer / TransformDisruptor
//   (the buff shot toward the aim point; buff shots now carry their owner, so a Remote seat's shot can hit the local pawn).
// - Remote Shockwave: its 0.25 s delay ticks in tickRemoteSeats (only bots and the local player ticked it).
// - Still PARTIAL: the guided missile for Remote seats (the missile is the local player's singleton).
// Offline / bot matches: unchanged (buff shots of the local player keep owner -1 = local). Run from a tree root (integration f46bdfe4+).
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
    ['struct RemoteCommand { uint32_t tick = 0; int8_t moveForward = 0, moveRight = 0; uint32_t buttons = 0, pressed = 0; float aimYaw = 0.0f, aimPitch = 0.0f; };\n',
     'struct RemoteCommand { uint32_t tick = 0; int8_t moveForward = 0, moveRight = 0; uint32_t buttons = 0, pressed = 0; float aimYaw = 0.0f, aimPitch = 0.0f;\n' +
     '                       core::Vec3 aimPoint{0, 0, 0}; };   // aimPoint: world (3 x int24 at 1 cm), valid with kRemoteBitAimPoint\n'],
    ['constexpr uint32_t kRemoteBitActive = 1u << 30;   // the sender\'s look axis was non-zero this tick or it sent team chat (idle rule)\n',
     'constexpr uint32_t kRemoteBitActive = 1u << 30;   // the sender\'s look axis was non-zero this tick or it sent team chat (idle rule)\n' +
     'constexpr uint32_t kRemoteBitAimPoint = 1u << 31; // aimPoint is valid (the sender\'s camera-trace result); not an input for the idle rule\n'],
    ['    void applyQueuedRemoteCommands();\n    void applyRemoteNow(MatchOpponent& o, const RemoteCommand& c);\n    bool remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, MoveIntent& in);\n    void remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& dir);\n',
     '    void applyQueuedRemoteCommands(float dt);\n    void applyRemoteNow(MatchOpponent& o, const RemoteCommand& c, float dt);\n' +
     '    bool remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in);\n' +
     '    void remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& aimPt, bool consume = true);\n' +
     '    core::Vec3 seatAimPoint(const Character& pc, const RemoteCommand& c, float range) const;\n' +
     '    void participantBeam(int shooter, const Weapon& w, const core::Vec3& eye, const core::Vec3& dir);\n' +
     '    int pickTargetFrom(int self, const core::Vec3& eye, const core::Vec3& fwd, bool allowRobots, float range) const;\n'],
    ['    struct BuffShot { core::Vec3 pos, vel; float radius, life; int kind; };   // kind 0 jammer, 1 transform disruptor\n',
     '    struct BuffShot { core::Vec3 pos, vel; float radius, life; int kind; int owner = -1; };   // kind 0 jammer, 1 transform disruptor; owner -1 = local\n'],
]);
ed('src/game/World.cpp', [
    // idle: bit 31 is not an input
    ['    if (c.buttons || c.pressed || c.moveForward || c.moveRight) noteRemoteCommand(p);\n',
     '    if ((c.buttons & ~kRemoteBitAimPoint) || c.pressed || c.moveForward || c.moveRight) noteRemoteCommand(p);   // bit 31: not an input\n'],
    ['void World::applyQueuedRemoteCommands() {\n', 'void World::applyQueuedRemoteCommands(float dt) {\n'],
    ['if (o->matchPlayer() == q.first && (size_t)q.first < match_.players().size() && match_.players()[(size_t)q.first].kind == ParticipantKind::Remote) applyRemoteNow(*o, q.second);\n',
     'if (o->matchPlayer() == q.first && (size_t)q.first < match_.players().size() && match_.players()[(size_t)q.first].kind == ParticipantKind::Remote) applyRemoteNow(*o, q.second, dt);\n'],
    ['void World::applyRemoteNow(MatchOpponent& oRef, const RemoteCommand& c) {\n', 'void World::applyRemoteNow(MatchOpponent& oRef, const RemoteCommand& c, float dt) {\n'],
    ['        if (robot && down(platform::Button::Dash)) remoteTriggerAbility(*o, 0, c.aimYaw, in);\n' +
     '        if (robot && down(platform::Button::Ability1)) remoteTriggerAbility(*o, 1, c.aimYaw, in);\n',
     '        const core::Vec3 aimPt = seatAimPoint(pc, c, 300.0f);   // abilities: the homing / buff-shot reach\n' +
     '        if (robot && down(platform::Button::Dash)) remoteTriggerAbility(*o, 0, c.aimYaw, aimPt, in);\n' +
     '        if (robot && down(platform::Button::Ability1)) remoteTriggerAbility(*o, 1, c.aimYaw, aimPt, in);\n' +
     '        // Killstreak (TriggerKillstreak, B): the newest earned reward, robot form [the same entry the bots use].\n' +
     '        if (robot && down(platform::Button::Killstreak) && !match_.players()[(size_t)p].acquiredKillstreaks.empty()) triggerKillstreakFor(p);\n'],
    ['        const bool wantFire = w && !pc.isMeleeing() && (w->autoFire ? held(platform::Button::Fire) : down(platform::Button::Fire));\n' +
     '        if (wantFire && w->canFire()) remoteFire(*o, *w, core::forwardFromYawPitch(c.aimYaw, c.aimPitch));\n' +
     '        else if (wantFire && w->ammo == 0 && w->canReload()) w->beginReload();\n',
     '        const core::Vec3 fireAt = w ? seatAimPoint(pc, c, w->rangeM > 0.0f ? w->rangeM : 300.0f) : core::Vec3{0, 0, 0};\n' +
     '        if (w && robot && w->charge()) {\n' +
     '            // TnChargeWeapon (the local path\'s state machine): hold -> Charging, the level by ChargeTime, full charge drains clip ammo,\n' +
     '            // release fires the level\'s projectile (level 1: a fizzle), an empty clip reloads.\n' +
     '            const bool chargeHeld = held(platform::Button::Fire) || down(platform::Button::Fire);\n' +
     '            const bool usable = !pc.isMeleeing();\n' +
     '            if (w->chargeState == 0) {\n' +
     '                if (chargeHeld && usable && w->ammo > 0 && !w->reloading() && w->sinceCharge >= w->fireInterval) { w->setChargeState(1); w->chargeTime = 0.0f; w->chargeDrained = 0.0f; }\n' +
     '                else if (usable && w->ammo == 0 && w->canReload()) w->beginReload();\n' +
     '            } else if (!usable || w->reloading()) {\n' +
     '                w->setChargeState(0); w->sinceCharge = 0.0f;\n' +
     '            } else {\n' +
     '                w->chargeTime += dt;\n' +
     '                w->setChargeState(w->desiredChargeState());\n' +
     '                bool release = !held(platform::Button::Fire);\n' +
     '                if (w->chargeState == 4 && !release) {\n' +
     '                    const float nd = w->chargeDrained + dt * Weapon::kChargeDrainRate;\n' +
     '                    const int take = (int)nd - (int)w->chargeDrained;\n' +
     '                    w->chargeDrained = nd; w->ammo = std::max(0, w->ammo - take);\n' +
     '                    if (w->ammo == 0) release = true;\n' +
     '                }\n' +
     '                if (release) {\n' +
     '                    if (w->chargeState >= 2) {\n' +
     '                        const int mode = w->chargeState - 2;\n' +
     '                        const Weapon::ChargeLevel& L = Weapon::chargeLevel(mode);\n' +
     '                        w->chargeShotLevel = mode + 1;\n' +
     '                        Weapon shot = *w;\n' +
     '                        shot.projSpeed = L.speed; shot.projDamage = L.damage; shot.projRadiusM = L.radiusM; shot.projClass = mode;\n' +
     '                        w->onFired();\n' +
     '                        w->ammo = std::max(0, w->ammo - (L.shotCost - 1));\n' +
     '                        remoteFire(*o, shot, fireAt, false);\n' +
     '                    } else ++w->chargeFizzle;\n' +
     '                    w->setChargeState(0); w->sinceCharge = 0.0f;\n' +
     '                    if (w->ammo == 0 && w->canReload()) w->beginReload();\n' +
     '                }\n' +
     '            }\n' +
     '        } else {\n' +
     '            const bool wantFire = w && !pc.isMeleeing() && (w->autoFire ? held(platform::Button::Fire) : down(platform::Button::Fire));\n' +
     '            if (wantFire && w->canFire()) remoteFire(*o, *w, fireAt);\n' +
     '            else if (wantFire && w->ammo == 0 && w->canReload()) w->beginReload();\n' +
     '        }\n'],
    ['                    remoteGrenades_.push_back({p, 0.4f, botEye(pc) + core::forwardFromYawPitch(c.aimYaw, c.aimPitch) * 20.0f});\n',
     '                    remoteGrenades_.push_back({p, 0.4f, (c.buttons & kRemoteBitAimPoint) ? c.aimPoint : botEye(pc) + core::forwardFromYawPitch(c.aimYaw, c.aimPitch) * 20.0f});\n'],
    ['bool World::remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, MoveIntent& in) {\n',
     'bool World::remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in) {\n'],
    ['    else if (a.id == "Drain") pc.drainRemain_ = 7.0f;\n',
     '    else if (a.id == "Drain") pc.drainRemain_ = 7.0f;\n' +
     '    else if (a.id == "HardLock" || a.id == "MarkTarget") {   // TnBuffHardLocked 10 s on the homing-lock pick (picker 4) toward the aim; fails without one\n' +
     '        pc.playAction("Skill_MarkTarget", false);\n' +
     '        const core::Vec3 eye = botEye(pc);\n' +
     '        const int tgt = pickTargetFrom(p, eye, core::normalize(aimPt - eye), true, 300.0f);\n' +
     '        if (Character* t = tgt >= 0 ? participantPawnMutable(tgt) : nullptr) { t->hardLockedRemain_ = 10.0f; t->hardLockedByTeam_ = match_.players()[(size_t)p].team; }\n' +
     '    }\n' +
     '    else if (a.id == "AbilityJammer" || a.id == "TransformDisruptor") {   // the buff shot from the owner toward the aim point\n' +
     '        const bool jam = a.id == "AbilityJammer";\n' +
     '        pc.playAction("Skill_AbilityJammer", false);\n' +
     '        const core::Vec3 rt = core::normalize(core::cross(core::forwardFromYawPitch(pc.yaw(), 0.0f), core::Vec3{0, 1, 0}));\n' +
     '        const core::Vec3 from = pc.actorLocation() + (jam ? rt * 1.75f + core::Vec3{0, 0.25f, 0} : core::Vec3{0, 0.25f, 0});\n' +
     '        const core::Vec3 dir = core::normalize(aimPt - from);\n' +
     '        buffShots_.push_back({from, dir * (jam ? 100.0f : 60.0f), jam ? 5.0f : 2.5f, 3.0f, jam ? 0 : 1, p});\n' +
     '    }\n'],
    ['void World::remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& dir) {\n' +
     '    Character& pc = o.pawn();\n' +
     '    const int p = o.matchPlayer();\n' +
     '    pc.exposeSelf();\n' +
     '    w.onFired();\n' +
     '    pc.notifyFired();\n' +
     '    const core::Vec3 eye = botEye(pc), d = core::normalize(dir);\n',
     'core::Vec3 World::seatAimPoint(const Character& pc, const RemoteCommand& c, float range) const {\n' +
     '    if (c.buttons & kRemoteBitAimPoint) return c.aimPoint;   // the sender\'s camera-trace result (absolute)\n' +
     '    return botEye(pc) + core::forwardFromYawPitch(c.aimYaw, c.aimPitch) * range;\n' +
     '}\n\n' +
     'int World::pickTargetFrom(int self, const core::Vec3& eye, const core::Vec3& fwd, bool allowRobots, float range) const {\n' +
     '    // The homing-lock pick (picker 4) from any participant\'s eye: the enemy nearest the aim inside max(4 deg, atan(3.5 m / d)), in range.\n' +
     '    int best = -1; float bestAng = 1e9f;\n' +
     '    for (size_t i = 0; i < match_.players().size(); ++i) {\n' +
     '        const int q = (int)i;\n' +
     '        if (q == self || match_.sameTeam(q, self)) continue;\n' +
     '        const Character* t = participantPawn(q);\n' +
     '        if (!t || (!allowRobots && t->moveForm() == Form::Robot)) continue;\n' +
     '        const core::Vec3 d = t->actorLocation() - eye; const float dist = core::length(d);\n' +
     '        if (dist < 0.1f || dist > range) continue;\n' +
     '        const float ang = std::acos(core::clampf(core::dot(d * (1.0f / dist), fwd), -1.0f, 1.0f));\n' +
     '        if (ang > std::max(4.0f * 0.0174533f, std::atan(3.5f / dist)) || ang >= bestAng) continue;\n' +
     '        best = q; bestAng = ang;\n' +
     '    }\n' +
     '    return best;\n' +
     '}\n\n' +
     'void World::participantBeam(int shooter, const Weapon& w, const core::Vec3& eye, const core::Vec3& dir) {\n' +
     '    // TnWeaponRepair for any participant: GetRepairTarget (picker 6, Angle 4 deg widened by distance, no team filter) along the aim,\n' +
     '    // else the first pawn on the trace; heal a team mate / damage an enemy (RepairAmount 60 / s per fire tick).\n' +
     '    const float range = w.rangeM > 0.0f ? w.rangeM : 35.0f, tickSecs = w.fireInterval > 0.0f ? w.fireInterval : 0.1f;\n' +
     '    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);\n' +
     '    int hit = -1; float bestAng = 1e9f;\n' +
     '    for (size_t i = 0; i < match_.players().size(); ++i) {\n' +
     '        const int q = (int)i;\n' +
     '        const Character* t = q == shooter ? nullptr : participantPawn(q);\n' +
     '        if (!t) continue;\n' +
     '        const core::Vec3 to = t->actorLocation() - eye; const float d = core::length(to);\n' +
     '        if (d < 0.1f || d > range) continue;\n' +
     '        const float allowed = std::max(4.0f * 0.0174533f, std::atan(core::clampf(d * 0.1f, 2.0f, 4.0f) / d));\n' +
     '        const float ang = std::acos(core::clampf(core::dot(to * (1.0f / d), dir), -1.0f, 1.0f));\n' +
     '        float tl;\n' +
     '        if (ang > allowed || ang >= bestAng || (line && line->segmentHit(eye, t->actorLocation(), tl))) continue;\n' +
     '        hit = q; bestAng = ang;\n' +
     '    }\n' +
     '    if (hit < 0 || !matchActive_) return;\n' +
     '    if (match_.sameTeam(hit, shooter)) { if (Character* t = participantPawnMutable(hit)) t->health().heal(Health::HealType::AddHealthToAll, 60.0f * tickSecs); }\n' +
     '    else applyMatchDamage(hit, shooter, 60.0f * tickSecs, false, "TransGame.TnDamageTypeRepairEnemy");\n' +
     '}\n\n' +
     'void World::remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& aimPt, bool consume) {\n' +
     '    Character& pc = o.pawn();\n' +
     '    const int p = o.matchPlayer();\n' +
     '    pc.exposeSelf();\n' +
     '    if (consume) w.onFired();\n' +
     '    pc.notifyFired();\n' +
     '    const core::Vec3 eye = botEye(pc), d = core::normalize(aimPt - eye);   // from the eye toward the aim point\n' +
     '    if (w.beam()) { participantBeam(p, w, eye, d); return; }\n'],
    ['        participantShots_.push_back({p, w.def ? w.def->id : "", muzzle, eye + d * 50.0f, false, -1});\n',
     '        participantShots_.push_back({p, w.def ? w.def->id : "", muzzle, aimPt, false, -1});\n'],
    ['        tickMeleeFor(pc, p, dt);   // the melee sweeps / timer (the bots\' pass and the local tick run their own)\n',
     '        tickMeleeFor(pc, p, dt);   // the melee sweeps / timer (the bots\' pass and the local tick run their own)\n' +
     '        if (pc.shockwaveDelay_ >= 0.0f && (pc.shockwaveDelay_ -= dt) < 0.0f) applyShockwave(pc, p);   // Delay 0.25 -> Shockwave()\n'],
    ['    applyQueuedRemoteCommands();   // the Remote seats\' commands for this step (main thread, after the background part joined)\n',
     '    applyQueuedRemoteCommands(dt);   // the Remote seats\' commands for this step (main thread, after the background part joined)\n'],
    // buff shots: owner team (owner -1 = the local player); a Remote seat's shot can hit the local pawn
    ['            for (MatchOpponent* o : opponents_) {\n                if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;\n                Character& e = o->pawn();\n',
     '            for (size_t ti = 0; ti <= opponents_.size(); ++ti) {\n' +
     '                const int owner = s.owner >= 0 ? s.owner : localPlayer_;\n' +
     '                const bool localT = ti == opponents_.size();   // the local pawn last (a Remote seat\'s shot)\n' +
     '                if (localT && (owner == localPlayer_ || localDead_)) continue;\n' +
     '                MatchOpponent* o = localT ? nullptr : opponents_[ti];\n' +
     '                const int tp = localT ? localPlayer_ : o->matchPlayer();\n' +
     '                if ((!localT && !o->spawned()) || tp == owner || match_.sameTeam(tp, owner)) continue;\n' +
     '                Character& e = localT ? player_.pawn() : o->pawn();\n'],
    ['                if (s.kind == 0) { e.applyJammed(15.0f); LOG_INFO("ability AbilityJammer: player %d jammed 15 s", o->matchPlayer()); }\n',
     '                if (s.kind == 0) { e.applyJammed(15.0f); LOG_INFO("ability AbilityJammer: player %d jammed 15 s", tp); }\n'],
    ['                    LOG_INFO("ability TransformDisruptor: player %d forced to transform", o->matchPlayer());\n',
     '                    LOG_INFO("ability TransformDisruptor: player %d forced to transform", tp);\n'],
]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    // The PARTIAL group: killstreak, Plasma Cannon charge, beam, HardLock, jammer; one Remote seat against a harness enemy each.\n' +
    '    auto partialCase = [&](const char* what, game::Specialty spec, std::vector<std::string> weapons, std::vector<std::string> abilities, const std::function<bool(int, game::MatchOpponent*, game::RemoteCommand&, const std::function<void()>&, const std::function<void()>&)>& body) {\n' +
    '        game::MatchLaunch A; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", A);\n' +
    '        A.localTeam = 0;\n' +
    '        // A fresh member per case: earlier launches\' harness opponents (kind Remote, no member) stay in the player list, so\n' +
    '        // removeBots keeps the old seats too and the same fingerprint would find a stale one.\n' +
    '        static int caseNo = 0; const std::string member = "WFC-PART-" + std::to_string(++caseNo);\n' +
    '        game::RemoteSeat rs = seat("Remote1", 1, member.c_str()); rs.selection.specialty = spec; rs.selection.weapons = weapons; rs.selection.abilities = abilities;\n' +
    '        A.remotes = {rs};\n' +
    '        world_.launchMatch(A);\n' +
    '        world_.match().selectCharacter(world_.localMatchPlayer(), cs);\n' +
    '        std::vector<game::MatchOpponent*> tg;\n' +
    '        for (int i = 0; i < 3; ++i) tg.push_back(world_.addMatchOpponent("PT" + std::to_string(i), false));\n' +
    '        const int q = byMember(member.c_str());\n' +
    '        game::RemoteCommand cmd;\n' +
    '        std::function<void()> stepCmd = [&]() { cmd.tick = (uint32_t)(world_.simTick() + 1); world_.applyRemoteCommand(q, cmd); world_.handleInput(idle, dt); world_.tick(dt); cmd.pressed = 0; };\n' +
    '        for (int i = 0; i < 20 * 60; ++i) { bool all = world_.participantPawn(q) != nullptr; for (auto* t : tg) all &= t->spawned(); if (all) break; stepCmd(); }\n' +
    '        game::MatchOpponent* T = nullptr;\n' +
    '        for (auto* t : tg) if (t->spawned() && !world_.match().sameTeam(t->matchPlayer(), q)) { T = t; break; }\n' +
    '        const game::Character* rp = world_.participantPawn(q);\n' +
    '        if (!T || !rp) { check(false, std::string(what) + ": seat and target spawned"); return; }\n' +
    '        const core::Vec3 P = rp->actorLocation();\n' +
    '        world_.setParticipantPosition(T->matchPlayer(), P + core::Vec3{6.0f, 0.0f, 0.0f});\n' +
    '        std::function<void()> aimAt = [&]() {\n' +
    '            const game::Character* me2 = world_.participantPawn(q); const game::Character* tp = world_.participantPawn(T->matchPlayer());\n' +
    '            if (!me2 || !tp) return;\n' +
    '            const core::Vec3 d = tp->actorLocation() - me2->actorLocation();\n' +
    '            cmd.aimYaw = std::atan2(-d.x, -d.z); cmd.aimPitch = std::asin(core::clampf(d.y / std::max(core::length(d), 0.01f), -1.0f, 1.0f));\n' +
    '            cmd.aimPoint = tp->actorLocation(); cmd.buttons |= game::kRemoteBitAimPoint;   // the camera-trace result on the target\n' +
    '        };\n' +
    '        LOG_INFO("SESSIONTEST %s: seat weapon %s abilities %s / %s", what, rp->weapon().def ? rp->weapon().def->id : "-", rp->abilities_[0].id.c_str(), rp->abilities_[1].id.c_str());\n' +
    '        check(body(q, T, cmd, stepCmd, aimAt), what);\n' +
    '    };\n' +
    '    partialCase("remote killstreak trigger", game::Specialty::Soldier, {"AssaultRifle", "HomingRocket", "FlakGrenades"}, {"Hover", "Cloaking"},\n' +
    '        [&](int q, game::MatchOpponent*, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>&) {\n' +
    '            world_.match().playerMutable(q).acquiredKillstreaks.push_back("FastAbilityCooldownStreak");\n' +
    '            cmd.pressed = 1u << (int)platform::Button::Killstreak; stepCmd();\n' +
    '            for (int i = 0; i < 10; ++i) stepCmd();\n' +
    '            return world_.match().players()[(size_t)q].acquiredKillstreaks.empty();\n' +
    '        });\n' +
    '    partialCase("remote Plasma Cannon charge and release", game::Specialty::Scout, {"PlasmaCannon", "HeavyPistol", "FlakGrenades"}, {"Hover", "Cloaking"},\n' +
    '        [&](int q, game::MatchOpponent*, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            const game::Character* c = world_.participantPawn(q);\n' +
    '            if (!c || !c->weapon().charge()) return false;\n' +
    '            const int ammo0 = c->weapon().ammo;\n' +
    '            cmd.buttons = 1u << (int)platform::Button::Fire; cmd.pressed = cmd.buttons;\n' +
    '            for (int i = 0; i < 90; ++i) { aimAt(); stepCmd(); }\n' +
    '            const int state = world_.participantPawn(q)->weapon().chargeState;\n' +
    '            cmd.buttons = 0; aimAt(); stepCmd();   // release\n' +
    '            c = world_.participantPawn(q);\n' +
    '            return c && state >= 2 && c->weapon().chargeShotLevel >= 1 && c->weapon().ammo < ammo0;\n' +
    '        });\n' +
    '    partialCase("remote beam (repair ray) damages an enemy", game::Specialty::Scientist, {"RepairRay", "HeavyPistol", "HealGrenades"}, {"Hover", "Cloaking"},\n' +
    '        [&](int q3, game::MatchOpponent* T, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            const game::Character* tp = world_.participantPawn(T->matchPlayer());\n' +
    '            const game::Character* me3 = world_.participantPawn(q3);\n' +
    '            if (!tp || !me3 || !me3->weapon().beam()) return false;   // the repair ray must be the held weapon\n' +
    '            const float h0 = tp->health().current;\n' +
    '            cmd.buttons = 1u << (int)platform::Button::Fire;\n' +
    '            for (int i = 0; i < 60; ++i) { aimAt(); stepCmd(); }\n' +
    '            cmd.buttons = 0;\n' +
    '            tp = world_.participantPawn(T->matchPlayer());\n' +
    '            return !tp || tp->health().current < h0;\n' +
    '        });\n' +
    '    partialCase("remote HardLock marks the aimed enemy", game::Specialty::Soldier, {"AssaultRifle", "HomingRocket", "FlakGrenades"}, {"Hover", "HardLock"},\n' +
    '        [&](int, game::MatchOpponent* T, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            aimAt(); cmd.pressed = 1u << (int)platform::Button::Ability1; stepCmd();\n' +
    '            for (int i = 0; i < 5; ++i) { aimAt(); stepCmd(); }\n' +
    '            const game::Character* tp = world_.participantPawn(T->matchPlayer());\n' +
    '            return tp && tp->hardLockedRemain_ > 0.0f;\n' +
    '        });\n' +
    '    partialCase("remote AbilityJammer jams the aimed enemy", game::Specialty::Soldier, {"AssaultRifle", "HomingRocket", "FlakGrenades"}, {"Hover", "AbilityJammer"},\n' +
    '        [&](int, game::MatchOpponent* T, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            aimAt(); cmd.pressed = 1u << (int)platform::Button::Ability1; stepCmd();\n' +
    '            for (int i = 0; i < 40; ++i) { aimAt(); stepCmd(); }\n' +
    '            const game::Character* tp = world_.participantPawn(T->matchPlayer());\n' +
    '            return tp && tp->jammedRemain_ > 0.0f;\n' +
    '        });\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
