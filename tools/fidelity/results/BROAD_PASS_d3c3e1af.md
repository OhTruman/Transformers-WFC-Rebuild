# Broad pass 1 - d3c3e1af (campaign runtime 3-7, streaming, Matinee, FMV + dialog, invites, member relay, Escalation nav default, E3b, voice channels) - 2026-10-23 08:26-08:43

Headless, CPU only, one runner (work/udp/batch.ps1). The exe: wfc_rebuild from the untouched d3c3e1af export (Release, WFC_BUILD_FIDELITY
off - my harness overlay's stale CMakeLists, not the head; synced since).

| Area | Result |
|---|---|
| TDM 10 maps x 300 s x seeds 7 / 1234 (20 runs, 4 v 5 Smart) | **PASS**: 20 / 20 exit 0 at 300 s, 0 ERROR lines, no crash / hang |
| Campaign D5_OME + A1_IAC x 300 s x 2 seeds (SOLO, PlayerBot) | **PASS**: 4 / 4 exit 0, 18,061 steps each, 0 errors, no game over, 0 suicides; D5 seed 7: 'campaign: squad p2 stranded 30 s: moved back beside its buddy' (27d2405f's fix working) |
| CAMPAIGNTEST / KISMETTEST / VOICETEST | 24 / 24, 10 / 10, PASS |
| Escalation BrokenHope x 2 seeds | PASS: weapon kills (3 / 3), game over in wave 1 (the lone player) |
| **Escalation Remnant x 2 seeds** | **no horde combat** on the new default Escalation nav ('nav bot_nav_escalation.json: 1414 cells, 28 blocked at start'): 0 horde kills in 300 s on both seeds; the players' team bots strand again (seed 1234: p1 x9 'stranded off the mesh', 'trapped off the mesh (3 dither escapes)'), and the strand rule **suicides a players'-team bot** (seed 1234: 'p1 stranded 30 s with no way back at t 214.64: suicide' = Torque) - the horde is exempt since 7272c4ac, the team bots are not. On e5674e33 (versus nav + stand-in) Remnant had 4-7 weapon kills |
| Net: five profiles, late join, late join + early pick, relay, leave (switch, 3 seats) | **PASS**: every hash identical, every exit 0 |

One finding: the Remnant Escalation default (nav v3) - see the row. Everything else in the batch holds.
Runs work/batch_d3c3e1a/, work/udp/*_d3c3e1a/.
