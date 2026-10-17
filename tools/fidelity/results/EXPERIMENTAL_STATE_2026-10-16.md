# Experimental lane - state at 2026-10-16 19:31 (resume note before the GPU curfew / morning reboot)

## Done today
- Package 6 gate on 4078e47b and the respin 9350f19b (pre-volumetric checkpoint): results/PACKAGE-GATE_9350f19b.md.
  FAST PASS x3, leak PASS, eye pass 1.28 / 1.30, Realistic + HD 2 105-137 fps (p50); RT-off regress REPORT, attributed to the
  death bodies (results/BISECT_corpse_501.txt: NOCORPSE = base; 5v5 +0.06 ms).
- Gate base = work/ab/fast_398b27e_rise (398b27ef + yaw 022e5e34 + drive-probe 56a9beca + steer one-liner + walkable f19e0052@src/game
  + rise-snap 63368d05@CharacterMovement.cpp). Sim frozen for package 6.

## Next
- Package 6 is held for Rendering's froxel volumetrics (user 2026-10-16, ~2-3 days). Gate each candidate:
  eyepass.sh (EYE_EXE=<their exe>, EYE_RENDER_DATA=Rebuild/work/render; contrast floors 1.28 / 1.30 print OK / BELOW FLOOR),
  then package-respin.sh <candidate> 9350f19b (regress + delta FAST + report).
- GPU curfew: no exes 00:00 until the user's morning reboot (non-paged pool ~6.9 GB at 19:30, +0.43 GB/h).
- After the reboot: restart the TDR ledger (work/sweep/tdrwatch.ps1 -Minutes 720 -Out work/sweep/tdrledger_day6.csv).
- Watchdog busy signal DONE (908f9ee2, lib/Run.ps1). X3 candidate gate: tools/fidelity/x3-gate.sh <name> <exe> [CAND_SHA=...]
  (WFC_X3=0 = off; keys WFC_X3_DENSITY / HAZE / G).
- A reboot may come any time tonight (user, no warning); after a relaunch the pool is clean and the 00:00 curfew lifts.

## Trees in work/ab
fast_398b27e_rise (gate base), fast_7cd2202, fast_4078e47, fast_9350f19, _third_party; int_* / m9c_int_* = read-only junctions to
other lanes' builds (remove with rmdir only).
