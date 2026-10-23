// Escalation final-build fixes: M10 (pickup weapons take the rest of PlayerData: damage, fire interval, spread, range falloff, shots,
// timings) + the horde's 1 s melee interval. WorldSurvival.cpp copied from argv[2]; the melee rule from edit_escalation_horde_melee.js.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
fs.copyFileSync(path.join(G, 'docs/handoff/escalation/WorldSurvival.cpp'), 'src/game/WorldSurvival.cpp');
require(path.join(G, 'docs/handoff/escalation/edit_escalation_horde_melee.js'));
