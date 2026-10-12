# The ~20 leftover character / melee materials: no-lightmap base-pass check (report only, 2026-10-12)

Integration's request: check the ~20 per-map leftovers of the lightmap sweep with a NoLightMap base-pass test (from Rendering's
`work/re/chr_emis.py`, copied and adapted: `chr_d.py`, diff in `chr_d_vs_chr_emis.diff`). Rust's list (`chr_keys.txt`) is the same
set as on every map. Run one process at a time, light (<0.5 GB), untimed; outputs only in Experimental work/.

**Method.** UE3's FNoLightMapPolicy base pass outputs `Emissive + NoLightMapColor * Diffuse` (x fog). The original PS is
emulated twice, with NoLightMapColor = 0 and = 1. That gives the original's Emissive E and DiffuseColor D, which are compared with
ours under one fitted fog factor (relative residuals). Shared-map pairing is extended: with `CHR_BRUTE`, every named
NoLightMap shader map under the same root master is tried, and the best fit is kept.

## Why the lightmap sweep flagged them

- **Melee weapons** (LongSword, ProngDagger, SerratedBlade, SpikedBat): paired with PROP_NEU_AmmoPickup's map, the no-lightmap
  base pass is **exact (0.0)**. The lightmap-test mismatch was a pairing artefact.
- **The six PC-adaptation bodies** (bot soldiers, Frenzy / Rumble, Laserbeak; Rendering's `character_materials.cooked_copies`):
  they are cooked into **no MP map**, so no MP ShaderCache holds their shaders, and their MICs only load from campaign packages.
  Against MP caches the test fell back to the master's default permutation (meaningless). Against the campaign packages that
  export them (A2_KON_BASE_m, A1_IAC_Base_m, A2_KON_Soundwave_Design_m, A1_IAC_Elevator_Design_m, A1_IAC_DecoFoyer_Design_m,
  D1_ORB_Crash2B_DGN_m, D1_ORB_Base_m), 11 of 14 are **exact** (residual 6e-06 to 3e-05, fog 1.000):
  Laserbeak, Frenzy, FrenzyWeapon, Rumble, RumbleWeapon, RB/VH_SoldierCarAut, RB_SoldierMultiAut, RB_SoldierCarDec,
  RB_SoldierMultiDec, VH_SniperDec.

## Ranked suspects (no exact sibling map found; residual = best over all sibling maps tried)

| rank | material | best residual | diffuse | emissive | fitted fog | best sibling map | cache |
|---|---|---|---|---|---|---|---|
| 1 | RB_SoldierMultiNeu_MatINST | 0.289 | 0.281 | 0.289 | 1.28 | VH_AirRaid_NormSpec_Cust_E | campaign |
| 2 | VH_SoldierCarNeu_NormSpec_Cust_E_Mat_INST | 0.158 | 0.058 | 0.177 | 1.18 | VH_AirRaid_NormSpec_Cust_E | campaign |
| 3 | RB_SoldierCarNeu_Robo_MatINST | 0.112 | 0.033 | 0.129 | 1.03 | VH_AirRaid_NormSpec_Cust_E | campaign |
| 4 | VH_Slipstream_NormSpec_Cust_E_Mat_INST | 0.054 | 0.007 | 0.054 | 0.99 | RB_Skywarp_Mat_INST | Rust (MP) |
| 5 | RB_Slipstream_NormSpec_Cust_E_Mat_INST | 0.043 | 0.006 | 0.043 | 0.99 | RB_Skywarp_Mat_INST | Rust (MP) |

- All five errors are mostly in **emissive**: diffuse is near-exact for 2-5, and only SoldierMultiNeu (1) is off in diffuse too.
- "No exact sibling" is not yet proof of a translator bug. The material's own permutation may not be cooked in the caches tried. The
  Neu (neutral) soldiers' own campaign copy is D1_ORB_Crash2B_DGN_m, which was included. Slipstream is a real MP character
  (cooked into every MP map), but no map in Rust's cache carries its name, and the closest sibling (Skywarp) differs only in emissive.
- Suggested next step (Rendering or me on request): compare the emissive expressions of the Neu soldiers / Slipstream with their
  closest siblings' switch sets (team colour / Cust_E emissive switches are the likely difference).

## Follow-up (14:50): the 5 suspects are explained by one static switch each, so they are not translator evidence

Static switch values of each suspect against its best sibling map's material (`mic_static_params`; component masks equal in all three):

| suspect | best sibling | only switch that differs (suspect, sibling) |
|---|---|---|
| RB/VH_Slipstream_NormSpec_Cust_E | RB_Skywarp_Mat_INST | UseReconstructedNormal (True, False) |
| RB_SoldierMultiNeu_MatINST | VH_AirRaid_NormSpec_Cust_E | UseNeutralEnergonColor (True, False) |
| RB_SoldierCarNeu_Robo_MatINST (and VH_SoldierCarNeu) | VH_AirRaid_NormSpec_Cust_E | UseNeutralEnergonColor (True, False) |

The residuals measure that switch's effect against a different permutation (NeutralEnergonColor feeds the emissive, which matches
the emissive-heavy residuals). Their own permutation is not in the caches tried, so they are **unverifiable, not suspects**. No translator
issue is indicated among the ~20 leftovers.
