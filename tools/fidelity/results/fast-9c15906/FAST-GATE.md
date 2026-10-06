# FAST development gate - 9c159066cac1fe332b922406f1f32a6030e7177d

| | |
|---|---|
| tier | FAST (not a certification; TARGETED / FULL on request - VALIDATION-TIERS.md) |
| integration commit | `9c159066cac1fe332b922406f1f32a6030e7177d` |
| previous validated | `175a6348` |
| changed since | gameplay 18, other 15, gameplay systems/audio 14, frontend 10, rendering 7, assets / render data 6, systems/audio 2 |
| exe | Debug `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build\bin\wfc_rebuild.exe` / Release `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin\wfc_rebuild.exe` (graphical runs: Release) |
| wall time | 0 min |

## VERDICT: **BROKEN - visible regression (2)**

**PASS 54 / KNOWN 2 / INFO 6 / PARTIAL 3 / UNKNOWN 1 / SKIP 1 / HUMAN 1 / PRODUCT 2**

## PRODUCT FAIL: 2

- **m07state.transition.t9_display1080** [Rendering/Integration]: world FAIL (detail 0.085); renderer ok (world 541, bsp 163); GL state left by the overlay sane
- **m07state.transition.t9b_display720** [Rendering/Integration]: world FAIL (detail 0.061); renderer ok (world 255, bsp 81); GL state left by the overlay sane

## HARNESS FAIL: 0


## KNOWN: 2

- **fast.fx.partial_decode** [Rendering]: particle modules with an undecoded field (declared PARTIAL, a default is used): PMI_LocationPrimitiveSphere has no decoded StartRadius (0 used) / PMI_LocationPrimitiveSphere has no decoded VelocityScale (0 used) / PMI_VelocityOverLife has no decoded VelOverLife (0 used)
- **fast.fx.ribbon_beam** [Rendering]: Trail2 / Beam2 ribbons and beams (tracer smoke trails, repair / drain beams) not drawn - declared PARTIAL by the product (WeaponFx.cpp:391; STATUS.md); map-FX occurrences logged in this run: 0. KNOWN Rendering / Systems item, not a weapon failure (fire / reload / weapon test pass)

## UNKNOWN / PARTIAL: 4

- **present.gameplay.world.a09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.138 (PASS >= 0.15, FAIL < 0.10), black 0.577, largest blank 0.542, untextured 0.003 / 0.012, noise 0
- **present.gameplay.world.b09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.134 (PASS >= 0.15, FAIL < 0.10), black 0.552, largest blank 0.544, untextured 0.003 / 0.012, noise 0
- **present.exclusive.no_movement_under_menu** [Experimental/Frontend]: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 74.9 m; Paused 0.0 m; InGame 3.3 m; Paused 16.0 m; WaitingOnGameStart 0.0 m; InGame 75.7 m; Paused 0.0 m; InGame 3.7 m; Paused 15.8 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- **present.watchdog.customization.emblem_state** [Frontend]: STATE ONLY (trace): 12 emblem transitions; Opacity on 4 (8803,16331,5865,1120); Highlighted on 0 () - not exercised: this path never focuses a chassis button; Opacity still on after leaving Create a Character: none. Visible glow: drawable: renderer has setFrontendMaterialParam and F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin\..\..\work\render\UI_CharacterCustomization\material_instance_actors.json exists - HUMAN check (item 2): overview = red Autobot + purple Decepticon logos behind the robots; chassis menu = the selected faction's logo glowing, the other hidden; party lobby / class list = none

## HUMAN CHECK: 1

- **m07state.transition.t8_second_match_later** [Rendering/Integration]: world FAIL (detail 0.187); renderer ok (world 736, bsp 338); GL state left by the overlay sane

## PASS: 54

