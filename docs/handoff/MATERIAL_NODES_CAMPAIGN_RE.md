# Campaign material nodes: If, Ddx/Ddy, CameraWorldPosition, LensFlare*, DecalMaterial (RE, 2026-10-18)

Labels: CONFIRMED (C) = cooked shader microcode / CTAB or authored data; HIGH (H) = stock UE3 semantics, not traced in WFC.
Method: `tools/shadermap_scan.py` (per-material shader maps; now also parses `DecalMaterial` labels) → shader GUIDs →
`tools/shader_by_guid.py` (blob, CTAB, literals, Xenos disassembly).

## 1. MaterialExpressionIf (C)

**Authored use:** Splash_Big_Clip_MAT and Splash_Big_Clip_02_MAT:
- A = texture alpha (TextureSampleParameter2D output 4 / 3), B = DynamicParameter;
- AGreaterThanB = Constant 1.0, AEqualsB = ALessThanB = Constant 0 (R unset);
- **no EqualsThreshold property exists in WFC's If** (only A, B and the three result inputs).

**Compiled** (Splash_Big_Clip_MAT base-pass PS, A5_TRY_BASE_m, FParticleDynamicParameterVertexFactory, GUID 26b4ae39…):
```
SETGTv  t1 = (A >  B)            ; r8.w
SETGTEv t2 = (A >= B)            ; r7.w
CNDGTEv r  = (-|t2| >= 0) ? Less : t1      ; t2 == 0 (A < B) → Less (literal 0); else (A > B ? Greater=1 : Equal=0) folded into t1
```
- This is the stock form **`(A >= B) ? (A > B ? Greater : Equal) : Less`**: **exact** `>` and `>=`, **no epsilon**. The constant results
  are folded into the compare (Greater = 1 → the SETGT result itself).
- **Translator:** emit `(A >= B) ? ((A > B) ? G : E) : L`, per component on the arithmetic type of G / L. Don't add a threshold.

## 2. Ddx / Ddy (C: present in graphs, compiled out of every shipped permutation)

**Authored:** TR_OmegaSupreme_Mat (Ddx + Ddy on a ComponentMask → two DotProducts, the usual mip-size math) and CHR_Vignette_Mat
(Ddx + Ddy on a texture channel → ShaderCode nodes).

**Cooked:** **no** derivative instruction in any of their pixel shaders. Checked:
- TR_OmegaSupreme_Mat: base pass in A3_COR_OmegaSupreme_m;
- CHR_Vignette_Mat: base + directional / spot / one-light-uber light passes in A1 / A2 / A3, on the Local, GPU-skin and GPU-skin-morph VFs (VF names corrected 2026-10-18, see §6).

On Xenos a derivative needs a `getGradients` fetch; none occurs (only `tfetch`, one explicit `setTextureLod` in Omega's). So the Ddx /
Ddy chains are unreachable in the cooked permutations (static switches / unconnected outputs).

**Translator:** map Ddx / Ddy to GLSL `dFdx` / `dFdy` (stock semantics, H) for completeness. Since no shipped shader evaluates them,
emitting `0.0` is also faithful for these materials.

## 3. CameraWorldPosition (C)

- **Authored:** ENV_ORB_CybertronPlanet_p.MATTE.CybertronPenumber_MAT (×2).
- **Compiled** (A4_ORB_Track1_Global_Art_m, base pass): the node reads the **PS constant `WorldCameraPosition` (c3.xyz)**, the
  absolute world-space camera position in UE units (**not** pre-view-translated). The shader adds it directly to authored absolute
  positions: literals 311150 / −31146.7 / 295801 … (the planet / penumbra centres), e.g. `c3.xyzy + c255`.
- `PreViewTranslation` (c4) is separate: the material's WorldPosition path uses it.
- **Translator:** `CameraWorldPosition` = the absolute camera position (UE units, Z up), the same value as WorldCameraPosition.

## 4. LensFlareIntensity / LensFlareOcclusion (C for the plumbing; H for the values)

**Plumbing:**
- The flare-node values are **not shader constants**. flare_sun_Mat's PS on **FLensFlareVertexFactory** (D3A_IAC_City1_ART_m, GUID
  e468bbcf…) has a CTAB of only Texture2D_0, UniformScalar_0, UniformVector_0 and SceneColorBiasFactor.
- The intensity / occlusion / radial-distance terms come from **interpolators** (r0.zw, r2.y in the PS: `sat(…)`, `1 − x` …).
- The flare VS (GUID dabef980…, constants CameraRight / CameraUp / LocalToWorld / ViewProjection) vertex-fetches 7 streams and
  passes them straight to oC0–oC3. So the values are **per-vertex attributes filled on the CPU** by the flare renderer.

**Usage:**
- 32 LensFlareComponents / 31 LensFlareSources / 10 LensFlare assets exist; elements reference these materials (`LFMaterials`, e.g.
  IAC_A1_Far_LF → ORB_LensFlare_BASE).
