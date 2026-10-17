# NET.identity on e5bc5a22 - **PASS** (Experimental, 2026-10-17 14:5x; net-identity.ps1 -Replay, 60 s match time, paced)

Two separate headless processes (null render, no card) through tools/fidelity/netshim.py; Streets TDM 2 v 2 bots + 2 human seats
driven by their PlayerBots; seed 7; WFC_NET_PACE=1; D auto from 4; SIMHASH every tick; the host records and one no-network process
replays its recording. Joined by tick with netlog_join.py.

| profile | exit (s0 / s1) | ticks | live peers | host vs replay | stalls s0 / s1 | traffic per peer |
|---|---|---|---|---|---|---|
| lan | 0 / 0 | 4,201 | **AGREE** every tick | **AGREE** | 1 tick 4 ms / 0 | 4.5 KB/s each way |
| typical | 0 / 0 | 4,201 | **AGREE** | **AGREE** | 0 / 7 ticks 76 ms (max 54) | 10.8-10.9 KB/s |
| bad | 0 / 0 | 4,201 | **AGREE** | **AGREE** | 5 ticks 126 ms / 11 ticks 364 ms (max 111) | 14.8-15.5 KB/s |

- Both peers and the replay produce the identical SIMHASH stream for the whole match (4,201 ticks, 70 s wall) under every profile;
  every run ends with exit 0 (match end point), no desync, no timeout. Stalls appear only under impairment, bounded (max 111 ms).
- History of this row: c702c93b split at 602 (local-pawn launch) -> e1fc0c6c at 1299 -> df4d62aa still 1299 (perspective-dependent
  pose) -> e5bc5a22 (Character::simMatrix without the presentation yaw offset for sim queries + per-seat pickups) PASS.
- Negative control kept: 02b4bbfa (before Systems' launch switch) splits at tick 601. Raw: work/netid/e5bc5a2/<profile>/.
