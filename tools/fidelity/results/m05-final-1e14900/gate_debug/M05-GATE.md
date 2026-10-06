# Milestone 05 end-to-end gate (Debug)

- target: ref=1e14900b150349f8da2affb024106f3d7b83cc50; sha=1e14900b150349f8da2affb024106f3d7b83cc50; built=2026-10-04T00:36:46
- exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build\bin\wfc_rebuild.exe`
- runs: R1, R2, R4; cycles 3; 2026-10-04 06:27

**PASS 66 / FAIL 2 / KNOWN 0 / INFO 3 / SKIP 5 / HUMAN-CHECK 14**

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
| PASS | intro_played_to_end | Frontend | movie.finished: Logo_Activision pos 10.400003 skipped False; Logo_Hasbro pos 13.200103 skipped False; Logo_HighMoon pos 7.480086 skipped False; FMV_intro pos 128.680089 skipped False |
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
| FAIL | return_lands_on_main_menu | Frontend | frontend after the match resembles the main menu (diff 3.14) rather than Press START (diff 0.07) (Online.ShouldShowStartScreen: true only until ShowDeviceSelectionUI, Frontend 76b8287 from the recovered script) |
| PASS | second_launch_profile | Frontend | watchedIntro=True with the first launch's profile |
| PASS | second_launch_skips_intro | Frontend | intro movies on the second launch: 0; title reached |

## PRESENTED

| status | check | owner | evidence |
|---|---|---|---|
| PASS | intro_movies_decoded | Frontend | intro movies with a decoded first frame: 4/4; finished events 4; unavailable 0  |
| HUMAN | intro_on_screen | Frontend | frames at 6 s / 14 s after boot: near-black 84% / 2%, mean luma 14.7 / 52.8, change 57.34. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check |
| HUMAN | screen.title |  | b_title.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.main_menu |  | c_mainmenu.bmp: near-black 87%, mean luma 9.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.party_lobby |  | d_party.bmp: near-black 64%, mean luma 11 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.mode_list |  | e_modes.bmp: near-black 65%, mean luma 10.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.host_options |  | f_hostoptions.bmp: near-black 65%, mean luma 11.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.game_lobby |  | g_gamelobby.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.lobby_countdown |  | h_countdown.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.loading_screen |  | i_loading.bmp: near-black 13%, mean luma 20.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.match_pre_game |  | j_match_pending.bmp: near-black 0%, mean luma 46.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.pause_menu |  | m_pause.bmp: near-black 0%, mean luma 69.3 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.returned_frontend |  | n_return.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| PASS | in_game_world_visible | Integration/Rendering/Frontend | in-game frames (trace: InGame, HUD on): near-black 0% / 10%, mean luma 28.8 / 19.2 |
| PASS | in_game_live | Integration | frames 8 s apart with walk + turn input differ by 20.18 luma (frozen view if ~0) |
| HUMAN | pause_menu_drawn | Frontend | pause frame differs from the game frame by 53.61 luma (menu over the running match; backdrop blend PARTIAL per Frontend) |
| HUMAN | pause_input_focus | Frontend | hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop) |

## MATCH

| status | check | owner | evidence |
|---|---|---|---|
| PASS | default_rules_tdm | Gameplay/Frontend | default host options -> Gameplay goal 40 / time 900 s (BLK B4/D2: 40 / 900) |
| PASS | pending_countdown | Gameplay/Frontend | match level -> InGame after 10.14 s (BLK D4 / RE 5.2: PendingMatch 10 s, PreGameCountdown shown yes) |
| PASS | no_spawn_before_start | Gameplay | first spawn log line 786 vs match level begin / InGame (BLK E5: no spawns in PendingMatch) |
| PASS | spawn_team_cluster | Gameplay | spawned at TnTeamPlayerStart_10894 in TnSpawnCluster_7810, team 0 (RE 5.3 / BLK E6: initial clusters Autobots 7810, Decepticons 4159, locked 15 s) |
| PASS | spawn_class | Gameplay | start actor TnTeamPlayerStart_10894 (TnTeamPlayerStart in TDM) |

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
| PASS | map_audio_released_each_unload | Systems | after each of 3 unloads (voices / instances / level cues / PCM MB): 0/0/0/36.489 / 0/0/0/36.489 / 0/0/0/36.489; pre-load baseline PCM 36.489 MB (Systems guarantee: 0 / 0 / 0 / baseline) |
| PASS | map_audio_loaded_each_match | Systems | map audio at each match load: level MP_IAC_Streets, level cues 32, PCM 97.179 / 97.179 / 97.179 MB |
| PASS | menu_music_resumes | Systems/Frontend | frontend music starts: 4 for 3 returns (+ boot) |
| PASS | no_menu_music_in_matches | Systems | frontend / lobby music starts inside 3 matches: 0 (game-type DM_* music excluded: it belongs to the match, OV A5) |
| PASS | no_stale_reverb | Systems | after 3 returns, the next reverb change still starts from a map preset (stale map reverb): 0 |
| PASS | doubled_sounds_cycles | Systems | doubled cue starts over 3 matches: 0  |

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

## LIFETIME

| status | check | owner | evidence |
|---|---|---|---|
| PASS | state_reset_after_return | Frontend | returned snapshot: mode '' mapId -1 team -1 hud False |
| PASS | cycles_completed | Integration | 3/3 frontend -> Streets TDM (15 s of walking, turning, firing, boosting) -> frontend cycles; exit 0 |
| PASS | memory_stabilizes |  | private MB after each unload: 2388.375 > 2485.082 > 2458.34 (slope -26.7 MB/cycle after cycle 1); at the frontend after each return: 1307.7 > 1365.5 > 1382.6 (slope 17.1).  |
| INFO | memory_peak |  | peak private memory over 3 cycles: 3211.6 MB |
| INFO | handles_threads | Integration | handles at each return: 651 > 653 > 662 (slope 9); threads: 18 > 16 > 17 (slope 1) |
| PASS | gl_release_per_return | Rendering | GL objects released at each return: textures=72 buffers=0 framebuffers=0 renderbuffers=0 vertexArrays=0 programs=0 |
| PASS | audio_pcm_per_match | Systems | peak decoded PCM per match: 97.2 > 97.2 > 97.2 MB; peak live instances per match: 68 > 67 > 68 |
| PASS | ui_movies_per_cycle | Frontend | GFx movies opened per cycle (after the first): 6, 6 |
| PASS | no_stale_flow_state | Frontend | returned snapshots with leftover match / lobby state: 0 |
| PASS | one_launch_per_cycle | Gameplay/Integration | Gameplay launches 3, frontend match loads 3 |
| SKIP | counter.map_collision | Integration | no product counter for map_collision across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.map_objects | Integration | no product counter for map_objects across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.particles | Integration | no product counter for particles across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.event_queues | Integration | no product counter for event_queues across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.timers | Integration | no product counter for timers across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |

