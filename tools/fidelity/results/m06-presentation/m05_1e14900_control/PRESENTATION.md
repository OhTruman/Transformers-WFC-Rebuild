# Presentation gate - integration/milestone-05 (control) Release 1e14900b150349f8da2affb024106f3d7b83cc50

Verdict: **VISUALLY BROKEN**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m5int\build-release\bin\wfc_rebuild.exe |
| render data | product default (<exe>\..\..\work\render) |
| result | pass 7 / fail 5 / known 0 / info 0 / skip 2 / human 0 / unknown 1 / waiting 0 / partial 0 |

## FAIL

- **present.route.completes** [Integration]: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: stopped at 't=1' (exit n/a, timed out n/a) - a soft lock or a missing transition
- **present.frontend.scene.a00_title** [Frontend/Rendering]: title 3D scene region: detail 0.034, black 0.971, largest blank area 0.959, untextured / placeholder surface largest 0 total 0, noise texture 0 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a01_title_9s** [Frontend/Rendering]: title 3D scene region: detail 0.034, black 0.971, largest blank area 0.959, untextured / placeholder surface largest 0 total 0, noise texture 0 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.screen.a02_party** [Frontend/Rendering]: screen: detail 0.141, black 0.723, largest blank 0.513, untextured 0 / 0, noise 0.006; scene area right of the panel: largest blank 0.87, flat grey 0%
- **present.frontend.screen.a03_lobby** [Frontend/Rendering]: screen: detail 0.189, black 0.664, largest blank 0.442, untextured 0 / 0, noise 0; scene area right of the panel: largest blank 0.624, flat grey 0%

## Other

- PASS **present.frontend.screen.a04_loading**: screen: detail 0.073, black 0.364, largest blank 0.178, untextured 0.002 / 0.004, noise 0
- PASS **present.charselect.ui.a**: Choose Character screen: detail 0.147; dump texts with class names: no dump (movie not reachable by dump:)
- SKIP **present.charselect.gameplay_receives**: this build has no WFC_CHARSELECT hook: the match auto-selects; selection cannot be exercised
- PASS **present.gameplay.world.a07_spawn**: world region (HUD band and player excluded): textured detail 0.198 (PASS >= 0.15, FAIL < 0.10), black 0.2, largest blank 0.185, untextured 0.019 / 0.095, noise 0.001
- PASS **present.gameplay.world.a08_moving**: world region (HUD band and player excluded): textured detail 0.224 (PASS >= 0.15, FAIL < 0.10), black 0.098, largest blank 0.082, untextured 0.032 / 0.159, noise 0.001
- PASS **present.gameplay.world.a09_moving2**: world region (HUD band and player excluded): textured detail 0.155 (PASS >= 0.15, FAIL < 0.10), black 0.083, largest blank 0.2, untextured 0.075 / 0.29, noise 0.001
- PASS **present.exclusive.menu_visible_while_moving**: pause menu still drawn after Resume: False; product UI state after Resume = InGame (real input routed to gameplay): False; pawn moved after Resume: False -> a menu stays visible while gameplay accepts movement
- UNKNOWN **present.exclusive.no_movement_under_menu**: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 18.8 m; Paused 3,912.9 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- SKIP **present.gameplay.route_vs_direct**: no direct-boot frame (WFC_START / WFC_SHOTEVERY hooks) or no route spawn frame
- PASS **present.reference.streets_pipeline**: 13 fixed Streets cameras vs the known-good reference (streets_spawn_m05_1e14900): median structural similarity 1; views lost (detail ratio < 0.5 or similarity < 0.4): 0 . FAIL flags the whole merged visual pipeline regardless of how many maps load
