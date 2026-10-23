# NET.identity - 4bcfde3d, online phase 2 (2026-10-23 01:05)

WFC_NET_* row: two headless peers through netshim + a host replay (tools/fidelity/net-identity.ps1 -Replay), Streets TDM, 2 humans + 4 Smart bots, hash every tick, 80 s = 4,801 ticks per profile. Head = Systems' UdpSession + NetMatchDriver + NatPortMap + lobby-service wiring + Choose Character as tick-stamped lockstep events (CMDS trailer).

| Profile | Exit | Live peers | Host vs replay | Stall ticks (s0 / s1) | KB/s in / out |
|---|---|---|---|---|---|
| lan | 0 / 0 | AGREE | AGREE | 0 / 0 | 4.6 / 4.6 |
| typical | 0 / 0 | AGREE | AGREE | 0 / 1 | 10.9 / 11.0 |
| bad | 0 / 0 | AGREE | AGREE | 8 / 12 | 15.1 / 15.5 |

**PASS** (WFC_NET_* path): SIMHASH identical on all 4,801 ticks under lan / typical / bad, live and replayed. Run: work/netid/4bcfde3/.

UdpSession path: NOT covered - WFC_UDPSESSIONTEST is an in-process protocol test (no match over it); a two-process headless UdpSession match entry was requested from Systems (2026-10-23).
