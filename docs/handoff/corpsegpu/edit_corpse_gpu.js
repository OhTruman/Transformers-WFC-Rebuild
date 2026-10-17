// Death bodies on the GPU-skinned character path (RENDERING_CORPSE_GPU_PATH.md; GAMEPLAY_CORPSE_GPU_IMPL.md): tickCorpses builds the
// joint globals (poseGlobals) and the bone palette (buildPalette) while the pose changes - no vertex skin - and drawCorpses draws through
// drawSkinnedGpu (queued: the shadow caster batches with live robots) with a heap-held per-body key (stable across moves in corpses_)
// and the pose serial. A body whose GPU draw is refused (bone rows full, > 128 joints, WFC_NOGPUSKIN) latches the CPU fallback: skinned
// on the next tick, drawn posed as before. Same state calls (owner, colours, Defrag). Presentation only. Run from the tree root.
const fs = require('fs');
function ed(f, a, b) {
  let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
  const n = s.split(a).length - 1; if (n !== 1) throw new Error(f + ' ' + n + ' ' + a.slice(0, 70));
  s = s.replace(a, () => b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', `        bool poseStatic = false; uint64_t poseSerial = 0; // the pose stopped changing (clip ended, blend done): skinned once, drawn cached`,
`        bool poseStatic = false; uint64_t poseSerial = 0; // the pose stopped changing (clip ended, blend done): skinned once, drawn cached
        std::unique_ptr<uint8_t> gpuKey = std::make_unique<uint8_t>(0);   // the GPU-skin palette key: stable for the body's life (moves in corpses_)
        std::vector<core::Mat4> palette;                  // global(j) x invBind(j) of the current pose (GPU path)
        mutable bool gpuOk = true;                        // false: the renderer refused the GPU draw; CPU-skinned from then on`);
// tick: globals + palette instead of the vertex skin; the CPU skin only on the fallback
ed('src/game/World.cpp', `            assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            c.poseSerial = ++corpsePoseSerial_;`, `            assets::poseGlobals(*c.model, c.pose, c.glob);          // joint globals (sockets, the palette); no vertex skin on the GPU path
            buildPalette(*c.model, c.glob, c.palette);
            if (!c.gpuOk) assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            c.poseSerial = ++corpsePoseSerial_;`);
ed('src/game/World.cpp', `        if (!c.gibbed) {
            if (c.fxAt >= 0.0f && c.t >= c.fxAt) {`, `        if (!c.gibbed && !c.gpuOk && c.mesh.positions.empty() && !c.glob.empty()) {   // fallback latched after the pose settled: skin once
            assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            c.poseSerial = ++corpsePoseSerial_;
        }
        if (!c.gibbed) {
            if (c.fxAt >= 0.0f && c.t >= c.fxAt) {`);
ed('src/game/World.cpp', `        if (c.gibbed || c.mesh.positions.empty()) continue;
        r.setDrawOwner(c.owner);`, `        if (c.gibbed || (c.gpuOk ? c.palette.empty() : c.mesh.positions.empty())) continue;
        r.setDrawOwner(c.owner);`);
ed('src/game/World.cpp', `        r.drawDynamicMeshPosed(c.mesh, c.world, core::Vec3{1, 1, 1}, c.poseSerial);   // cached while the pose is static`,
`        bool drawn = false;
        if (c.gpuOk) {   // the character path: queued, its shadow caster batched with live robots; the palette row is cached while the serial holds
            render::MeshData& bind = bindMeshOf(*c.model);
            syncMats(bind.mats, c.model->mats);
            drawn = drawSkinnedGpu(r, bind, c.model->joints, c.model->weights, c.palette, nullptr, 1.0f, c.world, core::Vec3{1, 1, 1},
                                   c.gpuKey.get(), c.poseSerial, 0);
            if (!drawn) c.gpuOk = false;   // refused: the next tick CPU-skins this body (nothing drawn this frame)
        }
        if (!drawn && !c.gpuOk && !c.mesh.positions.empty())
            r.drawDynamicMeshPosed(c.mesh, c.world, core::Vec3{1, 1, 1}, c.poseSerial);   // CPU fallback, cached while the pose is static`);
console.log('ok');
