# FAST development gate - 175a6348cb9490fb090f467500ff8bdeb85ec1de

| | |
|---|---|
| tier | FAST (not a certification; TARGETED / FULL on request - VALIDATION-TIERS.md) |
| integration commit | `175a6348cb9490fb090f467500ff8bdeb85ec1de` |
| previous validated | `ed91718` |
| changed since | gameplay 29, other 8, frontend 7, rendering 5, gameplay systems/audio 5, assets / render data 5 |
| exe | Debug `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build\bin\wfc_rebuild.exe` / Release `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin\wfc_rebuild.exe` (graphical runs: Release) |
| wall time | 0 min |

## VERDICT: **NO OBVIOUS BREAKAGE** (only the KNOWN Rendering ribbon / beam FX item remains)

**PASS 52 / KNOWN 1 / INFO 7 / PARTIAL 5 / UNKNOWN 1 / SKIP 1**

## PRODUCT FAIL: 0


## HARNESS FAIL: 0


## KNOWN: 1

- **fast.fx.ribbon_beam** [Rendering]: Trail2 / Beam2 ribbons and beams (tracer smoke trails, repair / drain beams) not drawn - declared PARTIAL by the product (WeaponFx.cpp:391, WfcMapFx.cpp:354; STATUS.md); map-FX occurrences logged in this run: 0. KNOWN Rendering / Systems item, not a weapon failure (fire / reload / weapon test pass)

## UNKNOWN / PARTIAL: 6

- **present.gameplay.world.a09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.146 (PASS >= 0.15, FAIL < 0.10), black 0.119, largest blank 0.158, untextured 0.067 / 0.242, noise 0
- **present.gameplay.world.b09_moving2** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.145 (PASS >= 0.15, FAIL < 0.10), black 0.119, largest blank 0.16, untextured 0.067 / 0.241, noise 0.001
- **present.exclusive.no_movement_under_menu** [Experimental/Frontend]: not measurable with the current hooks: scripted input (WFC_AUTO*) is added after the menu input gate. Motion per UI state with scripted Forward, for reference only: WaitingOnGameStart 0.0 m; InGame 34.5 m; Paused 6.2 m; InGame 22.9 m; Paused 13.7 m; WaitingOnGameStart 0.0 m; InGame 34.5 m; Paused 6.2 m; InGame 23.1 m; Paused 13.7 m. Proposal: a pre-gate input hook (e.g. WFC_INPUTSCRIPT) so tests press keys the way a player does
- **present.watchdog.customization.emblem_state** [Frontend]: STATE ONLY (trace): 12 emblem transitions; Opacity on 4 (8803,16331,5865,1120); Highlighted on 0 () - not exercised: this path never focuses a chassis button; Opacity still on after leaving Create a Character: none. Visible glow: drawable: renderer has setFrontendMaterialParam and F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin\..\..\work\render\UI_CharacterCustomization\material_instance_actors.json exists - HUMAN check (item 2): overview = red Autobot + purple Decepticon logos behind the robots; chassis menu = the selected faction's logo glowing, the other hidden; party lobby / class list = none
- **m07state.transition.t8_second_match_later** []: world PARTIAL (detail 0.136); renderer ok (world 998, bsp 376); GL state left by the overlay sane
- **m07state.transition.t9_display1080** []: world PARTIAL (detail 0.104); renderer ok (world 46, bsp 27); GL state left by the overlay sane

## HUMAN CHECK: 0


## PASS: 52

