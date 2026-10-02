# Render audit captures

Deterministic (WFC_LOCKSTEP). Marks per FIDELITY.md: CONFIRMED / HIGH / PROV / UNKNOWN.

## robot

(image: run `bash tools/render/capture_audit.sh` -> robot.png)

- env: `default spawn`, frames 90
- effect templates: -
- provisional / open: Robot light environment: 6 authored visibility samples (bounds sampling semantics HIGH); character shadows not implemented

```
frame 90 camera (371.73 -719.28 -339.97) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   47 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            23 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    4 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              27 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      58 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   78 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  22 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            61 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     59 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               8 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       18 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      23 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                6 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                10 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         21 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            55 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 76 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              6 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    16 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        139 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       27 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       210 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            15 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              10 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     85 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            7 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       44 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       3 opaque      lit lightmap static - -
  ENV_KON_Architecture_p.Materials.BridgeSupport_01                         1 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            4 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             4 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              24 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 23 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          52 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                 9 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         14 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           13 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         30 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST                                   1 masked      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.134 0.022 0.033) -Y (0.000 0.000 0.000); samples robot(6)
```

## vehicle

(image: run `bash tools/render/capture_audit.sh` -> vehicle.png)

- env: `WFC_STARTVEHICLE=1`, frames 90
- effect templates: FX_Navigation_p.CarHover_A_01_FX x6 (HoverBooster_* sockets)
- provisional / open: base_glow/rays_Dup hover emitters not spawned (Systems); character shadows not implemented

```
frame 90 camera (370.23 -717.43 -337.57) fog on post bloom=1 dof=1 clut=1 distortion_pass=1
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   48 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            23 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    4 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              29 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      59 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   80 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  23 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            61 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     59 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               9 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       18 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      23 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                5 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                11 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         27 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            54 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       15 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 74 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              7 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    16 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        142 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       27 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       210 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            15 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              10 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     87 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            7 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       44 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       3 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            4 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             4 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              24 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 23 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          53 opaque      lit lightmap static - -
  FX_Materials_p.MatInst.LightCylinder_Rays_MAT_INST                       90 additive    unlit - dynamic fx -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                10 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         15 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           13 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  Ring_Distort_Add_MAT                                                      1 additive    unlit - dynamic fx distortion
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         30 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST                             1 opaque      lit - dynamic - -
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST                           1 opaque      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.074 0.018 0.022) -Y (0.000 0.000 0.000); samples vehicle(5)
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.074 0.018 0.022) -Y (0.000 0.000 0.000); samples vehicle(5)
```

## hover_idle

(image: run `bash tools/render/capture_audit.sh` -> hover_idle.png)

- env: `WFC_STARTVEHICLE=1 WFC_RENDERCAM=367.755,-723.480,-348.575,2.5808,-0.0624`, frames 90
- effect templates: FX_Navigation_p.CarHover_A_01_FX
- provisional / open: base_glow (Glow_Mod_MAT) and rays_Dup (Trail_Distort_MAT) not spawned (Systems); distortion RT format UNORM8 (HIGH)

```
frame 90 camera (367.76 -723.48 -348.58) fog on post bloom=1 dof=1 clut=1 distortion_pass=1
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                    2 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                             4 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST               4 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       2 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST           1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   1 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                   2 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                             1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_noEmiss_MATINST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                          1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                        4 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                5 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                       4 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           2 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                 2 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          1 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                          9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                             6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                        3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                      3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                  8 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                          9 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                        3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                               3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                        30 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance               7 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     21 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            2 translucent unlit - static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                        6 additive    unlit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_KON_Architecture_p.Materials.BridgeSupport_01                         1 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   2 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   2 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              2 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST                3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Silo11_NoEmiss_MATINST                 1 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                    2 opaque      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             1 opaque      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                  1 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                           4 opaque      lit - static - -
  FX_Materials_p.MatInst.LightCylinder_Rays_MAT_INST                       90 additive    unlit - dynamic fx -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST          1 opaque      lit lightmap static - -
  PROP_IAC_HospitalProps_p.Table.PROP_HospitalTable_MATINST                 1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           1 opaque      lit lightmap static - -
  Ring_Distort_Add_MAT                                                      1 additive    unlit - dynamic fx distortion
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST                             1 opaque      lit - dynamic - -
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST                           1 opaque      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.074 0.018 0.022) -Y (0.000 0.000 0.000); samples vehicle(5)
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.074 0.018 0.022) -Y (0.000 0.000 0.000); samples vehicle(5)
```

