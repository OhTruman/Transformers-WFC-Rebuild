// 300 fps sim plan (campaign matinee ends / transforming props, Escalation doors): TransformingActors::applyNavBlocks tested every
// box against every nav cell (A1: 6663 cells x 47 actors, 1.5-1.9 ms per call). The boxes now go into a coarse xz grid; each cell
// runs the SAME exact test against the boxes of the buckets its bounds touch (each box once per cell). A box that passes the test
// overlaps the cell by > 0.5 m on x and z, so it shares a bucket with the cell: the blocked set is identical. Run from the tree root.
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
ed('src/game/TransformingActors.cpp', [
    ['    std::vector<uint8_t> blocked(nav.cells().size(), 0);' + NL +
     '    for (const Actor& a : actors_) {' + NL +
     '        if (!a.collide || a.bridge) continue;   // a bridge deck is a walkable surface, not a wall' + NL +
     '        const float rest = a.desired ? a.length : 0.0f;' + NL +
     '        for (const Box& b : a.boxes) {' + NL +
     '            core::Vec3 lo, hi;' + NL +
     '            boxBounds(a, b, rest, lo, hi);' + NL +
     '            for (size_t c = 0; c < nav.cells().size(); ++c) {' + NL +
     '                const BotNav::Cell& cell = nav.cells()[c];' + NL +
     '                // A floor cell under the box: overlapping by > 0.5 m on both horizontal axes, its floor within the box\'s height span.' + NL +
     '                const float ox = std::min(hi.x, cell.bmax.x) - std::max(lo.x, cell.bmin.x), oz = std::min(hi.z, cell.bmax.z) - std::max(lo.z, cell.bmin.z);' + NL +
     '                if (ox > 0.5f && oz > 0.5f && cell.centroid.y > lo.y - 1.0f && cell.centroid.y < hi.y) blocked[c] = 1;' + NL +
     '            }' + NL +
     '        }' + NL +
     '    }' + NL,
     '    std::vector<uint8_t> blocked(nav.cells().size(), 0);' + NL +
     '    struct BB { core::Vec3 lo, hi; };' + NL +
     '    std::vector<BB> boxes;' + NL +
     '    for (const Actor& a : actors_) {' + NL +
     '        if (!a.collide || a.bridge) continue;   // a bridge deck is a walkable surface, not a wall' + NL +
     '        const float rest = a.desired ? a.length : 0.0f;' + NL +
     '        for (const Box& b : a.boxes) { BB bb; boxBounds(a, b, rest, bb.lo, bb.hi); boxes.push_back(bb); }' + NL +
     '    }' + NL +
     '    if (!boxes.empty()) {' + NL +
     '        // A coarse xz grid of the boxes (8 m buckets, at most 256 a side) over their union.' + NL +
     '        float gx0 = boxes[0].lo.x, gz0 = boxes[0].lo.z, gx1 = boxes[0].hi.x, gz1 = boxes[0].hi.z;' + NL +
     '        for (const BB& b : boxes) { gx0 = std::min(gx0, b.lo.x); gz0 = std::min(gz0, b.lo.z); gx1 = std::max(gx1, b.hi.x); gz1 = std::max(gz1, b.hi.z); }' + NL +
     '        const float gs = std::max(8.0f, std::max(gx1 - gx0, gz1 - gz0) / 256.0f);' + NL +
     '        const int nx = std::max(1, (int)std::ceil((gx1 - gx0) / gs) + 1), nz = std::max(1, (int)std::ceil((gz1 - gz0) / gs) + 1);' + NL +
     '        auto bx = [&](float x) { return std::clamp((int)std::floor((x - gx0) / gs), 0, nx - 1); };' + NL +
     '        auto bz = [&](float z) { return std::clamp((int)std::floor((z - gz0) / gs), 0, nz - 1); };' + NL +
     '        std::vector<std::vector<int>> grid((size_t)nx * (size_t)nz);' + NL +
     '        for (size_t k = 0; k < boxes.size(); ++k)' + NL +
     '            for (int z = bz(boxes[k].lo.z); z <= bz(boxes[k].hi.z); ++z)' + NL +
     '                for (int x = bx(boxes[k].lo.x); x <= bx(boxes[k].hi.x); ++x) grid[(size_t)z * (size_t)nx + (size_t)x].push_back((int)k);' + NL +
     '        std::vector<size_t> seen(boxes.size(), (size_t)-1);' + NL +
     '        for (size_t c = 0; c < nav.cells().size(); ++c) {' + NL +
     '            const BotNav::Cell& cell = nav.cells()[c];' + NL +
     '            if (cell.bmax.x < gx0 || cell.bmin.x > gx1 || cell.bmax.z < gz0 || cell.bmin.z > gz1) continue;   // no box can overlap it' + NL +
     '            for (int z = bz(cell.bmin.z); z <= bz(cell.bmax.z) && !blocked[c]; ++z)' + NL +
     '                for (int x = bx(cell.bmin.x); x <= bx(cell.bmax.x) && !blocked[c]; ++x)' + NL +
     '                    for (int k : grid[(size_t)z * (size_t)nx + (size_t)x]) {' + NL +
     '                        if (seen[(size_t)k] == c) continue;' + NL +
     '                        seen[(size_t)k] = c;' + NL +
     '                        const core::Vec3& lo = boxes[(size_t)k].lo; const core::Vec3& hi = boxes[(size_t)k].hi;' + NL +
     '                        // A floor cell under the box: overlapping by > 0.5 m on both horizontal axes, its floor within the box\'s height span.' + NL +
     '                        const float ox = std::min(hi.x, cell.bmax.x) - std::max(lo.x, cell.bmin.x), oz = std::min(hi.z, cell.bmax.z) - std::max(lo.z, cell.bmin.z);' + NL +
     '                        if (ox > 0.5f && oz > 0.5f && cell.centroid.y > lo.y - 1.0f && cell.centroid.y < hi.y) { blocked[c] = 1; break; }' + NL +
     '                    }' + NL +
     '        }' + NL +
     '    }' + NL],
]);
{   // <cmath> for floor / ceil
    const f = 'src/game/TransformingActors.cpp'; let s = fs.readFileSync(f, 'utf8');
    if (!s.includes('#include <cmath>')) { const nl = s.includes(CR + NL) ? CR + NL : NL; s = s.replace('#include <algorithm>', '#include <algorithm>' + nl + '#include <cmath>'); fs.writeFileSync(f, s); }
}
console.log('ok');
