# Presentation gate - 175a6348cb9490fb090f467500ff8bdeb85ec1de FAST Release 175a6348cb9490fb090f467500ff8bdeb85ec1de

Verdict: **NO CATASTROPHIC PRESENTATION FAILURE FOUND (human look still required)**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin\wfc_rebuild.exe |
| render data | product default (<exe>\..\..\work\render) |
| result | pass 26 / fail 0 / known 0 / info 0 / skip 0 / human 0 / unknown 1 / waiting 0 / partial 3 |

## FAIL


## Other

- PASS **present.route.completes**: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- PASS **present.frontend.scene.a00_title**: title 3D scene region: detail 0.814, black 0.046, largest blank area 0.015, untextured / placeholder surface largest 0.038 total 0.065, noise texture 0.074 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.a01_title_9s**: title 3D scene region: detail 0.722, black 0.178, largest blank area 0.091, untextured / placeholder surface largest 0.035 total 0.045, noise texture 0.058 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.a13_frontend_after**: title 3D scene region: detail 0.812, black 0.046, largest blank area 0.02, untextured / placeholder surface largest 0.038 total 0.066, noise texture 0.073 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.b13_frontend_after**: title 3D scene region: detail 0.813, black 0.046, largest blank area 0.013, untextured / placeholder surface largest 0.037 total 0.064, noise texture 0.074 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.screen.a02_party**: screen: detail 0.112, black 0, largest blank 0.337, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.593 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.frontend.screen.a03_lobby**: screen: detail 0.18, black 0.008, largest blank 0.244, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.121, largest blank 0.422 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.frontend.screen.a04_loading**: screen: detail 0.081, black 0.347, largest blank 0.177, untextured 0.001 / 0.001, noise 0
- PASS **present.frontend.screen.b02_party**: screen: detail 0.112, black 0, largest blank 0.389, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.841 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.frontend.screen.b03_lobby**: screen: detail 0.179, black 0.008, largest blank 0.291, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.12, largest blank 0.582 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- PASS **present.charselect.ui.a**: Choose Character screen: detail 0.26; dump texts with class names: 2
- PASS **present.charselect.ui.b**: Choose Character screen: detail 0.259; dump texts with class names: 2
- PASS **present.charselect.gameplay_receives**: selected Scientist (Jet4); Gameplay spawn chassis Jet4,Jet4
- PASS **present.charselect.body_resolves**: selected chassis Jet4; robot body loaded: Truck,Jet4 (Optimus for a non-Optimus selection = the selection is not drawn)
- PASS **present.gameplay.world.a07_spawn**: world region (HUD band and player excluded): textured detail 0.184 (PASS >= 0.15, FAIL < 0.10), black 0.301, largest blank 0.144, untextured 0.001 / 0.011, noise 0.001
- PASS **present.gameplay.world.a08_moving**: world region (HUD band and player excluded): textured detail 0.173 (PASS >= 0.15, FAIL < 0.10), black 0.077, largest blank 0.077, untextured 0.078 / 0.247, noise 0.001
- PARTIAL **present.gameplay.world.a09_moving2**: world region (HUD band and player excluded): textured detail 0.146 (PASS >= 0.15, FAIL < 0.10), black 0.119, largest blank 0.158, untextured 0.067 / 0.242, noise 0
- PASS **present.gameplay.world.b07_spawn**: world region (HUD band and player excluded): textured detail 0.185 (PASS >= 0.15, FAIL < 0.10), black 0.301, largest blank 0.144, untextured 0.001 / 0.011, noise 0.001
- PASS **present.gameplay.world.b08_moving**: world region (HUD band and player excluded): textured detail 0.171 (PASS >= 0.15, FAIL < 0.10), black 0.077, largest blank 0.083, untextured 0.078 / 0.246, noise 0
- PARTIAL **present.gameplay.world.b09_moving2**: world region (HUD band and player excluded): textured detail 0.145 (PASS >= 0.15, FAIL < 0.10), black 0.119, largest blank 0.16, untextured 0.067 / 0.241, noise 0.001
- PASS **present.transition.second_match.07_spawn**: world detail match 1 0.184 vs match 2 0.185 (ratio 1.01; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- PASS **present.transition.second_match.08_moving**: world detail match 1 0.173 vs match 2 0.171 (ratio 0.99; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- PASS **present.transition.pause_cleared.a**: 3.5 s after Resume: structural similarity to the PAUSE frame 0.131, to the gameplay frame before pause 0.737 (FAIL: the frame still looks like the pause screen)
- PASS **present.transition.pause_cleared.b**: 3.5 s after Resume: structural similarity to the PAUSE frame 0.131, to the gameplay frame before pause 0.736 (FAIL: the frame still looks like the pause screen)
- PASS **present.exclusive.menu_visible_while_moving**: pause menu still drawn after Resume: False; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement
- UNKNOWN **present.exclusive.no_movement_under_menu**: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 34.5 m; Paused 6.2 m; InGame 22.9 m; Paused 13.7 m; WaitingOnGameStart 0.0 m; InGame 34.5 m; Paused 6.2 m; InGame 23.1 m; Paused 13.7 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- PASS **present.gameplay.route_vs_direct**: same start (TnTeamPlayerStart_12481) - direct boot world detail 0.17, frontend-launched 0.184 (ratio 1.08; FAIL < 0.5: the frontend-launched match loses world content that direct boot draws -> route-specific seam, not the map data)
- PASS **present.watchdog.customization.softlock**: customization: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.904, still-inside similarity 0.226); focus present in the screen's dumps True
- PASS **present.watchdog.customization.classification**: customization: FULLY FUNCTIONAL. preview body loaded: True (TR_Sideswipe_ROBO_p, TR_Barricade_ROBO_p); customize.preview owner: renderer; 'LOADING' still shown: False; the posed body on screen: HUMAN (sheet)
- PARTIAL **present.watchdog.customization.emblem_state**: STATE ONLY (trace): 12 emblem transitions; Opacity on 4 (8803,16331,5865,1120); Highlighted on 0 () - not exercised: this path never focuses a chassis button; Opacity still on after leaving Create a Character: none. Visible glow: drawable: renderer has setFrontendMaterialParam and F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin\..\..\work\render\UI_CharacterCustomization\material_instance_actors.json exists - HUMAN check (item 2): overview = red Autobot + purple Decepticon logos behind the robots; chassis menu = the selected faction's logo glowing, the other hidden; party lobby / class list = none
