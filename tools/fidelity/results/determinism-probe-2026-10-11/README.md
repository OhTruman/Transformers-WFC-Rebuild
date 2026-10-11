# Determinism probe: is a free-running match the same match at two frame rates? (UNTIMED, 2026-10-11)

Question (Integration, after the Gorge real play 4K spread): the seeds are fixed (sim RNG reseeded per match, WFC_FLOWSEED=1), so why
does each rep play a different route? Build 5aa2d22, Gorge 64p TDM, 60 s matches, Classic bots, per-step `WFC_SIMHASH`. Each arm ran
at RenderSize 1280x720 and 3840x2160 (different frame rates). Hash streams were aligned at `match: launched` (`hashcmp.py`).
These rows are not timing rows. Two of them overlapped another lane's exe (CONTAMINATED); that is irrelevant to determinism.

| arm | local player input | avg fps 720p / 4K (match 2) | identical prefix, match 1 / match 2 |
|---|---|---|---|
| A | capacity-stress overview inputs: auto walk / strafe + a jump every 150 RENDERED frames | 336 / 378 | 615 / 616 steps (spawn at 601: diverges ~15 steps after spawn) |
| B | none (WFC_AUTOWALK / AUTOSTRAFE / AUTOJUMP_EVERY empty) | 376 / 264 | **5103 / 5103: whole match identical** |
| P | real play: WFC_PLAYERBOT=1 | 573 / 298 | 1000 / 667 steps (diverges 1-7 s after spawn) |

## Finding

- The simulation itself is frame-rate independent. With no local input, a 64-bot match is bit-identical, step for step, over the whole
  85 s, at 376 vs 264 fps.
- Every divergence enters through the local player's input, which is sampled per RENDERED frame, not per sim step:
  - A: `WFC_AUTOJUMP_EVERY` counts rendered frames, so the jump lands on a different sim step. The divergence starts within 15 steps of
    the spawn. Since cf6d68b the FixedCam overview rows drop the jump.
  - P: the player bot is driven by `playerBotInput(realDt)` / `handleInput(realDt)` and takes the shared bot path search per rendered
    frame (WorldBots.cpp ~1148). A faster renderer gives it more decisions per second, the player's moves differ, and the 63 bots
    react. The whole match diverges within seconds.
- So the rep-to-rep spread of real-play rows is not a sim determinism bug. It is the per-frame input coupling of the player bot.
  Any change in frame time (another build, GPU state, background load) gives a different route. This is why two runs of one build
  differ by up to 0.27 ms p90 on Gorge real play 4K.

## Consequences

- No product change is proposed. Integration decided against route pinning: the user wants Gorge above 300 fps across real routes.
  The protocol is therefore 300 s x3 with each rep <= 3.33 plus the pooled p90 (`pooled-p90.ps1`).
- A sim-step-driven player bot (input generated inside the fixed step) would make real-play rows reproducible per seed. That is an
  option for Gameplay if a pinned-route A/B tool is ever wanted. It is not needed for the verdict protocol, and bot work is paused.
- Lockstep A/Bs (`lockstep-ab.ps1`, WFC_LOCKSTEP=1) stay the tool for build-vs-build comparisons: one step per frame removes this
  coupling, which is why they replay the same match.