## boost

(image: run `bash tools/render/capture_audit.sh` -> boost.png)

- env: `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1`, frames 120
- effect templates: FX_Navigation_p.bumble_boost_small1_FX (BoostSocket_L/R)
- provisional / open: particle colour encoding clamped by Systems (hdr())

```
frame 120 camera (360.15 -719.28 -343.87) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  Basic_Particle_Add_MAT                                                    1 additive    unlit - dynamic fx -
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   33 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            22 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    3 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              24 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      55 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   76 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  21 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            58 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                45 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       15 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      21 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                5 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                 7 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          1 masked      lit - static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            53 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                      9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 71 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              6 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    15 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        131 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       25 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       207 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            14 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance               4 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     79 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            5 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       42 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       2 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            2 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             3 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              22 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 19 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          48 opaque      lit lightmap static - -
  FX_Navigation_p.Boostermaterial_02_MAT                                    8 additive    unlit - dynamic fx -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                 6 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         14 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           12 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         28 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST                             1 opaque      lit - dynamic - -
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST                           1 opaque      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST: 2 direct [SpotLight_14023_LC(vis 0.60), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples vehicle(5)
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST: 2 direct [SpotLight_14023_LC(vis 0.60), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples vehicle(5)
```

## hover_dash

(image: run `bash tools/render/capture_audit.sh` -> hover_dash.png)

- env: `WFC_STARTVEHICLE=1 WFC_AUTODASH=60`, frames 68
- effect templates: CarHover_A_01_FX + jump/dash FX (Systems)
- provisional / open: -

```
frame 68 camera (370.23 -717.43 -337.57) fog on post bloom=1 dof=1 clut=1 distortion_pass=1
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   48 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            23 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    4 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              29 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      59 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   80 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  23 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            61 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     59 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               9 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       18 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      23 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                5 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                11 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         27 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            54 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       15 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 74 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              7 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    16 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        142 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       27 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       210 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            15 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              10 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     87 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            7 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       44 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       3 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            4 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             4 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              24 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 23 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          53 opaque      lit lightmap static - -
  FX_Materials_p.MatInst.LightCylinder_Rays_MAT_INST                       66 additive    unlit - dynamic fx -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                10 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         15 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           13 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  Ring_Distort_Add_MAT                                                      1 additive    unlit - dynamic fx distortion
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         30 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST                             1 opaque      lit - dynamic - -
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST                           1 opaque      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.074 0.018 0.022) -Y (0.000 0.000 0.000); samples vehicle(5)
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.074 0.018 0.022) -Y (0.000 0.000 0.000); samples vehicle(5)
```

## ram_nitro

(image: run `bash tools/render/capture_audit.sh` -> ram_nitro.png)

- env: `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 WFC_AUTODASH=60`, frames 100
- effect templates: FX_Navigation_p.FX.Truck_ram_FX (RamSocket)
- provisional / open: Ram_model_MAT rim term literal (Normal node semantics PROV); dust/rays_Dup not spawned (Systems)

```
frame 100 camera (360.15 -719.28 -343.87) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  Basic_Particle_Add_MAT                                                    1 additive    unlit - dynamic fx -
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   33 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            22 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    3 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              24 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      55 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   76 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  21 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            58 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                45 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       15 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      21 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                5 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                 7 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          1 masked      lit - static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            53 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                      9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 71 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              6 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    15 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        131 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       25 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       207 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            14 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance               4 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     79 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            5 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       42 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       2 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            2 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             3 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              22 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 19 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          48 opaque      lit lightmap static - -
  FX_Materials_p.Materials.Ram_model_MAT                                   13 additive    unlit - dynamic fx -
  FX_Navigation_p.Boostermaterial_02_MAT                                    8 additive    unlit - dynamic fx -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                 6 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         14 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           12 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         28 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST                             1 opaque      lit - dynamic - -
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST                           1 opaque      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST: 2 direct [SpotLight_14023_LC(vis 0.60), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples vehicle(5)
  TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST: 2 direct [SpotLight_14023_LC(vis 0.60), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples vehicle(5)
```

