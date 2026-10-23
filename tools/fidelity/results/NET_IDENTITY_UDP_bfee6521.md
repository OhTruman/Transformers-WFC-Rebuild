# NET rows + VOICETEST - bfee6521 (late-join load window + keepalives 8d7c0cab, E3a2, voice 1 + 2) (2026-10-23 ~04:35)

| Row | Result |
|---|---|
| VOICETEST (headless, WFC_NULLRENDER_AUDIO=1, Opus in) | **PASS (0 failed)** |
| Five profiles (head's net_udp_profiles.ps1 + netshim) | **PASS**: every hash equal (4,200-4,201 ticks), exits 0, no dump. Hitches > 50 ms host / member: lan 0/0, typical 0/0, bad 3/2 (max 243 ms), burst 0/0, rttjump 0/1 (113 ms) |
| Late join (+25 s) | **PASS**: 3 peers identical on all 6,001 ticks; 'late join accepted - match seat 2 of 3', caught up at tick 1458 - READY; **1 CatchUpProgress event** (the flood is fixed) |
| Late join, joiner WFC_LOADDELAY_MS=20000 (a 20 s blocking load) | **PASS**: the joiner survives the 20 s load (load window + keepalives), caught up at tick 2661 (history 2671) - READY; 3 peers identical on all 6,001 ticks; every exit 0 |

Runs work/udp/run_bfee/, late_bfee/, late20_bfee/, voice_bfee/.
