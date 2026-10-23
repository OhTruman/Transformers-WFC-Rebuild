// Escalation E2b part 3 (on the strand / spawn patch): the Escalation nav - AssetTools' bot_nav_escalation.json (the ORIGINAL
// NavigationCell mesh) in GameModeTag ESC, the blocked-at-start cells and each generator's opened cells from
// escalation_nav_blockers.json (the original's own gating, replacing the door-box blocking); the versus nav comes back on a non-ESC
// launch. WorldSurvival.cpp / Survival.h / Application_EscTest.cpp copied from argv[2]; BotNav::isBlocked for the test.
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
for (const [src, dst] of [['docs/handoff/escalation/Survival.h', 'src/game/Survival.h'], ['docs/handoff/escalation/WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'],
                          ['docs/handoff/escalation/Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);
ed('src/game/BotNav.h', [
    ['    void recomputePieces() { computePieces(); }\n',
     '    void recomputePieces() { computePieces(); }\n' +
     '    bool isBlocked(int cell) const { return cell >= 0 && (size_t)cell < blocked_.size() && blocked_[(size_t)cell] != 0; }\n' +
     '    int pieceOf(int c) const { return c >= 0 && (size_t)c < piece_.size() ? piece_[(size_t)c] : -1; }\n' +
     '    int mainPieceId() const { return mainPieceId_; }\n'],
]);
console.log('ok');
