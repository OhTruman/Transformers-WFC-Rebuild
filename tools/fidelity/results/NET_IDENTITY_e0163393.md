# NET.identity - e0163393 (2026-10-23 00:56)

Two headless peers through netshim + a host replay (tools/fidelity/net-identity.ps1 -Replay), Streets TDM, 2 humans + 4 Smart bots (2 v 2), hash every tick, 80 s = 4,801 ticks per profile. Covers e6feadfc (tick-stamped Choose Character, per-seat homing lock, per-seat Thermo Mines) and f8a02fc8 (#11 spread: every shot direction).

| Profile | Exit | Live peers | Host vs replay | Stall ticks (s0 / s1) | KB/s in / out |
|---|---|---|---|---|---|
| lan | 0 / 0 | AGREE | AGREE | 0 / 0 | 4.5 / 4.5 |
| typical | 0 / 0 | AGREE | AGREE | 0 / 3 | 10.8 / 10.9 |
| bad | 0 / 0 | AGREE | AGREE | 19 / 10 | 14.9 / 15.5 |

**PASS**: SIMHASH identical on every one of 4,801 ticks under lan / typical / bad, live and replayed. Run: work/netid/e016339/. Note: the 80 s window (was 70 s at e5bc5a22) gives the bots time to fire; the logs do not count shots, so per-shot coverage rests on the hash covering projectile and spread state every tick.
