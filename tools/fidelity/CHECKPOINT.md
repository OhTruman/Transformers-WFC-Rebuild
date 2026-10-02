# Experimental / Fidelity — integration checkpoint (2026-10-01)

> **Superseded in part by native RE evidence:** see [RE-EXPECTATIONS.md](RE-EXPECTATIONS.md).
> - Patch 4 is withdrawn.
> - Patches 1–3 no longer apply to Gameplay Pass 12. Their intent (trapezoidal jump, 37 UU step,
>   projected wall slide) is now CONFIRMED; see [proposals/README.md](proposals/README.md).

Branch `agents/experimental`. Tooling and findings only: **no product code in this branch was
changed, and nothing was applied to Gameplay or Systems.** Validated against owner heads
**agents/gameplay 8045676 (Pass 9)** and **agents/systems 3160a66**. Neither head has moved since.

## Harness status
- `.\build.ps1 -Jobs 2` builds `wfc_rebuild.exe` and `wfc_fidelity.exe` with no warnings.
- `wfc_fidelity.exe` on this branch (= main product code):
  **100 pass, 0 FAIL, 16 known, 51 info, 1 skip**, exit 0.
- `--no-assets`: 77 / 0 / 8 known / 25 info / 10 skip, exit 0.
- Owner branches (`ab.ps1`):

  | Run | pass | FAIL | known |
  |---|---|---|---|
  | gameplay | 109 | 0 | 7 |
  | gameplay + patches 1–3 | 114 | 0 | 2 |
  | systems | 101 | 0 | 15 |
  | systems + patch 4 | 102 | 0 | 14 |

## The four owner patches (`tools/fidelity/proposals/`)
Apply in the owner's own worktree with `git am <patch>`, then run `.\build\bin\wfc_fidelity.exe`.
The patches are LF and apply cleanly to both LF and CRLF checkouts.

### 1. `gameplay-1-jump-integration.patch`
- **Owner:** Gameplay.
- **Subsystem:** movement, vertical integration.
- **Bug:** the jump apex is 6.41 m against MaxJumpHeight 625 UU = 6.25 m, and it depends on tick
  rate (6.57 at 30 Hz, 6.33 at 120 Hz). Position is integrated with the post-gravity velocity
  (explicit Euler). The vehicle jump has the same cause: 3.81 vs 3.71 m.
- **Evidence:**
  - `movement.robot_jump_apex` KNOWN 6.41 → PASS 6.2498.
  - `movement.vehicle_jump_apex` KNOWN 3.812 → PASS 3.711.
  - With the patch, 30/60/120 Hz give 6.247/6.250/6.250. Airtime 1.317 s (analytic 1.304).
- **Direction:** apply the jump impulse before gravity, then `p.y += 0.5*(vOld+vNew)*dt`. This is
  UE3 physFalling's averaged velocity, which is exact for constant gravity.
- **Files:** `src/game/CharacterMovement.cpp`, `CharacterMovement::update` (gravity/jump block).
- **Overlap:** none. Systems does not touch `CharacterMovement.cpp`.

### 2. `gameplay-2-step-height.patch` (apply with 3)
- **Owner:** Gameplay.
- **Subsystem:** ground resolution and step-up.
- **Bug:** the robot steps onto ~1.1 m ledges. `kStepUp` is 0.6, and it is added twice: once to
  the search top and once to the `groundHeight` window.
- **Evidence:** `collision.max_step_height` KNOWN 1.10 → PASS 0.35. The per-height sweep is in
  the note on that check.
- **Direction:** `kStepUp = 0.35` (UE3 `Pawn.MaxStepHeight` default 35 UU; no TnPawn override
  found), counted once (`groundHeight(x, z, base - hover, kStepUp + hover)`).
- **Files:** `src/game/CharacterMovement.cpp` (`kStepUp`, ground-resolution block).
- **Overlap:** none.
- **Must ship with #3.** On its own, ledges of 0.4–2 m become walk-into, because the only wall
  probe is at 2 m.

