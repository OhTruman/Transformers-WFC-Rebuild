# Presentation gate - integration/milestone-06 95edd7b (human config: Debug + Integration render data) Debug 95edd7b34107e6bab5a44d6cbbc191db46209e8a

Verdict: **VISUALLY BROKEN**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m6int\build\bin\wfc_rebuild.exe |
| render data | F:\Transformers Rebuild\Rebuild-Experimental\work\m06h\render_integ |
| result | pass 15 / fail 26 / known 0 / info 0 / skip 0 / human 0 / unknown 1 / waiting 0 / partial 0 |

## FAIL

- **present.frontend.scene.a00_title** [Frontend/Rendering]: title 3D scene region: detail 0.124, black 0.595, largest blank area 0.6, untextured / placeholder surface largest 0.028 total 0.037, noise texture 0.001 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a01_title_9s** [Frontend/Rendering]: title 3D scene region: detail 0.116, black 0.584, largest blank area 0.6, untextured / placeholder surface largest 0.025 total 0.036, noise texture 0 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.screen.a02_party** [Frontend/Rendering]: screen: detail 0.112, black 0, largest blank 0.437, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.713, flat grey 0%
- **present.frontend.screen.b02_party** [Frontend/Rendering]: screen: detail 0.112, black 0, largest blank 0.384, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.849, flat grey 0%
- **present.frontend.screen.b03_lobby** [Frontend/Rendering]: screen: detail 0.179, black 0.008, largest blank 0.314, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.608, flat grey 0%
- **present.charselect.body_resolves** [AssetTools/Gameplay]: selected chassis Jet4; robot body loaded: Optimus (Optimus for a non-Optimus selection = the selection is not drawn)
- **present.gameplay.world.a07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.056 (PASS >= 0.15, FAIL < 0.10), black 0.05, largest blank 0.562, untextured 0.004 / 0.027, noise 0
- **present.gameplay.world.a08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.02 (PASS >= 0.15, FAIL < 0.10), black 0.283, largest blank 0.35, untextured 0.006 / 0.017, noise 0
- **present.gameplay.world.a09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.022 (PASS >= 0.15, FAIL < 0.10), black 0.674, largest blank 0.65, untextured 0 / 0, noise 0
- **present.gameplay.world.b07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.05 (PASS >= 0.15, FAIL < 0.10), black 0.058, largest blank 0.37, untextured 0.004 / 0.026, noise 0
- **present.gameplay.world.b08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.019 (PASS >= 0.15, FAIL < 0.10), black 0.3, largest blank 0.373, untextured 0.007 / 0.017, noise 0
- **present.gameplay.world.b09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.022 (PASS >= 0.15, FAIL < 0.10), black 0.675, largest blank 0.647, untextured 0 / 0, noise 0
- **present.transition.second_match.07_spawn** [Integration/Rendering]: world detail match 1 0.056 vs match 2 0.05 (ratio 0.89; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.second_match.08_moving** [Integration/Rendering]: world detail match 1 0.02 vs match 2 0.019 (ratio 0.95; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.pause_cleared.a** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.971, to the gameplay frame before pause 0.254 (FAIL: the frame still looks like the pause screen)
- **present.transition.pause_cleared.b** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.971, to the gameplay frame before pause 0.254 (FAIL: the frame still looks like the pause screen)
- **present.exclusive.menu_visible_while_moving** [Frontend]: pause menu still drawn after Resume: True; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement
- **present.gameplay.route_vs_direct** [Integration]: same start (TnTeamPlayerStart_12481) - direct boot world detail 0.197, frontend-launched 0.056 (ratio 0.28; FAIL < 0.5: the frontend-launched match loses world content that direct boot draws -> route-specific seam, not the map data)
- **present.watchdog.extras_movies.softlock** [Frontend]: extras_movies: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: none; after 3 Back press(es) the frame matches the main menu False (scene similarity 0.075, still-inside similarity 0.958); focus present in the screen's dumps False
- **present.watchdog.extras_movies.classification** [Frontend]: extras_movies: DISPLAY CORRECT (automated part). a movie started: False
- **present.watchdog.extras_credits.softlock** [Frontend]: extras_credits: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: none; after 2 Back press(es) the frame matches the main menu False (scene similarity -0.011, still-inside similarity 0.924); focus present in the screen's dumps True
- **present.watchdog.extras_credits.classification** [Frontend]: extras_credits: DISPLAY CORRECT (automated part). after Accept on Credits the screen differs from the Extras list: False (similarity 0.915)
- **present.watchdog.accounts_create.softlock** [Frontend]: accounts_create: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: none; after 3 Back press(es) the frame matches the main menu False (scene similarity -0.043, still-inside similarity 0.959); focus present in the screen's dumps True
- **present.watchdog.accounts_create.classification** [Frontend]: accounts_create: DISPLAY CORRECT (automated part). typed 'WFCQA' with ordinary key events: text in a dump False; frame changed after typing False
- **present.watchdog.settings_controls.classification** [Frontend]: settings_controls: DISPLAY CORRECT (automated part). Mouse / Keyboard Layout page, Accept on the first binding, then the K key: binding texts changed True, a 'K' binding shown False
- **present.watchdog.customization.classification** [Frontend/AssetTools]: customization: DISPLAY CORRECT (automated part). character model loaded for the preview: False; 'LOADING' still shown: False

## Other

- PASS **present.route.completes**: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- PASS **present.frontend.scene.a13_frontend_after**: title 3D scene region: detail 0.384, black 0.007, largest blank area 0.048, untextured / placeholder surface largest 0.028 total 0.124, noise texture 0.01 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.b13_frontend_after**: title 3D scene region: detail 0.385, black 0.007, largest blank area 0.025, untextured / placeholder surface largest 0.028 total 0.12, noise texture 0.01 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.screen.a03_lobby**: screen: detail 0.18, black 0.008, largest blank 0.318, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.467, flat grey 0%
- PASS **present.frontend.screen.a04_loading**: screen: detail 0.077, black 0.355, largest blank 0.172, untextured 0.001 / 0.001, noise 0
- PASS **present.charselect.ui.a**: Choose Character screen: detail 0.244; dump texts with class names: 2
- PASS **present.charselect.ui.b**: Choose Character screen: detail 0.239; dump texts with class names: 2
- PASS **present.charselect.gameplay_receives**: selected Scientist (Jet4); Gameplay spawn chassis Jet4,Jet4
- UNKNOWN **present.exclusive.no_movement_under_menu**: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 29.4 m; Paused 5.9 m; InGame 17.8 m; Paused 5.1 m; WaitingOnGameStart 0.0 m; InGame 29.8 m; Paused 7.5 m; InGame 16.7 m; Paused 5.7 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- PASS **present.reference.streets_pipeline**: 13 fixed Streets cameras vs the known-good reference (streets_spawn_m05_1e14900): median structural similarity 1; views lost (detail ratio < 0.5 or similarity < 0.4): 0 . FAIL flags the whole merged visual pipeline regardless of how many maps load
- PASS **present.watchdog.extras_concept.softlock**: extras_concept: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 2 Back press(es) the frame matches the main menu True (scene similarity 0.607, still-inside similarity 0.111); focus present in the screen's dumps True
- PASS **present.watchdog.extras_concept.classification**: extras_concept: FULLY FUNCTIONAL. 
- PASS **present.watchdog.settings_controls.softlock**: settings_controls: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.55, still-inside similarity 0.179); focus present in the screen's dumps True
- PASS **present.watchdog.customization.softlock**: customization: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.887, still-inside similarity 0.185); focus present in the screen's dumps True
- PASS **present.watchdog.back_forward.softlock**: back_forward: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 0 Back press(es) the frame matches the main menu True (scene similarity 0.642, still-inside similarity 0.993); focus present in the screen's dumps True
- PASS **present.watchdog.back_forward.classification**: back_forward: FULLY FUNCTIONAL. 
