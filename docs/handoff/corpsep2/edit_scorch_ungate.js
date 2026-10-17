// Patch 2 follow-up: the scorch ON by default again (removes edit_scorch_gate.js's WFC_DEATHSCORCH gate). Systems' regenerated
// DeathDecal_MAT (the translated lerp weight now G x Color.a, as the original) switches into the material sets with this commit's merge.
// Run from a tree root that carries patch 2.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '    static const bool scorchOn = std::getenv("WFC_DEATHSCORCH") != nullptr;   // gated until the runtime-decal blend is fixed (Rendering)\n' +
          '    if (scorchOn && !c.vehicle && !c.noDecal && renderer_ && collision_.valid()) {\n';
const b = '    if (!c.vehicle && !c.noDecal && renderer_ && collision_.valid()) {\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, b);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
