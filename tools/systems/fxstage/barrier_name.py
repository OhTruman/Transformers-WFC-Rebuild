"""Barrier material fix: WEP_Barrier_SKEL.gltf's material has no extras.wfc_material, so the renderer drew the barrier
with the generic glTF fallback (an opaque lit surface) instead of the compiled WEP_Shield_p.Barrier.WEP_Barrier_Defrag_MATINST
(ENV_ForceField_Defrag_MAT: BLEND_Additive, unlit, distortion, depth-biased alpha - the original's see-through force
field). World::ensureAbilityModels names it from its package path. argv[1] = tree root."""
import sys
p = sys.argv[1].rstrip('/\\') + '/src/game/World.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = """        if (assets::loadSkinnedGlb(ext + "WEP_Barrier_SKEL.gltf", barrierModel_) && barrierModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_Barrier_ANIM.anim.gltf", barrierModel_);""".replace('\n', nl)
b = """        if (assets::loadSkinnedGlb(ext + "WEP_Barrier_SKEL.gltf", barrierModel_) && barrierModel_.valid()) {
            assets::loadAnimationsByName(ext + "WEP_Barrier_ANIM.anim.gltf", barrierModel_);
            // The export carries no extras.wfc_material for the barrier, so the renderer fell back to an opaque lit surface
            // (user 2026-10-14: the grid must be see-through). Its compiled material is the package's
            // WEP_Shield_p.Barrier.WEP_Barrier_Defrag_MATINST (ENV_ForceField_Defrag_MAT: BLEND_Additive, unlit, distortion,
            // depth-biased alpha), present in every MP map's materials_glsl.json.
            for (render::Material& mt : barrierModel_.mats)
                if (mt.wfcName.empty() && !mt.sourceName.empty()) mt.wfcName = "WEP_Shield_p.Barrier." + mt.sourceName;""".replace('\n', nl)
assert s.count(a) == 1, s.count(a)
open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
print('ok')
