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

## Follow-up (2026-10-18): GUID-level resolution across every cooked package. CONFIRMED

**Method (`tools/shadermap_scan.py`, read-only, no package cache):**
- It reads every `*_m` map package + Startup_FRA / Startup_int (**460 packages**, 83 contain a map for one of the materials below).
- In each ShaderCache export it parses the material shader-map records:
  - `Material <path>` + material GUID;
  - per vertex factory: VF FName, count, then count × **32-byte entries** = (shader type FName, shader GUID, type FName again).
- **Shader GUID → code:** in the same ShaderCache, a shader record is `type FName, GUID, u32 end offset, u32, u16`, then the blob
  (`0x102A1100` pixel / `0x102A1101` vertex). Verified on ScreenSplatter_01_MAT's DistortMesh PS (Streets, GUID e7b100a3…).
- Output: `work/q/smap_all.json` (not committed).

**Union over all packages:**

| Material (blend) | Packages with a shader map | Packages whose map lists the DistortMesh accumulate PS / VS | VFs with the pair |
|---|---|---|---|
| **BlastSphere_Death_MAT** (Modulate) | 43 | **0** | — |
| **BlastShell_MAT** (Modulate) | 41 | **2, both campaign** (D1_ORB_ScienceLabs_DGN_m, D2_COR_Gorge_03_Design_m) | Foliage, ParticleDynamicParameter, ParticleSubUVDynamicParameter |
| ScreenSplatter_01_MAT (Modulate) | 48 | **48** | Particle, ParticleInstancedMesh, HmParticleCloud_NoSubUV |
| Trail_Distort_MOD_MAT (Modulate) | 48 | **28** (incl. BrokenHope, Remnant, Streets, Gorge, Debris) | ParticleDynamicParameter, ParticleSubUVDynamicParameter, ParticleBeamTrailDynamicParameter, Foliage |
| Rings_Distort_MAT (Translucent) | 24 | 2 (Rust, Streets) | Foliage, GPUSkinDecal, GPUSkinMorph |
| Ring_Distort_Add_MAT (Additive) | 48 | **0** | — |

**Conclusions:**
1. **BlastSphere_Death_MAT has no distortion shader anywhere in the shipped game (C, 43 / 43 maps without).** The original cannot
   draw the Exp_Death_FX distortion swirl, so the earlier "no pass" answer stands, now proven. The vec2-vs-scalar strength question
   is moot for this material: skip its distortion.
2. **BlastShell_MAT:** the distortion pair exists only in two campaign sublevels, never in an MP package. **In MP (BrokenHope / Debris)
   it has no distortion pass (C).**
3. **ScreenSplatter_01_MAT and Trail_Distort_MOD_MAT do distort in MP (C).** They are the modulate exceptions.
4. **Ring_Distort_Add_MAT has no distortion pass in any package (C)**, although it authors bUsesDistortion. If our distortion
   accumulate constants were taken "from Ring_Distort_Add_MAT's microcode", that microcode is not a distortion PS. Re-derive them
   from a real DistortMesh PS (below).
5. **So the rule is not "by blend mode":** whether a material distorts is per material and per vertex factory, as cooked. The
   faithful rule for Rendering is **"draw distortion only for (material, VF) pairs whose cooked map lists the DistortMesh PS"**
   (export the list from `smap_all.json`). As blend-mode heuristics, both "modulate never distorts" and "bUsesDistortion always
   distorts" are wrong.

**A real DistortMesh accumulate PS (ScreenSplatter_01_MAT, Streets; C structure, literal values not decoded):**
- CTAB: `Texture2D_0` s0, `Texture2D_1` s1, `UniformVector_1` c3.
- It samples the two textures at `uv × c3.xy` and forms a 2-D offset from the material's Distortion (texture-driven, with
  `saturate` / clamp steps), then **KILL**s when its squared length is below a literal threshold (no-distortion pixels are discarded).
- It scales the offset by a literal, then writes **`oC0.xy = max(offset, 0)`, `oC0.zw = |min(offset, 0)|`**: the stock UE3 split of
  positive and negative offsets into separate channels for additive accumulation.
- The literal constants (c254 / c255: the scale and threshold) sit in the blob's literal block, which `xenosdis.py` does not yet
  print. Reading them is the next step if Rendering needs the exact accumulate scale (vs our ×4).

## CORRECTION (2026-10-18, later): validated parser. The 4a7ca06 union above is WRONG

- **What was wrong:** the parser attributed each material's per-VF lists to the label before them. In the cooked layout a
  record's VF lists come **before** its own `Material <path>` label (`[VF blocks…][FString label][GUID]…`, the last VF key carrying
  no count).
- **The fix:** `tools/shadermap_scan.py` now accepts only the contiguous VF chain that ends exactly at the label's length prefix.
- **Validation (C):** over the Streets and BrokenHope caches, **0** materials without `bUsesDistortion` get a DistortMesh pair, and
  **every** bUsesDistortion material does (Streets 24 / 24, BrokenHope 29 / 29). Over all 233 packages with a pair, **0** pairs land
  on a material whose master lacks bUsesDistortion.

