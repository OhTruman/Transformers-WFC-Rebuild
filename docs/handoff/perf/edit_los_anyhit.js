// 300 fps sim plan (D1 sentries 0.35 ms mean at ~7 rays a step; every bot sight check): botLineOfSight only needs "is a-b blocked",
// but used the nearest-hit segmentHit (moving sets brute force to the nearest t, the static walk to the nearest t, then a normal
// search). CollisionWorld::segmentBlocked answers the same boolean - any moving-set triangle hit in [0, 1] (the same tests, the
// same set filters and IgnoreDynamicScope) or the static segmentAnyHit (walk<true>: "same boolean as segmentHit") - and stops at
// the first hit. Run from the tree root.
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
ed('src/game/Collision.h', [
    ['    bool segmentAnyHit(const core::Vec3& a, const core::Vec3& b) const;' + NL,
     '    bool segmentAnyHit(const core::Vec3& a, const core::Vec3& b) const;' + NL +
     '    // The boolean of the 4-argument segmentHit (moving sets + static geometry), stopping at the first hit: line-of-sight checks.' + NL +
     '    bool segmentBlocked(const core::Vec3& a, const core::Vec3& b) const;' + NL],
]);
ed('src/game/Collision.cpp', [
    ['bool CollisionWorld::segmentAnyHit(const core::Vec3& a, const core::Vec3& b) const { float t; return walk<true>(a, b, t); }' + NL,
     'bool CollisionWorld::segmentAnyHit(const core::Vec3& a, const core::Vec3& b) const { float t; return walk<true>(a, b, t); }' + NL + NL +
     '// The moving sets\' test of the 4-argument segmentHit (same filters, same triangle test, any t in [0, 1]), then the static any-hit.' + NL +
     'bool CollisionWorld::segmentBlocked(const core::Vec3& a, const core::Vec3& b) const {' + NL +
     '    const core::Vec3 d = b - a;' + NL +
     '    const core::Vec3 smin{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};' + NL +
     '    const core::Vec3 smax{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};' + NL +
     '    for (size_t si = 0; si < dyn_.size(); ++si) {' + NL +
     '        const DynamicSet& s = dyn_[si];' + NL +
     '        if (isIgnored(si)) continue;' + NL +
     '        if (!s.enabled || smax.x < s.bmin.x || smin.x > s.bmax.x || smax.y < s.bmin.y || smin.y > s.bmax.y ||' + NL +
     '            smax.z < s.bmin.z || smin.z > s.bmax.z) continue;' + NL +
     '        for (const Tri& t : s.world) {' + NL +
     '            core::Vec3 e1 = t.b - t.a, e2 = t.c - t.a, pv = core::cross(d, e2);' + NL +
     '            float det = core::dot(e1, pv);' + NL +
     '            if (std::fabs(det) < 1e-8f) continue;' + NL +
     '            float inv = 1.0f / det; core::Vec3 tv = a - t.a;' + NL +
     '            float u = core::dot(tv, pv) * inv; if (u < 0 || u > 1) continue;' + NL +
     '            core::Vec3 q = core::cross(tv, e1);' + NL +
     '            float v = core::dot(d, q) * inv; if (v < 0 || u + v > 1) continue;' + NL +
     '            float tt = core::dot(e2, q) * inv;' + NL +
     '            if (tt >= 0 && tt <= 1) return true;' + NL +
     '        }' + NL +
     '    }' + NL +
     '    return segmentAnyHit(a, b);' + NL +
     '}' + NL],
]);
ed('src/game/WorldBots.cpp', [
    ['    float t; core::Vec3 n;' + NL + '    return !lw.segmentHit(from, to, t, n);' + NL,
     '    return !lw.segmentBlocked(from, to);   // the boolean of segmentHit(from, to, t, n), first hit only' + NL],
]);
console.log('ok');
