# Driving hull probes at the authored ride height: design (package 7)

Gameplay, 2026-10-16. A design note only; the patch follows after package 6. It follows `GAMEPLAY_DRIVE_PROBE.md`, whose 0.45 m clamp was the interim fix, and is Integration's package 7 item.

## Problem

In Driving the rebuild rests the vehicle root on the floor: wheel contact counts when root − ground ≤ 0.05 m (`CharacterMovement` Driving).

The hull is blocked by three horizontal probes at **hull-frame** heights above that root:
- the floor probe: 0.45 m (wheel clearance);
- the mid probe: max(0.45, ½ (hullBottom + hullTop));
- the top probe: max(0.45, hullTop − 0.1).

The original's PHYSSYS hull doesn't sit at the floor. It rides on the raycast `TnWheelAssembly` wheels, with its origin one wheel-contact height above the ground. So the rebuild tests the hull about 0.5-0.6 m lower than the original holds it. The 0.45 m clamp only hides that for the low car hulls.

## Authored ride height (authored.db, VEH_SHARED_p)

| Blueprint | Wheel Z | WheelRadius | RestingLength | Contact below the hull origin |
|---|---|---|---|---|
| Car_Physics (all seven cars) | −13 UU | 30 | 20 (Car_Suspension) | **63 UU = 0.63 m** |
| Truck_Physics (all trucks) | +21 UU | 45 | 30 (class default) | **54 UU = 0.54 m** |

These are at rest length, before load sag.

**Load sag (optional, PROV):**
- per-wheel load = mass × g / 4 (Car 1,500 kg, Truck 2,500 kg);
- Stiffness: Car_Suspension 100,000, Truck_Suspension 120,000, in UE3 units (force per UU, scaled by the PhysX unit setup).
- The unit scale isn't recovered, so the first patch uses rest length and logs the sag it would apply.

## Model

In Driving, every hull-frame height h becomes **ride + h**, with ride = the contact depth above (0.63 m for cars, 0.54 m for trucks):
- the three wall probes: ride + {hullBottom + 0.15, ½ (hullBottom + hullTop), hullTop − 0.1};
- the ceiling probe (hull top against overhead geometry): ride + hullTop.

The 0.45 m clamp is removed: the floor probe is now the hull's own bottom edge plus 0.15 m.

Hover / tank / jet are unchanged (their hover simulation already places the hull).

## Probe heights per chassis (m above the floor)

| Chassis | Hull bottom above ground | Today: floor / mid / top | Ride-height model: floor / mid / top | Hull top |
|---|---|---|---|---|
| Car | 0.33 | 0.45 / 0.45 / 0.69 | 0.48 / 0.87 / 1.32 | 1.42 |
| Car2 | 0.32 | 0.45 / 0.45 / 0.46 | 0.47 / 0.75 / 1.09 | 1.19 |
| Car3 | 0.48 | 0.45 / 0.45 / 0.65 | 0.63 / 0.93 / 1.28 | 1.38 |
| Car4 | 0.40 | 0.45 / 0.45 / 0.73 | 0.55 / 0.93 / 1.36 | 1.46 |
| Car5 | 0.45 | 0.45 / 0.45 / 0.57 | 0.60 / 0.87 / 1.20 | 1.30 |
| Car6 | 0.39 | 0.45 / 0.45 / 0.53 | 0.54 / 0.83 / 1.16 | 1.26 |
| Car7 | 0.39 | 0.45 / 0.45 / 0.55 | 0.54 / 0.84 / 1.18 | 1.28 |
| Truck / Truck7 (Optimus) | 0.19 | 0.45 / 0.75 / 1.75 | **0.34** / 1.29 / 2.29 | 2.39 |
| Truck2 / Truck3 | 0.59 | 0.45 / 0.93 / 1.71 | 0.74 / 1.47 / 2.25 | 2.35 |
| Truck4 (Soundwave) | 0.51 | 0.45 / 0.69 / 1.32 | 0.66 / 1.23 / 1.86 | 1.96 |
| Truck5 | 0.67 | 0.45 / 1.03 / 1.83 | 0.82 / 1.57 / 2.38 | 2.48 |
| Truck6 | 0.55 | 0.45 / 1.14 / 2.18 | 0.70 / 1.68 / 2.72 | 2.82 |

## Consequences to check (player-facing)

- **Cars clear a little more:** floor probes 0.47-0.63 m, against 0.45 today. A 0.5 m kerb is cleared by most cars, matching the original's wheels-climb behaviour.
- **Optimus (Truck / Truck7) clears less:** the floor probe drops to 0.34 m. That hull's bottom really does ride 0.19 m above the ground in the original. Lips of 0.35-0.45 m that Optimus drives over today would block it. The VEHTEST step rows (0.30 / 0.35 / 0.40 m) must show this per chassis, and the Streets / Debris routes must be checked for such lips on the boost lanes.
- **Taller overhead clearance:** the ceiling probe rises by the ride height, so some low overhangs a Driving truck passes today would block it. That's faithful, since the original hull is there.
- **Classic:** a Classic re-baseline, the same as every movement change.

## Gate (as for the drive-probe patch)

- VEHTEST, all rows + the car / truck step rows extended to every Driving chassis (0.10-0.50 m steps head-on and the 36° riser), base vs new;
- VEHFRAMETEST identical;
- NAVQ 24 × 4 both AIs;
- BOTTEST 12 seeds;
- MAPTRAVERSE vehicle tours (list any route that flips);
- the Debris lip spot (−63.4, 130.3, −61.2) replay;
- Classic SIMHASH re-baseline.

**Open question for Integration:** if Optimus's lower floor probe blocks boost lanes the rebuild's maps rely on, should the faithful ride height ship anyway (the original's Optimus would also stop), or should Optimus keep a 0.45 m floor probe as a documented exception?
