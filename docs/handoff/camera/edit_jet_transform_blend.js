// Camera item, part 1 (RE PRESENTATION_AUDIT_2_RE.md part 3): the transform camera blend is the ENTERED strategy's TransitionTime:
// OverTheShoulder 1.5 s (to robot); the hover strategies of cars / trucks / tanks 1.5 s; every jet's HoverPlane_* strategy 1.0 s (all 11
// jets, Laserbeak included). Ours used kCamTransHover 1.5 for jets too. Camera only (presentation; no identity impact).
// Run from a tree root.
const fs = require('fs');
const f = 'src/game/PlayerController.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '        stratDur_ = want == 0 ? cfg::kCamTransRobot : (want == 1 ? cfg::kCamTransHover : cfg::kCamTransDrive);\n';
const b = '        // The entered strategy\'s TransitionTime: a jet\'s HoverPlane_* strategy is 1.0 s (RE presentation audit 2 part 3), the other hovers 1.5.\n' +
          '        stratDur_ = want == 0 ? cfg::kCamTransRobot : (want == 1 ? (jet ? 1.0f : cfg::kCamTransHover) : cfg::kCamTransDrive);\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, b);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
