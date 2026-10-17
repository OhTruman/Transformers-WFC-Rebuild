# Distortion shaders for BLEND_Modulate materials: shader-cache check (RE, 2026-10-18)

**Method.** In each package's ShaderCache exports, every material shader map is labelled `Material <path>`. The bytes that follow
list (shader-type FName, shader GUID) entries and vertex-factory types. A material has a compiled distortion pass in that cache when
its entry lists the pair **`TDistortionMeshPixelShaderFDistortMeshAccumulatePolicy` / `TDistortionMeshVertexShaderFDistortMeshAccumulatePolicy`**
(byte-granular FName search).
- Packages checked: MP_IAC_Streets_BASE_m, MP_UND_Gorge_BASE_m, MP_ORB_Debris_BASE_m, MP_ESC_BrokenHope_Base_m, plus the shared
  Startup_FRA / Startup_int.
- **Limit (found while checking):** **presence is proof** that the pass was cooked. **Absence is not.** The same scan finds no pair
  for the additive `Ring_Distort_Add_MAT` (bUsesDistortion True) in any of these packages, and finds `Rings_Distort_MAT`'s pair in
  Streets but not in Gorge / Debris / BrokenHope.
- Each cache lists only the shaders it adds (H: incremental per-package cooking against already-cooked caches). So a material's
  pair can sit in another package's cache.

## Results

| Material (blend) | Distortion accumulate pair found in | Reading |
|---|---|---|
| **ScreenSplatter_01_MAT** (Modulate, bUsesDistortion) | **Streets, Gorge, Debris, BrokenHope, Startup_FRA** | **CONFIRMED: modulate WITH a cooked distortion pass** |
| **Trail_Distort_MOD_MAT** (Modulate, bUsesDistortion) | **Streets, Gorge, Debris, BrokenHope** | **CONFIRMED: modulate WITH a cooked distortion pass** |
| BlastShell_MAT (Modulate, bUsesDistortion) | none (Debris, BrokenHope; Streets / Gorge have no entry) | not found: UNKNOWN (see Limit) |
| BlastSphere_Death_MAT (Modulate, bUsesDistortion) | none (all 4 maps + Startup_FRA) | not found: **downgraded from my earlier CONFIRMED "no distortion" to UNKNOWN / at most H** |
| Ring_Distort_Add_MAT (Additive), control | none anywhere checked | not found, although the material distorts, which proves the Limit |
| Rings_Distort_MAT (Translucent), control | Streets only | found once |

## Consequence for Rendering's merged rule (ef34a4f2)

**"No distortion program for BLEND_Modulate materials" is wrong as a blanket rule.**
- **ScreenSplatter_01_MAT** (the energon screen splatter, via EnergonSplatter_03_MAT_INST) and **Trail_Distort_MOD_MAT**
  (WarCry buff, Teleport_Char, DarkEnergon_Beam, door openers) have cooked distortion accumulate shaders, so the original draws their
  distortion.
- **For BlastSphere_Death_MAT and BlastShell_MAT** my evidence is only the absence of the pair, which this method can't prove.

**My earlier BlastSphere message overstated it. Correction:** the swirl's size problem is better treated as a strength / translation
question again (Rendering's open item: the vec2 `t11 × DistortionStr` vs scalar `× 20`, and the ×4 accumulate scale), not as "no pass".

**Recommendation:**
- Revert the blanket modulate rule.
- Keep the per-material evidence above.
- If a definite answer for BlastSphere / BlastShell is needed, the next step is to resolve the shader GUIDs a material's map
  references to the actual PS blobs across all cooked packages (≈1–2 h). That also gives the accumulate PS microcode for the strength
  question.
