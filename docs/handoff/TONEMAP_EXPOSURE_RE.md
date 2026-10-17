# Original tonemap / exposure: is there eye adaptation? (RE, 2026-10-18)

**Answer: no. The shipped 360 build has a fixed exposure and no eye adaptation, auto-exposure or filmic tonemapper (CONFIRMED).**

## Evidence

1. **The scene post chain** (`EngineMaterials.DefaultScenePostProcess`, the only scene chain; MAP_LIGHTING_REFERENCE_RE.md)
   contains exactly two effects:
   - `MotionBlurEffect_1`;
   - `UberPostProcessEffect_0` (DOF + bloom + scene tone + CLUT).
   - Every effect class in the game data: UberPostProcessEffect ×2, PostProcessEffect ×1. There is **no tonemapper / luminance /
     adaptation effect class**, and **no property** named *Adapt*, *Exposure*, *Tonemap* or *HDR* anywhere (only Desaturation
     `LuminanceFactors` on 4 material expressions).
2. **The post shaders that exist** (Engine.xxx ShaderCache; global shaders): TDOFAndBloomGather PS / VS (16 and FP-filter variants),
   FDOFAndBloomBlend PS / VS, **FUberPostProcess PS**, FUberPostProcessClut PS, FUberPostProcessClutInterp PS, plus distortion apply.
   - The Uber PS constants: `SceneShadowsAndDesaturation`, `SceneInverseHighLights`, `SceneMidTones`, `SceneScaledLuminanceWeights`
     (the desaturation weights), `GammaColorScaleAndInverse`, `GammaOverlayColor`, `ColorCorrectionTexture(2)` + `TexCoordScaleBiasLerp`,
     `PackedParameters` / `MinMaxBlurClamp` (DOF), `SCENE_COLOR_BIAS_FACTOR`, `SceneColorTexture`, `BlurredImage`, `SceneDepthTexture`.
   - **No adapted-luminance texture, no exposure / key constant, no luminance-history target.** (Names searched: Tonemap, Luminance,
     Adapt, Exposure, HDR: only `LuminanceFactors` / `SceneScaledLuminanceWeights`.)
3. **The HDR scale is constant:** `SceneColorBiasFactor` = 2^(surface exp bias) = **8** into the 7e3 scene colour, with resolve ÷ 8
   (HDR_SCENE_PATH_RE.md: surface ctor 0x829C2C20 sets bias 3 for 7e3; RHISetRenderTarget writes 2^bias). It is per surface format,
   never per frame.
4. **Runtime post changes in MP are neutral or authored-static:**
   - `TnCamera.PostProcessModifier` (TnPostProcessModifier) can lerp bloom / DOF / scene tone toward a "Modification". Its only
     activators are `TnPlayerControllerMultiplayer.GameCountdownPostProcess`, `TnPlayerPawn` (the damage type's
     `PostProcessModification`) and a TnPlayerController wrapper.
   - The authored values are **identity settings** (Desaturation 0, HighLights 1, MidTones 1, Shadows 0, Bloom 1 / 1) with zero ramp
     times, and no damage-type subclass overrides `PostProcessModification`.
   - `SeqAct_SetPostProcessChain` exists only in HM_ / UI_ packages (2 objects, none in MP maps). DynamicPostProcessVolume is
     campaign-only data (21; none placed in MP maps).
   - So **per map, the grade is the static PostProcessVolume / world settings**: Scene tone + CLUT + bloom (MAP_LIGHTING_REFERENCE_RE.md).

## So for Rendering's Realistic auto-exposure

- **The original never adapted.** A dark room in the original is as dark as its lightmaps × the fixed bias, with the authored
  Shadows / HighLights / MidTones / Desat and CLUT applied identically everywhere. Haze in a dark room shows at its authored strength.
- **An exposure lift is a Realistic-only design choice** (fine as such). To stay close to the authored look, options are:
  - clamp the Realistic exposure range close to 1;
  - or apply exposure to the lit scene only and composite fog / haze in-scatter after exposure, so authored haze keeps its
    original absolute level (in the original, fog is in the same fixed-scale HDR scene colour as everything else).
- **Off / Faithful should keep a fixed exposure of 1** (× the authored scene tone). This matches what Rendering's X1 note already says
  ("Off / Faithful drive the authored post exactly").
