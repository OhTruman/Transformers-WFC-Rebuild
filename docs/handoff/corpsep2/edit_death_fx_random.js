// Patch 2 follow-up (on integration cccff627, which carries patch 2 as 1e11edc2): TnDeathModifierPlayEffect.PlayType PET_Random.
// SharedCarDeaths / SharedTankDeaths play ONE of TF_Death_Buildup_FX / TF_Death_MED_FX at TargetingSocket (a uniform pick); jets and
// robots play all of play[]. Systems' DeathFx.inc (agents/systems a22a2f69) adds `bool playRandom` per row (true on the 8 car / tank
// rows); patch 2 followed the earlier table, which ignored PlayType, and played both. The pick uses the corpse's own frand() once,
// before the log and the spawn, so the DEATHFX line lists what plays. Presentation only.
// Run from a tree root with corpsep2/new/DeathFx.inc (a22a2f69) copied to src/game/.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.readFileSync('src/game/DeathFx.inc', 'utf8').includes('playRandom')) throw new Error('DeathFx.inc lacks playRandom: copy the a22a2f69 version');
ed('src/game/World.cpp', [
    ['                if (deathFxLogOn() && c.fxRow >= 0) {\n                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n                    std::string pl;\n                    for (int i = 0; i < R.nPlay; ++i) { core::Vec3 tmp;',
     '                int onlyPlay = -1;   // PET_Random (car / tank): one uniformly picked entry of play[]\n' +
     '                if (c.fxRow >= 0 && kDeathFx[(size_t)c.fxRow].playRandom && kDeathFx[(size_t)c.fxRow].nPlay > 0)\n' +
     '                    onlyPlay = (int)(c.frand() * (float)kDeathFx[(size_t)c.fxRow].nPlay) % kDeathFx[(size_t)c.fxRow].nPlay;\n' +
     '                if (deathFxLogOn() && c.fxRow >= 0) {\n                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n                    std::string pl;\n' +
     '                    for (int i = 0; i < R.nPlay; ++i) { if (onlyPlay >= 0 && i != onlyPlay) continue; core::Vec3 tmp;'],
    ['                    for (int i = 0; i < R.nPlay; ++i) {\n                        const char* so = R.play[i].socket;\n',
     '                    for (int i = 0; i < R.nPlay; ++i) {\n                        if (onlyPlay >= 0 && i != onlyPlay) continue;\n                        const char* so = R.play[i].socket;\n'],
]);
console.log('ok');
