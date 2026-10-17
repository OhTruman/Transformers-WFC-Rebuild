# Material eye pass: before (integration render data) vs after (candidate)

before: `work/eyepass/matB_before`  after: `work/eyepass/matB_after`

Reference: no Xbox 360 captures exist (Systems 2026-10-17). The reference is the shipped shader's intent (Systems' SYSTEMS_MATC_VECTOR_CHANNEL_SCAN.md section (B) step 1): each listed material's colour is tint x the vector parameter's alpha intensity where the old translation gave tint x tint - e.g. light shafts (rgb, 2).w twice as bright instead of about half, force fields (blue, 3).w 3x, energon plasma (rgb, 150).w an emissive glow that blooms. 'Closer to the original' = the effect reads as authored (a visible shaft / field / glow), neither washed out nor invisible.

| map | mode | same match (SIMHASH) | frames | frames changed | pixels changed % (mean over changed frames) | luminance shift in changed px | expected-change materials on the map | read |
|---|---|---|---|---|---|---|---|---|
| Seed (501) | off | yes | 26 | 20 | 3.46 | +19.9 | 48: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, BrightSmokeDecagon2_MATINST, ChasmDepths_Light_MATINST ... | |
| Seed (501) | faithful | yes | 26 | 20 | 3.46 | +19.9 | 48: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, BrightSmokeDecagon2_MATINST, ChasmDepths_Light_MATINST ... | |
| Seed (501) | real | yes | 26 | 18 | 3.08 | +18.7 | 48: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, BrightSmokeDecagon2_MATINST, ChasmDepths_Light_MATINST ... | |
| Berth (502) | off | yes | 26 | 13 | 2.86 | +19.6 | 40: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Berth (502) | faithful | yes | 26 | 13 | 2.86 | +19.6 | 40: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Berth (502) | real | yes | 26 | 10 | 3.42 | +20.7 | 40: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Complex (503) | off | yes | 26 | 20 | 1.12 | +22.8 | 38: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Complex (503) | faithful | yes | 26 | 20 | 1.12 | +22.8 | 38: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Complex (503) | real | yes | 26 | 15 | 1.11 | +21.9 | 38: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Rust (504) | off | yes | 26 | 24 | 3.50 | +23.3 | 43: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT ... | |
| Rust (504) | faithful | yes | 26 | 24 | 3.50 | +23.3 | 43: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT ... | |
| Rust (504) | real | yes | 26 | 22 | 3.24 | +24.4 | 43: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT ... | |
| Debris (507) | off | yes | 26 | 18 | 1.01 | +25.2 | 39: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Debris (507) | faithful | yes | 26 | 18 | 1.01 | +25.2 | 39: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Debris (507) | real | yes | 26 | 18 | 1.03 | +24.0 | 39: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Streets (508) | off | yes | 26 | 20 | 1.25 | +25.8 | 45: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT ... | |
| Streets (508) | faithful | yes | 26 | 20 | 1.25 | +25.8 | 45: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT ... | |
| Streets (508) | real | yes | 26 | 20 | 1.08 | +27.6 | 45: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, BckSillouhetteSmoke_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT ... | |
| Molten (509) | off | yes | 26 | 16 | 1.50 | +20.6 | 43: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Molten (509) | faithful | yes | 26 | 16 | 1.50 | +20.6 | 43: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Molten (509) | real | yes | 26 | 12 | 0.75 | +16.2 | 43: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Gorge (510) | off | yes | 26 | 23 | 0.84 | +20.1 | 42: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Gorge (510) | faithful | yes | 26 | 23 | 0.84 | +20.1 | 42: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |
| Gorge (510) | real | yes | 26 | 23 | 0.94 | +22.6 | 42: AP_VendingMachineAmmo_Glass_MATINST, BarricadeShield_MAT, BatteryOmegaGlass_MATINST, DarkEnergonCrystal_MAT_INST, DeathDecal_MAT, Drip_MAT_INST ... | |

Exe (both arms): /f/Transformers Rebuild/Rebuild-Experimental/work/ab/fast_5fc2e55/build-release/bin/wfc_rebuild.exe (integration 5fc2e552). Roots: before /f/Transformers Rebuild/Rebuild-Experimental/work/matb/matB_base_root, after /f/Transformers Rebuild/Rebuild-Experimental/work/matb/matB_root (one snapshot of integration's render data at 2026-10-17 10:17; the after arm = the same + the (B) entries of Systems' MATFIX_MANIFEST.json).

Note: the after root's Energon_Lava_Mat entry (Streets, BrokenHope) was missing at snapshot time (matb_root BOM bug) and was
patched in at 10:57, before the after arm reached Streets - the after arm is the full (B). Energon_Lava_Mat itself is not visible
in any versus frame (736 m below Streets' playable area; BrokenHope Escalation-only): see PACKAGE7-GATE.md's eye list.

## Experimental's read (2026-10-17, from the sheets work/matb/matB/sheet_<map>.png, views/sheet_<map>.png and the review pairs
## tools/fidelity/results/matb_review/*.png)

All 24 map x mode rows play the same match (SIMHASH identical); every change is a brightening (+16 to +28 levels in the changed
pixels), Off and Faithful identical, Realistic close to them - the (B) change is mode-independent as Systems said.

| map | read | what changes |
|---|---|---|
| Seed 501 | CLOSER | the ceiling lamps' light shafts are now visible beams onto the floor / robots (before: nearly invisible) |
| Berth 502 | **CHECK** | a light volume / haze now lifts the whole mid-ground (lilac-grey veil over the structures, contrast visibly lower: zoom_502.png). Could be the authored atmosphere, but it reads close to "washed out" - Systems to name the material (LightVolumePurplish?) and confirm its alpha |
| Complex 503 | CLOSER (small) | small changes (1.1 % of pixels): glow accents; no veil |
| Rust 504 | CLOSER | smoke plumes / volumes brighter and more visible; scene contrast kept |
| Debris 507 | CLOSER in match frames; **CHECK** in the 'reflective' view | match frames: a light shaft top right now visible. The authored 'reflective' view: the pool surface turns milky grey-white (46 % of the view changed): the dark reflective liquid / glass becomes opaque - likely a glass / energon-glass material at x alpha that should stay dark (rev_views.png) |
| Streets 508 | CLOSER | a haze / light volume through the far arch; force-field and glass accents brighter |
| Molten 509 | CLOSER | a light column at the left now visible; Realistic changes less (0.75 %) |
| Gorge 510 | CLOSER in match frames; **CHECK** the glass floor | small light-volume glow; in the 'firefight' view the glass floor panels lift to a milky grey (rev_views.png) - same pattern as Debris' pool |

Summary: light shafts, light volumes and smoke now read as authored (visible, not blown out) on Seed / Rust / Debris / Streets /
Molten / Gorge. Two findings for Systems before the switch: (1) glass / liquid surfaces going milky (Debris' pool, Gorge's floor
panels) and (2) Berth's mid-ground veil - both "washed out" by Systems' own criterion unless the original also shows them.
