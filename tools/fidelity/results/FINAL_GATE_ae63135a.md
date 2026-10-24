# Final gate ae63135a (next package) - menu flows PASS, same-match pre PASS - 2026-10-24 16:40

Head work/ab/fast_ae63135 (ae63135a: 277d6e33 + Systems' packaging / data-path fixes, sim unchanged) vs base work/ab/fast_af4d073_final15 (af4d0731 + FINAL_BASE_ITEMS 01-31 from 3aa61a9f). DLC root work/final_assets (nine DLC maps), render work/final_render.

## Menu flows (windowed, real menus, fresh seeded profile, muted, NOINTERACT)

- **Multiplayer: PASS** (every row): Create a Character + back, Game Settings bots 4 + 4, map, Team, Start -> countdown -> Back cancels -> menu restored -> Start again, Scout, the PreGameCountdown PlayerList, move / shoot / transform, Pause > Change Character > back, quit to the lobby.
- **Escalation: PASS** (every row): lobby, Host Options, Create Game, character picked in the lobby and kept, TnUIControllerSurvival, no character screen after the load, moves, return to the party lobby.

Logs + shots work/menuqa/ae63135/<flow>/. Campaign is not in this gate (Integration: mp + esc).

## Same match (headless lockstep 600-9600, seed 1234): head = base on all 12 rows; the negative control differs on every row

```
16:01:10 map 508 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:01:33 map 506 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:01:56 map 505 same match: lockstep.head.r1: head: steps 1998 (600-9600), same match as 'base' True; negative control differs
16:03:15 map 511 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:03:57 map 512 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:04:29 map MP_ESC_Tranquillity same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:04:47 map MP_ESC_District same match: lockstep.head.r1: head: steps 1621 (600-9600), same match as 'base' True; negative control differs
16:05:26 map MP_IAC_Horizon same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:06:00 map MP_IAC_Metropolis same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:06:40 map MP_KON_Sector same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:07:00 map MP_ESC_Pulse same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
16:07:22 map MP_ESC_Static same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control differs
```

## Stability set (broad-pass on the head, now with the DLC asset root - fa19f406)

```
16:07:30 CAMPAIGNTEST exit 0: CAMPAIGNTEST SUMMARY: 24/24 checks passed
16:07:31 KISMETTEST exit 0: KISMETTEST SUMMARY: 10/10 checks passed
16:07:48 VOICETEST exit 0: VOICETEST SUMMARY: PASS (0 failed)
16:07:51 D5_OME seed 7 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
16:07:54 D5_OME seed 1234 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
16:08:12 A1_IAC seed 7 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
16:08:27 A1_IAC seed 1234 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
16:08:32 ESC MP_ESC_Remnant seed 7 exit 0, kills 0 (world 0), waves 1/0, errors 0, 
16:08:34 ESC MP_ESC_BrokenHope seed 7 exit 0, kills 3 (world 0), waves 1/0, errors 0, game over at wave 1
16:08:39 ESC MP_ESC_Tranquillity seed 7 exit 0, kills 6 (world 2), waves 1/0, errors 0, 
16:08:43 ESC MP_ESC_District seed 7 exit 0, kills 9 (world 0), waves 1/0, errors 0, 
16:08:47 ESC MP_ESC_Pulse seed 7 exit 0, kills 3 (world 0), waves 1/0, errors 0, 
16:08:53 ESC MP_ESC_Static seed 7 exit 0, kills 2 (world 0), waves 1/0, errors 0, 
16:08:57 ESC MP_ESC_Remnant seed 1234 exit 0, kills 0 (world 0), waves 1/0, errors 0, 
16:09:00 ESC MP_ESC_BrokenHope seed 1234 exit 0, kills 5 (world 0), waves 1/0, errors 0, game over at wave 1
16:09:05 ESC MP_ESC_Tranquillity seed 1234 exit 0, kills 9 (world 0), waves 1/0, errors 0, 
16:09:08 ESC MP_ESC_District seed 1234 exit 0, kills 7 (world 0), waves 1/0, errors 0, 
16:09:13 ESC MP_ESC_Pulse seed 1234 exit 0, kills 1 (world 0), waves 1/0, errors 0, 
16:09:19 ESC MP_ESC_Static seed 1234 exit 0, kills 2 (world 0), waves 1/0, errors 0, 
16:12:37 TDM: runs 38, non-zero exit 0, not at time 0, errors 0, warns 234
```

All 19 versus maps incl. the nine DLC maps clean on TDM (38 runs, 0 non-zero exits, 0 errors); all six ESC maps clean at both seeds.

---

## mp: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 160 s wall |
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
| exit | PASS | exit 0, 98 s wall |
| reaches the end | PASS | 11 / 11 checkpoints |
| checkpoint main | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuMain_mc |
| checkpoint esc.menu | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint esc.hostoptions | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menuEscalationHostGame_mc |
| checkpoint esc.lobby | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint esc.charselect | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.charSelect_mc.charSelectMenu_mc |
| checkpoint esc.lobby.picked | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint ingame.spawned | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.played | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint quitbox | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint returned | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint flow.end | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| expect mode SV|ESC | PASS | match mode SV |
| expect log match: launched \S+ ESC | PASS | log line 1500 |
| expect log controller=TnUIControllerSurvival | PASS | log line 1556 |
| expect nolog-after FLOW.loading.match.world OpenUI.*(CustomTransformers_GFX|CharacterSelect_GFX) | PASS | 0 hits after line 716: - |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect chassis-kept | PASS | picks [('true', 'Truck3')] |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.played 2 | PASS | moved 37.86 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect level returned PartyLobby | PASS | level=PartyLobby |
