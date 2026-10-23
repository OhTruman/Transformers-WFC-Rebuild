# Escalation soak - e2a58726 (E1 + the Application_EscTest.cpp fix) (2026-10-23)

Headless (WFC_NULLRENDER, lockstep, CPU only), ?GameModeTag=ESC on MP_ESC_Remnant / MP_ESC_BrokenHope, WFC_MATCH_SECONDS=300,
seeds 7 + 1234 (4 runs, BotsFriendly=4 BotsEnemy=5) + checks with the plain URL (seed 7) and with WFC_PLAYERBOT=1 + BotsFriendly=4.
Runner work/stabsoak/run2.sh (SOAK_MAPS); logs work/stabsoak/esc_e2a5872/, esc_plain/.

Note first: a clean checkout of effecacc .. 718efdee did not link (src/core/Application_EscTest.cpp, a new file of the E1 patch, was
not committed; undefined runEscalationTest). Integration fixed it in e2a58726.

| Run | Exit | Crash / assert | Waves begun / cleared | End | Spawn-finder failures |
|---|---|---|---|---|---|
| Remnant seed 7 | 0 | 0 | 1 / 0 | 300 s time, **wave 1 never progresses** | **97** x 'Failed to find a place to spawn a dude (HoloBrute_Autobot_AICD)' |
| Remnant seed 1234 | 0 | 0 | 1 / 0 | same | 97, same entry |
| Remnant plain URL (no bots) | 0 | 0 | 1 / 0 | same | 97, same entry |
| BrokenHope seed 7 | 0 | 0 | 1 / 0 | **'AllDead' game over at t 47.5 s** (score 0-0) | 0 |
| BrokenHope seed 1234 | 0 | 0 | 1 / 0 | AllDead at t 49.0 s | 0 |
| BrokenHope plain URL | 0 | 0 | 1 / 0 | AllDead at t 47.6 s | 0 |
| BrokenHope PLAYERBOT + 4 friendlies | 0 | 0 | 1 / 0 | AllDead at t 51.6 s | 0 |

Stable (no crash, assert, hang or non-zero exit), but two suspected E1 bugs, both deterministic:

1. **Remnant: stuck wave 1.** 'wave 1 begins (row 0: budget 100, alive 50, deficit 30)', then the horde spawn finder fails 97 times
   for HoloBrute_Autobot_AICD and nothing spawns or fights for the rest of 300 s (0 kills, 0 respawns, no 'cleared').
2. **BrokenHope: 'AllDead' game over with nobody dead.** 'game over at wave 1 (TnSurvivalGameOverMessage 1)' -> EndGame "AllDead"
   at ~48-52 s in every configuration, while the log shows no death and no respawn on the players' team (0 'MATCH respawn' lines;
   with 4 friendly bots + a bot-driven player too). Possibly the alive check counts pawns not yet spawned / in a launch, or the wrong team.

## Gameplay's read (2026-10-23)
- BrokenHope 'AllDead' = a real death (Match does not log kills): seed 7 'last player death: Player by Soldier_MachineGun_Decepticon_AICD
  at 47 s' - the horde gets SeeEnemy on spawn as the original; friendly bots are not 'players' for AllDead (the original had none;
  bots never fill co-op seats). Not a bug; Gameplay added the last death to the game-over line. Soaks judge BrokenHope on waves
  reached / no crash, not on game-over time.
- Remnant stuck wave 1 = a real E1 issue (every active zone scores 0 for ShortRange, the zone sort's tie order puts zero-point zones in
  the top 3, all 5 tries fail every recheck); RE asked for the original order; fix with E2 -> rerun on that sha.

## Rerun on the merged E2a head 29c1ce7d (2026-10-23 ~02:50)

| Run | Exit | Waves begun / cleared | Spawn-finder failures | Generators | End |
|---|---|---|---|---|---|
| Remnant seed 7 | 0 | 1 / 0 | 0 (was 97) | 35, 10 pickup factories hidden; 0 bought | 300 s, no death |
| Remnant seed 1234 | 0 | 1 / 0 | 0 | same | 300 s, no death |
| BrokenHope seed 7 | 0 | 1 / 0 | 0 | 35, 6 hidden; 0 bought | game over at 43 s: 'last player death: Player by Soldier_MachineGun_Decepticon_AICD 4 (AssaultRifle)' |
| BrokenHope seed 1234 | 0 | 1 / 0 | 0 | same | game over at 41 s (IonBlaster) |

