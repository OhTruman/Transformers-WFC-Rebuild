# NET rows + VOICETEST - 8b1cd99e (voice steps 1 + 2) (2026-10-23 04:26)

## VOICETEST (headless, WFC_NULLRENDER_AUDIO=1): **PASS (0 failed)**, exit 0

- VOICETEST clean: 152 frames (3 concealed), SNR 34.6 dB, amplitude 0.299 (sent 0.300), indicator 2
- VOICETEST muted: 0 frames played, 149 muted packets, indicator 2
- VOICETEST lobby: member seat 1 talked 2 s - the host decoded 99 frames, the other member 99 (relayed)

Note: the first run failed 'Opus in this build (WFC_OPUS)' - Opus is a gitignored third_party fetch (tools/net/fetch_opus.py, like DXC); my build-target third_party lacked it. Copied Integration's third_party/opus (1.5.2) into work/ab/_third_party, reconfigured (WFC_OPUS=1 in build.ninja) and reran. Any lane building from git needs fetch_opus.py or VOICETEST fails at its first check.

## Five profiles: **PASS**

Every hash equal (4,200-4,201 ticks), every exit 0, no dump. Hitches > 50 ms host / member: lan 0/0 (max 27.4 ms); typical 0/0 (max 28.1 ms); bad 3/4 (max 300.0 ms); burst 0/1 (max 87.2 ms); rttjump 0/1 (max 111.4 ms); 
