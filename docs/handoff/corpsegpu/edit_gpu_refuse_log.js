// A body whose GPU-skinned draw is refused logs it once (the fallback count for the checks; bone rows full, > 128 joints, WFC_NOGPUSKIN).
// Run from the tree root, after edit_corpse_gpu.js.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '            if (!drawn) c.gpuOk = false;   // refused: the next tick CPU-skins this body (nothing drawn this frame)';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, '            if (!drawn) { c.gpuOk = false; LOG_INFO("corpse p%d %s: GPU-skinned draw refused, CPU fallback", c.player, c.chassis.c_str()); }   // the next tick CPU-skins it');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
