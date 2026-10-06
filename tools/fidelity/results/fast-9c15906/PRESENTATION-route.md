# Presentation gate - 9c159066cac1fe332b922406f1f32a6030e7177d FAST Release 9c159066cac1fe332b922406f1f32a6030e7177d

Verdict: **NO CATASTROPHIC PRESENTATION FAILURE FOUND (human look still required)**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin\wfc_rebuild.exe |
| render data | product default (<exe>\..\..\work\render) |
| result | pass 26 / fail 0 / known 0 / info 0 / skip 0 / human 0 / unknown 1 / waiting 0 / partial 3 |

## FAIL


## Other

- PASS **present.route.completes**: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- PASS **present.frontend.scene.a00_title**: title 3D scene region: detail 0.815, black 0.052, largest blank area 0.024, untextured / placeholder surface largest 0.035 total 0.071, noise texture 0.076 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.a01_title_9s**: title 3D scene region: detail 0.802, black 0.075, largest blank area 0.022, untextured / placeholder surface largest 0.044 total 0.046, noise texture 0.059 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.a13_frontend_after**: title 3D scene region: detail 0.814, black 0.051, largest blank area 0.029, untextured / placeholder surface largest 0.036 total 0.072, noise texture 0.074 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.b13_frontend_after**: title 3D scene region: detail 0.813, black 0.052, largest blank area 0.024, untextured / placeholder surface largest 0.036 total 0.071, noise texture 0.076 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.screen.a02_party**: screen: detail 0.111, black 0, largest blank 0.271, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.579 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.frontend.screen.a03_lobby**: screen: detail 0.18, black 0.008, largest blank 0.198, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.121, largest blank 0.488 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.frontend.screen.a04_loading**: screen: detail 0.081, black 0.347, largest blank 0.177, untextured 0.001 / 0.001, noise 0
- PASS **present.frontend.screen.b02_party**: screen: detail 0.113, black 0.001, largest blank 0.283, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.794 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.frontend.screen.b03_lobby**: screen: detail 0.18, black 0.009, largest blank 0.181, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.121, largest blank 0.531 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.charselect.ui.a**: Choose Character screen: detail 0.345; dump texts with class names: 2
- PASS **present.charselect.ui.b**: Choose Character screen: detail 0.344; dump texts with class names: 2
- PASS **present.charselect.gameplay_receives**: selected Scientist (Jet); Gameplay spawn chassis Jet,Jet
- PASS **present.charselect.body_resolves**: selected chassis Jet; robot body loaded: Truck,Jet (Optimus for a non-Optimus selection = the selection is not drawn)
- PASS **present.gameplay.world.a07_spawn**: world region (HUD band and player excluded): textured detail 0.296 (PASS >= 0.15, FAIL < 0.10), black 0.087, largest blank 0.3, untextured 0.002 / 0.012, noise 0.003
- PASS **present.gameplay.world.a08_moving**: world region (HUD band and player excluded): textured detail 0.197 (PASS >= 0.15, FAIL < 0.10), black 0.044, largest blank 0.182, untextured 0.004 / 0.007, noise 0.001
- PARTIAL **present.gameplay.world.a09_moving2**: world region (HUD band and player excluded): textured detail 0.138 (PASS >= 0.15, FAIL < 0.10), black 0.577, largest blank 0.542, untextured 0.003 / 0.012, noise 0
- PASS **present.gameplay.world.b07_spawn**: world region (HUD band and player excluded): textured detail 0.291 (PASS >= 0.15, FAIL < 0.10), black 0.087, largest blank 0.273, untextured 0.009 / 0.021, noise 0.002
- PASS **present.gameplay.world.b08_moving**: world region (HUD band and player excluded): textured detail 0.188 (PASS >= 0.15, FAIL < 0.10), black 0.047, largest blank 0.093, untextured 0.009 / 0.01, noise 0.001
- PARTIAL **present.gameplay.world.b09_moving2**: world region (HUD band and player excluded): textured detail 0.134 (PASS >= 0.15, FAIL < 0.10), black 0.552, largest blank 0.544, untextured 0.003 / 0.012, noise 0
- PASS **present.transition.second_match.07_spawn**: world detail match 1 0.296 vs match 2 0.291 (ratio 0.98; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- PASS **present.transition.second_match.08_moving**: world detail match 1 0.197 vs match 2 0.188 (ratio 0.95; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- PASS **present.transition.pause_cleared.a**: 3.5 s after Resume: structural similarity to the PAUSE frame 0.13, to the gameplay frame before pause 0.692 (FAIL: the frame still looks like the pause screen)
- PASS **present.transition.pause_cleared.b**: 3.5 s after Resume: structural similarity to the PAUSE frame 0.139, to the gameplay frame before pause 0.685 (FAIL: the frame still looks like the pause screen)
- PASS **present.exclusive.menu_visible_while_moving**: pause menu still drawn after Resume: False; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement
- UNKNOWN **present.exclusive.no_movement_under_menu**: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 74.9 m; Paused 0.0 m; InGame 3.3 m; Paused 16.0 m; WaitingOnGameStart 0.0 m; InGame 75.7 m; Paused 0.0 m; InGame 3.7 m; Paused 15.8 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- PASS **present.gameplay.route_vs_direct**: same start (TnTeamPlayerStart_8835) - direct boot world detail 0.437, frontend-launched 0.296 (ratio 0.68; FAIL < 0.5: the frontend-launched match loses world content that direct boot draws -> route-specific seam, not the map data)
- PASS **present.watchdog.customization.softlock**: customization: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.908, still-inside similarity 0.211); focus present in the screen's dumps True
- PASS **present.watchdog.customization.classification**: customization: FULLY FUNCTIONAL. preview body loaded: True (TR_Sideswipe_ROBO_p, TR_Barricade_ROBO_p); customize.preview owner: renderer; 'LOADING' still shown: False; the posed body on screen: HUMAN (sheet)
- PARTIAL **present.watchdog.customization.emblem_state**: STATE ONLY (trace): 12 emblem transitions; Opacity on 4 (8803,16331,5865,1120); Highlighted on 0 () - not exercised: this path never focuses a chassis button; Opacity still on after leaving Create a Character: none. Visible glow: drawable: renderer has setFrontendMaterialParam and F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin\..\..\work\render\UI_CharacterCustomization\material_instance_actors.json exists - HUMAN check (item 2): overview = red Autobot + purple Decepticon logos behind the robots; chassis menu = the selected faction's logo glowing, the other hidden; party lobby / class list = none
