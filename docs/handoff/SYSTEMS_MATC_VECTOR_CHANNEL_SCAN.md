# matc vector-parameter channel scan (Systems, 2026-10-17)

**Cause (found):** tools/render/matc.py `x_VectorParameter` returns the expression's selected output pin (1..4 = R / G / B / A)
only for materials listed in VECTOR_CHANNEL_PROVEN (vector_channel_proven.txt) or with WFC_MATC_VECTOR_CHANNELS=1; for every
other material it returns the whole vec4, so a graph that takes Color.A (an intensity) gets Color.rgba (a tint). DeathDecal_MAT:
weight G x Color.A (1) became G x Color.rgb (20) -> m max 25.7 instead of 2.24 (Rendering's check), the white / black scorch.

**Method:** the 10 MP maps' material sets built twice with af4d0731's tools/render (build_materials_only.py; WFC_ASSETTOOLS set):
as shipped (reproduces the shipped materials_glsl.json exactly on all 10 maps: 0 differing of 574-669 entries per map) and with
WFC_MATC_VECTOR_CHANNELS=1; per material the GLSL diffed and the selected literal channel compared with the components the
shipped code used (tools/systems/matscan_diff.py, matscan_class.py, matscan_show.py; data: docs/handoff/matscan/).

**Result: 116 materials' GLSL changes; 108 change value** (the selected channel differs from the rgb the shipped code used),
7 are equivalent (uniform vectors, e.g. DecalDefault (1, 1, 1, 1)), 1 not classified (a non-literal source). No compile error
with the channels honoured. The 108 are mostly one UE3 idiom - a colour VectorParameter whose alpha is an intensity:
light shafts / volumes (LightRaysV2* / LightVolume*: (tint, 1-2).w), force fields and glass (ENV_ForceField*, IAC_Glass*,
barricades: (blue, 3).w), energon plasma / lava ((rgb, 35-150).w), rockets ((1.25, 0.05, 0.05, 2).w), the decals
(DeathDecal (20, 20, 20, 1).w, MetalDecal (5, 5, 5, 1).w), vending glass ((rgb, 5).w), shields, the Omega battery, the
corruptor crystals ((0, 0.03, 0.46, 10).w). Full list with the selected values: docs/handoff/matscan/scan_classes.json.

**What it means:** today these draw with intensity x tint where the graph has intensity x scalar (or the reverse), in every
mode incl. Off / Faithful. Honouring the pin is the faithful translation (the pin is authored in the graph; DeathDecal is
proven against the shipped PS by RE). The visual change is broad (glows, glass, force fields, shafts on every map), so the
regen needs a per-map eye check before it ships, and ideally a microcode spot check on 2-3 representative materials (one
LightRaysV2, one ForceField, one plasma) the way DeathDecal was proven.

## (B) step 1: microcode spot check against the shipped PS (2026-10-17) - PASS on all three
Tools: tools/systems/vcspot/scmap_all.py (Rendering's scmap.py without its lightmapped-only filter: every PS of a map's
ShaderCache exports with its shader-map friendly names; run on MP_IAC_Seed / MP_UND_Complex) and swz.py (the swizzles each
UniformVector_N register is read with, via tools/render/xenos_dis.py). UE3 emits one UniformVector per VectorParameter.
| representative (in the 108) | shipped PS | how the vector parameter is read |
|---|---|---|
| force field: ENV_AllShader_p.Special.ENV_ForceField_ALL_MAT (master of ENV_ForceField_MATINST, BarricadeShield ...) | Seed ShaderCache_12865 @ 275968, NoLightMap base pass | UniformVector_1: `.xyz` (l. 39, the tint) AND `.wwww` as a separate multiplier (l. 40, 43: the alpha intensity) |
| energon plasma: ENV_CORE_Complex_p.Material.EnergonPlasmaAquaLight_INST ((rgb, 150).w) | Complex ShaderCache_8932 @ 299975 | UniformVector_6: `.x` / `.yz` (l. 8, 80, the tint) AND `.wwww` / scalar `.w` (l. 81, 87, the intensity) |
| light shafts: ENV_CORE_Complex_p.Material.LightCylinder_Rays_Blue_less_MATINST (LightVolume_Base_MAT family) | Seed ShaderCache_6631 @ 89371 | UniformVector_1.w read alone as a scalar multiplier (l. 48, muls_prev), the tint vectors separately |
**Verdict:** the shipped shaders read the parameter's alpha as its own intensity term next to the rgb tint - exactly what
the pin-honouring translation (WFC_MATC_VECTOR_CHANNELS=1) emits, and what today's full-vector reading gets wrong (it
multiplies by the rgb tint where the shader multiplies by the alpha). The spot check does not prove each of the 110
individually; the per-map eye pass (step 3) is the gate for all of them.

