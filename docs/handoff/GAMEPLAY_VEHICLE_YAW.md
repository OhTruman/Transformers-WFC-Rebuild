# Eye-pass items 2 and 3: bot vehicle heading at the authored AI rate; walking-leader facing eased

Gameplay, 2026-10-16. The patch is `docs/handoff/GAMEPLAY_VEHICLE_YAW.patch`, against integration f7b7828d, and changes `BotBrain.h`, `BotSmart.h` and `WorldBots.cpp`.

## Item 2 (Smart and Classic): AI hover vehicles turn at AiMaxAngularSpeed

**Original (CONF script):** an AI-driven hover vehicle runs `TnCarForm` / `TnTankForm.Hovering.DoUpdateAi`, which calls `UpdateFromLocationAndRotation`, which calls `HoverPhysics.RotateToTargetOrientation` toward the DesiredRotation (the focus or move direction):
- desired w = ClampLength(delta / dt, min(sqrt(AiMaxAngularAcceleration · |delta|), AiMaxAngularSpeed));
- w += ClampLength((desired − w) / dt, AiMaxAngularAcceleration) · dt.

**Authored values per vehicle form** (the data the tip-over righting already uses):

| Form | AiMaxAngularSpeed (rad/s) | AiMaxAngularAcceleration (rad/s²) |
|---|---|---|
| Car | 12 | 12 |
| Truck | 20 | 20 |
| Tank | 6 | 6 |

At 60 Hz that is at most about 6-19° per step.

**Rebuild before:**
- `botSteer` set `in.faceYaw` to the move heading, or to the aim yaw while fighting.
- The hover simulation then took the facing yaw in one step.
- So bot vehicles flicked 25-90° (a few over 150°) at path corners: `GAMEPLAY_EYEPASS_AUDIT.md` item 2, 1,016 events in 15 matches.
- This was a reconstruction error, not Classic behaviour (Integration 2026-10-16), so **both AIs change** and the Classic byte-exact reference is re-baselined on this commit (below).

**Patch:** for a bot in hover form, the facing turns toward the same desired yaw through that law, carrying the AI yaw rate between steps (`BotBrain::vehYawRate`). Excluded:
- **Driving:** DoUpdateAi is a no-op there, and the steering still reads the full heading error.
- **Jets:** they keep their flight path; the plane AI is not ported.

## Item 3 (Smart only): the waiting leader's look-around is eased

- **Before:** Smart's waiting squad leader looked around with `faceYaw + 0.9 · sin(0.8 t)`, and the offset (up to 51.6°) was dropped in one step when the wait ended or a fight interrupted it. That was audit item 3: 133 snaps of 25-52° at the 2.0 m/s squad-slow pace.
- **Patch:** the offset (`SmartBot::lookOffset`) eases toward its target at 1.5 rad/s, and back to 0 after the wait. The stick is re-projected, so the walk direction is unchanged.

## Measurements

**Eye-pass audit re-run** (the same 15 windowed matches: 5 user maps × seeds 7 / 1 / 3, `WFC_EYEBOTAUDIT`):

| Snap kind | Before | After |
|---|---|---|
| Vehicle heading snaps over 25° in one step | 1,016 | **173** |
| Walking robot snaps | 133 | **0** |

- **The remaining vehicle snaps** are mostly at 20-30 m/s, consistent with jets and Driving (both excluded on purpose). They are not traced per event.
- **Other counts:**
  - deaths 340;
  - pops 1 (was 5);
  - wall pushes 29 (was 22);
  - stuck vehicles 15 (was 11);
  - airborne 21 (was 12; all Hover-ability bots, item 7).

  These are trajectory changes within the same order.

**NAVQ, 24 seeds × 4 maps, z-gated** (`work/pass29/navq_cmp.js`; base = f7b7828d, which reproduces the 7d9edf78 lines):

| Map | Classic: largest z | Smart slow % | Smart off-mesh % | Smart stucks |
|---|---|---|---|---|
| Streets | 0.4 | 6.19 → 4.59 (z −1.9) | 5.42 → 4.52 (−1.0) | 1.70 → 1.32 (−1.6) |
| Molten | −1.2 (rev) | 1.79 → 1.96 (0.6) | 1.96 → 1.84 (−0.4) | 0.60 → 0.74 (1.4) |
| Debris | −1.5 (idle) | **3.48 → 4.77 (z 2.7)** | **1.99 → 2.99 (z 2.2)** | 0.96 → 1.25 (1.7) |
| Gorge | 0.7 (rev) | 5.52 → 4.45 (−1.5) | 5.81 → 4.84 (−1.1) | 1.57 → 1.32 (−1.2) |

- **Classic:** no column is beyond noise on any map.
- **Smart:** better on Streets and Gorge, but **worse on Debris** (slow and off-mesh at z 2.7 / 2.2).
  - With the slower turn, Debris' narrow catwalk corners plausibly see more overshoot; not traced.
  - Debris also moved the wrong way under several unrelated Smart arms earlier (`GAMEPLAY_SMART_ROUND_7d9edf78.md`).
  - Integration's fidelity rule ships the authored turn rate; the Debris cost is the open item.

## Classic byte-exact reference: re-baselined on this commit

`WFC_SIMHASH=0`, Classic, 24 seeds × 4 maps (WFC_NAVQTEST env), old (f7b7828d) vs new (this patch):

- **Every run diverges:** 96 / 96, as expected for an intended Classic change.
- **Where:** the first divergent step is 602-614, which is when bots first drive in hover form.
- **Per-run table:** `docs/handoff/vehyaw/CLASSIC_SIMHASH_old_vs_new.txt` (final step and hash, old and new, first divergent step).
- **The new Classic byte-exact reference** for later rounds is the NAVQ SUMMARY lines in `docs/handoff/vehyaw/CLASSIC_NAVQ_REFERENCE_new.txt` (96 runs) and the new final hashes in the table.
- **Smart unchanged:** Classic lines must reproduce these exactly from this commit on.

## Next Smart item (Integration 2026-10-16): vehicle corner anticipation

The faithful turn limit stays as it is. With it, Smart's vehicles lose pace on Debris (slow +1.3, off-mesh +1.0). The next Smart item is corner anticipation, so limited vehicles keep their pace without softening the limit:
- **Start the turn early:** look at the waypoint after the next one once the current corner is within the turn radius v / AiMaxAngularSpeed.
- **Smooth the path corners:** the facing target blends between the two segment headings.
- **Brake before corners** the chassis cannot take at speed: scale the stick so that v ≤ AiMaxAngularSpeed × the distance to the corner apex.
- **Gate:** 24 seeds, z-scored. Debris slow / off-mesh back within noise of the f7b7828d base, and no map worse.
