// Remote seats, part 2 (Integration 2026-10-17): a Remote participant's command does everything a local pawn's input does.
// - applyRemoteCommand only QUEUES the command (it is called between steps, while the previous step's background part may still run);
//   applyQueuedRemoteCommands at the start of tickPrefix (after joinStep, main thread, ascending match player) applies it:
//   movement intent (axes, aim, jump, boost = FineAim, dash, ascend / descend, Hover bit, dodge), transform, fire (auto weapons while held,
//   others on the press; the vehicle weapon in vehicle form; empty -> reload), reload, weapon switch (Next / Prev), melee, abilities
//   (Ability0 = Dash in robot form, Ability1; the same effects as the bot / local paths, Decoy Trap included), grenade (thrown along the
//   aim, released 0.4 s later like the bots'), pickup press (Interact).
// - tickRemoteSeats after the bots: the Remote pawns' weapon / ability / buff ticks (bots tick theirs in their own pass; brainless
//   pawns did not tick at all).
// - Not yet: killstreak trigger, charge-weapon (Plasma Cannon) hold / release, the beam weapons [PARTIAL].
// Offline / bot matches: no Remote seats -> unchanged. Run from a tree root (integration 8454c173+, with GAMEPLAY_SESSION_FOLLOWUP).
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
    ['    uint32_t hostSeed_ = 0;   // MatchLaunch.seed of the running match (0: the local source)\n',
     '    uint32_t hostSeed_ = 0;   // MatchLaunch.seed of the running match (0: the local source)\n' +
     '    std::vector<std::pair<int, RemoteCommand>> remoteQueue_;   // commands for the step about to run (applied after joinStep)\n' +
     '    struct RemoteGrenade { int player = -1; float delay = -1.0f; core::Vec3 target{0, 0, 0}; };\n' +
     '    std::vector<RemoteGrenade> remoteGrenades_;\n' +
     '    void applyQueuedRemoteCommands();\n' +
     '    void applyRemoteNow(MatchOpponent& o, const RemoteCommand& c);\n' +
     '    bool remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, MoveIntent& in);\n' +
     '    void remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& dir);\n' +
     '    void tickRemoteSeats(float dt);\n'],
]);
ed('src/game/World.cpp', [
    ['    auto held = [&](platform::Button b) { return ((c.buttons >> (int)b) & 1u) != 0; };\n' +
     '    auto down = [&](platform::Button b) { return ((c.pressed >> (int)b) & 1u) != 0; };\n' +
     '    for (MatchOpponent* o : opponents_) {\n' +
     '        if (o->matchPlayer() != p) continue;\n',
     '    remoteQueue_.push_back({p, c});   // applied after joinStep (the previous step\'s background part may still be running now)\n' +
     '    return true;\n' +
     '}\n\n' +
     'void World::applyQueuedRemoteCommands() {\n' +
     '    std::stable_sort(remoteQueue_.begin(), remoteQueue_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });   // ascending match player\n' +
     '    for (const auto& q : remoteQueue_)\n' +
     '        for (MatchOpponent* o : opponents_)\n' +
     '            if (o->matchPlayer() == q.first && (size_t)q.first < match_.players().size() && match_.players()[(size_t)q.first].kind == ParticipantKind::Remote) applyRemoteNow(*o, q.second);\n' +
     '    remoteQueue_.clear();\n' +
     '}\n\n' +
     'void World::applyRemoteNow(MatchOpponent& oRef, const RemoteCommand& c) {\n' +
     '    MatchOpponent* o = &oRef;\n' +
     '    const int p = o->matchPlayer();\n' +
     '    auto held = [&](platform::Button b) { return ((c.buttons >> (int)b) & 1u) != 0; };\n' +
     '    auto down = [&](platform::Button b) { return ((c.pressed >> (int)b) & 1u) != 0; };\n' +
     '    o->pressesPickup = held(platform::Button::Interact) || down(platform::Button::Interact);   // TryPickup\n' +
     '    {\n'],
    ['        o->setIntent(in);\n' +
     '        if (down(platform::Button::Transform) && o->spawned() && !o->pawn().isTransforming()) o->pawn().beginTransform();\n' +
     '    }\n' +
     '    return true;\n' +
     '}\n',
     '        if (!o->spawned()) { o->setIntent(MoveIntent{}); return; }\n' +
     '        Character& pc = o->pawn();\n' +
     '        if (pc.inputLocked()) { o->setIntent(MoveIntent{}); return; }   // TnBuffDecoyTrap: no movement / buttons\n' +
     '        const bool robot = pc.moveForm() == Form::Robot && !pc.isTransforming();\n' +
     '        if (pc.moveForm() == Form::Robot) in.wantDash = false;   // robot form: Dash = Ability0 (the vehicle special move only in vehicle form)\n' +
     '        // Abilities: Ability0 = Dash in robot form (Shift), Ability1 (Ctrl) [CONF Controls card]; Dodge / Hover go into this step\'s intent.\n' +
     '        if (robot && down(platform::Button::Dash)) remoteTriggerAbility(*o, 0, c.aimYaw, in);\n' +
     '        if (robot && down(platform::Button::Ability1)) remoteTriggerAbility(*o, 1, c.aimYaw, in);\n' +
     '        o->setIntent(in);\n' +
     '        if (down(platform::Button::Transform) && !pc.isTransforming()) pc.beginTransform();\n' +
     '        if (robot && down(platform::Button::Melee) && !pc.isMeleeing()) startMeleeFor(pc, p, false, c.aimYaw);\n' +
     '        if (robot && down(platform::Button::NextWeapon)) pc.requestWeaponSwitch(1);\n' +
     '        if (robot && down(platform::Button::PrevWeapon)) pc.requestWeaponSwitch(-1);\n' +
     '        Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : (robot ? &pc.weapon() : nullptr);\n' +
     '        if (w && down(platform::Button::Reload) && w->canReload()) w->beginReload();\n' +
     '        // Fire: auto weapons while held, the others on the press (bAutoFire); an empty clip reloads.\n' +
     '        const bool wantFire = w && !pc.isMeleeing() && (w->autoFire ? held(platform::Button::Fire) : down(platform::Button::Fire));\n' +
     '        if (wantFire && w->canFire()) remoteFire(*o, *w, core::forwardFromYawPitch(c.aimYaw, c.aimPitch));\n' +
     '        else if (wantFire && w->ammo == 0 && w->canReload()) w->beginReload();\n' +
     '        // Grenade: TossGrenade along the aim (the throw anim, released 0.4 s later, as the bots\').\n' +
     '        if (robot && down(platform::Button::Grenade) && !pc.isMeleeing() && pc.carryingHeavy_ == 0) {\n' +
     '            bool pending = false;\n' +
     '            for (const RemoteGrenade& g : remoteGrenades_) pending |= g.player == p && g.delay >= 0.0f;\n' +
     '            for (Weapon& bag : pc.inventoryMutable())\n' +
     '                if (!pending && bag.grenade() && bag.reserve > 0) {\n' +
     '                    bag.reserve -= 1;\n' +
     '                    pc.playAction("GrenadeThrow", true);\n' +
     '                    pc.exposeSelf();\n' +
     '                    remoteGrenades_.push_back({p, 0.4f, botEye(pc) + core::forwardFromYawPitch(c.aimYaw, c.aimPitch) * 20.0f});\n' +
     '                    break;\n' +
     '                }\n' +
     '        }\n' +
     '    }\n' +
     '}\n\n' +
     'bool World::remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, MoveIntent& in) {\n' +
     '    Character& pc = o.pawn();\n' +
     '    if (slot < 0 || slot > 1 || pc.weapon().reloading() || pc.isDodging() || pc.jammedRemain_ > 0.0f) return false;\n' +
     '    Character::AbilitySlot& a = pc.abilities_[slot];\n' +
     '    if (!a.implemented || a.spam > 0.0f || a.cooldown > 0.0f || a.pendingCooldown) return false;\n' +
     '    const int p = o.matchPlayer();\n' +
     '    if (a.id == "Whirlwind") { if (pc.isMeleeing()) return false; startMeleeFor(pc, p, true, aimYaw); if (!pc.isMeleeing()) return false; }\n' +
     '    else if (a.id == "Dodge") {   // TnPlayerInput.Dodge: |Up| >= |Right| ? (Up < 0 back : forward) : (Right < 0 left : right)\n' +
     '        in.dodgeDir = std::fabs(in.moveForward) >= std::fabs(in.moveRight) ? (in.moveForward < 0.0f ? 4 : 3) : (in.moveRight < 0.0f ? 1 : 2);\n' +
     '    }\n' +
     '    else if (a.id == "Cloaking") pc.cloakRemain_ = 20.0f;\n' +
     '    else if (a.id == "Hover") { in.hoverRequest = true; pc.hoverRequested_ = true; }\n' +
     '    else if (a.id == "Warcry") applyWarcry(pc, p);\n' +
     '    else if (a.id == "Shockwave") pc.shockwaveDelay_ = 0.25f;\n' +
     '    else if (a.id == "SpawnSentry") requestSentry(p);\n' +
     '    else if (a.id == "Barrier") { pc.playAction("Skill_Barrier", false); requestBarrier(p); }\n' +
     '    else if (a.id == "DecoyTrap") { pc.playAction("Skill_Barrier", false); requestDecoyTrap(p); }\n' +
     '    else if (a.id == "SpawnAmmoCrate") { pc.playAction("Skill_Barrier", false); requestAmmoBeacon(p); }\n' +
     '    else if (a.id == "RollerSphere") { pc.playAction("Skill_AbilityJammer", false); requestRoller(p); }\n' +
     '    else if (a.id == "Drain") pc.drainRemain_ = 7.0f;\n' +
     '    else return false;   // [PARTIAL] the aimed abilities (HardLock / jammer / disruptor / guided missile) for Remote seats come later\n' +
     '    a.spam = 1.0f; a.pendingCooldown = true;\n' +
     '    if (participantAbilityHook) presentHook([this, p, id = std::string(a.id), ch = pc.chassis().id, at = pc.actorLocation()] { participantAbilityHook(p, id, ch, at); });\n' +
     '    return true;\n' +
     '}\n\n' +
     'void World::remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& dir) {\n' +
     '    Character& pc = o.pawn();\n' +
     '    const int p = o.matchPlayer();\n' +
     '    pc.exposeSelf();\n' +
     '    w.onFired();\n' +
     '    pc.notifyFired();\n' +
     '    const core::Vec3 eye = botEye(pc), d = core::normalize(dir);\n' +
     '    if (w.projectile()) {\n' +
     '        const core::Vec3 muzzle = eye + d * 1.5f;   // the bots\' muzzle stand-in (no posed held weapon on participants)\n' +
     '        spawnProjectile(muzzle, d * w.projSpeed, w, p);\n' +
     '        participantShots_.push_back({p, w.def ? w.def->id : "", muzzle, eye + d * 50.0f, false, -1});\n' +
     '    } else if (w.simulated() && !w.beam()) {\n' +
     '        for (int k = 0; k < std::max(1, w.shots); ++k) { pellet_ = k; fireHitscanAs(p, pc, w, eye, d); }\n' +
     '        pellet_ = 0;\n' +
     '    }\n' +
     '}\n\n' +
     'void World::tickRemoteSeats(float dt) {\n' +
     '    for (MatchOpponent* o : opponents_) {\n' +
     '        const int p = o->matchPlayer();\n' +
     '        if (!o->spawned() || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Remote) continue;\n' +
     '        Character& pc = o->pawn();\n' +
     '        pc.regenBuffRemain_ = std::max(0.0f, pc.regenBuffRemain_ - dt);\n' +
     '        pc.fastCooldownRemain_ = std::max(0.0f, pc.fastCooldownRemain_ - dt);\n' +
     '        pc.ammoLockRemain_ = std::max(0.0f, pc.ammoLockRemain_ - dt);\n' +
     '        pc.tickAbilities(dt);\n' +
     '        tickMeleeFor(pc, p, dt);   // the melee sweeps / timer (the bots\' pass and the local tick run their own)\n' +
     '        if (pc.moveForm() == Form::Robot) { pc.tickSpreadModifier(dt); pc.tickWeaponSwitch(dt); pc.weapon().tick(dt); }\n' +
     '        else if (Weapon* vw = pc.vehicleWeapon()) vw->tick(dt);\n' +
     '    }\n' +
     '    for (RemoteGrenade& g : remoteGrenades_) {\n' +
     '        if (g.delay < 0.0f || (g.delay -= dt) >= 0.0f) continue;\n' +
     '        Character* pc = participantPawnMutable(g.player);\n' +
     '        if (pc && pc->moveForm() == Form::Robot) for (Weapon& w : pc->inventoryMutable()) if (w.grenade()) { releaseGrenade(*pc, g.player, w, g.target); break; }\n' +
     '    }\n' +
     '    remoteGrenades_.erase(std::remove_if(remoteGrenades_.begin(), remoteGrenades_.end(), [](const RemoteGrenade& g) { return g.delay < 0.0f; }), remoteGrenades_.end());\n' +
     '}\n'],
    ['void World::tickPrefix(float dt) {\n    joinStep();\n',
     'void World::tickPrefix(float dt) {\n    joinStep();\n' +
     '    applyQueuedRemoteCommands();   // the Remote seats\' commands for this step (main thread, after the background part joined)\n'],
    ['    { TickTimer tt(3); tickBots(dt); }',
     '    tickRemoteSeats(dt);                                                 // the Remote seats\' weapon / ability ticks\n' +
     '    { TickTimer tt(3); tickBots(dt); }'],
    ['    simTick_ = 0; pendingSwaps_.clear(); remoteIdle_.clear(); remoteIdleKicks_ = 0;\n',
     '    simTick_ = 0; pendingSwaps_.clear(); remoteIdle_.clear(); remoteIdleKicks_ = 0; remoteQueue_.clear(); remoteGrenades_.clear();\n'],
]);
ed('src/game/World.h', [[
    '    bool applyRemoteCommand(int matchPlayer, const RemoteCommand& c);\n',
    '    bool applyRemoteCommand(int matchPlayer, const RemoteCommand& c);\n' +
    '    void setParticipantPosition(int matchPlayer, const core::Vec3& pos);   // tests: place a participant pawn (velocity zeroed)\n'
]]);
ed('src/game/World.cpp', [[
    'void World::tickRemoteSeats(float dt) {\n',
    'void World::setParticipantPosition(int p, const core::Vec3& pos) {\n' +
    '    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p && o->spawned()) o->setPosition(pos);\n' +
    '}\n\n' +
    'void World::tickRemoteSeats(float dt) {\n'
]]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    // A Remote seat does what a local pawn does: scripted commands fire (a kill + its kill-feed line), switch weapons, use an ability,\n' +
    '    // melee, throw a grenade and reload.\n' +
    '    {\n' +
    '        game::MatchLaunch A; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", A);\n' +
    '        A.localTeam = 0;\n' +
    '        game::RemoteSeat rs = seat("Remote1", 1, "WFC-TEST-0001"); rs.selection.abilities = {"Hover", "DecoyTrap"};\n' +
    '        A.remotes = {rs};\n' +
    '        world_.launchMatch(A);\n' +
    '        world_.match().selectCharacter(world_.localMatchPlayer(), cs);\n' +
    '        std::vector<game::MatchOpponent*> tgts;\n' +
    '        for (int i = 0; i < 3; ++i) tgts.push_back(world_.addMatchOpponent("Target" + std::to_string(i), false));\n' +
    '        const int q = byMember("WFC-TEST-0001");\n' +
    '        game::RemoteCommand cmd;\n' +
    '        auto stepCmd = [&]() { cmd.tick = (uint32_t)(world_.simTick() + 1); world_.applyRemoteCommand(q, cmd); world_.handleInput(idle, dt); world_.tick(dt); cmd.pressed = 0; };\n' +
    '        for (int i = 0; i < 20 * 60; ++i) { bool all = world_.participantPawn(q) != nullptr; for (auto* t : tgts) all &= t->spawned(); if (all) break; stepCmd(); }\n' +
    '        game::MatchOpponent* T = nullptr;\n' +
    '        for (auto* t : tgts) if (t->spawned() && !world_.match().sameTeam(t->matchPlayer(), q)) { T = t; break; }\n' +
    '        const game::Character* rp = world_.participantPawn(q);\n' +
    '        if (!T || !rp) check(false, "remote seat and an enemy target spawned");\n' +
    '        else {\n' +
    '            const core::Vec3 P = rp->actorLocation();\n' +
    '            auto aimAt = [&]() {\n' +
    '                const game::Character* me2 = world_.participantPawn(q); const game::Character* tp = world_.participantPawn(T->matchPlayer());\n' +
    '                if (!me2 || !tp) return;\n' +
    '                const core::Vec3 d = tp->actorLocation() - me2->actorLocation();\n' +
    '                cmd.aimYaw = std::atan2(-d.x, -d.z); cmd.aimPitch = std::asin(core::clampf(d.y / std::max(core::length(d), 0.01f), -1.0f, 1.0f));\n' +
    '            };\n' +
    '            const int kills0 = world_.match().players()[(size_t)q].kills;\n' +
    '            cmd.buttons = 1u << (int)platform::Button::Fire;\n' +
    '            for (int i = 0; i < 8 * 60 && world_.match().players()[(size_t)q].kills == kills0; ++i) {\n' +
    '                if (i % 30 == 0 && T->spawned()) world_.setParticipantPosition(T->matchPlayer(), P + core::Vec3{6.0f, 0.0f, 0.0f});\n' +
    '                aimAt(); stepCmd();\n' +
    '            }\n' +
    '            cmd.buttons = 0;\n' +
    '            bool feed = false;\n' +
    '            for (const auto& k : world_.match().killHistory()) feed |= k.killer == q && k.victim == T->matchPlayer();\n' +
    '            check(world_.match().players()[(size_t)q].kills == kills0 + 1 && feed, "remote fire: a scripted command sequence kills the target; the kill feed shows it");\n' +
    '            for (int i = 0; i < 120; ++i) stepCmd();\n' +
    '            const game::Character* r2 = world_.participantPawn(q);\n' +
    '            const std::string w0 = r2 && r2->weapon().def ? r2->weapon().def->id : "";\n' +
    '            cmd.pressed = 1u << (int)platform::Button::NextWeapon; stepCmd();\n' +
    '            for (int i = 0; i < 120; ++i) stepCmd();\n' +
    '            r2 = world_.participantPawn(q);\n' +
    '            check(r2 && r2->weapon().def && w0 != r2->weapon().def->id, "remote weapon switch (Next)");\n' +
    '            cmd.pressed = 1u << (int)platform::Button::Ability1; stepCmd();\n' +
    '            for (int i = 0; i < 60; ++i) stepCmd();\n' +
    '            bool trap = false; for (const auto& d : world_.decoyTraps()) trap |= d.owner == q && (d.alive || d.delay >= 0.0f);\n' +
    '            check(trap, "remote ability (Ability1 = Decoy Trap)");\n' +
    '            for (int i = 0; i < 60; ++i) stepCmd();\n' +
    '            cmd.pressed = 1u << (int)platform::Button::Melee; stepCmd();\n' +
    '            r2 = world_.participantPawn(q);\n' +
    '            check(r2 && r2->isMeleeing(), "remote melee");\n' +
    '            for (int i = 0; i < 120; ++i) stepCmd();\n' +
    '            auto bagReserve = [&]() { const game::Character* c3 = world_.participantPawn(q); if (c3) for (const auto& w : c3->inventory()) if (w.grenade()) return w.reserve; return -1; };\n' +
    '            const int g0 = bagReserve();\n' +
    '            { const game::Character* c4 = world_.participantPawn(q); if (c4) LOG_INFO("SESSIONTEST remote state: form %d transforming %d meleeing %d heavy %d dead %d", (int)c4->moveForm(), (int)c4->isTransforming(), (int)c4->isMeleeing(), c4->carryingHeavy_, (int)c4->health().isDead()); }\n' +
    '            cmd.pressed = 1u << (int)platform::Button::Grenade; stepCmd();\n' +
    '            for (int i = 0; i < 60; ++i) stepCmd();\n' +
    '            check(g0 > 0 && bagReserve() == g0 - 1, "remote grenade throw (reserve " + std::to_string(g0) + " -> " + std::to_string(bagReserve()) + ")");\n' +
    '            cmd.buttons = 1u << (int)platform::Button::Fire;\n' +
    '            for (int i = 0; i < 90; ++i) { aimAt(); if (i % 30 == 0) cmd.pressed = 1u << (int)platform::Button::Fire; stepCmd(); }   // semi-auto weapons fire on the press\n' +
    '            cmd.buttons = 0; cmd.pressed = 1u << (int)platform::Button::Reload; stepCmd();\n' +
    '            r2 = world_.participantPawn(q);\n' +
    '            check(r2 && r2->weapon().reloading(), "remote reload (" + std::string(r2 && r2->weapon().def ? r2->weapon().def->id : "-") + " ammo " + std::to_string(r2 ? r2->weapon().ammo : -1) + " reserve " + std::to_string(r2 ? r2->weapon().reserve : -1) + ")");\n' +
    '        }\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
