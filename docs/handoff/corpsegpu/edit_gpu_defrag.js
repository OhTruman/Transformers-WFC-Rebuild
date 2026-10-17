// Defrag (melee energy-death) bodies stay on the CPU path: the queued GPU-skinned character draw does not take setDrawEnergyDeath (crops:
// the GPU body rendered undissolved). The body's first tick latches the fallback, so it is CPU-skinned and drawn posed as before.
// Run from the tree root, after edit_corpse_gpu.js.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '        if (!c.gibbed && !c.poseStatic) {\n            if (c.clip >= 0) {';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, '        if (c.defrag) c.gpuOk = false;   // the Defrag energy-death material is not applied on the queued GPU draw: CPU path\n' + a);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