- **fast.build.debug** [Integration]: Debug exe from 175a634
- **fast.build.release** [Integration]: Release exe from 175a634
- **fast.unit.build** [Integration]: ctest (build): 100% tests passed out of 2
- **fast.unit.build-release** [Integration]: ctest (build-release): 100% tests passed out of 2
- **fast.selftest.weapontest** [Gameplay]: WFC_WEAPONTEST: 19/19 checks passed
- **fast.selftest.chassistest** [Gameplay]: WFC_CHASSISTEST: 13/13 checks passed
- **fast.mechanics.move_jump** [Gameplay]: 150 frame samples, net displacement 39 m, airborne samples 77
- **fast.mechanics.fire** [Gameplay]: clip decreased on 57 samples (WFC_AUTOFIRE)
- **fast.mechanics.reload** [Gameplay]: clip refilled from reserve 3x; reloading flag seen True
- **fast.mechanics.transform** [Gameplay]: forms seen ROBOT,VEHICLE; form switches 4 (transform every 300 frames)
- **fast.fx.weapon_assets** [Systems/Rendering]: weapon FX load: 9/9 original FX textures loaded; 2/2 mesh-particle meshes loaded
- **fast.fx.other_errors** [Rendering]: other FX / particle warnings or errors: 0
- **fast.audio.route** [Systems]: map audio loads 2 (MP_IAC_Streets -> MP_IAC_Streets), unloads 2; voices / instances still alive after an unload: 0; PCM MB per load 146.026 / 146.026
- **fast.audio.mapswitch** [Systems]: map audio loads 3 (MP_IAC_Streets -> MP_IAC_Berth -> MP_IAC_Streets), unloads 3; voices / instances still alive after an unload: 0; PCM MB per load 146.026 / 156.607 / 146.026
- **present.route.completes** [Integration]: frontend -> MP_IAC_Streets -> pause / resume -> frontend -> second match: completed
- **present.frontend.scene.a00_title** [Frontend/Rendering]: title 3D scene region: detail 0.814, black 0.046, largest blank area 0.015, untextured / placeholder surface largest 0.038 total 0.065, noise texture 0.074 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a01_title_9s** [Frontend/Rendering]: title 3D scene region: detail 0.722, black 0.178, largest blank area 0.091, untextured / placeholder surface largest 0.035 total 0.045, noise texture 0.058 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.a13_frontend_after** [Frontend/Rendering]: title 3D scene region: detail 0.812, black 0.046, largest blank area 0.02, untextured / placeholder surface largest 0.038 total 0.066, noise texture 0.073 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.scene.b13_frontend_after** [Frontend/Rendering]: title 3D scene region: detail 0.813, black 0.046, largest blank area 0.013, untextured / placeholder surface largest 0.037 total 0.064, noise texture 0.074 (FAIL: black >= 0.70, one blank area >= 0.45, detail < 0.10, untextured largest >= 0.12 or total >= 0.40, noise >= 0.15)
- **present.frontend.screen.a02_party** [Frontend/Rendering]: screen: detail 0.112, black 0, largest blank 0.337, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.593 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.frontend.screen.a03_lobby** [Frontend/Rendering]: screen: detail 0.18, black 0.008, largest blank 0.244, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.121, largest blank 0.422 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.frontend.screen.a04_loading** [Frontend/Rendering]: screen: detail 0.081, black 0.347, largest blank 0.177, untextured 0.001 / 0.001, noise 0
- **present.frontend.screen.b02_party** [Frontend/Rendering]: screen: detail 0.112, black 0, largest blank 0.389, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.066, largest blank 0.841 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.frontend.screen.b03_lobby** [Frontend/Rendering]: screen: detail 0.179, black 0.008, largest blank 0.291, untextured 0 / 0, noise 0; lobby backdrop (dome + 4 cards, sparse by design) right of the panel: detail 0.12, largest blank 0.582 (FAIL as clear colour: detail < 0.02 or blank >= 0.95), flat grey 0%; dome/cards level UI_CharacterCustomization_m drawn: True; emblem glow and no robots outside Create a Character: HUMAN
- **present.charselect.ui.a** [Frontend]: Choose Character screen: detail 0.26; dump texts with class names: 2
- **present.charselect.ui.b** [Frontend]: Choose Character screen: detail 0.259; dump texts with class names: 2
- **present.charselect.gameplay_receives** [Gameplay]: selected Scientist (Jet4); Gameplay spawn chassis Jet4,Jet4
- **present.charselect.body_resolves** [AssetTools/Gameplay]: selected chassis Jet4; robot body loaded: Truck,Jet4 (Optimus for a non-Optimus selection = the selection is not drawn)
- **present.gameplay.world.a07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.184 (PASS >= 0.15, FAIL < 0.10), black 0.301, largest blank 0.144, untextured 0.001 / 0.011, noise 0.001
- **present.gameplay.world.a08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.173 (PASS >= 0.15, FAIL < 0.10), black 0.077, largest blank 0.077, untextured 0.078 / 0.247, noise 0.001
- **present.gameplay.world.b07_spawn** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.185 (PASS >= 0.15, FAIL < 0.10), black 0.301, largest blank 0.144, untextured 0.001 / 0.011, noise 0.001
- **present.gameplay.world.b08_moving** [Rendering/Integration]: world region (HUD band and player excluded): textured detail 0.171 (PASS >= 0.15, FAIL < 0.10), black 0.077, largest blank 0.083, untextured 0.078 / 0.246, noise 0
- **present.transition.second_match.07_spawn** [Integration/Rendering]: world detail match 1 0.184 vs match 2 0.185 (ratio 1.01; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.second_match.08_moving** [Integration/Rendering]: world detail match 1 0.173 vs match 2 0.171 (ratio 0.99; equivalent within 0.6 - 1.7, and match 2 itself must not FAIL)
- **present.transition.pause_cleared.a** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.131, to the gameplay frame before pause 0.737 (FAIL: the frame still looks like the pause screen)
- **present.transition.pause_cleared.b** [Frontend]: 3.5 s after Resume: structural similarity to the PAUSE frame 0.131, to the gameplay frame before pause 0.736 (FAIL: the frame still looks like the pause screen)
- **present.exclusive.menu_visible_while_moving** [Frontend]: pause menu still drawn after Resume: False; product UI state after Resume = InGame (real input routed to gameplay): True; pawn moved after Resume: True -> a menu stays visible while gameplay accepts movement
- **present.gameplay.route_vs_direct** [Integration]: same start (TnTeamPlayerStart_12481) - direct boot world detail 0.17, frontend-launched 0.184 (ratio 1.08; FAIL < 0.5: the frontend-launched match loses world content that direct boot draws -> route-specific seam, not the map data)
- **present.watchdog.customization.softlock** [Frontend/AssetTools]: customization: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.904, still-inside similarity 0.226); focus present in the screen's dumps True
- **present.watchdog.customization.classification** [Frontend/AssetTools]: customization: FULLY FUNCTIONAL. preview body loaded: True (TR_Sideswipe_ROBO_p, TR_Barricade_ROBO_p); customize.preview owner: renderer; 'LOADING' still shown: False; the posed body on screen: HUMAN (sheet)
- **maploss.normal.process** [Rendering/Integration]: process outcome: clean exit; display-driver / crash events in the run window: none
- **maploss.normal.intro_edges.i1_intro_2s** [Frontend/Rendering]: intro frame side columns (8 % each): detail 0.006 / 0.038, black 0.948 / 0.936; centre detail 0.136 (FAIL: content beside the movie - the frontend shows through)
- **maploss.normal.intro_edges.i2_intro_6s** [Frontend/Rendering]: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.117 (FAIL: content beside the movie - the frontend shows through)
- **maploss.normal.s1.MP_IAC_Streets.environment** [Rendering/Frontend/Integration]: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.207/0.247/0.237, black 0.174/0.152/0.092; product VISUALCHECK median world draws 43, BSP draws 24, materials 37; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 1.62). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- **maploss.normal.b.MP_IAC_Berth.environment** [Rendering/Frontend/Integration]: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.206/0.215/0.221, black 0.355/0.438/0.546; product VISUALCHECK median world draws 195, BSP draws 24, materials 53. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- **maploss.normal.s2.MP_IAC_Streets.environment** [Rendering/Frontend/Integration]: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.206/0.254/0.235, black 0.174/0.157/0.097; product VISUALCHECK median world draws 41, BSP draws 23, materials 35; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.47). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- **maploss.normal.streets_return_counts** [Rendering]: Streets world draws first visit 43 vs after Berth 41 (ratio 0.95); BSP 24 vs 23
- **m07state.transition.t1_after_charselect** []: world PASS (detail 0.207); renderer ok (world 1114, bsp 228); GL state left by the overlay sane
- **m07state.transition.t3_after_resume** []: world PASS (detail 0.231); renderer ok (world 212, bsp 82); GL state left by the overlay sane
- **m07state.transition.t5_after_respawn** []: world PASS (detail 0.154); renderer ok (world 1177, bsp 240); GL state left by the overlay sane
- **m07state.transition.t7_second_match** []: world PASS (detail 0.269); renderer ok (world 2433, bsp 767); GL state left by the overlay sane
- **m07state.transition.t9b_display720** []: world PASS (detail 0.151); renderer ok (world 27, bsp 20); GL state left by the overlay sane

