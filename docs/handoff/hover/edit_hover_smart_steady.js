// Hover item, part 3 (Integration decision (b), 2026-10-17, after the 24-seed NAVQ TIP table docs/handoff/hover/tip_summary_24seeds.txt):
// - Classic (the fidelity reference) runs the original AI path: no player UpdateTurn pitch / roll pull, no UpdateRoll, RB angular
//   damping 0.1, the absolute 30 deg IsUnstable gate. Its stuck-tipped episodes vanish (Streets longest 214 -> 9 s).
// - Smart keeps the steadier righting on top of the faithful springs (the player's 5 % pull, UpdateRoll, the contact-relative gate):
//   a deliberate Smart improvement over the original (the faithful path gave Smart ~3x more tilts past 30 deg).
// MoveIntent.aiFaithful selects it per bot. Run from a tree root after edit_hover_ai_path.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/CharacterMovement.h', [[
    '    bool aiTipRel = false;',
    '    bool aiTipRel = false;\n    bool aiFaithful = false;   // the original AI hover path (Classic); false keeps the player-path righting (Smart, Integration (b))'
]]);
ed('src/game/CharacterMovement.cpp', [
    ['            if (tipMode < 0 && !(!unstable && vs.onTheGround)) { vs.angVel.x = k30 * vs.roll / dt; vs.angVel.y = k30 * vs.pitch / dt; }\n',
     '            if ((tipMode < 0 || !in.aiFaithful) && !(!unstable && vs.onTheGround)) { vs.angVel.x = k30 * vs.roll / dt; vs.angVel.y = k30 * vs.pitch / dt; }\n'],
    ['            if (tipMode < 0 && (contacts == 0 || B.z.y < 0.01f)) {   // ShouldUpright (player path only [CONF RE hover 2.4])\n',
     '            if ((tipMode < 0 || !in.aiFaithful) && (contacts == 0 || B.z.y < 0.01f)) {   // ShouldUpright (player path; Smart keeps it)\n'],
    ['        if (tipMode < 0) angAx += core::clampf(in.moveRight, -1.0f, 1.0f) - vs.angVel.z;   // player path only (not in the AI path [CONF RE hover 2.4])\n',
     '        if (tipMode < 0 || !in.aiFaithful) angAx += core::clampf(in.moveRight, -1.0f, 1.0f) - vs.angVel.z;   // player path (Smart keeps it); not in the AI path\n'],
    ['    if (tipMode >= 0 && !vs.driving) {   // the AI path: RB angular damping 0.1 (the player path sets 0) [CONF RE hover 2.4]\n',
     '    if (tipMode >= 0 && in.aiFaithful && !vs.driving) {   // the AI path: RB angular damping 0.1 (the player path sets 0) [CONF RE hover 2.4]\n'],
]);
ed('src/game/WorldBots.cpp', [[
    '            if (bt) { in.aiTip = std::atoi(bt); in.aiTipRel = tr && std::atoi(tr) != 0; }\n' +
    '            else { in.aiTip = 0; in.aiTipRel = tr && std::atoi(tr) != 0; }\n',
    '            // Classic: the original AI path (absolute gate, aiFaithful). Smart: the steadier righting, contact-relative gate unless WFC_TIPREL=0\n' +
    '            // (Integration (b): a deliberate Smart improvement).\n' +
    '            const bool smartBot = botIsSmart(b.player);\n' +
    '            in.aiFaithful = !smartBot;\n' +
    '            if (bt) { in.aiTip = std::atoi(bt); in.aiTipRel = tr && std::atoi(tr) != 0; }\n' +
    '            else { in.aiTip = 0; in.aiTipRel = smartBot ? (!tr || std::atoi(tr) != 0) : (tr && std::atoi(tr) != 0); }\n'
]]);
console.log('ok');
