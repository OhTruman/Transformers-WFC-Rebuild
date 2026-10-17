# Materials with BlendMode BLEND_Modulate AND a connected Distortion input (Systems, 2026-10-17)

**Why:** Rendering ef34a4f2 draws no distortion program for BLEND_Modulate materials (RE-confirmed on BlastSphere_Death_MAT). This lists
the rule's whole reach. **Source:** AssetTools authored.db (read only).
- Materials: class Material, BlendMode == BLEND_Modulate, Distortion.Expression set. That is **7 of the 29 Modulate materials**.
- MICs: through the Parent chain.
- Particle templates: refs to the material or its MICs, resolved to the owning ParticleSystem.
- Campaign maps: refs to those templates.
- MP reach: the compiled per-map materials_glsl.json, read from Systems' render snapshot work/matA/src (the MP maps reference FX through weapons and characters, not map packages).
- Full listing: docs/handoff/modulate_distortion_scan.txt.

| material (FX_Materials_p) | MICs | MP maps (compiled) | templates using it (examples) | campaign maps |
|---|---|---|---|---|
| Materials.BlastSphere_Death_MAT | - | all 10 | Exp_Death_FX / _Lrg / _XLrg, TF_Death_MED / LRG / Energy_Med (death explosions), Drain_Blast_FX, PowerUp_PlasmaCannon_FX, Trail_PlasmaCannon_Med / Lrg_FX, SoundWave_AOE_FX, Rumble_GroundPound_FX, Tank_AoE_Lrg / Robo_FX, ShieldGen_PowerUp_FX, TRYP_CORRUPTER_EXPL / SPAWN, Corruptor AE_Blast_FX, Shield_Slam_(Holo)Brute_FX | every campaign base map (A1-A5, D1-D5) + cines / vignettes |
| Materials.ScreenSplatter_01_MAT | MatInst.EnergonSplatter_03_MAT_INST | all 10 | Screen_EnergonBloodsplatter_FX | every campaign base map |
| Materials.Trail_Distort_MOD_MAT | Materials.Trail_Distort_MOD_01_MAT_INST | all 10 | WarCry_Buff_01 / 02 / 03_FX, Teleport_Char_FX, DarkEnergon_Beam_FX, Ax_DoorOpener_Blast / Buildup_FX, AudioBeam_Tracer_FX | every campaign base map |
| Materials.BlastShell_MAT | - | MP_ESC_BrokenHope, MP_ORB_Debris | hover_jump_Lrg_A / Lrg / Sm_FX, Omega_RightHand_fireball_FX, Trypticon Squib_Mouth_Mortar / Trail_Head_Mortar / Trail_Missiles / Trail_Mouth_Mortar_FX | A3 OmegaGate cine, A4, A5_TRY, D2 Gorge vig, D3B intro, D5_OME |
| Materials.Distortion_Cloud_MOD_MAT | - | none | (no template ref resolved; referenced by the campaign base map packages' cooked FX) | A1-A5, D1-D5 base maps |
| Materials.Ring_Distort_MOD_MAT | - | none | (as above) | A1-A5, D1-D5 base maps |
| Materials.Trail_DrainRay_MOD_MAT | - | none | (as above; NOT used by DrainRay_Beam_FX, which uses HealBeam_MAT / Ring_Distort*_Add / Blaster_Trail / Embers / Fire_Ring) | A1-A5, D1-D5 base maps |

**Reach in MP:**
- All 10 maps: 3 materials (BlastSphere_Death, the energon screen splatter, the Trail_Distort instance).
- 2 maps: BlastShell.
- The visible MP users are the death explosions, Drain_Blast_FX (Systems' ability trigger FX), the WarCry buff, Teleport_Char, the Plasma Cannon power-up / trails and the screen energon splatter.

**Campaign:** all 7 materials.

**Caveat:** a template counts as a user when a ref resolves to its ParticleSystem. The three materials with no resolved template are referenced only from the cooked campaign map packages, so their template owners aren't named here.
