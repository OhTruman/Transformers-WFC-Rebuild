// Weapon batch eye check (per-weapon body recoil): node mkeye.js
// Per gun class, the local player holds Fire from spawn (WFC_AUTOFIRE) on Streets with no bots; base int_ssb (integration 8a080210)
// vs new int_wb2 (+ GAMEPLAY_WEAPON_MELEE.patch). Windowed, RT off, lockstep; shots every 4 frames over the first seconds of firing.
const fs = require('fs');
const bs = String.fromCharCode(92);
const P = ['F:', 'Transformers Rebuild', 'Rebuild-Gameplay', 'work'].join(bs);
const W = [P, 'pass29', 'wbeye'].join(bs);
fs.mkdirSync(W, {recursive: true});
const exe = t => ['..', '..', '..', 'int_' + t, 'build-rel', 'bin', 'wfc_rebuild.exe'].join(bs);   // relative to the step's work dir
const guns = [['pistol', 'Car'], ['rifle', 'Car2'], ['shotgun', 'Car3'], ['ionblaster', 'Truck'], ['launcher', 'Tank'], ['burst', 'Jet6']];
const out = [];
for (const [gun, chassis] of guns) for (const [arm, t] of [['base', 'ssb'], ['new', 'wb2']]) {
  const r = W + bs + 'fire_' + gun + '_' + arm; fs.mkdirSync(r + bs + 'shots', {recursive: true});
  const e = ['WFC_BOOT=match', 'WFC_SKIPINTRO=1', 'WFC_NOMOUSE=1', 'WFC_AUTODETECT=0', 'WFC_LOCKSTEP=1', 'WFC_SEED=7', 'WFC_FLOWSEED=7',
    'WFC_RT_MODE=0', 'WFC_SMOKE_FRAMES=2000000', 'WFC_MATCH_SECONDS=20', 'WFC_CHASSIS=' + chassis, 'WFC_AUTOFIRE=1',
    'WFC_MATCH_URL=MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=0?BotsEnemy=0?PointsToWin=1000', 'WFC_SHOTMATCH=' + r + bs + 'shots,600,900,4'];
  out.push([r, e.join('^^'), exe(t), r + bs + 'run.log', '240'].join('|'));
}
fs.writeFileSync(W + bs + 'steps_eye.txt', out.join('\n') + '\n');
console.log(out.length + ' steps -> ' + W + bs + 'steps_eye.txt');
