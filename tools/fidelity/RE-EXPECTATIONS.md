# Harness expectations updated from native RE evidence (2026-10-01)

Source: `F:\Transformers Rebuild\RE-Workspace\notes\` (commit 3d57666). The files are
`HANDOFF_2026-10-01.md`, `TARGETED_PASS_2026-10-01.md`, `TARGETED_PASS2_2026-10-01.md` and
`STEPUP_LEDGE_2026-10-01.md`. They contain UnrealScript bytecode decoded from `TransGame.xxx`, plus
native code in `default.xex` read through headless Ghidra. **No product code was changed.**

## How confirmed values are reported
New status helper `Report::conf` / `confTruth`:
- **PASS** when the build matches the confirmed original.
- **KNOWN (owner)** when a product branch has not adopted it yet.

So confirmed values never turn an older branch into a regression. The source string of each check
starts with `CONFIRMED:` and cites the RE note.

The reference sheet (`reference/original_measurements.json`) now holds RE-derived originals.
They are marked `"evidence": "native RE (not a capture)"` with an `owner`. A capture should still
confirm them where practical.

## Corrections requested
| Topic | Old expectation | New expectation (CONFIRMED) | Checks | RE source |
|---|---|---|---|---|
| Foot sliding at full speed | (none; risk of being "fixed") | **Original.** Jog plays at 1.0× at 14 m/s; there's no rate scaling, and the "Strafers" synch group has RateScale 1. This protects the 1.0× rate. | `animation.locomotion_rate_1x_at_full_speed` | PASS2 #3 |
| Ion Blaster cadence | 0.065 s / 923 RPM; KNOWN, with patch `systems-1` proposed | **0.0667 s (≈900 RPM)**: the refire timer is non-looping, uses a strict `>`, and discards overshoot. The **systems-1 patch is withdrawn** (deleted). | `weapon.fire_interval_effective`, `weapon.rounds_per_minute` (900), `weapon.mag_empty_time` (3.267 s) | TARGETED_PASS #6, PASS2 #6 (SetTimer 0x82F292A8, TickTimers 0x82F867C8) |
| Transform input | Movement/fire must be locked during the fold | **Movement is live from frame 0.** Only a second transform, weapon switching and firing (robot weapon stored on robot→vehicle) are refused. | `transform.*_movement_input_live`, `transform_momentum.{r2v,v2r}_input_live_from_frame0`, `transform.to_vehicle_robot_weapon_stored` | PASS2 #1 |
| Robot→vehicle velocity | UNKNOWN; "not a dead stop" | **Kept: `ClampLength(Velocity, 3500)` written to the RB at t=0.** | `transform_momentum.r2v_*.v_first_step` (= min(v, 35 m/s)), `.momentum_kept_through_fold`, `transform.transform_while_running_speed` | TARGETED_PASS #1d, PASS2 #1 |
| Vehicle→robot velocity | UNKNOWN | **Kept** (local player). The robot falls; it only decelerates above robot MaxSpeed, using InAir momentum (≈1.19 m/s² pushing forward or neutral). | `transform_momentum.v2r_*.v_first_step` (= v), `.momentum_kept_through_fold` | TARGETED_PASS #1e, PASS2 #1 |
| Form / physics switch | mid-fold handoff (`kTransformHandoffFrac` 0.5) | Target form `Activate()` and the physics swap at **t=0**. The fold ends at min(src, dst clip)/Rate. Both meshes animate the whole duration. | `transform.*_movement_form_switch_time`, `transform.*_total_time`; mesh handoff is INFO | HANDOFF #4 |
| Fine aim | absent; FOV 35/45/55 (point-of-interest bands, wrong table) | **Generic `[TnPCS_FineAim]` profile:**<br>• FOV 80 → **45** (smoothing 0.1, exit 0.4)<br>• look 50/25 → **25/12.5** (×0.5)<br>• speed **×0.5**, spread **×0.5**<br>• shoulder X 150 → −50 UU<br>• blocked while reloading, meleeing or dodging; auto-resumes<br>• input: RMB toggle, LT hold | `fine_aim.fov_normal`, `.fov_fine_aim`, `.fine_aim_active`, `.speed_mult`, `.look_scale`, `.blocked_while_reloading`, `.resumes_after_reload`; camera shift is INFO | TARGETED_PASS #3, PASS2 #4 |
| Weapon restore | 0.25 (from ini) | **CONFIRMED in script:** `RemainingTimeAsFactor <= 0.75` = 75% remaining / **25% elapsed**, vehicle→robot only, then the 0.2 s equip. | `transform_momentum.weapon_restore_frac_elapsed_to_robot` | PASS2 #7 |

## Further expectations the RE notes overturned (handed to Experimental)
| Topic | Old | New (CONFIRMED) | RE source |
|---|---|---|---|
| Robot movement values | GroundSpeed 550, AccelRate 2048, AirControl 0.7, AirSpeed 1500, JumpHeight 625, radius 175, BaseEyeHeight 80 (class defaults) | **1400 / 12000 / 0.4 / 1200 / 500 / 200 / 150** (`Optimus_ROBODEF` via `TnPawn.ApplyTransformer`). JumpZ 1714.6. No sprint state: full input = the 14 m/s jog. | HANDOFF #1, TARGETED_PASS #2 |
| Braking | INFO | no stick input on ground → desired 0 at AccelRate → stop in 0.117 s | HANDOFF #2 |
| Jump apex | 6.25 m | **5.0 m exactly, at any tick rate** (trapezoidal `physFalling`, 0.05 s substeps) | STEPUP_LEDGE #4 |
| Step-up | 0.35 m | **0.37 m** (instant pop, MaxStepHeight 35 + 2 UU); walkable normal Z ≥ 0.7 | STEPUP_LEDGE #2 |
| Wall contact | slide (engine assumption) | slide = delta projected on the wall + 2 UU push-out, up to 3 sweeps | STEPUP_LEDGE #1 |
| Ledges | — | the player robot **always walks off** ledges, keeping its horizontal velocity (new `collision.walks_off_ledges`, `ledge_leave_speed_ratio`) | STEPUP_LEDGE #3 |
| Camera FOV | 75 (Xe-TransCamera.ini class default) | **80** (OverTheShoulder strategy) | TARGETED_PASS #3 |
| Vehicle boost | DashSpeed 5000 / 0.3 s, held Sprint | boost = **Driving mode** (Truck MaxSpeed **3000**, MaxAccel 2500), sustained while held, released through a 0.5 s drift. Input is RMB / LT, shared with FineAim. Dash = VehicleSpecialMove (3000 for 0.5 s, 2 s cooldown). 5000/0.3 are unused class defaults. | HANDOFF #6, PASS2 #2 |
| Hover height | 2.0 m | **1.85 m** (HoverTruck_Physics SuspensionRadius 185) | PASS2 #2 |
| Vehicle heading | faces travel; AiMaxAngularSpeed 180°/s | **yaw-tracks the camera** every physics step in Hovering and strafes in local axes. AiMaxAngularSpeed is AI-only. | HANDOFF #7, PASS2 #1 |
| Input edges | "latch the press" (own inference) | pattern CONFIRMED: latch on edge, consume in the sim. **Reload fires on the release of a tap < 0.3 s**; a hold ≥ 0.3 s is revive/pickup (new `input_edges.reload_on_tap_release`, `reload_hold_is_not_reload`). | PASS2 #5 |
| Dodge clips | trigger UNKNOWN | dodge is an **ability** (`TnAbilityDodge`, Ability0/1, DodgeSpeed 3000, 0.5 s), not sprint. Optimus's loadout slot is still UNKNOWN. | TARGETED_PASS #2 |
| Energon colour (Rendering lead) | compiled red constant vs blue bakes | set at runtime by `TnCharacterApplier` through the `EnergonColor` parameter (character data, else the robot MIC). The compiled constant is likely the parent default, which the rebuild never overrides. | PASS2 #8 |

## Harness fixes found while adopting the new values
- Step-up sweep: at 14 m/s the pawn overran the 34 m test platform, so "INSIDE" was false.
  Classification is now from the trace on a longer platform.
- `camera_behind_body` / `aim_ray_through_crosshair`: the over-the-shoulder offset is original.
  The checks now assert "behind the body" and "the shot ray crosses the camera centre ray".
- Reload tests use a real tap (press, then release), because the original reloads on release.
- Boost tests drive the authored input (the FineAim button) when the build has one.
