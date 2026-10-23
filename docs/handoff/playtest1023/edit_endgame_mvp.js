// RE versus end-of-match check (2026-10-23, C: TnMultiplayerGame / TnVersusGame MatchOver.CanAwardMVP + AwardMVPAchievement,
// TnFreeForAllGame.GetGameCompletionType, GetWinningPRI):
// 1. MVP needs GRI.CanGainXp() (= !IsPrivateGame): MatchSettings::canGainXp (default true; the frontend sets it from its XP gate,
//    URL ?WfcCanGainXp=0|1); the top score is over every participant, but only player controllers receive achievement 37 (bots
//    count toward the top, never get it).
// 2. FFA equal top score: GetWinningPRI walks the PRI array with a strict > - the first tied player (join order) is the announced
//    winner (GRI.Winner, TnFreeForAllGameOverMessage); the completion types are unchanged (tied top 2, the rest 1, a sole top 0).
// Run from a tree root.
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
    ['    bool kickIdle = false;',
     '    bool canGainXp = true;             // GRI.CanGainXp (= !IsPrivateGame): gates the MVP award; the frontend\'s XP rule (?WfcCanGainXp)\n' +
     '    bool kickIdle = false;'],
]);
ed('src/game/Match.cpp', [
    ['        // GetWinningPRI: highest score [HIGH: stock first-highest; tie handling PARTIAL].\n' +
     '        // GetWinningPRI: the top score; an equal top score is a draw (completion type 2) [CONF RE PLAYTEST §3].\n' +
     '        int best = -1; bool tie = false;\n' +
     '        for (size_t i = 0; i < players_.size(); ++i) {\n' +
     '            if (best < 0 || players_[i].score > players_[(size_t)best].score) { best = (int)i; tie = false; }\n' +
     '            else if (players_[i].score == players_[(size_t)best].score) tie = true;\n' +
     '        }\n' +
     '        winnerPlayer = tie ? -1 : best;\n',
     '        // GetWinningPRI: the PRI array walked with a strict > - on an equal top score the first one (join order) is the announced\n' +
     '        // winner (GRI.Winner); the completion types below still mark the tied players as a draw [CONF RE end-of-match check].\n' +
     '        int best = -1;\n' +
     '        for (size_t i = 0; i < players_.size(); ++i)\n' +
     '            if (best < 0 || players_[i].score > players_[(size_t)best].score) best = (int)i;\n' +
     '        winnerPlayer = best;\n'],
    ['        // FFA: the winner wins, an equal top score draws [CONF RE PLAYTEST §3 / MP_PROGRESSION §1].\n',
     '        // FFA (GetGameCompletionType): 1 if anyone scored higher, else 2 if anyone scored the same, else 0 - independent of the\n' +
     '        // announced winner [CONF RE PLAYTEST §3 / MP_PROGRESSION §1 / end-of-match check].\n'],
    ['            else e.completion[i] = (winnerPlayer < 0 && players_[i].score == top) ? 2 : ((int)i == winnerPlayer ? 0 : 1);\n',
     '            else e.completion[i] = players_[i].score == top ? (topCount > 1 ? 2 : 0) : 1;\n'],
    ['        // MVP (achievement 37): every PRI tied for the top personal score, more than one player (versus: both teams present).\n' +
     '        int n0 = 0, n1 = 0; for (const MatchPlayer& p : players_) { n0 += p.team == 0; n1 += p.team == 1; }\n' +
     '        if (players_.size() > 1 && (!s_.teamGame || (n0 > 0 && n1 > 0)))\n' +
     '            for (size_t i = 0; i < players_.size(); ++i) if (players_[i].score == top) e.mvp.push_back((int)i);\n',
     '        // MVP (achievement 37, CanAwardMVP + AwardMVPAchievement): CanGainXp, more than one PRI (versus: both teams present); every\n' +
     '        // PRI tied for the top personal score over all participants, received only by player controllers (bots count, never get it).\n' +
     '        int n0 = 0, n1 = 0; for (const MatchPlayer& p : players_) { n0 += p.team == 0; n1 += p.team == 1; }\n' +
     '        if (s_.canGainXp && players_.size() > 1 && (!s_.teamGame || (n0 > 0 && n1 > 0)))\n' +
     '            for (size_t i = 0; i < players_.size(); ++i)\n' +
     '                if (players_[i].score == top && players_[i].kind != ParticipantKind::Bot) e.mvp.push_back((int)i);\n'],
]);
ed('src/game/World.h', [['    int winnerPlayer = -1;                       // FFA winner (GetWinningPRI; -1 draw)\n',
    '    int winnerPlayer = -1;                       // FFA winner (GetWinningPRI: on a tie the first tied player; -1 none)\n']]);
ed('src/game/World.cpp', [
    ['    if (opt.count("WfcIdleKick")) out.settings.kickIdle',
     '    if (opt.count("WfcCanGainXp")) out.settings.canGainXp = std::atoi(opt["WfcCanGainXp"].c_str()) != 0;   // GRI.CanGainXp (MVP gate)\n' +
     '    if (opt.count("WfcIdleKick")) out.settings.kickIdle'],
]);
console.log('ok');
