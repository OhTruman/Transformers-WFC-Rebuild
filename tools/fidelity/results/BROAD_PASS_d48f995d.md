# Broad pass 2 - d48f995d (runtime 8-11: real streaming, AnimControl, destructibles, drops, chapter audio + dialog events, matinee sound keys; Escalation nav default off again; the load fix) - 2026-10-23 09:02-09:24

Headless, CPU only, work/udp/batch.ps1 (fresh export + build; the harness now links too).

| Area | Result |
|---|---|
| TDM 10 maps x 300 s x seeds 7 / 1234 (20 runs, 4 v 5 Smart) | **PASS**: 20 / 20 exit 0 at 300 s, 0 ERROR lines |
| Campaign D5_OME + A1_IAC x 300 s x 2 seeds (SOLO, PlayerBot) | **PASS**: 4 / 4 exit 0, 18,061 steps each, 0 errors, no game over, 0 suicides; D5 seed 7: one squad move-back (the fix working) |
| CAMPAIGNTEST / KISMETTEST / VOICETEST | 24 / 24, 10 / 10, PASS |
| Escalation Remnant + BrokenHope x 2 seeds (default = versus nav + stand-in) | **PASS**: weapon kills only (Remnant 4 / 7, BrokenHope 3 / 5), 0 world kills, 0 errors, the lone player killed in wave 1 - Remnant's combat is back (pass 1 had none on the cell-nav default) |
| Net: five profiles (status 0, hashes identical), late join, late join + early pick, relay, leave (switch, 3 seats) | **PASS**: every hash identical, every exit 0 |

**All PASS; no findings.** Runs work/batch_d48f995/, work/udp/*_d48f995/.
