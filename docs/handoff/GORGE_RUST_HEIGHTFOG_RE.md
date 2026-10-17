# Gorge / Rust height fog: authored layers and the original's fog math (RE, 2026-10-18)

Labels: CONFIRMED (C) = cooked data / shader microcode; HIGH (H) = stock UE3 CPU-side reading not traced in the WFC binary.

## (1) Authored layers (C, authored.db HeightFogComponent + the owning HeightFog actor)

`Height` equals the actor's Location.Z in every case. Missing fields take the CDO / class defaults:
Density 5e-5, LightBrightness 0.1, bEnabled true, ExtinctionDistance 1e8, StartDistance 0.

| Map | Component | Height uu | Density | LightBrightness | LightColor sRGB | StartDistance | ExtinctionDistance | bEnabled |
|---|---|---|---|---|---|---|---|---|
| MP_UND_Gorge | HeightFog_4020 | 38003.2 | 1.5e-5 | 1.5 | 165/187/211 | 20000 | 1e8 | yes |
| | HeightFog_2860 | 23205.9 | 3e-6 | 0.6 | 149/191/215 | 500 | 1e8 | yes |
| | HeightFog_12129 | −1530.4 | 9e-5 | **0.1 (default)** | 113/147/202 | 0 | 1e8 | yes |
| | HeightFog_6102 | −12772.6 | 1e-4 | **10** | 102/131/198 | 10000 | **1e6** | yes |
| MP_IAC_Rust | HeightFog_2815 | 23271.7 | (5e-5) | 0.5 | 135/197/220 | 10000 | 1e8 | **no (bEnabled False)** |
| | HeightFog_6999 | 18878.0 | 2e-6 | 1.0 | 185/222/236 | 0 | 1e8 | yes |
| | HeightFog_13733 | −18561.9 | 1e-4 | **0.0** | 217/147/43 | 20000 | 1e8 | yes |

Rendering's readings match.

**Rust's −18562 layer** has LightBrightness **0**: it adds no light and only extinguishes (darkens). Below −18562 the scene goes
toward black, not grey-blue. Only one Rust layer adds colour (18878, weak).

## (2) The 360 fog shader (C, base-pass VS microcode, MP_UND_Gorge_BASE_m ShaderCache)

