# Final combined gate - pre stage (gated sha c3c9c157; code dffcf31f; sim = 903b2ab7) - 2026-10-23 11:37-12:13

Headless, CPU only (no card), pool guard raised to 11 GB for these steps only (nonpaged pool 9.5-9.8 GB: the Malwarebytes EDR leak;
the timed gate waits for the reboot). Runner tools/fidelity/final-gate.sh; logs work/finalgate/903b2ab/.

## Builds
- **Base**: af4d0731 + Gameplay's FINAL_BASE_ITEMS 01-07 (903b2ab7 docs/handoff/FINAL_BASE_ITEMS; `git apply` in a git-initialised af4d0731
  export, each patch `--check`ed first; 129 paths) -> work/ab/fast_af4d073_final2 (Release, Opus, headless runner + ESC in the exe).
- **Head (sim)**: 903b2ab7 -> work/ab/fast_903b2ab. **Negative control**: af4d0731 + hover + the headless runner only.
- (dffcf31f = 903b2ab7 + a render-only probe fix, sim identical: built separately for the timed rows.)

## Same match (headless, lockstep 600-9600, seed 1234, PlayerBot)

| Map | Head steps | Head vs base | Negative control vs base |
|---|---|---|---|
| 508 Streets (TDM) | 9,000 | **True** | False |
| 506 Remnant (ESC) | 1,192 | **True** | False (boots no ESC: 0 steps) |
| 505 BrokenHope (ESC) | 1,998 | **True** | False (0 steps) |

The ESC runs end at the player's game over in both builds (as Gameplay's README). **The base plays the head's match: the gate is valid.**

## Stability set on the 903b2ab7 exe: PASS

| Row | Result |
|---|---|
| TDM 10 maps x 300 s x seeds 7 / 1234 | 20 / 20 exit 0 at 300 s, 0 ERROR lines |
| Escalation Remnant / BrokenHope x 2 seeds (ESCLOG) | weapon kills only (4 / 6, 3 / 5), 0 world kills, 0 errors; the lone player killed in wave 1 |
| Campaign A1_IAC x 2 seeds | 18,061 steps (300 s), 0 errors, no game over |
| Campaign D5_OME x 2 seeds | 0 errors; **ends at 40.3 s by design**: 07 starts the chapter at the first checkpoint, Omega grabs the PlayerBot player ('Omega status 8 LockingGrabOnPlayer at t 21.2'), the player dies -> 'campaign: game over (Player died) at t 40.3 -> InProgressOver' (identical both seeds) - the campaign ends on the player's death as designed; not a stability issue |
| CAMPAIGNTEST / KISMETTEST / VOICETEST | 24 / 24, 10 / 10, PASS |
| Net: five profiles (status 0), late join, late join + early pick, relay, leave (switch, 3 seats) | every hash identical, every exit 0 |

**Pre stage PASS.** Next: the timed gate after the reboot and Integration's PERF_HOLD grant (pool counter read first):
`FINAL_BASE=work/ab/fast_af4d073_final2 bash tools/fidelity/final-gate.sh dffcf31f gate` - 10 MP maps x {1440p, 1080p} x {base, head}.
