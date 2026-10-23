# NET.identity, UdpSession path - 221e7b83 (2026-10-23)

The real online session end to end: two headless processes (WFC_NULLRENDER, no card), host WFC_UDPHOST=14200, member
WFC_UDPJOIN through netshim (WFC_UDPBIND=14201): UdpSession lobby -> launch record -> both load -> AllLoaded -> NetMatchDriver steps
the lockstep match in real time; hash every tick. Streets TDM, 2 humans + 2 v 2 Smart bots, seed 7, 70 s of match (4,201 ticks).
Runner: Systems' net_udp_profiles.ps1 (from 7ee650c7; not on the head under tools/systems) with the 7ee650c7 netshim (reverse-port
routing; the head's tools/net/netshim.py does not have it yet); join + hitches: tools/fidelity/netlog_join.py. Build: integration
221e7b83 (phase 2b cfa618a2 + the lobby re-delivery fix). Run: work/udp/run_221e7b8/.

| Profile | Exit host / member | Ticks compared | SIMHASH | D host / member | RTT ms | Hitches > 50 ms host / member (max gap ms) | Out B/s host / member |
|---|---|---|---|---|---|---|---|
| lan | 0 / 0 | 4,201 | identical | 3 / 3 | 3 | 0 / 0 (21 / 21) | 4,806 / 4,806 |
| typical | 0 / 0 | 4,200 | identical | 6 / 10 | 102-104 | 0 / 0 (29 / 24) | 8,380 / 10,045 |
| bad | 0 / 0 | 4,201 | identical | 15 / 15 | 206-207 | 4 / 1 (139 / 105) | 15,632 / 15,611 |
| burst | 0 / 0 | 4,200 | identical | 5 / 10 | 100-107 | 0 / 0 (36 / 37) | 8,389 / 10,099 |
| rttjump | 0 / 0 | 4,200 | identical | 6 / 10 | 101-102 | 0 / 1 (35 / 97) | 9,992 / 13,177 |

**PASS**: every hash equal on every compared tick in all five profiles, both exits 0, no leftover exes. Smoothness (the
step-gap rule): no hitch on lan / typical / burst; bad 4 + 1 gaps over 50 ms in 70 s (max 139 ms, at 200 ms RTT + loss);
rttjump 1 member gap (97 ms). (stall_ms - member 0 / 8 / 92 / 9 / 447 ms - is diagnostic only.)

Notes: the member's last net.csv row is all zero at shutdown (bytes / rtt 0); netlog_join now treats one drop as a cumulative
counter (it summed the column once and printed 10 MB/s). The member runs a larger D (10) than the host (5-6) on the 100 ms profiles.
