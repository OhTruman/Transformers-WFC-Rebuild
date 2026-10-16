# Robot ground support: authored WalkableFloorZ 0.7 (robots climbed steep faces into traps)

Gameplay, 2026-10-16. The patch is `docs/handoff/GAMEPLAY_WALKABLE_FLOOR.patch`, against integration 01f2316a.

**Files:**
- `Collision.h` / `.cpp`: `groundHeight` and `dynamicGround` take an optional minimum |n.y|, default 0, so other callers are unchanged;
- `CharacterMovement.cpp`: the robot ground snap uses it;
- `Application.cpp`: the BOTTEST idle row skips squad waits.

It changes players and both AIs (robot form). Classic is re-baselined below.

## The bug (eye-pass item 4, the BOTTEST idle-20 s row)

**The authored limit:** WalkableFloorZ is 0.7 on `Engine.Default__Pawn`, `TnRobotForm` and `TnVehicleForm` (authored.db), and `BotNav` is built with it.

**The rebuild:** the robot ground snap took the highest triangle under (x, z) within MaxStepHeight, **whatever its normal**. A robot pushing into a steep face climbed it one step-up at a time.

**The evidence:** a POCKETENTRY ring-buffer replay of BOTTEST seed 1 (Systems' env).
- At t 73.55-73.88 bot p4 runs at about 14 m/s on the floor and climbs y −721.9 → −720.1 over about 0.6 m horizontally.
- It wedges at (121.4, −719.78, −538.5), off the mesh, under an overhang with only 2.28 m clear.
- Its stuck-jump then puts it into a pocket at (120.9, −716, −538.4), whose "ground" is a steep face (normal (−0.91, −0.33, 0.24)).
- A bot placed there stayed put for 20 s, and its Smart squad member slotted on it idled underneath.

**The fix:** only faces with **|n.y| ≥ 0.7** carry a robot. It has to be |n.y| because the collision winding is unreliable. Of the faces under nav-cell centroids, 26 % (Streets), 21 % (Molten), 9 % (Debris) and 37 % (Gorge) have downward normals, so a signed test would drop pawns through real floors.

## Player-facing change (faithful)

**Surfaces steeper than about 45° no longer carry a robot.** That matches the original (WalkableFloorZ 0.7). Robots meeting such a face slide or stop at it instead of walking up it.

**MAPTRAVERSE**, the robot tours between authored points on 4 maps: 2 of about 460 routes changed, both ARRIVED → BLOCKED.

| Map | Route | Old | New |
|---|---|---|---|
| Streets | 15, TnHealthPickupFactory_7385 → TnFlagCapturePoint_5353 | climbed to y −700.7 | blocked |
| Debris | 55, TnHealthPickupFactory_7431 → TnFreeForAllPlayerStart_3869 | arrived | blocked |

- Both used a face steeper than WalkableFloorZ.
- No route flipped the other way.
- No vehicle route changed; vehicles don't use this snap.
- No spawn changed: TRAVERSE, the robot walk from every start, shows only small y-range differences.

## Not in this patch (open)

- **Barrier-top pops.** A rising jump is still landed on any surface within MaxStepHeight above the feet: seed 11's pawn rose through a barrier's footprint onto its top. The rework with the previous-bottom rule (land while rising only on a surface the previous bottom was already above) comes next, on its own gate.
- **An underside test** (a floor needs 2.5 m of free space above it; nav floors have ≥ 3.94 m) was tried and dropped. It failed the gate (Classic Molten stucks z 3.0, Smart Gorge off-mesh z 2.8): off-nav geometry with low ceilings is crossed legitimately.
- **The third Streets pocket** at (125.9, −721.0, −439.9): its "ground" has normal (0.41, −0.91, 0) with 6 m clear above, so neither test catches it. It's a separate nav / collision item, queued.

## BOTTEST row: squad waits

"No bot idle / stuck out of combat for 20 s" now skips a Smart squad member that holds its squad goal while its leader moves: it is waiting at the shared trip destination, which is by design. That was seed 5's failure: the member reached the destination about 20 s before its leader. Test row only.

## Checks

| Check | Result |
|---|---|
| Both traps (`WFC_STUCKSPOT_BOT`) | Escaped: 161 m and 83 m from the spot in 20 s (before: 0.2 / 0.5 m) |
| NAVQ, 24 seeds × 4 maps, vs the steer head | No map worse for either AI; largest z 1.86 (Classic Streets off-mesh); Smart within ±1.8 |
| BOTTEST, 12 seeds, Systems' env | See below |
| BOTNAV / DOUBLEJUMP / BARRIERWALK / PARTICIPANT / XFORM / HEIGHT / DETERMINISM, 4 maps | Unchanged, all passing as before |

**BOTTEST, head tree (01f2316a + this patch, row change included):** 48 / 48 on 11 of 12 seeds; seed 1 failed only the AI-cost timing row (one 7.8 ms max spike under CPU load). The head tree reproduces the gated runs exactly (NAVQ 16 / 16, Classic SIMHASH 8 / 8).

**BOTTEST before the row change:** 48 / 48 on 9 seeds.
- Seed 5: the squad wait, fixed by the row change above.
- Seed 12: a vehicle next to the third Streets pocket, queued.
- Seed 9: the AI-cost timing row, under CPU load from a parallel build.

## Classic byte-exact reference: re-baselined on this commit

`WFC_SIMHASH=0`, Classic, 24 seeds × 4 maps. The old column is the steer-sign reference (`docs/handoff/drivesteer`); the new column is this patch.

- **94 of 96 runs diverge.** The first divergent step is between 929 and 15,412 (median 3,347).
- **Files:** `docs/handoff/walkable/CLASSIC_SIMHASH_old_vs_new.txt` and `CLASSIC_NAVQ_REFERENCE_new.txt`.
