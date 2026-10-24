// Static data (escalation_MP_ESC_Static.json + EscalationData.inc come from gen_escalation.py; run it after this script) and an
// ESCTEST honesty fix: "due" covers the enemy rows with a MinCount only. The director spawns a type below its MinCount exclusively,
// and ESCTEST clears the horde every 2 s, so optional rows (MinCount 0) depend on budget and kill cadence (Static: its MinCount rows
// are always below their minimum after each clear, so the optional rows never get a free pick in the test). Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
const g = 'src/core/Application_EscTest.cpp';
let t = fs.readFileSync(g, 'utf8'); const crlf = t.includes(CR + NL); t = t.split(CR + NL).join(NL);
const a = '            if (D.enemies[e].minWave >= S.currentWave - 1 || D.enemies[e].maxCount <= 0) continue;' + NL;
if (t.split(a).length !== 2) throw new Error('ESCTEST anchor');
t = t.replace(a, '            if (D.enemies[e].minWave >= S.currentWave - 1 || D.enemies[e].maxCount <= 0 || D.enemies[e].minCount <= 0) continue;   // MinCount rows' + NL);
t = t.replace('"every enemy type due by the reached wave spawned"', '"every enemy type with a MinCount due by the reached wave spawned"');
fs.writeFileSync(g, crlf ? t.split(NL).join(CR + NL) : t);
console.log('ok');
