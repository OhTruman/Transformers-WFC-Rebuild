# Final gate - 1e14900b150349f8da2affb024106f3d7b83cc50

| | |
|---|---|
| integration commit | `1e14900b150349f8da2affb024106f3d7b83cc50` |
| Release exe | `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build-release\bin\wfc_rebuild.exe` |
| Debug exe | `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build\bin\wfc_rebuild.exe`  |
| run | 2026-10-04 06:27 |

**PRODUCT FAIL 24 / TEST/HARNESS FAIL 0 / PASS 253 / KNOWN 1 / UNKNOWN/WAITING 9 / HUMAN 44 / INFO 12 / SKIP 18 / PARTIAL 1**

| suite | result |
|---|---|
| lifecycle gate (Release) | PRODUCT FAIL 3 / TEST/HARNESS FAIL 0 / PASS 100 / KNOWN 1 / UNKNOWN/WAITING 0 / HUMAN 20 / INFO 4 / SKIP 8 / PARTIAL 0 |
| playtest acceptance | PRODUCT FAIL 7 / TEST/HARNESS FAIL 0 / PASS 16 / KNOWN 0 / UNKNOWN/WAITING 5 / HUMAN 5 / INFO 1 / SKIP 0 / PARTIAL 1 |
| motion / jitter | PRODUCT FAIL 0 / TEST/HARNESS FAIL 0 / PASS 31 / KNOWN 0 / UNKNOWN/WAITING 4 / HUMAN 1 / INFO 1 / SKIP 0 / PARTIAL 0 |
| lifecycle gate (Debug) | PRODUCT FAIL 2 / TEST/HARNESS FAIL 0 / PASS 66 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 14 / INFO 3 / SKIP 5 / PARTIAL 0 |
| transform stress | PRODUCT FAIL 1 / TEST/HARNESS FAIL 0 / PASS 13 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 0 / INFO 0 / SKIP 0 / PARTIAL 0 |
| vehicle collision | PRODUCT FAIL 11 / TEST/HARNESS FAIL 0 / PASS 1 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 2 / INFO 1 / SKIP 0 / PARTIAL 0 |
| visual (merged vs baseline vs lane) | PRODUCT FAIL 0 / TEST/HARNESS FAIL 0 / PASS 3 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 2 / INFO 1 / SKIP 0 / PARTIAL 0 |
| lifetime soak | PRODUCT FAIL 0 / TEST/HARNESS FAIL 0 / PASS 23 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 0 / INFO 1 / SKIP 5 / PARTIAL 0 |

## PRODUCT FAIL (owner lane): 24

