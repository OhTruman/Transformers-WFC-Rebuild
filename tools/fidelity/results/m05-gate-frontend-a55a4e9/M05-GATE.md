# Milestone 05 end-to-end gate - F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fe1 (2026-10-03 15:40)

**PASS 47 / FAIL 2 / KNOWN 3 / INFO 3 / SKIP 29 / HUMAN-CHECK 1**

## DATA

| status | check | owner | evidence |
|---|---|---|---|
| PASS | manifest.frontend_flow | AssetTools | AssetTools manifests\frontend_flow.json present |
| PASS | manifest.frontend_gfx | AssetTools | AssetTools manifests\frontend_gfx.json present |
| PASS | manifest.frontend_modes | AssetTools | AssetTools manifests\frontend_modes.json present |
| PASS | manifest.frontend_maps | AssetTools | AssetTools manifests\frontend_maps.json present |
| PASS | manifest.frontend_loading | AssetTools | AssetTools manifests\frontend_loading.json present |
| PASS | manifest.frontend_audio | AssetTools | AssetTools manifests\frontend_audio.json present |
| PASS | manifest.frontend_localization | AssetTools | AssetTools manifests\frontend_localization.json present |
| PASS | intro_movie_files |  | authored intro movies on disk (ExtractedAssets\movies): missing  |
| FAIL | frontend_unit_tests | Frontend | wfc_frontend_tests exit 1 (exit = failures); failing: catalog.tdm_selectable_maps_now  2; output frontend_tests.txt |

## STATE

