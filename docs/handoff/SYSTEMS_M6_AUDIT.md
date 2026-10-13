# M6 Modern materials: classification audit + the "pbr" mapping contract (Systems, 2026-10-12; revised after Experimental's review)

Tools:
- tools/systems/m6/m6_audit.py: the audit, mapping, targets and oracle vectors.
- tools/systems/m6/glsl_emu.py: the emulator, which defines the base-colour strip.
- tools/systems/m6/tint_channels.py: which texture channel masks the customisation tint.

They read the render data's materials_glsl.json for all 10 maps and the materials' own textures from ExtractedAssets, read-only. m6_mapping.json (about 6 MB) and m6_vectors.json (the oracle) are regenerated into work/m6.

Shipped here:
- SYSTEMS_M6_WORKLIST.json: AssetTools' work list with the targets.
- SYSTEMS_M6_TINT_CHANNELS.json.
- SYSTEMS_M6_LOW_BASE.json: the low-base review list.

## The contract for matc's "pbr" mode (Rendering implements; this file and glsl_emu define it)

**baseColor = DiffuseColor under the base-colour strip, then max(., 0).** Every value carries a "depends on the view" flag. It is set at m.cameraVector, m.reflectionVector, m.lightVector and every cube sample, and propagated through all operations.

1. **Additive view terms are zeroed.** At a colour sum (+ or -, both operands vec3 / vec4) where exactly one operand depends on the view, the result is the view-independent operand.
   - This removes the cube reflection added into DiffuseColor.
   - It also removes the anisotropic camera specular of WEP_PhongMain and ENV_PhongMain_NS. That term is pow(sqrt(1 - dot(V, T)^2), n) * colour * k, ADDED to the diffuse chain, so it is additive and zeroed.
   - Scalar arithmetic inside a view term (1 - dot^2, falloffs) is not a boundary.
2. **A remaining multiplicative Fresnel / view factor is replaced by its view-independent mean.** If DiffuseColor still depends on the view after rule 1, the base colour is its mean over the 32 directions of glsl_emu.VIEW_SET (cube samples held fixed).
   - This is never "1": the mean preserves average brightness.
   - It occurs in 1 material today.
3. **Only the base colour is stripped.** EmissiveColor, SpecularColor, SpecularPower and Opacity are evaluated normally. 113 emissives read the cube; they keep it (Experimental 2c).

**Roughness** = clamp(sqrt(2 / (SpecularPower + 2)), 0.02, 1) per pixel, from the material's own SpecularPower.
- hdPbrTexture(roughness_parent, "roughness") replaces it.
- The generated map's mean is normalised to the material's target_mean_roughness, its derived mean from its own textures.

**Metallic** = 0 and **F0** = 0.04 everywhere until reviewed metal rows exist.
- The renderer reads an ORM metal channel only for a reviewed (texture, material) row.
- Customisation-tint consumers (403) are forced dielectric.
- HUD-marker / team-state materials (FriendlyColor, EnemyColor, AutobotColor, DecepticonColor, TeamID, NeutralColor, Flashing, PingOpacity) are never remapped.

**Oracle (m6_vectors.json)**: per material, 3 fixed-seed sample tables plus 1 with every runtime parameter set (uRT_* = 1: Overheat / Defrag / EnergonColor ...).
- It records the view inputs (top level: camera, reflection, light, view_set) and the expected baseColor (+ which rule applied), diffuseOriginal, roughness, metallic, F0, emissive and specularPower.
- matc's pbr GLSL must reproduce baseColor / roughness / emissive through glsl_emu.run within 1e-6.

## Revisions after Experimental's review

- 1a: HUD-marker parameters are never remapped (1 material).
- 1b / 1c:
  - ORM candidacy is per (texture, material): orm_candidate_materials in the work list.
  - The name match uses whole words, so "Ironhide" no longer matches "iron".
  - Robot interiors and weapon metals no longer inherit a texture-level flag.
