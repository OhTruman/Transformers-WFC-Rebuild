# Validated fix proposals (for the owning workstream to apply)

These patches are **not applied to any product branch** by the Experimental agent. Each one was
written against the owner's branch head and scored with
`ab.ps1 -Ref <branch> -Patch …` → `diff-reports.ps1 <branch> <branch+patch>`. Owners can apply
one with `git am <patch>` (or `git apply`) in their own worktree, then re-run `wfc_fidelity`.

| Patch | Base | Fixes (harness ids) | Regressions | Notes |
|---|---|---|---|---|
| `gameplay-1-jump-integration.patch` | agents/gameplay 21902b6, re-verified on 8045676 (Pass 9) | `movement.robot_jump_apex` 6.41 → **6.250 m**, `movement.vehicle_jump_apex` 3.81 → **3.711 m** | 0 | Vertical motion uses the average of old and new velocity (UE3 physFalling), which is exact under constant gravity. Apex at 30/60/120 Hz: 6.247 / 6.250 / 6.250 m (was 6.57 / 6.41 / 6.33). Airtime 1.317 s (analytic 1.304). |
| `gameplay-2-step-height.patch` (needs 1) | 〃 | `collision.max_step_height` 1.1 → **0.35 m** | **Apply with 3.** On its own, ledges of 0.4–2 m become walk-into (the 2 m centre probe misses them). | `kStepUp` 0.35 (UE3 `Pawn.MaxStepHeight` default 35 UU). The step was counted twice before. |
| `gameplay-3-wall-probe-slide.patch` (needs 1+2) | 〃 | `collision.no_low_obstacle_penetration`, `collision.wall_slide_speed` 0.26 → **3.64 m/s** (UE3 ≈3.89) | 0 | Probes at 0.37, 2.0 and 3.9 m; surfaces with `normal.y ≥ 0.7` (UE3 `WalkableFloorZ`) are left to ground resolution, so 20°/35° ramps still climb and 60° still blocks. The into-wall velocity is removed and the tangential part kept. Adds `CollisionWorld::segmentHit(…, outNormal)` (the old overload is kept, so `World::fireHitscan` is untouched). |
| `systems-1-fire-interval-remainder.patch` | agents/systems 3160a66 (LF; applies to LF and CRLF checkouts) | `weapon.fire_interval_effective` 0.0667 → **0.0653 s** (900 → 919 RPM; tends to 923 over long bursts) | 0 | Carries the sub-step refire remainder like a UE3 looping timer, bounded to one step after a pause. Patch content is LF like the branch blob. |

Gameplay 1+2+3 together: 109 → 114 pass, 7 → 2 known, 0 FAIL. On the real Streets map
(`--map` 8-direction sweep from the FFA spawn) the behaviour is unchanged: 137.6 vs 138.4 m
travelled, no fall-through, step cost 0.016 → 0.044 ms. The spawn area is flat, so the map run
mainly exercises walls; ledges and steps on real geometry still need an in-game look.

Reproduce:
```powershell
$p = 'tools\fidelity\proposals'
.\tools\fidelity\ab.ps1 -Ref agents/gameplay -Name gameplay
.\tools\fidelity\ab.ps1 -Ref agents/gameplay -Patch "$p\gameplay-1-jump-integration.patch","$p\gameplay-2-step-height.patch","$p\gameplay-3-wall-probe-slide.patch" -Name gp-all3 -HarnessArgs '--map'
.\tools\fidelity\diff-reports.ps1 work\ab\gameplay\report.json work\ab\gp-all3\report.json
```
If an owner branch moves on and a patch no longer applies, `ab.ps1` stops with "patch did not
apply"; regenerate it against the new head.
