// Patch 2 checks. node mkp2.js headless
// headless: BOTTEST seeds 1-12 on int_p2 (Systems' env, null render) + SIMHASH seed 7 on int_p2 and the base int_p2b (phases 1-2).
const fs = require('fs');
const bs = String.fromCharCode(92);
const mode = process.argv[2];
const P = ['F:', 'Transformers Rebuild', 'Rebuild-Gameplay', 'work'].join(bs);
const W = [P, 'pass29', 'p2'].join(bs);
fs.mkdirSync(W, {recursive: true});
const exe = t => ['..', '..', '..', 'int_' + t, 'build-rel', 'bin', 'wfc_rebuild.exe'].join(bs);   // relative to the step's work dir (the runner joins them)
const env = s => 'WFC_MUTE=1;WFC_NOINTERACT=1;WFC_NOMOUSE=1;WFC_AUTODETECT=0;WFC_SEED=' + s + ';WFC_FLOWSEED=' + s +
    ';WFC_GPULOST_DIALOG_S=0;WFC_NULLRENDER=1;WFC_BOTTEST=1';
const out = [];
if (mode === 'headless') {
  for (let s = 1; s <= 12; ++s) {
    const run = W + bs + 'run_bt_s' + s; fs.mkdirSync(run, {recursive: true});
    out.push([run, env(s) + (s === 7 ? ';WFC_SIMHASH=1' : ''), exe('p2'), W + bs + 'bt_s' + s + '.log', '1800'].join('|'));
  }
  const run = W + bs + 'run_base_s7'; fs.mkdirSync(run, {recursive: true});
  out.push([run, env(7) + ';WFC_SIMHASH=1', exe('p2b'), W + bs + 'base_s7.log', '1800'].join('|'));
}
fs.writeFileSync(W + bs + 'steps_' + mode + '.txt', out.join('\n') + '\n');
console.log(out.length + ' steps -> ' + W + bs + 'steps_' + mode + '.txt');
