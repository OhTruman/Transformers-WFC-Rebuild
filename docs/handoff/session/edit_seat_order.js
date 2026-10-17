// Online determinism (Systems' catch, Integration 2026-10-17): one GLOBAL human seat order on every peer.
// - MatchLaunch.humans (every human in the host's seat order, this machine's included) + localSeat. Online launches (humans non-empty):
//   every participant is cleared, the local player is added (one team-tie draw, the same on every peer), the other humans join in seat order
//   with their exact teams (no tie draw), and the match players are permuted so match player i = seat i on every peer; bots after.
//   A launch after a seat-ordered one clears every participant too. Offline (no humans): unchanged.
// - simStateHash goes by match player index (it hashed the local pawn first: a different order per perspective).
// - WFC_SESSIONTEST: the cross-perspective identity: A = local seat 0, B = local seat 1, the same per-seat command streams -> equal state.
// Run from a tree root (integration 53548ac3+ with GAMEPLAY_SEAT_FOLLOWUP).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/MatchOpponent.h', [[
    '    int matchPlayer() const { return player_; }\n',
    '    int matchPlayer() const { return player_; }\n' +
    '    void setMatchPlayer(int p) { player_ = p; }   // the online seat order permutation at launch\n'
]]);
ed('src/game/Match.h', [[
    '    void truncatePlayers(size_t n) {\n',
    '    // Online: the Match\'s own RNG (team ties, start choices) restarts from the host\'s seed at every launch, so every peer draws alike\n' +
    '    // whatever matches its process played before (offline: never reseeded, as before).\n' +
    '    void seedRandom(unsigned s) { rng_ = s ? s : 0x1234ABCDu; }\n' +
    '    // The login start without advancing the clusters\' round-robin (online: only this machine\'s player logs in locally, so a\n' +
    '    // mutating login would advance a different team\'s iterator per perspective).\n' +
    '    int loginStartPeek(int p) {\n' +
    '        std::vector<int> it; it.reserve(clusters_.size());\n' +
    '        for (const Cluster& c : clusters_) it.push_back(c.iterator);\n' +
    '        const int r = loginStart(p);\n' +
    '        for (size_t i = 0; i < clusters_.size(); ++i) clusters_[i].iterator = it[i];\n' +
    '        return r;\n' +
    '    }\n' +
    '    // A participant with its exact team (no TnTeamHandler.PickTeam tie draw): the online seats, so every peer draws alike.\n' +
    '    int addPlayerExact(const std::string& name, int team) {\n' +
    '        MatchPlayer p; p.name = name; p.team = team; p.hasSelectedCharacter = true;\n' +
    '        players_.push_back(p); locs_.push_back({0, 0, 0}); spawnAt_.push_back(-1); damageHistory_.emplace_back(); deathTime_.push_back(-1.0f);\n' +
    '        return (int)players_.size() - 1;\n' +
    '    }\n' +
    '    // Permute the participants (before any spawn / event refers to them): new player i = old player order[i].\n' +
    '    void reorderPlayers(const std::vector<int>& order) {\n' +
    '        if (order.size() != players_.size()) return;\n' +
    '        std::vector<int> newOf(order.size());\n' +
    '        for (size_t i = 0; i < order.size(); ++i) newOf[(size_t)order[i]] = (int)i;\n' +
    '        auto perm = [&](auto& v) { if (v.size() != order.size()) return; auto c = v; for (size_t i = 0; i < order.size(); ++i) v[i] = c[(size_t)order[i]]; };\n' +
    '        perm(players_); perm(locs_); perm(radii_); perm(lastDamagedAt_); perm(lastHitBy_); perm(spawnAt_); perm(damageHistory_); perm(deathTime_);\n' +
    '        for (int& h : lastHitBy_) if (h >= 0 && (size_t)h < newOf.size()) h = newOf[(size_t)h];\n' +
    '    }\n' +
    '    void truncatePlayers(size_t n) {\n'
]]);
ed('src/game/World.h', [
    ['    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n',
     '    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n' +
     '    std::vector<RemoteSeat> humans;    // online: EVERY human in the host\'s seat order (this machine\'s included); empty offline\n' +
     '    int localSeat = -1;                // online: this machine\'s index in humans (match player localSeat on every peer)\n'],
    ['    int addRemoteParticipant(const RemoteSeat& r);\n',
     '    int addRemoteParticipant(const RemoteSeat& r, bool exactTeam = false);\n'],
    ['    bool onlineSeats_ = false;          // online: the local seat through the wire path (MatchLaunch.online, or the test override)\n',
     '    bool onlineSeats_ = false;          // online: the local seat through the wire path (MatchLaunch.online, or the test override)\n' +
     '    bool seatOrdered_ = false;          // the running match numbers its humans by the host\'s seat order (the next launch clears all)\n' +
     '    void clearAllParticipants();\n'],
]);
ed('src/game/World.cpp', [
    ['int World::addRemoteParticipant(const RemoteSeat& r) {\n    const int p = match_.addPlayer(r.name, r.team);\n',
     'int World::addRemoteParticipant(const RemoteSeat& r, bool exactTeam) {\n    const int p = exactTeam ? match_.addPlayerExact(r.name, r.team) : match_.addPlayer(r.name, r.team);\n'],
    ['        hostSeed_ = l.seed;   // online: the host\'s seed replaces the local source on every peer\n',
     '        hostSeed_ = l.seed;   // online: the host\'s seed replaces the local source on every peer\n' +
     '        if (l.online && l.seed) match_.seedRandom(l.seed * 2246822519u + 3u);   // the Match RNG too (it never restarted between matches)\n'],
    ['    removeBots();   // the previous match\'s bots leave with it (a new match is a fresh level in the original)\n',
     '    removeBots();   // the previous match\'s bots leave with it (a new match is a fresh level in the original)\n' +
     '    if (seatOrdered_ || !l.humans.empty()) clearAllParticipants();   // online seat order: every peer builds the same numbering from scratch\n' +
     '    seatOrdered_ = !l.humans.empty();\n'],
    ['    for (const RemoteSeat& r : l.remotes) addRemoteParticipant(r);   // online: the other peers\' humans (before the bot fill counts them)\n',
     '    if (!l.humans.empty()) {\n' +
     '        // Online: every human in the host\'s seat order on every peer. The local player was added first (startLocalMatch); the others\n' +
     '        // join in seat order with their exact teams, then the participants are permuted so match player i = seat i.\n' +
     '        const int ls = std::clamp(l.localSeat, 0, (int)l.humans.size() - 1);\n' +
     '        {\n' +
     '            MatchPlayer& me = match_.playerMutable(localPlayer_);\n' +
     '            me.name = l.humans[(size_t)ls].name; me.sessionMember = l.humans[(size_t)ls].member;\n' +
     '            if (match_.settings().teamGame) me.team = l.humans[(size_t)ls].team;\n' +
     '        }\n' +
     '        std::vector<int> order(l.humans.size(), -1);\n' +
     '        order[(size_t)ls] = localPlayer_;\n' +
     '        for (size_t i = 0; i < l.humans.size(); ++i) if ((int)i != ls) order[i] = addRemoteParticipant(l.humans[i], true);\n' +
     '        match_.reorderPlayers(order);\n' +
     '        std::vector<int> newOf(order.size());\n' +
     '        for (size_t i = 0; i < order.size(); ++i) newOf[(size_t)order[i]] = (int)i;\n' +
     '        localPlayer_ = newOf[(size_t)localPlayer_];\n' +
     '        for (MatchOpponent* o : opponents_) o->setMatchPlayer(newOf[(size_t)o->matchPlayer()]);\n' +
     '        rebuildOppIndex();\n' +
     '        remoteIdle_.assign(match_.players().size(), 0.0f);\n' +
     '        LOG_INFO("session: %zu humans in the host\'s seat order, this machine at seat %d", l.humans.size(), ls);\n' +
     '    } else\n' +
     '        for (const RemoteSeat& r : l.remotes) addRemoteParticipant(r);   // online: the other peers\' humans (before the bot fill counts them)\n'],
    // The bot fill's "friendly" team: online, seat 0's team on every peer (it was the local player's: a different fill per perspective).
    ['    if (match_.settings().teamGame && (b.autobot >= 0 || b.decepticon >= 0) && localPlayer_ >= 0) {\n' +
     '        const bool humanAutobot = match_.players()[(size_t)localPlayer_].team != 1;\n',
     '    const int refPlayer = seatOrdered_ ? 0 : localPlayer_;   // online: seat 0 (the same reference on every peer)\n' +
     '    if (match_.settings().teamGame && (b.autobot >= 0 || b.decepticon >= 0) && refPlayer >= 0) {\n' +
     '        const bool humanAutobot = match_.players()[(size_t)refPlayer].team != 1;\n'],
    ['    const int humanTeam = localPlayer_ >= 0 ? match_.players()[(size_t)localPlayer_].team : 0;\n',
     '    const int humanTeam = refPlayer >= 0 ? match_.players()[(size_t)refPlayer].team : 0;\n'],
    ['    int ls = match_.loginStart(localPlayer_);\n',
     '    int ls = seatOrdered_ ? match_.loginStartPeek(localPlayer_) : match_.loginStart(localPlayer_);   // online: no round-robin advance\n'],
    ['uint64_t World::simStateHash() const {\n',
     'void World::clearAllParticipants() {\n' +
     '    // Every participant leaves (the online seat order rebuilds the numbering identically on every peer).\n' +
     '    for (MatchOpponent* o : opponents_) o->destroy();\n' +
     '    opponents_.clear();\n' +
     '    rebuildOppIndex();\n' +
     '    for (size_t i = 0; i < actors_.size();) { if (!actors_[i]->alive()) { actors_[i] = std::move(actors_.back()); actors_.pop_back(); } else ++i; }\n' +
     '    match_.truncatePlayers(0);\n' +
     '    localPlayer_ = -1;\n' +
     '    bots_.clear(); smart_.clear(); anySmart_ = false; remoteIdle_.clear();\n' +
     '}\n\n' +
     'uint64_t World::simStateHash() const {\n'],
    ['    if (!localDead_) pawn(player_.pawn());\n    for (const MatchOpponent* o : opponents_) if (o->spawned()) pawn(o->pawn());\n',
     '    for (size_t i = 0; i < match_.players().size(); ++i) if (const Character* c = participantPawn((int)i)) pawn(*c);   // by match player index\n' +
     '    // The bots\' decision state (their RNGs, targets, aim): a divergence shows here long before it moves a pawn.\n' +
     '    for (const BotBrain& b : bots_) { mix(&b.player, sizeof b.player); mix(&b.rng, sizeof b.rng); mix(&b.target, sizeof b.target); mix(&b.yaw, sizeof b.yaw); }\n' +
     '    for (const SmartBot& sb : smart_) mix(&sb.rng, sizeof sb.rng);\n'],
]);
ed('src/core/Application_Frontend.cpp', [[
    'void fillOnlineLaunch(game::MatchLaunch& gl, const frontend::MatchLaunch& m) {\n',
    'void fillOnlineLaunch(game::MatchLaunch& gl, const frontend::MatchLaunch& m) {\n' +
    '    // Every human in the host\'s seat order, this machine\'s included (game::MatchLaunch::humans + localSeat): one numbering on every peer.\n' +
    '    for (const frontend::SessionSlot& sl : m.onlineSlots) {\n' +
    '        if (sl.kind != frontend::SessionSlot::Kind::Human) continue;\n' +
    '        game::RemoteSeat h;\n' +
    '        h.team = sl.team;\n' +
    '        h.member = sl.member.fingerprint;\n' +
    '        for (const auto& n : m.onlineNames) if (n.first == sl.member) h.name = n.second;\n' +
    '        if (h.name.empty()) h.name = "Player";\n' +
    '        if (sl.member == m.self) gl.localSeat = (int)gl.humans.size();\n' +
    '        gl.humans.push_back(h);\n' +
    '    }\n' +
    '    if (!gl.humans.empty()) {   // the seat order replaces the remotes list\n' +
    '        gl.seed = m.seed; gl.online = true;\n' +
    '        frontend::FlowTrace::emit("online.launchHumans", {{"humans", std::to_string(gl.humans.size())}, {"localSeat", std::to_string(gl.localSeat)}});\n' +
    '        return;\n' +
    '    }\n'
]]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    // The cross-perspective identity (Systems): A = this machine at seat 0, B = at seat 1; the same per-seat command streams and seed.\n' +
    '    {\n' +
    '        auto seatCmd = [](int seat, int i, int64_t tick) {\n' +
    '            game::RemoteCommand c; c.tick = (uint32_t)tick;\n' +
    '            using B = platform::Button;\n' +
    '            auto at = [&](int k, B b) { if (i == k) c.pressed |= 1u << (int)b; };\n' +
    '            auto hold = [&](int a, int z, B b) { if (i >= a && i < z) c.buttons |= 1u << (int)b; };\n' +
    '            c.aimPitch = 0.2f * std::sin((float)i * 0.01f * (seat ? 1.3f : 1.0f));\n' +
    '            if (seat == 0) { c.moveForward = ((i / 150) % 2) ? 127 : 40; c.aimYaw = std::remainder((float)(i % 2048) * (6.2831853f / 2048.0f), 6.2831853f);\n' +
    '                             hold(600, 760, B::Fire); at(500, B::Jump); at(650, B::Dash); at(800, B::Transform); hold(900, 1000, B::Fire);\n' +
    '                             at(1100, B::Transform); at(1300, B::Grenade); hold(1400, 1450, B::Fire); }\n' +
    '            else { c.moveRight = ((i / 100) % 2) ? 127 : -127; c.moveForward = 60; c.aimYaw = std::remainder(3.0f - (float)(i % 4096) * (6.2831853f / 4096.0f), 6.2831853f);\n' +
    '                   hold(400, 520, B::Fire); at(700, B::Melee); at(620, B::Ability1); at(900, B::Transform); at(1000, B::Dash); hold(1020, 1080, B::Fire);\n' +
    '                   at(1200, B::Transform); at(1350, B::NextWeapon); hold(1420, 1500, B::Fire); }\n' +
    '            return c;\n' +
    '        };\n' +
    '        uint64_t hs[2] = {0, 0}; int seatOk = 0;\n' +
    '        std::vector<uint64_t> per[2]; std::vector<std::string> st[2];   // per-step hashes and seat states (the first divergence)\n' +
    '        for (int ls = 0; ls < 2; ++ls) {\n' +
    '            game::MatchLaunch S; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?BotsFriendly=1?BotsEnemy=1", S);\n' +
    '            S.seed = 0x5eedab01u; S.online = true; S.localTeam = ls == 0 ? 0 : 1;\n' +
    '            S.humans = {seat("Seat0", 0, "WFC-AB-0"), seat("Seat1", 1, "WFC-AB-1")}; S.localSeat = ls;\n' +
    '            world_.launchMatch(S);\n' +
    '            world_.match().selectCharacter(world_.localMatchPlayer(), S.humans[(size_t)ls].selection);\n' +
    '            seatOk += world_.localMatchPlayer() == ls && world_.match().players()[0].sessionMember == "WFC-AB-0" && world_.match().players()[1].sessionMember == "WFC-AB-1";\n' +
    '            for (int i = 0; i < 1600; ++i) {\n' +
    '                for (int st = 0; st < 2; ++st) world_.applyRemoteCommand(st, seatCmd(st, i, world_.simTick() + 1));   // ascending match player\n' +
    '                world_.handleInput(idle, dt); world_.tick(dt);\n' +
    '                per[ls].push_back(world_.simStateHash());\n' +
    '                std::string row;\n' +
    '                for (size_t pp = 0; pp < world_.match().players().size(); ++pp) {\n' +
    '                    const game::Character* c = world_.participantPawn((int)pp); char b[320];\n' +
    '                    if (c) std::snprintf(b, sizeof b, " p%zu (%.4f %.4f %.4f) v(%.5f %.5f %.5f) y%.4f h%.3f", pp, c->position().x, c->position().y, c->position().z,\n' +
    '                                         c->velocity().x, c->velocity().y, c->velocity().z, c->yaw(), c->health().current);\n' +
    '                    else std::snprintf(b, sizeof b, " p%zu -", pp);\n' +
    '                    row += b;\n' +
    '                }\n' +
    '                { char b[48]; std::snprintf(b, sizeof b, " rand %08x", core::simRandState()); row += b; }\n' +
    '                st[ls].push_back(row);\n' +
    '            }\n' +
    '            hs[ls] = world_.simStateHash();\n' +
    '        }\n' +
    '        for (size_t i = 0; i < per[0].size() && i < per[1].size(); ++i)\n' +
    '            if (per[0][i] != per[1][i]) {\n' +
    '                LOG_INFO("SESSIONTEST cross-perspective: first divergent step %zu", i);\n' +
    '                LOG_INFO("SESSIONTEST   A:%s", st[0][i].c_str());\n' +
    '                LOG_INFO("SESSIONTEST   B:%s", st[1][i].c_str());\n' +
    '                if (i > 0) { LOG_INFO("SESSIONTEST   A prev:%s", st[0][i - 1].c_str()); LOG_INFO("SESSIONTEST   B prev:%s", st[1][i - 1].c_str()); }\n' +
    '                break;\n' +
    '            }\n' +
    '        LOG_INFO("SESSIONTEST cross-perspective hashes %016llx %016llx", (unsigned long long)hs[0], (unsigned long long)hs[1]);\n' +
    '        check(seatOk == 2, "seat order: match player i = seat i on both perspectives (local at seat 0 / seat 1)");\n' +
    '        check(hs[0] == hs[1] && hs[0] != 0, "cross-perspective identity: local at seat 0 vs seat 1, the same seat streams -> equal state at step 1600");\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
