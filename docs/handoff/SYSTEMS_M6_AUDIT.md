# M6 Modern materials: classification audit + the "pbr" mapping contract (Systems, 2026-10-12; revised after Experimental's review)

Tools:
- tools/systems/m6/m6_audit.py: the audit, mapping, targets and oracle vectors.
- tools/systems/m6/glsl_emu.py: the emulator, which defines the base-colour strip.
- tools/systems/m6/tint_channels.py: which texture channel masks the customisation tint.

They read the render data's materials_glsl.json for the 10 MP maps and the 5 UI_* dirs, and the materials' own textures from ExtractedAssets, read-only. m6_mapping.json (about 6 MB) and m6_vectors.json (the oracle) are regenerated into work/m6.

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
   - Superseded: a material that still needs rule 2 keeps the Original shading (see "Rule 2 keeps the Original shading" below).
3. **A cube-derived factor multiplying a texture-derived colour (vec3/vec4, not a broadcast scalar) is kept, not dropped.** The cube stands for its view-independent mean; matc uses the cube map's mean colour, its 1x1 mip.
   - This is the robot paint x cube x 0.2 of CHR_Transformer_NormSpec_Cust_E (all 67 robot bodies) and the ENV tint x cube (rule 3; 74 materials).
   - It does not cover a camera-dependent factor (the anisotropic specular stays rule 1).
   - It does not cover a cube x constant tint x scalar mask (the ENV reflection term stays rule 1).
4. **Then max(., 0).** Oversaturation mixes such as mix(c, lum(c), -0.15) can go negative.
5. **Only the base colour is stripped.** EmissiveColor, SpecularColor, SpecularPower and Opacity are evaluated normally. 113 emissives read the cube; they keep it (Experimental 2c).

**Roughness** = clamp(sqrt(2 / (SpecularPower + 2)), 0.02, 1) per pixel, from the material's own SpecularPower.
- hdPbrTexture(roughness_parent, "roughness") replaces it.
- The generated map's mean is normalised to the material's target_mean_roughness, its derived mean from its own textures.