## transform_mid

(image: run `bash tools/render/capture_audit.sh` -> transform_mid.png)

- env: `WFC_AUTOTRANSFORM=30`, frames 80
- effect templates: -
- provisional / open: -

```
frame 80 camera (370.23 -717.43 -337.57) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   48 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            23 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    4 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              29 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      59 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   80 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  23 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            61 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     59 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               9 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       18 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      23 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                5 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                11 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         27 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            54 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       15 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 74 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              7 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    16 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        142 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       27 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       210 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            15 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              10 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     87 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            7 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       44 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       3 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            4 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             4 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              24 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 23 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          53 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                10 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         15 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           13 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         30 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.075 0.018 0.022) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.075 0.018 0.022) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.075 0.018 0.022) -Y (0.000 0.000 0.000); samples robot(6)
```

## streets_dark

(image: run `bash tools/render/capture_audit.sh` -> streets_dark.png)

- env: `WFC_SPAWN_INDEX=10`, frames 60
- effect templates: -
- provisional / open: 360 BSP elements without static lighting use the dynamic env (PROV)

```
frame 60 camera (96.07 -711.98 -559.60) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   56 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 3 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            19 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    3 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              36 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      45 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.TunnelPipeA_MAT                        1 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomCnrWall_A_MATINST                 2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   67 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          20 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST            9 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    6 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             7 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 1 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  36 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_noEmiss_MATINST                        10 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            40 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                23 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         61 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     59 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                              11 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_NOEMISS_MATINST                       3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       32 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST               13 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      18 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           4 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  1 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST               10 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                15 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_MATINST                             3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           4 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                            11 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         32 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            50 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           15 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       17 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                   12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    6 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.BlastBurn_MAT                                   1 translucent unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                111 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     2 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT             13 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    17 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        108 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                               11 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       25 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              27 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 9 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       168 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance             9 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              28 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     59 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance             4 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST           13 translucent unlit - static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   3 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       42 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            2 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                      13 opaque      lit lightmap static - -
  ENV_KON_Architecture_p.Materials.BridgeSupport_01                        12 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            6 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             6 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   2 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   3 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   2 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    40 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              6 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              22 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Silo11_NoEmiss_MATINST                 2 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   17 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             3 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_declogo_clr_Mat                            1 additive    unlit vertexLM static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                         6 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     3 opaque      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             1 opaque      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 38 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   15 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     7 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          40 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          3 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   1 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                10 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         16 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             1 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     27 modulate    unlit - static - -
  PROP_IAC_HospitalProps_p.Table.PROP_HospitalTable_MATINST                 3 opaque      lit lightmap static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          3 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           7 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                            5 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         15 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          1 opaque      lit lightmap static - -
  TPRO_ORB_D1_Doors_p.Materials.InteriorDoorLock_MATINST                    1 opaque      lit lightmap static - -
  TPRO_ORB_D1_Doors_p.Materials.InteriorDoor_Inside_MATINST                 1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST                                   1 masked      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
```

## streets_bright

(image: run `bash tools/render/capture_audit.sh` -> streets_bright.png)

- env: `WFC_SPAWN_INDEX=14`, frames 60
- effect templates: -
- provisional / open: -

```
frame 60 camera (229.45 -699.91 -299.91) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   17 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            21 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    3 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              28 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      42 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   30 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           15 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 2 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  14 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            52 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                45 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              5 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         64 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     13 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      20 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  11 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         20 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                3 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                11 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_MATINST                             1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          1 masked      lit - static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                             6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             7 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          16 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         16 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            39 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           17 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                        9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                      8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    4 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    4 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.BlastBurn_MAT                                   1 translucent unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 51 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    13 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                         77 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       22 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              13 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            7 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       175 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            12 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     80 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            11 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            5 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       30 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            2 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             3 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   2 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    38 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST                1 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              14 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Silo11_NoEmiss_MATINST                 2 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                    9 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                          7 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            2 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                         6 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 16 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                       9 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          42 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                 7 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST          9 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     23 modulate    unlit - static - -
  PROP_IAC_HospitalProps_p.Table.PROP_HospitalTable_MATINST                 2 opaque      lit lightmap static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           4 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                            9 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         29 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          4 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST                                   1 masked      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST: 1 direct [DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
```

