# Menu-flow QA gate - a2e9fbd9 (USER_PLAYTEST_2026-10-23b section 43) - 2026-10-24 10:31

One exe (work/ab/fast_a2e9fbd), windowed 04:33-04:47, muted, NOINTERACT, a seeded fresh profile per flow (HasAdjustedGamma=True), the real menus with the Frontend's steps; logs + shots work/menuqa/a2e9fbd/<flow>/.

| Flow | Verdict | Headline |
|---|---|---|
| Escalation | **PASS** | as on 7a7b8ba6 |
| Campaign | **PASS** | as on 7a7b8ba6 |
| Multiplayer | **FAIL** at one step | Create a Character list + overview and back, Game Settings bots 4 + 4 (lobby.bots) + Back closes, map 502, Team row, countdown + cancel by Back (#25) all PASS; **Start after the cancel does nothing** (startGame_mc found 50 px higher, y=308 vs 358; the click lands, no countdown; flow timeout). Cause (Frontend): the versus lobby's updateMenu hides Start / Select Map and raises the menu during the countdown and never restores it (the original could not cancel); fix df8f62a4 (FRONTEND_PM_RESTART.patch). The in-match steps after Start (Scout, move / shoot / transform, Change Character, quit) were not reached - rerun mp on the fixed sha. |

Checker notes: lobby.bots logs field=autobot / decepticon (expectation corrected after this run); the new '#expect log lobby.countdownCancelMenu restored=true' row is the fix's own trace (absent here = the bug).

## mp: **FAIL**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 604 s wall |
| reaches the end | FAIL | stuck before checkpoint 'charselect' (13 / 27 reached); last script step: script.clickclip path=lobby_mc.menuAnchor_mc.menu_mc.startGame_mc found=true x=384 y=308 |
| timeout | FAIL | flow timeout reported |
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
| expect mode TDM | FAIL | match mode None |
| expect log FLOW.lobby.bots.field=autobot.autobot=[1-9] | PASS | log line 770 |
| expect log FLOW.lobby.bots.field=decepticon.*decepticon=[1-9] | PASS | log line 817 |
| expect log BotsAutobot=[1-9].*BotsDecepticon=[1-9]|BotsDecepticon=[1-9].*BotsAutobot=[1-9] | PASS | log line 907 |
| expect state cac.back InLobby | PASS | uiState=InLobby |
| expect level cac.back PartyLobby | PASS | level=PartyLobby |
| expect log FLOW.gamelobby.map.mapId= | PASS | log line 564 |
| expect log FLOW.lobby.team.team= | PASS | log line 894 |
| expect log FLOW.gamelobby.countdown.*state=cancelled.by=Back | PASS | log line 930 |
| expect log FLOW.lobby.countdownCancelMenu.restored=true | FAIL | not in the log |
| expect state countdown.cancelled InLobby | PASS | uiState=InLobby |
| expect level countdown.cancelled GameLobby | PASS | level=GameLobby |
| expect chassis Car2|Car4 | FAIL | first pick chassis None |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect state ingame.spawned InGame | UNKNOWN | uiState=- |
| expect moved ingame.spawned ingame.moved 2 | UNKNOWN | no pos field on navcheck (FRONTEND_QA_GATE_HOOKS not in this build) |
| expect still ingame.spawned 2 | UNKNOWN | jitter=None |
| expect shots ingame.moved ingame.shot | UNKNOWN | shots None -> None |
| expect form ingame.shot ingame.transformed | UNKNOWN | form None -> None |
| expect owner changechar.select customSelection | UNKNOWN | inputOwner=- |
| expect owner changechar.pause pauseShell_mc.mainMenu_mc | UNKNOWN | inputOwner=- |
| expect state changechar.back InGame | UNKNOWN | uiState=- |
| expect owner changechar.back game | UNKNOWN | inputOwner=- |
| expect level lobby.returned PartyLobby | UNKNOWN | level=- |

## esc: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 122 s wall |
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
| expect log match: launched \S+ ESC | PASS | log line 1468 |
| expect log controller=TnUIControllerSurvival | PASS | log line 1524 |
| expect nolog-after FLOW.loading.match.world OpenUI.*(CustomTransformers_GFX|CharacterSelect_GFX) | PASS | 0 hits after line 715: - |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect chassis-kept | PASS | picks [('true', 'Truck3')] |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.played 2 | PASS | moved 38.08 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect level returned PartyLobby | PASS | level=PartyLobby |

## camp: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 90 s wall |
| reaches the end | PASS | 11 / 11 checkpoints |
| checkpoint main | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuMain_mc |
| checkpoint campaign.root | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuCampaign_mc |
| checkpoint campaign.difficulty | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.difficultySelectLoader_mc.difficultySelect_mc.difficultyMenu_mc |
| checkpoint campaign.chapters | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.campaignSelectLoader.campaignSelect_mc.selectMap_mc.missionMenu_mc |
| checkpoint campaign.chapter1.checkpoints | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.campaignSelectLoader.campaignSelect_mc.selectMap_mc.checkpointMenu_mc |
| checkpoint campaign.crashsite.charselect | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.charSelect_mc.charSelectMenu_mc |
| checkpoint campaign.loading.done | PASS | ui WaitingOnGameStart, level Match, movie -, owner WaitingOnGameStart: |
| checkpoint ingame.spawned | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.played | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.shot | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint flow.end | PASS | ui InGame, level Match, movie -, owner game |
| expect mode SOLO | PASS | match mode SOLO |
| expect log gamelobby.soloStart | PASS | log line 522 |
| expect nolog-after FLOW.loading.match.world OpenUI.*(CustomTransformers_GFX|CharacterSelect_GFX) | PASS | 0 hits after line 538: - |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.played 2 | PASS | moved 6.44 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect shots ingame.played ingame.shot | PASS | shots 0 -> 3 |
