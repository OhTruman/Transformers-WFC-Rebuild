// Camera follow-up eye check: node mkcam2.js -> work/pass29/cam2/steps_fire2.txt (4 windowed runs, ~3 min).
// emp:   Car + WFC_LOADOUT=EMPShotgun,... autofire from spawn: the new arm kicks (4 deg climb, FOV -1, shake 0.2); the base has no row.
// drive: Car transforms (frame 640), boosts into the wheeled Driving strategy (WFC_AUTOBOOST 700) and fires the vehicle weapon: the new
//        arm shakes without the pitch / FOV kick, the base kicks too.
// Base int_c2b (integration f886856b) vs new int_c2 (+ GAMEPLAY_CAMERA_FOLLOWUP.patch); both carry the harness-only WFC_LOADOUT.
const fs = require('fs');
const bs = String.fromCharCode(92);
const W = ['F:', 'Transformers Rebuild', 'Rebuild-Gameplay', 'work', 'pass29', 'cam2'].join(bs);
fs.mkdirSync(W, {recursive: true});
const exe = t => ['..', '..', '..', 'int_' + t, 'build-rel', 'bin', 'wfc_rebuild.exe'].join(bs);
const base = ['WFC_BOOT=match', 'WFC_SKIPINTRO=1', 'WFC_NOMOUSE=1', 'WFC_AUTODETECT=0', 'WFC_LOCKSTEP=1', 'WFC_SEED=7', 'WFC_FLOWSEED=7',
              'WFC_RT_MODE=0', 'WFC_SMOKE_FRAMES=2000000', 'WFC_MATCH_SECONDS=22', 'WFC_CHASSIS=Car', 'WFC_AUTOFIRE=1',
              'WFC_MATCH_URL=MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=0?BotsEnemy=0?PointsToWin=1000'];
const cases = [['emp', ['WFC_LOADOUT=EMPShotgun^^HeavyPistol^^FlashBangs'.replace(/\^\^/g, '|')], 600, 900],
               ['drive', ['WFC_AUTOTRANSFORM=640', 'WFC_AUTOBOOST=700', 'WFC_AUTOWALK=1'], 720, 1020]];
const out = [];
for (const [name, extra, f0, f1] of cases) for (const [arm, t] of [['base', 'c2b'], ['new', 'c2']]) {
    const r = W + bs + name + '_' + arm; fs.mkdirSync(r + bs + 'shots', {recursive: true});
    const e = base.concat(extra.map(x => x.replace(/\|/g, ','))).concat(['WFC_SHOTMATCH=' + r + bs + 'shots,' + f0 + ',' + f1 + ',4']);
    out.push([r, e.join('^^'), exe(t), r + bs + 'run.log', '240'].join('|'));
}
fs.writeFileSync(W + bs + 'steps_fire2.txt', out.join('\n') + '\n');
console.log(out.length + ' steps -> ' + W + bs + 'steps_fire2.txt');
