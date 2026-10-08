# Playtest build 09c 798bb26 (tag playtest/09c-798bb26) - Experimental confirmation checks (2026-10-08 14:28-14:55)

| check | result |
|---|---|
| AVM1 end-of-match crash: WFC_GFX_FORCEGC=1 + GCCHECK=1, 64 p frontend real play, 2 x 60 s matches | **0 guard hits, 0 crashes**, 2 results screens (positive control on Frontend's unfixed11: 20 hits - see crashfix-91574bc) |
| 32 v 32 SIMHASH, seed 123, direct boot, lockstep, FLOWSEED, 60 s of match time | **3 / 3 identical** (serial ref vs 1 serial + 2 threaded): 4,201 per-step hashes and 3,247 BOTLOG lines equal; all 4 runs exit at frame 4202 |

Performance for this lineage: results/final-0afd806 (0afd806 + crash / PLAYERBOT / race fixes); 10 v 10 p90 2.25 ms, 32 v 32 p90 3.34 ms.
The 0c8e05d seed-123 waypoint divergence has not reproduced since (f7bae0d 8/8 at 8 v 8, 798bb26 3/3 at 32 v 32).
