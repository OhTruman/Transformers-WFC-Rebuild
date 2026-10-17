// The default death's gibs effect (no picked entry: the blueprint's DefaultGibs_DSYS, Exp_Death_FX at +50 UU; vehicles TF_Death_MED /
// jets Exp_Death_Lrg at +0) at PositionSocket like the entries' rows, instead of the actor origin (Systems' FX-count row, 2026-10-17).
// A mesh without the socket row keeps the actor origin. Run from a tree root after edit_deathfx_log.js.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '        const core::Vec3 at = c.pos + core::Vec3{0, c.vehicle ? 0.0f : 0.5f, 0};\n        fxList = std::string(fx) + "@pos";\n';
const b = '        core::Vec3 ps = c.pos; const bool psOk = c.deathSocketWorld("PositionSocket", ps);\n' +
          '        const core::Vec3 at = ps + core::Vec3{0, c.vehicle ? 0.0f : 0.5f, 0};\n' +
          '        fxList = std::string(fx) + (psOk ? "@PositionSocket" : "@pos");\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, b);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
