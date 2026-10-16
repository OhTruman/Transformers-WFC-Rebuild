// Static-pose corpse checks. node mkcs.js headless|crops
// headless: BOTTEST seeds 1-12 on int_csn (Systems' env) + seed 7 with SIMHASH (phases 1-2).
// crops: WFC_CORPSESTAGE on Streets seed 7, camera 10 m / 40 m, head (int_csb) vs patch (int_csn), frames 280-1560 every 20.
const fs = require('fs');
const bs = String.fromCharCode(92);
const mode = process.argv[2];
const P = ['F:', 'Transformers Rebuild', 'Rebuild-Gameplay', 'work'].join(bs);
const W = [P, 'pass29', 'c45'].join(bs);
fs.mkdirSync(W, {recursive: true});
const exe = (t, depth) => Array(depth).fill('..').concat(['int_' + t, 'build-rel', 'bin', 'wfc_rebuild.exe']).join(bs);
const out = [];
if (mode === 'headless') {
  for (let s = 1; s <= 12; ++s) {
    const run = W + bs + 'run_bt_s' + s; fs.mkdirSync(run, {recursive: true});
    out.push([run, 'WFC_NOMOUSE=1;WFC_AUTODETECT=0;WFC_SEED=' + s + ';WFC_FLOWSEED=' + s + ';WFC_GPULOST_DIALOG_S=0;WFC_NULLRENDER=1;WFC_BOTTEST=1' + (s === 7 ? ';WFC_SIMHASH=1' : ''),
              exe('csn', 3), W + bs + 'bt_s' + s + '.log', '1800'].join('|'));
  }
} else {
  const common = ['WFC_BOOT=match', 'WFC_SKIPINTRO=1', 'WFC_NOMOUSE=1', 'WFC_AUTODETECT=0', 'WFC_LOCKSTEP=1', 'WFC_SEED=7', 'WFC_FLOWSEED=7',
    'WFC_SIMHASH=0', 'WFC_RT_MODE=0', 'WFC_SMOKE_FRAMES=2000000', 'WFC_MATCH_SECONDS=45', 'WFC_CORPSESTAGE=1',
    'WFC_MATCH_URL=MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=3?BotsEnemy=4?PointsToWin=1000'];
  for (const [arm, t] of [['head', 'csb'], ['new', 'csn']]) for (const d of ['10', '40']) {
    const r = W + bs + 'crop_' + arm + '_' + d; fs.mkdirSync(r + bs + 'shots', {recursive: true});
    const e = common.concat(['WFC_CORPSESTAGE_DIST=' + d, 'WFC_SHOTMATCH=' + r + bs + 'shots,280,1560,20']);
    out.push([r, e.join('^^'), exe(t, 3), r + bs + 'run.log', '420'].join('|'));
  }
}
fs.writeFileSync(__dirname + '/steps_c45_' + mode + '.txt', out.join('\n') + '\n');
console.log(out.length + ' steps');
