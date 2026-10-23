// Escalation follow-up (on the merged E3a): Round Begin / Round End also fire World::levelAudioEvent("Game:TnSeqEvent_SurvivalEvents:...")
// for the maps' audio Kismet (Systems' LevelAudioScript), and WFC_ESCLOG (DEV, log only) adds every kill (victim type, killer, damage
// type), generator buy (buyer) and weapon take (factory / drop) for Experimental's soak counts. WorldSurvival.cpp copied from argv[2].
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
fs.copyFileSync(path.join(G, 'docs/handoff/escalation/WorldSurvival.cpp'), 'src/game/WorldSurvival.cpp');
console.log('ok');
