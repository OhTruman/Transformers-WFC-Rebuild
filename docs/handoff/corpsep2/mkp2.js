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
// crops (windowed, RT off, run with -EnvSep '^^'): base int_p2b vs patch int_p2, both with the stage hooks.
//   corpse_10: WFC_CORPSESTAGE (6 staged kills every 5 s: melee, tank shell, sniper, assault rifle, vehicle tank shell, KillZ), camera
//              10 m back; frames 280-2800 every 20 (the ragdolls collapse and sleep, the scorches, a melee kill without gibs, the
//              insta-gib).
//   deathcam_robot / deathcam_vehicle: WFC_DEATHCAMSTAGE: the local player killed at + 6 s; frames 200-1200 every 10.
if (mode === 'crops') {
  const common = ['WFC_BOOT=match', 'WFC_SKIPINTRO=1', 'WFC_NOMOUSE=1', 'WFC_AUTODETECT=0', 'WFC_LOCKSTEP=1', 'WFC_SEED=7', 'WFC_FLOWSEED=7',
    'WFC_SIMHASH=0', 'WFC_RT_MODE=0', 'WFC_SMOKE_FRAMES=2000000', 'WFC_CORPSETEST=1', 'WFC_MUZZLELOG=1',
    'WFC_MATCH_URL=MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=3?BotsEnemy=4?PointsToWin=1000'];
  const scenes = [
    ['corpse_10', ['WFC_MATCH_SECONDS=55', 'WFC_CORPSESTAGE=1', 'WFC_CORPSESTAGE_DIST=10'], '280,2800,20', '600'],
    ['deathcam_robot', ['WFC_MATCH_SECONDS=20', 'WFC_DEATHCAMSTAGE=robot'], '200,1500,10', '300'],
    ['deathcam_vehicle', ['WFC_MATCH_SECONDS=20', 'WFC_DEATHCAMSTAGE=vehicle'], '200,1500,10', '300'],
  ];
  for (const [scene, env, frames, to] of scenes) for (const [arm, t] of [['base', 'p2b'], ['new', 'p2']]) {
    const r = W + bs + 'crop_' + scene + '_' + arm; fs.mkdirSync(r + bs + 'shots', {recursive: true});
    const e = common.concat(env, ['WFC_SHOTMATCH=' + r + bs + 'shots,' + frames]);
    out.push([r, e.join('^^'), exe(t), r + bs + 'run.log', to].join('|'));
  }
}
// The transforming-actor visual check (Integration 2026-10-17, rides in the crop slot): a fixed camera 8 m along +X of a Rust door
// (TnTransformingWorldActor_6492) and a BrokenHope bridge (TnTransformingWorldActor_1476), glTF metres from
// mp_transforming_actors.json: are they drawn at all?
if (mode === 'crops') {
  const tw = [['transform_rust_door', 'MP_IAC_Rust', '-174.56,28.88,60.80,90,-10'],
              ['transform_brokenhope_bridge', 'MP_ESC_BrokenHope', '67.52,19.36,-44.80,90,-20']];
  for (const [scene, map, cam] of tw) {
    const r = W + bs + 'crop_' + scene; fs.mkdirSync(r + bs + 'shots', {recursive: true});
    const e = ['WFC_BOOT=match', 'WFC_SKIPINTRO=1', 'WFC_NOMOUSE=1', 'WFC_AUTODETECT=0', 'WFC_LOCKSTEP=1', 'WFC_SEED=7', 'WFC_FLOWSEED=7',
      'WFC_RT_MODE=0', 'WFC_SMOKE_FRAMES=2000000', 'WFC_MATCH_SECONDS=15', 'WFC_FIXEDCAM=' + cam,
      'WFC_MATCH_URL=' + map + '?GameModeTag=TDM?BotsFriendly=0?BotsEnemy=0?PointsToWin=1000', 'WFC_SHOTMATCH=' + r + bs + 'shots,200,800,100'];
    out.push([r, e.join('^^'), exe('p2'), r + bs + 'run.log', '240'].join('|'));
  }
}
fs.writeFileSync(W + bs + 'steps_' + mode + '.txt', out.join('\n') + '\n');
console.log(out.length + ' steps -> ' + W + bs + 'steps_' + mode + '.txt');
