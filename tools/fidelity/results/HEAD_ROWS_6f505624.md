# Head rows - 6f505624 (BotNav v3 kinds, chapter units from the campaign root, campaign boot glue) - 2026-10-23 07:21-07:40

The last per-merge batch before the broad-stroke directive (user, 07:30: one broad pass per big batch / package from now on).

| Row | Result |
|---|---|
| CAMPAIGNTEST (D5_OME) | 21 / 21 |
| KISMETTEST | 10 / 10 |
| VOICETEST | PASS |
| D5_OME 300 s soak, seeds 7 / 1234 | exit 0, game over never fired, **0 strands, 0 suicides**; 1 error each: collision_weapon.glb missing (AssetTools now produces it; reaches a package at Systems' next build) |
| Escalation, both maps x 2 seeds | exit 0; weapon kills only (3-7, 0 world), wave 1, the lone player killed (as designed); 0 errors. (The first try in the batch passed a garbage URL - a PowerShell '$m?' variable-name bug in my batch, fixed to '${m}?' and rerun) |
| Five profiles | PASS (hashes equal, exits 0) |
| Late join / late join + early pick | PASS, 3 peers identical |
| Relay fallback | PASS, identical |
| Leave (switch, 3 seats) | PASS, host = stay |

Runs work/batch_6f50562/, work/stabsoak/esc_6f5/, work/udp/*_6f50562/. Batch runner: work/udp/batch.ps1 (-Ref -Tag).
