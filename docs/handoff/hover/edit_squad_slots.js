// Package 7 squad follow reversals (Smart only). The NAVQ trace (2026-10-17, WFC_FOLLOWTRACE) found the "alone" reversals (the member
// turned back while its leader kept its heading, ~80 % of follow reversals) on wedge Roam goals with two mechanisms:
//  A. the wedge slot was not walkable (a wall / an edge) and fell back to the LEADER's position with a 2.5 m radius: the member walked
//     into the leader and turned away (slotOff (0, 0) in the trace);
//  B. the member flipped between the wedge (<= squadRegroupM) and the regroup / trip goal (> squadRegroupM) as its distance crossed
//     the threshold: the goal jumped 13-270 m back and forth.
// Fix A: an unwalkable slot tries the mirrored side, then half-depth slots on both sides, then straight behind; then the member's last
// usable slot (if still within squadRegroupM of the leader); only then the leader's position, with a 5 m radius.
// Fix B: hysteresis - a member that held a wedge slot within the last 1.5 s keeps the wedge until squadRegroupM + 5 m.
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
    '    bool formWedge = false; int formLeader = -1;',
    '    float wedgeAt = -1e9f; core::Vec3 lastSlot{0, 0, 0}; bool haveLastSlot = false;   // the wedge\'s hysteresis and last usable slot\n' +
    '    bool formWedge = false; int formLeader = -1;'
]]);
ed('src/game/WorldBots.cpp', [
    ['                    } else if (hdist(L, pos) <= smartTune().squadRegroupM) {\n',
     '                    } else if (hdist(L, pos) <= smartTune().squadRegroupM + (now - s.wedgeAt < 1.5f ? 5.0f : 0.0f)) {   // hysteresis (fix B)\n' +
     '                        s.wedgeAt = now;\n'],
    ['                        core::Vec3 slotP = usableSlot(q) ? q : L;\n',
     '                        // Fix A: an unwalkable slot tries the mirror side, half depth, straight behind, then the last usable slot, before the leader.\n' +
     '                        core::Vec3 slotP = q; bool slotOk = usableSlot(q);\n' +
     '                        if (!slotOk)\n' +
     '                            for (const core::Vec3& alt : {L - hd * back - perp * sideOff, L - hd * (0.5f * back) + perp * (0.5f * sideOff),\n' +
     '                                                         L - hd * (0.5f * back) - perp * (0.5f * sideOff), L - hd * back})\n' +
     '                                if (usableSlot(alt)) { slotP = alt; slotOk = true; break; }\n' +
     '                        if (!slotOk && s.haveLastSlot && hdist(s.lastSlot, L) < smartTune().squadRegroupM && botNav_.findCell(s.lastSlot, 0.0f) >= 0) {\n' +
     '                            slotP = s.lastSlot; slotOk = true;\n' +
     '                        }\n' +
     '                        if (slotOk) { s.lastSlot = slotP; s.haveLastSlot = true; } else slotP = L;\n'],
    ['                        squadGoal(BotGoalKind::Roam, slotP, 2.5f, -1);\n                        return;\n',
     '                        squadGoal(BotGoalKind::Roam, slotP, slotOk ? 2.5f : 5.0f, -1);   // at the leader: a wider radius (no walking into it)\n                        return;\n'],
    ['                if (hdist(lp->position(), pos) > smartTune().squadRegroupM) {   // regroup on the leader',
     '                if (hdist(lp->position(), pos) > smartTune().squadRegroupM + (now - s.wedgeAt < 1.5f ? 5.0f : 0.0f)) {   // regroup on the leader'],
]);
console.log('ok');
