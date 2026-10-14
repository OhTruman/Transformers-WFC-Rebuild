# Final gate - 95edd7b34107e6bab5a44d6cbbc191db46209e8a

| | |
|---|---|
| integration commit | `95edd7b34107e6bab5a44d6cbbc191db46209e8a` |
| Release exe | `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m6int\build-release\bin\wfc_rebuild.exe` |
| Debug exe | `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m6int\build\bin\wfc_rebuild.exe`  |
| run | 2026-10-04 16:10 |

**PRODUCT FAIL 12 / TEST/HARNESS FAIL 0 / PASS 214 / KNOWN 1 / UNKNOWN/WAITING 5 / HUMAN 48 / INFO 17 / SKIP 12 / PARTIAL 1**

| suite | result |
|---|---|
| lifecycle gate (Release) | PRODUCT FAIL 9 / TEST/HARNESS FAIL 0 / PASS 95 / KNOWN 1 / UNKNOWN/WAITING 0 / HUMAN 21 / INFO 8 / SKIP 6 / PARTIAL 0 |
| playtest acceptance | PRODUCT FAIL 2 / TEST/HARNESS FAIL 0 / PASS 17 / KNOWN 0 / UNKNOWN/WAITING 3 / HUMAN 10 / INFO 1 / SKIP 1 / PARTIAL 1 |
| motion / jitter | PRODUCT FAIL 1 / TEST/HARNESS FAIL 0 / PASS 34 / KNOWN 0 / UNKNOWN/WAITING 2 / HUMAN 1 / INFO 1 / SKIP 0 / PARTIAL 0 |
| lifecycle gate (Debug) | PRODUCT FAIL 0 / TEST/HARNESS FAIL 0 / PASS 68 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 14 / INFO 3 / SKIP 5 / PARTIAL 0 |
| transform stress | not run |
| vehicle collision | not run |
| visual (merged vs baseline vs lane) | PRODUCT FAIL 0 / TEST/HARNESS FAIL 0 / PASS 0 / KNOWN 0 / UNKNOWN/WAITING 0 / HUMAN 2 / INFO 4 / SKIP 0 / PARTIAL 0 |
| lifetime soak | not run |

## PRODUCT FAIL (owner lane): 12

- **e2e.STATE.match_url** [Frontend]: MP_IAC_Seed_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=501; mismatched keys: MapId=501 (RE 3.1 / BLK D1)
- **e2e.STATE.match_loading_text** [Frontend]: title 'Team Deathmatch' message 'in Seed Of Corruption' tips 3
- **e2e.OWNERSHIP.launch_request** [Frontend]: frontend MatchLaunch: mode TDM goal 40 time 900 runtimeDir MP_IAC_Seed team 1
- **e2e.MATCH.spawn_team_cluster** [Gameplay]: spawned at TnTeamPlayerStart_15214 in TnSpawnCluster_11813, team 0 (RE 5.3 / BLK E6: initial clusters Autobots 7810, Decepticons 4159, locked 15 s)
- **e2e.OWNERSHIP.audio_level** [Systems/Integration]: Systems AMB level during the match: MP_IAC_Seed
- **e2e.MATCH.match_over_return_to_lobby** [Gameplay/Frontend]: travel UI_Lobby_m?Game=TransContent.TnGameLobbyGameTeam?GameModeTag=TDM?GameTeamStatus=3?MaxPlayers=10?IconicMode=0?MapId=501?listen after 15.0 s (BLK F4/F6: MatchOver 15 s -> ReturnToGameLobby UI_Lobby_m?...?MapId=)
- **e2e.MATCH.match_over_15s_return** [Gameplay/Integration]: GameEnded -> lobby travel after 15.0, 15.0 s; URLs keep MapId: 501,501 (BLK F4/F6)
- **e2e.STATE.lobby_after_score_limit** [Frontend]: lobby after match 1: level GameLobby mode TDM mapId 501 (BLK C7: selection index 0 = Streets in the rebuild)
- **e2e.SELFTEST.WFC_CHAOS** [Gameplay]: seeded random play from nav points: under the map / KillZ / stuck / inside props: CHAOS SUMMARY: 123 starts x 20 s (147477 ticks, 691 transform presses, 635 jumps, 683 boosts): 1 runs UNDER THE MAP (BSP floor above), 0 KillZ, 2 stuck; 9 runs with the pawn inside / under a prop / CHAOS STUCK from TnTeamPlayerStart_12697 at (382.1 -718.9 -494.2) form VEHICLE blockers StaticMeshCollectionActor_11853,StaticMeshCollectionActor_11853,StaticMeshCollectionActor_1374,StaticMeshCollectionActor_1374 / CHAOS UNDER-FLOOR from TnTeamPlayerStart_2602: 26 frames, worst 2.83 m at (148.3 -709.4 -312.7) blockers StaticMeshCollectionActor_10677,StaticMeshCollectionActor_4989 / CHAOS STUCK from TnTeamPlayerStart_7032 at (311.5 -706.5 -319.1) form ROBOT blockers BlockingVolume_1621,BlockingVolume_8675,StaticMeshCollectionActor_1036,StaticMeshCollectionActor_10441,+6 (exit 1)
- **accept.nav.back_twice** [Frontend]: B twice from the host options returns to the party root: difference 6.08
- **accept.hud.point_event** [Frontend]: kill -> _global.PointEvent (TnHUD KillTransactionObserver, CONFIRMED authored) pushes traced: 0
- **jitter.pacing.robot_walk_turn** [Gameplay]: pawn screen-x second difference per rendered frame (mean / max): 60 Hz 0.00197 / 0.19174 (5.0% frames > 0.004); 144 Hz 0.00342 / 0.05417 (24.6% frames > 0.004); 240 Hz 0 / 4E-05 (0.0% frames > 0.004) (Rendering M08: broken 0.0095 mean at 144 Hz, fixed <= 0.0001; FAIL = mean > 0.002 or > 5% of frames > 0.004)

