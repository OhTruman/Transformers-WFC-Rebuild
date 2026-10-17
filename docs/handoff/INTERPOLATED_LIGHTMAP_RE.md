# HmInterpolatedLightMappedSkeletalMeshComponent: baked lighting of the transforming world actors (RE, 2026-10-18)

Labels: CONFIRMED (C) = cooked data / microcode / native code; HIGH (H) = stock UE3 reading, not traced.
Scope: the mesh of every `TnTransformingWorldActor` (doors / bridges / transforming props). 895 placed instances: **32 in MP maps
(all built)**, 863 campaign (665 built, 198 unbuilt).

## Answer in one paragraph

Each actor cooks **two texture light maps** (directional, 3 coefficient textures each), one per capture pose. The pose is
LightMapCapturePercentages = [0, 1] (the class default; no instance overrides it): capture 0 = untransformed, capture 1 =
transformed. The base-pass shader **lerps** the two decoded light maps by `LightMapLerpAmount`. Natively, **every component tick**
sets that amount to the AnimNodeSequence's **normalized position**. So the lighting follows the transform animation continuously,
not only at the end states. Script sets it once at setup (0 / 1) so the first frame is right.

## 1. Cooked data (C): native tail layout

Big-endian, after the tagged properties. Decoder: `tools/interp_lightmap.py <map package> [out.json]`. It was checked on all 895
instances; every tail is consumed exactly.

```
GUID16      copy of the LightingGuid property
u8          1 (all)
u32 N       0 → unbuilt: the tail ends here (21 bytes; 198 campaign instances, none in MP)
-- built (N = 1) --
u32 ×3      0, 0, 3 (all built; meaning UNKNOWN)
u32 n, n × GUID16      union of all captures' baked lights (light LightGuid)
u32         0
u32 C       capture count = 2 (all 697 built)
C × capture:
  u32 type             2 = texture light map (2D) on all
  u32 g, g × GUID16    lights baked into this capture (LightGuid)
  u32                  0
  3 × { u32 export index → LightMapTexture2D, float4 ScaleVector }   one per directional coefficient
  float2 CoordinateScale, float2 CoordinateBias                      the capture's atlas rectangle in those textures
u32 m, m × GUID16      static lights that do NOT affect this mesh (H: the "irrelevant lights")
```

**Example**: MP_ESC_BrokenHope TnTransformingWorldActor_11211.
- Capture 0: 9 lights; textures LightMapTexture2D_7958 / 13480 / 710; scale (1.14, 0.58, 0.53, 1) …; CoordinateScale (0.25, 0.25),
  CoordinateBias (0.2505, 0.0005).
- Capture 1: 7 lights; textures _3073 / _223 / _6695; scale (0.73, 0.31, 0.08, 1) …; same CoordinateScale, CoordinateBias
  (0.0005, 0.0005).
- 14 baked lights in the union, 17 irrelevant.

**Details:**
- The textures are `LightMapTexture2D` exports with outer `<map>_LM` (`bLightmapsInSeparatePackage` = True). They are separate
  textures per capture, each capture occupying one atlas rectangle.
- **Not vertex light maps.** No type-1 capture exists.
- Baked and irrelevant lights are both static-only, Static-channel lights (`LAC_STATIC_AFFECTING`, Dynamic channel off). The component's
  `LightingChannels` is {Static}. So in the original no dynamic light pass applies from those lights (H: stock light-interaction rules).

## 2. Shader (C): `TBasePassPixelShaderFDirectionalInterpolatedTextureLightMapPolicy`

Material usage flag 8 (`bUsedWithStaticInterpolatedLighting`, enforced by CheckForErrors) cooks this policy, with its VS
`TBasePassVertexShaderFDirectionalInterpolatedTextureLightMapPolicyFNoDensityPolicy`. Disassembly checked:
ENV_PhongMain_NS_Mat in MP_ESC_BrokenHope_Base_m on FLocalVertexFactory, PS 7daec599…, VS 37fa21ae…. The same policy is cooked on
**FGPUSkinVertexFactory** (the skeletal path these actors use), e.g. COR_RoboArmClaw_MATINST PS fc93526e… / VS 8293e7a3…:
identical structure with the registers moved (LightMapTextures s5–s10, LightMapScale c37–c42, LightMapLerpAmount c43; VS c13 / c14
+ BoneMatrices). Register numbers below are from the Local-VF variant.

