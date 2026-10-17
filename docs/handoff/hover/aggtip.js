const fs = require('fs'), path = require('path');
const root = process.argv[2];
const R = {};
for (const arm of (process.argv[3] || 'base,new').split(',')) for (const f of fs.readdirSync(path.join(root, arm))) {
  const m = f.match(/^navq_(.+)_(smart|classic)_s(\d+)\.log$/); if (!m) continue;
  const t = fs.readFileSync(path.join(root, arm, f), 'utf8');
  const tip = t.match(/NAVQ TIP .*hover bot-min ([\d.]+), tipped ([\d.]+)% of hover time, episodes (\d+) .* longest ([\d.]+) s/);
  const su = t.match(/NAVQ SUMMARY .*bot-min alive ([\d.]+).*off-mesh ([\d.]+)%.*stucks ([\d.]+) \/ bot-min/);
  if (!tip || !su) continue;
  const k = m[1].replace('MP_', '') + ' ' + m[2] + ' ' + arm;
  const r = R[k] || (R[k] = {n: 0, hov: 0, tipT: 0, ep: 0, lon: 0, off: [], st: []});
  r.n++; r.hov += +tip[1]; r.tipT += +tip[1] * +tip[2] / 100; r.ep += +tip[3]; r.lon = Math.max(r.lon, +tip[4]); r.off.push(+su[2]); r.st.push(+su[3]);
}
const mean = a => a.reduce((x, y) => x + y, 0) / a.length;
const sd = a => { const m = mean(a); return Math.sqrt(a.reduce((x, y) => x + (y - m) * (y - m), 0) / Math.max(1, a.length - 1)); };
console.log('map ai arm | runs | hover bot-min | tipped % | episodes / hover-min | longest s | off-mesh % (sd) | stucks / bot-min (sd)');
for (const k of Object.keys(R).sort()) {
  const r = R[k];
  console.log(`${k} | ${r.n} | ${r.hov.toFixed(1)} | ${(100 * r.tipT / r.hov).toFixed(2)} | ${(r.ep / r.hov).toFixed(2)} | ${r.lon.toFixed(2)} | ${mean(r.off).toFixed(2)} (${sd(r.off).toFixed(2)}) | ${mean(r.st).toFixed(2)} (${sd(r.st).toFixed(2)})`);
}
