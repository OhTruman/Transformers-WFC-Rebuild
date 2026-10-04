# Handoff to Gameplay: the vehicle rear propulsion "open / close" = Driving state flicker under held boost

**Human report:** the vehicle's rear propulsion effect looks abrupt, "open, close, open, close", and unnatural.

## What the effect is (authored, unchanged)
The boost afterburners are `bumble_boost_small1_FX` on `BoostSocket_L` / `_R`:
- one-shot on activation: `thruster`, `Cone_thrust_Dup`, `Particle Emitter_Dup`, each a 0.5 s burst;
- looping while active: `loopcone` (3–4 / s, 1 s life) and `Particle Emitter_Dup_Dup` (20 / s);
- `bKillOnDeactivate`.

`World::tickVehicleFx` starts the system when `vehicleState().driving` (the Boosting / Driving state) begins, and kills
it when Driving ends, when hover FX takes over.

**This matches the original.** BoostFx belongs to the Driving (Boosting) state, so the renderer plays the system
correctly. The open/close look comes from the state flipping.

## Measured on integration/milestone-05 (export with logging, `WFC_VFXLOG=1`)
Boost was held from frame 60 (`WFC_AUTOBOOST=60`, `WFC_AUTOWALK=1`, vehicle start, lockstep, 840 frames).

| start | Driving ↔ Hovering transitions (boost held throughout) |
|---|---|
| 1 | 29 |
| 4 | 22 |
| 7 | 27 |
| 18 | 15 |
| 21 | 31 |

**Every** drop is `CharacterMovement` → `Driving.OnRigidBodyCollision` ("frontal hull block"). The drops happen at floor
height, while driving straight at 3–17 m/s through open floor (e.g. start 21: (249.4, −724.5, −492.3),
(232.1, −724.5, −464.7), (221.5, −723.6, −463.2)).

Each drop:
1. Driving → Hovering, killing the boost FX;
2. the 0.5 s `kHoverDriftDuration`;
3. Driving again, so the boost FX restarts with its 0.5 s one-shot burst.

That cycle is ≈ 0.6 s: the visible "open / close". The boost-released path was never taken.

## Likely cause (Gameplay, pass 20)
The PROVISIONAL hull wall probes (`probes[3]`, the low probe at 0.45 m while driving, `skipWalkable`) report frontal
contacts on floor-level geometry. Candidates are seams, curbs, decal / trim strips, and non-walkable faces under 0.7 m.
`dot(moveDir, forward) > 0.866` then triggers the authored frontal-collision drop.

The original's rigid-body hull does not produce these contacts while rolling on wheels.

## Suggested checks
- Log the hit normal and the probe height for each drop.
- Require the hit to be a wall: a non-walkable normal facing against the motion and above the wheel contact height.
- Or require an actual speed loss, before applying `OnRigidBodyCollision`.

Rendering will keep the boost FX bound to the Driving state; it must not be smoothed over.

Rendering's `WFC_PICK` diagnostic (agents/rendering M08) names the rendered component under the crosshair. Use it to
identify the floor pieces involved.
