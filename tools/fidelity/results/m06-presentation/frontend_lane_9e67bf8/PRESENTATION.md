# Presentation gate - agents/frontend 9e67bf8 Release 9e67bf8c487b8ca243710695151a6a708266ccd1

Verdict: **VISUALLY BROKEN**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\lane_fe\build-release\bin\wfc_rebuild.exe |
| render data | product default (<exe>\..\..\work\render) |
| result | pass 3 / fail 21 / known 0 / info 0 / skip 1 / human 0 / unknown 1 / waiting 0 / partial 0 |

## FAIL

- **present.frontend.scene.a00_title** [Frontend/Rendering]: title 3D scene region: detail 0.189, black 0.112, largest blank area 0.187, untextured / placeholder surface largest 0.234 total 0.431, noise texture 0.002 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a01_title_9s** [Frontend/Rendering]: title 3D scene region: detail 0.182, black 0.129, largest blank area 0.203, untextured / placeholder surface largest 0.244 total 0.429, noise texture 0.003 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a13_frontend_after** [Frontend/Rendering]: title 3D scene region: detail 0.591, black 0.467, largest blank area 0.294, untextured / placeholder surface largest 0.009 total 0.012, noise texture 0.427 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.b13_frontend_after** [Frontend/Rendering]: title 3D scene region: detail 0.591, black 0.467, largest blank area 0.294, untextured / placeholder surface largest 0.009 total 0.012, noise texture 0.427 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.screen.a02_party** [Frontend/Rendering]: screen: detail 0.113, black 0.031, largest blank 0.323, untextured 0.317 / 0.519, noise 0; scene area right of the panel: largest blank 0.861, flat grey 34%
- **present.frontend.screen.a03_lobby** [Frontend/Rendering]: screen: detail 0.164, black 0.039, largest blank 0.305, untextured 0.301 / 0.547, noise 0; scene area right of the panel: largest blank 0.818, flat grey 26%
- **present.frontend.screen.a04_loading** [Frontend/Rendering]: screen: detail 0.039, black 0.942, largest blank 0.478, untextured 0.001 / 0.004, noise 0
- **present.frontend.screen.b02_party** [Frontend/Rendering]: screen: detail 0.11, black 0.032, largest blank 0.323, untextured 0.317 / 0.609, noise 0.001; scene area right of the panel: largest blank 0.861, flat grey 34%
- **present.frontend.screen.b03_lobby** [Frontend/Rendering]: screen: detail 0.146, black 0.086, largest blank 0.305, untextured 0.301 / 0.547, noise 0; scene area right of the panel: largest blank 0.818, flat grey 26%
- **present.charselect.gameplay_receives** [Frontend]: no match.characterSelected: the selection never reached the match (soft lock or skipped screen)
- **present.gameplay.world.a07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.07 (PASS >= 0.15, FAIL < 0.10), black 0.041, largest blank 0.382, untextured 0.03 / 0.069, noise 0
- **present.gameplay.world.a08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.024 (PASS >= 0.15, FAIL < 0.10), black 0.278, largest blank 0.333, untextured 0.008 / 0.023, noise 0
- **present.gameplay.world.a09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.023 (PASS >= 0.15, FAIL < 0.10), black 0.653, largest blank 0.615, untextured 0.001 / 0.002, noise 0
- **present.gameplay.world.b07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.063 (PASS >= 0.15, FAIL < 0.10), black 0.046, largest blank 0.281, untextured 0.03 / 0.068, noise 0
- **present.gameplay.world.b08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.019 (PASS >= 0.15, FAIL < 0.10), black 0.283, largest blank 0.329, untextured 0.008 / 0.024, noise 0
- **present.gameplay.world.b09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.02 (PASS >= 0.15, FAIL < 0.10), black 0.654, largest blank 0.623, untextured 0.001 / 0.002, noise 0
- **present.transition.second_match.07_spawn** [Integration/Rendering]: world detail match 1 0.07 vs match 2 0.063 (ratio 0.9; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.second_match.08_moving** [Integration/Rendering]: world detail match 1 0.024 vs match 2 0.019 (ratio 0.79; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.pause_cleared.a** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.97, to the gameplay frame before pause 0.257 (FAIL: the frame still looks like the pause screen)
- **present.transition.pause_cleared.b** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.999, to the gameplay frame before pause 0.011 (FAIL: the frame still looks like the pause screen)
- **present.exclusive.menu_visible_while_moving** [Frontend]: pause menu still drawn after Resume: True; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement

## Other

- PASS **present.route.completes**: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- PASS **present.charselect.ui.a**: Choose Character screen: detail 0.256; dump texts with class names: 2
- PASS **present.charselect.ui.b**: Choose Character screen: detail 0.23; dump texts with class names: 2
- UNKNOWN **present.exclusive.no_movement_under_menu**: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 18.6 m; Paused 7.0 m; InGame 18.2 m; Paused 5.8 m; WaitingOnGameStart 0.0 m; InGame 19.0 m; Paused 6.9 m; InGame 18.4 m; Paused 5.6 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- SKIP **present.gameplay.route_vs_direct**: no direct-boot frame (WFC_START / WFC_SHOTEVERY hooks) or no route spawn frame
