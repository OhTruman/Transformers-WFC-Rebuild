# BOTTEST "AI cost per step": gate on the average and p99, report the max

Gameplay, 2026-10-16. The patch is `docs/handoff/GAMEPLAY_BOTTEST_AICOST.patch`, against integration 067e4187; it also applies on 355dc2b7. It changes `World.h`, `WorldBots.cpp` and `Application.cpp`. Test row only: no sim change.

**The problem:** the row timed wall-clock AI steps on a shared machine and failed on a single step's max (> 6 ms). Single 7-13 ms spikes failed 4 runs on different builds while the average passed.

**The change:**
- `World` keeps the per-step AI cost samples, reset with the timing, and gains `botMsQuantile(q)`.
- The row now gates on **average < 0.04 ms per bot and p99 < 6 ms**.
- The max is printed as a reported value.

**The check:** BOTTEST on 12 seeds (Systems' env), built from 067e4187 + this patch, reads **48 / 48 on all 12**. The phase-2 values are about avg 0.045-0.049 ms for 15 bots, p99 0.19-0.22 ms, max 0.3-0.9 ms.
