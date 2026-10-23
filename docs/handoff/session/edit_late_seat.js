// Join in progress (public games; the original: a joiner spawns at once, mid-match, on the smaller team), agreed with Systems
// 2026-10-23: dormant reserved seats. The launch's humans list carries RemoteSeat::dormant entries after the real humans (seat i =
// match player i on every peer); a dormant participant has no team, never spawns, is not scored / counted / listed. Systems' driver
// calls queueLateSeat(seat, r, E) on every peer before step E: the seat wakes (name, member, the host's team, no character, so
// Choose Character, then an immediate spawn); a team full with bot fill retires its last bot at the same tick (the joiner takes a
// bot's place). The joiner launches with localSeat = its dormant seat and spectates until woken. seatPlayer / smallerTeam for the
// driver and the host. WFC_LATESEATTEST. Run from a tree root; argv[2] = the Gameplay worktree.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
fs.copyFileSync(path.join(G, 'docs/handoff/session/Application_LateSeatTest.cpp'), 'src/core/Application_LateSeatTest.cpp');

ed('src/game/Match.h', [
    ['    int currentKillStreak = 0;\n',
     '    int currentKillStreak = 0;\n' +
     '    bool dormant = false;              // online: a reserved seat no one holds yet (join in progress): no team, no spawn, not counted\n'],
    ['    void requireCharacterSelection(int p) {',
     '    // Join in progress: a dormant seat wakes (name, member, team; no character: Choose Character, then an immediate spawn while\n' +
     '    // InProgress) / a bot leaves for a joiner (despawned by the host first; dormant from here on).\n' +
     '    void wakePlayer(int p, const std::string& name, const std::string& member, int team);\n' +
     '    void retirePlayer(int p);\n' +
     '    void requireCharacterSelection(int p) {'],
]);
ed('src/game/Match.cpp', [
    ['    MatchPlayer& P = players_[(size_t)p];\n    if (!P.hasSelectedCharacter) { P.timeToRespawn = 0.1f; return; }',
     '    MatchPlayer& P = players_[(size_t)p];\n' +
     '    if (P.dormant) { P.alive = false; P.timeToRespawn = -1.0f; return; }   // a reserved seat: nobody to spawn\n' +
     '    if (!P.hasSelectedCharacter) { P.timeToRespawn = 0.1f; return; }'],
    ['bool Match::spectating(int p) const {\n',
     'bool Match::spectating(int p) const {\n    if (p >= 0 && (size_t)p < players_.size() && players_[(size_t)p].dormant) return true;   // a joiner before its wake\n'],
    ['void Match::restartPlayerAt(', // placed before the director helper from E1
     'void Match::wakePlayer(int p, const std::string& name, const std::string& member, int team) {\n' +
     '    if (p < 0 || (size_t)p >= players_.size()) return;\n' +
     '    MatchPlayer& P = players_[(size_t)p];\n' +
     '    P.dormant = false; P.name = name; P.sessionMember = member; P.team = team; P.alive = false;\n' +
     '    P.hasSelectedCharacter = false;   // Choose Character first\n' +
     '    // The joiner spawns at once (TnRespawnHelperWave TimeToAllowInstantInitialSpawns -1); Escalation (TnRespawnHelperRoundEnd): at once\n' +
     '    // only within the first 30 s of the match, else at the end of the current wave; MatchOver / pending: no spawn here [RE join-in-progress].\n' +
     '    P.timeToRespawn = state_ == State::InProgress && (!s_.survival || matchTime_ <= 30.0f) ? 0.0f : -1.0f;\n' +
     '    LOG_INFO("MATCH join player=%d name=%s team=%d t=%.2f", p, name.c_str(), team, matchTime_);   // audit line\n' +
     '}\n\n' +
     'void Match::retirePlayer(int p) {\n' +
     '    if (p < 0 || (size_t)p >= players_.size()) return;\n' +
     '    MatchPlayer& P = players_[(size_t)p];\n' +
     '    LOG_INFO("MATCH leave player=%d name=%s team=%d t=%.2f (a bot makes room for a joiner)", p, P.name.c_str(), P.team, matchTime_);\n' +
     '    P.alive = false; P.dormant = true; P.team = 255; P.timeToRespawn = -1.0f;\n' +
     '}\n\n' +
     'void Match::restartPlayerAt('],
]);
ed('src/game/World.h', [
    ['struct RemoteSeat { std::string name; int team = 255; std::string member; CharacterSelection selection; bool selected = false; };',
     'struct RemoteSeat { std::string name; int team = 255; std::string member; CharacterSelection selection; bool selected = false;\n' +
     '                    bool dormant = false; };   // dormant: a reserved seat for join in progress (no team / spawn until queueLateSeat)'],
    ['    void queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick);\n',
     '    void queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick);\n' +
     '    // Join in progress (Systems\' driver, every peer, before the step of atTick): the dormant seat wakes as r (name, member, the host\'s\n' +
     '    // team); a team full with bot fill retires its last bot at the same tick. seatPlayer: the match player of a launch seat (seat i =\n' +
     '    // player i; -1 past the launch\'s humans). smallerTeam (host): fewer non-dormant participants (bots included); a tie or both full:\n' +
     '    // fewer humans; then team 0. No RNG.\n' +
     '    void queueLateSeat(int seat, const RemoteSeat& r, uint32_t atTick);\n' +
     '    int seatPlayer(int seat) const { return seat >= 0 && seat < launchHumans_ ? seat : -1; }\n' +
     '    int smallerTeam() const;\n'],
    ['    std::vector<PendingSelection> pendingSelections_;',
     '    struct PendingLateSeat { int seat; uint32_t tick; RemoteSeat r; };\n' +
     '    std::vector<PendingLateSeat> pendingLateSeats_;   // by (tick, seat)\n' +
     '    int launchHumans_ = 0;                            // the launch\'s humans (dormant seats included)\n' +
     '    void applyPendingLateSeats();\n' +
     '    std::vector<PendingSelection> pendingSelections_;'],
]);
ed('src/game/World.cpp', [
    ['    applyPendingSelections();   // online Choose Character picks due this tick (before spawns)\n',
     '    applyPendingLateSeats();    // join in progress: dormant seats waking this tick (before their picks)\n' +
     '    applyPendingSelections();   // online Choose Character picks due this tick (before spawns)\n'],
    ['            if (match_.settings().teamGame) me.team = svTeam >= 0 ? svTeam : l.humans[(size_t)ls].team;\n',
     '            if (match_.settings().teamGame) me.team = svTeam >= 0 ? svTeam : l.humans[(size_t)ls].team;\n' +
     '            if (l.humans[(size_t)ls].dormant) { me.dormant = true; me.team = 255; }   // the joiner: spectates until its seat wakes\n'],
    ['            if ((int)i != ls) { RemoteSeat h = l.humans[i]; if (svTeam >= 0) h.team = svTeam; order[i] = addRemoteParticipant(h, true); }\n',
     '            if ((int)i != ls) { RemoteSeat h = l.humans[i]; if (svTeam >= 0 && !h.dormant) h.team = svTeam; order[i] = addRemoteParticipant(h, true); }\n'],
    ['        LOG_INFO("session: %zu humans in the host\'s seat order, this machine at seat %d", l.humans.size(), ls);\n',
     '        launchHumans_ = (int)l.humans.size();\n' +
     '        LOG_INFO("session: %zu humans in the host\'s seat order, this machine at seat %d", l.humans.size(), ls);\n'],
    ['        mp.sessionMember = r.member;\n',
     '        mp.sessionMember = r.member;\n' +
     '        if (r.dormant) { mp.dormant = true; mp.team = 255; }\n'],
    ['    for (const MatchPlayer& p : match_.players()) { taken.push_back(p.name); humans += p.kind != ParticipantKind::Bot; }\n',
     '    for (const MatchPlayer& p : match_.players()) { taken.push_back(p.name); humans += p.kind != ParticipantKind::Bot && !p.dormant; }\n'],
    ['        h.scoreboard.push_back({(int)i, p.name, p.team, p.score, p.kills, p.deaths, p.assists, p.alive, (int)i == localPlayer_});\n',
     '        if (p.dormant) continue;   // a reserved seat is nobody yet\n' +
     '        h.scoreboard.push_back({(int)i, p.name, p.team, p.score, p.kills, p.deaths, p.assists, p.alive, (int)i == localPlayer_});\n'],
    ['    if (!participantPawn(p)) return false;\n    return match_.sameTeam(p, localPlayer_) || (specKillerOk_ && p == specKiller_);\n',
     '    if (!participantPawn(p)) return false;\n' +
     '    if (match_.players()[(size_t)localPlayer_].dormant) return true;   // a joiner before its wake follows anyone\n' +
     '    return match_.sameTeam(p, localPlayer_) || (specKillerOk_ && p == specKiller_);\n'],
    ['seatLocks_.clear(); pendingSelections_.clear();', 'seatLocks_.clear(); pendingSelections_.clear(); pendingLateSeats_.clear(); launchHumans_ = 0;'],
    ['void World::queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick) {\n',
     'void World::queueLateSeat(int seat, const RemoteSeat& r, uint32_t atTick) {\n' +
     '    if ((int64_t)atTick <= (int64_t)simTick_) LOG_WARN("session: seat %d join for tick %u arrived at tick %lld: applied at the next step", seat, atTick, (long long)simTick_);\n' +
     '    PendingLateSeat p{seat, atTick, r};\n' +
     '    auto it = std::upper_bound(pendingLateSeats_.begin(), pendingLateSeats_.end(), p, [](const PendingLateSeat& a, const PendingLateSeat& b) {\n' +
     '        return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat; });\n' +
     '    pendingLateSeats_.insert(it, p);\n' +
     '}\n\n' +
     'void World::applyPendingLateSeats() {\n' +
     '    size_t n = 0;\n' +
     '    for (; n < pendingLateSeats_.size() && (int64_t)pendingLateSeats_[n].tick <= (int64_t)simTick_; ++n) {\n' +
     '        const PendingLateSeat& q = pendingLateSeats_[n];\n' +
     '        if (!matchActive_ || seatPlayer(q.seat) < 0 || !match_.players()[(size_t)q.seat].dormant) { LOG_WARN("session: seat %d join refused (not a dormant seat)", q.seat); continue; }\n' +
     '        const int team = match_.settings().teamGame ? q.r.team : 255;\n' +
     '        if (team == 0 || team == 1) {   // a team full with bot fill: its last bot leaves (despawned, dormant) so the joiner takes its place\n' +
     '            int count = 0, lastBot = -1;\n' +
     '            for (size_t i = 0; i < match_.players().size(); ++i) {\n' +
     '                const MatchPlayer& mp = match_.players()[i];\n' +
     '                if (mp.dormant || mp.team != team) continue;\n' +
     '                ++count;\n' +
     '                if (mp.kind == ParticipantKind::Bot && !mp.directorSpawned) lastBot = (int)i;\n' +
     '            }\n' +
     '            if (count >= match_.settings().maxPerTeam && lastBot >= 0) {\n' +
     '                for (MatchOpponent* o : opponents_) if (o->matchPlayer() == lastBot && o->spawned()) o->despawn();\n' +
     '                match_.retirePlayer(lastBot);\n' +
     '            }\n' +
     '        }\n' +
     '        match_.wakePlayer(q.seat, q.r.name, q.r.member, team);\n' +
     '        if (remoteIdle_.size() > (size_t)q.seat) remoteIdle_[(size_t)q.seat] = 0.0f;\n' +
     '        LOG_INFO("session: seat %d (%s) joined at tick %lld on team %d%s", q.seat, q.r.name.c_str(), (long long)simTick_, team, q.seat == localPlayer_ ? " (this machine)" : "");\n' +
     '    }\n' +
     '    if (n) pendingLateSeats_.erase(pendingLateSeats_.begin(), pendingLateSeats_.begin() + (long)n);\n' +
     '}\n\n' +
     'int World::smallerTeam() const {\n' +
     '    int n[2] = {0, 0}, h[2] = {0, 0};\n' +
     '    for (const MatchPlayer& mp : match_.players()) {\n' +
     '        if (mp.dormant || (mp.team != 0 && mp.team != 1)) continue;\n' +
     '        ++n[mp.team];\n' +
     '        h[mp.team] += mp.kind != ParticipantKind::Bot;\n' +
     '    }\n' +
     '    const int cap = match_.settings().maxPerTeam;\n' +
     '    if (n[0] != n[1] && !(n[0] >= cap && n[1] >= cap)) return n[0] < n[1] ? 0 : 1;\n' +
     '    if (h[0] != h[1]) return h[0] < h[1] ? 0 : 1;\n' +
     '    return 0;\n' +
     '}\n\n' +
     'void World::queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick) {\n'],
]);
ed('src/core/Application.h', [['    void runEscalationTest();', '    void runLateSeatTest();     // WFC_LATESEATTEST (Application_LateSeatTest.cpp)\n    void runEscalationTest();']]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }',
     '    if (std::getenv("WFC_LATESEATTEST")) { runLateSeatTest(); return false; }   // join in progress (dormant seats)\n' +
     '    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }'],
]);
ed('src/core/Application_Frontend.cpp', [['"WFC_TDMTEST", "WFC_ESCTEST",', '"WFC_TDMTEST", "WFC_ESCTEST", "WFC_LATESEATTEST",']]);
console.log('ok');
