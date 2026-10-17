// Session hooks follow-up (Integration 2026-10-17, RE MP_INPUT_CONTRACT_RE.md 4a1f8fb, contract docs/handoff/REMOTE_COMMAND_CONTRACT.md):
// 1. The 180 s idle kick only with the host Game Mode Option "Kick Idle Players" (?WfcIdleKick=1 -> MatchSettings.kickIdle; default off:
//    the original never kicks in private matches, TnGame.InitGame clears bKickLiveIdlers when IsPrivateMatch).
// 2. World::applyRemoteCommand(matchPlayer, RemoteCommand) inside the lockstep step for tick N (= simTick() + 1): "active" = the original's
//    input rule (UserInputDetectedThisFrame + team chat): any held / pressed bit (incl. the sender's kRemoteBitActive: look axis or team
//    chat) or a non-zero move axis -> noteRemoteCommand. No aim-delta test.
// 3. FineAim (Button::FineAim) and Hover (kRemoteBitHover) are held bits (the original's move flags 512 / 1024), applied in that step.
//    The command drives the Remote pawn's movement intent (axes, aim, jump, boost, dash, ascend / descend, hover, transform)
//    [PARTIAL: fire / weapons / abilities / melee through the PlayerController path come next].
// 4. MatchLaunch.seed (online: the host's launch record): when set it replaces the local source (WFC_SEED / the fixed value) for the
//    match RNG, the bot identity seed and the bot brains, so every peer starts identical. Offline launches leave it 0: unchanged.
// Run from a tree root (integration 6c58c023+, with GAMEPLAY_SESSION_HOOKS).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Match.h', [[
    '    bool iconicMode = false;           // ?IconicMode=1 (GRI _bIconicMode): iconic characters are claimed, one holder per match\n',
    '    bool iconicMode = false;           // ?IconicMode=1 (GRI _bIconicMode): iconic characters are claimed, one holder per match\n' +
    '    bool kickIdle = false;             // ?WfcIdleKick=1: host option "Kick Idle Players" (online only; the original: public matches only)\n'
]]);
ed('src/game/World.h', [
    ['    std::vector<RemoteSeat> remotes;   // online: the other peers\' humans in the host\'s slot order (empty offline)\n',
     '    std::vector<RemoteSeat> remotes;   // online: the other peers\' humans in the host\'s slot order (empty offline)\n' +
     '    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n'],
    ['struct SlotSwap { int matchPlayer = -1; bool toBot = true; std::string member; int64_t atTick = 0; };\n',
     'struct SlotSwap { int matchPlayer = -1; bool toBot = true; std::string member; int64_t atTick = 0; };\n' +
     '// One participant\'s decoded lockstep command for sim tick `tick` (REMOTE_COMMAND_CONTRACT.md, agreed with Systems): absolute aim on the\n' +
     '// rotator grid (decoded to radians), int8 move axes, held / pressed bits = platform::Button bits + kRemoteBitHover + kRemoteBitActive.\n' +
     'struct RemoteCommand { uint32_t tick = 0; int8_t moveForward = 0, moveRight = 0; uint32_t buttons = 0, pressed = 0; float aimYaw = 0.0f, aimPitch = 0.0f; };\n' +
     'constexpr uint32_t kRemoteBitHover = 1u << 29;    // the original\'s move flag 1024 (Hover), held\n' +
     'constexpr uint32_t kRemoteBitActive = 1u << 30;   // the sender\'s look axis was non-zero this tick or it sent team chat (idle rule)\n'],
    ['    int64_t simTick() const { return simTick_; }\n',
     '    int64_t simTick() const { return simTick_; }\n' +
     '    // Inside the lockstep step for tick N = simTick() + 1, after all of N\'s commands arrived, before tick(); ascending match player.\n' +
     '    bool applyRemoteCommand(int matchPlayer, const RemoteCommand& c);\n' +
     '    uint64_t simStateHash() const;   // every pawn\'s position / velocity / health / yaw + the sim RNG (peer comparisons, tests)\n'],
    ['    int64_t simTick_ = 0;\n',
     '    int64_t simTick_ = 0;\n' +
     '    uint32_t hostSeed_ = 0;   // MatchLaunch.seed of the running match (0: the local source)\n'],
]);
ed('src/game/World.cpp', [
    ['    if (opt.count("IconicMode")) out.settings.iconicMode',
     '    if (opt.count("WfcIdleKick")) out.settings.kickIdle = std::atoi(opt["WfcIdleKick"].c_str()) != 0;   // host option "Kick Idle Players"\n' +
     '    if (opt.count("IconicMode")) out.settings.iconicMode'],
    ['        const char* sd = std::getenv("WFC_SEED");\n        core::simRandSeed(sd ? (uint32_t)std::strtoul(sd, nullptr, 10) * 2654435761u + 1u : 0x5eed2026u);\n',
     '        const char* sd = std::getenv("WFC_SEED");\n' +
     '        hostSeed_ = l.seed;   // online: the host\'s seed replaces the local source on every peer\n' +
     '        core::simRandSeed(l.seed ? l.seed * 2654435761u + 1u : sd ? (uint32_t)std::strtoul(sd, nullptr, 10) * 2654435761u + 1u : 0x5eed2026u);\n'],
    ['    if (const char* sd = std::getenv("WFC_SEED")) { const unsigned v = (unsigned)std::strtoul(sd, nullptr, 10); matchSeed = v * 2654435761u ^ 0x5eedu; std::srand(v); }\n',
     '    if (hostSeed_) { matchSeed = hostSeed_ * 2654435761u ^ 0x5eedu; std::srand(hostSeed_); }   // online: the host\'s seed\n' +
     '    else if (const char* sd = std::getenv("WFC_SEED")) { const unsigned v = (unsigned)std::strtoul(sd, nullptr, 10); matchSeed = v * 2654435761u ^ 0x5eedu; std::srand(v); }\n'],
    ['    // Host idle rule: a Remote participant without commands for kRemoteIdleKickSeconds of play is handed to a bot.\n    if (match_.state() == Match::State::InProgress)\n',
     '    // Host idle rule (only with "Kick Idle Players"): a Remote participant without input for kRemoteIdleKickSeconds of play goes to a bot.\n' +
     '    if (match_.settings().kickIdle && match_.state() == Match::State::InProgress)\n'],
    ['void World::noteRemoteCommand(int matchPlayer) {\n',
     'bool World::applyRemoteCommand(int p, const RemoteCommand& c) {\n' +
     '    static_assert((int)platform::Button::Count <= 29, "Button bits must stay below kRemoteBitHover");\n' +
     '    if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Remote) return false;\n' +
     '    if ((int64_t)c.tick != simTick_ + 1) return false;   // the step about to run\n' +
     '    // The original\'s "active": any held / pressed button (the sender\'s active bit = look axis / team chat) or a non-zero move axis.\n' +
     '    if (c.buttons || c.pressed || c.moveForward || c.moveRight) noteRemoteCommand(p);\n' +
     '    auto held = [&](platform::Button b) { return ((c.buttons >> (int)b) & 1u) != 0; };\n' +
     '    auto down = [&](platform::Button b) { return ((c.pressed >> (int)b) & 1u) != 0; };\n' +
     '    for (MatchOpponent* o : opponents_) {\n' +
     '        if (o->matchPlayer() != p) continue;\n' +
     '        MoveIntent in;\n' +
     '        in.moveForward = c.moveForward / 127.0f; in.moveRight = c.moveRight / 127.0f;\n' +
     '        in.faceYaw = c.aimYaw; in.viewPitch = c.aimPitch;\n' +
     '        in.wantJump = held(platform::Button::Jump);\n' +
     '        in.wantBoost = held(platform::Button::FineAim);          // RightMouseButton = "ToggleFineAim | Boost" (the original\'s move flag 512)\n' +
     '        in.wantDash = down(platform::Button::Dash);\n' +
     '        in.ascend = held(platform::Button::Ascend); in.descend = held(platform::Button::Descend);\n' +
     '        in.hoverRequest = (c.buttons & kRemoteBitHover) != 0;     // the original\'s move flag 1024\n' +
     '        o->setIntent(in);\n' +
     '        if (down(platform::Button::Transform) && o->spawned() && !o->pawn().isTransforming()) o->pawn().beginTransform();\n' +
     '    }\n' +
     '    return true;\n' +
     '}\n\n' +
     'uint64_t World::simStateHash() const {\n' +
     '    uint64_t h = 1469598103934665603ULL;\n' +
     '    auto mix = [&](const void* q, size_t n) { const unsigned char* b = (const unsigned char*)q; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } };\n' +
     '    auto pawn = [&](const Character& c) { const core::Vec3 p = c.position(), v = c.velocity(); const float hp = c.health().current, y = c.yaw(); mix(&p, sizeof p); mix(&v, sizeof v); mix(&hp, sizeof hp); mix(&y, sizeof y); };\n' +
     '    if (!localDead_) pawn(player_.pawn());\n' +
     '    for (const MatchOpponent* o : opponents_) if (o->spawned()) pawn(o->pawn());\n' +
     '    const uint32_t r = core::simRandState(); mix(&r, sizeof r);\n' +
     '    return h;\n' +
     '}\n\n' +
     'void World::noteRemoteCommand(int matchPlayer) {\n'],
]);
ed('src/game/WorldBots.cpp', [[
    '    if (const char* sd = std::getenv("WFC_SEED")) b.rng ^= (unsigned)std::strtoul(sd, nullptr, 10) * 0x85ebca6bU;   // DEV / TEST only\n',
    '    if (hostSeed_) b.rng ^= hostSeed_ * 0x85ebca6bU;   // online: the host\'s seed\n' +
    '    else if (const char* sd = std::getenv("WFC_SEED")) b.rng ^= (unsigned)std::strtoul(sd, nullptr, 10) * 0x85ebca6bU;   // DEV / TEST only\n'
]]);
ed('src/core/Application_Frontend.cpp', [[
    '    if (!m.onlineSlots.empty()) frontend::FlowTrace::emit("online.launchRemotes"',
    '    gl.seed = m.seed;   // online: the host\'s match seed (0 offline)\n' +
    '    if (!m.onlineSlots.empty()) frontend::FlowTrace::emit("online.launchRemotes"'
]]);
ed('src/core/Application.cpp', [
    ['    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);\n    L.localTeam = 0;\n    auto seat =',
     '    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?WfcIdleKick=1", L);   // the idle check needs the option\n    L.localTeam = 0;\n    auto seat ='],
    ['    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
     '    // Two peers given the same host seed (two launches here) reach an equal sim state at step 1000; offline (no seed) is unchanged.\n' +
     '    {\n' +
     '        uint64_t hs[2] = {0, 0};\n' +
     '        for (int peer = 0; peer < 2; ++peer) {\n' +
     '            game::MatchLaunch S; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=3", S);\n' +
     '            S.localTeam = 0; S.seed = 0x5eed1234u;\n' +
     '            world_.launchMatch(S);\n' +
     '            world_.match().selectCharacter(world_.localMatchPlayer(), cs);\n' +
     '            for (int i = 0; i < 1000; ++i) { world_.handleInput(idle, dt); world_.tick(dt); }\n' +
     '            hs[peer] = world_.simStateHash();\n' +
     '        }\n' +
     '        LOG_INFO("SESSIONTEST seed hashes %016llx %016llx", (unsigned long long)hs[0], (unsigned long long)hs[1]);\n' +
     '        check(hs[0] == hs[1] && hs[0] != 0, "two peers with the same host seed: equal sim state at step 1000");\n' +
     '    }\n' +
     '    // A Remote command for the step about to run is applied; an empty one does not count as input (idle clock keeps running).\n' +
     '    {\n' +
     '        game::MatchLaunch R; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?WfcIdleKick=1", R);\n' +
     '        R.localTeam = 0; R.remotes = {seat("Remote1", 1, "WFC-TEST-0001")};\n' +
     '        world_.launchMatch(R);\n' +
     '        const int q = byMember("WFC-TEST-0001");\n' +
     '        game::RemoteCommand c; c.tick = (uint32_t)(world_.simTick() + 1); c.moveForward = 127;\n' +
     '        game::RemoteCommand late; late.tick = (uint32_t)(world_.simTick() + 5);\n' +
     '        check(q >= 0 && world_.applyRemoteCommand(q, c) && !world_.applyRemoteCommand(q, late), "applyRemoteCommand: the step about to run only");\n' +
     '    }\n' +
     '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'],
]);
console.log('ok');