| status | check | owner | evidence |
|---|---|---|---|
| PASS | flow_trace | Frontend | 144 flow events, run exit 0, timeout events 0 |
| PASS | boot_map | Frontend | boot map UI_FrontEnd_m (expected UI_FrontEnd_m, RE 1.1 / frontend_flow.json [URL]) |
| PASS | first_launch_profile | Frontend | fresh run directory: watchedIntro=False |
| PASS | boot_loading_movie | Frontend | first loading screen closes as kind=InitialStartup (expected InitialStartup = TF_InitialStartup, RE 1.1 [LoadingMovie] InitialStartupFileName) |
| INFO | boot_loading_trace | Frontend | trace detail for the frontend lane: the first loading.start says kind=Generic bink=TF_LoadingScreen but the screen closes as kind=InitialStartup; no event records the switch to TF_InitialStartup |
| PASS | intro_chain_order | Frontend | movie.play order: Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro; expected Logo_Activision,Logo_Hasbro,Logo_HighMoon,FMV_intro (RE 1.2 Kismet chain (each on the previous Stopped)) |
| PASS | ui_transitions_legal | Frontend | 8 UI state changes; not in the TnUIController table:  |
| PASS | movie_for.FrontEnd | Frontend | state FrontEnd: expected UI_GFxFrontEnd_p.FrontEnd_GFX_1 opened (RE 1.4); opened: UI_GFxFrontEnd_p.FrontEnd_GFX_1,UI_GFxLobbies_p.PartyLobby_GFX_1,UI_GFxLobbies_p.GameLobby_GFX_1,UI_GFxCustomize_p.CustomTransformers_GFX_1,UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1,UI_GFxPause_p.PauseMenu_GFX_1,UI_GFxFrontEnd_p.FrontEnd_GFX_1 |
| PASS | movie_for.InLobby@PartyLobby | Frontend | state InLobby@PartyLobby: expected UI_GFxLobbies_p.PartyLobby_GFX_1 opened (RE 1.4); opened: UI_GFxFrontEnd_p.FrontEnd_GFX_1,UI_GFxLobbies_p.PartyLobby_GFX_1,UI_GFxLobbies_p.GameLobby_GFX_1,UI_GFxCustomize_p.CustomTransformers_GFX_1,UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1,UI_GFxPause_p.PauseMenu_GFX_1,UI_GFxFrontEnd_p.FrontEnd_GFX_1 |
| PASS | movie_for.InLobby@GameLobby | Frontend | state InLobby@GameLobby: expected UI_GFxLobbies_p.GameLobby_GFX_1 opened (RE 1.4); opened: UI_GFxFrontEnd_p.FrontEnd_GFX_1,UI_GFxLobbies_p.PartyLobby_GFX_1,UI_GFxLobbies_p.GameLobby_GFX_1,UI_GFxCustomize_p.CustomTransformers_GFX_1,UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1,UI_GFxPause_p.PauseMenu_GFX_1,UI_GFxFrontEnd_p.FrontEnd_GFX_1 |
| PASS | movie_for.WaitingOnGameStart | Frontend | state WaitingOnGameStart: expected UI_GFxCustomize_p.CustomTransformers_GFX_1 opened (RE 1.4); opened: UI_GFxFrontEnd_p.FrontEnd_GFX_1,UI_GFxLobbies_p.PartyLobby_GFX_1,UI_GFxLobbies_p.GameLobby_GFX_1,UI_GFxCustomize_p.CustomTransformers_GFX_1,UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1,UI_GFxPause_p.PauseMenu_GFX_1,UI_GFxFrontEnd_p.FrontEnd_GFX_1 |
| PASS | movie_for.Paused | Frontend | state Paused: expected UI_GFxPause_p.PauseMenu_GFX_1 opened (RE 1.4); opened: UI_GFxFrontEnd_p.FrontEnd_GFX_1,UI_GFxLobbies_p.PartyLobby_GFX_1,UI_GFxLobbies_p.GameLobby_GFX_1,UI_GFxCustomize_p.CustomTransformers_GFX_1,UI_GFxPreGameCountdown_p.PreGameCountdown_GFX_1,UI_GFxPause_p.PauseMenu_GFX_1,UI_GFxFrontEnd_p.FrontEnd_GFX_1 |
| PASS | no_duplicate_screens | Frontend | movies opened again without being closed:  |
| PASS | no_stale_screens_across_travel | Frontend | more than one movie still open when a new level began (travel replaces the UI):  |
| PASS | party_lobby_url | Frontend | UI_PartyLobby_m?game=TransContent.TnPartyLobbyGame?listen (expected UI_PartyLobby_m?game=TransContent.TnPartyLobbyGame?listen) |
| PASS | multiplayer_call | Frontend | Online.OpenPartyLobby(GTS_TeamGame) (RE 1.5: GTS_TeamGame) |
| PASS | mode_selection | Frontend | publishGameInfo tag=TDM settings=TnOnlineGameSettingsTDMPrivate friendlyName=Team Deathmatch |
| PASS | game_lobby_url | Frontend | UI_Lobby_m?Game=TransContent.TnGameLobbyGameTeam?GameModeTag=TDM?GameTeamStatus=3?MaxPlayers=10?IconicMode=0?listen; mismatched keys:  |
| PASS | map_selection | Frontend | selected mapId=508 map=MP_IAC_Streets_Base_m name=Streets hasRequiredAssets=True; preselected before the call: 510 |
| PASS | lobby_countdown | Frontend | countdown 10 s, 10 ticks, final countdown after 10.00 s (expected 10 s, 1 s ticks) |
| PASS | team_pick | Frontend | finalCountdown team=1 Decepticons (RE 5.3: 0 Autobots, 1 Decepticons; PickTeam tie -> RandomInt(2) HIGH) |
| PASS | match_url | Frontend | MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508  expected (RE 3.1): MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508 |
| PASS | match_loading_text | Frontend | title 'Team Deathmatch' message 'in Streets' tips 3 gfx UI_GFxLoading_p.LoadScreen_GFX bink TF_LoadingScreen |
| PASS | match_launch | Frontend | mode TDM goalScore 40 timeLimit 900 team 1 runtimeDir MP_IAC_Streets (RE 5.1: 40 / 900; team = lobby pick 1) |
| INFO | match_load_time |  | blocking match load 4.347 s |
| PASS | hud_visible_in_game | Frontend | ui.hud visible=True on entering InGame (RE 1.4) |
| PASS | world_runs_while_paused | Frontend | match frames advance during pause: 1850 -> 2530 (RE 6: bPauseable false, HIGH) |
| PASS | quit_to_main_menu | Frontend | match.quit reason=QuitToMainMenu; travel UI_FrontEnd_m; match.unloaded 1; FrontEnd level entered 2 times (RE 5.7) |
| PASS | return_skips_intro | Frontend | MovieLoader after return: enterFrontEnd; intro movies after return 0 |
| SKIP | return_to_game_lobby_after_match_end | Gameplay | needs the TDM match end (Gameplay MatchOver 15 s -> ReturnToGameLobby UI_Lobby_m with MapId kept; RE 5.7) |
| PASS | second_launch_profile | Frontend | second launch in the same directory: watchedIntro=True (profile persisted; medium rebuild-only) |
| PASS | second_launch_skips_logos | Frontend | intro movies played on the second launch: 0 (RE 1.2: logos only until the intro has been watched once (SetHasWatchedIntroMovie); MovieLoader branch HIGH) |
| PASS | second_launch_reaches_menu | Frontend | main menu reached on the second launch |