- 1d:
  - Each unique material is classified once, with a seed fixed per material.
  - The class uses means over 128 points of the material's own textures, decoded as the GPU sees them.
  - The spec/base ratio is ignored at a base luminance below 0.02.
- 2b: the view strip above covers the 38 anisotropic-specular materials. Rule 1 never brightens the base (checked: 0 cases).
  - WEP_PhongMain keeps a median 73 % of the original diffuse luminance (min 3 %), ENV_PhongMain_NS a median 94 %.
- 2c: the emissive is untouched, and the runtime-set vectors are added.
- 3: the runtime ternary uRT_X(.swizzle) form is parsed. All 908 remapped materials emulate (was 871 of 909).
- 4: the roughness parent is picked deterministically (mask / spec / gloss-named first, then by name). The targets are the materials' own derived means (median 0.38, p10 0.26, p90 0.54).
- Low base after the strip: 19 materials keep under 25 % of their diffuse (mostly vehicle / weapon paint whose look is mostly reflection) in SYSTEMS_M6_LOW_BASE.json.
  - They are the strongest metal-review candidates. Without a reviewed metal row they would read too dark under Modern.

## Tint mask channels (for AssetTools' E pass: never generate inside these)

| master | texture | channel |
|---|---|---|
| ENV_AllShader_p.Release.ENV_PhongMain_NS_Mat | the TEXSET Masks (..._Masks_NormY) | **B** (Cust A / B blend; G is the gloss / spec mask, R the emissive mask, A the normal's Y) |
| WEP_Rocket_p.WEP_Projectile_MAT | ..._Masks_NormY | **B** |
| WEP_Shared_p.WEP_PhongMain_MAT | the weapon's _MASK texture | **B** |
| TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat, TR_Defrag_MAT | the separate CustAB_AuxGlow(Map) texture | **R** (the whole texture is mask data: leave it untouched) |
| WEP_TurretIonGun_p.WEP_TurretIonGun_MAT | the colour texture (TurretIonGun_CLR) | **G** (an outlier: its colour texture's G drives the tint) |

## Audit output

Maps: MP_ESC_BrokenHope, MP_ESC_Remnant, MP_IAC_Berth, MP_IAC_Rust, MP_IAC_Seed, MP_IAC_Streets, MP_KON_Molten, MP_ORB_Debris, MP_UND_Complex, MP_UND_Gorge. Unique materials: 1336.

| class | unique materials |
|---|---|
| dielectric | 541 |
| dielectric_tint | 342 |
| excluded | 427 |
| metal_candidate | 25 |
| never_remap | 1 |

## Per family

| family | dielectric | dielectric_tint | excluded | metal_candidate | never_remap |
|---|---|---|---|---|---|
| DES | 11 | 0 | 0 | 0 | 0 |
| DESIGNER | 3 | 0 | 0 | 0 | 0 |
| ENGINEMATERIALS | 1 | 0 | 1 | 0 | 0 |
| ENV | 382 | 219 | 175 | 16 | 0 |
| FX | 1 | 1 | 130 | 0 | 1 |
| PROP | 66 | 11 | 33 | 8 | 0 |
| TPRO | 14 | 6 | 2 | 0 | 0 |
| TR | 47 | 66 | 58 | 0 | 0 |
| UI | 0 | 0 | 17 | 0 | 0 |
| WEP | 16 | 39 | 11 | 1 | 0 |

## Emulation (glsl_emu; real textures for the targets)

- emulated: 908 of 908 remapped materials; not emulated: 0 ()
- base-colour strip: no view term 150, additive view terms dropped (rule 1) 757, multiplicative view factor averaged (rule 2) 1
- rule-1 base colour brighter than the original diffuse (must be 0): 0
- SpecularPower -> roughness -> SpecularPower: max relative error 5.51e-16
- customisation-tint consumers (forced dielectric): 403; HUD-marker materials (never remapped): 1
- metal candidates (PROVISIONAL, for review; not applied): 25

## AssetTools work list

- 489 unique parent textures need a roughness map (BC4, half the colour size), each with its target mean roughness; 18 have at least one (texture, material) ORM candidate

Metal candidates (review list):

- ENV_CORE_Deco_p.Textures.ENV_CORE_SupportDecoB_MATINST (ENV): strong, coloured specular (sat 0.32, spec/base lum 5.97)
- ENV_CORE_SupportColumns_p.Textures.Core_CorruptedVines_MAT (ENV): strong, coloured specular (sat 0.46, spec/base lum 15.96)
- ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT (ENV): name word: metal
- ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT (ENV): name word: metal
- ENV_IAC_Ground_p.Material.GroundVent_MATINST (ENV): strong, coloured specular (sat 0.40, spec/base lum 5.98)
- ENV_IAC_Turret_p.Texture.IAC_searchlight_RED_MATINST (ENV): strong, coloured specular (sat 0.35, spec/base lum 19.62)
- ENV_IAC_Turret_p.Texture.IAC_searchlight_purple_MATINST (ENV): strong, coloured specular (sat 0.66, spec/base lum 12.49)
- ENV_KON_Deco_p.Materials.BackgroundIsland_03_MATINST (ENV): strong, coloured specular (sat 0.44, spec/base lum 6.50)
- ENV_NEU_Structures_p.Big3.Big3_Building3Walkway_NoEmiss_MATINST (ENV): strong, coloured specular (sat 0.48, spec/base lum 14.41)
- ENV_NEU_Variation_p.ENV_F_rubblefloor_dark_CLR_Mat (ENV): strong, coloured specular (sat 0.75, spec/base lum 4.54)
- ENV_NEU_Variation_p.ENV_MetalPlates_1_Mat (ENV): name word: metal
- ENV_NEU_Variation_p.ENV_ebarrel_CLR_Mat (ENV): strong, coloured specular (sat 0.48, spec/base lum 1.39)
- ENV_NEU_Variation_p.ENV_groundblobs_CLR_Mat (ENV): strong, coloured specular (sat 0.71, spec/base lum 2.61)
- ENV_ORB_D1_Debris_p.Materials.MoltenMetalRing_SLAG_DULL_MATINST (ENV): name word: metal
- ENV_ORB_DecoTrim_p.Structural_Trim.MetalTest (ENV): name word: metal
- ENV_ORB_DecoTrim_p.Structural_Trim.Piston_Housing_Chrome_MATINST (ENV): name word: chrome
- PROP_IAC_Statues_p.IAC_Statue05_red_MATINST (PROP): strong, coloured specular (sat 0.37, spec/base lum 3.33)
- PROP_IAC_Statues_p.Materials.IAC_Statue01_MATINST (PROP): strong, coloured specular (sat 0.34, spec/base lum 3.87)
- PROP_IAC_Statues_p.Materials.IAC_Statue01_purple_MATINST (PROP): strong, coloured specular (sat 0.35, spec/base lum 5.74)
- PROP_IAC_Statues_p.Materials.IAC_Statue03_MATINST (PROP): strong, coloured specular (sat 0.33, spec/base lum 3.54)
- PROP_IAC_Statues_p.Materials.IAC_Statue04_aqua_MATINST (PROP): strong, coloured specular (sat 0.32, spec/base lum 3.56)
- PROP_IAC_Statues_p.Materials.IAC_Statue04_purple_MATINST (PROP): strong, coloured specular (sat 0.46, spec/base lum 1.58)
- PROP_IAC_Statues_p.Materials.IAC_Statue04_red_MATINST (PROP): strong, coloured specular (sat 0.71, spec/base lum 1.49)
- PROP_IAC_Statues_p.Texture.BB_ancestor_Statue_MATINST (PROP): strong, coloured specular (sat 0.43, spec/base lum 7.18)
- WEP_Rocket_p.WEP_Rocket_Temp_MAT (WEP): strong, coloured specular (sat 1.00, spec/base lum 1.00)
