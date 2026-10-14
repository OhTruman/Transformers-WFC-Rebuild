# Presentation gate - a96f841 route Debug a96f841bd6eaaa4ad8fdf255fabcd230b436ad7b

Verdict: **VISUAL DEFECTS**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fe_fix\build\bin\wfc_rebuild.exe |
| render data | product default (<exe>\..\..\work\render) |
| result | pass 19 / fail 4 / known 0 / info 0 / skip 0 / human 0 / unknown 1 / waiting 0 / partial 2 |

## FAIL

- **present.frontend.screen.a02_party** [Frontend/Rendering]: screen: detail 0.112, black 0, largest blank 0.463, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.762, flat grey 0%
- **present.frontend.screen.b02_party** [Frontend/Rendering]: screen: detail 0.112, black 0, largest blank 0.374, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.844, flat grey 0%
- **present.frontend.screen.b03_lobby** [Frontend/Rendering]: screen: detail 0.179, black 0.008, largest blank 0.308, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.608, flat grey 0%
- **present.charselect.body_resolves** [AssetTools/Gameplay]: selected chassis Jet4; robot body loaded: Optimus (Optimus for a non-Optimus selection = the selection is not drawn)

## Other

- PASS **present.route.completes**: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- PASS **present.frontend.scene.a00_title**: title 3D scene region: detail 0.827, black 0.012, largest blank area 0.019, untextured / placeholder surface largest 0.028 total 0.048, noise texture 0.072 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.a01_title_9s**: title 3D scene region: detail 0.733, black 0.125, largest blank area 0.085, untextured / placeholder surface largest 0.025 total 0.046, noise texture 0.053 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.a13_frontend_after**: title 3D scene region: detail 0.829, black 0.012, largest blank area 0.014, untextured / placeholder surface largest 0.028 total 0.048, noise texture 0.073 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.scene.b13_frontend_after**: title 3D scene region: detail 0.828, black 0.012, largest blank area 0.015, untextured / placeholder surface largest 0.027 total 0.048, noise texture 0.07 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- PASS **present.frontend.screen.a03_lobby**: screen: detail 0.18, black 0.008, largest blank 0.316, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.465, flat grey 0%
- PASS **present.frontend.screen.a04_loading**: screen: detail 0.077, black 0.355, largest blank 0.172, untextured 0.001 / 0.001, noise 0
- PASS **present.charselect.ui.a**: Choose Character screen: detail 0.259; dump texts with class names: 2
- PASS **present.charselect.ui.b**: Choose Character screen: detail 0.259; dump texts with class names: 2
- PASS **present.charselect.gameplay_receives**: selected Scientist (Jet4); Gameplay spawn chassis Jet4,Jet4
- PASS **present.gameplay.world.a07_spawn**: world region (HUD band and player excluded): textured detail 0.152 (PASS >= 0.15, FAIL < 0.10), black 0.258, largest blank 0.193, untextured 0.001 / 0.015, noise 0.002
- PASS **present.gameplay.world.a08_moving**: world region (HUD band and player excluded): textured detail 0.199 (PASS >= 0.15, FAIL < 0.10), black 0.073, largest blank 0.084, untextured 0.04 / 0.206, noise 0.001
- PARTIAL **present.gameplay.world.a09_moving2**: world region (HUD band and player excluded): textured detail 0.146 (PASS >= 0.15, FAIL < 0.10), black 0.075, largest blank 0.234, untextured 0.113 / 0.385, noise 0
- PASS **present.gameplay.world.b07_spawn**: world region (HUD band and player excluded): textured detail 0.153 (PASS >= 0.15, FAIL < 0.10), black 0.257, largest blank 0.2, untextured 0.001 / 0.014, noise 0.001
- PASS **present.gameplay.world.b08_moving**: world region (HUD band and player excluded): textured detail 0.206 (PASS >= 0.15, FAIL < 0.10), black 0.074, largest blank 0.082, untextured 0.04 / 0.21, noise 0.001
- PARTIAL **present.gameplay.world.b09_moving2**: world region (HUD band and player excluded): textured detail 0.143 (PASS >= 0.15, FAIL < 0.10), black 0.074, largest blank 0.236, untextured 0.113 / 0.387, noise 0
- PASS **present.transition.second_match.07_spawn**: world detail match 1 0.152 vs match 2 0.153 (ratio 1.01; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- PASS **present.transition.second_match.08_moving**: world detail match 1 0.199 vs match 2 0.206 (ratio 1.04; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- PASS **present.transition.pause_cleared.a**: 3.5 s after Resume: structural similarity to the PAUSE frame -0.037, to the gameplay frame before pause 0.774 (FAIL: the frame still looks like the pause screen)
- PASS **present.transition.pause_cleared.b**: 3.5 s after Resume: structural similarity to the PAUSE frame -0.026, to the gameplay frame before pause 0.773 (FAIL: the frame still looks like the pause screen)
- PASS **present.exclusive.menu_visible_while_moving**: pause menu still drawn after Resume: False; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement
- UNKNOWN **present.exclusive.no_movement_under_menu**: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 30.0 m; Paused 6.9 m; InGame 16.6 m; Paused 13.9 m; WaitingOnGameStart 0.0 m; InGame 30.1 m; Paused 6.4 m; InGame 16.9 m; Paused 12.0 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
