# NET.identity, UdpSession path - e2a58726 (contains 718efdee: the Frontend's vote fixes + flow-test checks; + E1) (2026-10-23)

Same setup as NET_IDENTITY_UDP_2528df5e.md, with the head's own net_udp_profiles.ps1 + netshim. (718efdee itself does not link from
git - Application_EscTest.cpp missing - so the row runs on e2a58726, which adds it and contains 718efdee.) Run: work/udp/run_e2a5872/.

| Profile | Exit host / member | Ticks | SIMHASH | D host / member | RTT ms | Hitches > 50 ms host / member (max gap ms) | Member shutdown |
|---|---|---|---|---|---|---|---|
| lan | 0 / 0 | 4,201 | identical | 3 / 3 | 3 | 0 / 0 (21 / 22) | HostLost (in match) |
| typical | 0 / 0 | 4,200 | identical | 5 / 10 | 103 | 0 / 0 (29 / 22) | HostLost (in match) |
| bad | 0 / 0 | 4,199 | identical | 12 / 15 | 198-204 | 1 / 2 (98 / 235) | HostLost (in match) |
| burst | 0 / 0 | 4,200 | identical | 6 / 10 | 99 | 0 / 0 (29 / 21) | HostLost (in match) |
| rttjump | 0 / 0 | 4,200 | identical | 5 / 10 | 100 | 0 / 1 (33 / 108) | HostLost (in match) |

**PASS**: every hash equal, every exit 0, no new minidump, no exception / assert line; the member went through HostLost in all five
profiles and exited 0 each time (the exit-crash fix holds).
