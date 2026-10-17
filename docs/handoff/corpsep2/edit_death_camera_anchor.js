// Death camera fix (crop slot 2026-10-17: the robot camera sat inside the falling corpse). The strategy's anchor is the bone PLUS
// HmOffsetAnchorPointRelativeToActor's offset (robot (0, -300, 150), vehicle (-100, 0, 50), camera axes); the camera is the anchor plus
// the inherited orbit offset, and TnThirdPersoncollision traces from that ANCHOR (as the live camera traces from its anchor, the actor
// + the strategy's Z offset). The edit traced from the bone itself, which lies near the floor once the body falls, so the trace grazed
// the floor and pulled the camera into the corpse. Run from a tree root after edit_default_gibs_socket.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/PlayerController.h', [[
    '    core::Vec3 deathAnchor_{0, 0, 0}, deathOff_{0, 0, 0};   // deathOff_: orbit space (x forward, y right, z up), m\n',
    '    core::Vec3 deathAnchor_{0, 0, 0}, deathOff_{0, 0, 0};   // deathOff_: orbit space (x forward, y right, z up), m\n' +
    '    core::Vec3 deathAnchorOff_{0, 0, 0};                   // the strategy\'s anchor offset (camera axes, m): the trace origin\n'
]]);
ed('src/game/PlayerController.cpp', [
    ['    deathOff_ = deathOff_ + (vehicle ? core::Vec3{-1.0f, 0.0f, 0.5f} : core::Vec3{0.0f, -3.0f, 1.5f});\n',
     '    deathAnchorOff_ = vehicle ? core::Vec3{-1.0f, 0.0f, 0.5f} : core::Vec3{0.0f, -3.0f, 1.5f};   // kept apart: the trace starts there\n'],
    ['    const core::Vec3 focus = deathAnchor_;\n    core::Vec3 to = focus + F * deathOff_.x + R * deathOff_.y + U * deathOff_.z;\n',
     '    const core::Vec3 focus = deathAnchor_ + F * deathAnchorOff_.x + R * deathAnchorOff_.y + U * deathAnchorOff_.z;   // the anchor point\n' +
     '    core::Vec3 to = focus + F * deathOff_.x + R * deathOff_.y + U * deathOff_.z;\n'],
]);
console.log('ok');