**VS:**
- Two light-map UV sets from the mesh's light-map UV: `oC2.xy = uv·c13.xy + c13.wz`, `oC2.zw = uv·c14.xy + c14.wz`.
- c13 / c14 (CTAB name `ShadowCoordinateScaleBias` ×2) are packed per capture as (ScaleX, ScaleY, BiasY, BiasX), the stock UE3
  packing. So the cooked (CoordinateScale, CoordinateBias) maps to uv·Scale + Bias.

**PS** (CTAB: `LightMapTextures` sampler ×6 at s4–s9, `LightMapScale` ×6 at c25–c30, `LightMapLerpAmount` c31):
```
A_i = tex(LightMapTextures[i],   uvA) · LightMapScale[i]       i = 0..2  (capture 0, s4–s6, c25–c27)
B_i = tex(LightMapTextures[3+i], uvB) · LightMapScale[3+i]               (capture 1, s7–s9, c28–c30)
L_i = A_i + LightMapLerpAmount · (B_i − A_i)                   ; MULADD (B·scale − A)·c31.x + A, no saturate
```
- The three blended coefficients then go through the usual directional-light-map decode: the HL2 basis literals 0.816497, 0.707107,
  −0.408248, 0.57735 are in the PS, dotted with the tangent-space normal.
- The shader **lerps the decoded per-coefficient values** (texture × scale), not the raw texels.
- It also samples `ShadowMaskTexture` (screen-space dynamic shadow mask) like the normal directional-light-map policy.

## 3. Driving the lerp (C): native

- `execSetInterpolationPercentage` (native table 0x8370D868 → 0x82C62BE8) calls **0x82CC6E08** `SetInterpolationPercentage(f)`. When
  the component has a scene proxy, that enqueues a render command (vtable 0x822C6738 → 0x829FDC50) that stores `f` at **proxy+0x118**,
  the value bound to `LightMapLerpAmount`. Otherwise it stores it directly (0x82FBF138).
- The component's tick override **0x82CCAB80** (vtable slot 0x13C of the class vtable at 0x822CF140; parent
  USkeletalMeshComponent tick 0x82D9EBC8):
  ```
  if (Animations) SetInterpolationPercentage( Animations->GetNormalizedPosition(-1.0) );   // vtbl+0x13C = GetNormalizedPosition
  Super::Tick(DeltaTime);
  ```
  - The vtbl+0x13C identity is confirmed: `UAnimNodeSequence::execGetNormalizedPosition` (0x82791E90) calls the same slot.
  - So **lerp = normalized anim position every tick**: 0 → 1 while Transforming (anim forward), 1 → 0 while Untransforming (anim
    backward), held at 0 / 1 in the end states.
  - With the default capture percentages [0, 1], percentage = lerp. No instance overrides the percentages, so no remap is needed.
- Script (`TnTransformingWorldActor.SetInitialTransformState`) additionally sets 0 / 1 at setup. The tick then keeps it in sync.

## 4. For Rendering / AssetTools

- **AssetTools:** export per actor the 2 × 3 LightMapTexture2D (from `<map>_LM`), the 6 scale vectors and the 2 CoordinateScale /
  CoordinateBias pairs (`tools/interp_lightmap.py` gives the layout). The light-map UV is the mesh's light-map UV channel (H: the
  skeletal mesh's second UV, as for static meshes).
- **Rendering:**
  - Decode each capture as a normal directional texture light map (texel × scale vector, HL2 basis);
  - lerp the three decoded coefficients by `t = AnimNodeSequence normalized position`, updated every frame, before the basis
    decode;
  - no dynamic lighting from the baked / irrelevant lights.
  - The 198 unbuilt campaign instances have no light map (N = 0). The original's fallback for them was not traced (UNKNOWN);
    none are in MP.
- **Open (UNKNOWN):** the three header u32 (0, 0, 3) and the zero u32s inside captures. Constant on all instances, so harmless.
