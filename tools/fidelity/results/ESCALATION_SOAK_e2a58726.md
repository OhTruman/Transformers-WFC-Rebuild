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
