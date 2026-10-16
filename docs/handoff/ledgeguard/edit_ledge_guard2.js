// Ledge guard tightening (on top of edit_ledge_guard.js): the probe point must be off the nav mesh by more than 1 m, and the drop must
// exceed max(2.5 m, 0.35 x the probe distance) (normal downslopes at speed are not drops). Run from the tree root.
const fs = require('fs');
function ed(f, a, b) {
  let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
  const n = s.split(a).length - 1; if (n !== 1) throw new Error(f + ' ' + n + ' ' + a.slice(0, 70));
  s = s.replace(a, () => b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/WorldBots.cpp', `                        if (botNav_.findCell(q, 0.0f) >= 0) continue;
                        float gq; core::Vec3 nq;
                        const bool floorNear = collision()->groundHeight(q.x, q.z, gy0 + 1.0f, 0.0f, gq, nq) && gq >= gy0 - 2.5f;`,
`                        if (botNav_.findCell(q, 1.0f) >= 0) continue;   // within 1 m of the mesh: not past a ledge
                        const float drop = std::max(2.5f, 0.35f * sp * t);   // a normal downslope at speed is not a drop
                        float gq; core::Vec3 nq;
                        const bool floorNear = collision()->groundHeight(q.x, q.z, gy0 + 1.0f, 0.0f, gq, nq) && gq >= gy0 - drop;`);
console.log('ok');
