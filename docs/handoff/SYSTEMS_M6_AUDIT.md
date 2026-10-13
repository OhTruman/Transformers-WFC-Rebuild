# M6 Modern materials: classification audit + the "pbr" mapping (Systems, 2026-10-12)

Tools: tools/systems/m6/m6_audit.py (audit + mapping + test vectors) and tools/systems/m6/glsl_emu.py (a numeric emulator of matc's material GLSL). They read the render data's materials_glsl.json for all 10 maps, read-only.

Outputs: the full table (m6_mapping.json, about 6 MB) and the oracle vectors (m6_vectors.json) are regenerated into work/m6. SYSTEMS_M6_WORKLIST.json is AssetTools' work list.

## Findings that shape matc's "pbr" mode (for Rendering)

1. **Reflection inside the diffuse.**
   - The REFLECTION_ON masters add the cube-map reflection into DiffuseColor (759 materials; median 7.6 % of the diffuse luminance).
   - The Modern base colour must be the graph evaluated with the cube samples at 0, with the reflection term routed out, because image-based lighting supplies specular in Modern. The split is exact: base + reflection == DiffuseColor within 4.4e-16 in emulation.
   - matc must emit DiffuseColor twice, or once with the cube sample replaced by 0. The reflection term is kept only for an "Original" comparison.
2. **Roughness per pixel.** SpecularPower is a per-pixel expression of the mask textures, not a constant.
   - Modern computes roughness = clamp(sqrt(2/(SpecularPower+2)), 0.02, 1) from the material's own SpecularPower output, per pixel. The round trip back to SpecularPower is exact to 5.1e-16 relative.
   - hdPbrTexture(parent, "roughness") replaces it where AssetTools generated a map. The parent is the texture feeding SpecularPower (else SpecularColor, else DiffuseColor): column roughness_parent.
3. **Tints.** Customisation-tint consumers (Cust_Color / Faction / Energon / uRT_Cust_* / the tint switches; 403 materials) are forced dielectric (Experimental's gate 1). The runtime-tint ternaries emulate as their default branch.
4. **Metal.** Nothing is metal by heuristic. The metal candidates below are a review list only; a reviewed per-family row is needed before any material gets metallic = 1.
5. **Test vectors.** m6_vectors.json holds, per material, 3 sample tables and the expected baseColor / roughness / metallic / F0 / emissive / reflection.
   - matc's "pbr" GLSL must reproduce them through glsl_emu.run (the same emulator), within 1e-6.
   - That is the acceptance check for the mapping: numeric and offline, before any render.
6. **Not emulated**: 38 materials use other ternary forms. They are listed in m6_mapping.json (emulation.why) and keep the derived mapping without the numeric check until the emulator covers them.

## Audit output

Maps: MP_ESC_BrokenHope, MP_ESC_Remnant, MP_IAC_Berth, MP_IAC_Rust, MP_IAC_Seed, MP_IAC_Streets, MP_KON_Molten, MP_ORB_Debris, MP_UND_Complex, MP_UND_Gorge. Material instances: 5555 rows, 1336 unique.

| class | unique materials |
|---|---|
| dielectric | 547 |
| dielectric_tint | 342 |
| excluded | 427 |
| metal_candidate | 20 |

## Per family

| family | dielectric | dielectric_tint | excluded | metal_candidate |
|---|---|---|---|---|
| DES | 11 | 0 | 0 | 0 |
| DESIGNER | 3 | 0 | 0 | 0 |
| ENGINEMATERIALS | 1 | 0 | 1 | 0 |
| ENV | 384 | 219 | 175 | 14 |
| FX | 2 | 1 | 130 | 0 |
| PROP | 71 | 11 | 33 | 3 |
| TPRO | 14 | 6 | 2 | 0 |
| TR | 45 | 66 | 58 | 2 |
| UI | 0 | 0 | 17 | 0 |
| WEP | 16 | 39 | 11 | 1 |

## Emulation (glsl_emu)

- emulated: 871 of 909 non-excluded materials; not emulated: 38 (ternary / index 38)
- identity: SpecularPower -> roughness -> SpecularPower max relative error 5.10e-16; base + reflection == DiffuseColor max abs 4.44e-16
- the cube reflection is added into DiffuseColor in 759 materials (stripped from the Modern base colour; median share of the diffuse luminance 7.6 %)
- customisation-tint consumers (forced dielectric): 403
- metal candidates (PROVISIONAL, for review; not applied): 20

## AssetTools work list

- 490 unique parent textures need a roughness map (BC4); 16 of them feed a metal candidate (ORM BC7 candidates)

Metal candidates (review list):

- ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT (ENV): strong, coloured specular (sat 0.99, spec/diffuse lum 1.09)
- ENV_IAC_Deco_1_p.MaterialOverride.Rust_Decals_OR.DecoBalcony1_MAT_INST_Rust_Decals_OR (ENV): strong, coloured specular (sat 0.30, spec/diffuse lum 1.85)
- ENV_IAC_Ground_p.Material.Decal_Crater_MAT (ENV): strong, coloured specular (sat 0.42, spec/diffuse lum 0.91)
- ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT (ENV): name
- ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT (ENV): name
- ENV_NEU_Variation_p.ENV_F_patch_CLR_Mat (ENV): strong, coloured specular (sat 0.67, spec/diffuse lum 0.94)
- ENV_NEU_Variation_p.ENV_MetalPlates_1_Mat (ENV): strong, coloured specular (sat 0.77, spec/diffuse lum 0.97)
- ENV_NEU_Variation_p.ENV_Metalbase_1_Mat (ENV): strong, coloured specular (sat 0.76, spec/diffuse lum 0.98)
- ENV_NEU_Variation_p.ENV_groundblobs_CLR_Mat (ENV): strong, coloured specular (sat 0.54, spec/diffuse lum 2.72)
- ENV_NEU_Variation_p.ENV_powerplug_CLR_Mat (ENV): strong, coloured specular (sat 0.33, spec/diffuse lum 1.00)
- ENV_ORB_D1_Architecture_p.Materials.StationFloor_A_MATINST (ENV): strong, coloured specular (sat 0.32, spec/diffuse lum 1.92)
- ENV_ORB_D1_Debris_p.Materials.MoltenMetalRing_SLAG_DULL_MATINST (ENV): name
- ENV_ORB_DecoTrim_p.Structural_Trim.MetalTest (ENV): name
- ENV_ORB_DecoTrim_p.Structural_Trim.Piston_Housing_Chrome_MATINST (ENV): name
- PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST (PROP): name
- PROP_IAC_HospitalProps_p.ChemicalTank.PROP_ChemicalTankLiquid_MATINST (PROP): strong, coloured specular (sat 0.75, spec/diffuse lum 469375.17)
- PROP_IAC_HospitalProps_p.ChemicalTank.PROP_ChemicalTankLiquid_red_MATINST (PROP): strong, coloured specular (sat 0.75, spec/diffuse lum 469375.17)
- TR_Ironhide_ROBO_p.RB_IronhideInteriorAlt_Energon_MAT_INST (TR): name
- TR_Ironhide_VEH_p.InteriorAlt_Energon_MAT_INST (TR): name
- WEP_Rocket_p.WEP_Rocket_Temp_MAT (WEP): strong, coloured specular (sat 1.00, spec/diffuse lum 1.00)