## (B) review finding (AssetTools, 2026-10-17): the two vending-screen MICs change their UV math - justified by the graph
PROP_VendingMachine_p.Ammo.PROP_VendingMachine_Ammo_MATINST and PROP_VendingMachine_p.PROP_VendingMachine_EnergonCube_MATINST
(MICs of Health_Screen_MATINST -> master PROP_VendingMachine_p.Material.VendingMachine_Screen_MAT) change outside the
alpha-intensity pattern. authored.db, the master's graph (MaterialExpressionTree_2411):
- VectorParameter_15751 "UV_ScaleXY_OffsetXY", default (R 1.85, G 1.10, B 0.27, A -0.02) - one parameter carrying two vec2s;
- AppendVector_13971 = (pin 1 R, pin 2 G) = the UV **scale** (1.85, 1.10) -> Multiply_8674 (x Constant_9596 -0.5) -> Subtract_7344;
- AppendVector_13310 = (pin 3 B, pin 4 A) = the UV **offset** (0.27, -0.02) -> Add_11512 (Subtract_7344 + offset).
The shipped translation (full-vector reading) used C.xy for both Appends, so the screen glyphs were offset by (1.85, 1.10)
instead of (0.27, -0.02) (and matc counted 5 type violations: vec4 + vec4 into a vec2 slot); the candidate builds
vec2(C.x, C.y) and vec2(C.z, C.w) exactly as the two AppendVectors' pins say (1 violation left, unrelated). This is the same
root cause (the output pin), applied to an AppendVector instead of a lerp alpha - a fidelity correction, in the expected-change
list for the vending machines' screens.

## (B) eye-pass findings (Experimental MATB_matB.md, 2026-10-17): both authored per the shipped PS
- **Milky glass** (Gorge 'firefight' glass floor, Debris 'reflective' pool):
  - Entries: IAC_Glass2_Dark_MATINST (Gorge) and IAC_Glass_Trans_MATINST (Debris), both MICs of ENV_AllShader_p.Special.ENV_ForceField_ALL_MAT
    (BLEND_Additive, MLM_Unlit). IAC_Glass_Opaque_MATINST (ENV_Refractive_Glass_Opaque_Mat, its alphas 1.0) changes slightly too.
  - Authored graph: FresnelColor_Intensity is used twice, Multiply_11030.A full vector and Multiply_9356.B pin 4 (the MIC's alpha 3.0).
  - Shipped PS (Seed ShaderCache_12865 @ 275968):
    - l.39 `mul r5.yzw, r5.y, c7{UniformVector_1}.xxyz` (by the rgb);
    - l.40 `mad r5.zw, r5.zw, c7{UniformVector_1}.wwww, r2.xy` and l.43 `mad r5.y, r5.y, c7{UniformVector_1}.wwww, r2.x` (by the alpha).
  - Verdict: the original multiplies by the rgb and then by the alpha intensity, as the candidate does. Live squared the colour and never
    applied the x3, so live was 5-20x too dark per channel. The additive fresnel sheen is authored. An OPAQUE-looking sheet would instead
    point at our BLEND_Additive compositing (a renderer question, not data).
- **Berth mid-ground veil:** the LightVolume_Base_MAT family entries (LightVolumeCool_LessLessDense, LightVolumeWarmThink, KON
  LightVolumeCool). Step 1 decoded this family's shipped PS (Seed ShaderCache_6631 @ 89371): the alpha is read alone as a scalar
  intensity. So the brighter volumes are the authored intensity.
- **Outcome:** (B) switches in whole (Integration).
- **Debris pool, OmegaLiquid_MATINST:**
  - Master ENV_ORB_PowerCorridor_p.Materials.TryptiGelGlass_Mat (BLEND_Translucent); its graph is node-for-node identical to
    ENV_Refractive_Glass_Opaque_Mat.
  - Shipped base-pass PS, MP_ORB_Art_m ShaderCache_1203 @ 127734:
    - l.72 `mul r8.xyz, c8{UniformVector_2}.xzyy, c8{UniformVector_2}.wwww`: Edge_Fresnel_Color's rgb x its own alpha (the authored
      Multiply_4383, A pin 0, B pin 4);
    - Refractive_Caustic_Color read as rgb (l.72-74) and separately as `.wwww` (l.77).
  - Verdict: the candidate (colour x alpha) is the original; live squared the colour.
  - Experimental's pool measurement fits: after = 1.06 x before + 18.6 levels, detail kept.
