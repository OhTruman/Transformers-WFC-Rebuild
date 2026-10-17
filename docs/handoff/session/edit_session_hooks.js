// Item 4: the match side of online sessions (Frontend's session layer; docs/handoff/ONLINE_MP_FRONTEND_TESTS.md section 4) + Iconic Mode.
// - MatchPlayer.sessionMember: the session member fingerprint of a Remote participant ("" for Local / Bot).
// - MatchLaunch.remotes: the other peers' humans (game::RemoteSeat {name, team, member, selection}); each becomes a ParticipantKind::Remote
//   participant with a drawn MatchOpponent pawn and no BotBrain (its commands come from the session). Empty = offline, unchanged.
// - World::applySlotSwap(game::SlotSwap {matchPlayer | member, toBot, member, atTick}): the participant's kind changes in the same slot
//   at the agreed sim tick (atTick <= 0: the next tick); score row, team, index and the event serials stay. To Bot: a BotBrain (+ Smart
//   state) takes over the pawn at once if alive, else at its next spawn. To Remote (late join): the brain is removed.
// - Host idle rule: a Remote participant with no noteRemoteCommand() for 180 s of InProgress sim time is swapped to Bot.
// - World::simTick(): sim steps since the match launch (the swap clock).
// - Iconic Mode (?IconicMode=1, MatchSettings.iconicMode): GRI.ValidateCharacterSelection refuses an iconic character another participant
//   already holds (whole match, every team; CONFIRMED RE via Frontend); refused = silent, the previous selection stays (CONFIRMED).
//   CharacterSelection type 1 with an empty chassisId falls back to the faction's chassis.
// - WFC_SESSIONTEST: the section-4 checks (Application.cpp runSessionTest), scripted remote commands, no network.
// Game-side types (no frontend headers in game code); Frontend's Application glue converts frontend::SessionSlot / SessionSlotSwap.
// Offline / bot matches: identical (no remotes, no swaps; iconicMode off). Run from a tree root (integration 8a080210+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Match.h', [
    ['    bool teamGame = true;\n',
     '    bool teamGame = true;\n' +
     '    bool iconicMode = false;           // ?IconicMode=1 (GRI _bIconicMode): iconic characters are claimed, one holder per match\n'],
    ['    std::string spawnError;            // non-empty while the resolved chassis cannot be spawned (no substitute body)\n',
     '    std::string spawnError;            // non-empty while the resolved chassis cannot be spawned (no substitute body)\n' +
     '    std::string sessionMember;         // online: the session member fingerprint of a Remote participant ("" Local / Bot)\n'],
    ['    void selectCharacter(int p, const CharacterSelection& s) {\n' +
     '        if (p < 0 || (size_t)p >= players_.size()) return;\n',
     '    // Returns false when the host refuses the selection (the previous one stays).\n' +
     '    bool selectCharacter(int p, const CharacterSelection& s) {\n' +
     '        if (p < 0 || (size_t)p >= players_.size()) return false;\n' +
     '        // GRI.ValidateCharacterSelection (Iconic Mode): an iconic character another participant already holds (any team) is refused\n' +
     '        // silently; ReplicatedCharacterData is untouched [CONF RE]. Bots play the class presets and never claim.\n' +
     '        if (s_.iconicMode && s.type == 1)\n' +
     '            for (size_t i = 0; i < players_.size(); ++i)\n' +
     '                if ((int)i != p && players_[i].hasSelectedCharacter && players_[i].selection.type == 1 && players_[i].selection.chassisId == s.chassisId)\n' +
     '                    return false;\n'],
    ['        e.text = s.type == 1 ? s.chassisId : specialtyName(s.specialty);\n    }\n',
     '        e.text = s.type == 1 ? s.chassisId : specialtyName(s.specialty);\n        return true;\n    }\n'],
    ['        ReturnToLobby, RoundEnded, RoundStarted,        // MatchOver.OnCountdownComplete -> TnGame.ReturnToGameLobby (host handoff)\n',
     '        ReturnToLobby, RoundEnded, RoundStarted,        // MatchOver.OnCountdownComplete -> TnGame.ReturnToGameLobby (host handoff)\n' +
     '        RemoteIdle,           // online host rule: Remote participant `player` idle for World::kRemoteIdleKickSeconds; the session kicks that peer\n'],
    ['    const std::vector<MatchEvent>& events() const { return events_; }',
     '    void notifyRemoteIdle(int p) { emit(MatchEvent::Type::RemoteIdle, p); }   // the host idle rule fired for that Remote participant\n' +
     '    const std::vector<MatchEvent>& events() const { return events_; }'],
]);
ed('src/game/CharacterRoster.h', [[
    '    if (sel.type == 1) return sel.chassisId;                // iconic characters keep their own chassis\n',
    '    if (sel.type == 1 && !sel.chassisId.empty()) return sel.chassisId;   // iconic characters keep their own chassis (empty: the faction\'s entry)\n'
]]);
ed('src/game/World.h', [
    ['struct MatchLaunch {\n',
     '// Online (Frontend\'s session layer, PC EXTENSION): another peer\'s human in this match -> a ParticipantKind::Remote participant.\n' +
     'struct RemoteSeat { std::string name; int team = 255; std::string member; CharacterSelection selection; bool selected = false; };\n' +
     '// A slot handover the session agreed (frontend::SessionSlotSwap in game terms). matchPlayer < 0: found by member (sessionMember).\n' +
     '// toBot: Remote -> Bot; else Bot -> Remote (late join, member = who takes it). atTick: World::simTick() to apply at (<= 0: next tick).\n' +
     'struct SlotSwap { int matchPlayer = -1; bool toBot = true; std::string member; int64_t atTick = 0; };\n\n' +
     'struct MatchLaunch {\n'],
    ['    BotLaunch bots;                // ?BotsFriendly ?BotsEnemy ?BotDifficulty (PC ADAPTATION; absent = no bots)\n',
     '    BotLaunch bots;                // ?BotsFriendly ?BotsEnemy ?BotDifficulty (PC ADAPTATION; absent = no bots)\n' +
     '    std::vector<RemoteSeat> remotes;   // online: the other peers\' humans in the host\'s slot order (empty offline)\n'],
    ['    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);\n',
     '    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);\n' +
     '    // Online sessions (see RemoteSeat / SlotSwap): a Remote participant, a queued slot handover, the remote command clock.\n' +
     '    int addRemoteParticipant(const RemoteSeat& r);\n' +
     '    bool applySlotSwap(const SlotSwap& s);\n' +
     '    void noteRemoteCommand(int matchPlayer);   // a command from that Remote participant arrived (resets the idle clock)\n' +
     '    int64_t simTick() const { return simTick_; }\n' +
     '    int remoteIdleKicks() const { return remoteIdleKicks_; }   // idle-rule kicks this match (tests)\n' +
     '    static constexpr float kRemoteIdleKickSeconds = 180.0f;   // host rule: no commands for this long -> the slot becomes a Bot\n'],
    ['    std::vector<BotBrain> bots_;\n',
     '    std::vector<BotBrain> bots_;\n' +
     '    std::vector<SlotSwap> pendingSwaps_;   // agreed handovers waiting for their tick\n' +
     '    std::vector<float> remoteIdle_;        // per match player: InProgress seconds since the last remote command\n' +
     '    int64_t simTick_ = 0;\n' +
     '    int remoteIdleKicks_ = 0;\n' +
     '    void tickSlotSwaps(float dt);\n' +
     '    void swapSlotToBot(int p);\n' +
     '    void swapSlotToRemote(int p, const std::string& member);\n'],
]);
ed('src/game/World.cpp', [
    ['    if (opt.count("BotsFriendly")) out.bots.friendly',
     '    if (opt.count("IconicMode")) out.settings.iconicMode = std::atoi(opt["IconicMode"].c_str()) != 0;   // GRI _bIconicMode (Frontend URL)\n' +
     '    if (opt.count("BotsFriendly")) out.bots.friendly'],
    ['    const int nb = addBots(l.bots);\n',
     '    simTick_ = 0; pendingSwaps_.clear(); remoteIdle_.clear(); remoteIdleKicks_ = 0;\n' +
     '    for (const RemoteSeat& r : l.remotes) addRemoteParticipant(r);   // online: the other peers\' humans (before the bot fill counts them)\n' +
     '    const int nb = addBots(l.bots);\n'],
    ['{ if (match_.players()[i].kind != ParticipantKind::Bot) break; first = i; }',
     '{ if (match_.players()[i].kind != ParticipantKind::Bot && match_.players()[i].sessionMember.empty()) break; first = i; }'],
    ['void World::tickPrefix(float dt) {\n    joinStep();\n',
     'void World::tickPrefix(float dt) {\n    joinStep();\n' +
     '    tickSlotSwaps(dt);   // session slot handovers at their agreed tick (main thread, before any bot / pawn work)\n'],
    ['MatchOpponent* World::addMatchOpponent(const std::string& name, bool drawn) {\n',
     '// ---- Online sessions: Remote participants and slot handovers (see World.h RemoteSeat / SlotSwap) ----\n' +
     'int World::addRemoteParticipant(const RemoteSeat& r) {\n' +
     '    const int p = match_.addPlayer(r.name, r.team);\n' +
     '    {\n' +
     '        MatchPlayer& mp = match_.playerMutable(p);\n' +
     '        mp.kind = ParticipantKind::Remote;\n' +
     '        mp.sessionMember = r.member;\n' +
     '    }\n' +
     '    if (r.selected) match_.selectCharacter(p, r.selection);\n' +
     '    const MatchPlayer& mp = match_.players()[(size_t)p];\n' +
     '    auto o = std::make_unique<MatchOpponent>(p, mp.team, true);\n' +
     '    opponents_.push_back(o.get());\n' +
     '    rebuildOppIndex();\n' +
     '    actors_.push_back(std::move(o));\n' +
     '    if (mp.hasSelectedCharacter) chassisAssets(resolveChassis(mp.selection, match_.faction(p)));   // under the match load\n' +
     '    if (remoteIdle_.size() <= (size_t)p) remoteIdle_.resize((size_t)p + 1, 0.0f);\n' +
     '    remoteIdle_[(size_t)p] = 0.0f;\n' +
     '    LOG_INFO("session: p%d %s team %d is Remote (member %s)", p, r.name.c_str(), mp.team, r.member.c_str());\n' +
     '    return p;\n' +
     '}\n\n' +
     'bool World::applySlotSwap(const SlotSwap& s) {\n' +
     '    int p = s.matchPlayer;\n' +
     '    if (p < 0 && !s.member.empty())\n' +
     '        for (size_t i = 0; i < match_.players().size(); ++i) if (match_.players()[i].sessionMember == s.member) { p = (int)i; break; }\n' +
     '    if (p < 0 || (size_t)p >= match_.players().size() || p == localPlayer_) return false;\n' +
     '    const ParticipantKind k = match_.players()[(size_t)p].kind;\n' +
     '    if (s.toBot ? k != ParticipantKind::Remote : (k != ParticipantKind::Bot || s.member.empty())) return false;\n' +
     '    SlotSwap q = s; q.matchPlayer = p;\n' +
     '    if (q.atTick <= 0) q.atTick = simTick_ + 1;\n' +
     '    pendingSwaps_.push_back(q);\n' +
     '    return true;\n' +
     '}\n\n' +
     'void World::noteRemoteCommand(int matchPlayer) {\n' +
     '    if (matchPlayer >= 0 && (size_t)matchPlayer < remoteIdle_.size()) remoteIdle_[(size_t)matchPlayer] = 0.0f;\n' +
     '}\n\n' +
     'void World::tickSlotSwaps(float dt) {\n' +
     '    if (!matchActive_) return;\n' +
     '    ++simTick_;\n' +
     '    // Host idle rule: a Remote participant without commands for kRemoteIdleKickSeconds of play is handed to a bot.\n' +
     '    if (match_.state() == Match::State::InProgress)\n' +
     '        for (size_t i = 0; i < match_.players().size(); ++i) {\n' +
     '            const MatchPlayer& mp = match_.players()[i];\n' +
     '            if (mp.kind != ParticipantKind::Remote || mp.sessionMember.empty()) continue;\n' +
     '            if (remoteIdle_.size() <= i) remoteIdle_.resize(i + 1, 0.0f);\n' +
     '            remoteIdle_[i] += dt;\n' +
     '            if (remoteIdle_[i] < kRemoteIdleKickSeconds) continue;\n' +
     '            remoteIdle_[i] = -1e9f;   // queued once\n' +
     '            SlotSwap s; s.matchPlayer = (int)i; s.toBot = true; s.atTick = simTick_;\n' +
     '            pendingSwaps_.push_back(s);\n' +
     '            match_.notifyRemoteIdle((int)i);   // MatchEvent RemoteIdle: the host\'s session glue kicks that peer ("Kicked for idling")\n' +
     '            ++remoteIdleKicks_;\n' +
     '            LOG_INFO("session: p%zu idle %.0f s: kicked, the slot becomes a bot", i, kRemoteIdleKickSeconds);\n' +
     '        }\n' +
     '    for (size_t k = 0; k < pendingSwaps_.size();) {\n' +
     '        if (pendingSwaps_[k].atTick > simTick_) { ++k; continue; }\n' +
     '        const SlotSwap s = pendingSwaps_[k];\n' +
     '        pendingSwaps_.erase(pendingSwaps_.begin() + (long)k);\n' +
     '        if (s.toBot) swapSlotToBot(s.matchPlayer); else swapSlotToRemote(s.matchPlayer, s.member);\n' +
     '    }\n' +
     '}\n\n' +
     'void World::swapSlotToBot(int p) {\n' +
     '    if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Remote) return;\n' +
     '    {\n' +
     '        MatchPlayer& mp = match_.playerMutable(p);\n' +
     '        mp.kind = ParticipantKind::Bot;\n' +
     '        mp.sessionMember.clear();\n' +
     '    }\n' +
     '    if (!match_.players()[(size_t)p].hasSelectedCharacter) {   // never selected: the Soldier class preset (bots play class presets)\n' +
     '        CharacterSelection cs; cs.type = 0; cs.specialty = Specialty::Soldier;\n' +
     '        match_.selectCharacter(p, cs);\n' +
     '    }\n' +
     '    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) o->pressesPickup = true;\n' +
     '    ensureBotNav();\n' +
     '    addBotBrain(p, botDifficulty_);\n' +
     '    {   // the bot AI: WFC_BOTAI (DEV / TEST) over the default (Smart) [PROVISIONAL: the lobby\'s per-faction AI row is not kept per match]\n' +
     '        static const char* envAi = std::getenv("WFC_BOTAI");\n' +
     '        const BotAi ai = botAiFromString(envAi, BotAi::Smart);\n' +
     '        if (smart_.size() <= (size_t)p) smart_.resize((size_t)p + 1);\n' +
     '        smart_[(size_t)p] = SmartBot{};\n' +
     '        smart_[(size_t)p].active = ai == BotAi::Smart;\n' +
     '        smart_[(size_t)p].rng = 0x51a7b07U ^ ((unsigned)p * 2654435761U);\n' +
     '        anySmart_ |= ai == BotAi::Smart;\n' +
     '        if (anySmart_) ensureSmartTactics();\n' +
     '    }\n' +
     '    if ((size_t)p < remoteIdle_.size()) remoteIdle_[(size_t)p] = 0.0f;\n' +
     '    LOG_INFO("session: p%d Remote -> Bot at tick %lld (slot, score row and team kept)", p, (long long)simTick_);\n' +
     '}\n\n' +
     'void World::swapSlotToRemote(int p, const std::string& member) {\n' +
     '    if (p < 0 || (size_t)p >= match_.players().size() || match_.players()[(size_t)p].kind != ParticipantKind::Bot) return;\n' +
     '    {\n' +
     '        MatchPlayer& mp = match_.playerMutable(p);\n' +
     '        mp.kind = ParticipantKind::Remote;\n' +
     '        mp.sessionMember = member;\n' +
     '    }\n' +
     '    bots_.erase(std::remove_if(bots_.begin(), bots_.end(), [p](const BotBrain& b) { return b.player == p; }), bots_.end());\n' +
     '    if ((size_t)p < smart_.size()) smart_[(size_t)p].active = false;\n' +
     '    for (MatchOpponent* o : opponents_) if (o->matchPlayer() == p) o->pressesPickup = false;\n' +
     '    if (remoteIdle_.size() <= (size_t)p) remoteIdle_.resize((size_t)p + 1, 0.0f);\n' +
     '    remoteIdle_[(size_t)p] = 0.0f;\n' +
     '    LOG_INFO("session: p%d Bot -> Remote (member %s) at tick %lld (slot, score row and team kept)", p, member.c_str(), (long long)simTick_);\n' +
     '}\n\n' +
     'MatchOpponent* World::addMatchOpponent(const std::string& name, bool drawn) {\n'],
]);
ed('src/core/Application.h', [[
    '    void runEventTest();\n',
    '    void runEventTest();\n    void runSessionTest();\n'
]]);
ed('src/core/Application_Frontend.cpp', [[
    '"WFC_EVENTTEST", ', '"WFC_EVENTTEST", "WFC_SESSIONTEST", '
]]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }',
     '    if (std::getenv("WFC_SESSIONTEST")) { runSessionTest(); return false; }          // online session match side (Remote / swaps)\n' +
     '    if (std::getenv("WFC_EVENTTEST")) { runEventTest(); return false; }'],
    ['// WFC_PACINGTEST: presented camera',
     '// WFC_SESSIONTEST (docs/handoff/ONLINE_MP_FRONTEND_TESTS.md section 4): the match side of online sessions over a scripted TDM match.\n' +
     '// Remote participants are fed scripted commands (noteRemoteCommand each step; no network). 1) Remote spawn / kill / death records\n' +
     '// and scoring, 2) Remote -> Bot at an agreed tick, 3) Bot -> Remote (late join), 4) the 180 s idle kick; slot continuity and serials.\n' +
     'void Application::runSessionTest() {\n' +
     '    int checks = 0, fails = 0;\n' +
     '    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("SESSIONTEST %s %s", ok ? "PASS" : "FAIL", what.c_str()); };\n' +
     '    const float dt = 1.0f / 60.0f;\n' +
     '    platform::InputFrame idle;\n' +
     '    std::vector<int> fed;   // Remote participants that send commands\n' +
     '    auto run = [&](float secs) {\n' +
     '        for (int i = 0; i < (int)(secs * 60.0f + 0.5f); ++i) { for (int p : fed) world_.noteRemoteCommand(p); world_.handleInput(idle, dt); world_.tick(dt); }\n' +
     '    };\n' +
     '    using T = game::GameplayEventType;\n' +
     '    game::MatchLaunch L; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM", L);\n' +
     '    L.localTeam = 0;\n' +
     '    auto seat = [](const char* name, int team, const char* member) {\n' +
     '        game::RemoteSeat r; r.name = name; r.team = team; r.member = member; r.selected = true;\n' +
     '        r.selection.type = 0; r.selection.specialty = game::Specialty::Soldier; r.selection.weapons = {"AssaultRifle", "HomingRocket", "FlakGrenades"};\n' +
     '        return r;\n' +
     '    };\n' +
     '    L.remotes = {seat("Remote1", 1, "WFC-TEST-0001"), seat("Remote2", 0, "WFC-TEST-0002"), seat("Remote3", 1, "WFC-TEST-0003")};\n' +
     '    world_.launchMatch(L);\n' +
     '    const int me = world_.localMatchPlayer();\n' +
     '    game::CharacterSelection cs; cs.type = 0; cs.specialty = game::Specialty::Soldier; cs.weapons = {"AssaultRifle", "HomingRocket", "FlakGrenades"};\n' +
     '    world_.match().selectCharacter(me, cs);\n' +
     '    auto byMember = [&](const char* m) { const auto& ps = world_.match().players(); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].sessionMember == m) return (int)i; return -1; };\n' +
     '    const int r1 = byMember("WFC-TEST-0001"), r2 = byMember("WFC-TEST-0002"), r3 = byMember("WFC-TEST-0003");\n' +
     '    check(r1 >= 0 && r2 >= 0 && r3 >= 0, "three Remote participants from MatchLaunch.remotes");\n' +
     '    if (r1 < 0 || r2 < 0 || r3 < 0) { LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks); return; }\n' +
     '    fed = {r1, r2};   // r3 sends nothing (the idle check)\n' +
     '    run(11.0f);\n' +
     '    const auto& P = [&](int p) -> const game::MatchPlayer& { return world_.match().players()[(size_t)p]; };\n' +
     '    check(P(r1).kind == game::ParticipantKind::Remote && world_.participantPawn(r1) && world_.participantPawn(r2), "Remote participants spawned (kind Remote)");\n' +
     '    // 1) a kill on a Remote participant scores; a Remote participant kills and scores.\n' +
     '    const int myKills0 = P(me).kills;\n' +
     '    if (world_.participantPawn(r1)) world_.applyMatchDamage(r1, me, 99999.0f, false, "TransGame.TnDamageTypeAssaultRifle");\n' +
     '    run(0.2f);\n' +
     '    run(7.0f);\n' +
     '    if (world_.participantPawn(r1) && !world_.localPlayerDead()) world_.applyMatchDamage(me, r1, 99999.0f, false, "TransGame.TnDamageTypeAssaultRifle");\n' +
     '    run(7.0f);\n' +
     '    int killOnRemote = 0, killByRemote = 0, remoteSpawns = 0;\n' +
     '    for (const auto& e : world_.match().gameplayEvents()) {\n' +
     '        if (e.type == T::Kill && e.victim == r1 && e.victimState.kind == game::ParticipantKind::Remote) ++killOnRemote;\n' +
     '        if (e.type == T::Kill && e.instigator == r1 && e.instigatorState.kind == game::ParticipantKind::Remote) ++killByRemote;\n' +
     '        if (e.type == T::Spawn && e.instigator == r1 && e.instigatorState.kind == game::ParticipantKind::Remote) ++remoteSpawns;\n' +
     '    }\n' +
     '    check(killOnRemote == 1 && P(me).kills == myKills0 + 1, "a kill on a Remote participant: a Kill record with victim kind Remote, and it scores");\n' +
     '    check(killByRemote == 1 && P(r1).kills == 1 && P(r1).score > 0, "a Remote participant\'s kill: a Kill record with instigator kind Remote, and it scores");\n' +
     '    check(remoteSpawns >= 2, "Spawn records with kind Remote (initial + respawn)");\n' +
     '    // 2) Remote -> Bot at an agreed tick: the slot, score row and team stay.\n' +
     '    const game::MatchPlayer before = P(r1);\n' +
     '    const int64_t at = world_.simTick() + 60;\n' +
     '    game::SlotSwap sw; sw.matchPlayer = -1; sw.member = "WFC-TEST-0001"; sw.toBot = true; sw.atTick = at;\n' +
     '    check(world_.applySlotSwap(sw), "swap to Bot accepted (found by member)");\n' +
     '    fed = {r2};   // the leaving peer sends nothing more\n' +
     '    run(0.5f);\n' +
     '    check(P(r1).kind == game::ParticipantKind::Remote, "not before the agreed tick");\n' +
     '    run(1.0f);\n' +
     '    check(P(r1).kind == game::ParticipantKind::Bot && world_.botBrain(r1) != nullptr && P(r1).sessionMember.empty(), "Remote -> Bot at the agreed tick (bot brain, member cleared)");\n' +
     '    check(P(r1).team == before.team && P(r1).name == before.name && P(r1).kills >= before.kills && P(r1).deaths >= before.deaths && P(r1).score >= before.score,\n' +
     '          "the score row and team stay (same index)");\n' +
     '    run(8.0f);\n' +
     '    // 3) Bot -> Remote (late join) at the next tick.\n' +
     '    game::SlotSwap sj; sj.matchPlayer = r1; sj.toBot = false; sj.member = "WFC-TEST-0004"; sj.atTick = 0;\n' +
     '    check(world_.applySlotSwap(sj), "swap to Remote accepted");\n' +
     '    run(0.1f);\n' +
     '    fed = {r1, r2};\n' +
     '    check(P(r1).kind == game::ParticipantKind::Remote && P(r1).sessionMember == "WFC-TEST-0004" && world_.botBrain(r1) == nullptr && P(r1).team == before.team,\n' +
     '          "Bot -> Remote: kind, member, no bot brain, team kept");\n' +
     '    // 4) idle: r3 never sent a command -> kicked at 180 s of play, its slot a Bot; the fed ones stay Remote.\n' +
     '    run(185.0f);\n' +
     '    check(P(r3).kind == game::ParticipantKind::Bot && world_.botBrain(r3) != nullptr, "idle 180 s: the Remote slot becomes a Bot");\n' +
     '    check(world_.remoteIdleKicks() == 1, "the idle rule fired once (MatchEvent RemoteIdle for the session kick)");\n' +
     '    check(P(r1).kind == game::ParticipantKind::Remote && P(r2).kind == game::ParticipantKind::Remote, "participants that send commands stay Remote");\n' +
     '    bool serialsOk = true; uint32_t prev = 0;\n' +
     '    for (const auto& e : world_.match().gameplayEvents()) { if (e.serial <= prev) serialsOk = false; prev = e.serial; }\n' +
     '    check(serialsOk, "event serials unique and increasing across the swaps");\n' +
     '    int myKillRecords = 0;\n' +
     '    for (const auto& e : world_.match().gameplayEvents()) myKillRecords += e.type == T::Kill && e.instigator == me;\n' +
     '    check(myKillRecords == P(me).kills, "kill records equal the participant stats");\n' +
     '    // Iconic Mode: a second claim of the same iconic character is refused, the previous selection stays.\n' +
     '    {\n' +
     '        game::MatchLaunch I; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?IconicMode=1", I);\n' +
     '        I.localTeam = 0;\n' +
     '        I.remotes = {seat("Remote1", 1, "WFC-TEST-0001")};\n' +
     '        world_.launchMatch(I);\n' +
     '        const int me2 = world_.localMatchPlayer(), q = byMember("WFC-TEST-0001");\n' +
     '        game::CharacterSelection bee; bee.type = 1; bee.chassisId = "Car";\n' +
     '        const bool first = world_.match().selectCharacter(me2, bee);\n' +
     '        const bool second = q >= 0 && world_.match().selectCharacter(q, bee);\n' +
     '        check(I.settings.iconicMode && first && !second && q >= 0 && world_.match().players()[(size_t)q].selection.type == 0,\n' +
     '              "Iconic Mode: one holder per match (a second claim is refused, the previous selection stays)");\n' +
     '    }\n' +
     '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n' +
     '}\n\n' +
     '// WFC_PACINGTEST: presented camera'],
]);
console.log('ok');
