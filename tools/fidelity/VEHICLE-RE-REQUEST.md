# Targeted native RE request: HoverTruck physics, Dash direction (ReVa, read-only)

**Status:** the vehicle physical response is measured but NOT asserted. Nothing below may be tuned by
feel. Each item flips its `vehicle_profiles` probe from INFO (UNKNOWN) to a `conf()` check (owner
Gameplay) once answered. The merge gate then enforces it automatically.

Already CONFIRMED and passing:
- hover height 1.85 m
- Boost = Driving 30 m/s
- Dash 30 m/s × 0.5 s, cooldown 2 s
- Nitro ×1.5 (45 m/s) for 3 s, cooldown 8 s, steer ×0.3
- hover heading follows the camera

## Measured rebuild behaviour (milestone-02 baseline, `results/m03-baseline/vehicle_profiles.json`)
| probe | rebuild | why it matters |
|---|---|---|
| 0.5 m step / 0.25 m bumps (`*.max_vertical_step`) | full height change in ONE 1/60 s step, vertical speed 0 | hull snaps to terrain |
| +1 m release (`vertical.drop1m_*`) | 0 oscillations, settles in 1 step | no spring / damper |
| 4 m ledge (`ledge.*`) | 0.63 s airtime, lands at −12.3 m/s, ride height back in 1 step | no landing compression |
| body pitch/roll (`vertical.body_pitch_roll`) | none (mesh rotates by yaw only) | no lean on slopes / accel / turns |
| accel t90 / release-to-stop (`long.HOVER.*`) | 0.45 s / 0.47 s | symmetric response |
| velocity vs heading after a 90° camera step (`steer.*.max_slip_deg`) | up to 88° (heading instant, velocity lags) | crab / skid |
| jump (`vertical.jump_launch_vy`) | 0 m/s | dead jump |
| Dash after a 90° camera step (`steer.DASH.velocity_t_to_81deg`) | velocity re-aimed in 3 frames | dash steerable instantly |

## Questions
1. **Suspension / ride-height correction.** `TnCarForm` / HoverTruck hovering physics.
   - How is ride height maintained: spring constant, damping, max correction speed or force, per-wheel
     or per-thruster probes (count, positions, trace length)?
   - Is the correction applied as a force (mass, `RBPhysicsGravityScaling` 0.66) or as a position lerp?
   - What happens over a step higher than the probe reach, and on landing from a fall?
2. **Body pitch/roll.**
   - Does the vehicle mesh/actor rotation take pitch/roll from the ground normal(s), from acceleration
     or turning, or from the rigid body?
   - Limits and interpolation rates.
   - Is it different in Hovering vs Driving?
3. **Jump.**
   - Is `JumpLinearSpeed` 1200 UU/s applied, in which states (Hovering / Driving), along which axis?
   - Airborne gravity scale for the vehicle.
   - Air control and landing behaviour.
4. **Dash direction.**
   - Is the dash vector fixed at activation ("forward only") or re-aimed with the camera/heading during
     its 0.5 s?
   - What does Dash do with no stick input?
5. **Accel/decel asymmetry and lateral grip.**
   - Hover truck acceleration vs braking/deceleration rates.
   - Lateral friction / velocity alignment toward the heading (the 88° slip).

## Not physics, already evidenced (owner Gameplay; gate checks in place)
- **Robot→vehicle mesh display.** The rebuild shows one mesh and switches at 1.017 s; the authored
  window shows both meshes 0.396–0.880 s. Checks: `transform_analyzer.r2v_*.target_mesh_first_visible`
  / `source_mesh_hidden` / `overlap_inside_authored_window`.
- **Vehicle→robot.** The switch is at 0.583 s; the authored window is 0.098–0.663 s. Same checks for
  `v2r_*`.
- **Weapon usable before the gun is visible** (5 steps): `*.no_usable_weapon_without_muzzle`.
- **Driving (Boost/Nitro) passes through a 0.5 m raised surface at constant height:**
  `vehicle_profiles.step.{BOOST,NITRO}.never_below_surface`.
- **Vehicle clip geometry ~39.5 m below the pawn for 17 steps** of `Transform_ToRobot_VEH`
  (`*.far_vertex_steps`, INFO). It is authored clip data. Whether the original hides it (two-mesh
  display / visibility) is a human/RE check; it is not asserted.
