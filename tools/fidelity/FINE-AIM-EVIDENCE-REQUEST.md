# Evidence request: Fine Aim camera offset semantics (ReVa / native RE)

**Status: UNRESOLVED. Neither the measured rebuild shift nor the "~2 m" reading is asserted.**
`fine_aim_probe.camera_lateral_*` stay INFO until this is answered.

## Authored data (already recovered, RE-Workspace TARGETED_PASS_2026-10-01 §3)
`CAM_Strategies_p.OverTheShoulder_STRATEGY`, behaviour `TnScreenSpaceOffsetByPitchCameraBehavior`. The
Ion Blaster has no WeaponPCS, so the generic `[TnPCS_FineAim]` row applies.

| state | offset (X,Y,Z) UU at pitch −75 / 0 / +75 | SmoothTime |
|---|---|---|
| normal | (150,300,150) / (150,300,−35) / (150,300,150) | 0.3 |
| fine aim | (−50,300,80) / (−50,300,−35) / (−50,300,80) | 0.1 |
| hovering + fine aim | (150/−25, 300, 150/−100) (row as noted) | — |

## What the rebuild does now
`src/core/Config.h` interprets the curve as a **rightward** offset using the Y column (300 UU = 3 m at
pitch 0, 1.5 m at the extremes, `[PROV] semantics`). It uses only that one curve; fine aim does not change
it.

Measured with `fine_aim_probe`:
- shoulder offset normal 2.83 m
- shoulder offset fine aim 2.83 m
- shift 0.00 m
- the real exe showed about 0.05 m (smoothing residue)

## Candidate interpretations (all consistent with the numbers; none confirmed)
| reading | axes | fine-aim change at pitch 0 |
|---|---|---|
| A (rebuild) | Y = camera right, X/Z unused | none |
| B | X = screen-horizontal (right), Y = vertical, Z = depth | camera moves 2.0 m LEFT (+1.5 → −0.5 m); Z depth −0.35 m unchanged |
| C | UE camera space: X = forward, Y = right, Z = up | 2.0 m dolly-IN (toward the target), no lateral change; height 1.5 → 0.8 m at extreme pitch |

## Request (native RE / ReVa; read-only)
1. **`TnScreenSpaceOffsetByPitchCameraBehavior::UpdateCamera`** (or its Apply/Evaluate virtual): how the
   interpolated offset vector is applied. Is it rotated by the camera rotation (`GetAxes`), by yaw only,
   or applied as a projection-plane shift? Which component goes to which axis? Are the units UU in world
   space or a screen fraction?
2. Whether the pitch curve evaluates X, Y and Z independently (per-component curves) or as one vector key.
3. How `PCSTransitionInTime/OutTime` and `SmoothTime` (0.3 normal, 0.1 fine aim) combine when the PCS row
   changes. That tells us the entry/exit profile.
4. Optional ground truth: one 360/emulator capture of entering fine aim with the Ion Blaster, standing at
   pitch 0, side by side with the HUD crosshair. Two frames are enough to decide between readings A, B
   and C.

## How it will be checked once answered
`fine_aim_probe.camera_lateral_shift` and `camera_distance_fine_aim` become `conf()` checks against the
confirmed reading (owner Gameplay), and the merge gate picks them up automatically.
