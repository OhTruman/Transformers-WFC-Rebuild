# Fidelity harness — findings for owning workstreams (2026-10-01)

These come from `wfc_fidelity` and screenshots taken in this worktree. **No product code was
changed.** Each item shows as `KNOWN owner=…` in the harness and turns to `RESOLVED` when fixed.

## HIGH — Gameplay

### 1. Robot and vehicle render 90° off their gameplay facing (`kMeshYawOffset`)
- Skeleton-derived facing, `forward = up × (R_shoulder − L_shoulder)`: shoulders **−104°**, hips
  **−108°** from the gameplay yaw. The vehicle's bind-pose long axis is X (6.68 m vs 3.13 m), so it
  sits **90°** to its travel direction: the truck drives sideways.
- Screenshot `work/fidelity/shots/idle_chase.png`: in the default chase cam Optimus is **side-on**,
  gun pointing screen-right. The muzzle sits 2.35 m *behind* the body.
- Cause: UE meshes are +X-forward. umodel maps UE +X → glTF +X, but the rebuild's forward is −Z.
  Pass 6's "offset 0 verified (face·toCam ≈ −0.9)" measured the yaw *variable*, which cannot see
  a mesh rotation error.
- Experiment (temporary local edit, reverted): `kMeshYawOffset = +π/2` puts shoulders/hips within
  14–18° (idle stance twist). The camera then sees Optimus's back, and the gun is on the right,
  pointing into the scene (`exp_yaw90_chase.png`). The muzzle moves 1.56 m forward and the barrel
  is 29° off facing (idle pose).
- Suggested fix: `kMeshYawOffset = core::PI * 0.5f`. Then re-check the muzzle offset and the
  vehicle `atan2` facing visually.

### 2. Robot→vehicle transform hands off into the wrong vehicle clip
- `vehicle.glb` has two clips in category `vehicle_transform_to_vehicle`:
  `Transform_ToVehicle_SuperBoost_Veh` (0.80 s) comes **first**, then `Transform_ToVehicle_VEH`
  (1.9667 s, the real pair of `Transform_ToVehicle_ROBO`). `firstClipOfCategory()` picks SuperBoost.
- Result: the robot fold runs to 50% and then resumes the *SuperBoost* clip at 50%. That gives a
  pose pop, and the transform ends after **1.42 s instead of 1.97 s**. Trace:
  `Transform_ToVehicle_ROBO > Transform_ToVehicle_SuperBoost_Veh > Nav_Hover_Pose_VEH`.
- The robot side works only by ordering luck (`…_ROBO` is listed before `…_SuperBoost_Robo`).
- Fix: select the incoming clip by name or matching duration, or exclude `SuperBoost` (which is
  probably the boost-transform variant).

## MED — Gameplay (movement/collision)
3. **Jump apex 6.41 m vs MaxJumpHeight 6.25 m.** It depends on tick rate (30 Hz 6.57, 120 Hz
   6.33) because position uses the post-gravity velocity. UE3 `physFalling` averages old and new
   velocity. Fix: vertical `p += (vOld+vNew)/2·dt`. The vehicle jump has the same cause
   (3.81 vs 3.71 m).
4. **Step height ~1.1 m vs UE3 default MaxStepHeight 35 UU = 0.35 m.** `kStepUp` 0.6 is counted
   twice: once in the search top and again in the `groundHeight` window.
5. **Ledges between 1.3 and 2 m are walked *into*.** The wall probe is a single ray at capsule
   centre (2 m), so geometry below it but above the step window is never blocked.
6. **No wall sliding.** Any probe hit zeroes all horizontal velocity, so diagonal input along a
   wall gives 0.26 m/s where UE3 `SlideAlongSurface` gives ≈3.9 m/s. This makes corners and
   doorways sticky.
7. Vehicle (SUSPECT, needs an original capture):
   - Sprint holds DashSpeed **indefinitely**, although DashDuration 0.3 s suggests a timed burst.
   - Releasing Sprint snaps the speed **50 → 15 m/s in one step**, because the grounded cap
     clamps it.
   - Max yaw rate is 162°/s, from velocity-direction facing; `AiMaxAngularSpeed` may only apply
     to AI.
8. The first transform step is swallowed: `playClip` resets animTime on the clip change, adding
   about 1 step. Minor.

## MED — Systems
9. **Fire rate 900 RPM vs authored 923 RPM** (0.065 s). `Weapon::onFired` sets
   `cooldown = fireInterval` and throws away the sub-step remainder, so every shot waits 4 steps
   at 60 Hz and a mag dump takes 3.27 s instead of 3.19 s. Fix: `cooldown += fireInterval` (clamp
   when idle). UE3 looping timers keep the remainder; confirm against an original capture.

## Verified OK (regression-locked now)
- All `[CONF]` constants match their UU sources.
- Weapon defaults match `weapon.json`.
- 75° hFOV survives projection.
- Input handedness: D → screen-right, mouse-right → turn right, mouse-up → look up.
- The hitscan ray passes exactly through the crosshair.
- Strafe clips F/B/L/R are chosen correctly.
- Reload: timing, ammo accounting and clip are correct.
- Input is locked and the weapon holstered during transform.
- Hover height is 2.0 m; cruise and dash speeds are correct.
- Replay is bit-identical.
- The full sim + CPU skin step costs ~0.8 ms (Debug build).
- Streets collision: 1.85 M tris; the spawn floor is at −724.48.

## Open measurement requests (original game)
`reference/original_measurements.json` lists 27 metrics with capture methods. The most
decision-relevant are:
- braking/stop time
- boost curve (burst vs sustained)
- transform total times
- fire RPM
- max step height
- camera distance and shoulder offset
- whether the robot keeps momentum when transforming on the move
