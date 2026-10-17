# Campaign material nodes: If, Ddx/Ddy, CameraWorldPosition, LensFlare*, DecalMaterial (RE, 2026-10-18)

Labels: CONFIRMED (C) = cooked shader microcode / CTAB or authored data; HIGH (H) = stock UE3 semantics, not traced in WFC.
Method: `tools/shadermap_scan.py` (per-material shader maps; now also parses `DecalMaterial` labels) → shader GUIDs →
`tools/shader_by_guid.py` (blob, CTAB, literals, Xenos disassembly).

## 1. MaterialExpressionIf (C)

**Authored use:** Splash_Big_Clip_MAT and Splash_Big_Clip_02_MAT:
- A = texture alpha (TextureSampleParameter2D output 4 / 3), B = DynamicParameter;
- AGreaterThanB = Constant 1.0, AEqualsB = ALessThanB = Constant 0 (R unset);
- **no EqualsThreshold property exists in WFC's If** (only A, B and the three result inputs).

**Compiled** (Splash_Big_Clip_MAT base-pass PS, A5_TRY_BASE_m, HmParticleCloud_NoSubUV, GUID 26b4ae39…):
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
- CHR_Vignette_Mat: base + directional / spot / one-light-uber light passes in A1 / A2 / A3, on the Foliage and GPU-skin-decal VFs.

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
- The flare-node values are **not shader constants**. In flare_sun_Mat's PS (D3A_IAC_City1_ART_m, GUID e468bbcf…) the CTAB is only
  Texture2D_0, UniformScalar_0, UniformVector_0 and SceneColorBiasFactor.
- The intensity / occlusion / radial-distance terms come from **interpolators** written by the vertex factory (r0.zw, r2.y in the
  PS: `sat(…)`, `1 − x` …).
- The material's vertex shader for the cooked VF passes vertex-fetched streams straight to those interpolators.

**Usage:**
- 32 LensFlareComponents / 31 LensFlareSources / 10 LensFlare assets exist; elements reference these materials (`LFMaterials`, e.g.
  IAC_A1_Far_LF → ORB_LensFlare_BASE).
- The cooked maps for the 14 flare materials list only **FParticleBeamTrailVertexFactory** and **FFoliageVertexFactory** shaders. **No
  `FLensFlareVertexFactory` shaders were found** for them in any package (scan over all 460).

**Values (H, stock UE3 lens flares):**
- **LensFlareIntensity** = the element's per-frame intensity: the element's Intensity distribution at its radial / ray position × the
  source's fade (bModulateColorBySource etc.).
- **LensFlareOcclusion** = the source's visible fraction from its occlusion query (0..1).
- **RadialDistance / RayDistance** = the element's normalised screen-space distances.
- On a non-flare VF the generic vertex factories feed defaults: intensity 1, occlusion 1, distances 0 (H).

**Open (UNKNOWN):** with no FLensFlareVertexFactory shaders cooked, how the shipped game draws these flares (through the beam-trail
VF, or not at all) is not settled. A runtime check of a campaign scene with a LensFlareSource in view is the next step.
- **Translator:** expose the four values as per-draw uniforms, defaulting to Intensity 1, Occlusion 1, distances 0. A flare renderer
  sets them per element.

## 5. DecalMaterial root (C)

**Class default (`Engine.Default__DecalMaterial`):** usage flags **bUsedWithDecals, bUsedWithSkeletalMesh, bUsedWithFracturedMeshes =
True**, **bCastShadows = False**. Otherwise a DecalMaterial is authored like a Material: its BlendMode / LightingModel / outputs decide
the pass. E.g. DarkEnergon_Spread_MAT is BLEND_Masked, MLM_Unlit, clip 0.05.

**Compiled:**
- **The pixel shader is an ordinary base-pass PS.** DarkEnergon_Spread_MAT under FLocalDecalVertexFactory (A1_IAC_HalfPipe_Vig_m,
  GUID 03b6a6b9…) has the same kind of CTAB as any masked material: OpacityMaskClipValue, textures, uniforms, WorldCameraPosition,
  SceneColorBiasFactor. There are no decal clip or projection constants in the PS.
- The decal lit passes use the normal TLightPixelShader policies.
- **The decal difference is in the vertex factory:**
  - **static decals** (FLocalDecalVertexFactory): the decal geometry carries its own decal UVs (H: stock UE3 builds the clipped
    decal mesh on the CPU);
  - **skinned decals** (FGPUSkinDecal / FGPUSkinMorphDecalVertexFactory, VS GUID a0265807…): **`BoneToDecalRow0`, `BoneToDecalRow1`,
    `DecalRefPoseLocation`** + BoneMatrices. The decal UV is projected from the bone-space position relative to the decal's
    reference pose (C: CTAB).
- **Blend:** from the material's own BlendMode (masked here). Modulate decals elsewhere (FX_Decals_p DeathDecal / MetalDecal) blend
  `dest × src` with no SceneColorBias (TARGETED_PASS5 §12 add. 29–32).
- **Translator:** treat a DecalMaterial root exactly as a Material root (same outputs and blend). Supply decal UVs from the decal
  geometry (static) or from the bone-to-decal projection (skinned).