- **fast.build.debug** [Integration]: Debug exe from 9c15906
- **fast.build.release** [Integration]: Release exe from 9c15906
- **fast.unit.build** [Integration]: ctest (build): 100% tests passed out of 2
- **fast.unit.build-release** [Integration]: ctest (build-release): 100% tests passed out of 2
- **fast.selftest.weapontest** [Gameplay]: WFC_WEAPONTEST: 19/19 checks passed
- **fast.selftest.chassistest** [Gameplay]: WFC_CHASSISTEST: 14/14 checks passed
- **fast.mechanics.move_jump** [Gameplay]: 150 frame samples, net displacement 41 m, airborne samples 74
- **fast.mechanics.fire** [Gameplay]: clip decreased on 57 samples (WFC_AUTOFIRE)
- **fast.mechanics.reload** [Gameplay]: clip refilled from reserve 3x; reloading flag seen True
- **fast.mechanics.transform** [Gameplay]: forms seen ROBOT,VEHICLE; form switches 4 (transform every 300 frames)
- **fast.fx.weapon_assets** [Systems/Rendering]: weapon FX load: 9/9 original FX textures loaded; 2/2 mesh-particle meshes loaded
- **fast.fx.other_errors** [Rendering]: other FX / particle warnings or errors: 0
- **fast.audio.route** [Systems]: map audio loads 2 (MP_IAC_Streets -> MP_IAC_Streets), unloads 2; voices / instances still alive after an unload: 0; PCM MB per load 149.804 / 149.804
- **fast.hud.goalscore.route** [Frontend]: <CurrentGame:GoalScore> unanswered 0x (Hud_GFX then scales the team bars to 10 points); other unanswered CurrentGame reads: none
- **fast.audio.mapswitch** [Systems]: map audio loads 3 (MP_IAC_Streets -> MP_IAC_Berth -> MP_IAC_Streets), unloads 3; voices / instances still alive after an unload: 0; PCM MB per load 149.804 / 160.385 / 149.804
- **fast.hud.goalscore.mapswitch** [Frontend]: <CurrentGame:GoalScore> unanswered 0x (Hud_GFX then scales the team bars to 10 points); other unanswered CurrentGame reads: none
- **fast.render.out_of_bounds_draws** [Rendering]: rejected out-of-bounds sub-mesh draws (GPU fault / driver-reset class, Rendering M45): 0
- **fast.render.legacy_renderer** [Rendering]: LEGACY RENDERER (no original render data) errors: 0
- **present.route.completes** [Integration]: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- **present.frontend.scene.a00_title** [Frontend/Rendering]: title 3D scene region: detail 0.815, black 0.052, largest blank area 0.024, untextured / placeholder surface largest 0.035 total 0.071, noise texture 0.076 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a01_title_9s** [Frontend/Rendering]: title 3D scene region: detail 0.802, black 0.075, largest blank area 0.022, untextured / placeholder surface largest 0.044 total 0.046, noise texture 0.059 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a13_frontend_after** [Frontend/Rendering]: title 3D scene region: detail 0.814, black 0.051, largest blank area 0.029, untextured / placeholder surface largest 0.036 total 0.072, noise texture 0.074 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.b13_frontend_after** [Frontend/Rendering]: title 3D scene region: detail 0.813, black 0.052, largest blank area 0.024, untextured / placeholder surface largest 0.036 total 0.071, noise texture 0.076 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.screen.a02_party** [Frontend/Rendering]: screen: detail 0.111, black 0, largest blank 0.271, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.579 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.frontend.screen.a03_lobby** [Frontend/Rendering]: screen: detail 0.18, black 0.008, largest blank 0.198, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.121, largest blank 0.488 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.frontend.screen.a04_loading** [Frontend/Rendering]: screen: detail 0.081, black 0.347, largest blank 0.177, untextured 0.001 / 0.001, noise 0
- **present.frontend.screen.b02_party** [Frontend/Rendering]: screen: detail 0.113, black 0.001, largest blank 0.283, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.794 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.frontend.screen.b03_lobby** [Frontend/Rendering]: screen: detail 0.18, black 0.009, largest blank 0.181, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.121, largest blank 0.531 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.charselect.ui.a** [Frontend]: Choose Character screen: detail 0.345; dump texts with class names: 2
- **present.charselect.ui.b** [Frontend]: Choose Character screen: detail 0.344; dump texts with class names: 2
- **present.charselect.gameplay_receives** [Gameplay]: selected Scientist (Jet); Gameplay spawn chassis Jet,Jet
- **present.charselect.body_resolves** [AssetTools/Gameplay]: selected chassis Jet; robot body loaded: Truck,Jet (Optimus for a non-Optimus selection = the selection is not drawn)
- **present.gameplay.world.a07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.296 (PASS >= 0.15, FAIL < 0.10), black 0.087, largest blank 0.3, untextured 0.002 / 0.012, noise 0.003
- **present.gameplay.world.a08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.197 (PASS >= 0.15, FAIL < 0.10), black 0.044, largest blank 0.182, untextured 0.004 / 0.007, noise 0.001
- **present.gameplay.world.b07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.291 (PASS >= 0.15, FAIL < 0.10), black 0.087, largest blank 0.273, untextured 0.009 / 0.021, noise 0.002
- **present.gameplay.world.b08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.188 (PASS >= 0.15, FAIL < 0.10), black 0.047, largest blank 0.093, untextured 0.009 / 0.01, noise 0.001
- **present.transition.second_match.07_spawn** [Integration/Rendering]: world detail match 1 0.296 vs match 2 0.291 (ratio 0.98; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.second_match.08_moving** [Integration/Rendering]: world detail match 1 0.197 vs match 2 0.188 (ratio 0.95; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.pause_cleared.a** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.13, to the gameplay frame before pause 0.692 (FAIL: the frame still looks like the pause screen)
- **present.transition.pause_cleared.b** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.139, to the gameplay frame before pause 0.685 (FAIL: the frame still looks like the pause screen)
- **present.exclusive.menu_visible_while_moving** [Frontend]: pause menu still drawn after Resume: False; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement
- **present.gameplay.route_vs_direct** [Integration]: same start (TnTeamPlayerStart_8835) - direct boot world detail 0.437, frontend-launched 0.296 (ratio 0.68; FAIL < 0.5: the frontend-launched match loses world content that direct boot draws -> route-specific seam, not the map data)
- **present.watchdog.customization.softlock** [Frontend/AssetTools]: customization: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.908, still-inside similarity 0.211); focus present in the screen's dumps True
- **present.watchdog.customization.classification** [Frontend/AssetTools]: customization: FULLY FUNCTIONAL. preview body loaded: True (TR_Sideswipe_ROBO_p, TR_Barricade_ROBO_p); customize.preview owner: renderer; 'LOADING' still shown: False; the posed body on screen: HUMAN (sheet)
- **maploss.normal.process** [Rendering/Integration]: process outcome: clean exit; display-driver / crash events in the run window: none
- **maploss.normal.intro_edges.i1_intro_2s** [Frontend/Rendering]: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.177 (FAIL: content beside the movie - the frontend shows through)
- **maploss.normal.intro_edges.i2_intro_6s** [Frontend/Rendering]: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.328 (FAIL: content beside the movie - the frontend shows through)
- **maploss.normal.s1.MP_IAC_Streets.environment** [Rendering/Frontend/Integration]: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.213/0.248/0.24, black 0.176/0.147/0.09; product VISUALCHECK median world draws 42, BSP draws 23, materials 35. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- **maploss.normal.b.MP_IAC_Berth.environment** [Rendering/Frontend/Integration]: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.21/0.22/0.223, black 0.353/0.442/0.55; product VISUALCHECK median world draws 179, BSP draws 21, materials 53. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- **maploss.normal.s2.MP_IAC_Streets.environment** [Rendering/Frontend/Integration]: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.32/0.313/0.23, black 0.124/0.05/0.152; product VISUALCHECK median world draws 452, BSP draws 142, materials 95. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- **m07state.transition.t1_after_charselect** []: world PASS (detail 0.323); renderer ok (world 1767, bsp 450); GL state left by the overlay sane
- **m07state.transition.t3_after_resume** []: world PASS (detail 0.247); renderer ok (world 1058, bsp 313); GL state left by the overlay sane
- **m07state.transition.t5_after_respawn** []: world PASS (detail 0.186); renderer ok (world 852, bsp 251); GL state left by the overlay sane
- **m07state.transition.t7_second_match** []: world PASS (detail 0.344); renderer ok (world 2168, bsp 766); GL state left by the overlay sane

## Commits since the previous validated commit

- 91672f1 Pass 24g: prewarm each chassis' robot / vehicle / arm materials when its assets are cached (Rendering M53)
- 4f080a2 Integration milestone 08g: Gameplay Pass 24 + Systems Gameplay-24 addendum validated (STATUS)
- 0f870ca Pass 24: handoff (docs/handoffs/GAMEPLAY_PASS24_PLAYTEST.md) and STATUS rows for 24d-24f
- ae2df5f Integration milestone 08f: accounts prompt fix, beam fill, match announcer / countdown audio, volume sliders (STATUS)
- c64c3ef Integration M08f: resolve FIDELITY.md conflict markers left by the rendering merge (both sides kept)
- 1168af2 Integration milestone 08e: Create a Character fix, menu hitches, per-chassis vehicle FX (STATUS)
- 7d78ac7 M08g addendum: correct RE commit reference (d832643)
- 6441a41 M08g addendum: mixer presets are a separate stage from the group-volume fader (RE b2d6bd8)
- 593311c Systems M08g: profile volume sliders (SetAudioGroupVolume Music / FX / Dialogue)
- 2692e46 Docs: beam noise / sine wave (native fill), frontend emitter prewarm (M56-M58)
- dfc20f8 Pass 24f: vehicle instant-hit trace starts at TnPlayerPawn.GetWeaponStartTraceLocation (crosshair ray point nearest the pawn)
- 5c5eb46 FIDELITY / STATUS: menu-loop Accounts fix (136ac7a) and the hitch re-profile on the owners' fixes
- cff0489 Rendering M58: frontend scenes prewarm their placed particle components during the load
- 6eac92d Rendering M57: Beam2 fill per RE's native decode (0x830298E8) - sine wave CONFIRMED, noise frame / smoothing / tangents
- 8253a62 Pass 24e: vehicle weapons alternate WeaponSocket_Primary / Primary2 per shot (Scout no longer fires only from the left)
- 136ac7a Selection.getFocus forgets a removed field: cancelled Accounts prompt no longer creates the account later
- 4134d5f Rendering M56: Beam2 noise (native rules), BeamSineWave (H, toggle), joined ribbon strips
- 8df544b Systems M08f: countdown ticks, objective announcer wiring, grenade fuse / bounce, async prefetch
- 9d4e706 Pass 24d: projectiles use their authored FlightEffect / ExplosionEffect instead of box markers
- cbbef53 FIDELITY / STATUS: FRONTEND PASS 7 (playtest presentation); soak harness weapon slots and waits
- 7b74b18 Docs: focused visual-fidelity pass M51-M55 (FIDELITY, STATUS)
- 822535f Integration milestone 08d: vehicle / weapon / projectile / beam audio by identity (STATUS)
- 9068069 QA panel: the spawned dummy is drawn (addMatchOpponent's second argument is 'drawn')
- c5bbe63 M08d addendum: Repair Ray hook point in Gameplay 24c (d0452a5); start/end are beam endpoints
- 25cc348 QA panel: Gameplay QA API (Pass 24c, detected) - weapon override, respawn, next start, noclip, god mode, dummy
- df0d4bb M08d glue addendum: Gameplay Pass 24b/24c signals (tank quickTurnSerial, explicit Repair Ray beam)
- d0452a5 Pass 24c: Energon Repair Ray beam, DEV / QA tooling API (WFC_QA), Pass 24 docs
- 87ec2eb M08e handoff: Rendering confirmed the vehicle FX API calls; build_map_fx regeneration note
- f875640 Rendering M55: Beam2 taper and Trail2 tessellation per RE's native trace (pass 5 s9)
- 4289b74 Systems M08e: per-chassis vehicle FX through Rendering's particle runtime
- ba68889 Rendering M54: load-time hitches - prewarm inside the load (yielding), none for menus; program cache across loads
- b689543 Rendering M53: IRenderer::prewarmDynamicMesh - resolve a transient mesh's programs / textures without drawing
- f3bf82a DEBUG-ONLY QA panel (WFC_QA=1, F10; NOT ORIGINAL) for fast fidelity-test scenarios
- 234576b Systems M08d: vehicle / weapon / projectile / beam audio by identity; integration glue patch
- aa1fb2a Rendering M52: particle template library includes the class-data FX templates (weapon / character data)
- e607961 Optional frame limiter (PC EXTENSION): [PCSettings] FrameLimit / WFC_FPS_LIMIT, off by default
- 4f1462b Frontend script call: steps go through the movie bridge (PCSettings.* etc.), not only GameFlow::call
- 6ee6d98 Rendering M51: Molten puddle box (ScreenAlign + UE3 screen-UV convention), Debris sky far-plane clip, WFC_FRAMELOG
- 5090ba0 Menu hitches: frame-gap profiler (WFC_FRAMEPROF) and the new scene's first draw under the loading screen
- d42367f Pass 24b: tank 180 quick turn (TnQuickTurnCameraBehavior) and per-weapon fine-aim camera (Null Ray FOV 20)
- c5c992c Pass 24a: presentation yaw between steps (fast-turn stutter), per-chassis transform ToggleHidden times
- 3185545 Create a Character shrinking / shifting after a weapon slot: removed timeline stays the function target
- fdffa7f Integration milestone 08c: human-playtest rendering, AMD stability guards, Gameplay Pass 23 (STATUS)
- ba1f31c Escalation wave keys only for the SV GRI: no 'NEW WAVE IN 0' in versus
- e554519 Integration M08c: Escalation wave values answer empty in versus (TDM HUD showed "NEW WAVE IN 0")
- 1677eb2 Integration M08c: one mouse-wheel accumulator (the merge left two members and two reads; the first read zeroed the wheel)
- aa0dfd1 Pass 23d: vehicle handling per RE pass 4 (UpdateTurn, boost jump) + skinPose fix + WFC_VEHPHYS + handoff
- d0eecf3 build_map_fx: guard the Size-module reference scan (single hit inside an object array; rejects logged)
- 51ec6b8 Rendering M47: Size-parameter SizeMultiplyLife bound per LOD by module reference (RE raw scan)
- 3f44104 Rendering M46: HoverFX 'Size' feeds ParticleModuleSizeMultiplyLife by parameter (RE pass 4), not effect-wide
- 84ba009 Docs: human-playtest rendering pass M41-M45 (FIDELITY, STATUS, handoff)
- cb51fc1 Integration M08b: playtest fixes recorded (vignette, HUD weapon class, lobby team, Systems audio) (STATUS)
- 79388f5 Rendering M45: skinPose always takes sub-mesh ranges / UVs from the posed model (out-of-bounds GPU fetch)
- 9081f22 M08c docs: every localized wave now has its int twin (AssetTools 768ea21)
- e15862f Rendering M43 / M44: AMD stability diagnostics + guards; Trail2 / Beam2 ribbons and PSC parameters
- 9cee8f0 Integration M08b: the lobby's team reaches gameplay (Experimental audit P1-1)
- 4a40ddc Systems M08c: per-form vehicle audio (speed / tread / booster loops, ascend / descend / roll / 180 / enter / exit); playtest handoff
- 466b9bd Pass 23c: Scout height measured (WFC_HEIGHTTEST): authentic shared-AnimSet posing, no root / scale / capsule change
- 386295d Answer the HUD / pause menu GRI objective values (Gameplay bec41cd, detected) with their sourced defaults
- bec41cd Pass 23b: fresh match state verified (WFC_SCORETEST); HUD weapon class; GRI values; middle-mouse melee; fire latch
- 2a7bdda Systems: localized announcer / dialogue waves from the GLanguage _LOC twin (wrong-language match start)
- e1db12b <CurrentGame:GoalScore> answered from the match's PointsToWin before Gameplay's values arrive
- b0c2a76 Rendering M42: compile every exported MP weapon's material (untextured Sniper, grey Scientist Burst Rifle)
- 6875f46 FIDELITY: GFx scale mode / viewport provenance from RE (native movie start path)
- 3e7ce28 HUD weapon: class name to NotifyCurrentWeaponChanged, fine aim forwarded; the movie picks the crosshair
- 7e078a5 Rendering M41: Dynamic-channel primitives lit by the Dynamic-channel lights (title ships / debris no longer black)
- 68cece1 Systems: Extras movie -> menu audio loss (two causes)
- 2089910 Pass 23a: weapon switching per the original (wheel = NextWeapon, TryPutDown states) + WFC_SWITCHTEST
- a661851 Title vignette / menu backgrounds cover the full screen: GFx Stage size and onResize for showAll movies

Evidence: `route/` (PRESENTATION.md, sheets), `mapswitch/` (MAP-LOSS.md), `renderstate/`, `mechanics/`, `selftest_*/`.

## Experimental review (2026-10-06)
- **Product FAIL t9 / t9b: display-mode change seam (Rendering).**
  - After `display:1920,1080,0` (and back to 1280×720) the frame has a hard horizontal seam about one third down.
  - Above it the scene is brighter; below it, darker (band luma about 21-32 vs about 14). The lower **two thirds**
    (= 720 / 1080) get Seed's colour grade (CLUT), the top third does not.
  - Likely cause: a post-process / CLUT target or viewport not resized on the display-mode change.
  - Evidence: `display_change_seam_m08b_vs_08g.jpg` (left M08b on Streets, right 08g on Seed).
  - **Not proven a regression:** M08b's frames were on Streets, whose CLUT is nearly neutral, so the same bug would be
    invisible there.
- **t8 (Seed, later in the second match): HUMAN.** Authored-dark Seed (per-map CLUT since Rendering M25; see
  `results/mapsweep-fdffa7f`).
- **Runtime proof of audit fixes:**
  - `<CurrentGame:GoalScore>` answered (0 unanswered), so P1-2 is fixed.
  - No unanswered CurrentGame reads at all, so P2-3 is closed.
- **Harness defects fixed during this run:**
  - build-target is called in its own process;
  - map-loss optional runs skip instead of throwing when the GPU is busy;
  - the FAST gate now generates render data for **every** launchable map: after a match the 08g lobby moves to the
    next map (Seed), and the missing render data produced a false LEGACY RENDERER and world loss;
  - a corrupted `src\render` path in the out-of-bounds check;
  - FX "has no decoded …" counts as KNOWN; `streets_return_counts` is INFO;
  - render state has a dark-but-intact rule (M08b control unchanged).
