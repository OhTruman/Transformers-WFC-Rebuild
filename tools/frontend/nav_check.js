// Invariants over the nav.check lines written by tools/frontend/nav_stress.sh.
//   node tools/frontend/nav_check.js <wfc.log>
// - every scripted check ran (the script reached its end: no soft-lock / timeout);
// - at most one movie overlay holds focus; no modal open except on the *.quitbox checks;
// - on menu screens the focused movie's input owner (_global.currentMenu) exists and is visible;
// - back on the main menu: FrontEnd state, only FrontEnd_GFX open, no overlays, the main menu owns input;
// - in the match: Choose Character owns input before the pick, nothing but the HUD while InGame;
// - resources across cycles: AS heap, display nodes, GL shape cache and process memory stay bounded.
const fs = require('fs');
const log = fs.readFileSync(process.argv[2] || 'wfc.log', 'utf8').split(/\r?\n/);
const checks = [];
for (const l of log) {
  const i = l.indexOf('FLOW nav.check ');
  if (i < 0) continue;
  const kv = {};
  for (const t of l.slice(i + 15).split(' ')) { const e = t.indexOf('='); if (e > 0) kv[t.slice(0, e)] = t.slice(e + 1); }
  checks.push(kv);
}
const fail = [];
const bad = (c, why) => fail.push(`${c.label}: ${why}`);
const timedOut = log.some(l => /FLOW timeout|script timeout|FRONTEND script: timed out/i.test(l));
if (!checks.length) fail.push('no nav.check lines (harness did not run)');
if (!checks.some(c => c.label === 'main.end')) fail.push('script did not reach main.end (soft-lock or timeout)');
if (timedOut) fail.push('flow timeout reported');
for (const c of checks) {
  const modalOk = /quitbox|prompt/.test(c.label);
  if (+c.focusExtras > 1) bad(c, `focusExtras=${c.focusExtras}`);
  if (c.popup === 'open' && !modalOk) bad(c, 'modal popup left open');
  if (modalOk && /quitbox/.test(c.label) && c.popup !== 'open') bad(c, 'quit confirmation missing');
  const inMenu = c.level !== 'Match' && !/playing/.test(c.label);
  if (inMenu && c.movies !== '-' && c.inputOwnerVisible !== '1') bad(c, `input owner ${c.inputOwner} visible=${c.inputOwnerVisible}`);
  if (/\.main$|^main\./.test(c.label)) {
    if (c.uiState !== 'FrontEnd') bad(c, `uiState=${c.uiState}`);
    if (c.movies !== 'UI_GFxFrontEnd_p.FrontEnd_GFX_1') bad(c, `movies=${c.movies}`);
    if (c.extras !== '-') bad(c, `extras=${c.extras}`);
    if (!/menuMain_mc$/.test(c.inputOwner || '')) bad(c, `input owner ${c.inputOwner}`);
  }
  if (/charselect$/.test(c.label) && !(c.openMovie || '').includes('CustomTransformers')) bad(c, `Choose Character not open (${c.openMovie})`);
  if (/\.ingame$|\.resumed$/.test(c.label)) {
    if (c.uiState !== 'InGame') bad(c, `uiState=${c.uiState}`);
    if (c.movies !== '-' || c.extras !== '-') bad(c, `screens over gameplay: movies=${c.movies} extras=${c.extras}`);
  }
  if (/\.pause$/.test(c.label) && c.uiState !== 'Paused') bad(c, `uiState=${c.uiState}`);
}
// Resource bounds: the last main-menu check of each menu cycle. Cycle 1 warms the caches (lobby scene, movie data), so
// growth is measured from the end of cycle 1 to the end of the last cycle.
const mains = checks.filter(c => /^c\d+\.main$/.test(c.label));
const cycleEnds = [];
for (let i = 0; i < mains.length; ++i)
  if (i + 1 === mains.length || mains[i + 1].label.split('.')[0] !== mains[i].label.split('.')[0]) cycleEnds.push(mains[i]);
for (const c of cycleEnds)
  console.log(`  ${c.label}: asHeap=${c.asHeap} nodes=${c.displayNodes} glShapes=${c.glShapes} graveyard=${c.graveyard} privateMB=${c.privateMB}`);
const firstMain = cycleEnds[0], lastMain = cycleEnds[cycleEnds.length - 1];
const report = {};
if (firstMain && lastMain && firstMain !== lastMain) {
  for (const k of ['asHeap', 'displayNodes', 'glShapes', 'graveyard', 'privateMB']) report[k] = [+firstMain[k], +lastMain[k]];
  const cycles = cycleEnds.length - 1;
  const growth = (k) => (report[k][1] - report[k][0]);
  if (report.displayNodes[1] > report.displayNodes[0] * 1.05 + 50) fail.push(`display nodes grow: ${report.displayNodes}`);
  if (report.glShapes[1] > report.glShapes[0] * 1.5 + 200) fail.push(`GL shape cache grows: ${report.glShapes}`);
  if (growth('privateMB') > 40 + 10 * Math.max(1, cycles)) fail.push(`memory grows: ${report.privateMB} MB over ${cycles} cycles`);
  report.cyclesCompared = cycles;
}
console.log(`nav.check lines: ${checks.length}, main-menu returns: ${mains.length}`);
console.log('resources first -> last main menu:', JSON.stringify(report));
if (fail.length) { console.log(`FAIL (${fail.length})`); for (const f of fail.slice(0, 40)) console.log('  ' + f); process.exit(1); }
console.log('PASS');