## PRESENTED

| status | check | owner | evidence |
|---|---|---|---|
| HUMAN | pause_input_focus | Frontend | hold W, press Esc in a match: the robot must stop while the pause menu is up and the world keeps running. Not automatable: scripted input (WFC_AUTOWALK) enters after the focus gate and the window reads GetAsyncKeyState. Proposed hook for the frontend lane: a scripted-input variant applied before the focus gate. Scripted walk for reference: before pause max 0.2 m/s, during pause max 0.2 m/s, displacement 0.45 m over 69 samples |
| KNOWN | intro_movies_played | Frontend | movie.unavailable 4: Logo_Activision (no video decoder integrated); Logo_Hasbro (no video decoder integrated); Logo_HighMoon (no video decoder integrated); FMV_intro (no video decoder integrated) - the files exist (DATA) but the user sees no movie |
| SKIP | capture_control |  | in-game control capture cap_0107_ui_state.png: flat 98%, near-black 0%. window capture unavailable this run (uniform control frame): screen checks below are SKIP. Re-run with the product window visible on an unlocked desktop |
| SKIP | frontend_screen |  | no window capture for this state (state not reached or window not capturable) |
| SKIP | party_lobby_screen |  | no window capture for this state (state not reached or window not capturable) |
| SKIP | game_lobby_screen |  | no window capture for this state (state not reached or window not capturable) |
| SKIP | loading_screen_match |  | no window capture for this state (state not reached or window not capturable) |
| SKIP | in_game |  | no window capture for this state (state not reached or window not capturable) |
| SKIP | pause_menu |  | no window capture for this state (state not reached or window not capturable) |
| SKIP | menu_music | Systems/Frontend | MUSIC log lines 0 (WFC_MUSICLOG; needs Systems FrontendAudio wired by the frontend owner):  |
| PASS | match_audio_sounding | Systems | AMB samples 15; backend voices max n/a (Systems field; absent before the Systems merge) |
| SKIP | ui_audio_ownership | Systems/Frontend | UI sounds (FrontendAudio.playUiSound) and per-level music handover need the Systems FrontendAudio wiring (call sequence in Systems FIDELITY M05) - expectations in m05\expectations.json audio_ownership |

## MATCH

