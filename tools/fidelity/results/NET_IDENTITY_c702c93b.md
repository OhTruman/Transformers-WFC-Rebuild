# NET.identity, transport half - integration c702c93b (Experimental, 2026-10-17 13:3x, tools/fidelity/net-identity.ps1 -Replay)

Two headless peers (null render, no card) through tools/fidelity/netshim.py, Streets TDM 2 v 2 bots + 2 human seats driven by their
PlayerBots, seed 7, WFC_NET_PACE=1, WFC_NET_HASH_EVERY=1, 30 s; the host records (WFC_NET_RECORD) and one no-network process replays
it (WFC_NET_REPLAY). Joined by tick with tools/fidelity/netlog_join.py.

| profile | live peers | command bundles | host vs replay | stalls (s0 / s1) | traffic per peer |
|---|---|---|---|---|---|
| lan | DESYNC at tick 602 (both exit 3) | identical on every common tick (602) | DESYNC at tick 602 (replay a third value) | 1 tick 12 ms / 0 | ~4.5 KB/s each way |
| typical | DESYNC at tick 602 | identical (602 ticks) | DESYNC at tick 602 | 0 / 12 ticks, 177 ms total, max 71 ms | ~11 KB/s |
| bad | DESYNC at tick 602 | identical (606 ticks) | DESYNC at tick 602 | 6 ticks 127 ms / 10 ticks 353 ms, max 129 ms | ~15 KB/s |

- The transport half PASSES: every profile delivers identical command bundles on every tick, no timeout / deadlock, stalls only
  where the impairment says (none on lan beyond the first tick), the traffic grows with loss (redundancy / resends) as designed.
- The sim split at tick 602 is Integration's known local-pawn item (Gameplay: humans[localSeat].selection not applied in
  launchMatch + a first-InProgress-step vertical velocity on the local pawn only): live s0 / s1 and the replay give three different
  hashes at 602 - each process's local pawn differs - so it is not the transport. NET.identity's full PASS re-runs on the head with
  Gameplay's fix.
- Caveat for the detector: SIMHASH is constant on ticks 1-599 (pre-match: the countdown), so agreement before tick 600 is trivial;
  the row reads from the first InProgress tick.
- Negative control (02b4bbfa, before Systems' launch switch): DESYNC at tick 601, bundles identical (work/netid/neg_02b4bbf).
- Tool fixes on the way: net.csv's bytes_in / bytes_out are cumulative counters (netlog_join now takes last - first); the column
  is 'bundlehash'. Raw: work/netid/c702c93/<profile>/{s0,s1,replay}/net.csv, shim.jsonl.
