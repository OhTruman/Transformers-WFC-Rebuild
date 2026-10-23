// Mover-attached nav cells re-linked where they come to rest (Integration 2026-10-23: A1's squad p2 stranded 30 s after the lift ride).
// translateCells moved the lift's cells but kept their portals to the top landing, so a bot on the landed lift had no way off it. Now
// BotNav::relinkCells drops the moved cells' portals to unmoved cells, adds a portal for every edge they share with an unmoved cell
// at rest (collinear in XZ within 0.2 m, the heights within 0.6 m, an overlap of 0.3 m or more), recomputes the pieces; and
// cpSyncMoverNav makes every bot replan after a move. Also (RE / Rendering 2026-10-23, A1 black opening): the lift's floor = its nav cells
// as a collision set riding with it (the InterpActor_13020 collision is the rim only, so the pawns fell to the bottom at the start), and
// a Move track's non-mover actors (TnTransformingWorldActor_6275, the pod) get setMapActorTransform. WorldCampaignMatinee.cpp from argv[2].
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/BotNav.h', [
    ['    void translateCells(const std::vector<int>& cells, const core::Vec3& d);\n',
     '    void translateCells(const std::vector<int>& cells, const core::Vec3& d);\n' +
     '    // After translateCells: the cells\' portals to unmoved cells are rebuilt from the edges they share at rest; returns the portal pairs made.\n' +
     '    int relinkCells(const std::vector<int>& cells);\n'],
]);
let c = fs.readFileSync('src/game/BotNav.cpp', 'utf8');
const crlf = c.includes('\r\n');
c = c.replace(/\r\n/g, '\n');
const fn =
'int BotNav::relinkCells(const std::vector<int>& ids) {\n' +
'    std::vector<uint8_t> moved(cells_.size(), 0);\n' +
'    for (int id : ids) if (id >= 0 && (size_t)id < cells_.size()) moved[(size_t)id] = 1;\n' +
'    // drop the portals between a moved and an unmoved cell (both sides)\n' +
'    for (size_t i = 0; i < cells_.size(); ++i) {\n' +
'        auto& ps = cells_[i].portals;\n' +
'        ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const Portal& p) { return moved[i] != moved[(size_t)p.to]; }), ps.end());\n' +
'    }\n' +
'    auto addPortal = [&](int from, int to, const core::Vec3& a, const core::Vec3& b) {\n' +
'        const Cell& c = cells_[(size_t)from];\n' +
'        core::Vec3 e = b - a; core::Vec3 n{e.z, 0.0f, -e.x};\n' +
'        const float l = std::sqrt(n.x * n.x + n.z * n.z); if (l > 1e-6f) { n.x /= l; n.z /= l; }\n' +
'        const core::Vec3 mid = (a + b) * 0.5f;\n' +
'        if ((c.centroid.x - mid.x) * n.x + (c.centroid.z - mid.z) * n.z > 0.0f) { n.x = -n.x; n.z = -n.z; }   // outward\n' +
'        cells_[(size_t)from].portals.push_back(Portal{to, a, b, std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z), n});\n' +
'    };\n' +
'    int made = 0;\n' +
'    for (int id : ids) {\n' +
'        if (id < 0 || (size_t)id >= cells_.size()) continue;\n' +
'        const Cell& m = cells_[(size_t)id];\n' +
'        std::vector<int> cand;\n' +
'        if (!grid_.empty()) {\n' +
'            const int ax = std::clamp((int)((m.bmin.x - 1.0f - gx0_) / gcell_), 0, gw_ - 1), bx = std::clamp((int)((m.bmax.x + 1.0f - gx0_) / gcell_), 0, gw_ - 1);\n' +
'            const int az = std::clamp((int)((m.bmin.z - 1.0f - gz0_) / gcell_), 0, gh_ - 1), bz = std::clamp((int)((m.bmax.z + 1.0f - gz0_) / gcell_), 0, gh_ - 1);\n' +
'            for (int z = az; z <= bz; ++z) for (int x = ax; x <= bx; ++x) for (int q : grid_[(size_t)z * (size_t)gw_ + (size_t)x]) cand.push_back(q);\n' +
'            std::sort(cand.begin(), cand.end()); cand.erase(std::unique(cand.begin(), cand.end()), cand.end());\n' +
'        } else for (size_t q = 0; q < cells_.size(); ++q) cand.push_back((int)q);\n' +
'        for (int q : cand) {\n' +
'            if (moved[(size_t)q]) continue;\n' +
'            const Cell& o = cells_[(size_t)q];\n' +
'            if (o.bmin.y > m.bmax.y + 0.6f || o.bmax.y < m.bmin.y - 0.6f) continue;\n' +
'            bool linked = false;\n' +
'            for (size_t i = 0; i < m.poly.size() && !linked; ++i) {\n' +
'                const core::Vec3 p = m.poly[i], r = m.poly[(i + 1) % m.poly.size()];\n' +
'                const float ex = r.x - p.x, ez = r.z - p.z, len = std::sqrt(ex * ex + ez * ez);\n' +
'                if (len < 0.3f) continue;\n' +
'                const float ux = ex / len, uz = ez / len;\n' +
'                for (size_t j = 0; j < o.poly.size() && !linked; ++j) {\n' +
'                    const core::Vec3 s = o.poly[j], t = o.poly[(j + 1) % o.poly.size()];\n' +
'                    auto off = [&](const core::Vec3& v) { return std::fabs((v.x - p.x) * uz - (v.z - p.z) * ux); };\n' +
'                    if (off(s) > 0.2f || off(t) > 0.2f) continue;   // not on the edge\'s line\n' +
'                    float s0 = (s.x - p.x) * ux + (s.z - p.z) * uz, t0 = (t.x - p.x) * ux + (t.z - p.z) * uz;\n' +
'                    if (s0 > t0) std::swap(s0, t0);\n' +
'                    const float lo = std::max(0.0f, s0), hi = std::min(len, t0);\n' +
'                    if (hi - lo < 0.3f) continue;\n' +
'                    const core::Vec3 a = p + (r - p) * (lo / len), b = p + (r - p) * (hi / len);\n' +
'                    if (std::fabs(a.y - (s.y + (t.y - s.y) * 0.5f)) > 0.6f) continue;\n' +
'                    addPortal(id, q, a, b); addPortal(q, id, b, a);\n' +
'                    linked = true; ++made;\n' +
'                }\n' +
'            }\n' +
'        }\n' +
'    }\n' +
'    // the moved cells\' links to unmoved cells are dropped; a moved cell left without a portal gets walk links both ways to the\n' +
'    // nearest unmoved cell at its height (<= 30 m, |dy| <= 1 m): A1\'s lift cells are 2 m patches inside the pod, no shared edge\n' +
'    {\n' +
'        std::vector<Link> keep;\n' +
'        for (const Link& L : links_) if (moved[(size_t)L.from] == moved[(size_t)L.to]) keep.push_back(L);\n' +
'        links_.swap(keep);\n' +
'        for (Cell& c : cells_) c.links.clear();\n' +
'        for (size_t i = 0; i < links_.size(); ++i) cells_[(size_t)links_[i].from].links.push_back((int)i);\n' +
'    }\n' +
'    for (int id : ids) {\n' +
'        if (id < 0 || (size_t)id >= cells_.size()) continue;\n' +
'        bool has = false;\n' +
'        for (const Portal& p : cells_[(size_t)id].portals) if (!moved[(size_t)p.to]) has = true;\n' +
'        if (has) continue;\n' +
'        const core::Vec3 m = cells_[(size_t)id].centroid;\n' +
'        int best = -1; float bd = 30.0f * 30.0f;\n' +
'        for (size_t q = 0; q < cells_.size(); ++q) {\n' +
'            if (moved[q]) continue;\n' +
'            const core::Vec3 o = cells_[q].centroid;\n' +
'            if (std::fabs(o.y - m.y) > 1.0f) continue;\n' +
'            const float d2 = (o.x - m.x) * (o.x - m.x) + (o.z - m.z) * (o.z - m.z);\n' +
'            if (d2 < bd) { bd = d2; best = (int)q; }\n' +
'        }\n' +
'        if (best < 0) continue;\n' +
'        for (int k = 0; k < 2; ++k) {\n' +
'            Link L{k == 0 ? id : best, k == 0 ? best : id, false, true, false, k == 0 ? m : cells_[(size_t)best].centroid, k == 0 ? cells_[(size_t)best].centroid : m};\n' +
'            L.walk = true;\n' +
'            links_.push_back(L);\n' +
'            cells_[(size_t)L.from].links.push_back((int)links_.size() - 1);\n' +
'        }\n' +
'        ++made;\n' +
'    }\n' +
'    computePieces();\n' +
'    return made;\n' +
'}\n\n';
const anchor = 'void BotNav::computePieces() {';
if (c.split(anchor).length !== 2) throw new Error('BotNav.cpp anchor');
c = c.replace(anchor, fn + anchor);
fs.writeFileSync('src/game/BotNav.cpp', crlf ? c.replace(/\n/g, '\r\n') : c);
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignMatinee.cpp'), 'src/game/WorldCampaignMatinee.cpp');
ed('src/game/Campaign.h', [
    ['    int pawnSet = -1;\n    std::vector<int> navCells;      // its mover-attached nav cells\n',
     '    int pawnSet = -1;\n    int floorSet = -1;              // its footprint as a walkable floor (a mover whose collision is the rim only: A1\'s lift)\n' +
     '    bool navMoving = false;         // its nav cells moved since the last relink\n' +
     '    std::vector<int> navCells;      // its mover-attached nav cells\n'],
]);
console.log('ok');
