// READY, APPLY ONLY IF Rendering's runtime-decal blend fix is not in by tonight (Integration 2026-10-17): the death scorch stays in the
// code but off by default (the crop slot showed DeathDecal_MAT drawn opaque white with black blobs). WFC_DEATHSCORCH=1 turns it on for
// the re-check after Rendering's fix; the follow-up patch removes this gate. Run from a tree root after edit_scorch.js.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '    if (!c.vehicle && !c.noDecal && renderer_ && collision_.valid()) {\n        const float y0 = c.pos.y;\n';
const b = '    static const bool scorchOn = std::getenv("WFC_DEATHSCORCH") != nullptr;   // gated until the runtime-decal blend is fixed (Rendering)\n' +
          '    if (scorchOn && !c.vehicle && !c.noDecal && renderer_ && collision_.valid()) {\n        const float y0 = c.pos.y;\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, b);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
