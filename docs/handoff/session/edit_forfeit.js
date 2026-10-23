// Leavers and the Forfeit rule (RE ONLINE_JOIN_IN_PROGRESS_RE.md; Integration: Systems feeds the tick-stamped leave, Match owns the
// timer): World::queueLeave(seat, atTick) - a human seat whose member left and is not handed to a bot (applySlotSwap toBot) leaves at
// the tick on every peer (its pawn despawned, the seat dormant: not counted / listed, never respawned); Match: a versus team that had
// participants and has none for 1.5 s ends the match, EndGame "Forfeit" (PickWinningTeam: the other team, the empty one). A team
// empty from the start never forfeits. Run from a tree root (the late-seat patch merged); argv[2] = the Gameplay worktree.
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
    ['    void retirePlayer(int p);\n',
     '    void retirePlayer(int p, const char* why = "a bot makes room for a joiner");\n'],
    ['    unsigned rng_ = 0x1234ABCDu;\n',
     '    unsigned rng_ = 0x1234ABCDu;\n' +
     '    float forfeitTimer_[2] = {-1.0f, -1.0f};   // a versus team at 0 participants: time so far (EndGame "Forfeit" at 1.5 s)\n' +
     '    bool teamSeen_[2] = {false, false};         // the team had a participant this match (an empty-from-start team never forfeits)\n' +
     '    void tickForfeit(float dt);\n'],
]);
ed('src/game/Match.cpp', [
    ['        if (s_.teamGame) updateClusters(dt);\n',
     '        if (s_.teamGame) updateClusters(dt);\n' +
     '        if (s_.teamGame && !s_.survival) tickForfeit(dt);\n' +
     '        if (state_ != State::InProgress) return;\n'],
    ['void Match::retirePlayer(int p) {\n',
     '// A versus team with nobody left: EndGame "Forfeit" after 1.5 s [RE join-in-progress: TnVersusGame].\n' +
     'void Match::tickForfeit(float dt) {\n' +
     '    for (int t = 0; t < 2; ++t) {\n' +
     '        int n = 0;\n' +
     '        for (const MatchPlayer& p : players_) n += !p.dormant && p.team == t;\n' +
     '        if (n > 0) { teamSeen_[t] = true; forfeitTimer_[t] = -1.0f; continue; }\n' +
     '        if (!teamSeen_[t]) continue;\n' +
     '        forfeitTimer_[t] = std::max(0.0f, forfeitTimer_[t]) + dt;\n' +
     '        if (forfeitTimer_[t] >= 1.5f) { endGame(-1, "Forfeit"); return; }\n' +
     '    }\n' +
     '}\n\n' +
     'void Match::retirePlayer(int p, const char* why) {\n'],
    ['    LOG_INFO("MATCH leave player=%d name=%s team=%d t=%.2f (a bot makes room for a joiner)", p, P.name.c_str(), P.team, matchTime_);\n',
     '    LOG_INFO("MATCH leave player=%d name=%s team=%d t=%.2f (%s)", p, P.name.c_str(), P.team, matchTime_, why);\n'],
    ['    state_ = State::InProgress;\n    gameStatus_ = 3;\n',
     '    state_ = State::InProgress;\n    gameStatus_ = 3;\n' +
     '    forfeitTimer_[0] = forfeitTimer_[1] = -1.0f; teamSeen_[0] = teamSeen_[1] = false;\n'],
]);
ed('src/game/World.h', [
    ['    int smallerTeam() const;\n',
     '    int smallerTeam() const;\n' +
     '    // A seat whose member left (not handed to a bot): at the tick on every peer its pawn goes and the seat turns dormant (Systems\'\n' +
     '    // driver, from the session\'s tick-stamped leave). An empty versus team then forfeits after 1.5 s (Match).\n' +
     '    void queueLeave(int seat, uint32_t atTick);\n'],
    ['    void applyPendingLateSeats();\n',
     '    void applyPendingLateSeats();\n' +
     '    std::vector<std::pair<uint32_t, int>> pendingLeaves_;   // (tick, seat), sorted\n'],
]);
ed('src/game/World.cpp', [
    ['void World::applyPendingLateSeats() {\n',
     'void World::queueLeave(int seat, uint32_t atTick) {\n' +
     '    const std::pair<uint32_t, int> e{atTick, seat};\n' +
     '    pendingLeaves_.insert(std::upper_bound(pendingLeaves_.begin(), pendingLeaves_.end(), e), e);\n' +
     '}\n\n' +
     'void World::applyPendingLateSeats() {\n' +
     '    size_t nl = 0;\n' +
     '    for (; nl < pendingLeaves_.size() && (int64_t)pendingLeaves_[nl].first <= (int64_t)simTick_; ++nl) {\n' +
     '        const int seat = pendingLeaves_[nl].second;\n' +
     '        if (!matchActive_ || seatPlayer(seat) < 0 || seat == localPlayer_ || match_.players()[(size_t)seat].dormant) continue;\n' +
     '        for (MatchOpponent* o : opponents_) if (o->matchPlayer() == seat && o->spawned()) o->despawn();\n' +
     '        match_.retirePlayer(seat, "left the match");\n' +
     '        LOG_INFO("session: seat %d left at tick %lld", seat, (long long)simTick_);\n' +
     '    }\n' +
     '    if (nl) pendingLeaves_.erase(pendingLeaves_.begin(), pendingLeaves_.begin() + (long)nl);\n'],
    ['pendingLateSeats_.clear(); launchHumans_ = 0;', 'pendingLateSeats_.clear(); pendingLeaves_.clear(); launchHumans_ = 0;'],
]);
console.log('ok');
