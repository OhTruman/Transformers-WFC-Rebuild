// Hover item, part 2: the rest of the original AI path for AI-driven hover vehicles (RE GAMEPLAY_HOVER_RE.md 2.3 / 2.4). The AI path
// (UpdateFromLocationAndRotation) runs UpdateSuspension, rigid-body damping, MoveToTargetLocation and RotateToTargetOrientation; it
// never runs the player path's UpdateRoll (roll torque from the strafe input). Its rigid-body angular damping is 0.1 (the player path
// sets 0). Measured after edit_hover_faithful.js: without these, bots took the player's roll input but not its upright pull, and
// hover tipping rose (NAVQ TIP, 2026-10-17). Run from a tree root after edit_hover_faithful.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/CharacterMovement.cpp', [
    ['        angAx += core::clampf(in.moveRight, -1.0f, 1.0f) - vs.angVel.z;\n',
     '        if (tipMode < 0) angAx += core::clampf(in.moveRight, -1.0f, 1.0f) - vs.angVel.z;   // player path only (not in the AI path [CONF RE hover 2.4])\n'],
    ['    // Rigid-body integration (semi-implicit Euler).\n    vs.angVel.x += angAx * dt;\n    vs.angVel.y += angAy * dt;\n',
     '    // Rigid-body integration (semi-implicit Euler).\n    vs.angVel.x += angAx * dt;\n    vs.angVel.y += angAy * dt;\n' +
     '    if (tipMode >= 0 && !vs.driving) {   // the AI path: RB angular damping 0.1 (the player path sets 0) [CONF RE hover 2.4]\n' +
     '        const float kd = 1.0f / (1.0f + 0.1f * dt);\n' +
     '        vs.angVel.x *= kd; vs.angVel.y *= kd;\n' +
     '    }\n'],
]);
console.log('ok');
