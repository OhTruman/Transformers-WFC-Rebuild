# NET.identity on e1fc0c6c (Gameplay's net identity fixes) - Experimental, 2026-10-17 13:5x (net-identity.ps1 -Replay, 30 s, paced)

| profile | live peers (seat 0 vs 1) | bundles | host (seat 0) vs single-process replay of its recording |
|---|---|---|---|
| lan | DESYNC at tick 1299 (exit 3 / 3) | identical up to it | **AGREE** on every common tick |
| typical | DESYNC at tick 1648 | identical up to it | **DESYNC at 1648** - replay a third value (s0 5f1e59e8, s1 c54e4609, replay 4a11dcfb) |
| bad | DESYNC at tick 1687 | identical up to it | **AGREE** |

- 602 -> 1299+ confirmed independently: ~700-1,090 InProgress ticks now identical on every profile (D auto from 4).
- The split tick moves with the profile (1299 / 1648 / 1687): the auto input delay shifts when commands land, so each profile plays a
  slightly different match; the split itself is the remaining perspective-dependent damage difference (Gameplay's 0.003 hp item).
- NEW for Systems: under 'typical' the replay of the host's own recording does NOT match the host at 1648 (a third hash), while it
  matches the host on lan and bad. Replay with the same seat should equal its recorder exactly whatever the perspective issue -
  either the recording misses an input the live host applied around tick 1648 (a late / resent command, a delay change, a stall
  boundary under typical's loss), or the replay applies one differently. Raw: work/netid/e1fc0c6/typical/{s0,s1,replay}/net.csv,
  rec.bin, shim.jsonl.
