# MP maps: authored lighting intent, reference numbers (RE, 2026-10-17)

> **Caveat:** in the "Ours" columns, **D** means our Realistic / Faithful path **reads that authored value from data**, not
> that the rendered result matches the original. **A** = approximated or hard-coded. **X1** = replaced or modified by the
> Realistic X1 tone path **by design** ("intentionally not driven" in that mode). **-** = not used.

Labels: CONFIRMED (C) = cooked data / our source read; HIGH (H) = stock UE3 reading not traced in the WFC binary.

- **Sources (all read-only):**
  - `ExtractedAssets/VerticalSlice/Maps/<MAP>/lighting.json` + `postprocess.json` (AssetTools exports from the cooked levels);
  - `authored.db` (fog volumes, Beast volumes);
  - ours: `Rebuild/work/render/<MAP>/lighting.json` + `fog_volumes.json`, `src/render/gl/WfcPipeline.cpp`, `tools/render/build_lighting.py`.
- **Tool:** `tools/map_lighting_reference.py` → `notes/data/map_lighting_reference.json` / `.md` (the per-map tables, also below).
- `docs/handoff/RT_FULL_PLAN.md` is not on any branch in the local Rebuild checkout. The X1 set below is taken from the tone code
  itself (`WfcPipeline.cpp` post pass ~8765–8860 and the post FS ~782–840) and `docs/handoff/RENDERING_PACKAGE7_POLISH.md`.

## Summary per map (ours as built)

| Map | Dominant sun sRGB × brightness | Height fogs authored (ours loads) | Fog volumes | Post source | Ours Bloom_Scale / Desat | CLUT |
|---|---|---|---|---|---|---|
| MP_ESC_BrokenHope | 239/168/148 × 3.5 | 1 enabled (1 loaded) | 0 | volume | 0.10000000149011612 / 0.20000000298023224 | clut_mp40 |
| MP_ESC_Remnant | 204/158/157 × 10 | 0 enabled (0 loaded) | 0 | volume | 0.10000000149011612 / 0.0 | desaturation40_CLR |
| MP_IAC_Berth | 234/191/191 × 2 | 2 enabled (1 loaded) | 1 | volume | 0.10000000149011612 / 0.20000000298023224 | clut_mp40 |
| MP_IAC_Rust | 242/182/164 × 2 | 2 enabled (1 loaded) | 6 | volume | 0.10000000149011612 / 0.0 | clut_mp40 |
| MP_IAC_Seed | 252/149/105 × 2.3 | 2 enabled (1 loaded) | 0 | volume | 0.10000000149011612 / 0.20000000298023224 | desaturation40_CLR |
| MP_IAC_Streets | 117/117/255 × 6 | 1 enabled (1 loaded) | 0 | world | 0.10000000149011612 / 0.0 | MP_Streets_CLUT |
| MP_KON_Molten | 218/218/154 × 4 | 1 enabled (1 loaded) | 0 | volume | 0.10000000149011612 / 0.10000000149011612 | clut_mp40 |
| MP_ORB_Debris | 166/166/249 × 3 | 0 enabled (0 loaded) | 0 | world | 0.10000000149011612 / 0.20000000298023224 | MP_OrbitalDebris_CLUT |
| MP_UND_Complex | 183/181/151 × 6 | 2 enabled (1 loaded) | 0 | volume | 0.10000000149011612 / 0.10000000149011612 | clut_mp40 |
| MP_UND_Gorge | 234/223/208 × 2 | 4 enabled (1 loaded) | 0 | volume | 0.10000000149011612 / 0.0 | clut_mp40 |
## What our path drives, per parameter

