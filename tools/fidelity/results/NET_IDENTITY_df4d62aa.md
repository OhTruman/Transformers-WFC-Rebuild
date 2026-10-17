# NET.identity on df4d62aa (Gameplay's 1299 fix + c1b0afb4) - Experimental, 2026-10-17 14:2x (net-identity.ps1 -Replay, 60 s, paced)

| profile | live peers (seat 0 vs 1) | bundles | host vs replay |
|---|---|---|---|
| lan | **DESYNC at tick 1299** (exit 3 / 3) - seat 0 e0d72116, seat 1 bdaaade7 | identical up to it | AGREE (replay = seat 0) |
| typical | DESYNC at tick 1844 | identical up to it | AGREE |
| bad | DESYNC at tick 1581 | identical up to it | AGREE |

- NOT the full PASS: on lan the live split is still at exactly tick 1299 (ticks up to 1298 identical, 3590cb61); typical / bad
  split later (1844 / 1581) as their input-delay timing shifts the match.
- Replay == its recorder on all three profiles now (the 'typical' 1648 replay mismatch of e1fc0c6c does not reproduce here).
- So a perspective-dependent difference remains at 1299 in this harness (2 human seats driven by PlayerBots, Streets TDM 2 v 2 bots,
  seed 7, D auto from 4); the per-seat animation fix did not remove it here. Raw: work/netid/df4d62a/<profile>/.
