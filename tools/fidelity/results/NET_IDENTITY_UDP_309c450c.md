# NET.identity, UdpSession path + late join - 309c450c (Systems' join in progress) (2026-10-23)

## Five profiles (head's net_udp_profiles.ps1 + netshim; Streets TDM, 2 humans + 2 v 2 Smart, seed 7, hash every tick)

| Profile | Exit host / member | Ticks | SIMHASH | D host / member | RTT ms | Hitches > 50 ms host / member (max gap ms) |
|---|---|---|---|---|---|---|
| lan | 0 / 0 | 4,200 | identical | 3 / 3 | 3 | 0 / 0 (21 / 21) |
| typical | 0 / 0 | 4,200 | identical | 5 / 10 | 104-105 | 0 / 0 (27 / 24) |
| bad | 0 / 0 | 4,201 | identical | 15 / 14 | 199-213 | 2 / 3 (236 / 128) |
| burst | 0 / 0 | 4,201 | identical | 5 / 10 | 102-105 | 0 / 0 (29 / 26) |
| rttjump | 0 / 0 | 4,200 | identical | 6 / 11 | 106 | 0 / 1 (48 / 127) |

## Late-join arm (new: work/udp/late_join.ps1)

Three headless processes on direct loopback (no shim): the host (WFC_UDPHOST, WFC_UDPSEATS=2, WFC_UDPRESERVE=1), an on-time member,
and a member that joins 25 s into the match with WFC_UDPLATEJOIN=1; 90 s of match, hash every tick.

| Peer | Exit | Steps | Notes |
|---|---|---|---|
| host | 0 | 6,001 | MemberJoined (in match), PeerLoaded (in match) |
| member | 0 | 6,001 | |
| late | 0 | 6,001 (history from tick 1) | 'late join accepted - match seat 2 of 3'; 'caught up at tick 1459 (history at 1469, host at 1469) - READY'; 0 step gaps > 50 ms (max 19.6 ms) |

**SIMHASH identical on every one of 6,001 ticks across all three peers** (the late peer's replayed history included).

**PASS** for the five profiles and the late-join arm: every exit 0, no new minidump, no exception / assert line. The member left
through HostLost in the late arm too and exited 0.

Minor (to Systems): after READY the late peer keeps raising 'UDPMATCH <- CatchUpProgress (in match)' on almost every tick until the
end - 4,532 events after READY, 2 before (UDPMATCH done reports 4,534). No effect on sync or smoothness; looks like the progress event
is not stopped once caught up (log spam, maybe UI churn).
