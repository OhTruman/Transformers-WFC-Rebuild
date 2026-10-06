# Playtest acceptance (Release)

exe `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build-release\bin\wfc_rebuild.exe`

**PASS 16 / FAIL 7 / KNOWN 0 / INFO 1 / SKIP 0 / HUMAN 5 / UNKNOWN 0 / WAITING 5 / PARTIAL 1**

| status | check | owner | evidence |
|---|---|---|---|
| PASS | intro.video | Frontend | intro movies with decoded frames 4/4; frame at 5 s near-black 84% |
| FAIL | intro.audio | Frontend/Systems | intro movies whose audio started on the Systems device (movie.audioStart with a handle): 0/4; decode failures 0. Audible balance / sync is a human check. Human playtest: the intro has no sound. Frontend documents movie audio as PARTIAL (10 mono FLAC tracks, layout unidentified; MoviesToAlwaysPlaySound lists the logos) - owner Frontend for decode, Systems for playback on the one audio device, AssetTools / RE for the track layout |
| FAIL | background.title | Frontend/Rendering/AssetTools | title: near-black 94%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| FAIL | background.main_menu | Frontend/Rendering/AssetTools | main_menu: near-black 87%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| FAIL | background.party_lobby | Frontend/Rendering/AssetTools | party_lobby: near-black 64%, flat untextured grey 0%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| FAIL | background.host_options | Frontend/Rendering/AssetTools | host_options: near-black 65%, flat untextured grey 0%; original = live 3D level UI_PartyLobby_m streaming UI_CharacterCustomization_m (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| FAIL | background.frontend_after_return | Frontend/Rendering/AssetTools | frontend_after_return: near-black 94%, flat untextured grey 0%; original = live 3D level UI_FrontEnd_m again after travel back (RE OV D, CONFIRMED). Black = the level / sublevel / camera is not running; flat grey = placeholder or missing-material geometry; otherwise drawn - fidelity is a human check |
| INFO | title.background_summary | AssetTools/Frontend/Rendering | title frame near-black 94%, main menu 87%. The original shows the UI_FrontEnd_m 3D scene (orbit camera, Cybertron, fireworks; RE 1.2 Kismet). Documented gap: UI_FrontEnd_m not exported (AssetTools) / not rendered (Frontend / Rendering) |
| PASS | title.start_advances | Frontend | Start (keyboard F3) -> ShowDeviceSelectionUI + main menu: True; A (Enter) before it advanced: False. CONFIRMED original: the title reacts to buttonStart only (FrontEnd_GFX inputEvent), so Enter doing nothing is original; keyboard Start is F3 in the rebuild's PROVISIONAL key map - usability decision for Frontend, not a fidelity defect |
| HUMAN | controller | Frontend | XInput is mapped (Win32Window: A/B/X/Y/Start/Back/D-pad/shoulders/triggers/thumbs -> UiKey) but pad input cannot be injected safely; play the whole route with a controller |
| PASS | nav.back_mode_list | Frontend | B in the mode list returns to the party lobby root: frame difference to the root 3.23 luma (focus highlight may differ) |
| PASS | nav.back_host_options | Frontend | B in the host options returns to the mode list: difference 0.62 |
| PASS | nav.back_twice | Frontend | B twice from the host options returns to the party root: difference 3.8 |
| PASS | nav.back_party_root | Frontend | B at the party root -> Game.QuitToMainMenu 1 -> travel to UI_FrontEnd_m 1 (BLK A3) |
| PASS | nav.back_sound | Frontend | BUTTON_BACK requested 5 times for 4 B presses (BLK A4) |
| PASS | nav.flow_completed | Frontend | navigation script completed |
| FAIL | loading.updates_during_load | Frontend/Integration | 18 window captures over 4.9 s of map load (4.057 s blocking per match.loaded); consecutive captures that change: 4/17; longest frozen interval 3.7 s (FAIL above 2.5 s). A frozen loading screen = the load blocks the presentation loop (Frontend documents the blocking load as PARTIAL; RE: native threaded load + Bink underlay keeps animating) |
| PASS | loading.overlay_gone | Frontend/Integration | in-game frame 2 s after InGame: near-black 3%, mean luma 18.1 (the loading underlay was composited over the match until Integration 7a11ce2) |
| PASS | match.clock_runs | Gameplay | Gameplay RemainingTime: 25 samples, 23 1-s decrements (GRI 1 Hz timer) |
| PASS | match.score_updates | Gameplay | team score lines: team=0 score=1 / team=1 score=1 / team=0 score=2 / team=1 score=2 / team=0 score=3 |
| PASS | match.kill_death_events | Gameplay/Integration | MATCH kill 5, MATCH death 5, flow match.kill 5 |
| PASS | match.respawn_sequence | Gameplay/Integration | respawns 4; Spectating UI shown |
| PASS | match.end_rule | Gameplay | reason=score_limit winner=0 t=22 (gameplay reason Score) |
| PARTIAL | match.result_exposed | Frontend/Integration | EndGameStats: data-store reads the rebuild does not answer: GetCollectionRowCount <TnMenuItems:Specialties>; ReadValue <CurrentGame:AttackingTeamIndex> (Integration documents 'undefined / +NaN' in the experience panel: no profile / XP service) |
| PASS | match.second_match_reset | Gameplay | second match from the lobby: MATCH restart + InGame reached (score reset is checked by m05-e2e-gate R7) |
| WAITING | hud.clock_displayed | Frontend/Rendering | Hud_GFX (clock / team score / health / ammo, BLK G) open in play: False; dumps 0. Until the HUD movie is drawn, displayed vs Gameplay values cannot be compared (owner Frontend / Rendering: HUD ownership handoff) |
| WAITING | hud.score_displayed | Frontend/Rendering | team score bars (<CurrentGame:Teams> polled every 500 ms, BLK G) - waiting for the HUD movie |
| WAITING | hud.kill_feed_event | Frontend/Rendering | kills 5; kill-feed (TnDeathMessage: DeathMessage(killer, victim) / SuicideMessage, 3.0 s lifetime, CONFIRMED) entries traced: 0. Original presentation (RE OV A2, CONFIRMED): Hud_GFX _global.GameMessage(html) at (84, 626), 5 rows, each 5.0 s + 1.0 s fade, local white / teammate (80,181,213) / enemy (240,60,60), weapon icon tokens. Gameplay 391b7f0 exposes HudGameState::killFeed; WAITING until a trace or drawn feed exists |
| WAITING | hud.point_event | Frontend | kill -> _global.PointEvent (TnHUD KillTransactionObserver, CONFIRMED authored) pushes traced: 0 |
| WAITING | hud.values_match_gameplay | Frontend | HUD text fields vs MATCH timer / score lines (hud_table.csv) - waiting for the HUD movie |
| PASS | hud.minimap_absent | Frontend/Rendering | CONFIRMED original: no minimap, radar or compass (RE OV A9, exhaustive absence; AssetTools negative search). Minimap / radar evidence in this run: 0. A minimap must not be presented as original |
| HUMAN | present.a_ingame | Frontend | a_ingame.bmp: near-black 3% |
| HUMAN | present.b_spectating | Frontend | b_spectating.bmp: near-black 0% |
| HUMAN | present.c_endgame | Frontend | c_endgame.bmp: near-black 0% |
| HUMAN | present.d_match2 | Frontend | d_match2.bmp: near-black 3% |
