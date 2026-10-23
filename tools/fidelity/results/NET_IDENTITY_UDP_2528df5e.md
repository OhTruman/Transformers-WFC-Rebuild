# NET.identity, UdpSession path - 2528df5e (v3 vote wire + exit-crash fix, kProtocol 0x57464303) (2026-10-23)

Same setup as NET_IDENTITY_UDP_221e7b83.md (two headless processes, WFC_UDPHOST / WFC_UDPJOIN through netshim, Streets TDM,
2 humans + 2 v 2 Smart, seed 7, hash every tick), now with the head's own tools/systems/net_udp_profiles.ps1 and tools/net/netshim.py
(reverse-port routing present). Run: work/udp/run_2528df5/.

| Profile | Exit host / member | Ticks | SIMHASH | D host / member | RTT ms | Hitches > 50 ms host / member (max gap ms) | Member shutdown path |
|---|---|---|---|---|---|---|---|
| lan | 0 / 0 | 4,200 | identical | 3 / 3 | 3 | 0 / 0 (23 / 23) | seat 0 left |
| typical | 0 / 0 | 4,200 | identical | 5 / 10 | 103 | 0 / 0 (28 / 21) | **HostLost (in match)** |
| bad | 0 / 0 | 4,201 | identical | 15 / 12 | 206-210 | 1 / 3 (236 / 126) | normal end |
| burst | 0 / 0 | 4,200 | identical | 6 / 10 | 101-107 | 0 / 0 (26 / 34) | **HostLost (in match)** |
| rttjump | 0 / 0 | 4,200 | identical | 5 / 10 | 100 | 0 / 1 (29 / 105) | seat 0 left |

**PASS**, including the fix under test: every exit 0, **no new minidump** (run dirs, the bin dir and %LOCALAPPDATA%\CrashDumps
checked against a timestamp taken before the run), no access-violation / exception / assert line in any log. The member went
through HostLost in two profiles - the path that crashed before the fix - and still exited 0.
Traffic 4.6-15.7 KB/s; bad: 1 + 3 gaps over 50 ms in 70 s (max 236 ms at 206 ms RTT + loss).
