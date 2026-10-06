# Vehicle native RE: status

The original request (suspension, pitch/roll, jump, Dash direction, grip) was **answered** by
`RE-Workspace/notes/MILESTONE03_VEHICLE_NATIVE_FIDELITY.md` (P1–P10). Those answers are now asserted
by the harness suites `native_vehicle` / `native_robot` and the `transform_analyzer` notify windows.
They were validated on agents/gameplay 6dfaf0d (`results/xbranch-gameplay-6dfaf0d`).

## Answered → asserted (conf, owner Gameplay)
| native item | harness checks |
|---|---|
| P1 suspension: 4 body-down probes on the COM plane, implicit push-only springs (K 10000, B 4000, m = M/4), RB gravity −1940.4, zero RB damping, no ride-height target | `native_vehicle.rest.*`, `free_fall.*`, `push_only.no_pull`, `step_*.no_one_frame_snap`, `step_*.rest_com_on_top`, `drop10.*`, `movement.vehicle_rest_com_height` |
| P2 attitude: grounded pitch/roll from the springs; yaw = camera each tick; upright 5 %/tick only when airborne or upside down | `step_*.attitude_follows_terrain`, `camera_turn.heading_vs_view_yaw`, `upright.airborne_ratio_per_tick`, `orientation.vehicle_heading_follows_camera_*` |
| P3 hover velocity: one ClampLength 3000 on local X/Y, no separate grip | `release_forward/lateral.time_to_stop` |
| P4 jumps: hover +1200 world-Z additive + local ω −1; Driving local (600, 0, 1400) | `hover_jump*.dvz`, `*.horizontal_kept`, `hover_jump.pitch_kick`, `boost_jump.dv_up/dv_forward` |
| P5 Dash: body forward, mask (1,1,1), exit snap to 1500, stick ignored | `dash.*` |
| P6 fine-aim offset: orbit-space translation, X toward the pawn (150 → −50) | `native_robot.fine_aim.camera_back_shift / lateral_shift` (level pitch only) |
| P7 hand shrink | `native_robot.hand_shrink.*` |
| P8 ram reaction | `native_robot.ram.*` (the reaction is invoked directly; victim selection is not testable) |
| P9/P10 transform notifies | `transform_analyzer.*.target_mesh_first_visible / source_mesh_hidden / overlap_*` |

## Stale expectations retired by this answer
- "Hover height 1.85 m" is gone. 185 UU is the SuspensionRadius (horizontal probe radius,
  `constants.vehicle_suspension_radius`). The rest height is the separately measured COM height
  1.287 m (`vehicle_rest_com_height`, mass link HIGH). The spring rest length (250 UU), pawn origin
  height and visual mesh clearance are reported separately as INFO.
- **Ballistic jump apex 3.71 m.** Native calls it the rise before the springs re-engage, so it is a
  lower bound now (INFO).
- **Boost / nitro speed after short windows.** The native fact is the cap (asserted over 6 s). The rise
  depends on the PROVISIONAL tire model.
- **Heading vs input yaw.** Native P2 tracks the CAMERA rotation, so checks compare with the rendered
  view yaw.
- **"One mesh at a time" / "no arm mesh".** These are now data-driven (`partnerShown()`, `armShown()`).
- **Far-parked clip geometry inside a visible window.** INFO; P9 only asserts the hide after 0.6634 s.

## Still open (PROVISIONAL / PARTIAL: not promoted)
1. **Hover RB mass link** (M = 2500 from Truck_Physics). Rest height and damping ratio depend on it (HIGH).
2. **Vehicle hull contact.** Minimum clearance (rebuild 0.6 m) and ceiling probe: PhysicalVehicleMesh hull
   not recovered.
3. **Boost tire lateral coefficient** (the 2(M/4)|g| cap is native). This sets the boost/nitro rise and
   cornering.
4. **Driving wheel steering behaviour.**
5. **Camera offset curve between keys** (CurveAutoClamped tangent rule). Only level pitch (a key) is
   asserted.
6. **Ram victim selection** (TnPawn only, mass ≤ 1000, other team). It lives in World code that the
   harness does not link, and the slice spawns no pawn victims: UNTESTED.