## Commits since the previous validated commit

- 175a634 Integration milestone 08b: weapon effects from the original data (STATUS / FIDELITY)
- f5490be Rendering M35: vector-channel proof applies to instances of a proven master (LogoAUT / LogoDEC GLSL error)
- 79148d0 FIDELITY: weapon FX colour now from each template's decoded DefaultColor (Rendering M34)
- 71c59f5 Rendering M34: per-template ColorByParameter DefaultColor decoded from the LOD streams
- 7a6255e FIDELITY: lobby backdrop (sparse original) and faction emblems verified on screen with Rendering M33
- 9eea24e Rendering M33: Matinee material parameters on MaterialInstanceActors (lobby faction emblems)
- 5ecdbf0 WeaponFx: generic particle runtime seam (Rendering 38c9ecf API) for unreconstructed templates
- 38c9ecf Rendering M32: runtime particle templates (weapon FX from cooked data) + spawn API
- a72befa Lobby emblem glow / fade: subsequence-input fscommands and material parameter tracks
- 1a9eab7 WeaponFx: log each unreconstructed template once; one seam for Rendering's generic runtime
- a1d38c5 Systems M08: weapon muzzle / tracer / squib FX by the held class's templates
- 9bc9f1b Integration M08 docs: lobby backdrop is the original (CONFIRMED, authored.db); crosshair type order HIGH
- 76b25dd Integration milestone 08: one offline multiplayer runtime (STATUS / FIDELITY)
- d889dfe FIDELITY: Streets camera-in-arch frame is authentic (camera trace uses simple collision, RE)
- ddd8a58 Docs: camera obstruction traces simple collision only (RE CONF); Ceiling_Arch clipping authentic
- bb4f209 Loadout: accept the class's own preset grenade bag (PCD_MP) on its chassis; WFC_POINTPROBE diagnostic
- 34aa084 Integration M08: spawn colours use the resolved faction (FFA = Decepticon), not the team
- ee86e95 WFC_MAPSUITE: pickup step starts from full health after the hazard step (harness order)
- 61b236d Integration M08: Gameplay Pass 22 harnesses select the direct boot (WFC_WEAPONTEST / PARTICIPANTTEST / CTFTEST / MAPSUITE / ... / WFC_CHASSIS)
- fbeb1aa Integration M08: weapon / team presentation follows the generic runtime (HUD weapon + crosshair, weapon audio, team energon)
- 8486aa7 Pass 22z: P.O.K.E. 2.0, Nucleon Shock Cannon, Thermo Mine Re-Spawner killstreaks (all 12 implemented)
- 84b3121 Pass 22y: HardLock / AbilityJammer / TransformDisruptor abilities; HardLocked damage-taken x1.4
- 2d31bd0 STATUS: M08 per-weapon impact / anim sounds, per-chassis vehicle audio
- b4b475f Systems M08: vehicle-form audio from each chassis's own HmPlayerVehicleAudioComponent
- 4619f2c Systems M08: per-weapon reload / idle / equip sounds from each weapon's own AnimSet
- df03a82 Docs: STATUS 60/144/240 Hz camera-sync check on the Pass 22 build
- 187ddf4 Docs: FIDELITY remaining-abilities list (fix)
- fc9028b Docs: FIDELITY remaining-abilities list
- fe689fe Pass 22x: RollerSphere ability; loadSkinnedGlb replaces the model on reload
- 5ef78f6 FIDELITY M30: robot / vehicle form on all 10 MP maps (20/20 PASS, visually checked)
- cfa5aeb Systems M08: per-weapon impact audio (DefaultImpactSound + victim HitEffectPlayer)
- 088b703 Rendering M29: loadSkinnedGlb replaces the model (in-process reload hang)
- 9f64885 Pass 22w: GuidedMissile ability + Omega Missile streak; ten-map stress table; map-independent tests
- e5b523a One renderer across matches by default when the renderer releases match textures (Rendering M28, detected)
- f65f4b2 Persistent renderer: clear the match HUD reticle at match unload
- ea2a3f3 Rendering M28: match-owned textures released by unloadMapRenderData (persistent renderer)
- c745d8c Pass 22v: SpawnSentry ability (deployable sentry turret)
- af9849c Pass 22u: Drain ability (TnAbilityDrain / TnBuffDrainSource)
- 278ce76 Pass 22t: flag / bomb taken with the contextual pickup button (E), not on touch
- a4c71e8 Pass 22s: buff killstreaks (Orbital Beacon 1/2, Health Matrix 2.0, EMP)
- f03b935 Pass 22r: SpawnAmmoCrate ability (ammo beacon)
- 437d07a Pass 22q: Barrier ability (TnAbilityBarrier / TnBarrierSpawnable)
- 6587680 Pass 22p: flag / bomb carrier heavy-weapon rules and damage-type-gated knockback
- f07132c Pass 22o: tank cannon pitch (VEH_Tank_ANIMTREE WeaponPrimary TurretConstrained on C_Cannon_XB)
- f1403a4 Pass 22n: grenades (TnGrenadeBag / TnGrenadeThrower / TnProjectileGrenadeBase)
- e9807c4 Pass 22m: homing lock-on (TnWeaponHoming / TnProjectileHoming) and TakeRadiusDamage falloff
- 5c6aada Pass 22l: melee (Q) and the Whirlwind ability from TnMeleeManager evidence
- 1fc54a3 Truck vehicle FX/sounds only for the Optimus chassis (no truck FX at truck sockets on other bodies)
- eb99e76 Docs: HUD contract Pass 22 field table
- e64e4af Pass 22k: Hover ability (acrobatics JumpingToHover / Hovering)
- 5decaf3 Pass 22j: Cloaking ability, HUD markers for CTF/EXT objectives, tag labels
- 3c0c1f9 Persistent renderer across matches (opt-in WFC_PERSISTENT_RENDERER=1), GL census measurement, map chain
- 607e8fd Pass 22i: Warcry and Shockwave abilities from the authored CDOs
- 6d34381 Pass 22h: per-chassis vehicle camera sets from the authored HmCameraStrategySet
- 0421074 Vehicle weapons fire from the chassis' vehicle WeaponSocket_Primary
- 5d1850a Pass 22g: map suite covers every mode and pickups; clearance diagnostics
- 3a47c12 Pass 22f: killstreaks (count, acquisition by specialty, B trigger, four authored effects)
- 7eeb76d Pass 22e: projectile weapons, vehicle-form weapons, form damage and self-damage multipliers
- 8f98043 FIDELITY: title-scene grade after M25 is UI_FrontEnd's authored PostProcessVolume (cameras inside its brush)
- f669251 Preview body handles: dropped with the renderer instance; renderer body count in the soak
- 920eeb8 Docs: restore code names lost in the 22d participant notes
- 5c68db9 Pass 22d: non-local participant pawns own full Characters (bot-ready, no AI)
- 7385d02 WeaponFx: tracer ribbon u orientation HIGH (stock UE3 Trail2 head = u 0); record PARTIAL items
- ba5e9da WeaponFx: tracer smoke ribbon follows Tracer_Smoke_MAT (fixes the Ion Blaster grey slabs)
- 8f06c90 Rendering M26: matc Panner/Rotator vector Time (Debris Megatron_com_Mat), tracer slab root cause
- 44cb835 Pass 22c: Code of Power (CTF rounds, flag) and Countdown to Extinction (bomb) on the shared framework
- 3c2febd Profile look settings -> Gameplay's setLookSettings (detected)
- c555833 Pass 22b: car/tank/jet vehicle forms, data-driven weapons and loadouts, abilities (Dodge), multi-map
- 92dd833 Close an open message box on level travel

Evidence: `route/` (PRESENTATION.md, sheets), `mapswitch/` (MAP-LOSS.md), `renderstate/`, `mechanics/`, `selftest_*/`.
