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

**Decided (Integration, 2026-10-16):** Optimus ships faithful (0.19 m ride height, 0.34 m floor probe), with no physics exception: the original's Optimus was blocked by those lips as well, and the bar is the original.

- **Added gate:** truck-chassis NAVQ / MAPTRAVERSE routes must not regress.
- **If truck bots stick at 0.35-0.45 m lips:** the fix is a bot path rule (route trucks around what they cannot climb), never a physics exception.

## RE update 2026-10-17 (RE lane, HOVER_RIDE_HEIGHT_RE.md aba7af9 → docs/handoff/GAMEPLAY_HOVER_RE.md)

**Hover height:** there is no target-ride-height controller.
- The height is the equilibrium of 4 diagonal spring probes, L = L0 − 1940.4·(M/4)/K from the COM plane.
- Car 109 UU (origin ≈ 125); truck / Optimus 128.7 (origin ≈ 109).
- Probe length = RestingLength: car 200, truck 250, tank 250. Springs: car 8000 / 4000, truck 10000 / 4000, tank 20000 / 6000.
- The rays point body-down, so they tilt with the hull.
- A hit counts if it BlockRigidBody with RBChannel != EffectPhysics, or is the world.
- Force: car / truck max(0, (−dir·N)·F) at the probe; tank max(0, F). No lift clamp, no tilt limit.
- No ramp / ledge code: an edge probe just loses force; a lip launch is ballistic with vZ ≥ −3500.
- This is independent of the Driving wheel-contact ride height above, which stays as designed.

**Bot self-righting is attitude-gated:** RotateToTargetOrientation toward a yaw-only target, with mask = (|pitch| or |roll| > 30°) ?
all axes : yaw.
- The gate ignores contacts. AiMaxAngularSpeed / Accel: car 12 / 12, truck 20 / 20, tank 6 / 6. Angular damping 0.1. AI translation
  is horizontal only.
- The contact gate (NumContacts == 0 || flipped, 5 % per 1/30 s) is the PLAYER rule only.

**Checked against af4d0731, to fix in this item:**
1. `CharacterMovement.cpp` ~529: the player's UpdateTurn ShouldUpright pull (contacts == 0 || flipped) runs for bots too. The AI path
   must skip it (an AI hover vehicle does not run the player's UpdateTurn).
2. `CharacterMovement.cpp` ~437: one tip mode measures the tilt against the contact normal (`dot(B.z, cn)`). The original's gate is
   world pitch / roll > 30°, ignoring contacts.
   - The `aiTipped` rate-limited correction itself (~543) already matches (the authored AiMaxAngular* values).
- Both are Classic-visible: they go in the shared Classic SIMHASH re-baseline with the squad item.

**Integration decision 2026-10-17:**
- **The deliverable is the faithful model:** four implicit spring probes with the authored HoverCar / HoverTruck / HoverTank values
  (Optimus HoverTruck_Physics 185 / 10000 / 4000 / 250), zero-extent traces along body-down, force max(0, (−dir·N)·F) along
  body-up, no clamp / tilt limit. Not the controller above.
  - If our hover already runs these probes, the item is a check of the values, the placement on the COM diagonals and the force
    direction, with the equilibrium heights (car origin ≈ 125 UU, truck ≈ 109) as the test.
- **Smart's righting becomes the faithful AI rule:** world pitch / roll > 30° (RE-confirmed: IsUnstable reads the rotator; the contact
  normal only feeds IsOnTheGround and the car probe force), and no player ShouldUpright pull for AI vehicles. Re-measure tipped time
  / stucks / off-mesh.
- **Classic** (af4d0731) does not run the AI righting path at all: aiTip −1, player rule only. That's a Classic fidelity gap,
  **raised to Integration, not fixed** (Classic byte-identity is a user rule).

**Integration decision 2026-10-17 (the user delegated it): Classic gets the original's AI righting path too.**
- The attitude gate > 30°, rate-limited AiMaxAngular*, and no player pull for AI vehicles: Classic's purpose is the original's AI,
  and this is a proven gap, not drift.
- Done inside this item, so ONE Classic SIMHASH re-baseline covers ride height + squad + righting.
- The re-baseline is recorded as a deliberate fidelity change in the package 7 doc, with before / after tipped time on the 4 maps for
  Classic as well as Smart.
- Until this item, Classic stays as is (the patch 2 and killer-cam gates keep the current baseline).
