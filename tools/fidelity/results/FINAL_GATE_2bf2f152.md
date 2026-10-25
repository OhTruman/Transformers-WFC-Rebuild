# Final gate 2bf2f152 - menu flows PASS; same match: versus = base, Escalation EXPECTED DIFF - 2026-10-25 00:34

Head work/ab/fast_2bf2f15 (2bf2f152) vs base work/ab/fast_af4d073_final15 (af4d0731 + FINAL_BASE_ITEMS 01-31; no item 32 - Gameplay out of usage). DLC root work/final_assets, render work/final_render.

## Menu flows (windowed, real menus, fresh seeded profile, muted, NOINTERACT)

- **Multiplayer: PASS** (every row) - Create a Character, bots 4 + 4, map, Team, countdown cancel -> restored -> Start again, Scout, PlayerList, move / shoot / transform, Change Character and back, quit.
- **Escalation on Tranquillity: PASS** (every judged row) - catalog 513 selectable=1, Online.SetSelectedMapID 513 -> gamelobby.map mapId=513 MP_ESC_Tranquillity, the match world MP_ESC_Tranquillity, the pick kept, no character screen after the load, moves, back to the party lobby. WFC_ESC_INVULN on + the player bot, 360 s cap. **Measured: wave 1 began, not cleared within 360 s** (allowed: Integration - Pulse clears, the others may not).

## Same match (headless lockstep 600-9600, seed 1234; FINAL_EXPECT_DIFF = the Escalation rows: the reachable-spawn adaptation has no base item)

```
23:40:12 map 508 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
23:40:14 map 506 same match: lockstep.head.r1: head: steps 2413 (600-9600), same match as 'base' False; negative control differs
23:40:15   506: EXPECTED DIFF (FINAL_EXPECT_DIFF)
23:41:05 map 505 same match: lockstep.head.r1: head: steps 1998 (600-9600), same match as 'base' True; negative control differs
23:41:06   505: matches although a difference was expected
23:42:09 map 511 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
23:42:47 map 512 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
23:43:07 map MP_ESC_Tranquillity same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' False; negative control differs
23:43:08   MP_ESC_Tranquillity: EXPECTED DIFF (FINAL_EXPECT_DIFF)
23:43:26 map MP_ESC_District same match: lockstep.head.r1: head: steps 2294 (600-9600), same match as 'base' False; negative control differs
23:43:27   MP_ESC_District: EXPECTED DIFF (FINAL_EXPECT_DIFF)
23:44:05 map MP_IAC_Horizon same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
23:44:40 map MP_IAC_Metropolis same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
23:45:20 map MP_KON_Sector same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
23:45:41 map MP_ESC_Pulse same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' False; negative control differs
23:45:43   MP_ESC_Pulse: EXPECTED DIFF (FINAL_EXPECT_DIFF)
23:46:05 map MP_ESC_Static same match: lockstep.head.r1: head: steps 1923 (600-9600), same match as 'base' False; negative control differs
23:46:06   MP_ESC_Static: EXPECTED DIFF (FINAL_EXPECT_DIFF)
```

Versus rows 508, 511 (Havoc), 512 (Fortress), Horizon, Metropolis, Sector: head = base; the negative control differs on every row. Escalation: 506 Remnant, Tranquillity, District, Pulse, Static differ as expected; **505 BrokenHope still matches** (the adaptation does not change its first 9000 steps at seed 1234).

## Stability set (broad-pass, DLC root)

```
23:46:14 CAMPAIGNTEST exit 0: CAMPAIGNTEST SUMMARY: 24/24 checks passed
23:46:14 KISMETTEST exit 0: KISMETTEST SUMMARY: 10/10 checks passed
23:46:32 VOICETEST exit 0: VOICETEST SUMMARY: PASS (0 failed)
23:46:34 D5_OME seed 7 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
23:46:37 D5_OME seed 1234 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
23:46:57 A1_IAC seed 7 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
23:47:11 A1_IAC seed 1234 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
23:47:15 ESC MP_ESC_Remnant seed 7 exit 0, kills 6 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:17 ESC MP_ESC_BrokenHope seed 7 exit 0, kills 3 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:20 ESC MP_ESC_Tranquillity seed 7 exit 0, kills 17 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:23 ESC MP_ESC_District seed 7 exit 0, kills 5 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:27 ESC MP_ESC_Pulse seed 7 exit 0, kills 81 (world 1), waves 3/2, errors 0, 
23:47:31 ESC MP_ESC_Static seed 7 exit 0, kills 2 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:33 ESC MP_ESC_Remnant seed 1234 exit 0, kills 5 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:36 ESC MP_ESC_BrokenHope seed 1234 exit 0, kills 5 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:40 ESC MP_ESC_Tranquillity seed 1234 exit 0, kills 19 (world 2), waves 1/0, errors 0, 
23:47:44 ESC MP_ESC_District seed 1234 exit 0, kills 5 (world 0), waves 1/0, errors 0, game over at wave 1
23:47:48 ESC MP_ESC_Pulse seed 1234 exit 0, kills 60 (world 0), waves 3/2, errors 0, 
23:47:54 ESC MP_ESC_Static seed 1234 exit 0, kills 6 (world 0), waves 1/0, errors 0, 
23:51:16 TDM: runs 38, non-zero exit 0, not at time 0, errors 0, warns 234
```

