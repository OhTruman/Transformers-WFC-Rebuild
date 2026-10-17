// Removes the Defrag CPU latch (edit_gpu_defrag.js) once the queued GPU-skinned draw honours setDrawEnergyDeath (Rendering's Defrag
// draw-list cache fix, in integration af4d0731). Defrag bodies then take the GPU path like every other body. Run from a tree root.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '        if (c.defrag) c.gpuOk = false;   // the Defrag energy-death material is not applied on the queued GPU draw: CPU path\n';
if (s.split(a).length !== 2) throw new Error('latch line not found exactly once');
s = s.replace(a, '');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('latch removed');
