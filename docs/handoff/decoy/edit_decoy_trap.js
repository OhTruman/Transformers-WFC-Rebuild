// Decoy Trap (Scout class-pool MP ability) [RE RE-Workspace/work/handoff/MP_DECOY_TRAP_RE.md 3f0271f; C unless marked]:
// - TnAbilityDecoyTrap: Cooldown 60 s, anim Skill_Barrier, DECOY_TRAP_DEPLOY at trigger; SpawnDelay 0.5 s -> a TnDecoyTrap dropped pickup
//   tossed with TossVelocity (2000, 1200, 0) in the owner's frame, team-stamped. One at a time: the cooldown starts once the trap is gone.
// - It poses as the owner's CURRENT weapon's pickup (weaponId); tells: spins the opposite way, defrags in over 3 s, then glitches out / in
//   every 5 s (1 s fade). Lifetime 30 s (H: MultiplayerLifeSpan); gone at once when the owner is dead.
// - Trigger: an enemy (not the owner, not the owner's team) picking it up like a weapon: touch cylinder r 200 / h 100 (+100 Z), the pickup
//   press (H: TakePickUp; bots press pickup every step as for the objectives). Robot form (ValidTouch). -> TnBuffDecoyTrap on the victim,
//   trap destroyed (pickup sound DECOY_TRAP_EXPLODE).
// - Destroyed by any non-team damage (FlashBang / Melee instantly; H: Health 0, so any hit): radius damage and melee sweeps here.
// - Stun (TnBuffDecoyTrap, 2.0 s): robot form HitReactions_React_DecoyTrap_In (2.17 s clip covers it) / _Out at the end; input lock
//   (SetCinematicMode movement / turning / buttons; HUD stays); velocity zeroed; ExposeSelf. No damage.
// - Not applied (as for the other abilities): skills (TnSkillDecoyDot 30 DPS x 2 s, TnSkillIncreaseDecoyTrapTime x1.5; no MP skills
//   in ours), the creator's "Victim Trapped" marker, the KillDecoyTrap XP (award point UNKNOWN in RE) [PARTIAL].
// - Presentation hooks: World::decoyTraps() (Rendering draws the owner's weapon pickup mesh at pos, yaw, alpha) and
//   World::onDecoyTrapEvent(Deploy / Trigger / StunEnd / Destroyed, owner, victim, pos), an empty stub Systems fills (DECOY_TRAP_DEPLOY,
//   DECOY_TRAP_EXPLODE, DecoyTrap_FX at the victim's TargetingSocket from Trigger to StunEnd).
// Bots never select it (class presets + the AI's own abilities): bot matches are identical. Run from a tree root (integration 8a080210+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Character.h', [
    ['    bool beaconAlive_ = false;\n',
     '    bool beaconAlive_ = false;\n' +
     '    bool decoyAlive_ = false;          // the owner\'s decoy trap (or its spawn) exists: the cooldown waits\n' +
     '    float decoyStunRemain_ = 0.0f;     // TnBuffDecoyTrap on this pawn: input locked while > 0\n' +
     '    int decoyStunBy_ = -1;             // the trap\'s creator (match player)\n' +
     '    bool inputLocked() const { return decoyStunRemain_ > 0.0f; }   // SetCinematicMode (movement, turning, buttons)\n' +
     '    void zeroVelocity() { velocity_ = core::Vec3{0, 0, 0}; }      // TnBuffDecoyTrap.Apply: Velocity and Acceleration zeroed\n'],
    ['a.id == "AbilityJammer" || a.id == "TransformDisruptor";\n',
     'a.id == "AbilityJammer" || a.id == "TransformDisruptor" || a.id == "DecoyTrap";\n'],
    ['|| a.id == "TransformDisruptor") ? 60.0f : 0.0f;',
     '|| a.id == "TransformDisruptor") ? 60.0f : a.id == "DecoyTrap" ? 60.0f : 0.0f;'],
    ['                !(a.id == "RollerSphere" && (rollerAlive_ || pendingAbilityEffect_ == "RollerSphere"))) {\n',
     '                !(a.id == "RollerSphere" && (rollerAlive_ || pendingAbilityEffect_ == "RollerSphere")) &&\n' +
     '                !(a.id == "DecoyTrap" && (decoyAlive_ || pendingAbilityEffect_ == "DecoyTrap"))) {\n'],
    ['meleeState_ = 0; meleeT_ = 0.0f; lungeRemain_ = 0.0f; meleeSweep_ = -1;',
     'meleeState_ = 0; meleeT_ = 0.0f; lungeRemain_ = 0.0f; decoyStunRemain_ = 0.0f; decoyStunBy_ = -1; meleeSweep_ = -1;'],
]);
ed('src/game/PlayerController.cpp', [
    ['void PlayerController::handleInput(const platform::InputFrame& in, float dt) {\n',
     'void PlayerController::handleInput(const platform::InputFrame& inRaw, float dt) {\n' +
     '    // TnBuffDecoyTrap: SetCinematicMode (movement, turning, buttons; not the HUD): the frame is ignored while stunned [CONF RE].\n' +
     '    static const platform::InputFrame kNoInput{};\n' +
     '    const platform::InputFrame& in = (pawn_ && pawn_->inputLocked()) ? kNoInput : inRaw;\n'],
    ['a.id == "HardLock" || a.id == "MarkTarget" || a.id == "AbilityJammer" || a.id == "TransformDisruptor") pawn_->pendingAbilityEffect_ = a.id;',
     'a.id == "HardLock" || a.id == "MarkTarget" || a.id == "AbilityJammer" || a.id == "TransformDisruptor" || a.id == "DecoyTrap") pawn_->pendingAbilityEffect_ = a.id;'],
]);
ed('src/game/WorldBots.cpp', [
    ['        if (!o->spawned()) continue;   // killed earlier in this pass\n',
     '        if (!o->spawned()) continue;   // killed earlier in this pass\n' +
     '        if (pc.inputLocked()) {        // TnBuffDecoyTrap: SetCinematicMode -> no movement, turning or buttons this step [CONF RE]\n' +
     '            pc.regenBuffRemain_ = std::max(0.0f, pc.regenBuffRemain_ - dt);\n' +
     '            pc.fastCooldownRemain_ = std::max(0.0f, pc.fastCooldownRemain_ - dt);\n' +
     '            pc.ammoLockRemain_ = std::max(0.0f, pc.ammoLockRemain_ - dt);\n' +
     '            pc.tickAbilities(dt);\n' +
     '            o->setIntent(MoveIntent{});\n' +
     '            continue;\n' +
     '        }\n'],
    ['        else if (a.id == "SpawnAmmoCrate") { pc.playAction("Skill_Barrier", false); requestAmmoBeacon(o.matchPlayer()); }',
     '        else if (a.id == "DecoyTrap") { pc.playAction("Skill_Barrier", false); requestDecoyTrap(o.matchPlayer()); }   // (no bot picks it)\n' +
     '        else if (a.id == "SpawnAmmoCrate") { pc.playAction("Skill_Barrier", false); requestAmmoBeacon(o.matchPlayer()); }'],
]);
ed('src/game/World.h', [
    ['    void requestAmmoBeacon(int owner);\n',
     '    void requestAmmoBeacon(int owner);\n' +
     '    // TnDroppedPickupDecoyTrap (TnAbilityDecoyTrap, any participant) [CONF RE MP_DECOY_TRAP_RE.md]. Rendering draws the pickup mesh\n' +
     '    // of weaponId (the owner\'s weapon at the toss) at pos with yaw (spinning the opposite way to a real drop) and alpha (defrag in\n' +
     '    // 3 s, glitch out / in every 5 s); alive && !landed = in flight. Read through decoyTraps().\n' +
     '    struct DecoyTrap { bool alive = false, landed = false; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float life = 0.0f, delay = -1.0f;\n' +
     '                       float yaw = 0.0f, alpha = 0.0f, armT = 0.0f, glitchT = 0.0f; int owner = -1, team = 255; std::string weaponId; int deadTicks = 0; };\n' +
     '    std::vector<DecoyTrap> decoys_;\n' +
     '    bool localPickupLatch_ = false;            // this step\'s local pickup press (the objectives consume the controller\'s)\n' +
     '    void requestDecoyTrap(int owner);\n' +
     '    void tickDecoyTraps(float dt);\n' +
     '    void destroyDecoyTrap(DecoyTrap& d);\n'],
    ['    const std::vector<AmmoBeacon>& ammoBeacons() const { return beacons_; }',
     '    const std::vector<DecoyTrap>& decoyTraps() const { return decoys_; }   // every participant\'s decoy traps (see DecoyTrap)\n' +
     '    // [Systems] Decoy Trap moments: Deploy (ability trigger: DECOY_TRAP_DEPLOY), Trigger (an enemy picked it up: DECOY_TRAP_EXPLODE;\n' +
     '    // DecoyTrap_FX on the victim\'s TargetingSocket until StunEnd), StunEnd, Destroyed (shot / meleed / expired / owner dead: fades).\n' +
     '    enum class DecoyEvent { Deploy, Trigger, StunEnd, Destroyed };\n' +
     '    void onDecoyTrapEvent(DecoyEvent e, int owner, int victim, const core::Vec3& pos);\n' +
     '    const std::vector<AmmoBeacon>& ammoBeacons() const { return beacons_; }'],
]);
ed('src/game/World.cpp', [
    ['    beacons_.clear();\n', '    beacons_.clear();\n    decoys_.clear();\n'],
    ['    { TickTimer tt(16); tickAmmoBeacon(dt); }\n', '    { TickTimer tt(16); tickAmmoBeacon(dt); tickDecoyTraps(dt); }\n'],
    ['        const bool localPickup = player_.controller().consumePickupRequest();\n',
     '        const bool localPickup = player_.controller().consumePickupRequest();\n' +
     '        localPickupLatch_ = localPickup;   // the decoy trap\'s pickup path reads it (next ability-actor tick)\n'],
    ['    } else if (fx == "SpawnAmmoCrate") {\n',
     '    } else if (fx == "DecoyTrap") {\n' +
     '        pc.playAction("Skill_Barrier", false);            // OnTriggerAnimParams Skill_Barrier\n' +
     '        requestDecoyTrap(localPlayer_);                   // SpawnDelay 0.5 -> SpawnInventory (TnDecoyTrap)\n' +
     '    } else if (fx == "SpawnAmmoCrate") {\n'],
    ['    for (size_t bi = 0; bi < beacons_.size(); ++bi)\n        if (beacons_[bi].alive && core::length(beacons_[bi].pos - at) < radius',
     '    for (DecoyTrap& d : decoys_)   // TnDroppedPickupDefrag.TakeDamage: any non-team damage destroys it (H: Health 0)\n' +
     '        if (d.alive && instigator >= 0 && instigator != d.owner && !(matchActive_ && match_.settings().teamGame && match_.sameTeam(instigator, d.owner)) &&\n' +
     '            core::length(d.pos - at) < radius && radiusVisible(at, d.pos))\n' +
     '            destroyDecoyTrap(d);\n' +
     '    for (size_t bi = 0; bi < beacons_.size(); ++bi)\n        if (beacons_[bi].alive && core::length(beacons_[bi].pos - at) < radius'],
    ['        const char* type = whirl ? "TransGame.TnDamageTypeWhirlwind"',
     '        for (DecoyTrap& d : decoys_)   // melee kills an enemy decoy trap instantly\n' +
     '            if (d.alive && self != d.owner && !(matchActive_ && match_.settings().teamGame && match_.sameTeam(self, d.owner)) &&\n' +
     '                std::fabs(d.pos.x - at.x) <= ex.x + 0.5f && std::fabs(d.pos.z - at.z) <= ex.z + 0.5f && std::fabs(d.pos.y - at.y) <= ex.y + 0.5f)\n' +
     '                destroyDecoyTrap(d);\n' +
     '        const char* type = whirl ? "TransGame.TnDamageTypeWhirlwind"'],
    ['void World::requestAmmoBeacon(int owner) {\n',
     '// ---- Decoy Trap [CONF RE MP_DECOY_TRAP_RE.md: TnAbilityDecoyTrap / TnDecoyTrap / TnDroppedPickupDecoyTrap / TnBuffDecoyTrap] ----\n' +
     'void World::onDecoyTrapEvent(DecoyEvent e, int owner, int victim, const core::Vec3& pos) {\n' +
     '    (void)e; (void)owner; (void)victim; (void)pos;   // [Systems] fills (sounds / FX); Gameplay\'s stub\n' +
     '}\n\n' +
     'void World::requestDecoyTrap(int owner) {\n' +
     '    // ServerTriggerAbility: DECOY_TRAP_DEPLOY now; SpawnDelay 0.5 -> SpawnInventory; one trap per owner (the cooldown waits for it).\n' +
     '    DecoyTrap d; d.owner = owner; d.delay = 0.5f;\n' +
     '    d.team = matchActive_ && owner >= 0 && (size_t)owner < match_.players().size() ? match_.players()[(size_t)owner].team : 255;\n' +
     '    Character* pc = participantPawnMutable(owner);\n' +
     '    decoys_.push_back(d);\n' +
     '    if (pc) pc->decoyAlive_ = true;\n' +
     '    onDecoyTrapEvent(DecoyEvent::Deploy, owner, -1, pc ? pc->actorLocation() : core::Vec3{0, 0, 0});\n' +
     '}\n\n' +
     'void World::destroyDecoyTrap(DecoyTrap& d) {\n' +
     '    if (!d.alive) return;\n' +
     '    d.alive = false;\n' +
     '    onDecoyTrapEvent(DecoyEvent::Destroyed, d.owner, -1, d.pos);\n' +
     '}\n\n' +
     'void World::tickDecoyTraps(float dt) {\n' +
     '    // The stun on every pawn: TnBuffDecoyTrap BuffTime 2.0; Unapply -> React_DecoyTrap_Out (robot form), input restored.\n' +
     '    auto tickStun = [&](Character& c) {\n' +
     '        if (c.decoyStunRemain_ <= 0.0f) return;\n' +
     '        c.decoyStunRemain_ -= dt;\n' +
     '        if (c.decoyStunRemain_ > 0.0f) return;\n' +
     '        c.decoyStunRemain_ = 0.0f;\n' +
     '        if (c.form() == Form::Robot && !c.isTransforming()) c.playAction("HitReactions_React_DecoyTrap_Out", false);\n' +
     '        onDecoyTrapEvent(DecoyEvent::StunEnd, c.decoyStunBy_, -1, c.actorLocation());\n' +
     '        c.decoyStunBy_ = -1;\n' +
     '    };\n' +
     '    if (!localDead_) tickStun(player_.pawn());\n' +
     '    for (MatchOpponent* o : opponents_) if (o->spawned()) tickStun(o->pawn());\n' +
     '    const bool teamGame = matchActive_ && match_.settings().teamGame;\n' +
     '    for (DecoyTrap& d : decoys_) {\n' +
     '        const Character* owner = participantPawn(d.owner);\n' +
     '        if (d.delay >= 0.0f) {\n' +
     '            d.delay -= dt;\n' +
     '            if (d.delay < 0.0f && owner) {   // DropFrom(owner location, TossVelocity (2000, 1200, 0) >> owner rotation)\n' +
     '                const core::Vec3 f = core::forwardFromYawPitch(owner->yaw(), 0.0f), r{-f.z, 0.0f, f.x};\n' +
     '                d.alive = true; d.landed = false; d.pos = owner->actorLocation(); d.vel = f * 20.0f + r * 12.0f;\n' +
     '                d.life = 30.0f; d.yaw = owner->yaw(); d.alpha = 0.0f; d.armT = 0.0f; d.glitchT = 0.0f;\n' +
     '                d.weaponId = owner->weapon().def ? owner->weapon().def->id : "";   // SetDecoyWeapon(the owner\'s current weapon)\n' +
     '                LOG_INFO("ability DecoyTrap (p%d): trap dropped as %s", d.owner, d.weaponId.c_str());\n' +
     '            }\n' +
     '        }\n' +
     '        if (d.alive) {\n' +
     '            d.life -= dt;\n' +
     '            if (d.life <= 0.0f || !owner) destroyDecoyTrap(d);   // lifespan (H: 30 s) / the owner dead -> FadeOut\n' +
     '        }\n' +
     '        if (d.alive && !d.landed) {   // PHYS_Falling until it lands (the beacon\'s drop physics)\n' +
     '            d.vel.y -= core::config::kGravity * dt;\n' +
     '            core::Vec3 next = d.pos + d.vel * dt;\n' +
     '            float t; core::Vec3 n;\n' +
     '            if (collision_.valid() && collision_.segmentHit(d.pos, next, t, n)) {\n' +
     '                next = d.pos + (next - d.pos) * std::max(0.0f, t - 1e-3f);\n' +
     '                if (n.y > 0.7f) { d.landed = true; d.vel = {0, 0, 0}; } else { d.vel.x = 0.0f; d.vel.z = 0.0f; }\n' +
     '            }\n' +
     '            d.pos = next;\n' +
     '            if (d.pos.y < killZ_) destroyDecoyTrap(d);\n' +
     '        }\n' +
     '        if (!d.alive) continue;\n' +
     '        // Tells: spins the opposite way to a real drop (yaw rate negated; Rendering applies the rate); defrags in over ArmDelay 3 s after\n' +
     '        // landing, then every GlitchDelay 5 s fades out over 1 s and back in [CONF; the fade-in rate H: 1 s].\n' +
     '        if (d.landed) {\n' +
     '            d.armT += dt;\n' +
     '            if (d.armT < 3.0f) d.alpha = d.armT / 3.0f;\n' +
     '            else {\n' +
     '                d.glitchT += dt;\n' +
     '                const float g = std::fmod(d.glitchT, 7.0f);   // 5 s shown, 1 s out, 1 s back in\n' +
     '                d.alpha = g < 5.0f ? 1.0f : (g < 6.0f ? 1.0f - (g - 5.0f) : g - 6.0f);\n' +
     '            }\n' +
     '        }\n' +
     '        // Pickup: an enemy robot pressing pickup inside the touch cylinder (r 200 UU, h 100 UU about +100 Z).\n' +
     '        const core::Vec3 cyl = d.pos + core::Vec3{0, 1.0f, 0};\n' +
     '        for (size_t i = 0; i < match_.players().size() && d.alive; ++i) {\n' +
     '            const int p = (int)i;\n' +
     '            if (p == d.owner || (teamGame && match_.sameTeam(p, d.owner))) continue;   // TnDecoyTrap.PickupAllowed\n' +
     '            Character* c = participantPawnMutable(p);\n' +
     '            if (!c || c->form() != Form::Robot || c->isTransforming() || c->inputLocked()) continue;\n' +
     '            bool presses = false;\n' +
     '            if (p == localPlayer_) presses = localPickupLatch_;\n' +
     '            else for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) { presses = o->pressesPickup; break; }\n' +
     '            if (!presses) continue;\n' +
     '            const core::Vec3 q = c->actorLocation();\n' +
     '            const float dx = q.x - cyl.x, dz = q.z - cyl.z;\n' +
     '            const float reach = 2.0f + c->cylinderRadius(c->moveForm());   // the touch cylinder overlaps the pawn\'s cylinder\n' +
     '            if (dx * dx + dz * dz > reach * reach || std::fabs(q.y - cyl.y) > 1.0f + c->cylinderHalfHeight(c->moveForm())) continue;\n' +
     '            // TnBuffPickup.GivenTo -> AddBuff(TnBuffDecoyTrap, applier = the creator); the trap is destroyed; DECOY_TRAP_EXPLODE.\n' +
     '            d.alive = false;\n' +
     '            c->decoyStunRemain_ = 2.0f;\n' +
     '            c->decoyStunBy_ = d.owner;\n' +
     '            c->zeroVelocity();                    // Velocity and Acceleration zeroed\n' +
     '            c->exposeSelf();                      // removes Cloak / Disguise\n' +
     '            c->playAction("HitReactions_React_DecoyTrap_In", false);   // robot form (2.17 s: covers the 2 s stun)\n' +
     '            LOG_INFO("ability DecoyTrap (p%d): p%d trapped", d.owner, p);\n' +
     '            onDecoyTrapEvent(DecoyEvent::Trigger, d.owner, p, d.pos);\n' +
     '        }\n' +
     '    }\n' +
     '    for (DecoyTrap& d : decoys_) if (!d.alive && d.delay < 0.0f) ++d.deadTicks;\n' +
     '    decoys_.erase(std::remove_if(decoys_.begin(), decoys_.end(), [](const DecoyTrap& d) { return d.deadTicks >= 2; }), decoys_.end());\n' +
     '    for (size_t i = 0; i < match_.players().size(); ++i)\n' +
     '        if (Character* pc = participantPawnMutable((int)i)) {\n' +
     '            bool any = false;\n' +
     '            for (const DecoyTrap& d : decoys_) any |= d.owner == (int)i && (d.alive || d.delay >= 0.0f);\n' +
     '            pc->decoyAlive_ = any;\n' +
     '        }\n' +
     '}\n\n' +
     'void World::requestAmmoBeacon(int owner) {\n'],
]);
ed('src/core/Application.h', [[
    '    void runEventTest();\n',
    '    void runEventTest();\n    void runDecoyTest();\n'
]]);
ed('src/core/Application_Frontend.cpp', [[
    '"WFC_EVENTTEST", ', '"WFC_EVENTTEST", "WFC_DECOYTEST", '
]]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }',
     '    if (std::getenv("WFC_DECOYTEST")) { runDecoyTest(); return false; }            // Decoy Trap ability (deploy, team rule, stun, cooldown)\n' +
     '    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }'],
    ['// WFC_PACINGTEST: presented camera',
     '// WFC_DECOYTEST: the Decoy Trap (RE MP_DECOY_TRAP_RE.md) over a scripted TDM match: the local Scout deploys it; a teammate cannot\n' +
     '// trigger it, an enemy pressing pickup inside the touch cylinder is stunned 2 s (input locked) and the trap goes; the 60 s cooldown\n' +
     '// starts only once the trap is gone; an untouched trap expires after 30 s.\n' +
     'void Application::runDecoyTest() {\n' +
     '    int checks = 0, fails = 0;\n' +
     '    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("DECOYTEST %s %s", ok ? "PASS" : "FAIL", what.c_str()); };\n' +
     '    const float dt = 1.0f / 60.0f;\n' +
     '    platform::InputFrame idle, ability;\n' +
     '    ability.pressed[(int)platform::Button::Ability1] = true; ability.down[(int)platform::Button::Ability1] = true;\n' +
     '    auto step = [&](const platform::InputFrame& in) { world_.handleInput(in, dt); world_.tick(dt); };\n' +
     '    auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) step(idle); };\n' +
     '    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);\n' +
     '    L.localTeam = 0;\n' +
     '    world_.launchMatch(L);\n' +
     '    const int me = world_.localMatchPlayer();\n' +
     '    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Scout;\n' +
     '    cs.weapons = {"Shotgun", "HeavyPistol", "FlashBangs"}; cs.abilities = {"Hover", "DecoyTrap"};\n' +
     '    world_.match().selectCharacter(me, cs);\n' +
     '    std::vector<game::MatchOpponent*> ops;\n' +
     '    for (int i = 0; i < 4; ++i) ops.push_back(world_.addMatchOpponent("DT" + std::to_string(i), false));\n' +
     '    run(11.0f);\n' +
     '    game::MatchOpponent* E = nullptr; game::MatchOpponent* F = nullptr;\n' +
     '    for (auto* o : ops) { if (!o->spawned()) continue; if (world_.match().sameTeam(o->matchPlayer(), me)) { if (!F) F = o; } else if (!E) E = o; }\n' +
     '    if (!E || !F || world_.localPlayerDead()) { check(false, "both teams spawned"); LOG_INFO("DECOYTEST SUMMARY: %d/%d checks passed", checks - fails, checks); return; }\n' +
     '    auto liveTrap = [&]() -> const game::World::DecoyTrap* { for (const auto& d : world_.decoyTraps()) if (d.alive && d.owner == me) return &d; return nullptr; };\n' +
     '    const core::Vec3 away = world_.player().pawn().actorLocation() + core::Vec3{0, 0, 40.0f};\n' +
     '    E->setPosition(away + core::Vec3{30.0f, 0, 0}); F->setPosition(away + core::Vec3{-30.0f, 0, 0});\n' +
     '    // Deploy: Skill_Barrier, SpawnDelay 0.5 s, then the toss.\n' +
     '    step(ability);\n' +
     '    run(0.3f);\n' +
     '    check(!liveTrap(), "no trap before SpawnDelay 0.5 s");\n' +
     '    run(2.7f);\n' +
     '    const game::World::DecoyTrap* t = liveTrap();\n' +
     '    const game::Weapon& myW = world_.player().pawn().weapon();\n' +
     '    check(t && t->landed && t->weaponId == (myW.def ? myW.def->id : ""), "the trap lands, posing as my current weapon");\n' +
     '    const auto& slot = world_.player().pawn().abilities_[1];\n' +
     '    check(slot.id == "DecoyTrap" && slot.pendingCooldown, "the cooldown waits while the trap exists");\n' +
     '    if (!t) { LOG_INFO("DECOYTEST SUMMARY: %d/%d checks passed", checks - fails, checks); return; }\n' +
     '    const core::Vec3 at = t->pos;\n' +
     '    // A teammate standing on it, pressing pickup: nothing.\n' +
     '    F->setPosition(at + core::Vec3{0, 0.1f, 0});\n' +
     '    run(0.5f);\n' +
     '    check(liveTrap() && !F->pawn().inputLocked(), "a teammate cannot trigger it");\n' +
     '    F->setPosition(away + core::Vec3{-30.0f, 0, 0});\n' +
     '    // An enemy pressing pickup inside the touch cylinder: stunned 2 s, the trap goes.\n' +
     '    E->setPosition(at + core::Vec3{1.5f, 0.1f, 0});\n' +
     '    run(0.1f);\n' +
     '    check(!liveTrap() && E->pawn().inputLocked() && E->pawn().decoyStunBy_ == me, "an enemy picking it up is stunned by me; the trap is gone");\n' +
     '    run(1.6f);\n' +
     '    check(E->pawn().inputLocked(), "still stunned at 1.7 s");\n' +
     '    run(0.5f);\n' +
     '    check(!E->pawn().inputLocked(), "the stun ends after 2 s");\n' +
     '    check(!slot.pendingCooldown && slot.cooldown > 55.0f, "the 60 s cooldown started once the trap was gone");\n' +
     '    // An untouched trap expires after 30 s.\n' +
     '    run(60.0f);\n' +
     '    E->setPosition(away + core::Vec3{30.0f, 0, 0});\n' +
     '    step(ability);\n' +
     '    run(3.0f);\n' +
     '    check(liveTrap() != nullptr, "a second trap after the cooldown");\n' +
     '    run(28.5f);\n' +
     '    check(!liveTrap(), "the trap expires (30 s lifespan)");\n' +
     '    LOG_INFO("DECOYTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n' +
     '}\n\n' +
     '// WFC_PACINGTEST: presented camera'],
]);
console.log('ok');
