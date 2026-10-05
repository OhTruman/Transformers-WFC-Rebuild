// Create a Character soak report (tools/frontend/customize_soak.sh): per-checkpoint state and growth across cycles.
// Usage: node tools/frontend/customize_soak.js wfc.log
const fs = require('fs');
const log = fs.readFileSync(process.argv[2] || 'wfc.log', 'utf8').split(/\r?\n/);
const checks = [];
for (const l of log) {
  const m = l.match(/FLOW nav\.check (.*)$/);
  if (!m) continue;
  const kv = {};
  for (const p of m[1].trim().split(/ (?=[A-Za-z]+=)/)) { const i = p.indexOf('='); kv[p.slice(0, i)] = p.slice(i + 1); }
  checks.push(kv);
}
const count = re => log.filter(l => re.test(l)).length;
const fails = [];
const want = (ok, what) => { if (!ok) fails.push(what); };
// Expectations per checkpoint kind.
for (const c of checks) {
  const lab = c.label || '';
  const pv = (c.preview || '0/0/0/0/0').split('/').map(Number);   // slots / visible / vehicles / meshes / bodies
  if (/\.(autobot|decepticon)$/.test(lab)) want(pv[1] === 1, `${lab}: one preview pawn visible (got ${pv[1]})`);
  if (/\.vehicle$/.test(lab)) want(pv[2] === 1, `${lab}: vehicle form (got ${pv[2]})`);
  if (/\.(overview|overview2)$/.test(lab)) want(pv[1] === 2 && pv[2] === 0, `${lab}: both pawns, robot form (got ${pv[1]} visible, ${pv[2]} vehicle)`);
  if (/\.(cac|list|overview|overview2|autobot|decepticon|vehicle|picker|picked)$/.test(lab) || /reopen/.test(lab))
    want(/PartyLobby/.test(c.openMovie || ''), `${lab}: customization stays in the party lobby movie (${c.openMovie})`);
  if (/\.picker$/.test(lab)) want(Number(c.stickCb) === 1, `${lab}: picker stick callback registered (got ${c.stickCb})`);
  if (/\.(picked|list)$/.test(lab)) want(Number(c.stickCb) === 0, `${lab}: stick callback released (got ${c.stickCb})`);
  if (/ingame$/.test(lab)) want(c.uiState === 'InGame', `${lab}: in game (uiState ${c.uiState})`);
}
// Growth: the same checkpoint across cycles (cycle 2 vs last) must not climb.
const series = {};
for (const c of checks) {
  const key = (c.label || '').replace(/^c\d+\./, 'c*.').replace(/^m\d+\./, 'm*.');
  (series[key] = series[key] || []).push(c);
}
const growth = [];
for (const [k, s] of Object.entries(series)) {
  if (s.length < 3) continue;
  const a = s[1], b = s[s.length - 1];
  for (const f of ['asHeap', 'displayNodes', 'asTimers', 'stickCb', 'glShapes']) {
    const va = Number(a[f]), vb = Number(b[f]);
    if (Number.isFinite(va) && Number.isFinite(vb) && vb > va * 1.05 + 2) growth.push(`${k} ${f} ${va} -> ${vb}`);
  }
  const pa = (a.preview || '').split('/').map(Number), pb = (b.preview || '').split('/').map(Number);
  if (pb[3] > pa[3] || pb[4] > pa[4]) growth.push(`${k} preview cache ${a.preview} -> ${b.preview}`);
  const ma = Number(a.privateMB), mb = Number(b.privateMB);
  if (ma && mb && mb > ma + 150) growth.push(`${k} privateMB ${ma} -> ${mb}`);
}
const gy = checks.map(c => Number(c.graveyard)).filter(Number.isFinite);
console.log(`checks ${checks.length}`);
console.log(`AS throws ${count(/GFX (interval|mouse listener|key listener)? ?threw|AVM1 .*threw/)}  GCCHECK uses ${count(/AVM1 GCCHECK/)}  missing fns ${count(/GFX missing function/)}`);
console.log(`graveyard first ${gy[0]} last ${gy[gy.length - 1]} (removed clips kept for stale references)`);
for (const k of ['main.start', 'c1.cac', 'c2.cac', `c${Math.max(...checks.map(c => Number((c.label || '').match(/^c(\d+)/)?.[1] || 0)))}.cac`, 'main.end']) {
  const c = checks.find(x => x.label === k);
  if (c) console.log(`${k.padEnd(10)} heap ${c.asHeap} nodes ${c.displayNodes} timers ${c.asTimers} grave ${c.graveyard} shapes ${c.glShapes} preview ${c.preview} MB ${c.privateMB}`);
}
for (const g of growth) console.log('GROWTH ' + g);
for (const f of fails) console.log('FAIL ' + f);
console.log(fails.length || growth.length ? `RESULT FAIL (${fails.length} checks, ${growth.length} growth)` : 'RESULT PASS');
