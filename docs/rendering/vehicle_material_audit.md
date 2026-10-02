# Material audit: vehicle.glb

Marks: **CONFIRMED** = read from cooked data or the compiled material resource; **HIGH** = standard UE3 semantics applied to confirmed data; **PROV** = provisional; **UNKNOWN**.

## Section 0: `TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST`

- Slot binding: section 0 (9728 tris) -> slot 0 -> `TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST` (cooked slot table) **CONFIRMED**
- Master: `TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat`; chain: ['TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST'] **CONFIRMED**
- Lighting model MLM_Phong, blend BLEND_Opaque, two-sided False **CONFIRMED**
- Static switches (value, MIC override) decoded from the MIC native tail and validated against StaticSwitchParameter names **CONFIRMED**:
    - `Enable_Autobot_Lut` = False (master default)
    - `Enable_Decepticon_Lut` = False (master default)
    - `UseAutobotEnergonColor` = True (MIC override)
    - `UseDiffuseReflection` = True (master default)
    - `UseEmissiveReflection` = True (master default)
    - `UseNeutralEnergonColor` = False (master default)
    - `UseReconstructedNormal` = True (MIC override)
    - `UseSpecPower_S_Curve` = True (MIC override)
    - `Use_Neutral_Lut` = False (master default)
    - `enableOvershield` = False (master default)
    - `enableScanLines` = False (master default)
    - `enableUPLight` = False (master default)
- Compiled permutation parameter set vs translation: **CONFIRMED** ({})
    - compiled scalars 20, vectors 4, textures 5
- Parameter values (source) **CONFIRMED**:
    - scalar `ReflectionMipLevelScale` = 1.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `Reflection_Intensity` = 0.20000000298023224  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `Fill_Reflection_Intensity` = 0.029999999329447746  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `Emissive_Intensity` = 3.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `ColorBrightness` = 0.6000000238418579  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `PowerDown` = 0.5  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `Specular_Intensity` = 0.6499999761581421  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `SpecularColor_Desaturation` = 0.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `SpecularPower_Intensity` = 35.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `SpecSpower_S_Constrast` = 15.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `SpecSpower_P_Constrast` = 0.6000000238418579  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `SpecSpowerAdd` = 0.15000000596046448  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `EnergonPanUVTile` = 10.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `Energon_Intensity` = 1.0  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - scalar `energonIntensity` = 0.5600000023841858  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - vector `Cust_Color_A` = [0.59894, 0.01607, 0.01607, 1.0]  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - vector `Cust_COLOR_B` = [0.0, 0.11658, 0.52952, 1.0]  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - texture `Reflection_Map` = TR_AllShader_p.CubeMaps.CHR_Metals_CUBEMAP3D  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - texture `Grunge_Map` = TR_Optimus_VEH_p.VH_Optimus_GrungeClr  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - texture `CustClrAlphasMap` = TR_Optimus_VEH_p.VH_Optimus_CustAB_AuxGlow  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - texture `Lum_Map` = TR_Optimus_VEH_p.VH_Optimus_SpecPwr_DifLum_EnrGlow  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - texture `VarienceNormal_XY_Map` = TR_Optimus_VEH_p.VH_Optimus_NORM  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - texture `Normal_Map` = TR_Optimus_VEH_p.VH_Optimus_NORM  (override in `tr_optimus_veh_p.rb_optimusprime_cust2_mat_inst`)
    - master expression defaults (no MIC override): `CameraSpec_Intensity_Val`, `Color_Saturation`, `Emissive_Saturation`, `EnergonColor`, `energonContrast`, `energonSpeed`
