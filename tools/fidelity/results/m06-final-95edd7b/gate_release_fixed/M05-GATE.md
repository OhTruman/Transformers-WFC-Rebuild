# Milestone 05 end-to-end gate (Release)

- target: ref=95edd7b34107e6bab5a44d6cbbc191db46209e8a; sha=95edd7b34107e6bab5a44d6cbbc191db46209e8a; built=2026-10-04T14:49:41
- exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m6int\build-release\bin\wfc_rebuild.exe`
- runs: R1, R5, R7; cycles 8; 2026-10-04 16:20

**PASS 68 / FAIL 0 / KNOWN 1 / INFO 1 / SKIP 0 / HUMAN-CHECK 19**

## DATA

| status | check | owner | evidence |
|---|---|---|---|
| PASS | manifest.frontend_flow | AssetTools | AssetTools manifests\frontend_flow.json |
| PASS | manifest.frontend_gfx | AssetTools | AssetTools manifests\frontend_gfx.json |
| PASS | manifest.frontend_modes | AssetTools | AssetTools manifests\frontend_modes.json |
| PASS | manifest.frontend_maps | AssetTools | AssetTools manifests\frontend_maps.json |
| PASS | manifest.frontend_loading | AssetTools | AssetTools manifests\frontend_loading.json |
| PASS | manifest.frontend_audio | AssetTools | AssetTools manifests\frontend_audio.json |
| PASS | manifest.frontend_localization | AssetTools | AssetTools manifests\frontend_localization.json |
| PASS | manifest.mp_map_catalog | AssetTools | AssetTools manifests\mp_map_catalog.json |
| PASS | frontend_unit_tests | Frontend | wfc_frontend_tests exit 0; failing:  |

## STATE

| status | check | owner | evidence |
|---|---|---|---|
| PASS | flow_trace | Frontend | 855 events, exit 0; script timeout 0 |
| PASS | boot_map | Frontend | boot map UI_FrontEnd_m; watchedIntro False (fresh profile expects False) |
| PASS | boot_loading_kind | Frontend | first loading screen closes as InitialStartup (RE 1.1 [LoadingMovie] InitialStartupFileName) |
| PASS | intro_chain_order | Frontend | movie.play: Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro (expected Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro) |
| PASS | intro_played_to_end | Frontend | movie.finished: Logo_Activision pos 10.401556 skipped False; Logo_Hasbro pos 13.200936 skipped False; Logo_HighMoon pos 7.480061 skipped False; FMV_intro pos 128.680376 skipped False |
| PASS | ui_transitions_legal | Frontend | 8 UI state changes; outside the TnUIController table:  |
| PASS | movie_for.FrontEnd | Frontend | expected UI_GFxFrontEnd_p.FrontEnd_GFX_1 opened (RE 1.4) |
| PASS | movie_for.InLobby@PartyLobby | Frontend | expected UI_GFxLobbies_p.PartyLobby_GFX_1 opened (RE 1.4) |
| PASS | movie_for.InLobby@GameLobby | Frontend | expected UI_GFxLobbies_p.GameLobby_GFX_1 opened (RE 1.4) |
| PASS | movie_for.Paused | Frontend | expected UI_GFxPause_p.PauseMenu_GFX_1 opened (RE 1.4) |
| PASS | no_duplicate_screens | Frontend | GFx movies opened again while open:  |
| PASS | key_multiplayer | Frontend | Start, Down, A on the main menu -> Online.OpenPartyLobby(GTS_TeamGame) (BLK A2: GTS_TeamGame) |
| PASS | key_mode_focus_tdm | Frontend | first EditGameMode on entering the mode list: TDM (BLK B2: initial focus TDM, EditGameMode on focus) |
| PASS | host_option_defaults | Frontend | Create Game with untouched rows wrote: AutobalanceTeams=Autobalanced, MapSelectionMethod=Host's Choice, TimeLimit=15 minutes, PointsToWin=40 (BLK B4: Autobalanced / Host's Choice / 15 minutes / 40) |
| PASS | lobby_default_map | Frontend | game lobby opened: server pick 508, then the lobby movie pushed SetSelectedMapID(501); expected the first selectable map in TransLevels order (BLK C4: index 0; rebuild-selectable maps 501,502,503,504,507,508,509,510 -> 501). Original dump: Seed of Corruption 501 is index 0; the rebuild only lists maps with runtime data |
| PASS | lobby_countdown | Frontend | A on Start Game -> countdown 10 s, final countdown after 10.00 s (BLK C8: 10 s) |
| PASS | match_url | Frontend | MP_IAC_Seed_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=501; mismatched keys:  (RE 3.1 / BLK D1) |
| PASS | match_loading_text | Frontend | title 'Team Deathmatch' message 'in Seed Of Corruption' tips 3 |
| PASS | quit_to_main_menu | Frontend | travel UI_FrontEnd_m; returned uiState FrontEnd; intro replayed after return: 0 |
| PASS | return_lands_on_main_menu | Frontend | frontend after the match resembles the main menu (diff 2.97) rather than Press START (diff 7.82) (Online.ShouldShowStartScreen: true only until ShowDeviceSelectionUI, Frontend 76b8287 from the recovered script) |
| PASS | lobby_after_score_limit | Frontend | lobby after match 1: level GameLobby mode TDM mapId 501 (BLK C7: selection index 0 = Streets in the rebuild) |

## PRESENTED

| status | check | owner | evidence |
|---|---|---|---|
| PASS | intro_movies_decoded | Frontend | intro movies with a decoded first frame: 4/4; finished events 4; unavailable 0  |
| HUMAN | intro_on_screen | Frontend | frames at 6 s / 14 s after boot: near-black 91% / 7%, mean luma 5.9 / 44.6, change 45.65. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check |
| HUMAN | screen.title |  | b_title.bmp: near-black 1%, mean luma 77.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.main_menu |  | c_mainmenu.bmp: near-black 1%, mean luma 78.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.party_lobby |  | d_party.bmp: near-black 0%, mean luma 37.3 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.mode_list |  | e_modes.bmp: near-black 0%, mean luma 37 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.host_options |  | f_hostoptions.bmp: near-black 0%, mean luma 37.9 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.game_lobby |  | g_gamelobby.bmp: near-black 0%, mean luma 39.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.lobby_countdown |  | h_countdown.bmp: near-black 0%, mean luma 39.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.loading_screen |  | i_loading.bmp: near-black 13%, mean luma 24.9 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.match_pre_game |  | j_match_pending.bmp: near-black 1%, mean luma 49.1 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.pause_menu |  | m_pause.bmp: near-black 3%, mean luma 29.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.returned_frontend |  | n_return.bmp: near-black 1%, mean luma 77.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| PASS | in_game_world_visible | Integration/Rendering/Frontend | in-game frames (trace: InGame, HUD on): near-black 2% / 0%, mean luma 50.8 / 53.7 |
| PASS | in_game_live | Integration | frames 8 s apart with walk + turn input differ by 31.04 luma (frozen view if ~0) |
| HUMAN | pause_menu_drawn | Frontend | pause frame differs from the game frame by 35.01 luma (menu over the running match; backdrop blend PARTIAL per Frontend) |
| HUMAN | pause_input_focus | Frontend | hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop) |
| HUMAN | lifecycle.b_m1_spectating | Frontend | b_m1_spectating.bmp: near-black 1%, mean luma 60.5 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.c_m1_endstats | Frontend | c_m1_endstats.bmp: near-black 5%, mean luma 31.4 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.d_lobby_after1 | Frontend | d_lobby_after1.bmp: near-black 0%, mean luma 37.1 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.e_m2_ingame | Frontend | e_m2_ingame.bmp: near-black 2%, mean luma 55.5 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.g_frontend | Frontend | g_frontend.bmp: near-black 1%, mean luma 77.8 - drawn; look is a human check (R7_sheet.png) |

## MATCH

| status | check | owner | evidence |
|---|---|---|---|
| PASS | default_rules_tdm | Gameplay/Frontend | default host options -> Gameplay goal 40 / time 900 s (BLK B4/D2: 40 / 900) |
| PASS | pending_countdown | Gameplay/Frontend | match level -> InGame after 10.16 s (BLK D4 / RE 5.2: PendingMatch 10 s, PreGameCountdown shown yes) |
| PASS | no_spawn_before_start | Gameplay | first spawn log line 1441 vs match level begin / InGame (BLK E5: no spawns in PendingMatch) |
| PASS | spawn_team_cluster | Gameplay | spawned at TnTeamPlayerStart_15214 in TnSpawnCluster_11813, team 0 (RE 5.3 / BLK E6: a faction's first cluster is an InitialSpawn cluster, locked 15 s; Streets Autobots 7810 / Decepticons 4159; this map's InitialSpawn clusters TnSpawnCluster_10766, TnSpawnCluster_11813) |
| PASS | spawn_class | Gameplay | start actor TnTeamPlayerStart_15214 (TnTeamPlayerStart in TDM) |
| PASS | lifecycle_three_matches | Gameplay/Frontend/Integration | 3 matches to the score limit (5) through the shipped lobby, then pause -> quit -> frontend: completed |
| PASS | death_to_spectating | Gameplay/Integration | 8 local deaths; Spectating UI after 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00 s (BLK E7: MinRespawnDelay 3.0 s -> UI event 4) |
| PASS | respawn_delay | Gameplay | respawn after 4.98, 4.99, 4.98, 4.98, 4.98, 4.98, 4.98, 4.98 s (BLK E7 / RE 5.6: wave 5.0 s) |
| PASS | respawn_returns_to_ingame | Integration/Frontend | Spectating -> InGame at the respawn (8 of 8; UI event 5) |
| PASS | kill_scores_team | Gameplay | per match, team score lines step 1, 2, 3 ... from 0 (BLK F1: kill +1 team): match 1: t0=1 t1=1 t0=2 t1=2 t0=3 t1=3 t0=4 t1=4 t0=5 / match 2: t0=1 t1=1 t0=2 t1=2 t0=3 t1=3 t0=4 t1=4 t0=5 / match 3: t0=1 |
| PASS | score_limit_end | Gameplay | matches ending on the score limit: 2 (match 1: score_limit winner 0 top 5-4; match 2: score_limit winner 0 top 5-4; match 3:  winner  top 1-); a team reached exactly 5 (BLK F3 CheckScore) |
| PASS | second_match_scores_reset | Gameplay | match 2 score lines start again at 1: 't0=1 t1=1 t0=2 t1=2 t0=3 t1=3 t0=4 t1=4 t0=5' (BLK F6: a new match is a fresh level) |
| PASS | match_over_15s_return | Gameplay/Integration | GameEnded -> lobby travel after 15.0, 15.0 s; URLs keep MapId: 501,501 (BLK F4/F6) |
| PASS | end_and_respawn_screens | Frontend/Integration | EndGameStats opened 4x, MultiplayerRespawn 16x, HUD hidden 2x (BLK E7/F4) |
| PASS | respawn_starts | Gameplay | local spawns: TnTeamPlayerStart_15214, TnTeamPlayerStart_6517, TnTeamPlayerStart_14561, TnTeamPlayerStart_10418, TnTeamPlayerStart_12030 |

## AUDIO

| status | check | owner | evidence |
|---|---|---|---|
| PASS | movie_mute | Systems/Frontend | audio.moviePlaying events 10: playing=True / playing=False / playing=True / playing=False / playing=True / playing=False (CINE_MUTE_FOR_BINK while a Bink plays: Systems M06 MovieMixerPreset) |
| PASS | music.FrontEnd | Systems | 2 visits, music plays: BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01,BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01 (expected FRONTEND_MX_ORBIT_01 once per visit; package prefix accepted) |
| PASS | music.PartyLobby | Systems | 1 visits, music plays: BL_LVL_HUD_INTERFACE.MP_PARTY_LOBBY_MX (expected MP_PARTY_LOBBY_MX once per visit; package prefix accepted) |
| PASS | music.GameLobby | Systems | 1 visits, music plays: BL_LVL_HUD_INTERFACE.MP_LOBBY_MX (expected MP_LOBBY_MX once per visit; package prefix accepted) |
| PASS | no_menu_music_in_match | Systems | frontend / lobby music started during the match:  |
| PASS | match_start_music | Systems/Gameplay | match music during the match: BL_LVL_MP_MX.DM_START; expected BL_LVL_MP_MX.DM_START at the match start (OV A5 TnGameTypeMessage GameTypeMusic, CONFIRMED) |
| PASS | ui_sounds_played | Systems/Frontend | 8 UI sounds requested by the movies (BUTTON_START,BUTTON_DOWN,BUTTON_ACCEPT); without an audio owner (audio=none): 0 |
| PASS | ui_sounds_single | Frontend | same UI sound requested twice within 20 ms: 0 |
| PASS | doubled_sounds_R1 | Systems | cue instances started twice at the same place and moment: 0  |
| PASS | doubled_sounds_lifecycle | Systems | doubled cue starts over 3 matches with deaths / respawns: 0  |
| PASS | audio_released_after_lifecycle | Systems | after each match: voices 0 instances 0 pcm 36.489 / voices 0 instances 0 pcm 36.489 / voices 0 instances 0 pcm 36.489 |

## OWNERSHIP

| status | check | owner | evidence |
|---|---|---|---|
| PASS | launch_request | Frontend | frontend MatchLaunch: mode TDM goal 40 time 900 runtimeDir MP_IAC_Seed team 1 |
| PASS | gameplay_match_settings | Integration/Gameplay | Gameplay launched map MP_IAC_Seed mode TDM goal 40 time 900; frontend selection runtimeDir MP_IAC_Seed mode TDM goal 40 time 900 (RE D1-D3: from the URL) |
| PASS | audio_map_follows_selection | Systems/Integration | Systems loaded level audio 'MP_IAC_Seed' for the selected map MP_IAC_Seed |
| PASS | gameplay_runs_request | Integration/Gameplay | Gameplay: 'launched MP_IAC_Seed TDM (goal 40, time 900 s)' vs frontend request mode TDM goal 40 time 900 |
| INFO | team_consistent | Integration | frontend launch team 1, Gameplay spawn team 0 (BLK E2/E4: offline the lobby assigns no team; the match PickTeam decides) |
| PASS | audio_level | Systems/Integration | Systems AMB level during the match: MP_IAC_Seed |
| PASS | render_map | Rendering | render log lines naming the map: wfc: released map render data (11 meshes, 85 programs, 118 textures) / wfc: released map render data (2 meshes, 53 programs, 52 textures) |
| KNOWN | gorge_not_selectable | AssetTools/Integration | map selector stepped right: selected 502; Gorge selectable False. Integration lists Gorge disabled by design (no AssetTools render export; not validated by Gameplay / Rendering) - not counted as a working map |
| PASS | ui_map_is_the_map_loaded | Integration/Gameplay | UI selected map 502; frontend launch runtimeDir MP_IAC_Berth; Gameplay: match: launched MP_IAC_Berth TDM (goal 40, time 900 s); Systems: ambient: MP_IAC_Berth: 182 level cues, 5 reverb presets, 57 emitters, 20 zones, 25 pools, 0 Kismet audio ops / 0 links (manifests: AssetTools Systems); level after 55 s: Match. FAIL = an owner runs Streets behind a Gorge selection; KNOWN = launch refused cleanly (Gameplay documents launchMatch accepts only MP_IAC_Streets in this milestone) |
| PASS | unsupported_map_handled | Integration | process exit 0; clean exit event 1 |

## LIFETIME

| status | check | owner | evidence |
|---|---|---|---|
| PASS | state_reset_after_return | Frontend | returned snapshot: mode '' mapId -1 team -1 hud False |