| Parameter group | Authored source | Ours | Where |
|---|---|---|---|
| Placed lights: colour, brightness, radius, falloff, cones, channels, shadow flags, LightGuid | SLCA LightComponents | **D** | WfcPipeline.cpp:1488–1559 |
| Sky light upper / lower colour × brightness | SkyLightComponent | **D** (Faithful); **X1-family by design** in RT Realistic with probes: the RT world's ambient (sky + bounce) replaces the SkyLights' terms (S3) | :1504, :3404–3423 |
| Dominant directional (sun) | DirectionalLight (baked: BSP / Static / CompositeDynamic) | **D** via the baked lightmaps and composite-dynamic; Realistic R1 "sun swap" replaces the baked sun share on maps with V_bake **by design** | :376, :1292 |
| Height fog | HeightFogComponent(s) | **D for the first component in export order only** (build_lighting.py `fog()` returns on the first) | build_lighting.py:215–222; :1561–1600 |
| Additional height fogs | 6 maps author 2–4 enabled (Berth 2, Rust 2 (+1 disabled), Seed 2, Complex 2, Gorge 4) | **-** (gap) | stock UE3 renders up to 4 height-fog layers per view (H; WFC's fog shader binds FogMinHeight / FogMaxHeight / FogDistanceScale / FogExtinctionDistance / FogInScattering / FogStartDistance at 0x82E94B88, C; array size not traced) |
| Constant-density fog volumes | FogVolumeConstantDensityInfo (Berth 1, Rust 6) | **D** | :1299 `loadFogVolumes`, :315 |
| Post settings source | Governing TnWorldInfo DefaultPostProcessSettings; one PostProcessVolume on 8 maps | **D**: the highest-priority enabled volume replaces the world settings (volume brush not exported; "inside" taken from player-start coverage, H for gameplay views) | build_lighting.py:715–735 |
| bEnableBloom | settings | **D** (Off / Faithful); **X1**: bloom always on (`bloomOn = post_.bloom \|\| filmic`) | :8779 |
| Bloom_Scale | settings (0.1 on every MP map's effective settings) | **D**; **X1**: × `toneBloom` | :8792 |
| Bloom_Threshold | settings (1.0 everywhere) | **D** (Off / Faithful); **X1 replaced**: 1.0 soft knee over the exposed HDR scene (`uSoftBloom`), input cap `uBloomMax` 16 | :8793–8797 |
| DOF (FocusDistance, Near / Far falloff, FalloffExponent, MaxNear / MaxFar) | settings | **D** (UE3 PackedParameters layout) | :1608–1614, :8800 |
| DOF_BlurKernelSize | 16 (world) / **32** on BrokenHope + Gorge volumes | **A**: the blur radius is fixed at the 16-px default (`[PROV]`) | :743 |
| DOF_FocusType / FocusPosition | FOCUS_Distance on every MP map | n/a (Position never used) | |
| Motion blur | bEnableMotionBlur **False** on every MP map's effective settings | **-** (not loaded; no impact) | |
| Scene_Shadows / HighLights / MidTones / Desaturation | settings | **D** (both modes). In X1 they act **after** the X1 exposure, contrast and saturation, and the filmic shoulder: authored tone kept, its input changed **by design** | :1617–1624, post FS |
| ColorCorrectionTexture (CLUT) | volume / world | **D** (both modes, last step after gamma) | :1628–1650 |
| DisplayGamma | Xe-TransEngine.ini 2.2 | **D** (`uInvGamma`) | :8848 |
| bAllowAmbientOcclusion | False everywhere | n/a | |
| X1 additions with **no authored counterpart** | none | **X1**: exposure (key 0.02, strength 0.65, clamp 0.7–1.8, adaptation 1 − exp(−1.5 dt)); contrast 1.15 around 18 % grey; saturation; shoulder 0.6; white roll; flash term (polish item 1) | post FS ~812–819; RENDERING_PACKAGE7_POLISH.md §1 |
| Character light environment: TotalLightCount 2, NormalizedSampleOffsets (robot / vehicle), UpdateDistanceThreshold 30, EnvironmentTransitionTime 0.5 | TnRobotForm / TnTruckForm / TnWeaponMesh LightEnvironment CDOs (same on every map) | **D** for TotalLightCount and the sample offsets; **-** for UpdateDistanceThreshold / EnvironmentTransitionTime (no grep hit in src) | :3409, :3453 |
| Beast light-environment volumes / boxes | listed per map below | not classified: no direct grep hit for the volume class; ours reads the LightsVisibilitiesVolume (lvv_) and Beast probes (only ORB ships probe data) | :1561ff |

**Gaps (actionable):**
1. **Additional height fogs**: only the first is loaded. Gorge loses 3 layers, including a dense low layer (−12773 uu, density 1e-4, ×10 brightness).
2. **DOF_BlurKernelSize 32** on the BrokenHope / Gorge volumes is rendered at 16.
3. Character light-environment **EnvironmentTransitionTime 0.5 s** (the lighting blend when moving between areas) is not modelled (H: stock DLE semantics).

## Per-map tables

### MP_ESC_BrokenHope

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_14211_LC | 3.5 | 239/168/148 | - | (-0.41999, 0.49682, -0.75946) yaw 130.2° pitch -49.4° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | False / False |
| dynamic_sky_light | SkyLight_12563_LC | 0.1 | 64/128/128 | 0/0/0 × None | (1, 0, 0) yaw 0.0° pitch 0.0° | BSP,CompositeDynamic,Dynamic,Static | False / False |

**Top 10 placed lights** (Brightness × Radius; 299 enabled point / spot lights; ours loads 303 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_14989_LC | Spot | 3 | 15000 | 2 | 248/176/124 | [11677, 29, 8268] |
| 2 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_2153_LC | Spot | 3 | 15000 | 2 | 248/176/124 | [6043, -5199, 7715] |
| 3 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_3435_LC | Spot | 3 | 10000 | 2 | 153/206/249 | [-20347, 9408, 3180] |
| 4 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_1310_LC | Spot | 3 | 9315.84 | 2 | 153/206/249 | [-232, 2340, 3348] |
| 5 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_828_LC | Spot | 3 | 9315.84 | 2 | 153/206/249 | [3622, 1683, 3348] |
| 6 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_15672_LC | Spot | 3 | 5292 | 2 | 153/206/249 | [-17906, 4989, 2670] |
| 7 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_8483_LC | Spot | 3 | 5292 | 2 | 153/206/249 | [-17908, 13581, 2670] |
| 8 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_15807_LC | Spot | 3 | 5077.94 | 2 | 248/176/124 | [4626, -1138, 876] |
| 9 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_2193_LC | Spot | 3 | 5000 | 2 | 248/176/124 | [9645, 7515, 3439] |
| 10 | MP_ESC_BrokenHope_ART_m:StaticLightCollectionActor_11219.SpotLight_6393_LC | Spot | 3 | 5000 | 2 | 248/176/124 | [5171, 7515, 3439] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_ESC_BrokenHope_ART_m:HeightFog_9064.HeightFogComponent_12587 | 12694.4 | 5e-06 | 121/159/223 × 1.28 | 1000 | 1e+13 | loaded |

**Post-process** (world = governing MP_ESC_BrokenHope_Base_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_ESC_BrokenHope_ART_m:PostProcessVolume_13331)

| Setting | World | PostProcessVolume_13331 (prio 1.0) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 15000 | 15000 |
| DOF_FocusNearFalloff | 2000 | 2000 | 2000 |
| DOF_FocusFarFalloff | 2000 | 2000 | 2000 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.5 | 0.5 |
| DOF_FalloffExponent | 4 | 4 | 4 |
| DOF_BlurKernelSize | 16 | 32 | 32 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0.2 | 0.2 |
| Scene_HighLights | (1, 1, 1) | (1.5, 1.5, 1.5) | (1.5, 1.5, 1.5) |
| Scene_MidTones | (1, 1, 1) | (1, 1, 1) | (1, 1, 1) |
| Scene_Shadows | (0, 0, 0) | (-0.001, -0.001, -0.001) | (-0.001, -0.001, -0.001) |
| ColorCorrectionTexture | None | clut_mp40 | clut_mp40 |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_ESC_Remnant

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_3897_LC | 10 | 204/158/157 | - | (0.41051, 0.74585, -0.52459) yaw 61.2° pitch -31.6° | BSP,CompositeDynamic,Static | True / False |
| dynamic_sky_light | SkyLight_16342_LC | 0.2 | 180/187/205 | 250/194/27 × 0.009999999776482582 | (1, 0, 0) yaw 0.0° pitch 0.0° | BSP,CompositeDynamic,Dynamic,Static | False / False |

**Top 10 placed lights** (Brightness × Radius; 170 enabled point / spot lights; ours loads 171 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.PointLight_2012_LC | Point | 200 | 3793.92 | 2 | 222/62/7 | [36475, 13312, 6490] |
| 2 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.SpotLight_5566_LC | Spot | 18 | 30256 | 2 | 85/205/244 | [8696, -3835, 21673] |
| 3 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.SpotLight_12145_LC | Spot | 8 | 50000 | 2 | 106/159/202 | [18043, 3239, 18713] |
| 4 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.SpotLight_1213_LC | Spot | 8 | 33060 | 2 | 44/140/237 | [26259, 20942, 16320] |
| 5 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.SpotLight_8972_LC | Spot | 10 | 9584.64 | 2 | 14/182/237 | [26157, 25, 4933] |
| 6 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.PointLight_13570_LC | Point | 5 | 6381.57 | 2 | 244/89/45 | [19984, 20608, 4450] |
| 7 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.PointLight_1253_LC | Point | 3 | 5000 | 2 | 62/131/230 | [33167, 1434, 3267] |
| 8 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.PointLight_10798_LC | Point | 2 | 7500 | 2 | 253/216/68 | [1428, 13377, 5940] |
| 9 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.PointLight_6952_LC | Point | 1 | 10394 | 4 | 252/221/114 | [-6898, 799, 3740] |
| 10 | MP_ESC_Remnant_ART_m:StaticLightCollectionActor_2946.PointLight_13531_LC | Point | 1 | 10000 | 2 | 80/119/152 | [37971, -1815, 2510] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| none | | | | | | |

**Post-process** (world = governing MP_ESC_Remnant_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_ESC_Remnant_ART_m:PostProcessVolume_10624)

| Setting | World | PostProcessVolume_10624 (prio None) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 3 | 3 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 7500 | 7500 |
| DOF_FocusNearFalloff | 2000 | 0 | 0 |
| DOF_FocusFarFalloff | 2000 | 3500 | 3500 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.6 | 0.6 |
| DOF_FalloffExponent | 4 | 4 | 4 |
| DOF_BlurKernelSize | 16 | 64 | 64 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0 | 0 |
| Scene_HighLights | (1, 1, 1) | (0.95, 0.95, 1.35) | (0.95, 0.95, 1.35) |
| Scene_MidTones | (1, 1, 1) | (0.75, 1, 1.1) | (0.75, 1, 1.1) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0.005) | (0, 0, 0.005) |
| ColorCorrectionTexture | None | desaturation40_CLR | desaturation40_CLR |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_IAC_Berth

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_2391_LC | 2 | 234/191/191 | - | (-0.57714, -0.20053, -0.79164) yaw -160.8° pitch -52.3° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | False / False |

**Top 10 placed lights** (Brightness × Radius; 295 enabled point / spot lights; ours loads 297 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_7556_LC | Spot | 10 | 8000 | 2 | 223/181/149 | [12031, -106, 4115] |
| 2 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_11091_LC | Spot | 10 | 5000 | 2 | 223/181/149 | [4928, 2913, 210] |
| 3 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_7397_LC | Spot | 10 | 5000 | 2 | 223/181/149 | [-7408, 2913, 210] |
| 4 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_6370_LC | Spot | 10 | 5000 | 2 | 223/181/149 | [-7560, -2772, 210] |
| 5 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_6780_LC | Spot | 10 | 5000 | 2 | 223/181/149 | [3940, -2834, 84] |
| 6 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.PointLight_2973_LC | Point | 5 | 7000 | 2 | 230/145/87 | [-1334, 32, -2034] |
| 7 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.PointLight_11888_LC | Point | 5 | 5000 | 2 | 222/156/112 | [3262, -7096, -1847] |
| 8 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_12080_LC | Spot | 10 | 2500 | 2 | 220/179/165 | [-28874, -138, 2132] |
| 9 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_9874_LC | Spot | 10 | 2500 | 2 | 220/179/165 | [-28078, -3028, 2150] |
| 10 | MP_IAC_Berth_ART_m:StaticLightCollectionActor_5895.SpotLight_7450_LC | Spot | 10 | 2500 | 2 | 220/179/165 | [-27960, 2917, 2132] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_IAC_Berth_ART_m:HeightFog_16051.HeightFogComponent_13424 | 11847.5 | 2e-06 | 169/213/226 × 1 | 1000 | 1e+08 | loaded |
| MP_IAC_Berth_ART_m:HeightFog_4411.HeightFogComponent_13424 | 15431.1 | 4e-06 | 152/215/222 × 2 | 50000 | 1e+08 | - |
| MP_IAC_Berth_ART_m:FogVolumeConstantDensityInfo_4795.FogVolumeConstantDensityComponent_15743 (ConstantDensity) | - | 4.999999873689376e-05 | 0/0/0 | - | - | - |

**Post-process** (world = governing MP_IAC_Berth_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_IAC_Berth_ART_m:PostProcessVolume_3052)

| Setting | World | PostProcessVolume_3052 (prio None) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | True | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 15000 | 15000 | 15000 |
| DOF_FocusNearFalloff | 2000 | 2000 | 2000 |
| DOF_FocusFarFalloff | 2000 | 2000 | 2000 |
| DOF_MaxNearBlurAmount | 0 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.5 | 0.5 |
| DOF_FalloffExponent | 4 | 4 | 4 |
| DOF_BlurKernelSize | 32 | 32 | 32 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0.2 | 0.2 |
| Scene_HighLights | (1, 1, 1) | (1.5, 1.5, 1.5) | (1.5, 1.5, 1.5) |
| Scene_MidTones | (1, 1, 1) | (0.95, 0.95, 0.95) | (0.95, 0.95, 0.95) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0) | (0, 0, 0) |
| ColorCorrectionTexture | clutshot1 | clut_mp40 | clut_mp40 |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); Beast light-environment volumes: MP_IAC_Berth_ART_m:BeastLightEnvironmentVolume_10099

### MP_IAC_Rust

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_7478_LC | 2 | 242/182/164 | - | (-0.50169, 0.29575, -0.81292) yaw 149.5° pitch -54.4° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | False / False |

**Top 10 placed lights** (Brightness × Radius; 346 enabled point / spot lights; ours loads 348 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.PointLight_10513_LC | Point | 0.4 | 150000 | 2 | 99/160/197 | [29874, -17096, -66360] |
| 2 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_6927_LC | Spot | 10 | 2500 | 2 | 255/255/255 | [-12646, -11769, 3519] |
| 3 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_8277_LC | Spot | 10 | 2500 | 2 | 255/255/255 | [-12678, -9879, 3519] |
| 4 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_13619_LC | Spot | 10 | 2500 | 2 | 255/255/255 | [-12473, -7672, 3519] |
| 5 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_3010_LC | Spot | 10 | 2500 | 2 | 255/255/255 | [-34046, -1297, 4807] |
| 6 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_13326_LC | Spot | 5 | 5000 | 2 | 249/225/193 | [-26654, -15505, 6706] |
| 7 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_5231_LC | Spot | 10 | 2500 | 2 | 255/255/255 | [-9430, -11193, 3518] |
| 8 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_164_LC | Spot | 3 | 6000 | 2 | 234/198/162 | [-15986, 3696, 3248] |
| 9 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_1967_LC | Spot | 3 | 6000 | 2 | 234/198/162 | [-14133, 3696, 3248] |
| 10 | MP_IAC_Rust_Art_m:StaticLightCollectionActor_8501.SpotLight_16073_LC | Spot | 3 | 6000 | 2 | 234/198/162 | [-12229, 3696, 3248] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_IAC_Rust_Art_m:HeightFog_13733.HeightFogComponent_14723 | -18561.9 | 0.0001 | 217/147/43 × 0 | 20000 | 1e+08 | loaded |
| MP_IAC_Rust_Art_m:HeightFog_2815.HeightFogComponent_14708 | 23271.7 | 5e-05 | 135/197/220 × 0.5 | 10000 | 1e+08 | - |
| MP_IAC_Rust_Art_m:HeightFog_6999.HeightFogComponent_14723 | 18878 | 2e-06 | 185/222/236 × 1 | 0 | 1e+08 | - |
| MP_IAC_Rust_Art_m:FogVolumeConstantDensityInfo_11774.FogVolumeConstantDensityComponent_14701 (ConstantDensity) | - | 0.004999999888241291 | 0/0/0 | - | - | - |
| MP_IAC_Rust_Art_m:FogVolumeConstantDensityInfo_191.FogVolumeConstantDensityComponent_14701 (ConstantDensity) | - | 0.004999999888241291 | 0/0/0 | - | - | - |
| MP_IAC_Rust_Art_m:FogVolumeConstantDensityInfo_3656.FogVolumeConstantDensityComponent_6339 (ConstantDensity) | - | 0.004999999888241291 | None | - | - | - |
| MP_IAC_Rust_Art_m:FogVolumeConstantDensityInfo_7738.FogVolumeConstantDensityComponent_6339 (ConstantDensity) | - | 0.004999999888241291 | None | - | - | - |
| MP_IAC_Rust_Art_m:FogVolumeConstantDensityInfo_7750.FogVolumeConstantDensityComponent_14701 (ConstantDensity) | - | 0.004999999888241291 | 0/0/0 | - | - | - |
| MP_IAC_Rust_Art_m:FogVolumeConstantDensityInfo_8380.FogVolumeConstantDensityComponent_14701 (ConstantDensity) | - | 0.004999999888241291 | 0/0/0 | - | - | - |

**Post-process** (world = governing MP_IAC_Rust_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_IAC_Rust_Art_m:PostProcessVolume_3052)

| Setting | World | PostProcessVolume_3052 (prio None) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 15000 | 15000 |
| DOF_FocusNearFalloff | 2000 | 0 | 0 |
| DOF_FocusFarFalloff | 2000 | 2000 | 2000 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.4 | 0.4 |
| DOF_FalloffExponent | 4 | 1 | 1 |
| DOF_BlurKernelSize | 16 | 32 | 32 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0 | 0 |
| Scene_HighLights | (1, 1, 1) | (1.5, 1.5, 1.5) | (1.5, 1.5, 1.5) |
| Scene_MidTones | (1, 1, 1) | (0.9, 0.9, 0.9) | (0.9, 0.9, 0.9) |
| Scene_Shadows | (0, 0, 0) | (-0.001, -0.001, -0.001) | (-0.001, -0.001, -0.001) |
| ColorCorrectionTexture | None | clut_mp40 | clut_mp40 |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_IAC_Seed

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_12837_LC | 2.3 | 252/149/105 | - | (-0.70372, 0.13773, -0.697) yaw 168.9° pitch -44.2° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic,PlayerOnly | False / False |
| dynamic_sky_light | SkyLight_7798_LC | 0.05 | 121/165/168 | 255/255/255 × None | (1, 0, 0) yaw 0.0° pitch 0.0° | BSP,CompositeDynamic,Dynamic,Static | False / False |

**Top 10 placed lights** (Brightness × Radius; 309 enabled point / spot lights; ours loads 317 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_IAC_Global_Art_m:StaticLightCollectionActor_11171.PointLight_1865_LC | Point | 1 | 1.2e+06 | 2 | 133/177/214 | [-2519, -1097, 20588] |
| 2 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_8585_LC | Spot | 10 | 28000 | 2 | 194/219/231 | [-1596, 227, 5698] |
| 3 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_2368_LC | Spot | 15 | 5787.14 | 2 | 247/232/213 | [-5193, -15405, 2260] |
| 4 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_2992_LC | Spot | 15 | 5787.14 | 2 | 247/232/213 | [-20462, -3683, 1498] |
| 5 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_16168_LC | Spot | 15 | 5787.14 | 2 | 247/232/213 | [-24129, -3113, 1778] |
| 6 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_6015_LC | Spot | 15 | 5787.14 | 2 | 247/232/213 | [-24129, 5971, 1876] |
| 7 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_8340_LC | Spot | 15 | 4000 | 2 | 247/232/213 | [-4669, 122, 1959] |
| 8 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_12925_LC | Spot | 10 | 5787.14 | 2 | 183/189/223 | [5867, -11216, 2569] |
| 9 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_11802_LC | Spot | 10 | 5787.14 | 2 | 183/186/223 | [20602, -13468, 1543] |
| 10 | MP_IAC_Seed_ART_m:StaticLightCollectionActor_670.SpotLight_2179_LC | Spot | 10 | 5787.14 | 2 | 183/186/223 | [19996, -9245, 2663] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_IAC_Seed_ART_m:HeightFog_13271.HeightFogComponent_6298 | 19750.8 | 5e-06 | 151/196/217 × 1.5 | 75000 | 1e+08 | loaded |
| MP_IAC_Seed_ART_m:HeightFog_69.HeightFogComponent_6298 | 15222.1 | 1e-05 | 214/230/245 × 0.25 | 200 | 1e+08 | - |

**Post-process** (world = governing MP_IAC_Seed_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_IAC_Seed_BASE_m:PostProcessVolume_13331)

| Setting | World | PostProcessVolume_13331 (prio 1.0) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 15000 | 15000 |
| DOF_FocusNearFalloff | 2000 | 0 | 0 |
| DOF_FocusFarFalloff | 2000 | 2000 | 2000 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.4 | 0.4 |
| DOF_FalloffExponent | 4 | 1 | 1 |
| DOF_BlurKernelSize | 16 | 32 | 32 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0.2 | 0.2 |
| Scene_HighLights | (1, 1, 1) | (1, 1, 1) | (1, 1, 1) |
| Scene_MidTones | (1, 1, 1) | (0.9, 0.9, 0.9) | (0.9, 0.9, 0.9) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0) | (0, 0, 0) |
| ColorCorrectionTexture | None | desaturation40_CLR | desaturation40_CLR |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_IAC_Streets

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_11360_LC | 6 | 117/117/255 | - | (-0.51792, -0.54232, -0.66155) yaw -133.7° pitch -41.4° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_727_LC | 0.2 | 149/149/198 | - | (0.01227, 0, -0.99992) yaw 0.0° pitch -89.3° | CompositeDynamic | False / False |
| dynamic_sky_light | SkyLight_3181_LC | 0.1 | 118/114/197 | 157/203/236 × None | (0, 1, 0) yaw 90.0° pitch 0.0° | CompositeDynamic,Dynamic | False / False |

**Top 10 placed lights** (Brightness × Radius; 265 enabled point / spot lights; ours loads 268 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_10647_LC | Point | 8 | 9864.97 | 2 | 244/80/0 | [25888, -60748, -66262] |
| 2 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_2700_LC | Point | 8 | 8663.09 | 2 | 244/80/0 | [3664, -33879, -64912] |
| 3 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_10185_LC | Point | 8 | 8663.09 | 2 | 244/80/0 | [33584, -25653, -64885] |
| 4 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.SpotLight_15850_LC | Spot | 12 | 5223.81 | 2 | 196/224/255 | [7298, -42849, -67322] |
| 5 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.SpotLight_8117_LC | Spot | 8 | 6954.91 | 2 | 139/198/241 | [16775, -28928, -70766] |
| 6 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_13357_LC | Point | 8 | 6902.82 | 2 | 244/80/0 | [43024, -32392, -68162] |
| 7 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_6440_LC | Point | 8 | 6902.82 | 2 | 244/80/0 | [42210, -29390, -68162] |
| 8 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_14088_LC | Point | 8 | 6902.82 | 2 | 244/80/0 | [41267, -32392, -70057] |
| 9 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.SpotLight_13830_LC | Spot | 8 | 6879 | 2 | 187/189/255 | [18261, -27779, -65558] |
| 10 | MP_IAC_Streets_ART_m:StaticLightCollectionActor_2528.PointLight_4521_LC | Point | 8 | 6839.56 | 2 | 244/80/0 | [37432, -35884, -66641] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_IAC_Streets_ART_m:HeightFog_8248.HeightFogComponent_11010 | -57273.8 | 2e-05 | 116/91/234 × 0.1 | 2048 | 1e+08 | loaded |

**Post-process** (world = governing MP_IAC_Streets_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source Engine.Default__WorldInfo, mp_iac_streets_base_m.theworld.persistentlevel.tnworldinfo_15559)

| Setting | World | Ours |
|---|---|---|
| bEnableBloom | True | True |
| Bloom_Scale | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 |
| bEnableDOF | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 0 |
| DOF_FocusNearFalloff | 2000 | 2000 |
| DOF_FocusFarFalloff | 40000 | 40000 |
| DOF_MaxNearBlurAmount | 1 | 1 |
| DOF_MaxFarBlurAmount | 0.6 | 0.6 |
| DOF_FalloffExponent | 4 | 4 |
| DOF_BlurKernelSize | 16 | 16 |
| bEnableMotionBlur | False | False |
| MotionBlur_Amount | 0.5 | 0.5 |
| bEnableSceneEffect | True | True |
| Scene_Desaturation | 0 | 0 |
| Scene_HighLights | (1, 1, 1) | (1, 1, 1) |
| Scene_MidTones | (1, 1, 1) | (1, 1, 1) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0) |
| ColorCorrectionTexture | MP_Streets_CLUT | MP_Streets_CLUT |
| bAllowAmbientOcclusion | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_KON_Molten

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_10855_LC | 4 | 218/218/154 | - | (-0.54884, -0.04218, -0.83486) yaw -175.6° pitch -56.6° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | False / False |
| dynamic_sky_light | SkyLight_11706_LC | 0.1 | 141/169/163 | 255/255/255 × None | (1, 0, 0) yaw 0.0° pitch 0.0° | BSP,CompositeDynamic,Dynamic,Static | False / False |

**Top 10 placed lights** (Brightness × Radius; 299 enabled point / spot lights; ours loads 302 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.SpotLight_15541_LC | Spot | 9 | 11426.2 | 2 | 194/177/130 | [37079, 3356, 1950] |
| 2 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.SpotLight_7258_LC | Spot | 9 | 11426.2 | 2 | 194/177/130 | [31879, 9581, 1950] |
| 3 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.SpotLight_4334_LC | Spot | 9 | 9400 | 2 | 194/177/130 | [22500, -14496, 10709] |
| 4 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.SpotLight_1178_LC | Spot | 9 | 8000 | 2 | 194/177/130 | [16076, 3675, 9785] |
| 5 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.PointLight_15962_LC | Point | 1.5 | 35000 | 2 | 173/165/248 | [36622, -21059, 3631] |
| 6 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.PointLight_1519_LC | Point | 6 | 8227.29 | 3 | 248/104/5 | [-13006, -10567, -1959] |
| 7 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.PointLight_6986_LC | Point | 6 | 8227.29 | 3 | 248/104/5 | [-11188, -13042, -1977] |
| 8 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.PointLight_9204_LC | Point | 6 | 8227.29 | 3 | 248/104/5 | [-13739, -16134, -1959] |
| 9 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.PointLight_2370_LC | Point | 6 | 8227.29 | 3 | 248/104/5 | [-11244, -19508, -1959] |
| 10 | MP_KON_Molten_ART_m:StaticLightCollectionActor_1914.PointLight_15081_LC | Point | 6 | 8227.29 | 3 | 248/104/5 | [-12669, -22488, -1959] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_KON_Molten_ART_m:HeightFog_7568.HeightFogComponent_1882 | 9088 | 5e-05 | 215/210/191 × 0.15 | 8000 | 1e+08 | loaded |

**Post-process** (world = governing MP_KON_Molten_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_KON_Molten_ART_m:PostProcessVolume_12250)

| Setting | World | PostProcessVolume_12250 (prio None) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 0 | 0 |
| DOF_FocusNearFalloff | 2000 | 2000 | 2000 |
| DOF_FocusFarFalloff | 2000 | 80000 | 80000 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.5 | 0.5 |
| DOF_FalloffExponent | 4 | 4 | 4 |
| DOF_BlurKernelSize | 16 | 16 | 16 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0.1 | 0.1 |
| Scene_HighLights | (1, 1, 1) | (1.2, 1.2, 1.2) | (1.2, 1.2, 1.2) |
| Scene_MidTones | (1, 1, 1) | (0.95, 0.9, 0.85) | (0.95, 0.9, 0.85) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0) | (0, 0, 0) |
| ColorCorrectionTexture | None | clut_mp40 | clut_mp40 |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_ORB_Debris

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_10885_LC | 3 | 166/166/249 | - | (0.57846, 0.74772, -0.32604) yaw 52.3° pitch -19.0° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_14274_LC | 0.1 | 202/196/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | True / False |

**Top 10 placed lights** (Brightness × Radius; 119 enabled point / spot lights; ours loads 121 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_16168_LC | Spot | 6 | 8870.19 | 2 | 204/0/5 | [15149, -8213, 16887] |
| 2 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_8718_LC | Spot | 6 | 8870.19 | 2 | 89/29/248 | [-16459, -5085, 16577] |
| 3 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_4979_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [-4954, -997, 13302] |
| 4 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_7632_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [-3743, -3224, 13307] |
| 5 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_15813_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [-1915, -4662, 13307] |
| 6 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_6022_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [3364, -3788, 13307] |
| 7 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_4362_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [4681, -1924, 13307] |
| 8 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_11884_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [4950, 995, 13307] |
| 9 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_8754_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [1148, -4846, 12356] |
| 10 | MP_ORB_Art_m:StaticLightCollectionActor_14925.SpotLight_12296_LC | Spot | 4 | 11742 | 2 | 255/169/132 | [-4500, 2283, 12495] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| none | | | | | | |

**Post-process** (world = governing MP_ORB_Debris_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source Engine.Default__WorldInfo, mp_orb_debris_base_m.theworld.persistentlevel.tnworldinfo_14116)

| Setting | World | Ours |
|---|---|---|
| bEnableBloom | True | True |
| Bloom_Scale | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 |
| bEnableDOF | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 1200 | 1200 |
| DOF_FocusNearFalloff | 2000 | 2000 |
| DOF_FocusFarFalloff | 128000 | 128000 |
| DOF_MaxNearBlurAmount | 1 | 1 |
| DOF_MaxFarBlurAmount | 0.2 | 0.2 |
| DOF_FalloffExponent | 4 | 4 |
| DOF_BlurKernelSize | 2 | 2 |
| bEnableMotionBlur | False | False |
| MotionBlur_Amount | 0.25 | 0.25 |
| bEnableSceneEffect | True | True |
| Scene_Desaturation | 0.2 | 0.2 |
| Scene_HighLights | (1, 1, 1) | (1, 1, 1) |
| Scene_MidTones | (1, 1, 1) | (1, 1, 1) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0) |
| ColorCorrectionTexture | MP_OrbitalDebris_CLUT | MP_OrbitalDebris_CLUT |
| bAllowAmbientOcclusion | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); no Beast light-environment volume

### MP_UND_Complex

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_2990_LC | 6 | 183/181/151 | - | (-0.52219, 0.62448, -0.58081) yaw 129.9° pitch -35.5° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | False / False |
| dynamic_sky_light | SkyLight_11533_LC | 0.1 | 200/224/236 | 255/255/255 × None | (1, 0, 0) yaw 0.0° pitch 0.0° | BSP,CompositeDynamic,Dynamic,Static | False / False |

**Top 10 placed lights** (Brightness × Radius; 149 enabled point / spot lights; ours loads 152 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.SpotLight_16166_LC | Spot | 8 | 10000 | 2 | 248/176/124 | [4915, 11287, 2621] |
| 2 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_12410_LC | Point | 5 | 15630.5 | 2 | 104/185/193 | [1327, 14560, -12460] |
| 3 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_13184_LC | Point | 5 | 15630.5 | 2 | 104/185/193 | [-10753, 21061, -15853] |
| 4 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_9545_LC | Point | 5 | 15630.5 | 2 | 104/185/193 | [-6621, 30282, -15853] |
| 5 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_15330_LC | Point | 5 | 15630.5 | 2 | 104/185/193 | [-22913, 31437, -15853] |
| 6 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_8618_LC | Point | 5 | 11625.1 | 2 | 88/164/209 | [-16594, 5950, 8163] |
| 7 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_10297_LC | Point | 5 | 11625.1 | 2 | 88/164/209 | [-9979, -7592, 8163] |
| 8 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.PointLight_8947_LC | Point | 5 | 11625.1 | 2 | 88/164/209 | [1821, -12620, 7829] |
| 9 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.SpotLight_2383_LC | Spot | 8 | 5291.72 | 2 | 248/176/124 | [-4628, 2860, 2000] |
| 10 | MP_UND_Complex_ART_m:StaticLightCollectionActor_12572.SpotLight_8284_LC | Spot | 8 | 5291.72 | 2 | 248/176/124 | [-886, 2860, 2128] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_UND_Complex_ART_m:HeightFog_273.HeightFogComponent_12537 | 10912 | 3.5e-05 | 146/179/182 × 0.1 | 3200 | 1e+08 | loaded |
| MP_UND_Complex_ART_m:HeightFog_4771.HeightFogComponent_1470 | -13711.3 | 8e-05 | 102/213/255 × 0.3 | 0 | 1e+08 | - |

**Post-process** (world = governing MP_UND_Complex_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_UND_Complex_ART_m:PostProcessVolume_15305)

| Setting | World | PostProcessVolume_15305 (prio None) | Ours |
|---|---|---|---|
| bEnableBloom | True | False | False |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 7500 | 7500 |
| DOF_FocusNearFalloff | 2000 | 17500 | 17500 |
| DOF_FocusFarFalloff | 2000 | 25000 | 25000 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.25 | 0.25 |
| DOF_FalloffExponent | 4 | 8 | 8 |
| DOF_BlurKernelSize | 16 | 32 | 32 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0.1 | 0.1 |
| Scene_HighLights | (1, 1, 1) | (1.2, 1.2, 1.2) | (1.2, 1.2, 1.2) |
| Scene_MidTones | (1, 1, 1) | (0.87, 0.9, 0.92) | (0.87, 0.9, 0.92) |
| Scene_Shadows | (0, 0, 0) | (0, 0, 0) | (0, 0, 0) |
| ColorCorrectionTexture | None | clut_mp40 | clut_mp40 |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); Beast light-environment volumes: MP_UND_Complex_ART_m:BeastLightEnvironmentVolume_6465

### MP_UND_Gorge

**Sun / sky** (UE direction = light travel direction)

| Role | Light | Brightness | Colour sRGB | Lower (sky) | Direction | Channels | Shadows (static / dyn) |
|---|---|---|---|---|---|---|---|
| dominant_baked_directional | DirectionalLight_12837_LC | 2 | 234/223/208 | - | (0.29561, -0.37581, -0.87828) yaw -51.8° pitch -61.4° | BSP,CompositeDynamic,Static | True / False |
| dynamic_fill_directional | DirectionalLight_12173_LC | 0.2 | 255/255/255 | - | (0, 0, -1) yaw 0.0° pitch -90.0° | CompositeDynamic | False / False |

**Top 10 placed lights** (Brightness × Radius; 404 enabled point / spot lights; ours loads 411 lights)

| # | Light | Type | Brightness | Radius uu | Falloff | Colour | Location UE |
|---|---|---|---|---|---|---|---|
| 1 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_604_LC | Point | 10 | 18000 | 2 | 88/135/192 | [-8626, -359, -15934] |
| 2 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_15354_LC | Point | 20 | 4000 | 2 | 88/135/192 | [-15582, 8724, -6595] |
| 3 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.SpotLight_8462_LC | Spot | 10 | 4500 | 2 | 255/255/255 | [-7569, -20276, 2617] |
| 4 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_11914_LC | Point | 20 | 2000 | 2 | 88/135/192 | [-14963, 7647, -1729] |
| 5 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.SpotLight_11402_LC | Spot | 10 | 3500 | 2 | 255/255/255 | [-7576, -20301, 2499] |
| 6 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.SpotLight_1204_LC | Spot | 10 | 3500 | 2 | 255/255/255 | [-7649, 17449, 2369] |
| 7 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_10739_LC | Point | 5 | 5000 | 2 | 88/135/192 | [-11391, -2876, -10191] |
| 8 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_9326_LC | Point | 5 | 5000 | 2 | 88/135/192 | [-12886, -3830, -9463] |
| 9 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_10794_LC | Point | 5 | 5000 | 2 | 88/135/192 | [-3816, 5115, -11531] |
| 10 | MP_UND_Gorge_ART_m:StaticLightCollectionActor_1343.PointLight_26_LC | Point | 4 | 5000 | 2 | 124/81/255 | [-7553, 9928, 5294] |

**Fog**

| Component | Height uu | Density | Inscatter colour × brightness | Start | Extinction | Ours |
|---|---|---|---|---|---|---|
| MP_UND_Gorge_ART_m:HeightFog_12129.HeightFogComponent_546 | -1530.44 | 9e-05 | 113/147/202 × 0.1 | 0 | 1e+08 | loaded |
| MP_UND_Gorge_ART_m:HeightFog_2860.HeightFogComponent_546 | 23205.9 | 3e-06 | 149/191/215 × 0.6 | 500 | 1e+08 | - |
| MP_UND_Gorge_ART_m:HeightFog_4020.HeightFogComponent_546 | 38003.2 | 1.5e-05 | 165/187/211 × 1.5 | 20000 | 1e+08 | - |
| MP_UND_Gorge_ART_m:HeightFog_6102.HeightFogComponent_546 | -12772.6 | 0.0001 | 102/131/198 × 10 | 10000 | 1e+06 | - |

**Post-process** (world = governing MP_UND_Gorge_BASE_m TnWorldInfo effective; volumes; ours = Rebuild lighting.json postprocess.settings, source MP_UND_Gorge_ART_m:PostProcessVolume_13331)

| Setting | World | PostProcessVolume_13331 (prio 1.0) | Ours |
|---|---|---|---|
| bEnableBloom | True | True | True |
| Bloom_Scale | 1 | 0.1 | 0.1 |
| Bloom_Threshold | 1 | 1 | 1 |
| bEnableDOF | False | True | True |
| DOF_FocusType | FOCUS_Distance | FOCUS_Distance | FOCUS_Distance |
| DOF_FocusDistance | 0 | 15000 | 15000 |
| DOF_FocusNearFalloff | 2000 | 2000 | 2000 |
| DOF_FocusFarFalloff | 2000 | 2000 | 2000 |
| DOF_MaxNearBlurAmount | 1 | 0 | 0 |
| DOF_MaxFarBlurAmount | 1 | 0.5 | 0.5 |
| DOF_FalloffExponent | 4 | 4 | 4 |
| DOF_BlurKernelSize | 16 | 32 | 32 |
| bEnableMotionBlur | False | False | False |
| MotionBlur_Amount | 0.5 | 0.5 | 0.5 |
| bEnableSceneEffect | True | True | True |
| Scene_Desaturation | 0 | 0 | 0 |
| Scene_HighLights | (1, 1, 1) | (1.5, 1.5, 1.5) | (1.5, 1.5, 1.5) |
| Scene_MidTones | (1, 1, 1) | (0.86, 0.97, 0.97) | (0.86, 0.97, 0.97) |
| Scene_Shadows | (0, 0, 0) | (-0.001, -0.001, -0.001) | (-0.001, -0.001, -0.001) |
| ColorCorrectionTexture | None | clut_mp40 | clut_mp40 |
| bAllowAmbientOcclusion | False | False | False |

**Characters**: robot: TransGame.Default__TnRobotForm.LightEnvironment0 (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); truck_vehicle: TransGame.Default__TnTruckForm.MyLightEnv (UpdateDistanceThreshold 30, TotalLightCount 2, bEnabled True, EnvironmentTransitionTime 0.5); weapon: TransGame.Default__TnWeaponMesh.MyLightEnvironment (bEnabled True, EnvironmentTransitionTime 0.5, UpdateDistanceThreshold 50, TotalLightCount 1); Beast light-environment volumes: MP_UND_Gorge_ART_m:BeastLightEnvironmentVolume_2219
