// Hazard (pain) volumes: the inside test was the half-space intersection of the brush's face planes, each oriented by the brush
// centroid. Right for convex brushes; a NON-convex brush (73 of 641, all campaign: a kill shell around a hollow play space, e.g.
// A4_ORB PhysicsVolume_6864, 10000 / s) with its centroid in the hollow flips faces, so the hollow tested "inside": the A4 player
// died at t 1 s at the chapter start. Convex brushes keep the exact plane test (versus / Escalation volumes are all convex: no
// change there); non-convex ones use a ray-crossing parity test over the brush's triangles (UE3 EncompassesPoint is the brush BSP).
// Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/World.h', [
    ['                          std::vector<Plane> planes; };   // outward normal n, offset w (inside: n.p <= w)',
     '                          std::vector<Plane> planes;      // outward normal n, offset w (inside: n.p <= w)' + NL +
     '                          bool concave = false;           // a non-convex brush: inside by ray-crossing parity over tris (bounds first)' + NL +
     '                          std::vector<core::Vec3> tris; core::Vec3 lo{0, 0, 0}, hi{0, 0, 0}; };'],
]);
ed('src/game/World.cpp', [
    ['            h.planes.push_back(HazardVolume::Plane{n, w});' + NL,
     '            h.planes.push_back(HazardVolume::Plane{n, w});' + NL +
     '            for (size_t k = 1; k + 1 < polys[p].size(); ++k) { h.tris.push_back(P(0)); h.tris.push_back(P(k)); h.tris.push_back(P(k + 1)); }' + NL],
    ['        hazards_.push_back(std::move(h));' + NL,
     '        {   // convex: every vertex on the inner side of every face plane; else the parity test (the centroid can lie outside the solid)' + NL +
     '            h.lo = h.hi = h.tris.empty() ? c : h.tris[0];' + NL +
     '            for (const core::Vec3& q : h.tris) {' + NL +
     '                h.lo = {std::min(h.lo.x, q.x), std::min(h.lo.y, q.y), std::min(h.lo.z, q.z)}; h.hi = {std::max(h.hi.x, q.x), std::max(h.hi.y, q.y), std::max(h.hi.z, q.z)};' + NL +
     '                for (const HazardVolume::Plane& pl : h.planes) if (core::dot(pl.n, q) > pl.w + 0.05f) h.concave = true;' + NL +
     '            }' + NL +
     '            if (!h.concave) h.tris.clear();' + NL +
     '        }' + NL +
     '        hazards_.push_back(std::move(h));' + NL],
    ['        bool in = !hazards_[i].planes.empty();' + NL +
     '        for (const HazardVolume::Plane& pl : hazards_[i].planes) if (core::dot(pl.n, p) > pl.w) { in = false; break; }' + NL,
     '        const HazardVolume& H = hazards_[i];' + NL +
     '        bool in = !H.planes.empty();' + NL +
     '        if (H.concave) {   // ray-crossing parity (a fixed skewed direction: no edge-aligned rays on axis-aligned brushes)' + NL +
     '            in = p.x >= H.lo.x && p.y >= H.lo.y && p.z >= H.lo.z && p.x <= H.hi.x && p.y <= H.hi.y && p.z <= H.hi.z;' + NL +
     '            if (in) {' + NL +
     '                const core::Vec3 d = core::normalize(core::Vec3{0.3137f, 0.9134f, 0.2593f});' + NL +
     '                int crossings = 0;' + NL +
     '                for (size_t t = 0; t + 2 < H.tris.size(); t += 3) {   // Moller-Trumbore, hits at distance > 0' + NL +
     '                    const core::Vec3 e1 = H.tris[t + 1] - H.tris[t], e2 = H.tris[t + 2] - H.tris[t];' + NL +
     '                    const core::Vec3 pv = core::cross(d, e2); const float det = core::dot(e1, pv);' + NL +
     '                    if (std::fabs(det) < 1e-9f) continue;' + NL +
     '                    const float inv = 1.0f / det; const core::Vec3 tv = p - H.tris[t];' + NL +
     '                    const float u = core::dot(tv, pv) * inv; if (u < 0.0f || u > 1.0f) continue;' + NL +
     '                    const core::Vec3 qv = core::cross(tv, e1); const float v = core::dot(d, qv) * inv;' + NL +
     '                    if (v < 0.0f || u + v > 1.0f) continue;' + NL +
     '                    if (core::dot(e2, qv) * inv > 0.0f) ++crossings;' + NL +
     '                }' + NL +
     '                in = (crossings & 1) != 0;' + NL +
     '            }' + NL +
     '        } else for (const HazardVolume::Plane& pl : H.planes) if (core::dot(pl.n, p) > pl.w) { in = false; break; }' + NL],
]);
console.log('ok');
