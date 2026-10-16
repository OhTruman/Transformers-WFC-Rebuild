# BOTTEST gate on 56a9beca: seed-robust rows and the unpresented bot deaths

Gameplay, 2026-10-16 09:47. The patch is `docs/handoff/GAMEPLAY_BOTTEST_GATE.patch`, against integration 56a9beca. It changes `Application.cpp` (two BOTTEST rows), `WorldBots.cpp` and `World.h` (presentation only).

## Systems' 45 / 46: reproduced; it was seed variance

**The report:** Systems' identity run (`WFC_SEED=7`, `WFC_FLOWSEED=7`, default threads, Smart, headless) read "bots toss grenades (2; melee strikes 0)". A clean 56a9beca export reproduces it exactly.

**It is not a regression.** A BOTTEST sweep over seeds 1-12 in Systems' env, on 56a9beca, pre-yaw f7b7828d and the steer-sign tree, shows four rows failing by seed on every build:

| Row | Failing runs (of 36) | Cause |
|---|---|---|
| bots toss grenades (phase 2 ≥ 3) | 2: 56a9beca seed 7 (2), f7b7828d seed 5 (1) | Rate check: a phase-2 match ranges 1-16 grenades by seed |
| bots trigger killstreak rewards (phase 2 > 0) | 3 | Rate check: 0-7 rewards by seed |
| DEATH every death presented | 3: one death short each time | **Real gap:** unpresented bot deaths (below) |
| no bot idle / stuck for 20 s | 4 (20.0-20.6 s) | Behaviour: eye-pass item 4 (stuck), queued |

## Fixes

**1. The grenade and killstreak rows count over the phases.** They prove the behaviour exists, not a rate:
- grenades ≥ 2 summed over phases 1 + 2 (the worst run in the sweep sums to 2);
- killstreak rewards ≥ 1 summed.

**2. Bot deaths that despawn at once are now presented.** Two paths called `match_.killed` and then despawned the bot in the same step:
- the stranded-30-s suicide;
- the KillZ fall (FellOutOfWorld).

tickMatch's PlayerKilled event presents only a still-spawned pawn, so these deaths got no death body, light or sound. Most suicides are self-kills through `applyMatchDamage`, which presents before the despawn, so only some phases missed one. UE3 runs `Pawn.Died` → `PlayDying` for both, so both are now presented the same way (`World::presentBotDeathNow`). This is presentation only.

## Checks (56a9beca + this patch)

- **BOTTEST:** 46 / 46 on all 12 seeds in Systems' env, including seed 7 (grenades 2 over the phases) and seed 2 (17 presentations for 17 deaths).
- **SIMHASH:** seed 7's 18,000 steps are identical to Systems' 56a9beca base log (`Rebuild-Systems/work/off_identity/simhash_base/wfc.log`).
- **NAVQ:** 8 / 8 SUMMARY lines identical to the drive-probe tree (Smart, seeds 1 and 7, 4 maps).
- **Classic byte-exact reference:** unchanged. No re-baseline.

**Left open:** the idle / stuck 20 s row still fails on a few seeds (4 of 36 in the sweep; none of the 12 here). That is eye-pass item 4, next in my queue.