**Metallic hooks (reviewed rows, Integration / Experimental 2026-10-12).** Per material, m6_mapping.json's `pbr_hooks` gives GLSL expressions over the body's own temporaries: `metallic`, `f0` (vec3) and `baseColor` (null = the stripped base), plus `glsl_hash`: the SHA-1 hex of the material's materials_glsl.json "glsl" string, UTF-8 as stored (the row also carries it as `glsl_sha1`). The hooks name temporaries by number, so matc_pbr skips a row with a warning when the hash differs. Every hook temporary is defined before the o.DiffuseColor line (checked: 103 rows). matc emits them as wfcPbrMetallic / wfcPbrF0 (and the base override). The shading uses diffuse = baseColor x (1 - metallic) and F0 = mix(0.04, f0, metallic). The vectors carry expect.metallic, expect.f0 (the hook's f0, unmixed) and expect.baseColor. Without a hook: metallic 0, f0 0.04.
- **metallic_paint** (CHR_Transformer_NormSpec_Cust_E, 69 robot bodies incl. 2 from the UI dirs): f0 = the tinted paint P bounded as P / max(1, max channel), which keeps the hue; baseColor = null (the stripped rule-3 base, which keeps the 1.5 x paint x DL diffuse term); metallic = 0.8, constant. The paint reaches about 2x on random sample tables (mask.r + sigmoid); on the real textures, 0 of 2,010 customised pixels exceed 1.
  - The paint is rule 3's colour operand, the TINTED paint mix(albedo, Cust A / B, masks), never SpecularColor. The temporary varies per material (t41 in 57, t40, t34, t46).
  - A dielectric share is kept: diffuse = base x 0.2. EnergonColor and the emissive are untouched.
  - Why baseColor is not P (Experimental's re-review): P carries the gloss pattern twice and drops the diffuse-luminance term, so baseColor = P put the gloss into the diffuse share and lost DL (L* correlation with Original 0.83; 0.98 with the stripped base).
  - Exit check (tools/systems/m6/chr_paint_check.py): CIEDE2000 per pixel, the IBL modelled as env x (F0 A + B) x occ (Karis' EnvBRDFApprox at the material roughness, NoV 0.3 / 0.6 / 0.9; env = the cube mean; occ 1). Results are below; the gate does not pass yet.
- **weapon_metal_mask** (WEP_PhongMain, 36 materials): metallic = clamp((1 - mask.b) x mask.g, 0, 1) per pixel. The mask is the weapon _MASK texture's sample whose B is the tint mask (t23 in tinted instances; t10, the first mask sample, in instances without the Cust path). G is the reflection / gloss mask: the original reflection is constant x cube x mask.g, so matte parts (grips, rubber) had none and stay dielectric.
  - f0 = the constant reflection colour: the cube term per unit cube light with every texture at 1, bounded as f / max(1, max channel) to keep the hue (as the CHR paint). A per-channel clamp whitened the blue steel (IonBlaster (3.9, 4.2, 6.0) -> now (0.65, 0.69, 1.0)).
  - 5 are truly white (1, 1, 1): Maul, SerratedBlade, ShortSword, FX_RollerMine and the WEP_PhongMain master. For the sheets: these, plus PROP_NEU_AmmoPickup and FX_IonBlaster Mag for readability.
  - The tinted paint stays dielectric per pixel (gate 1).
  - Weapons whose reflection colour is below 0.04 (5: the melee staves / daggers / bat) get no hook and stay dielectric.

**Rule 2 keeps the Original shading.** A material whose base colour still needs rule 2 (a multiplicative view factor) gets no pbr row (class never_remap, keep_original: true); Modern falls back to its original body. It occurs in 0 MP materials. In the UI dirs it is ENV_NEU_CybertronOrbit_P.ENV_NEU_CybertronLowShell_Mat, the front-end planet shell: diffuse x pow(N.V, 3) limb darkening, which a view mean (0.25 flat) would darken 4x and smear the rim glow.

**Scope.** The audit covers the 10 MP maps and the 5 UI_* render dirs (customisation, lobbies, front end). The MP maps come first in the unique-material pass, so existing rows keep their place; the UI dirs add 32 unique materials (11 dielectric, 2 metallic_paint, 1 keep-original, 18 excluded).

**Otherwise metallic** = 0 and **F0** = 0.04 until reviewed metal rows exist.
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
- Re-review (a): rule 3 for cube x colour products. The robot bodies keep their paint (base share 1.00 for all 67, was a median of 0.60). Rule-1 materials: 683; rule-2: 0 (the one rule-2 material resolves through rule 3).
  - WEP_PhongMain keeps a median 73 % of the original diffuse luminance (min 3 %), ENV_PhongMain_NS a median 94 %.
- 2c: the emissive is untouched, and the runtime-set vectors are added.
- 3: the runtime ternary uRT_X(.swizzle) form is parsed. All 908 remapped materials emulate (was 871 of 909).
- 4: the roughness parent is picked deterministically (mask / spec / gloss-named first, then by name). The targets are the materials' own derived means (median 0.38, p10 0.26, p90 0.54).
- Re-review (b): runtime-tint consumers are evaluated with representative tints, not the 0.1 grey placeholder: the 10 character-select palette swatches cycling as Cust A / B, plus the default energon (0.843, 0.302, 0.029). The low base share is now measured on the materials' own textures (128 points).
  - Low base after the strip (under 25 % of the original diffuse): 45, in SYSTEMS_M6_LOW_BASE.json.
  - 26 are non-tint: the metal-review candidates (metal_review_candidate: true). They include D1_FloorTile1 / 2, which are authored cube-only (base 0): keep the original shading or review them as metal.
  - 19 are tint consumers: 8 player weapons whose look is the anisotropic camera specular (rule 1, exact per Experimental), and 11 ENV materials with authored tints, mostly the reflection-dominated "White" set. They are review items, not metal candidates.
- Known limit: the view flag does not propagate through a sample's UV (a texture sampled at a view-dependent UV counts as view-independent; 1 material).

## Tint mask channels (for AssetTools' E pass: never generate inside these)

| master | texture | channel |
|---|---|---|
| ENV_AllShader_p.Release.ENV_PhongMain_NS_Mat | the TEXSET Masks (..._Masks_NormY) | **B, only in materials with Enable_CustomColorTint** (234 of 662): diffuse = mix(albedo x CustA, albedo x CustB, B), both colours designer constants that multiply the albedo (73 have CustA white, so B = 0 is untinted there; 82 have both non-white, so every texel is tinted). With the switch off (427 materials, e.g. Rust MPYFloor, WallPanelSign) B is the **detail-normal mask** (vec2(B) x the detail normal's xy), not a tint. Per mask texture: SYSTEMS_M6_TINT_AB.json. G is the gloss / spec mask, R the emissive mask, A the normal's Y |
| WEP_Rocket_p.WEP_Projectile_MAT | ..._Masks_NormY | **B** |
| WEP_Shared_p.WEP_PhongMain_MAT | the weapon's _MASK texture | **B** |
| TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat, TR_Defrag_MAT | the separate CustAB_AuxGlow(Map) texture | **R** (the whole texture is mask data: leave it untouched) |
| WEP_TurretIonGun_p.WEP_TurretIonGun_MAT | the colour texture (TurretIonGun_CLR) | **G** (an outlier: its colour texture's G drives the tint) |

## Audit output

Maps: MP_ESC_BrokenHope, MP_ESC_Remnant, MP_IAC_Berth, MP_IAC_Rust, MP_IAC_Seed, MP_IAC_Streets, MP_KON_Molten, MP_ORB_Debris, MP_UND_Complex, MP_UND_Gorge, UI_CampaignLobby, UI_CharacterCustomization, UI_FrontEnd, UI_Lobby, UI_PartyLobby. Unique materials: 1368.

| class | unique materials |
|---|---|
| dielectric | 547 |
| dielectric_tint | 244 |
| excluded | 445 |
| metal_candidate | 25 |
| metallic_paint | 69 |
| never_remap | 2 |
| weapon_metal_mask | 36 |

## Per family

| family | dielectric | dielectric_tint | excluded | metal_candidate | metallic_paint | never_remap | weapon_metal_mask |
|---|---|---|---|---|---|---|---|
| CINE | 3 | 0 | 0 | 0 | 0 | 0 | 0 |
| DES | 12 | 0 | 0 | 0 | 0 | 0 | 0 |
| DESIGNER | 3 | 0 | 0 | 0 | 0 | 0 | 0 |
| ENGINEMATERIALS | 1 | 0 | 1 | 0 | 0 | 0 | 0 |
| ENV | 389 | 219 | 184 | 16 | 0 | 1 | 0 |
| FX | 0 | 0 | 130 | 0 | 0 | 1 | 2 |
| PROP | 64 | 11 | 33 | 8 | 1 | 0 | 1 |
| TPRO | 14 | 6 | 2 | 0 | 0 | 0 | 0 |
| TR | 47 | 0 | 58 | 0 | 68 | 0 | 0 |
| UI | 0 | 0 | 26 | 0 | 0 | 0 | 0 |
| WEP | 14 | 8 | 11 | 1 | 0 | 0 | 33 |

## Emulation (glsl_emu; real textures for the targets)

- emulated: 921 of 921 remapped materials; not emulated: 0 ()
- base-colour strip: no view term 235, additive view terms dropped (rule 1) 686, multiplicative view factor averaged (rule 2) 0
- rule-1 base colour brighter than the original diffuse (must be 0): 0
- SpecularPower -> roughness -> SpecularPower: max relative error 5.51e-16
- low base after the strip (< 25 % of the original diffuse, real textures, palette tints for runtime-tint consumers): 46 (19 tint consumers, 27 non-tint = metal-review candidates: ENV_IAC_CleanMaterials_p.Materials.D1_FloorTile1_MATINST, ENV_IAC_CleanMaterials_p.Materials.D1_FloorTile2_MATINST, TPRO_SecurityCamera_p.PROP_SecurityCamera_MATINST, ENV_KON_Prison_p.Materials.PrisonCell_Plane_MATINST, ENV_ORB_WhiteMaterials_p.Materials.White_CanyonDeco_MATINST, CINE_D3B_LevelIntro_p.MegatronShip.PROP_MegatronShipBack_MATINST, TPRO_KON_Corruptor_p.Materials.KON_Corruptor_MATINST, ENV_ORB_WhiteMaterials2_p.Materials.White_LaserShield_A_MATINST, ENV_ORB_GalacticTransport_p.Materials.ENV_ORB_TransportInnerDEAD_MAT, DES_ORB_Asteroids_p.Textures.SpaceDebris_Large_MATINST, PROP_NEU_Battery_p.Battery1.PROP_NEU_Battery1_MATINST, ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_MATINST, ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST, ENV_CORE_SeedChamberBoss_p.Materials.ENV_COR_BatteryIacOrange_MAT, ENV_CORE_SeedChamberBoss_p.Materials.ENV_COR_CorruptedBattery_MAT, ENV_KON_Architecture_p.Materials.BasePlatform_Column_01_MATINST, ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_CeilingPanel_01_MATINST, ENV_ORB_WhiteMaterials_p.Materials.White_Street_MATINST, ENV_ORB_WhiteMaterials2_p.Materials.White_CanyonWall_A_MATINST, ENV_CORE_SeedChamberBoss_p.Materials.ENV_COR_Battery_MAT, PROP_NEU_Monitors_p.PROP_NEU_Monitor01_red_MATINST, PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST, PROP_NEU_Monitors_p.PROP_NEU_Monitor01noEmiss_MATINST, ENV_IAC_Turret_p.Texture.IAC_searchlight_RED_MATINST, ENV_ORB_WhiteMaterials_p.Materials.White_InroomCnrWall_B_MATINST, ENV_IAC_Turret_p.Texture.IAC_searchlight_purple_MATINST, ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_WallPanel_01_MATINST)
- palette tints loaded: 10 swatches
- customisation-tint consumers (forced dielectric): 409; HUD-marker materials (never remapped): 2
- metal candidates (PROVISIONAL, for review; not applied): 25

## AssetTools work list

- 498 unique parent textures need a roughness map (BC4, half the colour size), each with its target mean roughness; 18 have at least one (texture, material) ORM candidate

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

## Metallic-paint exit check (chr_paint_check.py: 67 materials x 10 swatches x 16 points, 2,010 customised pixels)

The gate is a median dE00 of at most 5 per (material, swatch). The table gives the pass share, the median of the medians, the median dH* and the median signed dL*. The L*-matched pass share is the same check with Modern at Original's L*, so colour without exposure.

| m | mode | pass | median dE00 | dH* | dL* | L*-matched pass (median) |
|---|---|---|---|---|---|---|
| 0.8 | diffuse | 20 % | 9.9 | 0.3 | -8.0 | 53 % (4.8) |
| 0.8 | IBL | 70 % | 2.8 | 0.4 | +2.5 | 86 % (1.5) |
| 0.8 | combined | 17 % | 9.7 | 0.7 | -3.7 | 57 % (3.9) |
| 0.7 | combined | 9 % | 9.9 | 0.6 | -0.6 | 60 % (4.1) |
| 0.6 | combined | 9 % | 10.5 | 0.6 | +2.6 | 62 % (3.9) |

Reading:
- The hue holds (dH* under 1 in every mode).
- The failure is lightness and chroma spread per pixel: the median dL* is small, but its sign varies across pixels. The real Original chain is a diffuse term 1.5 x paint x DL (the diffuse-luminance mask; t43 on Optimus Rust) plus the reflection cube(bias (1 - g) x 3) x P x 0.6, where P (the tinted paint) carries the gloss pattern. This table was measured with baseColor = P, which put the gloss into the diffuse share and lost DL. With baseColor = null (the stripped base; the re-review), Experimental measured the L* correlation with Original at 0.98 (was 0.83), exposure unchanged.
- m does not fix it. The combined pass share is 9 - 17 % at 0.6 - 0.8.
- Decided (Integration / Experimental): the M6 gate is the user's in-game full-frame A/B; this check is a diagnostic. Metallic stays a constant 0.8 and does not follow the gloss (the gloss is already in P twice and in SpecularPower). Fallback if the A/B shows the cube mean double-counted against the IBL: base = the DL term alone, bounded.

Like-for-like attempt (mode "lit", Integration 2026-10-13; diagnostic only). The M6 gate is the user's in-game full-frame A/B. Both sides are under one unit directional light (5 L / V pairs, N = +Z):
- Original: DiffuseColor x NdotL + SpecularColor x Phong(SpecularPower).
- Modern: Lambert + pi x GGX + the env split-sum IBL.

| m | lit pass | median dE00 | dH* | dL* | L*-matched pass (median) |
|---|---|---|---|---|---|
| 0.8 | 11 % | 34.8 | 0.3 | +45.5 | 42 % (6.5) |
| 0.7 | 9 % | 33.8 | 0.3 | +43.2 | 44 % (6.4) |
| 0.6 | 9 % | 33.0 | 0.4 | +41.4 | 43 % (6.0) |

Modern reads much brighter here, which this model can't separate from its own energy assumptions:
- The Original's cube term already sits inside its light-multiplied DiffuseColor.
- Modern adds a separate env IBL on top of the direct light.
- The unnormalised Phong and the pi-scaled GGX differ in peak energy.

So the hue holds again (dH* 0.3) and the exposure is not comparable offline. That supports the full-frame A/B as the gate.
