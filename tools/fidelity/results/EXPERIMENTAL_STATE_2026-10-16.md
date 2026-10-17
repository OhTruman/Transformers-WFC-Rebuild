# 2026-10-17 05:32 state
- Package 6 gate on af4d0731 DONE: results/PACKAGE-GATE_af4d0731.md (no blocking row; 1b = the attribution-hold head arm; X3 +0.40-0.60 ms, bodies at RT on +0.17-0.49).
- Leak series: results/NPP_LEAK_2026-10-17.txt - ~4 GB of the pool is Malwarebytes EDR (FlightRecorder.sys); the D3D12 presenter shows no AMD-tag growth. Fix = the user's decision (Integration surfaced it).
- OPEN: wfc_rebuild pid 19880 (arm B run 32) stuck in the kernel after an exit hang, unkillable; gone only at the reboot. Its leak-arm bash 14164 / lockstep-ab 31476 wait on it.
- NEXT after the reboot: package 7 gate design - per merged head identity + eye sheet as now; one combined timed regress / 1b / FAST on the package 7 head; write tonight's hold rules into package-respin.sh's header (no interpreters from any session during a hold, AssetTools' hold check, the 9.5 GB pool guard, clear-card wait before the first rep, rerun any CONTAMINATED map whole). Card held for all lanes until the reboot (Integration).
- TDR ledger day 6 running (work/sweep/tdrledger_day6.csv, from 05:0x).

# IN PROGRESS 2026-10-17 02:14: package 6 gate on af4d0731 (Integration GO 02:10; package 6 = af4d0731 as is, no Streets step)

- Running: RERUN_1B=1 EYE_FROM=x3_cand13 GATE_REGRESS_NOTE="death bodies: ..." bash work/package-gate/respin_af4d073.sh af4d0731 9350f19b (a copy of package-respin.sh 25e03f3f); log work/package-gate/af4d073/gate.log.
- If interrupted (reboot / pool guard exit 3): remove work/PERF_HOLD if present, then rerun with SKIP_BUILD=1 once the build log shows done (completed regress maps can be kept: the per-map outputs are in work/package-gate/af4d073/regress).
- Messages owed to Integration (rebuild-9c): regress TIMED DONE (hold removed); the final verdict with the report path (copy PACKAGE-GATE.md to results/PACKAGE-GATE_af4d0731.md).

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

## Evening (X3 volumetrics)
- cand10 (e224ebf2): FAIL on a grey veil (match 1.08 / 1.09); X3 cost +0.4-0.6 ms p50; FAST NO OBVIOUS BREAKAGE after the
  resources rule change (846eb860, FAIL > 6,500 MB; the mapswitch peak spread is ~400 MB: results/MAPSWITCH_MEM_AB_cand10.txt).
- cand12 (round 3): match 1.26 OK / 1.27 below by 0.01 (Streets), views 1.47 / 1.50 = checkpoint; results/X3_EYEPASS_cand12.md.
- GPU-skinned corpses (merged): -0.41 ms p90 on Berth, still +0.9 ms vs no bodies (package 7: fewer draws per settled body).
- Final head tomorrow: RERUN_1B=1 EYE_FROM=x3_<final cand> GATE_REGRESS_NOTE="..." bash tools/fidelity/package-respin.sh <final> 9350f19b
- cand13 (round 4) = the package 6 volumetrics (Integration 23:2x): match 1.2654 OK / 1.2748 vs 1.28 (miss 0.005, noted in the report),
  views 1.47 / 1.50 = checkpoint, no veil. Tomorrow: an optional Streets-only value step (eye pass, ~30 min) if ready within 40 min of the
  relaunch; then package-respin.sh on Integration's final sha with RERUN_1B=1 EYE_FROM=x3_cand13 (or the Streets step's sheet).