### 3. `gameplay-3-wall-probe-slide.patch` (needs 1 and 2)
- **Owner:** Gameplay.
- **Subsystem:** horizontal collision.
- **Bugs:**
  - One wall-probe ray at capsule centre (2 m), so ledges above step height and below 2 m are
    walked into.
  - Any probe hit zeroes all horizontal velocity, so there is no wall sliding.
- **Evidence:**
  - `collision.no_low_obstacle_penetration` KNOWN → PASS.
  - `collision.wall_slide_speed` KNOWN 0.26 → PASS 3.64 m/s (UE3 SlideAlongSurface ≈3.89).
  - Ramps 20°/35° still climb and 60° still blocks (`collision.ramp_*`).
  - Streets `--map` sweep is unchanged: 137.6 vs 138.4 m, no fall-through.
- **Direction:**
  - Probe at 0.37 / 2.0 / 3.9 m.
  - Ignore walkable normals (`|n.y| ≥ 0.7`, UE3 `WalkableFloorZ`).
  - On a hit, remove the into-wall velocity and keep the tangential part (2 iterations, then
    stop at corners).
- **Files:**
  - `src/game/CharacterMovement.cpp` (horizontal block).
  - `src/game/Collision.h/.cpp`: new overload `segmentHit(a, b, outT, outNormal)`. The old
    overload is kept, so `World::fireHitscan` is unchanged.
- **Overlap:** none. Neither branch touches `Collision.*`.

