// Death bodies: skin only while the pose changes. A corpse's pose is static once its death clip has ended (or it has none) and the
// blend-in is done; from then on tickCorpses skips samplePose / blendPose / skinPose and drawCorpses draws the cached vertex buffer
// (drawDynamicMeshPosed with a pose serial that changes only on a re-skin; the serials are unique across corpses, so a corpse moved
// in corpses_ never reuses another's buffer). Same look; no per-frame CPU skin or upload for a still body. Run from the tree root.
const fs = require('fs');
function ed(f, a, b) {
  let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
  const n = s.split(a).length - 1; if (n !== 1) throw new Error(f + ' ' + n + ' ' + a.slice(0, 70));
  s = s.replace(a, () => b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', `        unsigned rng = 1u;`,
`        unsigned rng = 1u;
        bool poseStatic = false; uint64_t poseSerial = 0; // the pose stopped changing (clip ended, blend done): skinned once, drawn cached`);
ed('src/game/World.h', `    std::vector<Corpse> corpses_;`, `    std::vector<Corpse> corpses_;
    uint64_t corpsePoseSerial_ = 0;                        // unique pose serials for drawDynamicMeshPosed (corpses move in corpses_)`);
ed('src/game/World.cpp', `        if (!c.gibbed) {
            if (c.clip >= 0) {`, `        if (!c.gibbed && !c.poseStatic) {
            if (c.clip >= 0) {`);
ed('src/game/World.cpp', `            assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            if (c.fxAt >= 0.0f && c.t >= c.fxAt) {`, `            assets::skinPose(*c.model, c.pose, c.glob, c.mesh);
            c.poseSerial = ++corpsePoseSerial_;
            const bool clipDone = c.clip < 0 || c.t >= c.model->clips[(size_t)c.clip].duration;
            if (clipDone && c.t >= c.blendIn) c.poseStatic = true;   // the last change: skinned, cached from here on
        }
        if (!c.gibbed) {
            if (c.fxAt >= 0.0f && c.t >= c.fxAt) {`);
ed('src/game/World.cpp', `        r.drawDynamicMesh(c.mesh, c.world, core::Vec3{1, 1, 1});
        if (c.defrag) r.setDrawEnergyDeath(-1.0f);`, `        r.drawDynamicMeshPosed(c.mesh, c.world, core::Vec3{1, 1, 1}, c.poseSerial);   // cached while the pose is static
        if (c.defrag) r.setDrawEnergyDeath(-1.0f);`);
console.log('ok');
// The hull wreck: the shared bind-pose hull mesh (heap-held in hullMeshes_, a stable key) never changes - only its model matrix moves -
// so it is drawn cached with a constant serial (Rendering: drawDynamicMesh rebuilt and uploaded it every frame).
ed('src/game/World.cpp', `            r.drawDynamicMesh(*c.hull, core::Mat4::translate(c.hullPos - c.pos) * c.world, core::Vec3{1, 1, 1});`,
`            r.drawDynamicMeshPosed(*c.hull, core::Mat4::translate(c.hullPos - c.pos) * c.world, core::Vec3{1, 1, 1}, 1u);   // bind mesh, never re-posed`);
console.log('ok hull');