- Texture set (format / colour space / unpack / address) **CONFIRMED**:
    - slot 0 `TR_Optimus_VEH_p.VH_Optimus_NORM` PF_DXT5 [1024, 1024], sRGB=False, unpack_min=[-1.0, -1.0, -1.0, -1.0], TA_Wrap/TA_Wrap, TEXTUREGROUP_Character [channels read: ag] -> unpacked x*2-1 (TextureSet / normal-map UnpackMin -1)
    - slot 1 `TR_AllShader_p.CubeMaps.CHR_Metals_CUBEMAP3D` PF_DXT1 [None, None], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, None [channels read: xyz]
    - slot 2 `TR_Optimus_VEH_p.VH_Optimus_SpecPwr_DifLum_EnrGlow` PF_DXT1 [2048, 2048], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, TEXTUREGROUP_Character [channels read: bgr]
    - slot 3 `TR_Optimus_VEH_p.VH_Optimus_GrungeClr` PF_DXT1 [512, 512], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, TEXTUREGROUP_Character [channels read: rgba]
    - slot 4 `TR_Optimus_VEH_p.VH_Optimus_CustAB_AuxGlow` PF_DXT1 [1024, 1024], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, TEXTUREGROUP_Character [channels read: bgr]
    - slot 5 `TR_AllShader_p.Textures.fractalMaps` PF_DXT1 [512, 512], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, None [channels read: r]
    - normal: UseReconstructedNormal=True -> Z = sqrt(1 - x^2 - y^2) from the XY map **CONFIRMED graph**
- TnCharacterApplier parameters (runtime override, all-zero = skip; Gameplay currently passes none, so the MIC values below render) **CONFIRMED**:
    - `Cust_Color_A` default vec4(0.59894198179245, 0.016068000346422195, 0.016068000346422195, 1.0)
    - `Cust_COLOR_B` default vec4(0.0, 0.11657600104808807, 0.5295230150222778, 1.0)
    - `EnergonColor` default vec4(1.25, 0.05000000074505806, 0.05000000074505806, 1.0)
- Reflection/camera vectors: camvec, normal, reflvec, tbn, time; cube reflection via TextureSampleParameterCube with LODBias input where authored **CONFIRMED graph**
- Colour space: sRGB textures decoded by the sampler (Xenon gamma textures; PWL vs exact sRGB curve not reproduced **PROV**); shading in linear HDR; display gamma 2.2 + CLUT in post **CONFIRMED**

## Section 1: `TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST`

- Slot binding: section 1 (1923 tris) -> slot 1 -> `TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST` (cooked slot table) **CONFIRMED**
- Master: `TR_AllShader_p.Release.InteriorAlt_Energon_MAT`; chain: ['TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST'] **CONFIRMED**
- Lighting model MLM_Phong, blend BLEND_Opaque, two-sided False **CONFIRMED**
- Static switches (value, MIC override) decoded from the MIC native tail and validated against StaticSwitchParameter names **CONFIRMED**:
    - `UseAutobotEnergonColor` = True (master default)
- Compiled permutation parameter set vs translation: **CONFIRMED** ({})
    - compiled scalars 3, vectors 1, textures 0
- Parameter values (source) **CONFIRMED**:
    - master expression defaults (no MIC override): `PowerDown`, `UTile_Scale`, `VTile_Scale`
- Texture set (format / colour space / unpack / address) **CONFIRMED**:
    - slot 0 `TR_AllShader_p.Textures.Interior_Norm` PF_DXT1 [1024, 1024], sRGB=False, unpack_min=[-1.0, -1.0, -1.0, 0.0], TA_Wrap/TA_Wrap, TEXTUREGROUP_WorldNormalMap [channels read: xyz] -> unpacked x*2-1 (TextureSet / normal-map UnpackMin -1)
    - slot 1 `TR_AllShader_p.Textures.Interior_Color` PF_DXT1 [1024, 1024], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, None [channels read: rgbaxyz]
    - slot 2 `TR_AllShader_p.Textures.Interior_Emiss` PF_DXT1 [1024, 1024], sRGB=True, unpack_min=None, TA_Wrap/TA_Wrap, None [channels read: bgr]
- Reflection/camera vectors: time; cube reflection via TextureSampleParameterCube with LODBias input where authored **CONFIRMED graph**
- Colour space: sRGB textures decoded by the sampler (Xenon gamma textures; PWL vs exact sRGB curve not reproduced **PROV**); shading in linear HDR; display gamma 2.2 + CLUT in post **CONFIRMED**

