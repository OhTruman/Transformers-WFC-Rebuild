// Death-body patch 2, part 2: the robot skeletal gib (TnSkeletalGibSpawner.Spawn from TnDeathPlayer.Deactivate; RE
// GAMEPLAY_SKELETAL_GIB_INIT_RE): one per robot gib, GIB_Body or GIB_LowBody2 at 50 / 50, at PositionSocket (the corpse's pose at gib
// time) with the pawn's Rotation, in the gib mesh's reference pose, at rest; a GibRagdoll (world-only collision) for LifeSpan U(5, 15) s
// (then hidden: LifeSpanExpired, no fade); drawn GPU-skinned with the owner's colours (ApplyColorsToMesh); none for Melee / Poke
// (their custom skeletal gibs are empty) or vehicles (their hull is the skeletal gib). Its random choices come from a hash of the body
// seed, so the corpse's own frand() sequence is unchanged. Presentation only.
// Run from a tree root after edit_faithful_picker.js (needs src/game/GibRagdoll.{h,cpp} + GibRagdollTable.inc).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}

ed('src/game/World.h', [
    ['#include "assets/SkinnedModel.h"\n', '#include "assets/SkinnedModel.h"\n#include "game/GibRagdoll.h"\n'],
    ['        bool instaGib = false, noGibs = false, noSkelGib = false, noDecal = false;   // ShouldInstaGib; the entry\'s custom gibs',
     '        bool instaGib = false, noGibs = false, noSkelGib = false, noDecal = false;   // ShouldInstaGib; the entry\'s custom gibs\n' +
     '        // The robot skeletal gib (one ragdoll, after the gib): its model, physics, life, and GPU-skin draw state.\n' +
     '        std::unique_ptr<GibRagdoll> skelGib; const assets::SkinnedModel* skelGibModel = nullptr; float skelGibAge = 0.0f, skelGibLife = 0.0f;\n' +
     '        std::vector<core::Mat4> skelGibGlob, skelGibPalette; std::unique_ptr<uint8_t> skelGibKey = std::make_unique<uint8_t>(0); unsigned skelGibSerial = 0;\n' +
     '        std::string socketBone; core::Mat4 socketLocal;   // the robot\'s PositionSocket (bone + local), for the gib location'],
    ['        core::Vec3 hitDir{}; bool haveHitDir = false;   // TearOffMomentum direction of the killing hit (lastHitDir_)',
     '        core::Vec3 hitDir{}; bool haveHitDir = false;   // TearOffMomentum direction of the killing hit (lastHitDir_)\n' +
     '        std::string socketBone; core::Mat4 socketLocal;   // PositionSocket'],
    ['    std::map<std::string, std::unique_ptr<render::MeshData>> hullMeshes_;   // hull wreck glTFs by path (bind pose)\n',
     '    std::map<std::string, std::unique_ptr<render::MeshData>> hullMeshes_;   // hull wreck glTFs by path (bind pose)\n' +
     '    std::unique_ptr<assets::SkinnedModel> skelGibModels_[2];   // GIB_Body_SKEL, GIB_LowBody2_SKEL (loaded at the first robot gib)\n' +
     '    bool skelGibModelsTried_ = false;\n' +
     '    unsigned skelGibSerial_ = 0;\n'],
]);

