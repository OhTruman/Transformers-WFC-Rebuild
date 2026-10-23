// Escalation E2 fixes (against the merged E2a): RE's E2a review G1 (the item generator's Taken chain run literally: a level drop
// re-fires "New Power Level", so a bought item stays out as a normal respawning factory) and the Remnant player spawn (its four
// TnTeamPlayerStarts carry no team, so the co-op Decepticons found no start and never got a pawn: in survival every team start is the
// players' [RE MP_ESCALATION_RE §1: every TnTeamPlayerStart is set up for attack / defend]). WorldSurvival.cpp / Survival.h /
// Application_EscTest.cpp are copied in from the Gameplay worktree (argv[2]). Run from a tree root.
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
ed('src/game/World.h', [['    void svFireGenerator(int generator);\n', '    void svFireGenerator(int generator, bool levelDrop = false);\n']]);
ed('src/game/Match.cpp', [
    ['        bool ok = s_.teamGame ? (!s.ffa && s.team == P.team) : s.ffa;\n',
     '        // Survival: every TnTeamPlayerStart serves the players (Remnant\'s carry no team); never the director\'s horde points (team 254).\n' +
     '        bool ok = s_.teamGame ? (!s.ffa && (s.team == P.team || (s_.survival && s.team != 254))) : s.ffa;\n'],
]);
console.log('ok');
