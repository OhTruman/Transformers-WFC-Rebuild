# Material fix (A): the death scorch / metal decals and the death gibs (Systems, 2026-10-17)

**Tools** (SYSTEMS_MATFIX_DECAL_GIBS.patch, against af4d0731's tools/render):
- vector_channel_proven.txt += FX_Decals_p.DeathDecal_MAT (its MICs MetalDecal_02 / 03 inherit): proof = the shipped decal
  PS (RE TARGETED_PASS5 add. 31) oC0.rgb = 1 + G*A*((R + Blacks)*Color - 1) - the lerp weight takes Color.A.
- systems_extra_materials.txt += TR_Death_Gibs_p.Materials.DeathSkelGib_Mat_INST, DeathGib_Mat_INST (compiled in no map).

**Data** (built with build_materials_only.py from a snapshot of the 10 MP maps' inputs, work/matA/src, 07:40; merged by
tools/systems/matfix_handoff.py with MATFIX_BASE = that snapshot): **Rebuild-Systems/work/matA/handoff/<map>/materials_glsl.json**
for the 10 MP maps + MATFIX_MANIFEST.json (copy in docs/handoff/matfix_decal_gibs/). Per map: the 3 decal entries replaced,
the 2 gib entries added, **every other entry identical** (602/602 ... 666/666), fresh-vs-current differences = exactly the 3
decals, no stale build paths; sha256 before (= the snapshot = integration at 07:40) and after per map. Integration: check the
live file still has sha256_before, then copy the 10 files over.

**Before / after (all 10 maps):**
| material | before (shipped) | after |
|---|---|---|
| DeathDecal_MAT | weight = G x vec4(20, 20, 20, 1) -> m = mix(1, (R - 1e-4) x 20, G x 20): max 25.7, 41.6 % < 0 (white / black blobs) | weight = G x Color.A = G x 1 -> m = 1 + G((R - 1e-4) x 20 - 1): max 2.24, 0.6 % < 0 (the shipped PS) |
| MetalDecal_02 / 03_MAT_INST | weight = G x vec4(5, 5, 5, 1) | weight = G x 1 (Color.A), as the PS; bright-metal highlights up to ~3.7 / 5 x |
| DeathSkelGib_Mat_INST / DeathGib_Mat_INST | (not compiled: flat colour draws) | compiled (RB_GibExtra_CLR / MASK / NORM; the textures ship as DDS in the package); glow = texture x tint (0.62, 0.16, 0.05) as today's translator does - the graph takes the alpha (5), so the gibs are in (B)'s list (110 materials) |

Gameplay: scorch default on + gib wfcName = "TR_Death_Gibs_p.Materials." + sourceName once the data is switched.
