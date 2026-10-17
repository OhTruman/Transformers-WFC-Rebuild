// Online step order (Systems' catch, part 2): every human seat - this machine's included - runs the same code at the same point of the
// step, in match player order, so the perspective (which seat is local) cannot change the simulation:
// - the local seat no longer moves in the step prefix (runLocalSeat): it joins the participants' movement + animation pool;
// - its per-step seat ticks (buff timers, abilities, melee, shockwave, weapons) run in tickRemoteSeats, which now walks the seats by
//   match player index (it walked opponents_ in creation order), and the local-only versions in the prefix / tickAbilityEffects skip;
// - Drain: one per-seat tick (the remote seats' Drain set the buff but never dealt damage: the drain tick was local-only);
// - separatePawns: online, the pawns by match player index and "the human yields last" for every human seat (it was the local pawn);
// - forced-direction volumes: a remote human counts as a player (onlyPlayers volumes; it counted as a bot).
// Offline (no seat order): unchanged. Run after edit_seat_order.js.
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
    '    void clearAllParticipants();\n',
    '    void clearAllParticipants();\n' +
    '    void tickSeatDrain(Character& pc, int p, float dt);   // TnBuffDrainSource for a seat (remote, or the online local seat)\n' +
    '    bool seatHuman(int p) const { return p >= 0 && (size_t)p < match_.players().size() && match_.players()[(size_t)p].kind != ParticipantKind::Bot; }\n'
]]);
ed('src/game/World.cpp', [
    // 1. The prefix: the local seat does not move here online.
    ['        forcedDirPre(player_.pawn(), dt, true);       // TnForcedDirVolume touches / pushes, then HardBlock (forcedDirPost)\n' +
     '        if (onlineSeats_) runLocalSeat(dt);           // online: the local seat through the wire path (the decoded command)\n' +
     '        else player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn\n' +
     '        forcedDirPost(player_.pawn());\n',
     '        if (!onlineSeats_) {   // online: the local seat moves with every other seat (the participants\' pool), in match player order\n' +
     '            forcedDirPre(player_.pawn(), dt, true);       // TnForcedDirVolume touches / pushes, then HardBlock (forcedDirPost)\n' +
     '            player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn\n' +
     '            forcedDirPost(player_.pawn());\n' +
     '        }\n'],
    // 2. The local buff timers: online in the seat ticks.
    ['        Character& lp = player_.pawn();\n' +
     '        lp.regenBuffRemain_ = std::max(0.0f, lp.regenBuffRemain_ - dt);\n' +
     '        lp.fastCooldownRemain_ = std::max(0.0f, lp.fastCooldownRemain_ - dt);\n' +
     '        if (lp.ammoLockRemain_ > 0.0f) {',
     '        Character& lp = player_.pawn();\n' +
     '        if (onlineSeats_) {}   // online: the seat ticks (tickRemoteSeats) run these for every seat\n' +
     '        else {\n' +
     '        lp.regenBuffRemain_ = std::max(0.0f, lp.regenBuffRemain_ - dt);\n' +
     '        lp.fastCooldownRemain_ = std::max(0.0f, lp.fastCooldownRemain_ - dt);\n' +
     '        }\n' +
     '        if (onlineSeats_) {}\n' +
     '        else if (lp.ammoLockRemain_ > 0.0f) {'],
    // 3. tickAbilityEffects: the local-only drain / melee / grenade / shockwave ticks skip online (the seat ticks run them).
    ['    if (pc.drainRemain_ > 0.0f) {\n        pc.drainRemain_ = localDead_ ? 0.0f : std::max(0.0f, pc.drainRemain_ - dt);\n',
     '    if (pc.drainRemain_ > 0.0f && !onlineSeats_) {\n        pc.drainRemain_ = localDead_ ? 0.0f : std::max(0.0f, pc.drainRemain_ - dt);\n'],
    ['    tickLocalMelee(dt);\n    tickHomingLock(dt);\n',
     '    if (!onlineSeats_) tickLocalMelee(dt);   // online: the seat ticks\n    tickHomingLock(dt);\n'],
    ['    if (pc.shockwaveDelay_ >= 0.0f) {\n        pc.shockwaveDelay_ -= dt;\n        if (pc.shockwaveDelay_ < 0.0f && !localDead_) applyShockwave(pc, localPlayer_);\n',
     '    if (pc.shockwaveDelay_ >= 0.0f && !onlineSeats_) {\n        pc.shockwaveDelay_ -= dt;\n        if (pc.shockwaveDelay_ < 0.0f && !localDead_) applyShockwave(pc, localPlayer_);\n'],
    // 4. The seat ticks by match player index, the online local seat included.
    ['void World::tickRemoteSeats(float dt) {\n' +
     '    for (MatchOpponent* o : opponents_) {\n' +
     '        const int p = o->matchPlayer();\n' +
     '        if (!o->spawned() || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Remote) continue;\n' +
     '        Character& pc = o->pawn();\n',
     'void World::tickRemoteSeats(float dt) {\n' +
     '    for (size_t i = 0; i < match_.players().size(); ++i) {   // by match player index (the same order on every peer)\n' +
     '        const int p = (int)i;\n' +
     '        const bool local = p == localPlayer_;\n' +
     '        if (local ? (!onlineSeats_ || localDead_) : match_.players()[i].kind != ParticipantKind::Remote) continue;\n' +
     '        Character* seat = local ? &player_.pawn() : nullptr;\n' +
     '        if (!local) { const MatchOpponent* o = (size_t)p < oppByPlayer_.size() ? oppByPlayer_[(size_t)p] : nullptr; if (!o || !o->spawned()) continue; seat = participantPawnMutable(p); }\n' +
     '        if (!seat) continue;\n' +
     '        Character& pc = *seat;\n'],
    ['        tickMeleeFor(pc, p, dt);   // the melee sweeps / timer (the bots\' pass and the local tick run their own)\n',
     '        tickMeleeFor(pc, p, dt);   // the melee sweeps / timer (the bots\' pass and the local tick run their own)\n' +
     '        tickSeatDrain(pc, p, dt);\n' +
     '        pc.ability().tick(dt);     // the legacy single-ability timer (the local path ticks it every step)\n'],
    // Every seat's aim pitch from its command (the remote seats kept 0; the local seat set it in runLocalSeat).
    ['        Character& pc = *s.pc;\n        if (pc.inputLocked()) { s.intent = MoveIntent{}; return; }   // TnBuffDecoyTrap: no movement / buttons\n',
     '        Character& pc = *s.pc;\n        pc.setAimPitch(c.aimPitch);\n        if (pc.inputLocked()) { s.intent = MoveIntent{}; return; }   // TnBuffDecoyTrap: no movement / buttons\n'],
    ['void World::clearAllParticipants() {\n',
     'void World::tickSeatDrain(Character& pc, int p, float dt) {\n' +
     '    // TnBuffDrainSource (the local path\'s tick, per seat): every enemy pawn within 2000 UU with line of sight takes 25 x dt, the caster\n' +
     '    // heals 35 x dt per target; targets by match player index.\n' +
     '    if (pc.drainRemain_ <= 0.0f) return;\n' +
     '    pc.drainRemain_ = std::max(0.0f, pc.drainRemain_ - dt);\n' +
     '    const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);\n' +
     '    int targets = 0;\n' +
     '    if (matchActive_)\n' +
     '        for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '            const Character* t = (int)q == p ? nullptr : participantPawn((int)q);\n' +
     '            if (!t || match_.sameTeam((int)q, p) || core::length(t->actorLocation() - pc.actorLocation()) > 20.0f) continue;\n' +
     '            float tt;\n' +
     '            if (line && line->segmentHit(pc.actorLocation(), t->actorLocation(), tt)) continue;\n' +
     '            ++targets;\n' +
     '            const core::Vec3 at = t->actorLocation();\n' +
     '            applyMatchDamage((int)q, p, 25.0f * dt, false, "TransGame.TnDamageTypeDrain");\n' +
     '            if (p == localPlayer_) onDrainVictimTick(at);   // [Systems M08i] presentation\n' +
     '        }\n' +
     '    if (targets > 0) pc.health().heal(Health::HealType::AddHealthToAll, 35.0f * dt * targets);\n' +
     '    if (p == localPlayer_) onDrainTick(targets);\n' +
     '}\n\n' +
     'void World::clearAllParticipants() {\n'],
    // 5. The participants' movement + animation pool: the online local seat is one more task (its own pawn only).
    ['        walkOut.assign(opponents_.size(), {});\n',
     '        const bool localTask = onlineSeats_ && !localDead_;   // online: the local seat moves here, like every other seat\n' +
     '        walkOut.assign(opponents_.size() + (localTask ? 1 : 0), {});\n'],
    ['        if (anyWalkOut) for (size_t i = 0; i < opponents_.size(); ++i) walkOut[i] = barrierIgnoreFor(opponents_[i]->matchPlayer());\n' +
     '        core::WorkerPool::get().run((int)opponents_.size(), [&](int i) {\n' +
     '            MatchOpponent* o = opponents_[(size_t)i];\n' +
     '            CollisionWorld::IgnoreDynamicScope walk(walkOut[(size_t)i].empty() ? nullptr : &walkOut[(size_t)i]);\n' +
     '            forcedDirPre(o->pawn(), dt, false);\n',
     '        if (anyWalkOut) for (size_t i = 0; i < opponents_.size(); ++i) walkOut[i] = barrierIgnoreFor(opponents_[i]->matchPlayer());\n' +
     '        if (anyWalkOut && localTask) walkOut[opponents_.size()] = barrierIgnoreFor(localPlayer_);\n' +
     '        core::WorkerPool::get().run((int)walkOut.size(), [&](int i) {\n' +
     '            if ((size_t)i == opponents_.size()) {   // the online local seat: the decoded command\'s intent\n' +
     '                Character& lp = player_.pawn();\n' +
     '                CollisionWorld::IgnoreDynamicScope walk(walkOut[(size_t)i].empty() ? nullptr : &walkOut[(size_t)i]);\n' +
     '                forcedDirPre(lp, dt, true);\n' +
     '                CharacterMovement::update(lp, localSeatIntent_, dt, col);\n' +
     '                forcedDirPost(lp);\n' +
     '                lp.updateAnimation(dt);\n' +
     '                return;\n' +
     '            }\n' +
     '            MatchOpponent* o = opponents_[(size_t)i];\n' +
     '            CollisionWorld::IgnoreDynamicScope walk(walkOut[(size_t)i].empty() ? nullptr : &walkOut[(size_t)i]);\n' +
     '            forcedDirPre(o->pawn(), dt, seatHuman(o->matchPlayer()));   // a human seat is a player (onlyPlayers volumes)\n'],
    ['        if (prof) { double sum = 0.0; for (double m : animMs) sum += m; tickProfAdd(5, sum); }\n    }\n',
     '        if (prof) { double sum = 0.0; for (double m : animMs) sum += m; tickProfAdd(5, sum); }\n' +
     '        if (localTask) {   // the edges are consumed; the presentation-yaw reference for the frames until the next step\n' +
     '            localSeatIntent_.wantJump = false; localSeatIntent_.wantDash = false; localSeatIntent_.dodgeDir = 0; localSeatIntent_.hoverRequest = false;\n' +
     '            player_.controller().presentationStep(localSeatIntent_.faceYaw, dt);\n' +
     '        }\n' +
     '    }\n'],
    ['    { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }\n',
     '    if (!(onlineSeats_ && !localDead_)) { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }   // online: in the pool\n'],
    // 6. separatePawns: online by match player index; every human seat yields last.
    ['    if (!localDead_) ps.push_back({&player_.pawn(), reach(player_.pawn())});\n' +
     '    for (MatchOpponent* o : opponents_) if (o->spawned()) ps.push_back({&o->pawn(), reach(o->pawn())});\n',
     '    static std::vector<char> human;   // per ps entry: a human seat (the bots yield to it)\n' +
     '    human.clear();\n' +
     '    if (seatOrdered_) {   // online: by match player index, every human seat alike\n' +
     '        for (size_t q = 0; q < match_.players().size(); ++q)\n' +
     '            if (Character* c = participantPawnMutable((int)q)) { ps.push_back({c, reach(*c)}); human.push_back(seatHuman((int)q) ? 1 : 0); }\n' +
     '    } else {\n' +
     '        if (!localDead_) { ps.push_back({&player_.pawn(), reach(player_.pawn())}); human.push_back(1); }\n' +
     '        for (MatchOpponent* o : opponents_) if (o->spawned()) { ps.push_back({&o->pawn(), reach(o->pawn())}); human.push_back(0); }\n' +
     '    }\n'],
    ['            const bool humanA = &A == &player_.pawn();   // the local pawn is only ever ps[0]\n',
     '            const bool humanA = human[i] && !human[j], humanB = human[j] && !human[i];   // offline the local pawn is only ever ps[0]\n'],
    ['                const bool humanIsRobot = &Rb == &player_.pawn(), humanIsVehicle = &V == &player_.pawn();\n',
     '                const bool humanIsRobot = va ? humanB : humanA, humanIsVehicle = va ? humanA : humanB;\n'],
    ['            if (humanA) { if (!tryMove(B, half * 2.0f)) tryMove(A, half * -2.0f); continue; }\n',
     '            if (humanA) { if (!tryMove(B, half * 2.0f)) tryMove(A, half * -2.0f); continue; }\n' +
     '            if (humanB) { if (!tryMove(A, half * -2.0f)) tryMove(B, half * 2.0f); continue; }   // online: a human seat later in the order\n'],
]);
// Smart hearing: online every seat's shots (the local seat's too) are participant shots; the local shot-serial hook is the direct path's.
ed('src/game/WorldBots.cpp', [[
    '    if (!localDead_ && localPlayer_ >= 0) {\n        const Character& pc = player_.pawn();\n        const Weapon* lw = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();\n',
    '    if (!localDead_ && localPlayer_ >= 0 && !onlineSeats_) {   // online: the seat path records the local seat\'s shots as participant shots\n        const Character& pc = player_.pawn();\n        const Weapon* lw = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();\n'
]]);
console.log('ok');