## TEST / HARNESS FAIL (Experimental): 0


## KNOWN (documented gaps): 1

- **e2e.OWNERSHIP.gorge_not_selectable** [AssetTools/Integration]: map selector stepped right: selected 502; Gorge selectable False. Integration lists Gorge disabled by design (no AssetTools render export; not validated by Gameplay / Rendering) - not counted as a working map

## UNKNOWN / WAITING (evidence or presentation pending): 6

- **accept.match.result_exposed** [Frontend/Integration]: EndGameStats: data-store reads the rebuild does not answer: ReadValue <CurrentGame:AttackingTeamIndex> (Integration documents 'undefined / +NaN' in the experience panel: no profile / XP service)
- **accept.hud.clock_displayed** [Frontend/Rendering]: Hud_GFX (clock / team score / health / ammo, BLK G) open in play: True; dumps 0. Until the HUD movie is drawn, displayed vs Gameplay values cannot be compared (owner Frontend / Rendering: HUD ownership handoff)
- **accept.hud.score_displayed** [Frontend/Rendering]: team score bars (<CurrentGame:Teams> polled every 500 ms, BLK G) - waiting for the HUD movie
- **accept.hud.values_match_gameplay** [Frontend]: HUD text fields vs MATCH timer / score lines (hud_table.csv) - waiting for the HUD movie
- **jitter.present.vehicle** [Frontend/Integration]: vehicle presentation frames need a scripted transform in a frontend-launched match (proposal for Frontend / Integration: FRONTEND_SCRIPT step press:transform / hold:boost). Vehicle simulation jitter: part A; look: HUMAN
- **jitter.bone_level** [Gameplay/Rendering]: no per-bone transform log in the product: root vs individual-bone separation cannot be measured (proposal: WFC_POSELOG 'POSE frame bone pos' for root / pelvis / head / hands). Presentation evidence above + HUMAN-CHECK

## HUMAN CHECK (framing evidence): 48

