# Bot behaviour eye-pass audit: the 5 user maps

Gameplay, 2026-10-16 06:20. Integration head 06110ef4, RT off. Candidates for the package 6 eye pass: things a player would SEE as wrong in bot behaviour. Each comes with a seed and a step (frame) so it can be staged. No code is changed here.

## How it was found

**Run env:** Systems' eye-pass env (`tools/systems/eyepass_run.ps1`), 120 s per match:
- Windowed lockstep, TDM 4 + 5 (`BotsFriendly=4?BotsEnemy=5?PointsToWin=1000`), `WFC_PLAYERBOT=1`.
- Muted, NOINTERACT, `WFC_RT_MODE=0`, `WFC_SEED` = `WFC_FLOWSEED`.

**Coverage:** Streets, Rust, Molten, Gorge and Debris, seeds 7 (the eye-pass seed), 1 and 3. That is 15 matches.

**Detectors:** a diagnostic-only `WFC_EYEBOTAUDIT` hook (not in any patch; `docs/handoff/eyepass/edit_eyeaudit.js`, a node edit script applied to an archive root):
- It is read-only per sim step.
- Its step counter is the one WFC_SIMHASH uses (one per `finishRemainder`).
- Raw lines: `docs/handoff/eyepass/EYEBOT_lines.txt`.

**To stage an item:**
- Use the same env and seed. The shot frame is about **step / 2** in this windowed lockstep env (verified on the mine capture: step 1081 is frame 540).
- Example: `WFC_SHOTMATCH=<dir>,<frame-30>,<frame+30>,2`.

## Findings (most visible first)

| # | Severity | What a player sees | Count (15 matches) | Examples: map seed, step (≈ frame), player |
|---|---|---|---|---|
| 1 | HIGH | **Dead bots vanish on the death step.** No death animation, no body, no gib or wreck. | Every death: 338 | Streets s7 step 1328 (≈ 664) p1 (TruckRocket); Rust s7 step 1321 p1 (TankShell); Molten s7 step 1565 p7; Gorge s7 step 1215 p7 (CarMachineGun); Debris s7 step 1365 p9 (PlaneRocket) |
| 2 | HIGH | **Bot vehicles flick their heading at path corners.** Yaw jumps 25-90° (a few over 150°) in one 1/60 s step while driving with no target. | 780 with no target + 236 with a target out of sight. Logged at most once per second per bot, so the true count is higher. | Streets s7 step 980 (≈ 490) p9, 46° at 27.8 m/s; Debris s7 step 905 p5, 50°; Debris s7 step 832 p9, 49°; Gorge s7 step 874 p9, 33° |
| 3 | MEDIUM | **Walking bots snap their facing 25-52°** in one step at exactly 2.0 m/s with no target. | 133 | Gorge s7 step 1193 p5, 52°; Molten s7 step 1433 p5, 49°; Debris s7 step 1753 p2, 40°; Rust s7 step 5071 p2, 33° |
| 4 | MEDIUM | **A bot vehicle sits still with throttle on**, under 2 m/s for 2-5.7 s. 8 of 11 were on flat ground (normal y 1.00), so a block rather than a ramp; 3 were tilted or on a slope. | 11 episodes | Streets s7 step 1609 (≈ 805) p8 Tank2 at (301.5 -705.1 -346.8), 5.7 s, also step 1391 same spot; Streets s7 step 1274 p9 Truck4, 3.0 s; Debris s7 step 5176 p4 Truck3, tilt 20°, slope 0.94 |
| 5 | MEDIUM | **A robot jumps 3.2-3.5 m in a single step** (a visible pop). | 5 | Rust s3 step 6498 p6; Streets s3 step 3845 p7 and step 7290 p7; Streets s7 step 1408 p5 (vehicle); Debris s3 step 4594 p5 |
| 6 | LOW | **A robot keeps walking into a wall** for 1-2 s. 11 of the 22 are in one match, around one spot. | 22 episodes | Rust s1 step 2309 p9 at (-26.8 8.5 -184.5), 11 episodes; Gorge s7 step 4673 p1 at (-112.6 11.5 -125.2), 1.4 s; Gorge s3 step 3081 p3 |
| 7 | CHECK | **A robot stays off the ground 4 s or more** with vertical speed 0. Every case is a bot with the **Hover** ability, so this is probably legitimate hovering. Check the hover duration against the original (TnBuffHover / TnAbilityHover). | 13 | Rust s7 step 2425 p8 (Whirlwind/Hover); Streets s7 step 2617 p8 |

## Clean in these runs

- **Spinning in place** (over 270°/s for 1 s): 0.
- **Pawns inside each other** (over half interpenetrating for 1 s): 0.
- **Idle sentries** (an enemy within 30 m in plain sight, no target for 3 s): 0.

## Causes (from the code; Gameplay can fix 2, 3 and 6, while 1 is a feature)

1. **Deaths.**
   - **Rebuild:** `World::applyMatchDamage` calls `opp->despawn()` on the kill, and nothing draws the pawn after that.
   - **Original** (RE-Workspace `notes/TARGETED_PASS5_2026-10-05.md` §12 addenda 17-19): the TnDeathFactory / TnRobotDeathPicker picks a death entry. The pawn plays its animation (Death_HeadShot / Death_FrontShot / Death_BackShot / InAir / Death_Martyrdom ...) for TotalTime 1-2.5 s, then gibs (gibs + decals; vehicle hull gibs; Defrag energy-death material for energy kills).
   - **Pieces that exist already:** the renderer's `setDrawEnergyDeath` (M74), the death lights, and the death sounds.
   - **What it needs:** a death body (the posed pawn kept, presentation only) plus the picker and the gib. This is the largest item; it needs a design note before code.
2. **Vehicle heading flicks.**
   - **Rebuild:** `botSteer` sets `in.faceYaw = yawOf(moveDir)` for a vehicle with no visible target, and the hover sim takes the facing yaw that step.
   - **Original:** AI hover vehicles turn through HoverPhysics.RotateToTargetOrientation at AiMaxAngularSpeed / Acceleration (car 12 / 12, truck 20 / 20, tank 6 / 6 rad/s, rad/s²; the same data as the tip-over righting). That is at most about 11-19° per 60 Hz step.
   - **Fix:** rate-limit the bot vehicle yaw with the authored AI values. This could be Smart and Classic alike, but changing Classic needs Integration's call because Classic is byte-exact by rule.
3. **Robot facing snaps** (likely; not traced step by step).
   - Smart's waiting squad leader looks around with `faceYaw + 0.9 · sin(0.8 t)`, up to 0.9 rad = 51.6°, the observed maximum.
   - When the wait ends, the offset is dropped in one step.
   - The snapping bots move at the squad-slow walking pace (2.0 m/s).
   - **Fix:** ease the offset out, Smart only.
4. **Stuck vehicles** (not traced): the same Tank2 sat at the same Streets spot twice (steps 1391 and 1609).
5. **Pops** (not traced). Candidates: the off-mesh unwedge / rejoin slide, collision depenetration, or the barrier walk-out.
6. **Wall pushes** (not traced): Rust s1 has one hot spot (p9, 11 episodes).

## Not covered by these detectors

- Upper-body aim (pitch) snapping.
- Shots fired through walls.
- Foot sliding.
- Weapon held vs fired direction.
- Death camera.

The eye pass should still look at those by eye.
