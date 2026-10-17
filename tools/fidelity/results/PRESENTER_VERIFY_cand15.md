# D3D12 presenter fix verification - Rendering int_cand15 (d2573ccc + rt2 package 7 set incl. 9015c4e9 + 8736608c), 2026-10-17 09:07

Before: af4d0731 arm B (2026-10-17 04:38-05:08): 1 exit hang in 32 runs (run 32, kernel-stuck exe, reboot); latency wait avg 0.120 ms, max 32.88 ms over 1,088 phase lines.

08:09:54 PRESENTER VERIFY cand15b: F:/Transformers Rebuild/Rebuild-Rendering/work/int_cand15/build/bin/wfc_rebuild.exe (64 runs, hang at 240 s)
work/leak/armB_d3d12present: latency wait avg 0.120 ms, max 32.88 ms over 1088 lines
work/presenter/cand15b: latency wait avg 0.002 ms, max 14.90 ms over 2176 lines
08:59:19 1 loop: 64 D3D12 runs, 0 hangs, 64 runs with watchdog lines; shutdown escape fired in 0 runs; queue not drained in 0 runs (must be 0)
08:59:19 2 FG run
fg UNKNOWN lockstep.arm.r1 present=D3D12 latencywait_max=0.00 smoke=1 watchdog=1 escape=0 nodrain=0
09:00:10 3 resize run
resize UNKNOWN lockstep.arm.r1 present=D3D12 latencywait_max=1.73 smoke=1 watchdog=1 escape=0 nodrain=0
09:00:56 4 mapswitch with the presenter
09:05:23 4 mapswitch: MAP-LOSS GATE: NO GROSS MAP LOSS (human look still required) - pass 6 / fail 0 / known 0 / info 5 / skip 1 / human 0 / unknown 0 present lines: 4
09:05:23 PRESENTER VERIFY cand15b DONE

Per-run lines: work/presenter/cand15b/verify.log (64 loop runs: present=D3D12, smoke=1, escape=0, nodrain=0 in all; 'watchdog=1' is the start-up 'GPU-loss probe' info line, no stall line).

Resize rerun (the first resize run ended before its 40 s trigger):
09:06:30 resize pid 21708 -> 1920x1080 True
09:06:34 resize pid 21708 -> 1600x900 True
09:06:38 resize pid 21708 -> 427x240 True
resize UNKNOWN lockstep.arm.r1 present=D3D12 latencywait_max=2.67 smoke=1 watchdog=1 escape=0 nodrain=0
