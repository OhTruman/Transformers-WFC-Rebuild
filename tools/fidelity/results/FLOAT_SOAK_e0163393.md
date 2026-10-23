# #18 (B) float soak - e0163393 + Gameplay's scratch WFC_FLOATLOG (2026-10-23 00:57)

Headless (WFC_NULLRENDER, lockstep), TDM 4 v 5 Smart bots (BotsFriendly=4, BotsEnemy=5), WFC_MATCH_SECONDS=300, all 10 maps, seeds 7 and 1234 (20 runs, ~6-8 s wall each). Detector: docs/handoff/playtest1023/scratch_floatlog.js (agents/gameplay) on a scratch copy of the head, not merged. Runner: work/floatsoak/run.sh; logs work/floatsoak/e016339/s<seed>-<map>/run.log.

## (B) stall cases (robot, airborne, not hovering / transforming, |vy| < 0.2 for 30 steps): 6 in 20 runs

| Seed | Map | Pawn | Chassis | First t (s) | Position (x y z) | Velocity | groundY | y - groundY |
|---|---|---|---|---|---|---|---|---|
| 1234 | MP_IAC_Berth | p5 | Car4 | 133.46 | -67.28 -5.22 65.65 | 9.63 0.00 -7.16 | 6.68 | -11.9 |
| 1234 | MP_ORB_Debris | p5 | Car4 | 59.58 | -45.09 124.19 -111.97 | 3.54 0.00 -11.47 | 131.00 | -6.8 |
| 7 | MP_IAC_Berth | p3 | Car2 | 203.54 | -123.79 0.51 -84.68 | 4.36 0.00 11.18 | 6.75 | -6.2 |
| 7 | MP_IAC_Streets | p3 | Car2 | 230.15 | 177.67 -726.24 -452.10 | 11.92 0.00 -1.40 | -716.80 | -9.4 |
| 7 | MP_ORB_Debris | p6 | Car4 | 46.52 | -17.45 130.60 -92.86 | -11.01 0.00 -4.78 | 131.00 | -0.4 |
| 7 | MP_ORB_Debris | p6 | Car4 | 234.89 | 10.21 125.05 -121.02 | -0.89 0.00 -11.97 | 132.99 | -7.9 |

Per map (seed 7 / seed 1234): Berth 1 / 1, Debris 2 / 1, Streets 1 / 0; the other 7 maps 0 / 0 (Remnant, BrokenHope, Rust, Seed, Molten, Complex, Gorge).

- Seed 7 reproduces Gameplay's own four hits exactly (Streets t 230.15 p3 Car2, Berth t 203.54 p3 Car2, Debris t 46.52 / 234.89 p6 Car4): deterministic, replayable.
- Every case is a Car (Scout) chassis, the Dodge class: consistent with Gameplay's trace (bot Dodge = 0.5 s PHYS_Flying at DodgeSpeed, no gravity, off a ledge; the original caps AI dodges by a nav-mesh WalkLine).
- In every case y is BELOW the detector's groundY (by 0.4 to 11.9 m; 5 of 6 by more than 6 m): the pawn has gone off a ledge / under the walkable surface the ground probe finds, i.e. it reads as 'under geometry' in play. (The 'under:' actor list is AABB-only - Gameplay: not proof of penetration.)

## (A) above-visual (grounded > 0.6 m above collision.glb for 60 steps)

18 to 203 lines per run, almost all vehicle-form (collision.glb lacks surfaces the vehicle collision has); robot-form lines 0 to 8 per run (Debris 8 / 8 the most). Gameplay: offsets of 5 m+ are mostly catwalks missing from collision.glb. Not counted as (B).

After Gameplay's dodge cap: rerun with the same runner + seeds; the 6 cases above are the 'before'.

## Update 2026-10-23 ~01:4x: refreshed detector + the dodge cap (before / after)

Gameplay refreshed the detector (97676327): a stall now also needs the pawn > 0.5 m above the pawn collision under it - the old
rule flagged every ordinary 0.5 s flat dodge along the floor, so the 6 rows above were mostly normal dodges (all sat at / below
the probe). (The committed script had a raw line feed inside a JS string; my local copy was repaired, Gameplay told.)

Same runner, 10 maps x seeds 7 / 1234, 300 s, 4 v 5 Smart, headless:

| Arm | Build | Real (B) stall cases | Case |
|---|---|---|---|
| before | 4bcfde3d + refreshed detector | **1** | seed 7 Berth p3 Car2 t 203.54 at (-123.79 0.51 -84.68) vel (4.36 0 11.18) |
| after | 0d8e3c06 (merged nav-limited AI dodge cap) + refreshed detector | **0** in 20 runs | - |

Above-visual (A) lines 2,770 -> 2,763 (unchanged: mostly vehicles over surfaces missing from collision.glb). **#18 (B) fixed on
0d8e3c06** as far as 20 seeded matches can show. Logs: work/floatsoak/fl2_base/, fl2_after/.
