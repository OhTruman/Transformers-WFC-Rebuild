// READY, NOT YET APPLIED: apply once Systems has compiled TR_Death_Gibs_p.Materials.DeathSkelGib_Mat_INST / DeathGib_Mat_INST
// (RB_GibExtra_CLR / MASK / NORM) into the MP maps' materials_glsl.json. The gib glTFs carry only colour factors (the crop slot showed
// the skeletal gibs flat lavender and the chunks plain grey); naming the compiled material lets the WFC pipeline draw the original's
// material, as the barrier / mine models do. Run from a tree root after edit_skeletal_gibs.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.cpp', [
    // Skeletal gibs (GIB_Body / GIB_LowBody2): DeathSkelGib_Mat_INST.
    ['                if (assets::loadSkinnedGlb(assetRoot() + gibMeshPath(g), *m) && m->valid()) { resolveModelTextures(*m); skelGibModels_[g] = std::move(m); }\n',
     '                if (assets::loadSkinnedGlb(assetRoot() + gibMeshPath(g), *m) && m->valid()) {\n' +
     '                    for (render::Material& mt : m->mats) if (mt.wfcName.empty() && !mt.sourceName.empty()) mt.wfcName = "TR_Death_Gibs_p.Materials." + mt.sourceName;\n' +
     '                    resolveModelTextures(*m); skelGibModels_[g] = std::move(m);\n' +
     '                }\n'],
    // Static chunks (RB_Gib_01..11): DeathGib_Mat_INST.
    ['            if (assets::loadGlb(assetRoot() + nm, md)) { for (render::Material& M : md.mats) { M.tex = resolveTexture(M.baseColorUri);',
     '            if (assets::loadGlb(assetRoot() + nm, md)) { for (render::Material& M : md.mats) { if (M.wfcName.empty() && !M.sourceName.empty()) M.wfcName = "TR_Death_Gibs_p.Materials." + M.sourceName; M.tex = resolveTexture(M.baseColorUri);'],
]);
console.log('ok');
