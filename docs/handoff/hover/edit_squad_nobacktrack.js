// Package 7 squad follow reversals (Smart only; Classic has no squad following, so its SIMHASH is untouched by this edit).
// NAVQ at af4d0731 (24 seeds): Smart follow reversals 1.55-2.44 / travel-min, about half of all Smart reversals. Mechanism: a wedge
// member walking to its slot behind a moving leader overtakes the slot whenever the leader slows or turns; the re-issued slot goal then
// lies behind it and it turns round (a reversal), then forward again.
// Rule (squadNoBacktrackM, default 8 m in the squadTactics >= 2 preset): while the leader moves, a slot that lies BEHIND the member along
// the leader's heading and within squadNoBacktrackM is not walked back to: the member holds where it is and lets the slot come back to
// it. A slot farther than that, or the leader standing, keeps the normal goal (real regrouping still happens).
// Run from a tree root after edit_hover_smart_steady.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/BotSmart.h', [[
    '    float squadSlotFreeze = 0.0f;',
    '    float squadNoBacktrackM = 0.0f;   // with squadFormation: a wedge slot behind the member (along the moving leader\'s heading) within this\n' +
    '                                      // distance is not walked back to (the member holds; follow reversals); 0 = off\n' +
    '    float squadSlotFreeze = 0.0f;'
]]);
ed('src/game/BotSmart.cpp', [
    ['            if (t >= 2) { v.squadMemberWalk = 1.0f; v.squadLeaderWaitS = 14.0f; v.squadLeaderWaitCooldownS = 4.0f; v.squadSlowM = 18.0f; }\n',
     '            if (t >= 2) { v.squadMemberWalk = 1.0f; v.squadLeaderWaitS = 14.0f; v.squadLeaderWaitCooldownS = 4.0f; v.squadSlowM = 18.0f; v.squadNoBacktrackM = 8.0f; }\n'],
    ['{"squadSize", &v.squadSize},', '{"squadNoBacktrackM", &v.squadNoBacktrackM}, {"squadSize", &v.squadSize},'],
]);
ed('src/game/WorldBots.cpp', [[
    '                        s.formWedge = true; s.formLeader = leader; s.formOffset = slotP - L;\n',
    '                        if (smartTune().squadNoBacktrackM > 0.0f) {   // follow reversals: do not walk back to a slot the member overtook\n' +
    '                            const bool leaderMoving = std::sqrt(lp->velocity().x * lp->velocity().x + lp->velocity().z * lp->velocity().z) >= 1.0f;\n' +
    '                            const core::Vec3 rel{slotP.x - pos.x, 0.0f, slotP.z - pos.z};\n' +
    '                            if (leaderMoving && core::dot(rel, hd) < 0.0f && core::length(rel) <= smartTune().squadNoBacktrackM) slotP = pos;   // hold\n' +
    '                        }\n' +
    '                        s.formWedge = true; s.formLeader = leader; s.formOffset = slotP - L;\n'
]]);
console.log('ok');
