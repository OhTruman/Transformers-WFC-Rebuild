# Robot landing while rising: the previous-bottom rule (barrier-top pops)

Gameplay, 2026-10-16. The patch is `docs/handoff/GAMEPLAY_RISE_SNAP.patch` (`CharacterMovement.cpp`, one condition), against the walkable-floor head. It applies on integration aafc018b with the file byte-identical to the tested tree.

It changes players and both AIs (robot form). Classic is re-baselined below.

## The bug

**What the rebuild did:** an airborne robot moving **up** was "landed" on any walkable surface within MaxStepHeight above its feet, because the snap searched from the current bottom up to bottom + 0.35 m.

**The evidence:** a POCKETENTRY replay of BOTTEST seed 11. Bot p4 was blocked inside a fresh barrier's footprint and stuck-jumped. At t 50.50, still rising at 14.7 m/s, it was snapped up 0.84 m and marked on the ground. A second jump did the same, and it ended on the barrier's top (off the mesh), where its Smart squad member idled underneath.

**The original:** UE3 physFalling lands only on a walkable hit while moving down; a surface met while rising is a ceiling.

**Standing on a barrier is itself faithful:** `TnBarrierSpawnable` has `bWorldGeometry=True` and a PHYSSYS box collision, so its flat top is a valid base. What was wrong is reaching the top by rising through the box.

## The fix

While rising (airborne, v.y > 0), a surface is landed on only if the pawn's **previous bottom was already above it**. So:
- a pawn rising through geometry no longer pops onto it;
- a jump whose top sits near the apex still lands, because once the pawn's bottom has passed the ledge top, the ledge counts.

## Checks (against the walkable-floor head)

| Check | Result |
|---|---|
| NAVQ, 24 seeds × 4 maps | Every column within noise for both AIs (\|z\| < 1.5) |
| MAPTRAVERSE robot tours, 4 maps | ARRIVED / BLOCKED counts identical (Streets 99 / 23, Molten 87 / 32, Debris 53 / 19, Gorge 133 / 13) |
| BOTNAV / DOUBLEJUMP / BARRIERWALK / PARTICIPANT / XFORM / HEIGHT / DETERMINISM, 4 maps | Same PASS / FAIL lines |
| Double jump | Still peaks at 5.14 m (single) / 9.77 m (double) |
| BOTTEST, 12 seeds, Systems' env | 48 / 48 on 11; seed 3 failed only the AI-cost timing row (one 13.4 ms max spike) |

**The AI-cost row is flaky:** it has now failed with a single max spike over 6 ms (7.2-13.4 ms) in four separate runs on different builds, while its average stays under the bar. It times wall-clock steps on a shared machine. Suggested follow-up: gate on the average, or on the max over a longer window, rather than one step's max.

## Classic byte-exact reference: re-baselined on this commit

The old column is the walkable-floor reference (`docs/handoff/walkable`).

- **78 of 96 runs diverge.** The first divergent step is between 1,515 and 17,479 (median 6,944): a run changes only when a robot's jump rises past a surface within MaxStepHeight.
- **Files:** `docs/handoff/risesnap/CLASSIC_SIMHASH_old_vs_new.txt` and `CLASSIC_NAVQ_REFERENCE_new.txt`.
