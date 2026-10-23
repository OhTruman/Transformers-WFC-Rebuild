# NET rows - 35114cb9 (the relay fallback daf03569) (2026-10-23 ~06:35)

| Row | Result |
|---|---|
| **Relay fallback (new: tools/fidelity/udp-relay.ps1)** | **PASS**: a local tools/net/lobby_service.py (127.0.0.1:14510, relay :14511); host registers ('lobby service code ZRXAPY'), member joins by the code; WFC_NET_DIRECT_DROP=1 on both, so the only path is the relay ('relay 127.0.0.1:14511, room 065e48337c0b2c74'; the host sees the member as 'relay:1927054119 as seat 1'). 4,800 ticks, SIMHASH identical, both exit 0, RTT 2.8 ms |
| VOICETEST | PASS (0 failed) |
| Five profiles | PASS: hashes equal, exits 0; hitches > 50 ms host / member: lan 0/0, typical 0/0, bad 1/1, burst 0/0, rttjump 0/1 |
| Late join | PASS: 3 peers identical on 6,001 ticks; 1 CatchUpProgress event |

Runs work/udp/relay_351/, voice_351/, run_351/, late_351/.