| status | check | owner | evidence |
|---|---|---|---|
| KNOWN | pending_countdown | Gameplay | PreGameCountdown -> InGame after 0.00 s (RE 5.2: 10 s). PROVISIONAL adapter: character selected + match begun (no PendingMatch in Gameplay) |
| SKIP | spawn_after_pending | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | team_assignment | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | damage | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | death | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | respawn_delay | Gameplay | expected 5.0 s (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | team_score | Gameplay | expected kill +1 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | player_score | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | score_limit_end | Gameplay | expected 40 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | time_limit_end | Gameplay | expected 900 s (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | tie | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | match_end_ui | Gameplay | expected UI event 9 -> EndGameStats_GFX, HUD hidden (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | reset | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |
| SKIP | second_match | Gameplay | expected RE 5 (m05\expectations.json match_tdm). not implemented: no Gameplay TDM lifecycle events yet (MATCH protocol / UI events 4, 5, 9) |

## LIFETIME

| status | check | owner | evidence |
|---|---|---|---|
| PASS | state_reset_after_return | Frontend | returned frontend snapshot: mode='' mapId=-1 team=-1 hud=False countdown=0 |
| PASS | cycles_completed | Frontend | 3/3 frontend -> TDM -> pause -> quit -> frontend cycles completed (exit 0) |
| KNOWN | memory_per_cycle | Rendering | private MB at each return to the frontend: 2507.8 -> 4033.9 -> 5667.3 (+1579.8 MB per cycle). The frontend lane documents that the renderer is recreated on return and the previous map's GL objects leak (handoff Rendering: map unload) |
| FAIL | memory_peak | Rendering/Frontend | peak private memory over 3 cycles: 6242.9 MB (process.csv). FAIL above 6 GB: at that size a user's ordinary session of a few matches exhausts a typical 8-16 GB machine |
| PASS | handles_per_cycle |  | handles at each return: 471 -> 476 -> 482 (+5.5 per cycle) |
| PASS | one_world_load_per_match | Gameplay/Frontend | match.loaded 3, world loads logged 3 |
| INFO | load_time_per_cycle |  | match load seconds per cycle: 4.408, 2.593, 2.941 |
| PASS | match_parameters_stable | Frontend | match.launch mode/goal/time per cycle: TDM/40/900/team 1; TDM/40/900/team 0; TDM/40/900/team 1 |
| PASS | no_stale_flow_state | Frontend | frontend snapshots after each return with leftover match/lobby state: 0 |
| PASS | ui_events_per_cycle_constant | Frontend | ui.open events per cycle: 6, 6 (growth = screens accumulating) |
| PASS | player_state_fresh_each_match | Gameplay | first frame of each match: ammo 50/150; form ROBOT (3 matches) |
| SKIP | audio_pcm_returns_to_base | Systems | decoded PCM per AMB sample (needs Systems): first  MB, last  MB, max  MB; Systems guarantee: back to base after unloadMapAudio |
| SKIP | map_collision | Frontend/Rendering/Systems | no product counter yet for map_collision across travel; proposal: one 'LIFETIME <name>=<count/bytes>' log line per level.begin (protocols\RUNTIME-EVENTS.md) |
| SKIP | render_assets | Frontend/Rendering/Systems | no product counter yet for render_assets across travel; proposal: one 'LIFETIME <name>=<count/bytes>' log line per level.begin (protocols\RUNTIME-EVENTS.md) |
| SKIP | frontend_movies | Frontend/Rendering/Systems | no product counter yet for frontend_movies across travel; proposal: one 'LIFETIME <name>=<count/bytes>' log line per level.begin (protocols\RUNTIME-EVENTS.md) |
| SKIP | event_queues | Frontend/Rendering/Systems | no product counter yet for event_queues across travel; proposal: one 'LIFETIME <name>=<count/bytes>' log line per level.begin (protocols\RUNTIME-EVENTS.md) |
| SKIP | timers | Frontend/Rendering/Systems | no product counter yet for timers across travel; proposal: one 'LIFETIME <name>=<count/bytes>' log line per level.begin (protocols\RUNTIME-EVENTS.md) |

Evidence: run1_first_launch\ (flow.jsonl, wfc.log, captures + captures.csv), sheet_first_launch.png, run3_loops\process.csv. HUMAN checks: tools\fidelity\HUMAN-CHECK-M05.md.
