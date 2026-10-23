# Head rows - 6d1e465a (campaign boot 14549980 + b949b58c; ESCNAV gate, pick hold, member relay) (2026-10-23 ~07:20)

| Row | Result |
|---|---|
| CAMPAIGNTEST (WFC_MAP=D5_OME, headless) | **21 / 21 checks passed**, exit 0 |
| KISMETTEST | 10 / 10 |
| VOICETEST | PASS (0 failed) |
| Late join (+25 s) | PASS: 3 peers identical on 6,001 ticks, 1 CatchUpProgress event |
| Late join, WFC_UDPPICK_EARLY=1 on the joiner | PASS: 'the joiner picks its character early (catching up)', caught up at tick 1452 - READY, 3 peers identical on 6,001 ticks |
| D5_OME 300 s campaign soak (SOLO, PlayerBot, difficulty 1, seeds 7 / 1234) | exit 0, 300 s, game over never fired; squad p1 Soundwave (Truck4) + p2 Breakdown (Car3), Kismet 898 ops. **Two findings** below |

## D5_OME findings
1. **Missing data**: 'glb: cannot read F:/Transformers Rebuild/AssetTools/out/campaign_render/D5_OME/D5_OME_BASE/collision_weapon.glb'
   on both seeds - the file is absent in the dev campaign_render (and in package v3); weapon traces on D5 fall back to whatever the
   loader does without it.
2. **Strand suicide on the campaign squad** (seed 7): 'bots: p2 stranded 30 s with no way back at t 261.91: suicide' then
   'MATCH respawn player=2 delay_s=0.02 reason=wave' - the versus strand rule (exempted for the Escalation horde in 7272c4ac) still
   applies to the campaign AI squad; Breakdown (Car3) died and respawned. Also p0 (the PlayerBot human) / p1 stranded off the mesh
   at t 153.8 / 268.2 (walking back). Audit: squad alive except that one strand suicide; game over not fired; strands 2 + 1 suicide
   (seed 7), 0 (seed 1234).

The late-join arms failed twice before for harness reasons (a PowerShell case-insensitive variable clash; then bash -> powershell
-File with cygpath paths: 'Illegal characters in path') - run the .ps1 tools from PowerShell directly.
