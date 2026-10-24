// Playtest 3, Streets crater (Integration decision b, 2026-10-24): ENV_CraterUpNoRing_STAT collides per-poly (its render
// triangles; both sections are enable_collision) instead of its authored simple hull. The hull is a lens 8-10 m wider than the
// visible crater whose 1.1 m knife edge stands 0.4-0.8 m above the floor: our knee probe (no UE3 cylinder sweep) treats it as a
// wall far from anything visible. PC ADAPTATION (not the original collision), one mesh on one map, movement and weapon worlds.
// The faithful fix (the movement model ramping up walkable hull faces) is on the post-build list.
// WFC_CRATERPROBE=1 (diagnostic): per overridden placement, 16 knee-height rays (floor + 0.55 m) from 40 local units out toward
// the centre; logs the nearest first-hit distance from the centre in mesh-local units (the hull rim is at 27.7, the visible
// mesh at ~18-19.5). Run from the tree root.
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
const FN = [
'// PC ADAPTATION (playtest 3, Integration decision 2026-10-24): meshes that collide per-poly (their render triangles at every colliding',
'// placement of props.json) instead of their authored simple hull. One entry: Streets\' ENV_CraterUpNoRing_STAT, whose hull lens is',
'// 8-10 m wider than the visible crater and whose rim lip our movement model (no cylinder sweep) cannot ramp onto.',
'void World::applyPerPolyCollision(render::MeshData& m, const std::string& mapDir, bool recordProbe, bool apply) {',
'    static const struct { const char* map; const char* mesh; } kPerPoly[] = {{"MP_IAC_Streets", "ENV_IAC_Ground_p.Debris.ENV_CraterUpNoRing_STAT"}};',
'    for (const auto& e : kPerPoly) {',
'        if (mapName_ != e.map) continue;',
'        std::string text; assets::Json props;',
'        if (!readTextFile(mapDir + "props.json", text) || !assets::Json::parse(text, props)) { LOG_WARN("per-poly collision: %sprops.json unreadable", mapDir.c_str()); continue; }',
'        int removed = 0, added = 0;',
'        for (render::SubMesh& sm : m.subs) if (sm.sourceMesh == e.mesh && sm.indexCount > 0) {   // the simple hull',
'            if (recordProbe && !apply) for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) { const uint32_t v = m.indices[i]; perPolyTris_.push_back({m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}); }',
'            if (apply) { removed += (int)sm.indexCount / 3; sm.indexCount = 0; }',
'        }',
'        render::MeshData rm; std::string gltf;',
'        const assets::Json& list = props["props"];',
'        for (size_t i = 0; i < list.size(); ++i) {',
'            const assets::Json& p = list[i];',
'            if (p["mesh"].asString() != e.mesh || !p["collide"].asBool(false) || !p["block"].asBool(false)) continue;',
'            if (gltf.empty()) { gltf = p["gltf"].asString(); if (!assets::loadGlb(assetRoot() + "/../" + gltf, rm)) { LOG_WARN("per-poly collision: %s unavailable", gltf.c_str()); break; } }',
'            const assets::Json& g = p["gltf_matrix"];',
'            if (g.size() != 16) continue;',
'            std::array<float, 16> M{}; for (int k = 0; k < 16; ++k) M[(size_t)k] = g[(size_t)k].asFloat(0.0f);   // column-major (glTF)',
'            if (recordProbe) perPolyProbe_.push_back(M);',
'            if (!apply) continue;',
'            render::SubMesh sm; sm.indexOffset = (uint32_t)m.indices.size(); sm.nodeName = p["actor"].asString(); sm.sourceMesh = e.mesh;',
'            const uint32_t base = (uint32_t)(m.positions.size() / 3);',
'            for (size_t v = 0; v + 2 < rm.positions.size(); v += 3) {',
'                const float x = rm.positions[v], y = rm.positions[v + 1], z = rm.positions[v + 2];',
'                m.positions.push_back(M[0] * x + M[4] * y + M[8] * z + M[12]);',
'                m.positions.push_back(M[1] * x + M[5] * y + M[9] * z + M[13]);',
'                m.positions.push_back(M[2] * x + M[6] * y + M[10] * z + M[14]);',
'            }',
'            for (uint32_t ix : rm.indices) { m.indices.push_back(base + ix); if (recordProbe) perPolyTris_.push_back({m.positions[(base + ix) * 3], m.positions[(base + ix) * 3 + 1], m.positions[(base + ix) * 3 + 2]}); }',
'            sm.indexCount = (uint32_t)rm.indices.size(); added += (int)sm.indexCount / 3;',
'            m.subs.push_back(sm);',
'        }',
'        if (apply) LOG_INFO("per-poly collision: %s %s: simple hull %d triangles -> render %d triangles", e.map, e.mesh, removed, added);',
'    }',
'}',
'',
'// WFC_CRATERPROBE (diagnostic): knee-height rays toward each per-poly placement\'s centre; the nearest first hit in mesh-local units.',
'void World::perPolyProbe() const {',
'    render::MeshData cm; CollisionWorld only;   // the crater triangles alone (the hull or the render mesh), floors from the full world',
'    for (const core::Vec3& v : perPolyTris_) { cm.positions.push_back(v.x); cm.positions.push_back(v.y); cm.positions.push_back(v.z); cm.indices.push_back((uint32_t)cm.indices.size()); }',
'    { render::SubMesh sm; sm.indexCount = (uint32_t)cm.indices.size(); cm.subs.push_back(sm); }',
'    only.build(cm);',
'    for (const std::array<float, 16>& M : perPolyProbe_) {',
'        const float s = std::sqrt(M[0] * M[0] + M[2] * M[2]);   // the horizontal scale (local -> world)',
'        const core::Vec3 o{M[12], M[13], M[14]};',
'        int hits = 0, outside = 0; float outer = 0.0f;',
'        for (int k = 0; k < 16; ++k) {',
'            const float a = 6.2831853f * (float)k / 16.0f;',
'            const core::Vec3 far{o.x + std::cos(a) * 40.0f * s, o.y, o.z + std::sin(a) * 40.0f * s};',
'            float gy = o.y; core::Vec3 n;',
'            if (!collision_.groundHeight(far.x, far.z, o.y + 3.0f, 0.0f, gy, n)) gy = o.y;',
'            const core::Vec3 from{far.x, gy + 0.55f, far.z}, to{o.x, gy + 0.55f, o.z};',
'            float t;',
'            if (only.segmentHit(from, to, t)) { const float d = (1.0f - t) * 40.0f; ++hits; outer = std::max(outer, d); outside += d > 21.0f; }',
'        }',
'        LOG_INFO("CRATERPROBE at (%.1f %.1f %.1f): %d of 16 knee rays blocked, %d of them outside the visible rim (> 21 local units), outermost %.1f (hull rim 27.7, visible ~18-19.5)",',
'                 o.x, o.y, o.z, hits, outside, outer);',
'    }',
'}',
''].join(NL);
ed('src/game/World.cpp', [
    ['    if (!authored) assets::loadGlb(mapDir + "collision.glb", colMesh);' + NL,
     '    if (!authored) assets::loadGlb(mapDir + "collision.glb", colMesh);' + NL +
     '    perPolyProbe_.clear(); perPolyTris_.clear();' + NL +
     '    if (!campaignMap_) applyPerPolyCollision(colMesh, mapDir, true, !std::getenv("WFC_NOPERPOLY"));   // PC ADAPTATION: per-poly meshes (Streets\' crater); WFC_NOPERPOLY=1: the authored hulls (A/B)' + NL],
    ['        collision_.build(pawnStatic);' + NL,
     '        collision_.build(pawnStatic);' + NL +
     '        if (std::getenv("WFC_CRATERPROBE")) perPolyProbe();   // diagnostic' + NL],
    ['        if (assets::loadGlb(mapDir + "collision_weapon.glb", weaponColMesh)) {' + NL,
     '        if (assets::loadGlb(mapDir + "collision_weapon.glb", weaponColMesh)) {' + NL +
     '            if (!campaignMap_) applyPerPolyCollision(weaponColMesh, mapDir, false, !std::getenv("WFC_NOPERPOLY"));' + NL],
    ['void World::ensureHullMeshes() {' + NL, FN + NL + 'void World::ensureHullMeshes() {' + NL],
]);
ed('src/game/World.h', [
    ['    void ensureHullMeshes();' + NL,
     '    void ensureHullMeshes();' + NL +
     '    void applyPerPolyCollision(render::MeshData& m, const std::string& mapDir, bool recordProbe, bool apply);   // PC ADAPTATION (playtest 3 crater)' + NL +
     '    void perPolyProbe() const;   // WFC_CRATERPROBE' + NL +
     '    std::vector<std::array<float, 16>> perPolyProbe_; std::vector<core::Vec3> perPolyTris_;' + NL],
]);
console.log('ok');
