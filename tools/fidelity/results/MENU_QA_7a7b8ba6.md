# Menu-flow QA gate - 7a7b8ba6 (USER_PLAYTEST_2026-10-23b section 43) - 2026-10-24 00:52

One exe (work/ab/fast_7a7b8ba, head 7a7b8ba6: frozen-pawn fix f0d500cc, both QA hook halves, the Solo start fix, the campaign pick carried into the map), windowed 00:46:56-00:51:39, muted, NOINTERACT, a seeded fresh profile per flow, the real menus (the Frontend's verified clickclip / ui steps), gameplay through buttonhold (the PC keyboard path). Runner tools/fidelity/menu-qa.ps1, checker tools/fidelity/menu_qa_check.py, flows tools/fidelity/menu-qa/{mp,esc,camp}.txt; logs + screenshots work/menuqa/7a7b8ba/<flow>/.

| Flow | Verdict | Headline |
|---|---|---|
| MP (Private Match) | **all measured checks PASS**; UNKNOWN = 5 steps not scriptable yet | Game Settings row opens + closes with its own input owner; Choose Character after the load; spawn; **moved 34.9 m, jitter 0** (the frozen-pawn P0 is fixed on the real input path); 3 shots; robot -> vehicle; pause / resume restores control; quit -> party lobby |
| Escalation (Host) | **PASS** | Escalation lobby + Host Options + Create Game; character picked in the lobby and kept (Truck3); TnUIControllerSurvival; **no character screen after the load**; moved 37.9 m, jitter 0; return to the party lobby |
| Campaign (Solo, Chapter I, Crash Site) | **PASS** | calibration -> Solo -> difficulty -> chapters -> checkpoints -> trio -> load (soloStart) -> InGame; no character screen in the map; moved 6.4 m, jitter 0; 3 shots |

Not covered yet (UNKNOWN rows, not passes): Game Settings controls inside the row, team switch (Manual balancing), countdown cancel (not implemented), the Scout pick on the wheel, Pause > Change Character > back. A note on the checker: the frontend's mode tag for Escalation is 'SV' (TnGameLobbyGameSurvival) - the first evaluation's 'mode ESC' FAIL was the checker's expectation, corrected to SV|ESC plus the log line 'match: launched ... ESC' (no product issue; no rerun).

Integration's note: the sim-side flow harness saw the bot-driven human not moving / shooting / transforming in 7 / 10 on this head; here the real input path (buttonhold) moves, shoots and transforms in every flow.

## mp: **UNKNOWN**

| Check | Result | Detail |
|---|---|---|
| pending step | UNKNOWN | Game Settings changes inside the row (mode / bots / map) - the row is opened and closed here; its controls follow |
| pending step | UNKNOWN | team switch (X, only with Team Balancing = Manual) (#23) |
| pending step | UNKNOWN | countdown cancel - not implemented (the base game cannot cancel; the user asked for it, #25) |
| pending step | UNKNOWN | Choose Character clip paths to pick Scout (the #16 / #28 class); the default pick is accepted meanwhile |
| pending step | UNKNOWN | Pause > Change Character > back (#17 / #29) - the clip paths follow from the Frontend |
| exit | PASS | exit 0, 108 s wall |
| reaches the end | PASS | 15 / 15 checkpoints |
| checkpoint main | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuMain_mc |
| checkpoint partylobby | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint gamelobby | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint gamelobby.settings | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.wfcGameSettings_mc.subMenu_mc |
| checkpoint gamelobby.after_settings | PASS | ui InLobby, level GameLobby, movie UI_GFxLobbies_p.GameLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint charselect | PASS | ui WaitingOnGameStart, level Match, movie UI_GFxCustomize_p.CustomTransformers_GFX_1, owner WaitingOnGameStart:UI_GFxCustomize_p.CustomTransformers_GFX_1 |
| checkpoint ingame.spawned | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.moved | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.shot | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint ingame.transformed | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint pause | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint resumed | PASS | ui InGame, level Match, movie -, owner game |
| checkpoint quitbox | PASS | ui Paused, level Match, movie UI_GFxPause_p.PauseMenu_GFX_1, owner Paused:UI_GFxPause_p.PauseMenu_GFX_1 |
| checkpoint lobby.returned | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| checkpoint flow.end | PASS | ui InLobby, level PartyLobby, movie UI_GFxLobbies_p.PartyLobby_GFX_1, owner _level0.lobby_mc.menuAnchor_mc.menu_mc |
| expect mode TDM | PASS | match mode TDM |
| expect chassis-kept | PASS | picks [('true', 'Car4')] |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.moved 2 | PASS | moved 34.89 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect shots ingame.moved ingame.shot | PASS | shots 0 -> 3 |
| expect form ingame.shot ingame.transformed | PASS | form robot -> vehicle |
| expect state resumed InGame | PASS | uiState=InGame |
| expect level lobby.returned PartyLobby | PASS | level=PartyLobby |

## esc: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 92 s wall |
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
| expect log match: launched \S+ ESC | PASS | log line 1463 |
| expect log controller=TnUIControllerSurvival | PASS | log line 1519 |
| expect nolog-after FLOW.loading.match.world OpenUI.*(CustomTransformers_GFX|CharacterSelect_GFX) | PASS | 0 hits after line 719: - |
| expect no-charselect-after-ingame | PASS | 0 re-entries into CharacterSelect after the first InGame |
| expect chassis-kept | PASS | picks [('true', 'Truck3')] |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.played 2 | PASS | moved 37.86 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect level returned PartyLobby | PASS | level=PartyLobby |

## camp: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 83 s wall |
| reaches the end | PASS | 12 / 12 checkpoints |
| checkpoint main | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.menuAnchor_mc.menuMain_mc |
| checkpoint campaign.calibration | PASS | ui FrontEnd, level FrontEnd, movie UI_GFxFrontEnd_p.FrontEnd_GFX_1, owner _level0.brightnessMenuLoader_mc.brightnessMenu_mc.brightnessMenu_mc |
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
| expect log gamelobby.soloStart | PASS | log line 565 |
| expect nolog-after FLOW.loading.match.world OpenUI.*(CustomTransformers_GFX|CharacterSelect_GFX) | PASS | 0 hits after line 581: - |
| expect state ingame.spawned InGame | PASS | uiState=InGame |
| expect moved ingame.spawned ingame.played 2 | PASS | moved 6.44 m |
| expect still ingame.spawned 2 | PASS | jitter=0 |
| expect shots ingame.played ingame.shot | PASS | shots 0 -> 3 |
