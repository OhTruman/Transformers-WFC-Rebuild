// Hit-surface physical material for Systems' impact rule (package 7). AssetTools 2c1d9ba: manifests/maps/<MAP>/collision_weapon_physmat.json
// describes collision_weapon.glb: physmats[] (0 = none), per glb mesh runs [first_tri, count, physmat_index, rule], engine_default_physmat
// (ENV_PHYSMAT_p.Metal_PHYSMAT, Engine.DefaultPhysMaterialName), glb_sha256, node_overrides (none authored).
// - Load: the glb's sha256 must match (else skipped and logged); per mesh, the computed triangle count must equal the file's.
//   The runs expand to a per-triangle uint16 for the static weapon collision (the same submesh filter as the mover split: mover and
//   TnForcedDirVolume submeshes are not static triangles). Moving sets report index 0.
// - Query: CollisionWorld::segmentHit(a, b, t, n, &phys) also returns the hit triangle's index; physmatName(i) = its path, the engine
//   default for 0 / unknown (a hit on an unauthored surface reports Metal, as UE3).
// - Carry: ParticipantShot.physmat (other participants' shots) and hitPhysmat in the local fire path, next to the spawnImpact calls
//   Systems owns ("" when the shot hit a pawn / barrier / destructible / nothing).
// File lookup: <mapDir>/collision_weapon_physmat.json, else <manifest root>/maps/<MAP>/collision_weapon_physmat.json (the frontend's
// root: WFC_FRONTEND_MANIFESTS or the AssetTools default). Presentation data only: SIMHASH identical. Run from a tree root with
// src/core/Sha256.h copied from docs/handoff/physmat/Sha256.h.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.existsSync('src/core/Sha256.h')) throw new Error('copy docs/handoff/physmat/Sha256.h to src/core/ first');
ed('src/render/Mesh.h', [[
    '    int sourceSection = -1;           // primitive index within that mesh\n',
    '    int sourceSection = -1;           // primitive index within that mesh\n' +
    '    int gltfMesh = -1;                // glTF mesh index of the node (collision physmat runs are per mesh)\n' +
    '    uint32_t meshTriOffset = 0;       // first triangle of this primitive within that glTF mesh\n'
]]);
ed('src/assets/Gltf.cpp', [
    ['        const Json& prims = mesh["primitives"];\n',
     '        const Json& prims = mesh["primitives"];\n' +
     '        const int meshIdx = node["mesh"].asInt(0);\n' +
     '        uint32_t meshTri = 0;   // triangles of the earlier primitives of this mesh\n'],
    ['            out.subs.push_back(sm);\n',
     '            sm.gltfMesh = meshIdx; sm.meshTriOffset = meshTri; meshTri += (uint32_t)(idx.size() / 3);\n' +
     '            out.subs.push_back(sm);\n'],
]);
ed('src/game/Collision.h', [
    ['    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN) const;\n',
     '    // outPhys (optional): the hit triangle\'s physical material index (0 = none / a moving set); physmatName() resolves it.\n' +
     '    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN, uint16_t* outPhys = nullptr) const;\n' +
     '    // Per static triangle physical material indices (same order as the built triangles) and their names; index 0 / unknown =\n' +
     '    // engineDefault. Returns false (nothing set) when the count does not match the built triangles.\n' +
     '    bool setPhysmats(std::vector<uint16_t> perTri, std::vector<std::string> names, std::string engineDefault) {\n' +
     '        if (perTri.size() != tris_.size()) return false;\n' +
     '        triPhys_ = std::move(perTri); physNames_ = std::move(names); physDefault_ = std::move(engineDefault);\n' +
     '        return true;\n' +
     '    }\n' +
     '    const std::string& physmatName(uint16_t i) const {\n' +
     '        return i > 0 && i < physNames_.size() && !physNames_[i].empty() ? physNames_[i] : physDefault_;\n' +
     '    }\n' +
     '    // Diagnostic: a short ray into the centroid of `samples` static triangles with a material; returns how many hits report it\n' +
     '    // (outTested = rays that hit anything).\n' +
     '    int physmatSelfTest(int samples, int& outTested) const {\n' +
     '        outTested = 0; int ok = 0;\n' +
     '        if (triPhys_.size() != tris_.size() || tris_.empty()) return 0;\n' +
     '        const size_t stride = std::max<size_t>(1, tris_.size() / (size_t)std::max(1, samples));\n' +
     '        for (size_t i = 0; i < tris_.size() && outTested < samples; i += stride) {\n' +
     '            if (!triPhys_[i]) continue;\n' +
     '            const Tri& t = tris_[i];\n' +
     '            const core::Vec3 c = (t.a + t.b + t.c) * (1.0f / 3.0f);\n' +
     '            float ht; core::Vec3 hn; uint16_t hp = 0;\n' +
     '            if (!segmentHit(c + t.n * 0.02f, c - t.n * 0.02f, ht, hn, &hp)) continue;\n' +
     '            ++outTested; if (hp == triPhys_[i]) ++ok;\n' +
     '        }\n' +
     '        return ok;\n' +
     '    }\n'],
    ['    std::vector<Tri> tris_;\n',
     '    std::vector<Tri> tris_;\n' +
     '    std::vector<uint16_t> triPhys_;          // per static triangle physmat index (empty: none loaded)\n' +
     '    std::vector<std::string> physNames_;\n' +
     '    std::string physDefault_;\n'],
]);
ed('src/game/Collision.cpp', [
    ['    tris_.clear();\n    grid_.clear();\n',
     '    tris_.clear();\n    triPhys_.clear();\n    grid_.clear();\n'],
    ['bool CollisionWorld::segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN) const {\n',
     'bool CollisionWorld::segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN, uint16_t* outPhys) const {\n' +
     '    if (outPhys) *outPhys = 0;\n'],
    ['        if (std::fabs(tt - outT) < best) { best = std::fabs(tt - outT); outN = t.n; }\n',
     '        if (std::fabs(tt - outT) < best) {\n' +
     '            best = std::fabs(tt - outT); outN = t.n;\n' +
     '            if (outPhys) *outPhys = (size_t)ti < triPhys_.size() ? triPhys_[(size_t)ti] : 0;\n' +
     '        }\n'],
]);
ed('src/game/World.h', [[
    '    struct ParticipantShot { int player; std::string weapon; core::Vec3 from, to; bool impact; int hitPlayer = -1; int pellet = 0; };\n',
    '    struct ParticipantShot { int player; std::string weapon; core::Vec3 from, to; bool impact; int hitPlayer = -1; int pellet = 0;\n' +
    '                             std::string physmat; };   // the hit surface\'s PhysicalMaterial path ("" = not a world hit)\n'
]]);
ed('src/game/World.cpp', [
    ['#include <fstream>\n', '#include <fstream>\n#include "core/Sha256.h"\n'],
    ['            weaponCollision_.build(weaponStatic);\n',
     '            weaponCollision_.build(weaponStatic);\n' +
     '            loadWeaponPhysmats(mapDir, weaponColMesh);\n'],
    ['void World::startLocalMelee(bool whirlwind) {\n',
     '// collision_weapon_physmat.json (AssetTools) -> the static weapon collision\'s per-triangle physical materials (see Collision.h).\n' +
     'void World::loadWeaponPhysmats(const std::string& mapDir, const render::MeshData& glbMesh) {\n' +
     '    std::string path = mapDir + "collision_weapon_physmat.json", text;\n' +
     '    if (!readTextFile(path, text)) {\n' +
     '        const char* mr = std::getenv("WFC_FRONTEND_MANIFESTS");   // the frontend\'s manifest root (frontend::Catalog)\n' +
     '        path = std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests") + "/maps/" + mapName_ + "/collision_weapon_physmat.json";\n' +
     '        if (!readTextFile(path, text)) { LOG_INFO("physmat: no collision_weapon_physmat.json for %s (impacts use the defaults)", mapName_.c_str()); return; }\n' +
     '    }\n' +
     '    assets::Json root;\n' +
     '    if (!assets::Json::parse(text, root)) { LOG_WARN("physmat: cannot parse %s", path.c_str()); return; }\n' +
     '    const std::string want = root["glb_sha256"].asString(), have = core::sha256File(mapDir + "collision_weapon.glb");\n' +
     '    if (want.empty() || want != have) { LOG_WARN("physmat: %s describes another collision_weapon.glb (sha256 %s, loaded %s): skipped", path.c_str(), want.c_str(), have.c_str()); return; }\n' +
     '    std::vector<std::string> names;\n' +
     '    for (size_t i = 0; i < root["physmats"].size(); ++i) names.push_back(root["physmats"][i].asString());\n' +
     '    // Per glb mesh: the expanded per-triangle index (checked against the mesh triangle count we baked).\n' +
     '    std::map<int, std::vector<uint16_t>> perMesh;\n' +
     '    std::map<int, uint32_t> baked;\n' +
     '    for (const render::SubMesh& sm : glbMesh.subs) if (sm.gltfMesh >= 0) baked[sm.gltfMesh] = std::max(baked[sm.gltfMesh], sm.meshTriOffset + sm.indexCount / 3);\n' +
     '    // Alignment check: the file\'s source_mesh per glb mesh vs the StaticMesh path our bake recorded for that mesh index.\n' +
     '    int nameChecked = 0, nameMismatch = 0;\n' +
     '    for (const render::SubMesh& sm : glbMesh.subs) {\n' +
     '        if (sm.gltfMesh < 0 || sm.sourceMesh.empty()) continue;\n' +
     '        const std::string fileMesh = root["meshes"][std::to_string(sm.gltfMesh)]["source_mesh"].asString();\n' +
     '        if (fileMesh.empty()) continue;\n' +
     '        ++nameChecked; if (fileMesh != sm.sourceMesh) ++nameMismatch;\n' +
     '    }\n' +
     '    if (nameMismatch) { LOG_WARN("physmat: %d of %d submeshes name another source mesh than the file: skipped", nameMismatch, nameChecked); return; }\n' +
     '    int mismatched = 0;\n' +
     '    for (const auto& kv : baked) {\n' +
     '        const assets::Json& m = root["meshes"][std::to_string(kv.first)];\n' +
     '        std::vector<uint16_t> v(kv.second, 0);\n' +
     '        if ((uint32_t)m["triangles"].asInt(-1) != kv.second) { ++mismatched; perMesh[kv.first] = v; continue; }\n' +
     '        const assets::Json& runs = m["runs"];\n' +
     '        for (size_t r = 0; r < runs.size(); ++r) {\n' +
     '            const int first = runs[r][0].asInt(0), count = runs[r][1].asInt(0), idx = runs[r][2].asInt(0);\n' +
     '            for (int t = first; t < first + count && t >= 0 && (size_t)t < v.size(); ++t) v[(size_t)t] = (uint16_t)idx;\n' +
     '        }\n' +
     '        perMesh[kv.first] = std::move(v);\n' +
     '    }\n' +
     '    // The static triangles in build order: splitMovers\' filter (movers and TnForcedDirVolume submeshes are not static).\n' +
     '    const std::vector<std::string> movers = mapState_.moverActorNames();\n' +
     '    const bool keepForcedDir = std::getenv("WFC_NOFORCEDDIR") != nullptr;\n' +
     '    std::vector<uint16_t> perTri;\n' +
     '    for (const render::SubMesh& sm : glbMesh.subs) {\n' +
     '        if (std::find(movers.begin(), movers.end(), sm.nodeName) != movers.end()) continue;\n' +
     '        if (!keepForcedDir && sm.nodeName.rfind("TnForcedDirVolume", 0) == 0) continue;\n' +
     '        const std::vector<uint16_t>* v = sm.gltfMesh >= 0 && perMesh.count(sm.gltfMesh) ? &perMesh[sm.gltfMesh] : nullptr;\n' +
     '        for (uint32_t t = 0; t < sm.indexCount / 3; ++t) perTri.push_back(v && sm.meshTriOffset + t < v->size() ? (*v)[sm.meshTriOffset + t] : 0);\n' +
     '    }\n' +
     '    const size_t n = perTri.size();\n' +
     '    if (!weaponCollision_.setPhysmats(std::move(perTri), std::move(names), root["engine_default_physmat"].asString()))\n' +
     '        LOG_WARN("physmat: %zu triangles vs %zu built: skipped", n, weaponCollision_.triangleCount());\n' +
     '    else {\n' +
     '        LOG_INFO("physmat: %s: %zu weapon-collision triangles, %d mesh(es) with a triangle-count mismatch (engine default), %d submesh source names checked",\n' +
     '                 mapName_.c_str(), n, mismatched, nameChecked);\n' +
     '        if (std::getenv("WFC_PHYSMATLOG")) {\n' +
     '            int tested = 0; const int ok = weaponCollision_.physmatSelfTest(2000, tested);\n' +
     '            LOG_INFO("physmat self-test: %d of %d centroid rays report their triangle\'s material", ok, tested);\n' +
     '        }\n' +
     '    }\n' +
     '}\n\n' +
     'void World::startLocalMelee(bool whirlwind) {\n'],
    // The local fire path: capture the hit surface.
    ['        if (lineWorld.segmentHit(origin, end, t, n)) bestDist = range * t;\n' +
     '    }\n',
     '        if (lineWorld.segmentHit(origin, end, t, n, &hitPhys)) { bestDist = range * t; hitPhysWorld = &lineWorld; }\n' +
     '    }\n'],
    ['    float bestDist = range;\n    // World geometry.\n',
     '    float bestDist = range;\n    uint16_t hitPhys = 0; const CollisionWorld* hitPhysWorld = nullptr;   // the world hit\'s physical material\n    // World geometry.\n'],
    ['    core::Vec3 hitPoint = origin + dir * dist;\n    if (hitTarget) hitTarget->applyDamage(w.damageAt(dist));   // [CONF] range-based falloff\n',
     '    core::Vec3 hitPoint = origin + dir * dist;\n' +
     '    // [Systems] the hit surface\'s PhysicalMaterial for the impact rule ("" = a target / pawn / barrier / nothing was hit).\n' +
     '    const std::string hitPhysmat = (!hitTarget && !hitDes && !hitOpp && !hitBarrier && hitPhysWorld) ? hitPhysWorld->physmatName(hitPhys) : std::string();\n' +
     '    (void)hitPhysmat;\n' +
     '    if (hitTarget) hitTarget->applyDamage(w.damageAt(dist));   // [CONF] range-based falloff\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['    float bestDist = range;\n    if (collision_.valid()) {\n        float t; core::Vec3 n;\n',
     '    float bestDist = range;\n    uint16_t hitPhys = 0; const CollisionWorld* hitPhysWorld = nullptr;   // the world hit\'s physical material\n' +
     '    if (collision_.valid()) {\n        float t; core::Vec3 n;\n'],
    ['        if (lineWorld.segmentHit(origin, origin + dir * traceLen, t, n)) bestDist = traceLen * t;\n',
     '        if (lineWorld.segmentHit(origin, origin + dir * traceLen, t, n, &hitPhys)) { bestDist = traceLen * t; hitPhysWorld = &lineWorld; }\n'],
    ['    participantShots_.push_back({instigator, w.def ? w.def->id : "", origin, hitPoint, dist < range - 0.01f, hitPlayer, pellet_});\n',
     '    participantShots_.push_back({instigator, w.def ? w.def->id : "", origin, hitPoint, dist < range - 0.01f, hitPlayer, pellet_, std::string()});\n' +
     '    if (hitPhysWorld && !(hitDes || hitPlayer >= 0 || hitBarrier || hitSentry)) participantShots_.back().physmat = hitPhysWorld->physmatName(hitPhys);   // [Systems] impact rule\n' +
     '    static const bool physLog = std::getenv("WFC_PHYSMATLOG") != nullptr;   // diagnostic: every participant shot\'s hit surface\n' +
     '    if (physLog && participantShots_.back().impact)\n' +
     '        LOG_INFO("PHYSMAT p%d %s -> %s", instigator, w.def ? w.def->id : "?", participantShots_.back().physmat.empty() ? "(pawn / target)" : participantShots_.back().physmat.c_str());\n'],
]);
ed('src/game/World.h', [[
    '    void startLocalMelee(bool whirlwind);\n',
    '    void startLocalMelee(bool whirlwind);\n' +
    '    void loadWeaponPhysmats(const std::string& mapDir, const render::MeshData& glbMesh);\n'
]]);
console.log('ok');
