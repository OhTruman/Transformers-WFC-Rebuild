# Driving hull probes above the wheel clearance (cars stopped dead on floor lips)

Gameplay, 2026-10-16 08:57. The patch is `docs/handoff/GAMEPLAY_DRIVE_PROBE.patch`, against integration 54ac361d; it applies unchanged on 997c0e89. It changes `CharacterMovement.cpp` (one line) and `VehicleTests.cpp` (new car rows).

This is a reconstruction correction that affects players and both bot AIs. Classic is re-baselined below.

## The bug

In Driving the vehicle root rests on the floor, and `vehicleStep` blocks the hull with three horizontal probes at hull-frame heights: the 0.45 m clearance probe, the mid probe 0.5 × (hullBottom + hullTop), and the top probe hullTop − 0.1.
- **The problem:** the mid probe of every car sits below the 0.45 m clearance, because the car hulls are low (table below).
- **What it caught:** a sub-0.45 m floor lip that the wheels should roll over.
- **At a V-shaped lip** the wall slide off one face wedged on the other, and the car stopped dead.
- **Not frontal:** forward · normal was 0.81, under the 0.866 frontal threshold, so there was no drop to Hovering. The car stayed pinned in Driving until the boost ended.

**Found while tracing the Debris Smart cost** after the vehicle yaw fix:
- **The split** (slow travel by form, Debris, 24 seeds): Driving slow went from 0.04 % to 0.46 % of travel time. Slow while turning more than 30° was 0.01 % in every arm, so it was not the corners.
- **The spot:** Car4 (Barricade) at (−63.4, 130.3, −61.2), an on-mesh straight.
  - **Lip faces:** n (−0.71, 0, 0.71) and (0.77, 0, 0.63), caught only by the 0.30 m mid probe.
  - **Speed:** 21.3 m/s → 0 in one step; then 1.3 → 0 every step for 1.5-4.5 s.
- **Why it got worse with the yaw fix:** the slower facing changed when bots boost along that stretch, so 7 of 8 seeds hit it, against 1 of 8 before. A player boosting over it stopped the same way.

## Original heights (authored.db, VEH_SHARED_p)

The original has no probes: the PhysX convex hull (`VH_*_PHYSSYS`) rides on raycast `TnWheelAssembly` wheels.

| Blueprint | Wheel LocalPosition Z | WheelRadius | Suspension RestingLength | Contact below the hull origin |
|---|---|---|---|---|
| Car_Physics | −13 UU | 30 | 20 (Car_Suspension) | 63 UU |
| Truck_Physics | +21 UU | 45 | 30 (class default; Truck_Suspension sets only K / D) | 54 UU |

**Ride height:** at rest length, before load sag, a car hull's bottom rides 0.32-0.48 m above the ground (Car4 0.40 m). The trucks range from 0.19 m (Optimus) to 0.51 m (Truck4).
- **So in the original** lips under about 0.4 m are the wheels' job on every car.
- **The rebuild's probes** ignore this ride height: they test hull-frame heights from a root on the floor.
- **The fix** keeps every Driving probe at or above the 0.45 m clearance [PROV]. That is inside the original's range.
- **Follow-up (next item):** the full model offsets all three probes by each chassis' authored wheel contact height, with load sag optional.

### Chassis with a Driving probe below the clearance (`VehicleHullTable.inc`)

| Chassis | Hull bottom / top (m) | Mid probe (m) | Top probe (m) |
|---|---|---|---|
| Car (Bumblebee) | −0.304 / 0.791 | **0.24** | 0.69 |
| Car2 (Sideswipe) | −0.314 / 0.562 | **0.12** | 0.46 |
| Car3 (Breakdown) | −0.149 / 0.748 | **0.30** | 0.65 |
| Car4 (Barricade) | −0.231 / 0.834 | **0.30** | 0.73 |
| Car5 (Arcee) | −0.184 / 0.672 | **0.24** | 0.57 |
| Car6 (Jazz) | −0.240 / 0.630 | **0.20** | 0.53 |
| Car7 (Deadend) | −0.243 / 0.654 | **0.21** | 0.55 |

- **Trucks:** all clear, with mid probes of 0.69-1.14 m. The patch changes nothing for them.
- **Tanks and jets:** they have no Driving state, so they are unaffected.