const W = 'src/game/World.cpp';
ed(W, [
    ['#include "game/GpuSkin.h"\n', '#include "game/GpuSkin.h"\n#include "game/GibRagdoll.h"\n'],
    // The request carries the robot's PositionSocket.
    ['    q.pos = pawn.actorLocation(); q.vel = pawn.velocity(); q.fwd = core::forwardFromYawPitch(pawn.yaw(), 0.0f);\n',
     '    q.pos = pawn.actorLocation(); q.vel = pawn.velocity(); q.fwd = core::forwardFromYawPitch(pawn.yaw(), 0.0f);\n' +
     '    if (pawn.chassis().positionSocket.valid) { q.socketBone = pawn.chassis().positionSocket.bone; q.socketLocal = pawn.chassis().positionSocket.local; }\n'],
    ['    c.pos = q.pos; c.vel = q.vel; c.chassis = q.chassis;\n',
     '    c.pos = q.pos; c.vel = q.vel; c.chassis = q.chassis; c.socketBone = q.socketBone; c.socketLocal = q.socketLocal;\n'],
    // Spawn at the gib, after the decal (TnRobotForm.OnPlayGib order: gibs DSYS, skeletal gibs, decal: all at the same instant).
    ['    // TnSkeletalGibSpawner (vehicles):',
     '    // TnSkeletalGibSpawner (robots): one of the two templates, at PositionSocket in the gib mesh\'s reference pose, at rest.\n' +
     '    if (!c.vehicle && !c.noSkelGib && renderer_) {\n' +
     '        if (!skelGibModelsTried_) {\n' +
     '            skelGibModelsTried_ = true;\n' +
     '            for (int g = 0; g < 2; ++g) {\n' +
     '                std::unique_ptr<assets::SkinnedModel> m = std::make_unique<assets::SkinnedModel>();\n' +
     '                if (assets::loadSkinnedGlb(assetRoot() + gibMeshPath(g), *m) && m->valid()) { resolveModelTextures(*m); skelGibModels_[g] = std::move(m); }\n' +
     '                else LOG_INFO("skeletal gib %s: not loaded", gibMeshPath(g));\n' +
     '            }\n' +
     '        }\n' +
     '        unsigned h = c.rng ^ 0x27d4eb2fu;\n' +
     '        auto hrand = [&h]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (float)(h >> 8) / 16777216.0f; };\n' +
     '        const int g = hrand() < 0.5f ? 0 : 1;   // Templates[Rand(2)]\n' +
     '        const float life = 5.0f + 10.0f * hrand();   // LifeSpan U(5, 15) (Brute\'s {15, 5}: the same range)\n' +
     '        if (skelGibModels_[g]) {\n' +
     '            core::Vec3 at = c.pos;   // Owner.Mesh.GetSocketWorldLocation(\'PositionSocket\'), the body\'s pose at the gib\n' +
     '            const int sn = c.socketBone.empty() || !c.model ? -1 : c.model->nodeByName(c.socketBone);\n' +
     '            if (sn >= 0 && (size_t)sn < c.glob.size()) { const core::Mat4 w = c.world * c.glob[(size_t)sn] * c.socketLocal; at = core::Vec3{w.m[12], w.m[13], w.m[14]}; }\n' +
     '            core::Mat4 rot = c.world; rot.m[12] = rot.m[13] = rot.m[14] = 0.0f;   // the pawn\'s Rotation (mesh yaw convention)\n' +
     '            size_t nb = 0, nj = 0;\n' +
     '            const GibBodyDef* bd = gibBodies(g, nb); const GibJointDef* jd = gibJoints(g, nj);\n' +
     '            std::unique_ptr<GibRagdoll> rd = std::make_unique<GibRagdoll>();\n' +
     '            if (rd->init(*skelGibModels_[g], core::Mat4::translate(at) * rot, bd, nb, jd, nj)) {\n' +
     '                c.skelGib = std::move(rd); c.skelGibModel = skelGibModels_[g].get(); c.skelGibAge = 0.0f; c.skelGibLife = life;\n' +
     '                c.skelGib->globals(c.skelGibGlob); buildPalette(*c.skelGibModel, c.skelGibGlob, c.skelGibPalette); c.skelGibSerial = ++skelGibSerial_;\n' +
     '            }\n' +
     '        }\n' +
     '    }\n' +
     '    // TnSkeletalGibSpawner (vehicles):'],
    // Tick: the ragdoll keeps the corpse alive until its LifeSpan.
    ['        if (!live) { corpses_.erase(corpses_.begin() + (long)i); continue; }   // removed (the original: TotalTime + 1 s; chunks / wreck live on)\n',
     '        if (c.skelGib) {\n' +
     '            c.skelGibAge += dt;\n' +
     '            if (c.skelGibAge >= c.skelGibLife) c.skelGib.reset();   // LifeSpanExpired: hidden, destroyed (no fade)\n' +
     '            else {\n' +
     '                live = true;\n' +
     '                if (!c.skelGib->asleep()) {\n' +
     '                    c.skelGib->step(dt, collision_.valid() ? &collision_ : nullptr);\n' +
     '                    c.skelGib->globals(c.skelGibGlob); buildPalette(*c.skelGibModel, c.skelGibGlob, c.skelGibPalette); c.skelGibSerial = ++skelGibSerial_;\n' +
     '                }\n' +
     '            }\n' +
     '        }\n' +
     '        if (!live) { corpses_.erase(corpses_.begin() + (long)i); continue; }   // removed (the original: TotalTime + 1 s; chunks / wreck live on)\n'],
    // Draw: GPU-skinned, world-space palette (world identity), the owner's colours.
    ['        if (c.hull) {\n            r.setDrawOwner(c.owner);\n',
     '        if (c.skelGib && c.skelGibModel && !c.skelGibPalette.empty()) {\n' +
     '            r.setDrawOwner(c.owner);\n' +
     '            if (c.owner) r.setCharacterColors(render::CharacterColors{});\n' +
     '            render::MeshData& bind = bindMeshOf(*c.skelGibModel); syncMats(bind.mats, c.skelGibModel->mats);\n' +
     '            drawSkinnedGpu(r, bind, c.skelGibModel->joints, c.skelGibModel->weights, c.skelGibPalette, nullptr, 1.0f, core::Mat4{}, core::Vec3{1, 1, 1},\n' +
     '                           c.skelGibKey.get(), c.skelGibSerial, 0);\n' +
     '            r.setDrawOwner(0);\n' +
     '        }\n' +
     '        if (c.hull) {\n            r.setDrawOwner(c.owner);\n'],
]);
console.log('ok');