- **e2e.STATE.return_lands_on_main_menu** [Frontend]: frontend after the match resembles the main menu (diff 3.14) rather than Press START (diff 0.09) (Online.ShouldShowStartScreen: true only until ShowDeviceSelectionUI, Frontend 76b8287 from the recovered script)
- **e2e.AUDIO.match_start_music** [Systems/Gameplay]: match music during the match: none; expected BL_LVL_MP_MX.DM_START at the match start (OV A5 TnGameTypeMessage GameTypeMusic, CONFIRMED)
- **e2e.SELFTEST.WFC_CHAOS** [Gameplay]: seeded random play from nav points: under the map / KillZ / stuck / inside props: CHAOS SUMMARY: 123 starts x 20 s (147477 ticks, 702 transform presses, 636 jumps, 637 boosts): 1 runs UNDER THE MAP (BSP floor above), 0 KillZ, 2 stuck; 7 runs with the pawn inside / under a prop / CHAOS STUCK from TnHealthPickupFactory_1394 at (221.6 -724.8 -441.3) form ROBOT blockers BlockingVolume_6729,BlockingVolume_7015,StaticMeshCollectionActor_4187,StaticMeshCollectionActor_4320,+2 / CHAOS UNDER-FLOOR from TnTeamPlayerStart_10894: 8 frames, worst 1.42 m at (105.9 -718.2 -632.7) blockers StaticMeshCollectionActor_4919 / CHAOS STUCK from TnTeamPlayerStart_6923 at (368.0 -706.6 -433.9) form ROBOT blockers StaticMeshActor_15698,StaticMeshActor_9634,StaticMeshCollectionActor_3000,StaticMeshCollectionActor_3000,+1 (exit 1)
- **accept.intro.audio** [Frontend/Systems]: intro movies whose audio started on the Systems device (movie.audioStart with a handle): 0/4; decode failures 0. Audible balance / sync is a human check. Human playtest: the intro has no sound. Frontend documents movie audio as PARTIAL (10 mono FLAC tracks, layout unidentified; MoviesToAlwaysPlaySound lists the logos) - owner Frontend for decode, Systems for playback on the one audio device, AssetTools / RE for the track layout
- **accept.background.title** [Frontend/Rendering/AssetTools]: title: near-black 94%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.main_menu** [Frontend/Rendering/AssetTools]: main_menu: near-black 87%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.party_lobby** [Frontend/Rendering/AssetTools]: party_lobby: near-black 64%, flat untextured grey 0%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.host_options** [Frontend/Rendering/AssetTools]: host_options: near-black 65%, flat untextured grey 0%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.frontend_after_return** [Frontend/Rendering/AssetTools]: frontend_after_return: near-black 94%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m again after travel back (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.loading.updates_during_load** [Frontend/Integration]: 18 window captures over 4.9 s of map load (4.057 s blocking per match.loaded); consecutive captures that change: 4/17; longest frozen interval 3.7 s (FAIL above 2.5 s). A frozen loading screen = the load blocks the presentation loop (Frontend documents the blocking load as PARTIAL; RE: native threaded load + Bink underlay keeps animating)
- **e2e.STATE.return_lands_on_main_menu** [Frontend]: frontend after the match resembles the main menu (diff 3.14) rather than Press START (diff 0.07) (Online.ShouldShowStartScreen: true only until ShowDeviceSelectionUI, Frontend 76b8287 from the recovered script)
- **e2e.AUDIO.match_start_music** [Systems/Gameplay]: match music during the match: none; expected BL_LVL_MP_MX.DM_START at the match start (OV A5 TnGameTypeMessage GameTypeMusic, CONFIRMED)
- **transform_stress.r2v_rapid** [Gameplay]: 84 runs: PASS 66, INFO large_drop 16, FAIL fell_through 2. Met: thin floor 29, wall 0, incline > 0.5 m 0, decline < -0.5 m 0, airborne at press 0, low ceiling 0, transforms 504. Large legal drops 16. Failing: r2v_rapid_s15 (FAIL fell_through, pen 9.73 m @f633); r2v_rapid_s35 (FAIL fell_through, pen 2.62 m @f345)
- **vehicle_collision.drive** [Gameplay]: 84 runs, 6.62 km driven: PASS 32, INFO stuck 51, FAIL ramp_hard_stop 1. Failing: drive_s66 through  at () h  m @f
- **vehicle_collision.boost** [Gameplay]: 84 runs, 7.56 km driven: INFO stuck 55, PASS 27, FAIL ramp_hard_stop 2. Failing: boost_s51 through  at () h  m @f; boost_s68 through  at () h  m @f
- **vehicle_collision.boost_right** [Gameplay]: 84 runs, 10.36 km driven: PASS 80, INFO stuck 1, FAIL passed_through 1, FAIL ramp_hard_stop 2. Failing: boost_right_s5 through StaticMeshCollectionActor_8404 at (176.35,-715.6,-577.69) h 1.2 m @f403; boost_right_s51 through  at () h  m @f; boost_right_s68 through  at () h  m @f
- **vehicle_collision.boost_left** [Gameplay]: 84 runs, 10.49 km driven: PASS 81, FAIL ramp_hard_stop 3. Failing: boost_left_s35 through  at () h  m @f; boost_left_s51 through  at () h  m @f; boost_left_s68 through  at () h  m @f
- **vehicle_collision.nitro** [Gameplay]: 84 runs, 7.96 km driven: INFO stuck 48, PASS 34, FAIL ramp_hard_stop 2. Failing: nitro_s51 through  at () h  m @f; nitro_s68 through  at () h  m @f
- **vehicle_collision.boost_cycle** [Gameplay]: 84 runs, 10.15 km driven: FAIL passed_through 2, PASS 65, INFO stuck 15, FAIL ramp_hard_stop 2. Failing: boost_cycle_s0 through StaticMeshCollectionActor_7270 at (201.88,-721.75,-552.8) h 1.6 m @f567; boost_cycle_s2 through  at () h  m @f; boost_cycle_s39 through  at () h  m @f; boost_cycle_s48 through StaticMeshCollectionActor_15307 at (196.46,-704.96,-354.39) h 1.2 m @f341
- **vehicle_collision.reverse** [Gameplay]: 84 runs, 3.69 km driven: PASS 78, INFO stuck 4, FAIL passed_through 2. Failing: reverse_s42 through StaticMeshCollectionActor_4707 at (358.24,-702.96,-434.94) h 1.6 m @f142; reverse_s45 through StaticMeshCollectionActor_4707 at (358.73,-702.81,-434.91) h 1.6 m @f181
- **vehicle_collision.strafe_boost** [Gameplay]: 84 runs, 8.73 km driven: INFO stuck 34, PASS 45, FAIL ramp_hard_stop 4, FAIL passed_through 1. Failing: strafe_boost_s51 through  at () h  m @f; strafe_boost_s55 through StaticMeshCollectionActor_10306 at (266.22,-723.01,-416.15) h 1.2 m @f570; strafe_boost_s56 through  at () h  m @f; strafe_boost_s61 through  at () h  m @f; strafe_boost_s68 through  at () h  m @f
- **vehicle_collision.robot_turn** [Gameplay]: 84 runs, 6.69 km driven: PASS 76, FAIL passed_through 7, INFO stuck 1. Failing: robot_turn_s22 through StaticMeshCollectionActor_13239 at (116.29,-704.62,-311.46) h 1.2 m @f544; robot_turn_s25 through StaticMeshCollectionActor_13239 at (116.03,-704.23,-311.59) h 1.6 m @f543; robot_turn_s28 through StaticMeshCollectionActor_13239 at (116.3,-704.62,-311.46) h 1.2 m @f545; robot_turn_s49 through StaticMeshCollectionActor_13239 at (115.94,-704.23,-311.61) h 1.6 m @f545; robot_turn_s67 through StaticMeshCollectionActor_7072 at (226.69,-722.84,-384.02) h 1.6 m @f419; robot_turn_s77 through StaticMeshCollectionActor_13239 at (116.3,-704.62,-311.46) h 1.2 m @f548; robot_turn_s82 through StaticMeshCollectionActor_5367 at (116.8,-706.73,-420.52) h 1.2 m @f322
- **vehicle_collision.robot_jump** [Gameplay]: 84 runs, 7.16 km driven: PASS 72, FAIL passed_through 8, INFO stuck 4. Failing: robot_jump_s24 through StaticMeshCollectionActor_112 at (143.2,-724.84,-451.72) h 1.2 m @f151; robot_jump_s27 through StaticMeshCollectionActor_6420 at (335.1,-717.78,-485.29) h 1.6 m @f488; robot_jump_s31 through StaticMeshCollectionActor_12216 at (18.06,-715.81,-591.69) h 0.8 m @f588; robot_jump_s35 through StaticMeshCollectionActor_11477 at (380.38,-699.6,-336.44) h 1.6 m @f470; robot_jump_s5 through StaticMeshCollectionActor_2508 at (113.66,-709.31,-630.9) h 1.6 m @f269; robot_jump_s62 through StaticMeshCollectionActor_4229 at (106.08,-709.77,-314.31) h 1.6 m @f561; robot_jump_s79 through StaticMeshCollectionActor_7201 at (132.41,-700.1,-447.44) h 1.6 m @f576; robot_jump_s8 through StaticMeshCollectionActor_6420 at (335.03,-718.15,-485.35) h 1.2 m @f486
- **vehicle_collision.ramp_hard_stops** [Gameplay]: hard stops (> 8 m/s to < 35% in 0.2 s) with no authored wall / step ahead on a rising walkable floor: 16 runs. boost_cycle_s2@f198 (83.3,-719.3,-575.4) from 17.7 m/s rise 2.45; boost_cycle_s39@f243 (83.7,-719.1,-580.2) from 14.3 m/s rise 1.91; boost_left_s35@f285 (324.4,-701.8,-383.2) from 12.2 m/s rise 4.81; boost_left_s51@f35 (111.3,-706.2,-419.3) from 8.4 m/s rise -0.42; boost_left_s68@f76 (111.3,-706.2,-419.8) from 19.9 m/s rise 0.35; boost_right_s51@f35 (111.3,-706.2,-419.3) from 8.4 m/s rise -0.42

## TEST / HARNESS FAIL (Experimental): 0


## KNOWN (documented gaps): 1

- **e2e.OWNERSHIP.gorge_not_selectable** [AssetTools/Integration]: map selector stepped right: selected 508; Gorge selectable False. Integration lists Gorge disabled by design (no AssetTools render export; not validated by Gameplay / Rendering) - not counted as a working map

## UNKNOWN / WAITING (evidence or presentation pending): 10

- **accept.match.result_exposed** [Frontend/Integration]: EndGameStats: data-store reads the rebuild does not answer: GetCollectionRowCount <TnMenuItems:Specialties>; ReadValue <CurrentGame:AttackingTeamIndex> (Integration documents 'undefined / +NaN' in the experience panel: no profile / XP service)
- **accept.hud.clock_displayed** [Frontend/Rendering]: Hud_GFX (clock / team score / health / ammo, BLK G) open in play: False; dumps 0. Until the HUD movie is drawn, displayed vs Gameplay values cannot be compared (owner Frontend / Rendering: HUD ownership handoff)
- **accept.hud.score_displayed** [Frontend/Rendering]: team score bars (<CurrentGame:Teams> polled every 500 ms, BLK G) - waiting for the HUD movie
- **accept.hud.kill_feed_event** [Frontend/Rendering]: kills 5; kill-feed (TnDeathMessage: DeathMessage(killer, victim) / SuicideMessage, 3.0 s lifetime, CONFIRMED) entries traced: 0. Original presentation (RE OV A2, CONFIRMED): Hud_GFX _global.GameMessage(html) at (84, 626), 5 rows, each 5.0 s + 1.0 s fade, local white / teammate (80,181,213) / enemy (240,60,60), weapon icon tokens. Gameplay 391b7f0 exposes HudGameState::killFeed; WAITING until a trace or drawn feed exists
- **accept.hud.point_event** [Frontend]: kill -> _global.PointEvent (TnHUD KillTransactionObserver, CONFIRMED authored) pushes traced: 0
- **accept.hud.values_match_gameplay** [Frontend]: HUD text fields vs MATCH timer / score lines (hud_table.csv) - waiting for the HUD movie
- **jitter.present.vehicle** [Frontend/Integration]: vehicle presentation frames need a scripted transform in a frontend-launched match (proposal for Frontend / Integration: FRONTEND_SCRIPT step press:transform / hold:boost). Vehicle simulation jitter: part A; look: HUMAN
- **jitter.bone_level** [Gameplay/Rendering]: no per-bone transform log in the product: root vs individual-bone separation cannot be measured (proposal: WFC_POSELOG 'POSE frame bone pos' for root / pelvis / head / hands). Presentation evidence above + HUMAN-CHECK
- **jitter.pacing** [Integration]: WFC_RENDERHZ / WFC_CAMLOG not in this exe (Rendering M08 diagnostics, agents/rendering 429bcf3: Integration to carry them)
- **jitter.present.vehicle** [Integration]: WFC_SHOTEVERY not in this exe (Rendering M08 per-frame capture; Integration to carry it)

## HUMAN CHECK (framing evidence): 44

- **e2e.PRESENTED.intro_on_screen** [Frontend]: frames at 6 s / 14 s after boot: near-black 67% / 6%, mean luma 38.4 / 46.7, change 62.68. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check
- **e2e.PRESENTED.screen.title** []: b_title.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.main_menu** []: c_mainmenu.bmp: near-black 87%, mean luma 9.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.party_lobby** []: d_party.bmp: near-black 64%, mean luma 11 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.mode_list** []: e_modes.bmp: near-black 65%, mean luma 10.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.host_options** []: f_hostoptions.bmp: near-black 65%, mean luma 11.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.game_lobby** []: g_gamelobby.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.lobby_countdown** []: h_countdown.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.loading_screen** []: i_loading.bmp: near-black 14%, mean luma 23 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.match_pre_game** []: j_match_pending.bmp: near-black 0%, mean luma 46.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.pause_menu** []: m_pause.bmp: near-black 0%, mean luma 69.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.returned_frontend** []: n_return.bmp: near-black 94%, mean luma 6.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.pause_menu_drawn** [Frontend]: pause frame differs from the game frame by 54.34 luma (menu over the running match; backdrop blend PARTIAL per Frontend)
- **e2e.PRESENTED.pause_input_focus** [Frontend]: hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop)
- **e2e.PRESENTED.end_stats_screen** [Frontend]: end-of-match screen: near-black 21% - results layout (columns Level / Name / Score / Kills / Deaths, BLK F5) is a human check
- **e2e.PRESENTED.lifecycle.b_m1_spectating** [Frontend]: b_m1_spectating.bmp: near-black 0%, mean luma 21.8 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.c_m1_endstats** [Frontend]: c_m1_endstats.bmp: near-black 0%, mean luma 49.9 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.d_lobby_after1** [Frontend]: d_lobby_after1.bmp: near-black 57%, mean luma 15 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.e_m2_ingame** [Frontend]: e_m2_ingame.bmp: near-black 0%, mean luma 25.9 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.g_frontend** [Frontend]: g_frontend.bmp: near-black 94%, mean luma 6.7 - drawn; look is a human check (R7_sheet.png)
- **accept.controller** [Frontend]: XInput is mapped (Win32Window: A/B/X/Y/Start/Back/D-pad/shoulders/triggers/thumbs -> UiKey) but pad input cannot be injected safely; play the whole route with a controller
- **accept.present.a_ingame** [Frontend]: a_ingame.bmp: near-black 3%
- **accept.present.b_spectating** [Frontend]: b_spectating.bmp: near-black 0%
- **accept.present.c_endgame** [Frontend]: c_endgame.bmp: near-black 0%
- **accept.present.d_match2** [Frontend]: d_match2.bmp: near-black 3%
- **vehicle_states.visuals** [Rendering/Systems]: hover boosters (6 HoverFX, Size from thruster contribution) / BoostFx / RamFX for Nitro must be distinguishable and steady (PT 1.2 propulsion visuals)
- **e2e.PRESENTED.intro_on_screen** [Frontend]: frames at 6 s / 14 s after boot: near-black 84% / 2%, mean luma 14.7 / 52.8, change 57.34. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check
- **e2e.PRESENTED.screen.title** []: b_title.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.main_menu** []: c_mainmenu.bmp: near-black 87%, mean luma 9.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.party_lobby** []: d_party.bmp: near-black 64%, mean luma 11 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.mode_list** []: e_modes.bmp: near-black 65%, mean luma 10.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.host_options** []: f_hostoptions.bmp: near-black 65%, mean luma 11.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.game_lobby** []: g_gamelobby.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.lobby_countdown** []: h_countdown.bmp: near-black 57%, mean luma 14.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.loading_screen** []: i_loading.bmp: near-black 13%, mean luma 20.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.match_pre_game** []: j_match_pending.bmp: near-black 0%, mean luma 46.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.pause_menu** []: m_pause.bmp: near-black 0%, mean luma 69.3 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.returned_frontend** []: n_return.bmp: near-black 94%, mean luma 6.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.pause_menu_drawn** [Frontend]: pause frame differs from the game frame by 53.61 luma (menu over the running match; backdrop blend PARTIAL per Frontend)
- **e2e.PRESENTED.pause_input_focus** [Frontend]: hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop)
- **vehicle_collision.visible_blocking_geometry** [Gameplay/AssetTools]: runs whose pawn crossed VISIBLE triangles of meshes authored blocking (collision.json block=True) without crossing the exported collision: 389 (simple collision bodies are not exported, so this is a look-here list, not proof: visible-but-passable vs a simplified body). Passing through exported collision is the FAIL above. boost_cycle_s14: StaticMeshCollectionActor_11907.2;StaticMeshCollectionActor_11907.1; boost_cycle_s16: StaticMeshCollectionActor_4868.4; boost_cycle_s17: StaticMeshCollectionActor_4549.1;StaticMeshCollectionActor_4549.3;StaticMeshCollectionActor_4549.0;StaticMeshCollectionActor_4549.2;StaticMeshCollectionActor_12216.5;StaticMeshCollectionActor_12216.7;StaticMeshCollectionActor_2895.1; boost_cycle_s19: StaticMeshCollectionActor_3034.3; boost_cycle_s2: StaticMeshCollectionActor_4549.4; boost_cycle_s22: StaticMeshCollectionActor_11907.2;StaticMeshCollectionActor_11907.1
- **vehicle_collision.open_floor_stops** [Gameplay]: hard stops with no authored wall / step ahead on a level floor: 11 runs (a prop / volume the probe misses, or an inappropriate stop: look at the location). boost_cycle_s46@f360 (319.9,-706.2,-322.3) from 14.2 m/s rise -0.71; boost_cycle_s48@f90 (223.1,-706.4,-324.8) from 14.4 m/s rise -0.8; boost_cycle_s74@f90 (223.6,-706.4,-326.8) from 17.7 m/s rise -0.8; boost_cycle_s76@f270 (298.0,-706.5,-407.4) from 13.4 m/s rise -0.87; boost_right_s82@f326 (114.9,-706.2,-415.7) from 18.5 m/s rise -0.42; nitro_s16@f331 (229.5,-724.3,-387.3) from 14.5 m/s rise -3.53
- **visual.far_veil_worst** [Rendering]: largest screen coverage by one smoke / steam / fog effect at 30-60 m on the merged build: BckSillouhetteSmoke_2 100% (int-04 100%), Steam_7 85% (int-04 100%), BckSillouhetteSmoke_0 65% (int-04 59%). Whether distant smoke still veils the geometry is the human check
- **visual.glass_from_above** [Rendering]: glass contribution over its own surface seen from above, merged vs int-04: IAC_Decagon_GlassPan2_0.above 12% (was 12%), IAC_Decagon_GlassPan2_0.aboveclose 24% (was 24%), IAC_Decagon_GlassPan2_1.above 0% (was 0%), IAC_Decagon_GlassPan2_1.aboveclose 0% (was 0%), IAC_Glass_0.above 27% (was 19%), IAC_Glass_0.aboveclose 0% (was 0%), IAC_Glass_1.above 0% (was 0%), IAC_Glass_1.aboveclose 0% (was 0%), IAC_Glass2_dense_Dark_0.above 77% (was 72%), IAC_Glass2_dense_Dark_0.aboveclose 75% (was 67%), IAC_Glass2_dense_Dark_1.above 47% (was 48%), IAC_Glass2_dense_Dark_1.aboveclose 35% (was 35%), IAC_Glass2_light_0.above 0% (was 0%), IAC_Glass2_light_0.aboveclose 0% (was 0%), IAC_Glass2_light_1.above 0% (was 0%), IAC_Glass2_light_1.aboveclose 0% (was 0%)

Human playtest list: `tools/fidelity/HUMAN-CHECK-NEXT.md`.
