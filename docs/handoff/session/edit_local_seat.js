// The online local seat through the wire path (REMOTE_COMMAND_CONTRACT.md AMENDED, Integration decision (A)).
// - Online (MatchLaunch.online, from the session's launch record; never inferred from the seed): the local PlayerController still reads input and drives the camera every frame, but the SIM takes the
//   local pawn's input only from the decoded command for tick N, through the same seat path as the Remote seats:
//   World::encodeLocalCommand() builds the wire command for N = simTick() + 1 from the controller (int8 axes; aim on the rotator grid:
//   orbit yaw / pitch in robot form, the view rotation in vehicles; held + pressed bits accumulated since the last step; Active = a look
//   axis moved; the aim point = the crosshair ray's world hit from the direct path's start (the camera line's point nearest the pawn),
//   absolute, 1 cm). The transport (Systems) sends it and hands every peer's commands to applyRemoteCommand, the local one included.
// - The local Character then moves by CharacterMovement with the seat intent and gets the seat actions / ticks; PlayerController::
//   applyToPawn (the direct path) and World's own melee / grenade / killstreak / pickup requests are skipped online.
// - Offline nothing changes (no seed -> the direct path).
// - Seat fix: Jump in the command is an edge (pressed), as the direct path's wantJumpLatched_ (held was a repeat jump for Remote seats).
// - WFC_SESSIONTEST: two peers with the same seed and local command stream reach an equal simStateHash; the direct-vs-wire difference
//   (shot origin eye vs camera line, final position / yaw) is measured and logged.
// Run from a tree root (integration 1c5de2d8+).
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
    '    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n',
    '    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n' +
    '    bool online = false;               // a session launch (from the launch record): every human through the wire path; offline false\n'
]]);
ed('src/core/Application_Frontend.cpp', [[
    '    gl.seed = m.seed;   // online: the host\'s match seed (0 offline)\n',
    '    gl.seed = m.seed;   // online: the host\'s match seed (0 offline)\n' +
    '    gl.online = !m.onlineSlots.empty();   // a session launch: the wire path for every human (offline: none)\n'
]]);
ed('src/game/PlayerController.h', [[
    '    float accMouseDX_ = 0.0f, accMouseDY_ = 0.0f;\n',
    '    float accMouseDX_ = 0.0f, accMouseDY_ = 0.0f;\n' +
    '    uint32_t heldBits_ = 0, pressedBits_ = 0;   // online wire path: the last frame\'s held buttons / presses since the last step\n' +
    '    bool lookActive_ = false;                    // a look axis moved since the last step (the command\'s Active bit)\n' +
    'public:\n' +
    '    uint32_t heldBits() const { return heldBits_; }\n' +
    '    uint32_t takePressedBits() { const uint32_t b = pressedBits_; pressedBits_ = 0; return b; }\n' +
    '    bool takeLookActive() { const bool a = lookActive_; lookActive_ = false; return a; }\n' +
    'private:\n'
]]);
ed('src/game/PlayerController.cpp', [[
    '    const platform::InputFrame& in = (pawn_ && pawn_->inputLocked()) ? kNoInput : inRaw;\n',
    '    const platform::InputFrame& in = (pawn_ && pawn_->inputLocked()) ? kNoInput : inRaw;\n' +
    '    {   // online wire path: held buttons (this frame) and presses since the last step, the look-axis activity\n' +
    '        heldBits_ = 0;\n' +
    '        for (int b = 0; b < (int)platform::Button::Count && b < 29; ++b) { if (in.down[b]) heldBits_ |= 1u << b; if (in.pressed[b]) pressedBits_ |= 1u << b; }\n' +
    '        if (in.mouseDX != 0.0f || in.mouseDY != 0.0f || (in.padConnected && (std::fabs(in.padRX) > 0.25f || std::fabs(in.padRY) > 0.25f))) lookActive_ = true;\n' +
    '    }\n'
]]);
ed('src/game/World.h', [
    ['    void applyQueuedRemoteCommands(float dt);\n    void applyRemoteNow(MatchOpponent& o, const RemoteCommand& c, float dt);\n' +
     '    bool remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in);\n' +
     '    void remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& aimPt, bool consume = true);\n',
     '    // One seat\'s pawn for the seat path (a Remote participant\'s MatchOpponent, or the local pawn online): its intent / pickup press out.\n' +
     '    struct SeatRef { Character* pc = nullptr; int player = -1; bool spawned = false; MoveIntent intent; bool pressesPickup = false; };\n' +
     '    void applyQueuedRemoteCommands(float dt);\n    void applyRemoteNow(MatchOpponent& o, const RemoteCommand& c, float dt);\n' +
     '    void applySeat(SeatRef& s, const RemoteCommand& c, float dt);\n' +
     '    bool remoteTriggerAbility(SeatRef& s, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in);\n' +
     '    void remoteFire(SeatRef& s, Weapon& w, const core::Vec3& aimPt, bool consume = true);\n' +
     '    void runLocalSeat(float dt);\n' +
     '    bool onlineSeats_ = false;          // online: the local seat through the wire path (MatchLaunch.online, or the test override)\n' +
     '    int localWireOverride_ = -1;        // tests: -1 auto, 0 direct, 1 wire\n' +
     '    MoveIntent localSeatIntent_; bool localSeatPickup_ = false; float localSeatAimPitch_ = 0.0f;\n'],
    ['    bool applyRemoteCommand(int matchPlayer, const RemoteCommand& c);\n',
     '    bool applyRemoteCommand(int matchPlayer, const RemoteCommand& c);\n' +
     '    // Online: this machine\'s command for tick simTick() + 1 from the local controller (the wire encoding; see the contract).\n' +
     '    RemoteCommand encodeLocalCommand();\n' +
     '    bool onlineSeats() const { return onlineSeats_; }\n' +
     '    void setLocalWireOverride(int v) { localWireOverride_ = v; }   // tests: -1 auto (seed), 0 direct, 1 wire; applies at the next launch\n' +
     '    core::Vec3 seatEye(int matchPlayer) const;                      // the seat path\'s shot origin (eye) of a participant\n'],
]);
ed('src/game/World.cpp', [
    ['        hostSeed_ = l.seed;   // online: the host\'s seed replaces the local source on every peer\n',
     '        hostSeed_ = l.seed;   // online: the host\'s seed replaces the local source on every peer\n' +
     '        onlineSeats_ = localWireOverride_ >= 0 ? localWireOverride_ == 1 : l.online;   // online (a session launch): the local seat through the wire path\n' +
     '        localSeatIntent_ = MoveIntent{}; localSeatPickup_ = false;\n'],
    ['    if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Remote) return false;\n    if ((int64_t)c.tick != simTick_ + 1) return false;',
     '    if (p < 0 || (size_t)p >= match_.players().size() || (match_.players()[(size_t)p].kind != ParticipantKind::Remote && !(onlineSeats_ && p == localPlayer_))) return false;\n    if ((int64_t)c.tick != simTick_ + 1) return false;'],
    ['    for (const auto& q : remoteQueue_)\n        for (MatchOpponent* o : opponents_)\n',
     '    for (const auto& q : remoteQueue_) {\n' +
     '        if (onlineSeats_ && q.first == localPlayer_) {   // the local seat (online): the decoded command, as every peer applies it\n' +
     '            SeatRef s; s.pc = &player_.pawn(); s.player = localPlayer_; s.spawned = !localDead_;\n' +
     '            applySeat(s, q.second, dt);\n' +
     '            localSeatIntent_ = s.intent; localSeatPickup_ = s.pressesPickup; localSeatAimPitch_ = q.second.aimPitch;\n' +
     '            continue;\n' +
     '        }\n' +
     '        for (MatchOpponent* o : opponents_)\n'],
    ['applyRemoteNow(*o, q.second, dt);\n    remoteQueue_.clear();\n',
     'applyRemoteNow(*o, q.second, dt);\n    }\n    remoteQueue_.clear();\n'],
    ['void World::applyRemoteNow(MatchOpponent& oRef, const RemoteCommand& c, float dt) {\n    MatchOpponent* o = &oRef;\n    const int p = o->matchPlayer();\n',
     'void World::applyRemoteNow(MatchOpponent& o, const RemoteCommand& c, float dt) {\n' +
     '    SeatRef s; s.pc = &o.pawn(); s.player = o.matchPlayer(); s.spawned = o.spawned();\n' +
     '    applySeat(s, c, dt);\n' +
     '    o.setIntent(s.intent); o.pressesPickup = s.pressesPickup;\n' +
     '}\n\n' +
     'void World::applySeat(SeatRef& s, const RemoteCommand& c, float dt) {\n' +
     '    const int p = s.player;\n'],
    ['    o->pressesPickup = held(platform::Button::Interact) || down(platform::Button::Interact);',
     '    s.pressesPickup = held(platform::Button::Interact) || down(platform::Button::Interact);'],
    ['        in.wantJump = held(platform::Button::Jump);\n',
     '        in.wantJump = down(platform::Button::Jump);   // an edge, as the direct path\'s wantJumpLatched_\n'],
    ['        if (!o->spawned()) { o->setIntent(MoveIntent{}); return; }\n        Character& pc = o->pawn();\n        if (pc.inputLocked()) { o->setIntent(MoveIntent{}); return; }',
     '        if (!s.spawned) { s.intent = MoveIntent{}; return; }\n        Character& pc = *s.pc;\n        if (pc.inputLocked()) { s.intent = MoveIntent{}; return; }'],
    ['remoteTriggerAbility(*o, 0, c.aimYaw, aimPt, in);', 'remoteTriggerAbility(s, 0, c.aimYaw, aimPt, in);'],
    ['remoteTriggerAbility(*o, 1, c.aimYaw, aimPt, in);', 'remoteTriggerAbility(s, 1, c.aimYaw, aimPt, in);'],
    ['        o->setIntent(in);\n        if (down(platform::Button::Transform)', '        s.intent = in;\n        if (down(platform::Button::Transform)'],
    ['remoteFire(*o, shot, fireAt, false);', 'remoteFire(s, shot, fireAt, false);'],
    ['remoteFire(*o, *w, fireAt);', 'remoteFire(s, *w, fireAt);'],
    ['bool World::remoteTriggerAbility(MatchOpponent& o, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in) {\n    Character& pc = o.pawn();\n',
     'bool World::remoteTriggerAbility(SeatRef& s, int slot, float aimYaw, const core::Vec3& aimPt, MoveIntent& in) {\n    Character& pc = *s.pc;\n'],
    ['    const int p = o.matchPlayer();\n    if (a.id == "Whirlwind")', '    const int p = s.player;\n    if (a.id == "Whirlwind")'],
    ['void World::remoteFire(MatchOpponent& o, Weapon& w, const core::Vec3& aimPt, bool consume) {\n    Character& pc = o.pawn();\n    const int p = o.matchPlayer();\n',
     'void World::remoteFire(SeatRef& s, Weapon& w, const core::Vec3& aimPt, bool consume) {\n    Character& pc = *s.pc;\n    const int p = s.player;\n'],
    // the local seat online: movement + the direct path's per-step pawn ticks
    ['        player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn\n',
     '        if (onlineSeats_) runLocalSeat(dt);           // online: the local seat through the wire path (the decoded command)\n' +
     '        else player_.controller().applyToPawn(*this, dt);   // also feeds the aim pitch to the pawn\n'],
    ['        if (matchActive_ && player_.controller().consumeKillstreakRequest()) triggerLocalKillstreak();\n' +
     '        if (player_.controller().consumeMeleeRequest()) startLocalMelee(false);\n' +
     '        if (player_.controller().consumeGrenadeRequest()) startLocalGrenadeToss();\n',
     '        {   // online the seat command carries these (consumed here so nothing accumulates)\n' +
     '            const bool ks = player_.controller().consumeKillstreakRequest(), ml = player_.controller().consumeMeleeRequest(), gr = player_.controller().consumeGrenadeRequest();\n' +
     '            if (!onlineSeats_) {\n' +
     '                if (matchActive_ && ks) triggerLocalKillstreak();\n' +
     '                if (ml) startLocalMelee(false);\n' +
     '                if (gr) startLocalGrenadeToss();\n' +
     '            }\n' +
     '        }\n'],
    ['        const bool localPickup = player_.controller().consumePickupRequest();\n',
     '        const bool localPickupCtl = player_.controller().consumePickupRequest();\n' +
     '        const bool localPickup = onlineSeats_ ? localSeatPickup_ : localPickupCtl;   // online: the seat command\'s press\n'],
    ['void World::tickRemoteSeats(float dt) {\n',
     'void World::runLocalSeat(float dt) {\n' +
     '    // The direct path\'s per-step pawn work, from the decoded command (movement intent, aim pitch) instead of the controller.\n' +
     '    Character& pc = player_.pawn();\n' +
     '    CharacterMovement::update(pc, localSeatIntent_, dt, collision());\n' +
     '    localSeatIntent_.wantJump = false; localSeatIntent_.wantDash = false; localSeatIntent_.dodgeDir = 0; localSeatIntent_.hoverRequest = false;   // edges consumed\n' +
     '    pc.setAimPitch(localSeatAimPitch_);\n' +
     '    pc.tickAbilities(dt); pc.tickSpreadModifier(dt); pc.tickWeaponSwitch(dt); pc.weapon().tick(dt); pc.ability().tick(dt);\n' +
     '    if (pc.moveForm() == Form::Vehicle && !pc.isTransforming()) if (Weapon* vw = pc.vehicleWeapon()) vw->tick(dt);\n' +
     '}\n\n' +
     'RemoteCommand World::encodeLocalCommand() {\n' +
     '    PlayerController& ctl = player_.controller();\n' +
     '    RemoteCommand c; c.tick = (uint32_t)(simTick_ + 1);\n' +
     '    const MoveIntent& mi = ctl.moveIntent();\n' +
     '    auto q8 = [](float v) { return (int8_t)std::lround(core::clampf(v, -1.0f, 1.0f) * 127.0f); };\n' +
     '    c.moveForward = q8(mi.moveForward); c.moveRight = q8(mi.moveRight);\n' +
     '    const bool veh = !localDead_ && player_.pawn().moveForm() == Form::Vehicle;\n' +
     '    const float yaw = veh ? mi.faceYaw : ctl.camYaw(), pitch = veh ? mi.viewPitch : ctl.camPitch();   // the aim the direct path uses\n' +
     '    const float kR = 65536.0f / 6.2831853f;   // rotator units\n' +
     '    const uint16_t qy = (uint16_t)(std::lround(yaw * kR) & 0xffff);\n' +
     '    const int16_t qp = (int16_t)std::lround(core::clampf(pitch, -1.5707963f, 1.5707963f) * kR);\n' +
     '    c.aimYaw = std::remainder((float)qy / kR, 6.2831853f); c.aimPitch = (float)qp / kR;\n' +
     '    c.buttons = ctl.heldBits(); c.pressed = ctl.takePressedBits();\n' +
     '    if (ctl.takeLookActive()) c.buttons |= kRemoteBitActive;\n' +
     '    if (!localDead_) {   // the crosshair ray\'s hit, from the direct path\'s start (the camera line\'s point nearest the pawn), absolute, 1 cm\n' +
     '        const core::Vec3 camPos = ctl.cameraPos(), dir = core::forwardFromYawPitch(yaw, pitch);\n' +
     '        const core::Vec3 start = camPos + dir * core::dot(player_.pawn().actorLocation() - camPos, dir);\n' +
     '        const Weapon* w = veh ? player_.pawn().vehicleWeapon() : &player_.pawn().weapon();\n' +
     '        const float range = w && w->rangeM > 0.0f ? w->rangeM : 300.0f;\n' +
     '        core::Vec3 end = start + dir * range; float t;\n' +
     '        const CollisionWorld* line = weaponCollision_.valid() ? &weaponCollision_ : (collision_.valid() ? &collision_ : nullptr);\n' +
     '        if (line && line->segmentHit(start, end, t)) end = start + dir * (range * t);\n' +
     '        c.aimPoint = core::Vec3{std::round(end.x * 100.0f) / 100.0f, std::round(end.y * 100.0f) / 100.0f, std::round(end.z * 100.0f) / 100.0f};\n' +
     '        c.buttons |= kRemoteBitAimPoint;\n' +
     '    }\n' +
     '    return c;\n' +
     '}\n\n' +
     'core::Vec3 World::seatEye(int p) const {\n' +
     '    const Character* c = participantPawn(p);\n' +
     '    return c ? botEye(*c) : core::Vec3{0, 0, 0};\n' +
     '}\n\n' +
     'void World::tickRemoteSeats(float dt) {\n'],
]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n' +
    '    {\n' +
    '        uint64_t hs[2] = {0, 0};\n' +
    '        for (int peer = 0; peer < 2; ++peer) {\n' +
    '            game::MatchLaunch S; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?BotsFriendly=1?BotsEnemy=2", S);\n' +
    '            S.localTeam = 0; S.seed = 0x5eed4321u; S.online = true;\n' +
    '            world_.launchMatch(S);\n' +
    '            const int meL = world_.localMatchPlayer();\n' +
    '            world_.match().selectCharacter(meL, cs);\n' +
    '            for (int i = 0; i < 1000; ++i) {\n' +
    '                game::RemoteCommand c; c.tick = (uint32_t)(world_.simTick() + 1);\n' +
    '                c.moveForward = ((i / 120) % 2) ? 127 : 0; c.moveRight = ((i / 200) % 2) ? -64 : 0;\n' +
    '                c.aimYaw = std::remainder((float)(i % 4096) * (6.2831853f / 4096.0f), 6.2831853f); c.aimPitch = 0.0f;\n' +
    '                if (i >= 600 && i < 700) c.buttons = 1u << (int)platform::Button::Fire;\n' +
    '                if (i == 650) c.pressed = 1u << (int)platform::Button::Jump;\n' +
    '                world_.applyRemoteCommand(meL, c);\n' +
    '                world_.handleInput(idle, dt); world_.tick(dt);\n' +
    '            }\n' +
    '            hs[peer] = world_.simStateHash();\n' +
    '        }\n' +
    '        LOG_INFO("SESSIONTEST local-seat hashes %016llx %016llx", (unsigned long long)hs[0], (unsigned long long)hs[1]);\n' +
    '        check(world_.onlineSeats() && hs[0] == hs[1] && hs[0] != 0, "online local seat: two peers with the same local command stream reach an equal state at step 1000");\n' +
    '    }\n' +
    '    // Direct vs wire for the same local input frames (same seed): the measured difference, documented in the log.\n' +
    '    {\n' +
    '        core::Vec3 pos[2]; float yawv[2] = {0, 0}; float originGap = -1.0f;\n' +
    '        platform::InputFrame f; f.down[(int)platform::Button::Forward] = true;\n' +
    '        for (int arm = 0; arm < 2; ++arm) {\n' +
    '            world_.setLocalWireOverride(arm);   // 0 direct, 1 wire\n' +
    '            game::MatchLaunch S; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", S);\n' +
    '            S.localTeam = 0; S.seed = 0x5eed9999u;\n' +
    '            world_.launchMatch(S);\n' +
    '            const int meL = world_.localMatchPlayer();\n' +
    '            world_.match().selectCharacter(meL, cs);\n' +
    '            for (int i = 0; i < 900; ++i) {\n' +
    '                platform::InputFrame fi = (i >= 660) ? f : idle;\n' +
    '                fi.mouseDX = (i >= 660 && i < 760) ? 2.0f : 0.0f;\n' +
    '                world_.handleInput(fi, dt);\n' +
    '                if (arm == 1) { game::RemoteCommand c = world_.encodeLocalCommand(); world_.applyRemoteCommand(meL, c); }\n' +
    '                world_.tick(dt);\n' +
    '                if (arm == 1 && i == 800 && !world_.localPlayerDead()) {   // the seat\'s eye vs the direct path\'s camera-line start\n' +
    '                    const auto& ctl = world_.player().controller();\n' +
    '                    const core::Vec3 camPos = ctl.cameraPos(), dir = core::forwardFromYawPitch(ctl.camYaw(), ctl.camPitch());\n' +
    '                    const core::Vec3 start = camPos + dir * core::dot(world_.player().pawn().actorLocation() - camPos, dir);\n' +
    '                    originGap = core::length(world_.seatEye(meL) - start);\n' +
    '                }\n' +
    '            }\n' +
    '            pos[arm] = world_.player().pawn().actorLocation(); yawv[arm] = world_.player().pawn().yaw();\n' +
    '        }\n' +
    '        world_.setLocalWireOverride(-1);\n' +
    '        LOG_INFO("SESSIONTEST direct vs wire: final position gap %.3f m, yaw gap %.3f deg, shot origin eye vs camera line %.3f m",\n' +
    '                 core::length(pos[0] - pos[1]), std::fabs(std::remainder(yawv[0] - yawv[1], 6.2831853f)) * 57.29578f, originGap);\n' +
    '        check(originGap >= 0.0f && core::length(pos[0] - pos[1]) < 2.0f, "direct vs wire: both paths move the local pawn alike (gaps logged)");\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
