# Milestone 05 end-to-end gate (Release)

- target: ref=1e14900b150349f8da2affb024106f3d7b83cc50; sha=1e14900b150349f8da2affb024106f3d7b83cc50; built=2026-10-04T00:36:46
- exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build-release\bin\wfc_rebuild.exe`
- runs: R1, R2, R3, R4, R5, R6, R7, R8, S; cycles 8; 2026-10-04 06:12

**PASS 100 / FAIL 3 / KNOWN 1 / INFO 4 / SKIP 8 / HUMAN-CHECK 20**

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
| PASS | flow_trace | Frontend | 315 events, exit 0; script timeout 0 |
| PASS | boot_map | Frontend | boot map UI_FrontEnd_m; watchedIntro False (fresh profile expects False) |
| PASS | boot_loading_kind | Frontend | first loading screen closes as InitialStartup (RE 1.1 [LoadingMovie] InitialStartupFileName) |
| PASS | intro_chain_order | Frontend | movie.play: Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro (expected Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro) |
| PASS | intro_played_to_end | Frontend | movie.finished: Logo_Activision pos 10.400077 skipped False; Logo_Hasbro pos 13.200030 skipped False; Logo_HighMoon pos 7.480086 skipped False; FMV_intro pos 128.680048 skipped False |
| PASS | ui_transitions_legal | Frontend | 8 UI state changes; outside the TnUIController table:  |
| PASS | movie_for.FrontEnd | Frontend | expected UI_GFxFrontEnd_p.FrontEnd_GFX_1 opened (RE 1.4) |
| PASS | movie_for.InLobby@PartyLobby | Frontend | expected UI_GFxLobbies_p.PartyLobby_GFX_1 opened (RE 1.4) |
| PASS | movie_for.InLobby@GameLobby | Frontend | expected UI_GFxLobbies_p.GameLobby_GFX_1 opened (RE 1.4) |
| PASS | movie_for.Paused | Frontend | expected UI_GFxPause_p.PauseMenu_GFX_1 opened (RE 1.4) |
| PASS | no_duplicate_screens | Frontend | GFx movies opened again while open:  |
| PASS | key_multiplayer | Frontend | Start, Down, A on the main menu -> Online.OpenPartyLobby(GTS_TeamGame) (BLK A2: GTS_TeamGame) |
| PASS | key_mode_focus_tdm | Frontend | first EditGameMode on entering the mode list: TDM (BLK B2: initial focus TDM, EditGameMode on focus) |
| PASS | host_option_defaults | Frontend | Create Game with untouched rows wrote: AutobalanceTeams=Autobalanced, MapSelectionMethod=Host's Choice, TimeLimit=15 minutes, PointsToWin=40 (BLK B4: Autobalanced / Host's Choice / 15 minutes / 40) |
| PASS | lobby_default_map | Frontend | game lobby opened: server pick 508, then the lobby movie pushed SetSelectedMapID(508); expected the first selectable map in TransLevels order (BLK C4: index 0; rebuild-selectable maps 501,502,503,504,507,508,509,510 -> 508). Original dump: Seed of Corruption 501 is index 0; the rebuild only lists maps with runtime data |
| PASS | lobby_countdown | Frontend | A on Start Game -> countdown 10 s, final countdown after 10.00 s (BLK C8: 10 s) |
| PASS | match_url | Frontend | MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508; mismatched keys:  (RE 3.1 / BLK D1) |
| PASS | match_loading_text | Frontend | title 'Team Deathmatch' message 'in Streets' tips 3 |
| PASS | quit_to_main_menu | Frontend | travel UI_FrontEnd_m; returned uiState FrontEnd; intro replayed after return: 0 |
| FAIL | return_lands_on_main_menu | Frontend | frontend after the match resembles the main menu (diff 3.14) rather than Press START (diff 0.09) (Online.ShouldShowStartScreen: true only until ShowDeviceSelectionUI, Frontend 76b8287 from the recovered script) |
| PASS | second_launch_profile | Frontend | watchedIntro=True with the first launch's profile |
| PASS | second_launch_skips_intro | Frontend | intro movies on the second launch: 0; title reached |
| PASS | lobby_after_match | Frontend | back in the game lobby; selected map 508 (BLK C7 HIGH: the lobby movie re-sends selector index 0 = first selectable = Streets in the rebuild) |
| SKIP | pc.sku | Frontend | this exe has no PC SKU switch (WFC_PLATFORM) or ui: actions |
| PASS | lobby_after_score_limit | Frontend | lobby after match 1: level GameLobby mode TDM mapId 508 (BLK C7: selection index 0 = Streets in the rebuild) |

## PRESENTED

| status | check | owner | evidence |
|---|---|---|---|
| PASS | intro_movies_decoded | Frontend | intro movies with a decoded first frame: 4/4; finished events 4; unavailable 0  |
| HUMAN | intro_on_screen | Frontend | frames at 6 s / 14 s after boot: near-black 67% / 6%, mean luma 38.4 / 46.7, change 62.68. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check |
| HUMAN | screen.title |  | b_title.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.main_menu |  | c_mainmenu.bmp: near-black 87%, mean luma 9.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.party_lobby |  | d_party.bmp: near-black 64%, mean luma 11 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.mode_list |  | e_modes.bmp: near-black 65%, mean luma 10.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.host_options |  | f_hostoptions.bmp: near-black 65%, mean luma 11.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.game_lobby |  | g_gamelobby.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.lobby_countdown |  | h_countdown.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.loading_screen |  | i_loading.bmp: near-black 14%, mean luma 23 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.match_pre_game |  | j_match_pending.bmp: near-black 0%, mean luma 46.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.pause_menu |  | m_pause.bmp: near-black 0%, mean luma 69.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.returned_frontend |  | n_return.bmp: near-black 94%, mean luma 6.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| PASS | in_game_world_visible | Integration/Rendering/Frontend | in-game frames (trace: InGame, HUD on): near-black 0% / 10%, mean luma 28.8 / 18.6 |
| PASS | in_game_live | Integration | frames 8 s apart with walk + turn input differ by 19.92 luma (frozen view if ~0) |
| HUMAN | pause_menu_drawn | Frontend | pause frame differs from the game frame by 54.34 luma (menu over the running match; backdrop blend PARTIAL per Frontend) |
| HUMAN | pause_input_focus | Frontend | hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop) |
| HUMAN | end_stats_screen | Frontend | end-of-match screen: near-black 21% - results layout (columns Level / Name / Score / Kills / Deaths, BLK F5) is a human check |
| HUMAN | lifecycle.b_m1_spectating | Frontend | b_m1_spectating.bmp: near-black 0%, mean luma 21.8 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.c_m1_endstats | Frontend | c_m1_endstats.bmp: near-black 0%, mean luma 49.9 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.d_lobby_after1 | Frontend | d_lobby_after1.bmp: near-black 57%, mean luma 15 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.e_m2_ingame | Frontend | e_m2_ingame.bmp: near-black 0%, mean luma 25.9 - drawn; look is a human check (R7_sheet.png) |
| HUMAN | lifecycle.g_frontend | Frontend | g_frontend.bmp: near-black 94%, mean luma 6.7 - drawn; look is a human check (R7_sheet.png) |

## MATCH

| status | check | owner | evidence |
|---|---|---|---|
| PASS | default_rules_tdm | Gameplay/Frontend | default host options -> Gameplay goal 40 / time 900 s (BLK B4/D2: 40 / 900) |
| PASS | pending_countdown | Gameplay/Frontend | match level -> InGame after 10.15 s (BLK D4 / RE 5.2: PendingMatch 10 s, PreGameCountdown shown yes) |
| PASS | no_spawn_before_start | Gameplay | first spawn log line 1151 vs match level begin / InGame (BLK E5: no spawns in PendingMatch) |
| PASS | spawn_team_cluster | Gameplay | spawned at TnTeamPlayerStart_10894 in TnSpawnCluster_7810, team 0 (RE 5.3 / BLK E6: initial clusters Autobots 7810, Decepticons 4159, locked 15 s) |
| PASS | spawn_class | Gameplay | start actor TnTeamPlayerStart_10894 (TnTeamPlayerStart in TDM) |
| PASS | full_lifecycle_completed | Gameplay/Frontend/Integration | match to the time limit -> end -> lobby -> second match -> frontend; completed |
| PASS | time_limit_passed_to_rules | Gameplay/Integration | launched MP_IAC_Streets TDM (goal 40, time 600 s) |
| PASS | time_limit_end | Gameplay | InGame -> GameEnded after 600.0 s (TimeLimit 600 s; BLK F3 RemainingTime 0 -> EndGame) |
| PASS | time_limit_winner | Gameplay | EndGame reason "" score 0-0 winner team 0 player -1; populated teams 0. TnVersusGame.PickWinningTeam: a team wins if it scores more OR the other team is empty; equal scores with both teams populated = no winner, no overtime (BLK F3) -> expected winner 0 |
| INFO | protocol_end_reason | Integration | MATCH protocol line: end reason=other winner=0 t=600 (gameplay reason ) (RUNTIME-EVENTS expects reason=time_limit for a clock end; Integration maps Gameplay's empty EndGame reason to 'other' - validation-hook nit) |
| PASS | match_end_ui | Gameplay/Frontend | GameEnded True; HUD hidden 1; EndGameStats opened 2 (BLK F4: event 9, HUD hidden, EndGameStats_GFX) |
| PASS | match_over_return_to_lobby | Gameplay/Frontend | travel UI_Lobby_m?Game=TransContent.TnGameLobbyGameTeam?GameModeTag=TDM?GameTeamStatus=3?MaxPlayers=10?IconicMode=0?MapId=508?listen after 15.0 s (BLK F4/F6: MatchOver 15 s -> ReturnToGameLobby UI_Lobby_m?...?MapId=) |
| PASS | second_match | Frontend/Gameplay | second launch: mode TDM goal 40 time 600 (host options persist for the session, HIGH: TimeLimit 600 expected) |
| PASS | second_match_fresh | Gameplay | match begins 2, local spawns 2 (BLK F6: a new match is a fresh level load) |
| SKIP | time_announcements | Gameplay/Systems | announcement lines: 2 (RE: at 120 / 60 / 30 s remaining; audible check in HUMAN-CHECK) |
| PASS | dm_spawn_class | Gameplay | DM local spawn: TnFreeForAllPlayerStart_11647 ( team 255) |
| PASS | lifecycle_three_matches | Gameplay/Frontend/Integration | 3 matches to the score limit (5) through the shipped lobby, then pause -> quit -> frontend: completed |
| PASS | death_to_spectating | Gameplay/Integration | 8 local deaths; Spectating UI after 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00 s (BLK E7: MinRespawnDelay 3.0 s -> UI event 4) |
| PASS | respawn_delay | Gameplay | respawn after 4.98, 4.98, 4.98, 4.98, 4.98, 4.98, 4.98, 4.98 s (BLK E7 / RE 5.6: wave 5.0 s) |
| PASS | respawn_returns_to_ingame | Integration/Frontend | Spectating -> InGame at the respawn (8 of 8; UI event 5) |
| PASS | kill_scores_team | Gameplay | per match, team score lines step 1, 2, 3 ... from 0 (BLK F1: kill +1 team): match 1: t0=1 t1=1 t0=2 t1=2 t0=3 t1=3 t0=4 t1=4 t0=5 / match 2: t0=1 t1=1 t0=2 t1=2 t0=3 t1=3 t0=4 t1=4 t0=5 / match 3: t0=1 |
| PASS | score_limit_end | Gameplay | matches ending on the score limit: 2 (match 1: score_limit winner 0 top 5-4; match 2: score_limit winner 0 top 5-4; match 3:  winner  top 1-); a team reached exactly 5 (BLK F3 CheckScore) |
| PASS | second_match_scores_reset | Gameplay | match 2 score lines start again at 1: 't0=1 t1=1 t0=2 t1=2 t0=3 t1=3 t0=4 t1=4 t0=5' (BLK F6: a new match is a fresh level) |
| PASS | match_over_15s_return | Gameplay/Integration | GameEnded -> lobby travel after 15.0, 15.0 s; URLs keep MapId: 508,508 (BLK F4/F6) |
| PASS | end_and_respawn_screens | Frontend/Integration | EndGameStats opened 4x, MultiplayerRespawn 16x, HUD hidden 2x (BLK E7/F4) |
| PASS | respawn_starts | Gameplay | local spawns: TnTeamPlayerStart_10894, TnTeamPlayerStart_12481, TnTeamPlayerStart_10925, TnTeamPlayerStart_1753, TnTeamPlayerStart_1276 |

## AUDIO

| status | check | owner | evidence |
|---|---|---|---|
| PASS | movie_mute | Systems/Frontend | audio.moviePlaying events 10: playing=True / playing=False / playing=True / playing=False / playing=True / playing=False (CINE_MUTE_FOR_BINK while a Bink plays: Systems M06 MovieMixerPreset) |
| PASS | music.FrontEnd | Systems | 2 visits, music plays: BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01,BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01 (expected FRONTEND_MX_ORBIT_01 once per visit; package prefix accepted) |
| PASS | music.PartyLobby | Systems | 1 visits, music plays: BL_LVL_HUD_INTERFACE.MP_PARTY_LOBBY_MX (expected MP_PARTY_LOBBY_MX once per visit; package prefix accepted) |
| PASS | music.GameLobby | Systems | 1 visits, music plays: BL_LVL_HUD_INTERFACE.MP_LOBBY_MX (expected MP_LOBBY_MX once per visit; package prefix accepted) |
| PASS | no_menu_music_in_match | Systems | frontend / lobby music started during the match:  |
| FAIL | match_start_music | Systems/Gameplay | match music during the match: none; expected BL_LVL_MP_MX.DM_START at the match start (OV A5 TnGameTypeMessage GameTypeMusic, CONFIRMED) |
| PASS | ui_sounds_played | Systems/Frontend | 8 UI sounds requested by the movies (BUTTON_START,BUTTON_DOWN,BUTTON_ACCEPT); without an audio owner (audio=none): 0 |
| PASS | ui_sounds_single | Frontend | same UI sound requested twice within 20 ms: 0 |
| PASS | doubled_sounds_R1 | Systems | cue instances started twice at the same place and moment: 0  |
| PASS | doubled_sounds_full_match | Systems | doubled cue starts over a 10-minute match with constant fire: 0  |
| PASS | voices_stable_in_match | Systems | live cue instances 25% / 90% through the match: 53 / 53; backend voices 55 / 54; PCM 97.2 / 97.2 MB |
| PASS | map_audio_released_each_unload | Systems | after each of 8 unloads (voices / instances / level cues / PCM MB): 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489; pre-load baseline PCM 36.489 MB (Systems guarantee: 0 / 0 / 0 / baseline) |
| PASS | map_audio_loaded_each_match | Systems | map audio at each match load: level MP_IAC_Streets, level cues 32, PCM 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 / 97.179 MB |
| PASS | menu_music_resumes | Systems/Frontend | frontend music starts: 9 for 8 returns (+ boot) |
| PASS | no_menu_music_in_matches | Systems | frontend / lobby music starts inside 8 matches: 0 (game-type DM_* music excluded: it belongs to the match, OV A5) |
| PASS | no_stale_reverb | Systems | after 8 returns, the next reverb change still starts from a map preset (stale map reverb): 0 |
| PASS | doubled_sounds_cycles | Systems | doubled cue starts over 8 matches: 0  |
| PASS | doubled_sounds_lifecycle | Systems | doubled cue starts over 3 matches with deaths / respawns: 0  |
| PASS | audio_released_after_lifecycle | Systems | after each match: voices 0 instances 0 pcm 36.489 / voices 0 instances 0 pcm 36.489 / voices 0 instances 0 pcm 36.489 |

## OWNERSHIP

| status | check | owner | evidence |
|---|---|---|---|
| PASS | launch_request | Frontend | frontend MatchLaunch: mode TDM goal 40 time 900 runtimeDir MP_IAC_Streets team 1 |
| PASS | gameplay_match_settings | Integration/Gameplay | Gameplay launched map MP_IAC_Streets mode TDM goal 40 time 900; frontend selection runtimeDir MP_IAC_Streets mode TDM goal 40 time 900 (RE D1-D3: from the URL) |
| PASS | audio_map_follows_selection | Systems/Integration | Systems loaded level audio 'MP_IAC_Streets' for the selected map MP_IAC_Streets |
| PASS | gameplay_runs_request | Integration/Gameplay | Gameplay: 'launched MP_IAC_Streets TDM (goal 40, time 900 s)' vs frontend request mode TDM goal 40 time 900 |
| INFO | team_consistent | Integration | frontend launch team 1, Gameplay spawn team 0 (BLK E2/E4: offline the lobby assigns no team; the match PickTeam decides) |
| PASS | audio_level | Systems/Integration | Systems AMB level during the match: MP_IAC_Streets |
| PASS | render_map | Rendering | render log lines naming the map: glb: F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/world.glb -> 2076144 verts, 1983988 tris, bounds [-4813.1 -3295.6 -4745.4]..[4135.4 5645.6 4171.8] / glb: F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/collision_pawn.glb -> 75742 verts, 101519 tris, bounds [-163.8 -769.2 -771.0]..[590.7 -405.4 -125.0] |
| PASS | host_option_to_rules | Frontend | Time Limit row set to '10 minutes' with the keys; match.launch timeLimit 600; URL TimeLimit 600.00 |
| KNOWN | gorge_not_selectable | AssetTools/Integration | map selector stepped right: selected 508; Gorge selectable False. Integration lists Gorge disabled by design (no AssetTools render export; not validated by Gameplay / Rendering) - not counted as a working map |
| PASS | ui_map_is_the_map_loaded | Integration/Gameplay | UI selected map 508; frontend launch runtimeDir MP_IAC_Streets; Gameplay: match: launched MP_IAC_Streets TDM (goal 40, time 900 s); Systems: ambient: MP_IAC_Streets: 32 level cues, 10 reverb presets, 70 emitters, 9 zones, 11 pools, 0 Kismet audio ops / 0 links (manifests: AssetTools Systems); level after 55 s: Match. FAIL = an owner runs Streets behind a Gorge selection; KNOWN = launch refused cleanly (Gameplay documents launchMatch accepts only MP_IAC_Streets in this milestone) |
| PASS | unsupported_map_handled | Integration | process exit 0; clean exit event 1 |
| PASS | ui_mode_is_the_mode_run | Integration/Gameplay | mode list Down -> DM; launch mode DM goal 20; loading title 'Deathmatch'; Gameplay: DM begin (goal 20, time limit 900 s, countdown 10 s) / launched MP_IAC_Streets DM (goal 20, time 900 s); spawn: TnFreeForAllPlayerStart_11647 ( team 255) |

## LIFETIME

| status | check | owner | evidence |
|---|---|---|---|
| PASS | state_reset_after_return | Frontend | returned snapshot: mode '' mapId -1 team -1 hud False |
| PASS | cycles_completed | Integration | 8/8 frontend -> Streets TDM (15 s of walking, turning, firing, boosting) -> frontend cycles; exit 0 |
| PASS | memory_stabilizes |  | private MB after each unload: 2463.004 > 2447.062 > 2474.27 > 2537.523 > 2492.926 > 2491.344 > 2490.492 > 2504.637 (slope 5.7 MB/cycle after cycle 1); at the frontend after each return: 1377.5 > 1392 > 1391.6 > 1397 > 1395.7 > 1398.7 > 1408.5 > 1396.2 (slope 1.7).  |
| INFO | memory_peak |  | peak private memory over 8 cycles: 3334.5 MB |
| PASS | handles_threads | Integration | handles at each return: 655 > 667 > 667 > 676 > 684 > 686 > 694 > 704 (slope 6.2); threads: 21 > 19 > 15 > 16 > 16 > 13 > 16 > 17 (slope -0.2) |
| PASS | gl_release_per_return | Rendering | GL objects released at each return: textures=72 buffers=0 framebuffers=0 renderbuffers=0 vertexArrays=0 programs=0 |
| PASS | audio_pcm_per_match | Systems | peak decoded PCM per match: 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 > 97.2 MB; peak live instances per match: 68 > 68 > 67 > 67 > 68 > 68 > 68 > 67 |
| PASS | ui_movies_per_cycle | Frontend | GFx movies opened per cycle (after the first): 6, 6, 6, 6, 6, 6, 6 |
| PASS | no_stale_flow_state | Frontend | returned snapshots with leftover match / lobby state: 0 |
| PASS | one_launch_per_cycle | Gameplay/Integration | Gameplay launches 8, frontend match loads 8 |
| SKIP | counter.map_collision | Integration | no product counter for map_collision across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.map_objects | Integration | no product counter for map_objects across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.particles | Integration | no product counter for particles across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.event_queues | Integration | no product counter for event_queues across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.timers | Integration | no product counter for timers across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |

## SELFTEST

| status | check | owner | evidence |
|---|---|---|---|
| PASS | WFC_TDMTEST | Gameplay | TDM session: hitscan kill, scoring, teammate damage, death mid-transform, 5 s respawn, score limit, MatchOver 15 s, second match reset, HUD state: TDMTEST SUMMARY: 30/30 checks passed (exit 1) |
| PASS | WFC_MATCHTEST | Gameplay | match core: TDM to 40, clock tie with 120/60/30 announcements, DM to 20, suicide / environmental death, respawn: MATCHTEST A after 40 kills: state MatchOver winner team 0 (my team 0) me score 40 kills 40 / MATCHTEST A kill in MatchOver ignored: me score 40 / MATCHTEST A MatchOver -> Returned after 15.5 s; World match active 0 / MATCHTEST B time limit: state MatchOver, score 1-1, winner team -1 (tie = -1), elapsed 125 / MATCHTEST C DM spawn starts TnFreeForAllPlayerStart_11647 / TnFreeForAllPlayerStart_11679 / MATCHTEST C DM: 20 kills -> state MatchOver, p0 score 20 (goal 20), winner team -1 (exit 1) |
| PASS | WFC_XFORMTEST | Gameplay | boost -> robot under-map stress (Gameplay's own): XFORM SUMMARY: 0/76 transforms ended UNDER THE MAP (no floor under the robot, a walkable surface above) or KillZ (worst 0.00 m); 0 ended on a real floor under a low overhang (exit 1) |
| FAIL | WFC_CHAOS | Gameplay | seeded random play from nav points: under the map / KillZ / stuck / inside props: CHAOS SUMMARY: 123 starts x 20 s (147477 ticks, 702 transform presses, 636 jumps, 637 boosts): 1 runs UNDER THE MAP (BSP floor above), 0 KillZ, 2 stuck; 7 runs with the pawn inside / under a prop / CHAOS STUCK from TnHealthPickupFactory_1394 at (221.6 -724.8 -441.3) form ROBOT blockers BlockingVolume_6729,BlockingVolume_7015,StaticMeshCollectionActor_4187,StaticMeshCollectionActor_4320,+2 / CHAOS UNDER-FLOOR from TnTeamPlayerStart_10894: 8 frames, worst 1.42 m at (105.9 -718.2 -632.7) blockers StaticMeshCollectionActor_4919 / CHAOS STUCK from TnTeamPlayerStart_6923 at (368.0 -706.6 -433.9) form ROBOT blockers StaticMeshActor_15698,StaticMeshActor_9634,StaticMeshCollectionActor_3000,StaticMeshCollectionActor_3000,+1 (exit 1) |
| INFO | WFC_VEHTEST | Gameplay | vehicle handling measurements: VEHTEST boost release while turning: driving 0, drift 0.48 s, yaw now follows view (err 0.000 rad) / VEHTEST boost reversal: +1 for 1 s -> 146.6 deg/s; -1: yaw rate crosses zero after 0.43 s, -133.4 deg/s at 1 s, slip 11.6 deg / VEHTEST boost grip: release at slip 11.6 deg -> 13.9 (0.25 s) -> 6.1 deg (1 s), speed 24.1 (exit 1) |
| PASS | WFC_RELOADTEST | Rendering | in-process map render unload / reload: frame 330 pos 363.52 -724.48 -341.76 grounded=1 form=ROBOT anim=NAV_Idle t=1.45 ammo=50/150 reloading=0 yaw=0.49 aimW=1.00 aimN=-0.14 reloadW=0.00 legYaw=0.0 aimYawN=0.00 turn=0 recoil=0 hspeed=0.00 moveForm=ROBOT fineAim=0 fov=80.0 drv=0 ride=0.00 dash=0.00 nitro=0.00 wpn=1 vy=0.00 pitch=0.0 roll=0.0 cont=0 vgnd=0 camS=0 vyaw=0.49 both=0 hasW=1 camD=7.62 camH=4.69 arm=0 hand=1 ram=0.00 hudAim=0 hudSpread=0.0800 hudW=TnWeaponIonBlaster xhair=1 / frame 360 pos 363.52 -724.48 -341.76 grounded=1 form=ROBOT anim=NAV_Idle t=1.55 ammo=50/150 reloading=0 yaw=0.49 aimW=1.00 aimN=-0.14 reloadW=0.00 legYaw=0.0 aimYawN=0.00 turn=0 recoil=0 hspeed=0.00 moveForm=ROBOT fineAim=0 fov=80.0 drv=0 ride=0.00 dash=0.00 nitro=0.00 wpn=1 vy=0.00 pitch=0.0 roll=0.0 cont=0 vgnd=0 camS=0 vyaw=0.49 both=0 hasW=1 camD=7.62 camH=4.69 arm=0 hand=1 ram=0.00 hudAim=0 hudSpread=0.0800 hudW=TnWeaponIonBlaster xhair=1 / frame 390 pos 363.52 -724.48 -341.76 grounded=1 form=ROBOT anim=NAV_Idle t=1.65 ammo=50/150 reloading=0 yaw=0.49 aimW=1.00 aimN=-0.14 reloadW=0.00 legYaw=0.0 aimYawN=0.00 turn=0 recoil=0 hspeed=0.00 moveForm=ROBOT fineAim=0 fov=80.0 drv=0 ride=0.00 dash=0.00 nitro=0.00 wpn=1 vy=0.00 pitch=0.0 roll=0.0 cont=0 vgnd=0 camS=0 vyaw=0.49 both=0 hasW=1 camD=7.62 camH=4.69 arm=0 hand=1 ram=0.00 hudAim=0 hudSpread=0.0800 hudW=TnWeaponIonBlaster xhair=1 (exit 0) |
| SKIP | systems_audio_suite | Systems | no *audio*suite* executable built in this tree (Systems' 544-check suite is a tools/systems target) |

