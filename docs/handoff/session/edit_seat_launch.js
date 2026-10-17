// NET.identity follow-up (Systems, 2026-10-17): the launch record alone defines every human seat.
// 1. launchMatch applies humans[localSeat].selection to the local player too (it applied the remote seats' only: the local pawn kept a
//    default, 300 vs 330 hp).
// 2. WFC_SESSIONTEST: the cross-perspective identity from the launch through the first InProgress steps, under NET.identity's conditions:
//    the selections only from the launch record (no selectCharacter afterwards), 2 v 2 bots, aim-only commands, every step compared.
// Run after edit_seat_iter.js (integration 0df2a0fa has the scripts before it).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.cpp', [[
    '            me.name = l.humans[(size_t)ls].name; me.sessionMember = l.humans[(size_t)ls].member;\n',
    '            me.name = l.humans[(size_t)ls].name; me.sessionMember = l.humans[(size_t)ls].member;\n' +
    '            if (l.humans[(size_t)ls].selected) match_.selectCharacter(localPlayer_, l.humans[(size_t)ls].selection);   // as the remote seats\n'
]]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    // The cross-perspective identity from the launch (NET.identity\'s conditions): selections only from the launch record, 2 v 2 bots,\n' +
    '    // aim-only commands, every step compared from the launch on.\n' +
    '    {\n' +
    '        std::vector<std::string> rows[2]; std::vector<uint64_t> hh[2];\n' +
    '        const bool pbot = game::World::playerBotDifficulty() >= 0;\n' +
    '        std::vector<game::RemoteCommand> rec;\n' +
    '        LOG_INFO("SESSIONTEST launch identity: seat 0 input from %s", pbot ? "the player bot (recorded on A, replayed on B)" : "aim-only commands");\n' +
    '        for (int ls = 0; ls < 2; ++ls) {\n' +
    '            game::MatchLaunch S; game::MatchLaunch::fromURL(world_.mapName() + "_BASE_m?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2", S);\n' +
    '            S.seed = 0x5eedc0deu; S.online = true; S.localTeam = ls == 0 ? 0 : 1;\n' +
    '            S.humans = {seat("Seat0", 0, "WFC-LA-0"), seat("Seat1", 1, "WFC-LA-1")}; S.localSeat = ls;\n' +
    '            world_.launchMatch(S);\n' +
    '            for (int i = 0; i < 2400; ++i) {\n' +
    '                // With WFC_PLAYERBOT the local seat\'s input comes from the player bot (as NET.identity\'s live peers): A sends seat 0\'s\n' +
    '                // encoded commands and records them, B replays them as its remote seat 0 (B\'s own player bot runs too, input only).\n' +
    '                platform::InputFrame fr = idle;\n' +
    '                if (pbot) world_.playerBotInput(fr, dt);\n' +
    '                world_.handleInput(fr, dt);\n' +
    '                for (int st = 0; st < 2; ++st) {\n' +
    '                    game::RemoteCommand c; c.tick = (uint32_t)(world_.simTick() + 1);\n' +
    '                    c.aimYaw = st == 0 ? -2.356194f : 0.785398f; c.aimPitch = 0.0f;\n' +
    '                    if (pbot && st == 0) {\n' +
    '                        if (ls == 0) { c = world_.encodeLocalCommand(); rec.push_back(c); }\n' +
    '                        else if ((size_t)i < rec.size()) c = rec[(size_t)i];\n' +
    '                        c.tick = (uint32_t)(world_.simTick() + 1);\n' +
    '                    }\n' +
    '                    world_.applyRemoteCommand(st, c);\n' +
    '                }\n' +
    '                world_.tick(dt);\n' +
    '                hh[ls].push_back(world_.simStateHash());\n' +
    '                std::string row;\n' +
    '                for (size_t pp = 0; pp < world_.match().players().size(); ++pp) {\n' +
    '                    const game::Character* c = world_.participantPawn((int)pp); char b[200];\n' +
    '                    if (c) std::snprintf(b, sizeof b, " p%zu (%.4f %.4f %.4f) v(%.4f %.4f %.4f) y%.4f h%.1f", pp, c->position().x, c->position().y, c->position().z,\n' +
    '                                         c->velocity().x, c->velocity().y, c->velocity().z, c->yaw(), c->health().current);\n' +
    '                    else std::snprintf(b, sizeof b, " p%zu -", pp);\n' +
    '                    row += b;\n' +
    '                }\n' +
    '                rows[ls].push_back(row);\n' +
    '            }\n' +
    '        }\n' +
    '        size_t first = hh[0].size();\n' +
    '        for (size_t i = 0; i < hh[0].size() && i < hh[1].size(); ++i) if (hh[0][i] != hh[1][i]) { first = i; break; }\n' +
    '        if (first < hh[0].size()) {\n' +
    '            LOG_INFO("SESSIONTEST launch identity: first divergent step %zu", first);\n' +
    '            LOG_INFO("SESSIONTEST   A:%s", rows[0][first].c_str()); LOG_INFO("SESSIONTEST   B:%s", rows[1][first].c_str());\n' +
    '            if (first > 0) { LOG_INFO("SESSIONTEST   A prev:%s", rows[0][first - 1].c_str()); LOG_INFO("SESSIONTEST   B prev:%s", rows[1][first - 1].c_str()); }\n' +
    '        }\n' +
    '        check(first == hh[0].size(), "launch identity: local at seat 0 vs seat 1 from the launch record alone, every step through the first InProgress seconds");\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