## Checks

**The Debris spot**, Smart seed 1, bot 9, the same run up to the lip:

| t (s) | Before: speed (m/s) | After: speed (m/s) |
|---|---|---|
| 20.5 | 19.5 | 19.5 |
| 21.0 | **0.0**, pinned at (−63.4, 130.3, −61.2) | 23.0 |
| 21.5 | 0.0 | 25.3 |
| 22.0 | 0.0 | 27.6 |
| 22.5 | 14.0, released | 24.9 |

After the fix the bot reaches its waypoint 46 about 1.5 s sooner.

**VEHTEST, new car rows** (Car2, Car4):

| Case | Before | After |
|---|---|---|
| Head-on step, Car2 | Driving drops from 0.20 m | drops only at 0.50 m |
| Head-on step, Car4 | Driving drops from 0.35 m | drops only at 0.50 m |
| 0.35 m riser at 36° | **blocked at 0 m/s** (both cars) | **crossed at 22.1 m/s**, still Driving |

**Other tests:**
- **VEHTEST:** every other line is byte-identical. VEHTEST's own rows use the Truck.
- **VEHFRAMETEST:** byte-identical, 4 / 4 PASS.
- **BOTTEST:** 46 / 46 for both AIs.

**NAVQ, 24 seeds × 4 maps, against the yaw base (54ac361d).** No map is worse for either AI: the largest z is 1.41 (Smart, Gorge, follow reversals).

| Map | Smart slow % | Smart off-mesh % | Classic slow % | Classic off-mesh % |
|---|---|---|---|---|
| Streets | 4.59 → 5.10 (0.9) | 4.52 → 5.06 (0.8) | 6.09 → 6.54 (0.7) | 5.14 → 5.38 (0.3) |
| Molten | 1.96 → 2.40 (1.0) | 1.84 → 2.23 (0.9) | 2.31 → 2.18 (−0.4) | 1.48 → 1.56 (0.4) |
| Debris | 4.77 → 4.51 (−0.5) | 2.99 → 2.91 (−0.2) | 4.42 → 3.68 (−1.3) | 2.33 → 2.43 (0.2) |
| Gorge | 4.45 → 4.73 (0.4) | 4.84 → 5.11 (0.3) | 6.32 → 6.08 (−0.3) | 7.19 → 7.07 (−0.1) |

**The rest of the Debris Smart cost is not this bug.** Against the pre-yaw base f7b7828d, Debris Smart slow is still at z 2.3 and off-mesh at z 2.5. The form split (% of all travel time, pre-yaw → yaw → this patch) puts the remainder in hover and jet form:

| Form | Pre-yaw | Yaw | This patch |
|---|---|---|---|
| Driving slow | 0.04 | 0.46 | **0.05** |
| Hover slow | 1.02 | 1.33 | 1.46 |
| Hover off-mesh | 0.70 | 1.09 | 1.18 |
| Jet slow | 1.72 | 1.77 | 2.08 |
| Robot slow | 0.69 | 1.22 | 0.93 (outlier seeds) |

Hover slow time is not spent while turning, so it is the next trace. Corner anticipation (an early facing turn) was measured as a null result and dropped.

## Classic byte-exact reference: re-baselined on this commit

`WFC_SIMHASH=0`, Classic, 24 seeds × 4 maps. The old column is the previous reference (`docs/handoff/vehyaw`, the yaw commit), which 54ac361d reproduces exactly; the new column is this patch.

- **75 of 96 runs diverge.** The first divergent step is between 777 and 18,133 (median 1,750): a run changes only when a car meets a lip between its old mid probe and 0.45 m.
- **Identical runs:** Streets 4, Molten 0, Debris 0, Gorge 17.
- **Files:**
  - `docs/handoff/driveprobe/CLASSIC_SIMHASH_old_vs_new.txt`: per run, the final step and hash old and new, and the first divergent step or "identical".
  - `docs/handoff/driveprobe/CLASSIC_NAVQ_REFERENCE_new.txt`: the 96 new NAVQ SUMMARY lines.
- **From this commit on,** Classic lines must reproduce these exactly. Smart is not a byte-exact reference.
