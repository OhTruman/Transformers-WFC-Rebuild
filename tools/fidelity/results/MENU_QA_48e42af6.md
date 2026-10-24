# Menu-flow QA - 48e42af6 (final build), Multiplayer rerun - 2026-10-24 10:45

One exe (work/ab/fast_48e42af = a2e9fbd9 + the Frontend's countdown-cancel layout fix in GfxPresenter), windowed 241 s, muted, NOINTERACT, a seeded fresh profile, the real menus. Escalation and Campaign PASS on a2e9fbd9 (results/MENU_QA_a2e9fbd9.md); the delta is frontend-only (GfxPresenter), so those stand. Logs + 17 shots work/menuqa/48e42af/mp/.

**Multiplayer: PASS, every row** (section 43 flow 1 end to end): Create a Character list + overview (#20 shot 02c) and back to the party lobby; Game Settings bots 4 + 4 (#27) and Back closes the page; map row; Team row; Start -> countdown -> Back cancels (#25) -> **the menu restored (lobby.countdownCancelMenu restored=true) -> Start again starts the match**; Choose Character -> Scout (Car4); the PreGameCountdown PlayerList (#27 shot 10b); spawn; moved 20.1 m, jitter 0; 3 shots; robot -> vehicle; Pause -> Change Character opens the MP screen (customSelection) -> Back -> pause menu -> Back -> InGame with input = game, the pawn moves again; quit -> party lobby.

Checker fix in this commit: nav.check carries inputOwner twice (the GFx clip, then the state owner); the owner expectation now matches either (it read only the last).

## mp: **PASS**

| Check | Result | Detail |
|---|---|---|
| exit | PASS | exit 0, 241 s wall |
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
| expect log FLOW.lobby.bots.field=autobot.autobot=[1-9] | PASS | log line 795 |
| expect log FLOW.lobby.bots.field=decepticon.*decepticon=[1-9] | PASS | log line 841 |
| expect log BotsAutobot=[1-9].*BotsDecepticon=[1-9]|BotsDecepticon=[1-9].*BotsAutobot=[1-9] | PASS | log line 931 |
| expect state cac.back InLobby | PASS | uiState=InLobby |
| expect level cac.back PartyLobby | PASS | level=PartyLobby |
| expect log FLOW.gamelobby.map.mapId= | PASS | log line 581 |
| expect log FLOW.lobby.team.team= | PASS | log line 918 |
| expect log FLOW.gamelobby.countdown.*state=cancelled.by=Back | PASS | log line 954 |
| expect log FLOW.lobby.countdownCancelMenu.restored=true | PASS | log line 956 |
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