## firing

(image: run `bash tools/render/capture_audit.sh` -> firing.png)

- env: `WFC_AUTOFIRE=1`, frames 90
- effect templates: Ion Blaster weapon FX (Systems, GL1 textured path: no material passed yet)
- provisional / open: weapon sprite FX not material-shaded until Systems passes ParticleBatch::material

```
frame 90 camera (371.73 -719.28 -339.97) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   47 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 1 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            23 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    4 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              27 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      58 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     3 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   78 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          23 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           17 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  22 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            61 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            41 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                46 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         79 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     59 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               8 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       18 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      23 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                6 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                10 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                            10 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          18 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         21 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               5 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            55 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     12 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    5 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    3 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 76 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              6 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    16 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        139 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       27 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              26 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       210 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            15 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              10 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     85 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            7 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       44 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       3 opaque      lit lightmap static - -
  ENV_KON_Architecture_p.Materials.BridgeSupport_01                         1 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            4 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             4 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              24 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   10 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            3 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                        10 opaque      lit lightmap static - -
  ENV_ORB_Beam_p.Material.GlowSphereBright_MAT_INST                         1 additive    unlit - static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     1 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 23 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     5 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  4 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          52 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          2 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                 9 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         14 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     29 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           5 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                           13 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         30 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          7 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST                        15 masked      lit - dynamic - -
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST                                   1 masked      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.138 0.023 0.034) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.061 0.017 0.019) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
  WEP_GrenadeLauncher_p.WEP_GrenadeLauncher_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples centre(1)
```

## fineaim

(image: run `bash tools/render/capture_audit.sh` -> fineaim.png)

- env: `WFC_FINEAIM_ON=20`, frames 90
- effect templates: -
- provisional / open: HUD scale mode / easeout curve PROV; no scope for the Ion Blaster (HUD script CONF)

```
frame 90 camera (371.73 -719.28 -339.97) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                    8 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            12 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    2 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              12 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        2 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      46 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     1 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   32 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          16 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           14 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightCylinder2_Rays_Red_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_MATINST                   2 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                   6 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            43 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            37 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                35 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         49 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                      7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                               4 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                        6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      11 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                   9 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         17 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           1 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST                4 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                             4 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          11 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             15 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            32 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           13 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                        3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                      6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    4 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 54 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              4 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    12 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                         82 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       16 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              22 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       152 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            14 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     63 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            10 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       25 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               4 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          3 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            1 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             2 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   1 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    38 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               11 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              18 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                          7 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                         6 opaque      lit lightmap static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST   22 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                  7 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   13 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                       7 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     2 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          39 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   1 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                 1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST          8 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             4 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     22 modulate    unlit - static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           2 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          9 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                            9 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         15 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST                                   1 masked      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_9898_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST: 2 direct [PointLight_9898_LC(vis 1.00), DirectionalLight_727_LC(vis 1.00)]; ambient cube +Y (0.134 0.022 0.033) -Y (0.000 0.000 0.000); samples robot(6)
```

## streets_floor

(image: run `bash tools/render/capture_audit.sh` -> streets_floor.png)

- env: `WFC_SPAWN_INDEX=2`, frames 60
- effect templates: -
- provisional / open: -

