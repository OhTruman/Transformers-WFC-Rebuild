# Bot Driving steer sign (every Driving bot steered away from its heading error)

Gameplay, 2026-10-16 09:51. The patch is `docs/handoff/GAMEPLAY_DRIVE_STEER.patch`, against 56a9beca + `GAMEPLAY_BOTTEST_GATE.patch`. Both stack in order on a clean 56a9beca with files byte-identical to the tested tree.

**Files:**
- `WorldBots.cpp`, `World.h`: the steer law, `World::botDriveSteer`;
- `VehicleTests.h` / `.cpp`: `game::driveSteerHalfTime`;
- `Application.cpp`: two BOTTEST DRIVE rows.

It changes both AIs, bots only (players steer themselves). Classic is re-baselined below.

## The bug

`botSteer` sent `in.steer = clamp(wrapPi(faceYaw − yaw) × 1.5)`. In the Driving simulation, + steer is a right turn: the wheel angle + = right, and `CharacterMovement` integrates `yaw −= angVel.z × dt` ("UE +z turn right = rebuild yaw decreasing").
- **So the sign was backwards:** a target to the right (faceYaw < yaw) got a negative steer, a left turn.
- **What that did:** every boosting (Driving) bot steered away from its heading error. The error grew until the boost-stop angle dropped the vehicle back to Hovering.
- **Why it hid until now:** before the vehicle yaw fix, the hover facing snapped back to the heading in one step after that drop. With the authored turn rate the recovery takes time at speed, so the vehicle left its corridor.

**Found by the Debris hover trace.** The new top hover hotspot (39 s, 25 episodes over 11 seeds; 1 episode pre-yaw) was Truck4 boosting away from its spawn:
- the bearing was −27° to −34°, and the yaw went −34 → −17 → −14 → −6 under steer −0.26 / −0.39;
- it left the mesh at 24.7 m/s, fell about 10 m and pinned at (−213, 125, 4).

**The fix** sends `clamp(wrapPi(yaw − faceYaw) × 1.5)`.

## Guard (Integration request)

Two permanent BOTTEST rows ("DRIVE a Driving bot halves a ±30 deg heading error within 1 s") drive a boosting vehicle with `World::botDriveSteer` through the vehicle sim (`driveSteerHalfTime`).
- **Fixed sign:** both rows pass at 0.45 s.
- **Old sign, built to check the guard:** both rows FAIL (the error is never halved within 2 s), and BOTTEST reads 46 / 48.

So a sign inversion anywhere between the steer law and the Driving integrator fails BOTTEST.

## Checks

**NAVQ, 24 seeds × 4 maps, against the drive-probe base.** Every map is better or equal for both AIs (z in parentheses):

| Map | Smart slow % | Smart off-mesh % | Classic slow % | Classic off-mesh % |
|---|---|---|---|---|
| Streets | 5.10 → 4.34 (−1.3) | 5.06 → **3.27 (−2.9)** | 6.54 → **5.18 (−2.1)** | 5.38 → **3.44 (−3.0)** |
| Molten | 2.40 → 1.85 (−1.2) | 2.23 → 1.74 (−1.2) | 2.18 → 1.67 (−1.9) | 1.56 → 1.38 (−1.0) |
| Debris | 4.51 → 3.91 (−1.3) | 2.91 → **1.89 (−2.3)** | 3.68 → 3.74 (0.1) | 2.43 → 1.95 (−0.8) |
| Gorge | 4.73 → 4.25 (−0.7) | 5.11 → 3.86 (−1.6) | 6.08 → 4.95 (−1.4) | 7.07 → 4.71 (−2.0) |

**The Debris Smart cost of the yaw fix is closed.** Against pre-yaw f7b7828d, Debris is within noise (slow z 1.1, off-mesh z −0.3), and Streets and Gorge are better than pre-yaw (off-mesh z −2.3 / −3.2).

**Debris form split** (% of all travel time, pre-yaw → this patch):

| Form | Pre-yaw | This patch |
|---|---|---|
| Driving slow | 0.04 | 0.03 |
| Hover slow | 1.02 | 1.03 |
| Hover off-mesh | 0.70 | **0.47** |
| Jet slow | 1.72 | 1.84 |
| Robot slow | 0.69 | 1.01 |

- Driving time went from 75 to 87 s per run, because boosts no longer end in a veer-off.
- The jet difference is within noise. Robot slow is the remainder (outlier seeds): that is audit items 4-6.

**BOTTEST** (Systems' env, seeds 1-12, stacked tree): 48 / 48 on 10 seeds, including the gate seed 7.
- Seeds 1 and 5 fail only "no bot idle / stuck for 20 s": phase-1 bots idle 20.8 / 22.7 s. Pre-yaw shows the same (20.6 / 21.2 s).
- That is eye-pass item 4, queued and not caused by this patch.

**Identity:** the stacked tree reproduces the measured steer runs exactly: NAVQ 16 / 16 lines and Classic SIMHASH 8 / 8 logs.

## Classic byte-exact reference: re-baselined on this commit

`WFC_SIMHASH=0`, Classic, 24 seeds × 4 maps. The old column is the drive-probe reference (`docs/handoff/driveprobe`); the new column is this patch.

- **Every run diverges:** 96 of 96. The first divergent step is between 631 and 1,505 (median 663), which is when bots first boost.
- **Files:** `docs/handoff/drivesteer/CLASSIC_SIMHASH_old_vs_new.txt` (per-run table) and `docs/handoff/drivesteer/CLASSIC_NAVQ_REFERENCE_new.txt` (the 96 new SUMMARY lines).
- **From this commit on,** Classic lines must reproduce these exactly.