38 TDM runs on all 19 versus maps: 0 non-zero exits, 0 errors. Headless Escalation progression: Pulse reaches wave 3 (2 waves cleared) at both seeds; the others end at wave 1.

---

## mp: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 161 s wall |
| reaches the end | PASS | 27 / 27 checkpoints |
| checkpoint main | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuMain_mc |
| checkpoint partylobby | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint cac.list | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.customTransMenuLoader_mc.customCharMenu_mc.customSelection_mc |
| checkpoint cac.overview | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.customTransMenuLoader_mc.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc |
| checkpoint cac.back | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint gamelobby | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint gamelobby.settings | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.wfcGameSettings_mc.subMenu_mc |
| checkpoint gamelobby.settings.changed | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.wfcGameSettings_mc.subMenu_mc |
| checkpoint gamelobby.after_settings | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint gamelobby.map | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint gamelobby.team | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint countdown.running | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint countdown.cancelled | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint charselect | PASS | ui WaitingOnGameStart, level Match, movie UI_GFxCustomize_p.CustomTransformers_GFX_1, owner WaitingOnGameStart:UI_GFxCustomize_p.CustomTransformers_GFX_1 |
| checkpoint pregame.countdown | PASS | ui WaitingOnGameStart, level Match, movie UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1, owner WaitingOnGameStart:UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1 |
| checkpoint ingame.spawned | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.moved | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.shot | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.transformed | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint pause | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint changechar.select | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint changechar.pause | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint changechar.back | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint changechar.moved_after | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint quitbox | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint lobby.returned | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint flow.end | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| expect mode TDM | PASS | match mode TDM |
| expect log FLOW.lobby.bots.field=autobot.autobot=[1-9] | PASS | log line 773 |
| expect log FLOW.lobby.bots.field=decepticon.*decepticon=[1-9] | PASS | log line 820 |
| expect log BotsAutobot=[1-9].*BotsDecepticon=[1-9]|BotsDecepticon=[1-9].*BotsAutobot=[1-9] | PASS | log line 910 |
| expect state cac.back InLobby | PASS | uiState=InLobby |
| expect level cac.back PartyLobby | PASS | level=PartyLobby |
| expect log FLOW.gamelobby.map.mapId= | PASS | log line 567 |
| expect log FLOW.lobby.team.team= | PASS | log line 897 |
| expect log FLOW.gamelobby.countdown.*state=cancelled.by=Back | PASS | log line 933 |
| expect log FLOW.lobby.countdownCancelMenu.restored=true | PASS | log line 935 |
| expect state countdown.cancelled InLobby | PASS | uiState=InLobby |
| expect level countdown.cancelled GameLobby | PASS | level=GameLobby |
| expect chassis Car2|Car4 | PASS | first pick chassis Car4 |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.moved 2 | PASS | moved 20.10 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect shots ingame.moved ingame.shot | PASS | shots 0 -> 3 |
| expect form ingame.shot ingame.transformed | PASS | form robot -> vehicle |
| expect owner changechar.select customSelection | PASS | inputOwner=_level0.customCharMenu_mc.customSelection_mc | Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| expect owner changechar.pause pauseShell_mc.mainMenu_mc | PASS | inputOwner=_level0.pauseShell_mc.mainMenu_mc | Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| expect state changechar.back InGame | PASS | uiState=InGame |
| expect owner changechar.back game | PASS | inputOwner=- | game |
| expect level lobby.returned PartyLobby | PASS | level=PartyLobby |

## esc: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 457 s wall |
| reaches the end | PASS | 13 / 13 checkpoints |
| checkpoint main | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuMain_mc |
| checkpoint esc.menu | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint esc.hostoptions | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menuEscalationHostGame_mc |
| checkpoint esc.lobby | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint esc.lobby.map | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint esc.charselect | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.charSelect_mc.charSelectMenu_mc |
| checkpoint esc.lobby.picked | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint ingame.spawned | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.played | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint esc.wave2 | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint quitbox | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint returned | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint flow.end | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| expect mode SV|ESC | PASS | match mode SV |
| expect log match: launched \S+ ESC | PASS | log line 1416 |
| expect log controller=TnUIControllerSurvival | PASS | log line 1467 |
| expect nolog-after FLOW.loading.match.world OpenUI.*(CustomTransformers_GFX|CharacterSelect_GFX) | PASS | 0 hits after line 723: - |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect chassis-kept | PASS | picks [('true', 'Truck3')] |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.played 2 | PASS | moved 5.85 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect level returned PartyLobby | PASS | level=PartyLobby |
| expect log FRONTEND catalog DLC map 513.*selectable=1 | PASS | log line 18 |
| expect log FLOW.gamelobby.map.mapId=513.map=MP_ESC_Tranquillity | PASS | log line 626 |
| expect log FLOW loading match world: map dir MP_ESC_Tranquillity | PASS | log line 723 |
| expect measure escalation: wave [0-9]+ begins | MEASURED | 1 hits: first at log line 1781: [info ] escalation: wave 1 begins (row 0: budget 100, alive 50, deficit 20) |
| expect measure escalation: wave [0-9]+ cleared | MEASURED | 0 hits |
| expect state esc.wave2 InGame | PASS | uiState=InGame |