```
frame 60 camera (93.84 -709.04 -282.44) fog on post bloom=1 dof=1 clut=1 distortion_pass=0
materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):
  DES_IAC_Omega_Cover_p.Material.innerCyberParts_MATINST                   53 opaque      lit lightmap static - -
  ENV_AllShader_p.Energon_River.Energon_Lava_Mat                            2 translucent unlit lightmap static - -
  ENV_AllShader_p.Release.ENV_ForceField_MATINST                            1 additive    unlit - static - -
  ENV_ArchiveInterior_p.ArchiveWall_MATINST                                 3 opaque      lit lightmap static - -
  ENV_ArchiveInterior_p.ArchiveWall_Off_MATINST                            20 opaque      lit lightmap static - -
  ENV_CORE_Deco_p.Textures.ENV_CORE_CanyonDeco_A_MATINST                    3 opaque      lit lightmap static - -
  ENV_CORE_Platform_p.Textures.ENV_CORE_CanyonFloor_A_MATINST              35 opaque      lit lightmap static - -
  ENV_CORE_Shader_p.Material.ENV_ForceFieldNoDestortion_MATINST_INST        1 additive    unlit lightmap static - -
  ENV_CORE_SupportColumns_p.Materials.CoolantPipe2_MATINST                  2 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.GS_SupportAB_MAT                      52 opaque      lit lightmap static - -
  ENV_CORE_SupportColumns_p.Textures.TunnelPipeA_MAT                        1 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_BSPWall_C_MATINST                       4 opaque      lit - static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_A_MATINST                     4 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_CliffWall_B_MATINST                     2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomCnrWall_A_MATINST                 2 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_InroomWall_A_MATINST                   85 opaque      lit lightmap static - -
  ENV_CORE_Wall_p.Textures.ENV_CORE_RoutingWallA_MATINST                    3 opaque      lit vertexLM static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Blue_MATINST          24 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_Purple_MATINST         4 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.FogSheet_DepthBiased_RED_MATINST           14 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.GlowSpherePeach_MAT_INST                    6 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Cool_MATINST                     1 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Orange_Light_MATINST             3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRaysV2Warm_Lighter_MATINST             7 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightRays_RED_MATINST                       3 additive    unlit - static - -
  ENV_GLB_EnvEffects_p.Material.LightVolumePurplish_MATINST                 2 additive    unlit - static - -
  ENV_IAC_BLDS_1_p.Material.Arch_MAT_INST                                  30 opaque      lit vertexLM static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_MAT_INST                            66 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.CurvedWall_Off_MAT_INST                         1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_MAT_INST                                2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.RibWall_noEmiss_MATINST                         2 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_MAT_INST                                 3 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Saucer_Off_MAT_INST                            43 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.Spires_MAT_INST                                44 opaque      lit lightmap static - -
  ENV_IAC_BLDS_1_p.Material.VentWall_NoEmis_MAT_INST                        1 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_MATINST                              6 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.ArchTop01_off_MATINST                         78 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.HalfArchInter_Off_MATINST                     52 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.Pillar01_MATINST                              12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_MATINST                       27 opaque      lit lightmap static - -
  ENV_IAC_BLDS_2_p.Materials.TallBuildingONE_noEmiss_MATINST                7 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_MATINST                      20 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.Bld_4096x4096x2048_Off_MATINST                  12 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.HalfArch_Detail_MATINST                         22 opaque      lit lightmap static - -
  ENV_IAC_BLDS_3_p.Texture.RectBld_LoSpec_MATINST                           3 opaque      lit lightmap static - -
  ENV_IAC_Background_p.Materials.CityBackdrop_Warm_MATINST                  2 masked      lit - static - -
  ENV_IAC_Background_p.Materials.Iacon_Nebula_D_MAT                         1 opaque      lit - static - -
  ENV_IAC_Decagon_p.DestroyedStairs.BurnHole_MAT                            1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.DecoPlazaFloor_noEmiss_MATINST               10 opaque      lit lightmap static - -
  ENV_IAC_Decagon_p.Materials.IAC_Decagon_GlassPan2_MATINST                16 additive    unlit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_MATINST                             3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_NOEMISS_MATINST                     1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ArchSupport_Var1_MATINST                        1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.ChainLinkFence_MATINST                          2 masked      lit vertexLM static - -
  ENV_IAC_Deco_1_p.Material.DecoBalcony1_MAT_INST                           6 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_MAT_INST                             8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.DecoDonut1_Off_MAT_INST                         3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.GiantPillar_MATINST                             9 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_MAT_INST                          20 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightFixture_NoLight_MAT_INST                   1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.LightPole_02_MAT_INST                           2 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_MATINST                             19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupp01_Off_MATINST                         29 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_BackLightBlue_MATINST               3 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SideSupport_MATINST                            57 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_MATINST                           19 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SphereHalf01_Off_MATINST                       14 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.Spire_Off_MATINST                               1 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.SubwayFloor_Straight_MATINST                    8 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_NOEMISS_MATINST                     11 opaque      lit lightmap static - -
  ENV_IAC_Deco_1_p.Material.TrainTrack_StreetsMP_MATINST                    6 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Blackout_MAT                                    6 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.BlastBurn_MAT                                   1 translucent unlit lightmap static - -
  ENV_IAC_Ground_p.Material.Crater_MATINST                                 89 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.ENV_EngergonGlass_MAT_INST                      1 opaque      unlit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_MATINST_BAT                     2 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.EVN_HeatedMetal_Purple_MATINST_BAT              8 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.FloorRamp_EMISSOFF_MAT_INST                    19 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.GroundVent_MATINST                              1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.HospitalFloor_MAT_INST                        118 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Panel_02_MATINST                                3 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_MATINST                                1 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Platform_noEmiss_MATINST                       24 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Sidewalk_MAT_INST                              25 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.StreetLines_MAT_INST                            8 masked      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MAT_INST                                 4 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.Street_MoreSpec_MATINST                       188 opaque      lit lightmap static - -
  ENV_IAC_Ground_p.Material.TireTracks_MAT_INST                             4 translucent unlit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_RoundWall_MATInstance            13 opaque      lit - static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_MATInstance              10 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_Spacers_Off_MATInstance           1 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interior_WallsCeilings_noEmiss_MATINST     2 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_MATInstance                     64 opaque      lit lightmap static - -
  ENV_IAC_Interior_1_p.Materials.Interiors_NoLights_MATInstance            12 opaque      lit lightmap static - -
  ENV_IAC_LightSources_p.LightPlanes.BckSillouhetteSmoke_MATINST            9 translucent unlit - static - -
  ENV_IAC_SentinelArena_p.Materials.CoreRing2_MAT_INST                     24 opaque      lit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_dense_Dark_MATINST                   4 additive    unlit vertexLM static - -
  ENV_IAC_Shader_p.Material.IAC_Glass2_light_MATINST                       42 additive    unlit lightmap static - -
  ENV_IAC_Shader_p.Material.IAC_Glass_MATINST                               6 additive    unlit lightmap static - -
  ENV_IAC_Terminal_p.Materials.fully_transparent                            1 masked      unlit lightmap static - -
  ENV_IAC_Train_p.Materials.ENV_TrainCoach_MATINST                          4 opaque      lit lightmap static - -
  ENV_IAC_Train_p.Materials.TrainEngine_MAT_INST                            1 opaque      lit lightmap static - -
  ENV_IAC_TubeRoads_p.Materials.TubeRoad_Blue_MATINST                       5 opaque      lit lightmap static - -
  ENV_KON_Architecture_p.Materials.BridgeSupport_01                         4 opaque      lit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_Emissive                            3 opaque      unlit lightmap static - -
  ENV_KON_Deco_p.Materials.LightPole_01_MATINST                             4 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Aqua_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Blue_MATINST                   1 opaque      lit lightmap static - -
  ENV_KON_PrisonBarge_p.Materials.DomeDeco_01Warm_MATINST                   3 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Piece2_3584x2048x1792_MATINST    45 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Big3.Big3_Building3Trim_NoEmiss_MATINST              4 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Mid1.Mid1_Base1_768x2048x2048_MATINST               12 opaque      lit lightmap static - -
  ENV_NEU_Structures_p.Small1.Small1_Box2_384x768x768_MATINST              28 opaque      lit lightmap static - -
  ENV_NEU_Variation_p.ENV_cboard_CLR_Mat                                   19 opaque      lit lightmap static - -
  ENV_NEU_WreckedVehicles_p.Material.Destroyed_AutobotTank_MATINST          1 opaque      lit lightmap static - -
  ENV_NEU_landmarking_p.landmark_Dchevrons_clr_Mat                         10 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_chevrons_clr_Mat                           4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_corner_clr_Mat                             5 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_dcorner_clr_Mat                            4 additive    unlit - static - -
  ENV_NEU_landmarking_p.landmark_declogo_clr_Mat                            1 additive    unlit vertexLM static - -
  ENV_ORB_A1_LowRez_p.Material.ENV_ORB_Wing_MATINST                         8 opaque      lit lightmap static - -
  ENV_ORB_D1_Architecture_p.Materials.FloorTrim_01_MATINST                  4 opaque      lit lightmap static - -
  ENV_ORB_D1_Debris_p.Materials.innerCyberParts_MATINST                     3 opaque      lit lightmap static - -
  ENV_ORB_D1_Deco_p.Materials.GasChamberSubstructure_MATINST                2 opaque      lit lightmap static - -
  ENV_ORB_D1_MegatronCruiser_p.Materials.Ship_FloorPanel_01_NOEMISS_MATINST    1 masked      lit lightmap static - -
  ENV_ORB_PowerCorridor_p.Materials.PowerTubeEmissPulse_MATINST             3 opaque      lit vertexLM static - -
  ENV_ORB_PowerCorridor_p.Materials.TrypPowerTubes_MATINST                 35 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Metal_Floor_Tile_MATINST                   17 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.TrypWallLight_MATINST                      12 opaque      lit lightmap static - -
  ENV_ORB_Trypticon_p.Materials.Tryp_MetalFloor_MATINST                     7 opaque      lit lightmap static - -
  ENV_UND_Support_p.Materials.FramePillars_CLR_MatINST_BAT                  5 opaque      lit lightmap static - -
  EngineMaterials.DefaultMaterial                                          54 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.AnimSign3_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign4_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign5_Decepticon_CLR_Mat                          3 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimSign6_CLR_Mat                                     1 additive    unlit lightmap static - -
  PROP_EnergonSigns_p.AnimTes_staticLong_CLR_Mat                            1 opaque      unlit lightmap static - -
  PROP_EnergonSigns_p.AutobotLOGO_CLR_Mat                                   2 additive    unlit - static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Aqua_MATINST            1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_MATINST                10 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Off_MATINST             1 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Purple_MATINST         15 opaque      lit lightmap static - -
  PROP_EnergonSigns_p.MetalSigns.PROP_NEU_CitySigns_Red_MATINST             8 opaque      lit lightmap static - -
  PROP_FloorStains_p.stains_01_CLR_LowSpec_MatINST_BAT                     30 modulate    unlit - static - -
  PROP_IAC_Statues_p.Texture.ChestOutStatue_MATINST                         1 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Blue_MATINST                          5 opaque      lit lightmap static - -
  PROP_NEU_Battery_p.PROP_NEU_Battery_Off_MATINST                           6 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActivePurple_MATINST                     1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenActive_MAT_INST                          4 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.MonitorScreenDead_MAT_INST                            1 opaque      lit lightmap static - -
  PROP_NEU_Monitors_p.PROP_NEU_Monitor01_MATINST                            9 opaque      lit lightmap static - -
  PROP_NEU_WreckedBodies_p.DeadBodies_Mat_INST                              1 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateGateA_NE_MATINST         28 opaque      lit lightmap static - -
  TPRO_COR_Lockgate_p.Materials.TPRO_CORE_LockGateStand_NE_MATINST          4 opaque      lit lightmap static - -
  TPRO_NEU_Cannons_p.TPRO_NEU_AAGun_MATINST                                 1 opaque      lit lightmap static - -
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST                            1 opaque      lit - dynamic - -
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B                         2 opaque      lit - dynamic - -
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST                                   1 masked      lit - dynamic - -
dynamic light environments (UberLight, TotalLightCount 2):
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_2650_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_2650_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_2650_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
  WEP_IonBlaster_p.WEP_IonBlaster_MATINST: 2 direct [DirectionalLight_727_LC(vis 1.00), PointLight_2650_LC(vis 1.00)]; ambient cube +Y (0.057 0.017 0.018) -Y (0.000 0.000 0.000); samples robot(6)
```

