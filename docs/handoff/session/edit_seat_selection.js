// Online in-match Choose Character, tick-stamped (the rendered net match, Integration 2026-10-17): the net driver hands every seat's pick
// as {seat, atTick, selection}; World::queueSeatSelection queues it and the step prefix applies it (Match::selectCharacter) when the sim
// tick reaches atTick, ordered by (tick, seat), before spawns - the same tick on every peer, so the next spawn uses it everywhere.
// A pick stamped for a tick already simulated applies at once with a warning (the driver must deliver before the tick, as commands).
// WFC_SESSIONTEST: the cross-perspective identity with a queued pick. Offline nothing calls it. Run from a tree root (ab855fe7+).
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
    ['    bool guidedMissileAlive() const { return missile_.alive; }\n',
     '    bool guidedMissileAlive() const { return missile_.alive; }\n' +
     '    // Online Choose Character: apply seat\'s pick at sim tick atTick (every peer the same tick; the net driver delivers it before).\n' +
     '    void queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick);\n'],
    ['    void clearAllParticipants();\n',
     '    void clearAllParticipants();\n' +
     '    struct PendingSelection { int seat; uint32_t tick; CharacterSelection sel; };\n' +
     '    std::vector<PendingSelection> pendingSelections_;   // by (tick, seat); applied in the step prefix\n' +
     '    void applyPendingSelections();\n'],
]);
ed('src/game/World.cpp', [
    ['    tickSlotSwaps(dt);   // session slot handovers at their agreed tick (main thread, before any bot / pawn work)\n',
     '    tickSlotSwaps(dt);   // session slot handovers at their agreed tick (main thread, before any bot / pawn work)\n' +
     '    applyPendingSelections();   // online Choose Character picks due this tick (before spawns)\n'],
    ['void World::clearAllParticipants() {\n',
     'void World::queueSeatSelection(int seat, const CharacterSelection& sel, uint32_t atTick) {\n' +
     '    if ((int64_t)atTick <= (int64_t)simTick_) LOG_WARN("session: p%d pick for tick %u arrived at tick %lld: applied at the next step", seat, atTick, (long long)simTick_);\n' +
     '    PendingSelection p{seat, atTick, sel};\n' +
     '    auto it = std::upper_bound(pendingSelections_.begin(), pendingSelections_.end(), p, [](const PendingSelection& a, const PendingSelection& b) {\n' +
     '        return a.tick != b.tick ? a.tick < b.tick : a.seat < b.seat; });\n' +
     '    pendingSelections_.insert(it, p);\n' +
     '}\n\n' +
     'void World::applyPendingSelections() {\n' +
     '    size_t n = 0;\n' +
     '    for (; n < pendingSelections_.size() && (int64_t)pendingSelections_[n].tick <= (int64_t)simTick_; ++n) {\n' +
     '        const PendingSelection& p = pendingSelections_[n];\n' +
     '        if (!matchActive_ || p.seat < 0 || (size_t)p.seat >= match_.players().size()) continue;\n' +
     '        const bool ok = match_.selectCharacter(p.seat, p.sel);\n' +
     '        LOG_INFO("session: p%d Choose Character at tick %lld: %s", p.seat, (long long)simTick_, ok ? "applied (next spawn)" : "refused");\n' +
     '    }\n' +
     '    if (n) pendingSelections_.erase(pendingSelections_.begin(), pendingSelections_.begin() + (long)n);\n' +
     '}\n\n' +
     'void World::clearAllParticipants() {\n'],
    ['    bots_.clear(); smart_.clear(); anySmart_ = false; remoteIdle_.clear();\n',
     '    bots_.clear(); smart_.clear(); anySmart_ = false; remoteIdle_.clear(); pendingSelections_.clear();\n'],
]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    {   // Online Choose Character: seat 1 picks the Scout preset for tick 300 (before the start); both perspectives apply it alike\n' +
    '        uint64_t hs[2] = {0, 0}; std::string spec[2];\n' +
    '        for (int ls = 0; ls < 2; ++ls) {\n' +
    '            game::MatchLaunch S; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?BotsFriendly=1?BotsEnemy=1", S);\n' +
    '            S.seed = 0x5eedc5e1u; S.online = true; S.localTeam = ls == 0 ? 0 : 1;\n' +
    '            S.humans = {seat("Seat0", 0, "WFC-CS-0"), seat("Seat1", 1, "WFC-CS-1")}; S.localSeat = ls;\n' +
    '            world_.launchMatch(S);\n' +
    '            game::CharacterSelection sc; sc.type = 0; sc.specialty = game::Specialty::Scout; sc.weapons = {"PlasmaCannon", "HeavyPistol", "FlakGrenades"};\n' +
    '            world_.queueSeatSelection(1, sc, (uint32_t)(world_.simTick() + 300));\n' +
    '            for (int i = 0; i < 900; ++i) {\n' +
    '                for (int st = 0; st < 2; ++st) { game::RemoteCommand c; c.tick = (uint32_t)(world_.simTick() + 1); c.aimYaw = st ? 0.785398f : -2.356194f; world_.applyRemoteCommand(st, c); }\n' +
    '                world_.handleInput(idle, dt); world_.tick(dt);\n' +
    '            }\n' +
    '            hs[ls] = world_.simStateHash();\n' +
    '            if (const game::Character* c = world_.participantPawn(1)) spec[ls] = c->specialty();\n' +
    '        }\n' +
    '        LOG_INFO("SESSIONTEST Choose Character: seat 1 specialty %s / %s, hashes %016llx %016llx", spec[0].c_str(), spec[1].c_str(), (unsigned long long)hs[0], (unsigned long long)hs[1]);\n' +
    '        check(hs[0] == hs[1] && spec[0] == spec[1] && spec[0] == "Scout", "online Choose Character: seat 1\'s tick-stamped Scout pick applies alike from both perspectives");\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