- **Placement:** 6 MP maps (BrokenHope, Remnant, Rust, Seed, ORB, Gorge: one each), UI_FrontEnd (3), and the campaign.
- **Correction (2026-10-18):** the flare materials (`bUsedWithLensFlare`) **do cook FLensFlareVertexFactory shaders** (5 per material:
  4 base-pass VS density variants + the base PS) plus FLocalVertexFactory ones. The earlier "no lens-flare shaders /
  BeamTrail + Foliage only" reading came from the scanner's VF labels being shifted by one block (§6). So the original draws its
  flares through the stock lens-flare VF, and the UNKNOWN is withdrawn.
- The lens-flare VF's ShouldCache (0x82FB6F88) is `Material->vfunc 0x50 || Material->vfunc 0x34` (H: IsUsedWithLensFlare ||
  IsSpecialEngineMaterial).

**Values (H, stock UE3 lens flares):**
- **LensFlareIntensity** = the element's per-frame intensity: the element's Intensity distribution at its radial / ray position × the
  source's fade (bModulateColorBySource etc.).
- **LensFlareOcclusion** = the source's visible fraction from its occlusion query (0..1).
- **RadialDistance / RayDistance** = the element's normalised screen-space distances.
- On a non-flare VF the generic vertex factories feed defaults: intensity 1, occlusion 1, distances 0 (H).

- **Translator:** expose the four values as per-draw uniforms, defaulting to Intensity 1, Occlusion 1, distances 0. A flare renderer
  sets them per element.

## 5. DecalMaterial root (C)

**Class default (`Engine.Default__DecalMaterial`):** usage flags **bUsedWithDecals, bUsedWithSkeletalMesh, bUsedWithFracturedMeshes =
True**, **bCastShadows = False**. Otherwise a DecalMaterial is authored like a Material: its BlendMode / LightingModel / outputs decide
the pass. E.g. DarkEnergon_Spread_MAT is BLEND_Masked, MLM_Unlit, clip 0.05.

**Compiled** (DarkEnergon_Spread_MAT, A1_IAC_HalfPipe_Vig_m; VF labels corrected 2026-10-18, §6):
- **Static decals** (FLocalDecalVertexFactory, base PS ed761550…): the material's normal masked PS **plus two decal constants,
  `DecalWorldTangent` (c20) and `DecalWorldBitangent` (c21)**. They supply the decal's tangent basis, which the PS multiplies into
  the tangent-space normal (`r0.z · c20.yxz`, `r0.z · c21.yxz`) in place of the mesh's tangents.
  - Otherwise the CTAB is identical to the FLocalVertexFactory PS (903020c1…): OpacityMaskClipValue, textures, uniforms,
    WorldCameraPosition, SceneColorBiasFactor. No clip or projection constants.
  - Decal UVs come from the decal geometry (H: stock UE3 builds the clipped decal mesh on the CPU).
- **Skinned decals** (FGPUSkinDecal / FGPUSkinMorphDecalVertexFactory): the base PS (03b6a6b9…) is **the same shader as the plain
  GPU-skin VFs'**, with no decal constants. The VS (a0265807…) adds **`BoneToDecalRow0`, `BoneToDecalRow1`, `DecalRefPoseLocation`**
  to BoneMatrices: the decal UV is projected from the bone-space position relative to the decal's reference pose (C: CTAB).
- The decal lit passes use the normal TLightPixelShader policies.
- **Blend:** from the material's own BlendMode (masked here). Modulate decals elsewhere (FX_Decals_p DeathDecal / MetalDecal) blend
  `dest × src` with no SceneColorBias (TARGETED_PASS5 §12 add. 29–32).
- **Translator:** treat a DecalMaterial root as a Material root (same outputs and blend). Supply decal UVs from the decal
  geometry (static) or from the bone-to-decal projection (skinned). For static decals, take the normal-map tangent frame from
  the decal's DecalWorldTangent / Bitangent.

## 6. Correction: shader-map VF labels (2026-10-18)

- **What was wrong:** `tools/shadermap_scan.py` read each vertex-factory block as (VF name, count, entries). The cooked layout is
  **u32 N, then N × (u32 count, count × 32-byte entries, VF FName)**: the entries come before the VF they belong to. Every VF label
  was therefore shifted by one block, and the first block's entries were dropped.
- **What was not wrong:** material attribution (each map precedes its label), which was validated against bUsesDistortion. It is
  re-validated with the fixed parser: all 221 MP_IAC_Streets maps parse (168 before), and the only (distortion shaders,
  flag False) cases are 7 MaterialInstanceConstants whose parents use distortion.
- **Effect:**
  - the VF names quoted in this doc's first version: fixed above;
  - the distortion allow-list: regenerated as v3 by `tools/distortion_allowlist.py`. The material set per MP map is unchanged;
    VF names are corrected, e.g. "Foliage / GPUSkinDecal / GPUSkinMorph" → "Local / GPUSkin / GPUSkinMorph".