### 4. ~~`systems-1-fire-interval-remainder.patch`~~ — WITHDRAWN (native RE, 2026-10-01)
The original's refire timer is non-looping, uses a strict `>` and discards overshoot, giving
0.0667 s ≈ 900 RPM at 30/60 Hz (RE TARGETED_PASS #6). The rebuild already matches it, and the
patch file is deleted. The original text is kept below for history only.

- **Owner:** Systems.
- **Subsystem:** weapon refire timing.
- **Bug:** the Ion Blaster fires at 900 RPM against the authored FireInterval 0.065 s (923 RPM).
  `Weapon::onFired` sets `cooldown = fireInterval` and drops the sub-step remainder, so at 60 Hz
  every shot waits 4 steps. A 50-round mag takes 3.27 s instead of 3.19 s.
- **Evidence:** `weapon.fire_interval_effective` KNOWN 0.0667 → PASS 0.0653 s; RPM 900 → 918.75
  (tends to 923 over long bursts).
- **Direction:** `cooldown = (cooldown < 0 ? cooldown : 0) + fireInterval` (the carry of a UE3
  looping refire timer, bounded to one step after a pause).
- **Files:** `src/game/Weapon.h`, `Weapon::onFired`.
- **Overlap:** none. Gameplay does not touch `Weapon.h`. Confidence MED that UE3 carries the
  remainder; an original fire-rate capture settles it.

## Duplicated Gameplay/Systems work — do NOT merge twice
Both branches independently implemented the same animation and weapon-feel features in
different ways:

| Feature | Gameplay (Pass 7–9) | Systems (501ec10…3160a66) |
|---|---|---|
| Upper-body layer + reload slot | `LocalPose` base/snap/final poses, `RobotRig.upperMask`, `reloadWeight()` | `LocalPose` base/over poses, `upperMask_`, `updateUpperBody`, `upperAnimName()/upperWeight()` |
| Upper-body aim offset | `TnAnimNodeAimOffset` "Default" profile baked into `RobotRig` (`Character.cpp`) | new `src/game/AimOffset.h` + `Character::evalLayered` |
| Aim-pitch input | `PlayerController::applyToPawn` → `pawn_->setAimPitch(camPitch_)` | `World::tick` → `pawn().setAimPitch(controller().camPitch())` |
| Weapon skeletal recoil | `src/game/Recoil.h` (`RecoilDef`, `ionBlasterSpineRecoil()`), triggered by `pawn_->notifyFired()` in `PlayerController` | `src/game/Recoil.h` (`RecoilControl recoilSpine_, recoilRHand_`), triggered via `Weapon::shotSerial` |

- **Textual conflicts** (`git merge-tree` preview):
  - `Character.cpp/.h`
  - `SkinnedModel.cpp`
  - `Recoil.h` (add/add)
  - `FIDELITY.md`, `STATUS.md`
- **Silent semantic duplicates** that merge without any conflict marker, because each side lives
  in a different file:
  - **two `setAimPitch` call sites** (`PlayerController.cpp` and `World.cpp`)
  - **two recoil triggers**: `notifyFired()` in `PlayerController.cpp` and the `shotSerial` watch
    in `World`/`Character`. If both survive, **recoil fires twice per shot**.
  - `src/game/AimOffset.h` (Systems, new file) beside Gameplay's in-`Character.cpp` aim offset.
- **Recommendation:**
  - Keep **Gameplay's** layering, aim offset and recoil: locomotion, turn-in-place, transform and
    vehicle hover depend on that framework.
  - Port onto it only what is unique to Systems: `WeaponMesh.*` owner animations, `WeaponFx.*`
    particle FX, the SoundCue playback, and `Weapon::shotSerial`/`reloadSerial` if WeaponFx
    needs them.
  - Drop Systems' `Recoil.h`, `AimOffset.h`, `updateUpperBody`/`evalLayered` and its
    `setAimPitch` call in `World.cpp`.
- **Merge gate:**
  ```powershell
  .\tools\fidelity\ab.ps1 -Ref <merge-commit> -Name merged
  .\tools\fidelity\diff-reports.ps1 work\ab\gp9\report.json work\ab\merged\report.json
  ```
  Expect 0 REGRESSED. Add a recoil-count check if both triggers are still present.

## Known differences remaining (on main / this branch)
On **agents/gameplay**, Pass 7–9 already fixes:
- `constants.mesh_yaw_offset`, the `muzzle.mesh_facing_vs_yaw_*` checks,
  `muzzle.vehicle_long_axis_vs_yaw` and `muzzle.muzzle_in_front_of_body`
- `transform.to_vehicle_incoming_clip_paired` and `transform.to_vehicle_total_time`
- `orientation.vehicle_max_yaw_rate`
- `animation.reload_on_the_move` and `animation.aim_pitch_follows_camera`

Fixed by patches 1–4:
- `movement.robot_jump_apex`, `movement.vehicle_jump_apex`
- `collision.max_step_height`, `collision.no_low_obstacle_penetration`,
  `collision.wall_slide_speed`
- `weapon.fire_interval_effective`

Still open after both:
- **`transform.to_robot_total_time`**: ~1 swallowed step (`playClip` resets animTime on the
  first transform step). Minor; within tolerance on most refs.
- **Suspected, needing an original capture** (INFO, not enforced):
  - vehicle dash is sustained, not a 0.3 s burst
  - releasing Sprint snaps 50 → 15 m/s in one step
  - velocity is zeroed when transforming on the move
  - robot braking model
  - vehicle-form weapon absent
  - aim-offset gain 0.70

## Original-game measurements still needed
`reference/original_measurements.json` has 28 slots, none filled yet. Priority:
1. robot stop time/distance (braking model)
2. vehicle boost curve (burst vs sustained, release decay)
3. transform total times (to vehicle / to robot) and momentum when transforming on the move
4. Ion Blaster RPM / mag-dump time
5. max step height and wall-slide speed
6. camera distance and shoulder offset (`pawn_screen_x`)
7. jump apex and airtime
8. aim-offset gain (barrel vs camera pitch)

## Next Experimental task (proposed, not started)
After the owners apply patches 1–4 and the integrator resolves the duplication:
1. Run the merge gate on the merged commit.
2. Add a **recoil-trigger count** check (exactly one recoil impulse per shot), so the
   double-trigger cannot slip through.
3. Check steps and ledges on the real Streets geometry with targeted `--map` probes beyond the
   flat spawn.