- **Where it was read:** the material base-pass VS carrying the fog constants (124 in Gorge's cache; one decoded in full, with
  `tools/xenosdis.py` at the blob's microcode offset +0x3D8).
- **Constants:** FogDistanceScale c13, FogExtinctionDistance c14, FogInScattering c15–c18 (one register per layer), FogStartDistance c19,
  FogMinHeight c20, FogMaxHeight c21. Each per-layer scalar is a float4 (.x = layer 0 … .w = layer 3).

**Per layer i, per vertex:**
1. `t_enter / t_exit = saturate((FogMin/MaxHeight[i] − CameraZ) / Δz)` (MULv_sat): the fraction of the camera → vertex ray inside
   the layer's height slab.
2. `d = max(|ray| − FogStartDistance[i], 0)`. Note: **StartDistance is subtracted from the whole ray length**, not from the in-slab part.
3. `in = |t_exit − t_enter| × d`: the in-slab distance past the start, in **UU**.
4. `scatter[i] = exp2(FogDistanceScale[i] × in)`, **forced to 0 when in ≥ FogExtinctionDistance[i]** (SETGT then multiply).
5. **Composite, layers in register order 0 → 3:** `fog.rgb = fog.rgb × scatter[i] + FogInScattering[i] × (1 − scatter[i])`,
   `fog.a *= scatter[i]` (an initial `ApproxFogColor` / fog-volume term is folded in).
   - **No saturate, no clamp, no MaxOpacity anywhere in the chain.** FogInScattering is used as given.
6. **Output:** fog.rgb and fog.a go out as interpolators. The pixel side is (H, stock) `colour × fog.a + fog.rgb`. Every WFC material
   PS then ends with `oC0.rgb = result × SceneColorBiasFactor` (C, HDR_BLEND_CLAMP_RULE_RE.md), so the fog is in the **linear HDR
   scene colour, before any tone / post pass**. It is bounded only by the 7e3 store: **3.984 real per channel per draw**.

**CPU fill (H, stock UE3; the setter was not reached in the 1 h cap):**
- `FogDistanceScale = −Density / ln 2` per UU;
- `FogInScattering = FLinearColor(LightColor) × LightBrightness`: sRGB-decoded colour × brightness, **unclamped**;
- FogMin / MaxHeight: the fogs sorted by Height, layer i spanning [the next lower fog's Height, its own Height], the lowest down to
  −HALF_WORLD_MAX;
- at most 4 per view, which matches the 4-wide constants.

Rendering's current implementation is exactly this form. The **sort direction** (which layer is register 0) is the remaining H. It
decides only the composite order: the layer composited last is not attenuated by the others.

## (3) Are all layers active? (C for data, H for culling)

- **Gorge:** all 4 enabled, which fills the shader's 4 slots exactly. No layer is beyond the limit.
- **Rust:** 2 enabled; HeightFog_2815 is bEnabled False.
- All seven actors are in the ART sublevels (always loaded with the map).
- There are no per-map fog switches: WorldInfo carries none (no authored fog / lighting-pass field in the MP TnWorldInfos;
  MAP_LIGHTING_REFERENCE_RE.md). Xe-TransEngine.ini SystemSettings has no height-fog setting (only `FogVolumes=True`, which is for
  the constant-density volumes).
- So nothing disables or culls layers in the shipped build (H: no culling beyond 4).

## Is "flat white" the authored look? Worked values (linear, before the 7e3 cap)

| Layer | FogInScattering (linear) | 1 − scatter at a typical in-slab distance |
|---|---|---|
| Gorge −12773 (× 10) | **(1.33, 2.27, 5.65)** | 0.63 at 10,000 uu in-slab |
| Gorge −1530 (× 0.1) | (0.017, 0.029, 0.059) | weak |
| Gorge 23206 (× 0.6) | (0.18, 0.31, 0.41) | 0.16 at 59,500 uu (a horizontal view from gameplay height) |
| Gorge 38003 (× 1.5) | (0.56, 0.75, 0.98) | 0.36 at 30,000 uu in-slab |
| Rust 18878 (× 1.0) | (0.49, 0.73, 0.84) | 0.095 at 50,000 uu, 0.18 at 100,000 uu |
| Rust −18562 (× 0) | (0, 0, 0) | 0.63 at 10,000 uu: a darkening |

**Reading:**
- **Gorge:**
  - Only rays that go **down into the abyss below −12773** get the × 10 layer. Its in-scatter (0.84, 1.43, 3.57 at 63 %) caps at
    3.98 and displays as **pale cyan-white**: authored, for the openings that look down into the valley.
  - **Horizontal** views from gameplay height (inside the 23206 layer) get only ~16 % of a weak blue, and sky rays ~36 % of a pale
    blue. Neither should be flat white.
- **Rust:** the only coloured layer is weak (≤ 18 % even at 100,000 uu), so a **visible grey-blue floor fill is too strong**. Below
  −18562 the original darkens.

**Checks for Rendering (most likely strength errors):**
1. **Units:** the distance must be in **UU** with `−Density / ln 2` per UU. Metres × the per-UU scale is 100× too weak, and the
   reverse is 100× too strong.
2. **StartDistance** is subtracted from the whole ray length before the in-slab fraction is applied, not from the in-slab distance.
3. **ExtinctionDistance** (Gorge low layer 1e6) forces scatter to 0 beyond it.
4. LightBrightness **0** (Rust low) must give no in-scatter. The **defaults** apply where the field is absent: Gorge −1530 has
   brightness 0.1, not 1.
5. No clamp before the scene's 7e3 store. In Off, the original's 3.984 cap applies after the blend.