- **e2e.PRESENTED.intro_on_screen** [Frontend]: frames at 6 s / 14 s after boot: near-black 73% / 6%, mean luma 21.5 / 49.5, change 60.01. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check
- **e2e.PRESENTED.screen.title** []: b_title.bmp: near-black 1%, mean luma 77.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.main_menu** []: c_mainmenu.bmp: near-black 1%, mean luma 78.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.party_lobby** []: d_party.bmp: near-black 0%, mean luma 37.3 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.mode_list** []: e_modes.bmp: near-black 0%, mean luma 37 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.host_options** []: f_hostoptions.bmp: near-black 0%, mean luma 37.9 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.game_lobby** []: g_gamelobby.bmp: near-black 0%, mean luma 39.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.lobby_countdown** []: h_countdown.bmp: near-black 0%, mean luma 39.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.loading_screen** []: i_loading.bmp: near-black 13%, mean luma 24.9 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.match_pre_game** []: j_match_pending.bmp: near-black 1%, mean luma 49.2 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.pause_menu** []: m_pause.bmp: near-black 2%, mean luma 29.5 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.returned_frontend** []: n_return.bmp: near-black 1%, mean luma 77.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.pause_menu_drawn** [Frontend]: pause frame differs from the game frame by 35.61 luma (menu over the running match; backdrop blend PARTIAL per Frontend)
- **e2e.PRESENTED.pause_input_focus** [Frontend]: hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop)
- **e2e.PRESENTED.end_stats_screen** [Frontend]: end-of-match screen: near-black 4% - results layout (columns Level / Name / Score / Kills / Deaths, BLK F5) is a human check
- **e2e.PRESENTED.pc.main_menu** [Frontend]: PC main menu frame near-black 1%: the PC menu look is a human check; the spec remains the Xbox 360 presentation (R1)
- **e2e.PRESENTED.lifecycle.b_m1_spectating** [Frontend]: b_m1_spectating.bmp: near-black 1%, mean luma 60.5 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.c_m1_endstats** [Frontend]: c_m1_endstats.bmp: near-black 4%, mean luma 31.5 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.d_lobby_after1** [Frontend]: d_lobby_after1.bmp: near-black 0%, mean luma 37.1 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.e_m2_ingame** [Frontend]: e_m2_ingame.bmp: near-black 2%, mean luma 55.9 - drawn; look is a human check (R7_sheet.png)
- **e2e.PRESENTED.lifecycle.g_frontend** [Frontend]: g_frontend.bmp: near-black 1%, mean luma 77.8 - drawn; look is a human check (R7_sheet.png)
- **accept.background.title** [Frontend/Rendering/AssetTools]: title: near-black 1%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.main_menu** [Frontend/Rendering/AssetTools]: main_menu: near-black 1%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.party_lobby** [Frontend/Rendering/AssetTools]: party_lobby: near-black 0%, flat untextured grey 0%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.host_options** [Frontend/Rendering/AssetTools]: host_options: near-black 0%, flat untextured grey 0%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.background.frontend_after_return** [Frontend/Rendering/AssetTools]: frontend_after_return: near-black 1%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m again after travel back (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check
- **accept.controller** [Frontend]: XInput is mapped (Win32Window: A/B/X/Y/Start/Back/D-pad/shoulders/triggers/thumbs -> UiKey) but pad input cannot be injected safely; play the whole route with a controller
- **accept.present.a_ingame** [Frontend]: a_ingame.bmp: near-black 1%
- **accept.present.b_spectating** [Frontend]: b_spectating.bmp: near-black 2%
- **accept.present.c_endgame** [Frontend]: c_endgame.bmp: near-black 8%
- **accept.present.d_match2** [Frontend]: d_match2.bmp: near-black 1%
- **vehicle_states.visuals** [Rendering/Systems]: hover boosters (6 HoverFX, Size from thruster contribution) / BoostFx / RamFX for Nitro must be distinguishable and steady (PT 1.2 propulsion visuals)
- **e2e.PRESENTED.intro_on_screen** [Frontend]: frames at 6 s / 14 s after boot: near-black 72% / 7%, mean luma 25.8 / 38.1, change 54.91. Drawn: whether it is the shipped intro presentation (logos, FMV, no audio PARTIAL) is a human check
- **e2e.PRESENTED.screen.title** []: b_title.bmp: near-black 1%, mean luma 77.1 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.main_menu** []: c_mainmenu.bmp: near-black 1%, mean luma 78.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.party_lobby** []: d_party.bmp: near-black 0%, mean luma 37.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.mode_list** []: e_modes.bmp: near-black 0%, mean luma 37 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.host_options** []: f_hostoptions.bmp: near-black 0%, mean luma 38 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.game_lobby** []: g_gamelobby.bmp: near-black 0%, mean luma 39.6 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.lobby_countdown** []: h_countdown.bmp: near-black 0%, mean luma 39.8 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.loading_screen** []: i_loading.bmp: near-black 13%, mean luma 22.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.match_pre_game** []: j_match_pending.bmp: near-black 1%, mean luma 49.4 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.pause_menu** []: m_pause.bmp: near-black 3%, mean luma 29.1 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.screen.returned_frontend** []: n_return.bmp: near-black 1%, mean luma 77.7 - drawn; whether it looks like WFC is a human check (R1_sheet.png)
- **e2e.PRESENTED.pause_menu_drawn** [Frontend]: pause frame differs from the game frame by 38.48 luma (menu over the running match; backdrop blend PARTIAL per Frontend)
- **e2e.PRESENTED.pause_input_focus** [Frontend]: hold W, press Esc: the robot must stop while the pause menu is up, and the world keeps running (scripted WFC_AUTO* input is injected after the focus gate; real keys cannot be sent safely on a shared desktop)
- **visual.far_veil_worst** [Rendering]: largest screen coverage by one smoke / steam / fog effect at 30-60 m on the merged build: BckSillouhetteSmoke_2 100% (int-04 100%), Steam_7 85% (int-04 85%), BckSillouhetteSmoke_0 65% (int-04 65%). Whether distant smoke still veils the geometry is the human check
- **visual.glass_from_above** [Rendering]: glass contribution over its own surface seen from above, merged vs int-04: IAC_Decagon_GlassPan2_0.above 12% (was 12%), IAC_Decagon_GlassPan2_0.aboveclose 24% (was 24%), IAC_Decagon_GlassPan2_1.above 0% (was 0%), IAC_Decagon_GlassPan2_1.aboveclose 0% (was 0%), IAC_Glass_0.above 27% (was 27%), IAC_Glass_0.aboveclose 0% (was 0%), IAC_Glass_1.above 0% (was 0%), IAC_Glass_1.aboveclose 0% (was 0%), IAC_Glass2_dense_Dark_0.above 77% (was 77%), IAC_Glass2_dense_Dark_0.aboveclose 75% (was 75%), IAC_Glass2_dense_Dark_1.above 47% (was 47%), IAC_Glass2_dense_Dark_1.aboveclose 35% (was 35%), IAC_Glass2_light_0.above 0% (was 0%), IAC_Glass2_light_0.aboveclose 0% (was 0%), IAC_Glass2_light_1.above 0% (was 0%), IAC_Glass2_light_1.aboveclose 0% (was 0%)

Human playtest list: `tools/fidelity/HUMAN-CHECK-NEXT.md`.