**Corrected results (C):**
- **BlastSphere_Death_MAT, BlastShell_MAT, Ring_Distort_Add_MAT, Rings_Distort_MAT, ScreenSplatter_01_MAT and Trail_Distort_MOD_MAT
  all cook the DistortMesh accumulate pair.** All six are on all 10 MP maps, except BlastShell (BrokenHope, Debris).
- **So whether a material distorts follows `bUsesDistortion`**, irrespective of blend mode. No allow-list is needed. The export is
  kept for the record: `work/handoff/distortion_allowlist.json` (schema line inside; per package + per MP map, master paths) and
  `work/handoff/distortion_allowlist/<MAP>.json`.

**BlastSphere_Death_MAT's real distortion PS** (Streets, FParticleDynamicParameterVertexFactory, GUID 3a518ccd…; C, literals
c252 = (−255, 10, 0, −2), c253 = (1/255, 4, 255, 1e-4), c254 = (2, 1.5, 0.001, −1), c255 = (3, 0.25, 1, 0.1)):
- n = tex × 2 − 1;
- p = ((1 − |n|) × 3)², with the × 3 / × 0.25 pattern terms;
- × sat(1.5 × dot(cameraVector, Z));
- × DepthBiasedAlpha (UniformScalar_1 = the depth bias scale);
- × **UniformScalar_2** (20 in the material's uniform constants);
- → **one scalar s**, then **s × 4**, **kill if s² − 0.1 < 0**, **clamp ±255**, **× 1/255**, **oC0 = (max(s, 0), max(s, 0), |min(s, 0)|,
  |min(s, 0)|)**: the same value on both axes.
- **Our translation (scalar × 20 on both axes, × 4 accumulate, 0.1, ±255 / 255) matches it.**
- The ScreenSplatter / Trail_Distort_MOD PSs read earlier use the **same template literals** (× 4, 0.1, ±255, 1/255). Note: with
  the old parser their owner attribution is unreliable, but the template part is the same in every DistortMesh PS.

## The apply pass: FDistortionApplyScreenPixelShader (C, Engine.xxx ShaderCache record, blob 0x454D04 and an identical copy at 0x4FF41C)

- **CTAB:** `SceneColorTexture` s0, `AccumulatedDistortionTexture` s1. Literals c255 = (0.25, −0.25, …).
- **Code (complete):**
  ```
  acc = tex(AccumulatedDistortionTexture, uv)
  uv' = uv + (acc.xy − acc.zw) × (0.25, −0.25)
  oC0 = tex(SceneColorTexture, uv')
  ```
- **(a)** The scale is the constant **0.25 / −0.25** in texture space: no texel-size or resolution term. A saturated channel shoves by
  a quarter of the screen.
- **(d)** **No clamp, no depth comparison, no attenuation** in the apply. Foreground in front of a distorting surface is smeared too.
- **(b) / (c)** The accumulate target format / size and its blend state are set natively, not in the shaders. **H (stock UE3):**
  - full-resolution 8-bit RGBA (the light-attenuation / scratch surface);
  - **ONE / ONE additive**, so each channel saturates at 1.0;
  - this is consistent with the accumulate PS writing non-negative values ≤ 1 per channel (its × 1/255 after the ±255 clamp).

**Verdict:** the original pipeline is accumulate s × 4 → clamp ±255 → / 255 → additive 8-bit → apply (acc.xy − acc.zw) × (0.25, −0.25).
Rendering's model matches it end to end. BlastSphere_Death_MAT's input chain (scalar × 20, both axes) matches too. A swirl that looks
too big therefore comes from an **input** difference: the depth-biased alpha (scene depth vs particle depth, DepthBiasScale), the
particle vertex alpha / size, the SmokeTile texture or the view-angle term. It does not come from a missing or extra constant.

## Correction 2 (2026-10-18): vertex-factory names in this doc are shifted by one block

`shadermap_scan.py` read VF blocks as (VF, count, entries). The cooked layout is (count, entries, VF), with the entries before
their VF. Every **VF name** above is the previous block's, and the first block of each map was dropped.

**Unaffected:** material attribution and the which-material / which-package findings (re-validated with the fixed parser).
The allow-list's material set per MP map is unchanged.

**Corrected VF sets with the DistortMesh pair** (union over all packages, regenerated v3 allow-list via tools/distortion_allowlist.py):

| Material | VFs |
|---|---|
| ScreenSplatter_01_MAT | ParticleDynamicParameter, ParticleSubUVDynamicParameter, ParticleBeamTrailDynamicParameter |
| Trail_Distort_MOD_MAT | Particle, ParticleSubUV, ParticleBeamTrail |
| Rings_Distort_MAT | Particle, ParticleSubUV, ParticleBeamTrail |
| BlastSphere_Death_MAT / BlastShell_MAT | Local, Particle |
| World glass / force fields (e.g. ENV_ForceField_Fac_MAT) | Local, GPUSkin, GPUSkinMorph (was "Foliage, GPUSkinDecal, GPUSkinMorph") |

Use the v3 `distortion_allowlist.json` / `distortion_allowlist/<MAP>.json` for any per-VF gating.
