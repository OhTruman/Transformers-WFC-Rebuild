# Milestone 05 end-to-end gate (Release)

- target: ref=origin/agents/frontend; sha=08ef880b54829475454adcfe5a64bd35e3a74c29; built=2026-10-03T22:55:57
- exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fe2\build-release\bin\wfc_rebuild.exe`
- runs: R1, R2, R4, R5, R6, S; cycles 3; 2026-10-03 23:42

**PASS 45 / FAIL 20 / KNOWN 0 / INFO 5 / SKIP 12 / HUMAN-CHECK 13**

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
| PASS | flow_trace | Frontend | 281 events, exit 0; script timeout 0 |
| PASS | boot_map | Frontend | boot map UI_FrontEnd_m; watchedIntro False (fresh profile expects False) |
| PASS | boot_loading_kind | Frontend | first loading screen closes as InitialStartup (RE 1.1 [LoadingMovie] InitialStartupFileName) |
| PASS | intro_chain_order | Frontend | movie.play: Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro (expected Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro) |
| PASS | intro_played_to_end | Frontend | movie.finished: Logo_Activision pos 10.400013 skipped False; Logo_Hasbro pos 13.200064 skipped False; Logo_HighMoon pos 7.480052 skipped False; FMV_intro pos 128.680203 skipped False |
| PASS | ui_transitions_legal | Frontend | 8 UI state changes; outside the TnUIController table:  |
| PASS | movie_for.FrontEnd | Frontend | expected UI_GFxFrontEnd_p.FrontEnd_GFX_1 opened (RE 1.4) |
| PASS | movie_for.InLobby@PartyLobby | Frontend | expected UI_GFxLobbies_p.PartyLobby_GFX_1 opened (RE 1.4) |
| PASS | movie_for.InLobby@GameLobby | Frontend | expected UI_GFxLobbies_p.GameLobby_GFX_1 opened (RE 1.4) |
| PASS | movie_for.Paused | Frontend | expected UI_GFxPause_p.PauseMenu_GFX_1 opened (RE 1.4) |
| PASS | no_duplicate_screens | Frontend | GFx movies opened again while open:  |
| PASS | key_multiplayer | Frontend | Start, Down, A on the main menu -> Online.OpenPartyLobby(GTS_TeamGame) (BLK A2: GTS_TeamGame) |
| PASS | key_mode_focus_tdm | Frontend | first EditGameMode on entering the mode list: TDM (BLK B2: initial focus TDM, EditGameMode on focus) |
| PASS | host_option_defaults | Frontend | Create Game with untouched rows wrote: AutobalanceTeams=Autobalanced, MapSelectionMethod=Host's Choice, TimeLimit=15 minutes, PointsToWin=40 (BLK B4: Autobalanced / Host's Choice / 15 minutes / 40) |
| PASS | lobby_default_map | Frontend | game lobby opened: server pick 510, then the lobby movie pushed SetSelectedMapID(508); expected the first selectable map in TransLevels order (BLK C4: index 0; rebuild-selectable maps 501,502,503,504,507,508,509,510 -> 508). Original dump: Seed of Corruption 501 is index 0; the rebuild only lists maps with runtime data |
| PASS | lobby_countdown | Frontend | A on Start Game -> countdown 10 s, final countdown after 10.00 s (BLK C8: 10 s) |
| PASS | match_url | Frontend | MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508; mismatched keys:  (RE 3.1 / BLK D1) |
| PASS | match_loading_text | Frontend | title 'Team Deathmatch' message 'in Streets' tips 3 |
| PASS | quit_to_main_menu | Frontend | travel UI_FrontEnd_m; returned uiState FrontEnd; intro replayed after return: 0 |
| PASS | second_launch_profile | Frontend | watchedIntro=True with the first launch's profile |
| PASS | second_launch_skips_intro | Frontend | intro movies on the second launch: 0; title reached |

## PRESENTED

| status | check | owner | evidence |
|---|---|---|---|
| PASS | intro_movies_decoded | Frontend | intro movies with a decoded first frame: 4/4; finished events 4; unavailable 0  |
| HUMAN | intro_on_screen | Frontend | frames at 6 s / 14 s after boot: near-black 43% / 0%, mean luma 83 / 52.9, change 77.18. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check |
| HUMAN | screen.title |  | b_title.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.main_menu |  | c_mainmenu.bmp: near-black 87%, mean luma 9.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.party_lobby |  | d_party.bmp: near-black 64%, mean luma 11 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.mode_list |  | e_modes.bmp: near-black 65%, mean luma 10.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.host_options |  | f_hostoptions.bmp: near-black 65%, mean luma 11.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.game_lobby |  | g_gamelobby.bmp: near-black 57%, mean luma 14.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.lobby_countdown |  | h_countdown.bmp: near-black 57%, mean luma 14.3 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.loading_screen |  | i_loading.bmp: near-black 14%, mean luma 23 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| FAIL | screen.match_pre_game | Frontend | j_match_pending.bmp: near-black 99%, mean luma 0.6 - nothing on screen |
| HUMAN | screen.pause_menu |  | m_pause.bmp: near-black 12%, mean luma 67.1 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| HUMAN | screen.returned_frontend |  | n_return.bmp: near-black 94%, mean luma 6.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png) |
| FAIL | in_game_world_visible | Integration/Rendering/Frontend | in-game frames (trace: InGame, HUD on): near-black 99% / 99%, mean luma 0.6 / 0.6 - the player sees no world although the trace says InGame (k_ingame0.bmp) |
| FAIL | in_game_live | Integration | frames 8 s apart with walk + turn input differ by 0 luma (frozen view if ~0) |
| HUMAN | pause_menu_drawn | Frontend | pause frame differs from the game frame by 66.88 luma (menu over the running match; backdrop blend PARTIAL per Frontend) |
| HUMAN | pause_input_focus | Frontend | hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop) |

## MATCH

| status | check | owner | evidence |
|---|---|---|---|
| FAIL | pending_countdown | Gameplay/Frontend | match level -> InGame after 0.00 s (BLK D4 / RE 5.2: PendingMatch 10 s, PreGameCountdown shown yes); PROVISIONAL adapter still in the path: character selected + match begun (no PendingMatch in Gameplay) |
| FAIL | no_spawn_before_start | Gameplay | first spawn log line  vs match level begin / InGame (BLK E5: no spawns in PendingMatch) |
| INFO | spawn_team_cluster | Gameplay | spawned at  in , team  (RE 5.3 / BLK E6: initial clusters Autobots 7810, Decepticons 4159, locked 15 s) |
| FAIL | spawn_class | Gameplay | start actor  (TnTeamPlayerStart in TDM) |
| FAIL | dm_spawn_class | Gameplay | DM local spawn:  |

## AUDIO

| status | check | owner | evidence |
|---|---|---|---|
| PASS | movie_mute | Systems/Frontend | audio.moviePlaying events 10: playing=True / playing=False / playing=True / playing=False / playing=True / playing=False (CINE_MUTE_FOR_BINK while a Bink plays: Systems M06 MovieMixerPreset) |
| FAIL | music.FrontEnd | Systems | 2 visits, music plays:  (expected FRONTEND_MX_ORBIT_01 once per visit) |
| FAIL | music.PartyLobby | Systems | 1 visits, music plays:  (expected MP_PARTY_LOBBY_MX once per visit) |
| FAIL | music.GameLobby | Systems | 1 visits, music plays:  (expected MP_LOBBY_MX once per visit) |
| PASS | no_music_in_match | Systems | music plays during the Streets match:  (Streets authors no SeqAct_PlayMusic) |
| FAIL | ui_sounds_played | Systems/Frontend | 8 UI sounds requested by the movies (BUTTON_START,BUTTON_DOWN,BUTTON_ACCEPT); without an audio owner (audio=none): 8 |
| PASS | ui_sounds_single | Frontend | same UI sound requested twice within 20 ms: 0 |
| FAIL | doubled_sounds_R1 | Systems | cue instances started twice at the same place and moment: 1  x1 |
| PASS | map_audio_released_on_return | Systems | first AMB sample after each unload: map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  / map  live  voices  pcm  (Streets audio must be gone; frontend level only) |
| FAIL | menu_music_resumes | Systems/Frontend | frontend music starts: 1 for 3 returns (+ boot) |
| PASS | no_menu_music_in_matches | Systems | music starts inside 3 matches: 0 |
| FAIL | no_stale_reverb | Systems | after 3 returns, the first reverb change is not the frontend's / None: 2 |
| FAIL | doubled_sounds_cycles | Systems | doubled cue starts over 3 matches: 1  x1 |

## OWNERSHIP

| status | check | owner | evidence |
|---|---|---|---|
| PASS | launch_request | Frontend | frontend MatchLaunch: mode TDM goal 40 time 900 runtimeDir MP_IAC_Streets team 1 |
| FAIL | gameplay_runs_request | Integration/Gameplay | Gameplay: '' vs frontend request mode TDM goal 40 time 900 |
| INFO | team_consistent | Integration | frontend launch team 1, Gameplay spawn team  (BLK E2/E4: offline the lobby assigns no team; the match PickTeam decides) |
| FAIL | audio_level | Systems/Integration | Systems AMB level during the match:  |
| PASS | render_map | Rendering | render log lines naming the map: glb: F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/world.glb -> 2076144 verts, 1983988 tris, bounds [-4813.1 -3295.6 -4745.4]..[4135.4 5645.6 4171.8] / glb: F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/collision_pawn.glb -> 75742 verts, 101519 tris, bounds [-163.8 -769.2 -771.0]..[590.7 -405.4 -125.0] / glb: F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/collision_weapon.glb -> 74034 verts, 100959 tris, bounds [-56.4 -769.2 -771.0]..[590.7 -405.4 -125.0] / pickups: 24 factories (ammo crate 14, health 9, overshield 1) from F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/gameplay.json |
| INFO | ui_map_is_the_map_loaded | Integration/Gameplay | UI selected map 510; frontend launch runtimeDir MP_UND_Gorge; Gameplay: ; Systems: ; level after 55 s: Match. FAIL = an owner runs Streets behind a Gorge selection; KNOWN = launch refused cleanly (Gameplay documents launchMatch accepts only MP_IAC_Streets in this milestone) |
| PASS | unsupported_map_handled | Integration | process exit 0; clean exit event 1 |
| FAIL | ui_mode_is_the_mode_run | Integration/Gameplay | mode list Down -> DM; launch mode DM goal 20; loading title 'Deathmatch'; Gameplay: ; spawn:  |

## LIFETIME

| status | check | owner | evidence |
|---|---|---|---|
| PASS | state_reset_after_return | Frontend | returned snapshot: mode '' mapId -1 team -1 hud False |
| PASS | cycles_completed | Integration | 3/3 frontend -> Streets TDM (15 s of walking, turning, firing, boosting) -> frontend cycles; exit 0 |
| FAIL | memory_stabilizes | Rendering/Systems/Integration | private MB after each unload: 2657.395 > 2579.188 > 2586.02 (slope 6.8 MB/cycle after cycle 1); at the frontend after each return: 940.1 > 955.8 > 981.7 (slope 25.9). neither GL release counts nor audio PCM explain it: run with WFC_NO_GL_RELEASE A/B and Systems WFC_LEVELAUDIO_CYCLE to split; unattributed growth -> Integration |
| INFO | memory_peak |  | peak private memory over 3 cycles: 3308.3 MB |
| PASS | handles_threads | Integration | handles at each return: 652 > 660 > 665 (slope 5); threads: 20 > 17 > 17 (slope 0) |
| PASS | gl_release_per_return | Rendering | GL objects released at each return: textures=489 buffers=41 framebuffers=6 renderbuffers=0 vertexArrays=21 programs=253 |
| SKIP | audio_pcm_per_match | Systems | peak decoded PCM per match:  >  >  MB; peak live instances per match:  >  >  |
| PASS | ui_movies_per_cycle | Frontend | GFx movies opened per cycle (after the first): 5, 5 |
| PASS | no_stale_flow_state | Frontend | returned snapshots with leftover match / lobby state: 0 |
| FAIL | one_launch_per_cycle | Gameplay/Integration | Gameplay launches 1, frontend match loads 3 |
| SKIP | counter.map_collision | Integration | no product counter for map_collision across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.map_objects | Integration | no product counter for map_objects across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.particles | Integration | no product counter for particles across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.event_queues | Integration | no product counter for event_queues across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |
| SKIP | counter.timers | Integration | no product counter for timers across travel (proposal: one 'LIFETIME <name>=<count>' line per level.begin, protocols\RUNTIME-EVENTS.md); covered indirectly by process memory / handles |

## SELFTEST

| status | check | owner | evidence |
|---|---|---|---|
| SKIP | WFC_TDMTEST | Gameplay | WFC_TDMTEST not compiled into this exe |
| SKIP | WFC_MATCHTEST | Gameplay | WFC_MATCHTEST not compiled into this exe |
| SKIP | WFC_XFORMTEST | Gameplay | WFC_XFORMTEST not compiled into this exe |
| SKIP | WFC_CHAOS | Gameplay | WFC_CHAOS not compiled into this exe |
| INFO | WFC_VEHTEST | Gameplay | vehicle handling measurements: VEHTEST rest: COM height 1.2872 m (native L_eq 1.287 m) contacts=4 pitch=-0.000 roll=0.000 / VEHTEST forward: cruise 15.00 m/s, release -> stop in 0.500 s (native 0.5 s) / VEHTEST strafe: cruise 15.00 m/s, release -> stop in 0.500 s (native 0.5 s) / VEHTEST bump 0.25 m @15 m/s: COM-above-ground min 1.075 max 1.575 (rest 1.287), pitch -1.8..4.0 deg, /vy/ max 1.27, speed after 15.00 / VEHTEST bump 0.50 m @15 m/s: COM-above-ground min 0.863 max 1.857 (rest 1.287), pitch -3.0..8.1 deg, /vy/ max 2.17, speed after 15.00 / VEHTEST drop 10 m: first-contact vy -17.14 m/s, min COM height 0.400 m, settled 1.287 m, reversals 2 / VEHTEST hover jump (standing, 0.0 m/s): vy after 12.00 m/s, apex +3.803 m over rest, max nose-up 7.6 deg, horizontal kept 0.00 m/s / VEHTEST hover jump (moving, 15.0 m/s): vy after 12.00 m/s, apex +3.807 m over rest, max nose-up 7.6 deg, horizontal kept 15.00 m/s / VEHTEST dash (stick right): tick1 fwd 14.91 lat 7.53 / tick2 fwd 29.82 lat 0.08 / t=0.47 s fwd 30.00 lat -0.01 vy -0.00 / exit fwd 15.00 lat -0.01 / VEHTEST drift turn t=0.15 s: yaw 90.0 deg, travel heading 0.2 deg, speed 26.87 (start 27.24) / VEHTEST drift turn t=0.30 s: yaw 90.0 deg, travel heading 1.3 deg, speed 25.66 (start 27.24) / VEHTEST drift turn t=0.45 s: yaw 90.0 deg, travel heading 4.6 deg, speed 23.18 (start 27.24) / VEHTEST drift turn t=0.60 s: yaw 90.0 deg, travel heading 11.9 deg, speed 19.57 (start 27.24) / VEHTEST drift turn t=0.75 s: yaw 90.0 deg, travel heading 22.5 deg, speed 16.40 (start 27.24) / VEHTEST boost jump: speed before 25.48, after: fwd 31.48 vy 13.68, pitch rate 1.78 rad/s / VEHTEST boost steer full           u0 30.0 stick 1.00: yaw rate  53.8/115.1/146.6/146.6/122.0 deg/s, slip@1s -51.1 deg, speed@3s 16.0 / VEHTEST boost steer half           u0 30.0 stick 0.50: yaw rate  12.3/ 22.3/ 28.6/ 30.5/ 32.5 deg/s, slip@1s  -6.3 deg, speed@3s 28.5 / VEHTEST boost steer quarter        u0 30.0 stick 0.25: yaw rate   2.9/  4.8/  5.7/  5.9/  5.9 deg/s, slip@1s  -1.1 deg, speed@3s 29.9 / VEHTEST boost steer full slow      u0 10.0 stick 1.00: yaw rate  20.6/ 51.5/ 95.5/140.4/121.5 deg/s, slip@1s -26.1 deg, speed@3s 15.9 / VEHTEST boost steer full           u0 30.0 stick 1.00 NITRO: yaw rate  15.3/ 29.2/ 40.3/ 47.1/ 49.4 deg/s, slip@1s  -9.7 deg, speed@3s 34.5 / VEHTEST stick raw 0.20 -> input 0.000 -> steering 0.000 (nitro 0.000) / VEHTEST stick raw 0.30 -> input 0.067 -> steering 0.004 (nitro 0.001) / VEHTEST stick raw 0.50 -> input 0.333 -> steering 0.111 (nitro 0.033) / VEHTEST stick raw 0.75 -> input 0.667 -> steering 0.444 (nitro 0.133) / VEHTEST stick raw 1.00 -> input 1.000 -> steering 1.000 (nitro 0.300) / VEHTEST boost stick release: yaw rate 146.6 -> 28.7 (0.25 s) -> 0.1 deg/s (1 s) / VEHTEST boost left-stick X only (RollControl): yaw change -0.01 deg in 1 s, rollControl 1.0 / VEHTEST boost release while turning: driving 0, drift 0.48 s, yaw now follows view (err 0.000 rad) (exit 1) |
| SKIP | WFC_RELOADTEST | Rendering | WFC_RELOADTEST not compiled into this exe |
| SKIP | systems_audio_suite | Systems | no *audio*suite* executable built in this tree (Systems' 544-check suite is a tools/systems target) |

