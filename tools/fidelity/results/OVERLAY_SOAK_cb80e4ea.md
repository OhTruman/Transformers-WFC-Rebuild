# Shipping-overlay soak - cb80e4ea + overlay_20261023d (2026-10-23 02:45)

Headless, 10 maps x TDM x 300 s x seeds 7 / 1234, 4 v 5 Smart. WFC_OVERLAY_ROOT = AssetTools/work/overlay_20261023d (read only; frozen 02:41, bc66720: adapted collision / physics on all 10 maps, rebuilt nav on BrokenHope / Remnant / Seed). Detector: Gameplay's refreshed WFC_FLOATLOG (97676327, string fixed locally) + my scratch fall-through rule (not merged): a pawn whose pawn-collision floor at its exact x / z is 1.0-1.5 m ABOVE its feet (walkable faces |n.y| >= 0.7) with |v| < 0.05 m/s on every axis for 30 steps. Baseline: the same exe and seeds with no overlay (original collision). Logs work/stabsoak/overlay_d/, overlay_cb80_base/ (and overlay_cb80/ = the earlier 20261023c run with trimmed nav: 26 hits, 8 on Seed).

## Stability

20 / 20 runs exit 0, end at 300 s, 0 ERROR lines, no crash / assert. The overlay is live: 3 overlay lines per map (physics.json x2 + collision_pawn.glb), + bot_nav.json on BrokenHope / Remnant / Seed only. (B) FLOAT stall: 0 in all 20.

## Fall-through candidates: 18 hits with the overlay vs 6 with the original collision

| Map | Spot (x y z) | Floor above feet | Hits (seed: pawn chassis form, first t) | Also with the original collision? |
|---|---|---|---|---|
| MP_ESC_BrokenHope | (101.2 45.5 -78.6) | 46.79 (1.34 m) | 1: s1234: p3 Car2 vehicle t 212.64 | **no - overlay only** |
| MP_ESC_BrokenHope | (76.1 1.3 -87.9) | 2.62 (1.34 m) | 1: s1234: p3 Car2 vehicle t 292.12 | **no - overlay only** |
| MP_IAC_Streets | (115.9 -707.5 -421.4) | -706.16 (1.31 m) | 1: s1234: p2 Truck3 robot t 242.50 | **no - overlay only** |
| MP_KON_Molten | (112.1 -6.4 -109.3) | -5.23 (1.17 m) | 2: s1234: p7 Tank2 robot t 236.90; s7: p3 Car2 robot t 42.37 | **no - overlay only** |
| MP_ORB_Debris | (-134.9 121.3 31.4) | 122.57 (1.29 m) | 3: s1234: p1 Jet4 robot t 147.06; s1234: p1 Jet4 robot t 151.77; s1234: p8 Jet robot t 171.12 | **no - overlay only** |
| MP_UND_Complex | (42.9 -8.8 6.0) | -7.82 (1.01 m) | 3: s7: p6 Car4 robot t 159.99; s7: p6 Car4 robot t 161.70; s7: p4 Truck3 robot t 308.55 | **no - overlay only** |
| MP_UND_Complex | (10.3 9.0 -10.4) | 10.45 (1.43 m) | 1: s7: p9 Truck4 robot t 195.95 | **no - overlay only** |
| MP_UND_Gorge | (-68.0 -4.6 -53.6) | -3.45 (1.11 m) | 3: s1234: p8 Jet robot t 121.33; s1234: p8 Jet robot t 124.58; s7: p9 Truck4 robot t 49.70 | **no - overlay only** |
| MP_UND_Gorge | (-77.4 12.0 -99.4) | 13.08 (1.09 m) | 1: s1234: p2 Truck3 robot t 97.74 | **no - overlay only** |
| MP_UND_Gorge | (-83.2 -3.4 -57.6) | -2.24 (1.11 m) | 1: s7: p1 Jet4 robot t 121.88 | **no - overlay only** |
| MP_UND_Gorge | (-76.1 -4.1 -55.2) | -2.95 (1.16 m) | 1: s7: p3 Car2 robot t 135.18 | yes |

Original collision only (not with the overlay):
- MP_IAC_Streets (157.7 -726.4 -456.2) floor -725.19 (1.21 m), 1 hit(s)
- MP_UND_Complex (144.9 -19.4 12.5) floor -18.29 (1.07 m), 1 hit(s)
- MP_IAC_Berth (-24.5 -3.5 -12.0) floor -2.18 (1.34 m), 1 hit(s)
- MP_IAC_Seed (180.8 -1.3 -132.2) floor 0.09 (1.35 m), 1 hit(s)
- MP_KON_Molten (111.7 1.3 -133.6) floor 2.52 (1.24 m), 1 hit(s)

## Read

- Every hit is onGround with a walkable face 1.0-1.45 m above the feet at the same x / z, i.e. a pawn-collision floor passing through the pawn's body. The rule is new and unvalidated: the original collision gives 6 such hits too, so part of this is a baseline (thin treads / lips counted walkable, or vehicle-form pawns under a low overhang - both BrokenHope hits are Car2 in vehicle form). **Robot-form hits at the overlay-only spots are the candidates**; the strongest (several pawns, both seeds or repeated): Debris (-134.9 121.3 31.4) x3, Complex (42.9 -8.8 6.0) x3, Gorge (-68.0 -4.6 -53.6) x3, Molten (112.1 -6.4 -109.3) x2 (both seeds).
- Seed: 8 hits with the 'c' overlay (old nav) -> 0 with 'd' (rebuilt nav), so the bots stop routing there; the collision under them is unchanged (collision_pawn.glb identical c / d).
- The known Berth seed 1234 hole (-57.7, -18.0, -7.6) did not trip this rule in these runs.

## Candidate: AssetTools out/pc_adapt/_candidates/overlay_e_revert_broken_floors (2026-10-23 03:43-03:45, not frozen)

Overlay e (115 files) with the #18 per-poly broken-floor placements on Debris (SMCA_10904 CanyonFloor_B_Destroy + SMCA_15156 DebrisFloor_01, 12 placements) and Gorge (SMCA_8751 CanyonFloor_B) back to their authored hulls. Same exe, seeds and rule; 20 / 20 exit 0 at 300 s, 0 ERROR, 0 FLOAT stall.

| Map | Spot (x y z) | Floor above feet | Hits (seed: pawn chassis form, first t) | Also with the original collision? |
|---|---|---|---|---|
| MP_ESC_BrokenHope | (101.2 45.5 -78.6) | 46.79 (1.34 m) | 1: s1234: p3 Car2 vehicle t 212.64 | **no - overlay only** |
| MP_ESC_BrokenHope | (76.1 1.3 -87.9) | 2.62 (1.34 m) | 1: s1234: p3 Car2 vehicle t 292.12 | **no - overlay only** |
| MP_IAC_Seed | (223.5 9.0 -106.7) | 10.43 (1.47 m) | 1: s7: p8 Tank2 robot t 143.79 | **no - overlay only** |
| MP_IAC_Seed | (-59.2 -3.2 -50.4) | -1.77 (1.48 m) | 1: s7: p3 Car2 robot t 222.32 | **no - overlay only** |
| MP_IAC_Streets | (115.9 -707.5 -421.4) | -706.16 (1.31 m) | 1: s1234: p2 Truck3 robot t 242.50 | **no - overlay only** |
| MP_KON_Molten | (112.1 -6.4 -109.3) | -5.23 (1.17 m) | 2: s1234: p7 Tank2 robot t 236.90; s7: p3 Car2 robot t 42.37 | **no - overlay only** |
| MP_ORB_Debris | (31.2 115.2 44.6) | 116.21 (1.02 m) | 1: s7: p7 Jet robot t 67.78 | **no - overlay only** |
| MP_UND_Complex | (42.9 -8.8 6.0) | -7.82 (1.01 m) | 3: s7: p6 Car4 robot t 159.99; s7: p6 Car4 robot t 161.70; s7: p4 Truck3 robot t 308.55 | **no - overlay only** |
| MP_UND_Complex | (10.3 9.0 -10.4) | 10.45 (1.43 m) | 1: s7: p9 Truck4 robot t 195.95 | **no - overlay only** |

Original collision only (not with the overlay):
- MP_IAC_Streets (157.7 -726.4 -456.2) floor -725.19 (1.21 m), 1 hit(s)
- MP_UND_Complex (144.9 -19.4 12.5) floor -18.29 (1.07 m), 1 hit(s)
- MP_IAC_Berth (-24.5 -3.5 -12.0) floor -2.18 (1.34 m), 1 hit(s)
- MP_IAC_Seed (180.8 -1.3 -132.2) floor 0.09 (1.35 m), 1 hit(s)
- MP_KON_Molten (111.7 1.3 -133.6) floor 2.52 (1.24 m), 1 hit(s)
- MP_UND_Gorge (-78.7 -3.3 -57.6) floor -2.23 (1.04 m), 1 hit(s)

Read: **the three real crack spots are gone** - Debris (-134.9 121.3 31.4) and Gorge (-68.0 -4.6 -53.6) / (-83.2 -3.4 -57.6); Gorge 7 hits -> 0; total 18 (overlay d) -> 12 (baseline, original collision: 6). What remains is mostly AssetTools' 'behavioural, same geometry in live and shipping' list (Complex (42.9, 6.0) x3 and (10.3, -10.4), Molten (112.1, -109.3) x2, Streets (115.9, -421.4), the two BrokenHope vehicle-form hits). New single hits: Seed (223.5 9.0 -106.7), Seed (-59.2 -3.2 -50.4), Debris (31.2 115.2 44.6) - any collision change re-rolls the whole match (bots go elsewhere), so single new spots are not proof of a regression; worth a probe.

## Frozen overlay_20261023f (2026-10-23 04:07-04:09)

Byte-identical to the candidate (diff: only CANDIDATE.json / FROZEN.json). Same exe, seeds and rule: 20 / 20 exit 0 at 300 s, 0 ERROR, 0 FLOAT stall, the same 12 fall-through hits at the same spots as the candidate run (deterministic). **PASS** for the shipping overlay; the 3 crack spots stay gone. Logs work/stabsoak/overlay_f/.