Stable; the 97 'Failed to find a place' lines are gone. But on **Remnant the horde never spawns at all**: with WFC_ESCLOG=1 (seed 7,
plain ?GameModeTag=ESC, idle local player) there is not one 'ESCLOG try' line in 300 s - svSpawn is never called after 'wave 1 begins
(row 0: budget 100, alive 50, deficit 30)'; no death, no clear. (Gameplay's ESCTEST Remnant 5 / 5 spawns at 5771: the harness boot
differs - zone activation / PreGame from level start / the player's position?) BrokenHope spawns and kills as expected. 0 generator
buys is expected headless (no human buying). Logs work/stabsoak/esc_29c/, esc_29c_log/.

**Correction (Gameplay, confirmed):** the 'never calls svSpawn' read above is WRONG - my greps ('escalation' / 'spawn') missed the
'ESCLOG try' lines: the log has 5 tries, all successes at zone 5771 (5 HoloBrutes fill the alive cost). The real bug: the local player
never got a pawn (0 'local player spawned' lines) - Remnant's TnTeamPlayerStarts carry no team and its players are team 1, so no start
matched; the horde had nobody to hunt. Fix: GAMEPLAY_ESCALATION_E2FIX.patch (agents/gameplay bec7beea). Pass criterion on Remnant:
'local player spawned at TnTeamPlayerStart_...' then waves begun / cleared.

## Rerun on 309c450c (E2FIX 29f253cd merged) - 2026-10-23 ~03:15

| Run | Exit | Player pawn | Waves begun / cleared | Spawn tries / failures | Player alive | End |
|---|---|---|---|---|---|---|
| Remnant seed 7 | 0 | spawned (TnTeamPlayerStart_1675, team 1) | 3 / 2 | 40 / 0 | 300 s (whole run) | 300 s time, wave 3 running |
| Remnant seed 1234 | 0 | same | 3 / 2 | 41 / 0 | 300 s | same |
| BrokenHope seed 7 | 0 | spawned (TnTeamPlayerStart_10074, team 0) | 1 / 0 | 9 / 0 | 43 s | game over: Soldier_MachineGun_Decepticon_AICD 4 (AssaultRifle) |
| BrokenHope seed 1234 | 0 | same | 1 / 0 | 8 / 0 | 41 s | game over: ... 2 (IonBlaster) |

**PASS** on Gameplay's criterion: Remnant's player spawns, waves 1 (100) and 2 (150) clear, wave 3 (budget 200) begins at the same
point on both seeds; no failed spawn, no crash / assert. BrokenHope as designed (a lone headless player dies to wave 1). 0 generator buys
(no human). Logs work/stabsoak/esc_309/ (WFC_ESCLOG on).

## Rerun on bfee6521 (E3a2: WFC_ESCLOG logs every kill / buy / take) - 2026-10-23 04:31

| Run | Exit | Waves begun / cleared | ESCLOG kills | Kill sources | Buys / takes | End |
|---|---|---|---|---|---|---|
| Remnant seed 7 | 0 | 3 / 2 | 35 | **all 35 'by p-1 Engine.DmgType_Suicided'** (21 HoloBrute, 13 Soldier_MachineGun, 1 Soldier_Rocket) | 0 / 0 | 300 s |
| Remnant seed 1234 | 0 | 3 / 2 | 35 | identical: all suicides | 0 / 0 | 300 s |
| BrokenHope seed 7 | 0 | 1 / 0 | 12 | weapon kills (players' bots: CarMachineGun, HeavyPistol, HomingRocket, BurstRifle; horde kills a bot and the player) | 0 / 0 | game over 63 s |
| BrokenHope seed 1234 | 0 | 1 / 0 | 5 | weapon kills (Shotgun, BurstRifle, Sentry, ...) | 0 / 0 | game over 46 s |

**New suspect (Remnant):** no horde member on Remnant is killed by a player or a weapon - every death is 'Suicided' by the world (p-1),
and they come exactly 3 s apart (t 45.0, 48.1, 51.1, 54.1, 57.1, then 78.1, 81.1, 84.1, 87.1, 90.2 -> 'wave 1 cleared'), in step with the
director's 3 s spawn recheck. So Remnant's 'waves 1-2 cleared' are the director culling its own horde (a stuck / unreachable cull, a
kill volume at the spawn zones 11696 / 1584 / 9840, or a respawn that suicides the previous body), not combat. Buys / takes 0 as
Gameplay expects headless (no Interact). VOICETEST on bfee6521: PASS. Logs work/stabsoak/esc_bfee/.
