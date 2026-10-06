# WFC Reconstruction — Fidelity Audit

**Principle:** the original Xbox 360 build is the specification. Recover what WFC actually
did; never let a provisional rebuild constant masquerade as original behaviour.

**Source-of-truth order:** (1) observed original behaviour · (2) `default.xex` code/constants ·
(3) authored cooked data (packages, `Coalesced_*` `.ini`) · (4) extracted metadata ·
(5) current rebuild · (6) guesswork.

**Evidence roots:**
- Cooked UE3 config (decompiled Coalesced): `ExtractedAssets/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/*.ini`
- Per-sublevel map metadata: `ExtractedAssets/maps/*.json`
- Extracted asset metadata: `ExtractedAssets/VerticalSlice/**/{character,weapon,map}.json`
- Executable: `Game Dump/default.xex` (via Ghidra/ReVa) — for compiled class defaults.

Legend — CONFIDENCE: **CONF**(irmed from authored data/exe) · **HI** · **MED** · **LOW/GUESS**.

---

## INTEGRATION MILESTONE 08 — CLASSIFICATION (2026-10-05)
| Item | Mark | Notes |
|---|---|---|
| Selected class / chassis → spawned body, both factions, 8 chassis played through the frontend | CONFIRMED contract / VISUALLY VERIFIED | 10-match soak frames; FFA = Decepticon (TnGame) |
| Robot ↔ vehicle pairs per chassis | VISUALLY VERIFIED (Runner, Warpath, Soundwave, Ironhide, Brawl) | Gameplay Pass 22 forms |
| Class loadouts from the generic weapon table (no universal Ion Blaster) | HIGH CONFIDENCE (authored presets + provider restrictions) | grenade bags = the class PCD_MP grenade (Gameplay bb4f209) |
| HUD current weapon from the equipped weapon | CONFIRMED movie API / HIGH mapping | crosshair type per weapon PROVISIONAL (native UNKNOWN) |
| Weapon audio follows the equipped weapon | HIGH CONFIDENCE (Systems tables) | integration seam |
| Team EnergonColor (Autobots / Decepticons / neutral) | CONFIRMED values (class defaults) / HIGH application | drives the energon trim; DM neutral orange is a human check |
| Ion Blaster tracer (Tracer_Smoke_MAT) | VISUALLY VERIFIED | Systems ba5e9da / 7385d02 |
| Persistent renderer, bounded resources | HIGH CONFIDENCE | GL census +1 texture per new map, flat on revisits; decoded audio baseline; release_path_check PASS |
| Per-map colour grades on the player route | CONFIRMED data / applied | MP_Streets_CLUT, MP_OrbitalDebris_CLUT, clut_mp40, desaturation40 per map |
| Camera inside Streets Ceiling_Arch underside | CONFIRMED ORIGINAL | TraceCamera simple collision only (RE f150a6a) |
| Escalation maps under versus modes | not applicable | no versus mode actors authored |
| Lobby backdrop (SpaceDome + CybertronCards + emblems, no room / characters) | CONFIRMED ORIGINAL (authored.db, Frontend) | the presentation gate's "mostly blank" lobby check is a stale expectation |
| Non-Ion weapon muzzle / tracer FX | HIGH CONFIDENCE (cooked templates) / VISUALLY VERIFIED (Shotgun, Assault Rifle) [M08b] | Rendering M32 runtime + Systems WeaponFx; Trail2 / Beam2 ribbons PARTIAL |

---

## INTEGRATION MILESTONE 07 — CLASSIFICATION (2026-10-05)
| Item | Mark | Notes |
|---|---|---|
| Selected chassis spawned (frontend selection → Gameplay body → drawn) | CONFIRMED contract (TnPlayerCharacterData / ResolveReplicatedCharacterData) / PARTIAL content | 27 MP chassis load from the AssetTools export; no substitute body. A body that cannot be built refuses the spawn and logs it [RECONSTRUCTION: loud failure; the original leaves a body-less pawn] |
| Custom character ChassisTypes per faction → spawned body | CONFIRMED (script) | Frontend fillFullSelection → Gameplay resolveChassis(chassisByFaction[team]) |
| Preview vs match body / colours | HIGH CONFIDENCE | the same roster mesh and chassis id; the same sRGB → linear colour conversion; verified by frames |
| Character colours on the match pawn (Cust_Color_A / Cust_COLOR_B) | CONFIRMED parameters (TnCharacterApplier) / PARTIAL | energon (team) colour not applied yet |
| Character audio profile follows the body | HIGH CONFIDENCE (Systems profiles from authored SoundEventSets) | integration seam in applyChassisToLocalPawn |
| Colour picker input (left-stick callback; LT / RT palettes; no mouse on swatches on PC) | CONFIRMED (movie script) | Frontend 1af7e74 |
| Create a Character persistence (wfc_characters.ini) | PC ADAPTATION | the original writes the profile customization file |
| Locked chassis skipped in the chassis cycle | CONFIRMED (LockedChassis flags) | e.g. Scattershot needs the campaign |
| Title / customization desaturation | HIGH (authored PostProcessVolume_15709 desat 0.5 / bloom 0.2), not CONFIRMED | human check against an original capture |
| Map memory across 8 maps × 2 | HIGH CONFIDENCE: no accumulating leak | high water at Rust, then a plateau; per-match renderer reset stays the default |
| Tracer smoke slabs | PARTIAL (Systems WeaponFx) | the original material's width mask is missing |
| Experimental presentation-gate expectations: Quit → main menu; keyboard rebinding; Back to main menu from Create a Character; "Optimus for a non-Optimus selection" | STALE (retired here) | the original confirms Quit through the lobby; the PC menus have no rebinding; Back from Create a Character returns to the party lobby; the body check now passes on the real body |

---

## INTEGRATION MILESTONE 06c — CLASSIFICATION (2026-10-05)
| Item | Mark | Notes |
|---|---|---|
| Frontend-route world without depth testing (UI pass left GL state) | ROOT CAUSE CONFIRMED / FIXED / VISUALLY VERIFIED | Frontend a96f841 restores; Rendering ab851f5 establishes; release_path_check noDepth = 0 on every capture |
| Render-data root (Release layout) | FIXED (M06b) | Rendering 398b732 |
| Streets ↔ Seed memory | HIGH CONFIDENCE: plateau, no accumulating leak | Debug and Release measured; Rendering 412713c drops load peaks by ~100 MB |
| Movie pillarbox (16:9 over black) | CONFIRMED ORIGINAL presentation / PC window handling | verified at 2000×800 |
| HUD layout from the live window size (Hud_GFX noScale) | CONFIRMED (movie) / PC ADAPTATION (window sizes) | Frontend f5ada69 |
| Offline account identity | PC ADAPTATION | the typed name in the lobbies / kill feed; no Xbox Live identity |
| Fullscreen at the saved resolution (display mode change) | PC ADAPTATION | Frontend 74b84d8 |
| Particle size × emitter scale | CONFIRMED ORIGINAL (xex) | Rendering b0d47b2; the Streets pinned cameras are unchanged |
| Quit → confirmation → party lobby | CONFIRMED ORIGINAL (script) | Experimental's direct-to-main-menu expectation is stale |
| Selected body drawn | PARTIAL (RECONSTRUCTION FALLBACK) | Gameplay explicit fallback |

---

## INTEGRATION MILESTONE 06b — PLAYTEST REGRESSION CLASSIFICATION (2026-10-04)
Full report: STATUS.md (INTEGRATION MILESTONE 06b).

| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| M06 black world / malformed menus | — | plain Release launch reproduced it; Rendering 398b732 reproduced it independently | **INTEGRATION REGRESSION — ROOT CAUSE CONFIRMED, FIXED, VISUALLY VERIFIED** | render-data root resolved from build/release/bin to a missing folder → silent legacy fallback. Rendering's renderDataRoot() search; visible failure (error + VISUALCHECK FAIL + red frame); 11 / 11 visual suite and 29 / 29 human-flow frames on the original path |
| Pause menu over gameplay after Resume | pause UI closes on Resume | merged-build trace (no ui.close) | INTEGRATION REGRESSION — FIXED, VISUALLY VERIFIED | onCurrentUIClosed runs closeCurrentUI |
| Choose Character at match start | stays until chosen (UseInGameLobby = !HasSelectedCharacter) | Frontend 6fb19f8 (script) | CONFIRMED ORIGINAL | Frontend implementation |
| Extras Movies / Credits | Game.PlayMovie → MovieStarted / MovieEnded | Frontend 732a5b2 (script) | CONFIRMED ORIGINAL / VISUALLY VERIFIED | Credits plays with audio, skip returns control |
| Intro on later boots | HasWatchedIntroMovie is a session flag | Frontend 3e9db97 | CONFIRMED ORIGINAL | the earlier M06 note ("persisted = original") is corrected |
| Account name / rename text entry | TextPrompt_GFX input fields | Frontend 9dc70ec | PC ADAPTATION (local accounts) | verified by typing |
| HUD tweens / kill-feed stacking | HmObjectInterpolator.addInterp | unhandled bridge call | PARTIAL (Frontend) | open |
| Customization preview pawn | preview pawn of the selected chassis | Rendering / Frontend handoff | PARTIAL | open |
| Lobby / title presentation | live UI levels | Rendering M09 | PARTIAL (some scenes authored dark; one title laser / Matinee effect partial) | — |
| SwapBuffers hang at 1600×900 under multi-process GPU load | — | 2 hangs in the AMD driver with 3–4 other lanes' GL processes running | UNKNOWN | human check |

---

## INTEGRATION MILESTONE 06 — PROVENANCE OF INTEGRATION DECISIONS (2026-10-04)
Full report: STATUS.md (INTEGRATION MILESTONE 06). Lane provenance stays in each lane's section.

| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Boot-movie sound | the Bink's own audio tracks (no SoundCue); MoviesToAlwaysPlaySound lists the logos; CINE_MUTE_FOR_BINK mutes the game mix | Systems M07 (bink_tracks.py on the dump; config) | CONFIRMED (data / config) | Systems MovieAudioPlayer streams the tracks; Frontend reports start / stop. One decoder (Frontend's WAV-cache path removed) |
| Movie language track | native `HmPlayerController.MovieAudioSetup` / BinkSetSoundTrack | not recovered | **UNKNOWN** | centre track 5 for English, **PROVISIONAL** (logos: all centre tracks identical); `WFC_MOVIE_LANGSLOT` override |
| Movie A/V start offset | Bink plays audio and video from one clock | — | UNKNOWN (native) | measured 0.1–0.35 s constant start offset (audio after video), no drift over 128.7 s. Video not slaved to the audio clock [PARTIAL] |
| Match announcer / music | TnGameTypeMessage 0 / 1 / 2, TnGameProgressAnnouncementMessage 0–7, TnAnnouncer OPRIME / MGTRON | Systems M07 (decompiled script) | CONFIRMED (script) | Gameplay's MatchEvents → World::matchAudio() (no second timer / state machine) |
| Versus game-over line | TnVersusGameOverMessage | TransContent not decompiled | PARTIAL | winning-team announcer event |
| In-match HUD | Hud_GFX + data stores; no minimap | RE OVERNIGHT A0–A9 | CONFIRMED | Frontend runs Hud_GFX from World::hudState; no minimap |
| Kill-feed presentation | 5 rows, 5 s + 1 s fade | RE A2 | CONFIRMED | rows overlap in the rebuild [PRODUCT FAIL, Frontend] |
| Character → pawn | selection → PRI._SelectedCharacter → ResolveReplicatedCharacterData; spawn waits for it | RE / Gameplay CharacterRoster.h | CONFIRMED (rules) | selection reaches Gameplay and the chassis is resolved; drawn body Optimus [RECONSTRUCTION FALLBACK] |
| Map KillZ | persistent level TnWorldInfo.KillZ | AssetTools physics.json (BASE) | CONFIRMED (data) | per map (Streets -750 m unchanged; Remnant authors 0) |
| Rotating movers | PHYS_Rotating RotationRate | AssetTools <map>_movers.json | CONFIRMED (data) | Gameplay: yaw-only in collision; other axes render-only [PARTIAL] |
| Generic render index | AssetTools map_complete.py (render_index_generic) | AssetTools MAP_PIPELINE | CONFIRMED (data) | pickup visuals converted (Streets 14 / 14 exact; the rotation conversion is only exercised with identity rotations: HIGH); totems / KOTH rings / destructibles not converted [PARTIAL] |
| Map selectability | HasRequiredAssets | RE 3.2 | CONFIRMED (concept) | cooked + world + (runtime or generic render index) [REBUILD RULE] |
| PC UI bindings | console pad (A / B / Start / D-pad) | shipped movies | CONFIRMED (console) | Enter / Escape / arrows / F3 / Tab + mouse [PC ADAPTATION] |
| Brightness | DisplayGamma = 2.2 + Lerp(-0.95, 0.95, GammaSetting / 100) | decompiled GetGammaSetting | CONFIRMED (mapping) | profile → IRenderer::setDisplayGamma at boot and after each renderer recreation; persists through map loads |

---

## INTEGRATION MILESTONE 05 — PROVENANCE OF THE INTEGRATION GLUE (2026-10-04)
Full report: STATUS.md (INTEGRATION MILESTONE 05). Only glue added at integration is listed here; each lane's own
provenance stays in its section.

| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Match launch from the frontend | StartLevel URL → TnVersusGame InitGame (GameModeTag / PointsToWin / TimeLimit) | RE M05 blockers D1–D3 | CONFIRMED | `World::launchMatch(MatchLaunch::fromURL(url))`; map = catalog runtime directory |
| PendingMatch UI | WaitingOnGameStart → character select → PreGameCountdown → event 3 at InProgress | RE D5 | CONFIRMED order; character select absent | default character selected at load; countdown 10.14 s measured |
| Death / respawn UI | MinRespawnDelay 3.0 s → event 4; wave 5.0 s → RestartPlayer → event 5 | RE E7 | CONFIRMED | glue timer 3.0 s; Gameplay wave (4.98 s measured) |
| Match end / return | MatchOver → event 9; +15 s → ReturnToGameLobby → UI_Lobby_m?...?MapId= | RE F4 / F6 | CONFIRMED | `GameFlow::returnToGameLobby` |
| In-match data-store values | GRI / PRI pushed to <CurrentGame:*> / <PlayerOwner:*> | RE G | HIGH | `frontend::MatchValues` from `World::hudState` |
| Teams | offline PickTeam(255) before StartMatch | RE E4 → rebuild | HIGH | Gameplay PickTeam; frontend team index not passed |
| Map selectability | HasRequiredAssets | RE 3.2 | CONFIRMED (concept) | rebuild: cooked + world.glb + render_index.json (Gorge disabled) — PARTIAL (rebuild-specific asset rule) |
| Loading underlay lifetime | Bink loading movie ends with the loading screen | TnMoviePlayer | HIGH | released in the match loop too (was left composited over the world) |
| Health regeneration | after 2.0 s, 20 HP/s, segment-limited | RE I3 | CONFIRMED (RE) | NOT IMPLEMENTED (Gameplay: UNKNOWN) |
| EndGameStats experience | profile XP | — | UNKNOWN to the rebuild | "undefined / NaN" (no XP service) — PARTIAL |
| Hud_GFX | GFx HUD movie | shipped movie | CONFIRMED (data) | not drawn; reticle only — PARTIAL (Frontend / Rendering handoff) |

---

## FRONTEND PASS 7: PLAYTEST PRESENTATION (2026-10-05, agents/frontend)
- **Create a Character shrink / shift after a weapon slot: fixed (runtime bug, Flash semantics CONFIRMED).** The weapon
  menu's background registers a Stage listener (onResize: setProperty('', _width / _height, Stage size + 30)) and is
  removed when the menu closes; the listener stays. The AVM1 VM ran a function whose defining timeline was removed with
  the movie ROOT as its "" target, so a later onResize resized the whole movie. A removed defining timeline now stays
  the target (the dead clip, as in Flash). Verified by clip-geometry dumps (all top-level clips identical) after
  primary / secondary slots, the Decepticon chassis menu, a class change and leaving / reopening, at 1280x720 windowed
  and 2560x1440 fullscreen; customize soak with weapon-slot visits in every class PASS at 2560x1440 fullscreen and
  1920x1080 windowed (42 checks, no AS errors / collected-object uses). Note: soaks at a non-native fullscreen mode
  (1920x1080 on a 2560x1440 desktop) are unreliable on this shared machine - another session's window takes focus and the
  fullscreen window minimizes by design (desktop restore on focus loss), so scripted clicks land on a 0-size viewport.
- **Menu hitches.** WFC_FRAMEPROF=<ms> logs every presented-frame gap with per-category time. The visible stall after
  title <-> party lobby travel was the renderer's first drawn frame (effect / weapon program prewarm, 737 ms first,
  327 ms repeat, then a driver stall). Frontend now draws the new scene once under the loading screen [PC ADAPTATION:
  the original travels behind its loading screen]; Rendering (ba68889) moved the prewarm out of frontend scenes, keeps
  linked programs across loads and yields finer. Remaining: audio prefetch 37-94 ms at travel start (Systems).
- **Resolution: CONFIRMED path.** Settings Commit -> PCSettings.SetResolution(w, h, fullscreen) -> windowed client size,
  or fullscreen = a real display-mode change to w x h (current refresh rate; desktop restored on windowed / alt-tab /
  exit); the GL default framebuffer is the window, so rendering is at the chosen size; the UI (showAll stage, visible
  area, vignette) lays out for it. Verified: 1280x720 / 2560x1440 fullscreen and 1600x900 windowed frames at those
  sizes, monitor modes logged. (The fullscreen mode switch is the PC SKU behaviour reconstructed; not borderless.)
- **Frame limiter: PC EXTENSION.** [PCSettings] FrameLimit (0 = off, the default) / WFC_FPS_LIMIT; waits after the swap;
  no original menu row. Measured 60 -> 59, 144 -> 143, 30 -> 29 fps; match clock real-time at 30 fps.
- **QA panel: DEBUG ONLY, NOT ORIGINAL.** WFC_QA=1 (F10): a separate tool window to launch / restart map / mode / class
  scenarios through the normal flow; WFC_QA_LAUNCH / WFC_QA_RESTART_AFTER. Weapon override pending Gameplay.
- **Title vignette:** unchanged since a661851 (human-confirmed); holds at 1280x720, 1600x900, 1920x1080, 2560x1440.
- **Cancelled Accounts prompt created the account later: fixed (Flash semantics CONFIRMED).** TextPrompt_GFX's Key
  listener outlives the prompt and submits on Enter only while Selection.getFocus() is its field; the runtime kept the
  removed field as focus, so Accept in Extras created the typed account. Selection.getFocus now forgets a removed object
  (136ac7a). Menu enter/leave loop (nav_stress 3 cycles + 1 match): 79 checks PASS, title state identical every cycle.
- **Menu hitches after the owners' fixes:** Rendering ba68889 / 7b74b18 removed the lobby first-frame stall (no gap > 40 ms
  once a menu is visible; title revisits 30-41 ms); Systems 8df544b moved the audio prefetch to a worker (~0.2 ms).
  Boot title: Rendering 2692e46 prewarms the title's placed emitters (VIG lightning / steam) during the load; the two
  slow first scene draws (269 / 215 ms) are gone, no scene.draw > 40 ms after the load (1920x1080 windowed,
  WFC_FRAMEPROF=40). Remaining: one 90 ms frame as the menu opens at the end of the load (no scene draw in it) and the
  loading-screen steps (40-150 ms, single indivisible items), so the loading animation is choppy, not frozen.
  Integration milestone 08g (all three owners' fixes + Systems 593311c volumes): travel start 42-47 ms (audio prefetch
  0.6 ms), title returns 42-46 ms, Settings / Extras / Movies clean, lobby load steps 112-167 ms; the profile volumes are
  applied at boot (profile.apply owner "volumes -> Systems"). A first run with 200 / 238 ms load steps was machine load
  from other lanes (a 3-round-trip rerun matched the earlier numbers).
  Boot title-open frame (95-105 ms): 48 ms was the boot startup movie decoding its backlog after the title load (fixed,
  fe26688: underlay / boot-hold movies advance one frame per update, PC ADAPTATION); the remaining 43-44 ms is the
  title's level audio start (uiLevelStarted, Systems) on the frame the menu appears. Now 53 ms. The same 43-55 ms
  level-audio start recurs on every return to the title (reported to Systems).
  Travel start: reopening the TF_LoadingScreen underlay cost 31-34 ms per travel; the decoder is now kept between
  loading screens (PC ADAPTATION, one reader resident), so only the first travel after boot pays it. A load step's
  frontend share (loading movie + underlay) is ~7.5 ms; the rest of each 112-167 ms step is the load work (Rendering).
  Systems 47b74c0 (level waves warmed by prefetchLevel on a worker) confirmed on 08g + 47b74c0 + 94797cd: no frame
  over 40 ms involves audio across 7 level starts; the boot title-open frame is under 40 ms. The travel underlay is
  now opened under the boot loading screen (PC ADAPTATION), so no travel opens it. Left over 40 ms: the load steps
  and loading-screen frames during scene loads (Rendering's load work).

## FRONTEND: TITLE VIGNETTE / MENU BACKGROUNDS COVER THE SCREEN (2026-10-05, agents/frontend)
- Human-confirmed: the title vignette left bright vertical strips at both sides (87.5 % of the width covered at 16:9).
- Cause (Frontend, GFx host): the menus are authored on a 1120 x 720 stage, fitted (showAll) and centred; every menu sizes
  its full-screen pieces in its own Stage.onResize from Stage.width / height (FrontEnd_GFX: screenSoftEdges_mc, the
  authored scale-9 vignette FrontEnd_GFX_I24, centred at 560; PauseMenu bg_mc / bg2_mc; Settings, Extras, Accounts, lobby
  movies alike) and footer_mc is authored at x = 1200, outside the stage. The rebuild reported Stage.width = 1120 and
  sent onResize only to noScale movies, so the vignette stopped at the stage edges.
- Fix: showAll movies report the visible area in stage units (1280 x 720 at 16:9) and every movie gets onResize when the
  viewport changes or a listener registers. The authored vignette is unchanged; it now spans -80..1200. [GFx behaviour
  the movies rely on: HIGH; nothing replaced]. Hud_GFX keeps noScale.
- RE (re-workspace, native): UGFxMovie::execStart (0x82A24770) -> view setup (0x82A1FF08) sets the viewport to the full
  game viewport and never calls SetViewScaleMode or an alignment setter [CONFIRMED], so the menus run at Scaleform's
  default showAll / Align_Center with off-stage content unclipped [HIGH] - the model the fix uses.
- Verified at 2560 x 1440 fullscreen, 1920 x 1080 fullscreen and windowed: edge columns 36-47 (dark) vs 61-71 before;
  Settings / Extras / party lobby render with full-width backgrounds.

## FRONTEND: LOBBY EMBLEMS AND BACKDROP (2026-10-05, agents/frontend)
- **Party / game lobby backdrop: CONFIRMED ORIGINAL (sparse).** UI_PartyLobby_m / UI_Lobby_m hold no geometry; the
  streamed UI_CharacterCustomization_m shows UI_LobbyMaterials_p.SpaceDome_STAT and four CybertronCard_STAT planes; the
  two robot SkeletalMeshActors are authored hidden; robots appear only in Create a Character (initStreamingLvl).
- **Faction emblems: CONFIRMED.** Four MaterialInstanceActors (Autobot / Decepticon icon and glow) animated by matinees
  (InterpTrackFloatMaterialParam Highlighted 0 -> 1 / 0.5 s, Opacity 0 -> 1 / 0.3 s) that the movie drives through
  subsequence inputs (glow / dim / fadein / fadeout <Faction>, hidePlayer / unhidePlayer). The exporter resolves those
  inputs; values go to Rendering's setFrontendMaterialParam (M33). Verified on screen: emblems behind the robots in
  the overview, the selected faction glowing in its chassis menu, none in the party lobby / class list. FLOW
  scene.emblem for the gates. No vector material tracks exist in the frontend levels.

## FRONTEND: ONE RENDERER ACROSS MATCHES (2026-10-05, agents/frontend)
- The match cleanup recreated the renderer (M06 hard reset, not original). With Rendering M28 (unloadMapRenderData also
  releases the textures a match uploaded) one renderer now serves the session: detected at compile time; renderers
  without M28 keep the hard reset. WFC_RECREATE_RENDERER=1 / WFC_PERSISTENT_RENDERER=1 force either path. PC ADAPTATION
  (engine lifecycle).
- Evidence (merge preview with agents/rendering ea2a3f3): release_path_check PASS on the default path; 8-map chain live
  textures 43 -> 49 (+1 per new map: its UI thumbnail; the Streets revisit adds none) and privateMB 2253 -> 2612 (the
  recreate path: 42 -> 48, 2256 -> 2646); before M28 the persistent chain leaked 114 -> 716. Customize soak 69 checks
  PASS with preview bodies kept across the match; the post-match Create a Character frame matches the recreate path
  after the match reticle is cleared at unload. Preview body handles belong to the renderer instance.

## FRONTEND PASS 6: CREATE A CHARACTER PREVIEW AND SELECTION (2026-10-05, agents/frontend)
Original behaviour from TnCharacterScriptBinding / TnCharacterCustomizationData / TnCharacterApplier (decompiled),
CustomTransformers_GFX (AVM1) and UI_CharacterCustomization_m (Kismet). RE re-workspace confirmed the room, camera and
palette findings independently (notes/TARGETED_PASS3_2026-10-05.md §B).

| Area | Original | Rebuild | Label |
|---|---|---|---|
| Preview bodies | PreviewGuy0 / 1 per faction, respawned only on a chassis change (UpdateSinglePreviewCharacter) | roster robot glTF per slot; the respawn resets form and idle clock | CONFIRMED |
| Idle | SetupPreviewAnim: IdleNode.SetAnim(Cust_Idle); 13 chassis have it, the rest keep the reference pose | Rendering loadPreviewBody / posePreviewBody (detected) | CONFIRMED |
| Ground | OnPreviewPawnTick FindGround | Rendering sceneGroundHeight (detected) | HIGH |
| Change Form | TransformPreviewCharacter toggles the first visible pawn; ...ToRobot forces robot (clickLStick / buttonY) | vehicle glTF swap; no transform animation | CONFIRMED script / PC ADAPTATION (no animation) |
| Rotation | none (bRotateTowardFocus false, no rotate call in the movie) | none | CONFIRMED |
| Weapon on the preview | none (preview CharacterData has chassis + colours only) | none; weapons are 2D icons in the menu | CONFIRMED |
| Faction pawn visibility | Preview_Characters fscommand -> ToggleHidden PreviewGuyN | pawnVisibility export | CONFIRMED |
| Class camera | Chassis_To_Cam_ID* by CustomizationCameraId; Play / Reverse; FOVAngle 70 -> 60 / 65 | as original | CONFIRMED |
| UpdatePreviewCharacter args | wrapper (CharacterChassis, CharacterFaction, CharacterPrimary, CharacterSecondary) | fixed (faction was read as the primaries) | CONFIRMED |
| Palette textures | GFxMovie ExternalTextures: autobotPalette_0..4 -> UI_CustomChar_p.A_*, decepticonPalette_5..9 -> D_* | sampled from the bound textures (was the "EXTERNAL TEXTURE" placeholders) | CONFIRMED |
| Palette coordinates | picker gradient 256 x 256 units (gradWidth / gradHeight) over the 128 px texture | scaled x * w / 256 | HIGH |
| New / reset character colours | ResetCharacterFromName: random palettes (Autobot 0-4, Decepticon 5-9), coords RandomInt(255), colours black; reset keeps FriendlyName | as original; a fresh profile is randomised once and written | CONFIRMED |
| Match colours | black = kUseDefaultColor (material default paint); committed picker colours are sRGB | carried in the selection contract | CONFIRMED |
| Colour picker cursor | Input.RegisterLeftStickCallback(path.updatePaletteCursor), (StickX, StickY) per frame; LT / RT change palette | presenter calls it with the pad stick / held arrows | CONFIRMED contract, PC keys PC ADAPTATION |
| Selection handoff | TnPlayerController.SelectCharacter -> PRI._SelectedCharacter | GameFlow::SelectedCharacter contract -> game::CharacterSelection (full fields, detected) | CONFIRMED fields |

GFx runtime fixes found by this pass: intervals on removed clips no longer fire; the collector roots removed clips'
subtrees (intermittent use-after-free crash); unloadMovie keeps children alive. Diagnostics: WFC_GFX_GCCHECK,
WFC_GFX_NO_GC, WFC_NOPAD. The collector counter is per movie (it was shared, so one movie took every collection).
Posed preview bodies: LRU cache of 8 (Rendering: bodies are CPU-only; releasePreviewBody on eviction). Preview handles
belong to the renderer instance: the match cleanup recreates the renderer, so setRenderer drops the cache (soak: the
renderer held 0 bodies after a match while 8 stale handles were cached); previewBodyCount guards it as well.

UNKNOWN: the native GetPixelColor coordinate scaling (taken as 256-unit gradient space); the PC key binding of the
picker cursor (arrows used).

## FRONTEND: CUSTOMIZATION CAMERA PER CHASSIS, MATINEE FLOAT TRACKS (2026-10-04, agents/frontend)
- **Customization camera per chassis: CONFIRMED ORIGINAL.**
  - The chain, all from cooked data:
    - `SeqVar_TnCustomizationCameraId` (decompiled): the preview pawn's chassis provider's `CustomizationCameraId` (TransCustomization.ini).
    - `UI_CharacterCustomization_m` `Chassis_To_Cam_ID` / `_0` / `_1` / `_2` compare it with 0..3 and finish with Scout / Scientist / Leader / Soldier.
    - Those outputs Play (Autobot_IN / Decepticon_IN) or Reverse (Autobot_OUT / Decepticon_OUT) the eight class camera matinees on CameraActor_2082.
  - The movie's `shiftChassis` / `shiftCenter` (CustomTransformers_GFX) send `hideAutobot` / `hideDecepticon` / `unhideAutobot` / `unhideDecepticon`. Through Preview_Characters these reach those subsequences.
  - The exporter records this as `cameraSwitches`; the runtime evaluates it with the preview controllers' chassis.
  - Matinee Reverse runs the camera back to the centre pose.
- **FOVAngle and DrawScale tracks (InterpTrackFloatProp): CONFIRMED ORIGINAL data, evaluated like the move tracks.**
  - The class cameras key FOVAngle 70 -> 60 / 65. Keyless title FOV tracks keep the camera's own FOV.
  - The title vignette DrawScale (ships, boosters) is sent to Rendering's `setFrontendActorScale` when the renderer has it (compile-time detected); otherwise the data is evaluated but not applied.
- Verified:
  - Live: Scout -> camera id 0 -> SCOUT moves; Leader (Truck) -> 2 -> LEADER moves; both sides open and reverse.
  - `wfc_frontend_tests` 74 / 0 (`scene.customizeCamera.*`, `scene.drawScale`: djDS01 0.08, DSbooster 1.0 at 250 s).
  - On screen (merge preview of df79c6f with agents/rendering e014f45 and its UI_PartyLobby render data): all four classes, both factions. Opening a chassis menu shows only that faction's pawn, fully framed on the floor (Autobot left of the menu, Decepticon right); Back restores both.
- **Preview pawn visibility: CONFIRMED ORIGINAL.** The Preview_Characters fscommand -> ToggleHidden (PreviewGuy0 / 1) wiring is exported (`pawnVisibility`) and applied.
- **Preview pawn height: HIGH.** Stood on the floor under the PathNode with Rendering's `sceneGroundHeight` (the original's OnPreviewPawnTick FindGround), until Gameplay supplies posed bodies.
- **GFx runtime fixes found by this check (PC ORIGINAL Flash semantics):** an interval on a removed clip no longer fires; removed clips' descendants stay rooted for the collector (an intermittent use-after-free crash, about 1 in 3 customization runs); `unloadMovie` keeps unloaded children. Diagnostics: `WFC_GFX_GCCHECK`, `WFC_GFX_NO_GC`.

## FRONTEND PASS 5: WORLD LOSS, VIEWPORT, HUD PRESENTATION (2026-10-04, agents/frontend)
Human playtest of the integrated Release build plus Experimental's presentation gate (bisect: first bad b1fce97).

**Frontend-launched world loss (fixed, a96f841):**
- **Cause:** `GfxRendererGL::begin` disabled `GL_DEPTH_TEST` / `GL_CULL_FACE` (plus scissor / alpha test / lighting /
  fog), enabled stencil and blend, and bound its own program / VAO / buffers / FBOs; `end()` did not restore them.
  From b1fce97 the in-match HUD movie ran that pass on every match frame, so the next world frame drew Streets
  without depth testing: later draws (sky, smoke, translucents) covered the architecture; the pawn, effects, decals and
  HUD stayed visible.
- **Why direct boot worked:** it never runs the UI pass (no frontend, no HUD movie). `WFC_GFX_EMPTY` skips only the
  movie draws, not begin / end; `WFC_NO_FRONTEND_SCENE` removes an unrelated layer.
- **Fix:** the UI pass records the GL state it changes and restores it (state contract below). The HUD stays.
  `WFC_GFX_NO_GLRESTORE=1` reproduces the pre-fix frames.

**CONFIRMED ORIGINAL (authored movie behaviour now honoured):**
- **Hud_GFX native extensions** (`_global.gfxExtensions`): `MovieClip.interp` → `HmObjectInterpolator.addInterp` and
  `setColor` → `HmActionScript.setColor` were unhandled, so every HUD tween stayed at its start values and every
  colour stayed white (the "too large" health segments and ammo bar were their glow / start states; the clock and
  announcements never appeared). They now run with the menus' own AS interpolator semantics.
- **`Stage.scaleMode = 'noScale'`** (Hud_GFX): the movie lays itself out from the viewport (`Stage.width / height`,
  9 safe-frame anchors scaled by `Stage.height / 720`, origin centred) and listens for `onResize`. The runtime now
  draws a noScale movie 1:1 in pixels and reports the viewport.
- **Account creation result** (`OnCreateAccountComplete`): the CreateAccountTitle message box.

**HIGH:**
- the ease curve forms of `findInterpValue` (structure from the AS; the folded exponent forms are standard power /
  back easing);
- full-screen movies letterbox over black (the engine's movie player); the startup movie stays up until the logo
  chain starts (it plays until the front-end map is loaded in the original).

**PC ADAPTATION:** a newly created local account is signed in when none is (offline there is no login service), so the
typed name is the player name at once.

**GL state contract (for Integration / Rendering):** the UI pass (`GfxRendererGL::begin .. end`) may change any GL
state inside; on return every enable flag, mask, blend func / equation, viewport, clear value, program, VAO, array
buffer, active texture + 2D binding and draw / read framebuffer equals what the caller had. Renderers must still not
depend on UI-pass state; the frame order is world → frontend scene → UI pass → present.

## FRONTEND PASS 4: HUMAN-PLAYTEST CORRECTNESS (2026-10-04, agents/frontend, based on integration/milestone-06)
Full detail: `docs/FRONTEND.md` §1, §3, §8, §9, §13-§17. Each playtest finding was traced to the original script, native
code or authored data before anything changed.

**CONFIRMED ORIGINAL:**
- **Intro chain on every launch.** HasWatchedIntroMovie is one zero-initialised native global (0x83757450), written
  only by SetHasWatchedIntroMovie and the controller-assignment tick, never saved. The rebuild had persisted it; it is
  now a session flag (the player's "cinematic did not replay" was the rebuild's deviation).
- **Extras Movies / Credits.** `_global.MovieStarted` removes the menu's input; `Game.PlayMovie` plays a full-screen
  movie; EndMovieMode → `InvokeOnCurrentUIConditional(FrontEndUI, "_global.MovieEnded")` gives it back. The rebuild
  never played the movie or called MovieEnded (the soft-lock).
- **Profile defaults.** All 74 `Default__TnProfileSettings` fields (A1..D5Difficulty = -1: the unlockable movies are
  locked on a fresh profile).
- **Match entry.** `UseInGameLobby = !PRI.HasSelectedCharacter()`; `WaitingOnGameStart.OnBeginGame` only leaves for
  InGame when !_InGameLobby; OnCharacterSelected after the start is a notification; the spawn's OnRespawn enters
  InGame; every TnUIController EndState (screens close with their state, the HUD hides when InGame ends).
- **Return routing.** TnQuitMessageBox (Quit Game? Yes / No, "Quitting...") then `QuitGame(0)`: game lobby / private
  match → party lobby; party lobby → front end. Exit Game asks (Continue / Cancel). The message box is TnUIController
  ShowPopupUI: MessagePrompt_GFX, `_global.DisplayMessage`, `MessageBox.OnA..OnY`.
- **Create a Character.** TnCharacterScriptBinding as decompiled: ChassisTypes[FactionFilter], melee inserted at weapon
  index 2, palette-swatch colours (GetPixelColor on the palette textures, `0xRRGGBB;palette;x;y`), CommitCharacter /
  ResetCharacter / ClearCharacter rules.
- **Text entry path.** PC names go through DisplayTextPrompt → TextPrompt_GFX's input TextField (Selection.setFocus,
  Enter / Escape listener, callback); `GetAccountNames` is a comma-separated string.
- **Controls.** Mouse/Keyboard Layout is a read-only reference card: `GetKeyDescription` = MapInputKeyForController +
  `TnPlayerInput.KeyDescriptions` (Xe-TransInput.ini + TransGame.int, per-form overrides). **No key rebinding exists
  in the shipped menus.**

**HIGH:**
- the full-screen movie takes all menu input while it plays (UI event 12 hides the UI);
- the waiting-state sender of OnRespawn for the first spawn (PlayerWaitingSpectating / WatchingMatinee EndState);
- Flash input-field behaviour (caret, editing keys, click focus) as Flash Player 8;
- TextField bounds measured on read (the lobby ticker spaces messages by `_width` right after `htmlText`).

**PC ADAPTATION:**
- Accounts: local account names (create / delete / sign in / out; signed-in name = player name) - the original's
  Demonware accounts are SERVICE DEPENDENT;
- the custom-character file `wfc_characters.ini` (the original's WriteCustomizationFile location is native);
- undefined ExternalInterface arguments reach handlers as "" (UnrealScript string parameters); an unset weapon slot
  ("undefined" inside the movie's array) keeps the stored weapon.

**PARTIAL:**
- the preview pawn: drawn through Rendering's setFrontendSceneDraw at the authored PreviewGuy spawn points (detected,
  verified in a merge preview); bind pose, no ground snap, no form switch, default lobby camera (the per-chassis
  SeqVar_TnCustomizationCameraId camera is the next frontend item);
- the selected body in the match (Gameplay's pawn is still Optimus);
- the frontend scene needs the UI render data built per worktree (without it: black, not the malformed raw level).

**SERVICE DEPENDENT:** Demonware accounts, leaderboards / challenge progress, friends, matchmaking (unchanged).

**UNKNOWN / PROVISIONAL:** `ColorToHexColor` digit case (upper used); where the PC SKU stored the customization file.

**PC EXTENSION / FUTURE:** key rebinding, FOV, refresh rate, mouse sensitivity, quality presets - not added.

## FRONTEND PASS 3: PC SKU, LIVE SCENES, LOADING, MOVIE AUDIO, HUD, SETTINGS, CHARACTER SELECTION (2026-10-04, agents/frontend)
Full detail: `docs/FRONTEND.md`. This pass supersedes the pass-2 entries on Bink audio, black backgrounds, the frozen
loading screen and the opaque pause backdrop.

**CONFIRMED ORIGINAL — authored data, movies and script, running unmodified:**
- **PC SKU behaviour** (the movies' `WIN` branches):
  - no Press START gate; `mc_menuMainPC` with Accounts and Exit Game;
  - clickable footer buttons; the authored mouse cursor (`TnUIController.MouseCursorUI`);
  - PC graphics settings (resolution / fullscreen / texture quality / VSync).
- **Menu key map** = `GFxUI.KeyMap` (A Enter, B Escape, Start F3, Back F4, LB/RB PgUp/PgDn, LT/RT Home/End).
- **Start screen:** `ShouldShowStartScreen` is true only until Press START (`ShowDeviceSelectionUI`).
- **`ProfileIsReady` callback** gates Campaign / Escalation / Settings.
- **Frontend scenes:**
  - which levels sit under each screen (UI_FrontEnd_m + capture_VIG, UI_CharacterCustomization_m);
  - their matinees, remote events (`StartFireworks`) and cameras (CameraActor_6585 FOV 45, Orbiter 393.55 s;
    CameraActor_2082 FOV 70).
- **Profile settings:** fields and defaults (FX / Dialogue / Music 80, Subtitles off, Vibration on, Scheme A, Invert ×4
  off, Sensitivity 30, Gamma 50).
- **XP:** the `LevelTable` (500 … 355000) and the offline XP = 0 behaviour.
- **Characters:**
  - specialty providers;
  - the four `*_PCD_MP` presets as the fresh profile's characters; slot unlocks at 5 / 10;
  - the selection flow (WaitingOnGameStart → Choose Character → SelectCharacter → pre-game).
- **HUD (Hud_GFX):**
  - TnHUD ownership and A8 visibility;
  - TnDeathMessage templates and colours;
  - ShowScores scoreboard with focus;
  - EndGameStats data bindings.
- **Challenges:** 121 authored providers.
- **Movie audio:** the intro movies' own soundtracks.

**HIGH:**
- the matinee evaluation (UE3 interp curves, RelativeToInitial, hard attachment);
- the Flash 8 runtime additions:
  - mouse button semantics, `TextField.variable`;
  - parent-before-children frame-script order;
  - blend modes over the backdrop;
- the movie track layout (front bed / surrounds / LFE / five language centres);
- the loading-screen behaviour (presents through the load, closes when the world is loaded).

**PC ADAPTATION:**
- the default `WIN` identity (`WFC_PLATFORM=XBOX360` for the console presentation);
- Space as Start on the console presentation; Tab as ShowScores;
- borderless fullscreen (pass 3; superseded: fullscreen now switches the monitor to the saved resolution);
- `wfc_input.ini` binding overrides;
- the local identity fallback (`[Identity]` / `WFC_PLAYERNAME` / "Player").

**PARTIAL:**
- **Scene presentation in this tree:** interim legacy shading. Rendering's full path is integrated via detection
  (merge preview verified).
- **Movie audio:** language track 5 = INT.
- **Blend modes:** per item, not group-composited.
- **Settings without runtime owners:** volumes, camera, gamma, texture quality.
- **Character customization edits:** not persisted.
- **Campaign / Escalation:** the menus work; the content is out of scope; ESC maps are not exported.
- **Loading:** one 1.9–2.1 s step (the world.glb parse) still blocks presentation.

**PROVISIONAL:**
- `SetWeaponCrosshair(2)` for the IonBlaster;
- intro skip on A / Start / B.

**SERVICE DEPENDENT (empty, never fabricated):** Accounts (Demonware), Leaderboards / challenge progress (stats
archive), Friends, Find Match.

**UNKNOWN:**
- the Bink language-track selection;
- the native crosshair mapping;
- the provider column header strings;
- the region / `$version` digits.

**PC EXTENSION / FUTURE:** FOV, refresh rate, rebinding UI, mouse sensitivity, quality presets. None of them are in the
original menus, and none have been added to them.

## FRONTEND PASS 2 — THE SHIPPED SCALEFORM MOVIES RUN THE FLOW (2026-10-03, agents/frontend)
Full tables: `docs/FRONTEND.md`. This pass supersedes the pass-1 PARTIAL "no GFx presentation".

**CONFIRMED ORIGINAL — content and behaviour from the cooked movies, which run their own AS2:**
- Movies: MovieLoader, FrontEnd (title, Press START, mc_menuMain360), PartyLobby (menus, Private Match game modes,
  Host Options), GameLobby (map thumbnail, Start Game, countdown), LoadScreen, PauseMenu.
- Shared libraries: SharedComponents, SharedIcons, ButtonIcons, PlayerList.
- Fonts: Fonts_EFIGS outlines through the `GFxUI.int [Fonts]` aliases.
- Text: `$UIText` / TransGame localization.
- Logo: ExternalTextures `UI_TransformersWFCLogo_p`.
- Map thumbnail: `UI_LevelThumbnails_p.MP_Streets` through `Self.SetExternalTextureWithPath`.
- Host options: `TnOnlineGameSettings*` LocalizedSettingsMappings / PropertyMappings with their authored defaults
  (authored.db; e.g. TDM 15 minutes / 40 points / Host's Choice / Autobalanced). The chosen values feed the match URL.
- Key codes: the movies' KeyListener contract (13 / 27 / 112–117 / 33–36 / arrows); `$version` "XBOX360".
- Return path: PauseMenu "Quit Game" → `Game.QuitToMainMenu` → `UI_FrontEnd_m`.

**HIGH:**
- The AS2 / GFx runtime is clean-room. Where Flash semantics were ambiguous, the choices were:
  - a plain call's `this` is the calling timeline;
  - a function's `_parent` is its defining timeline;
  - `var x;` keeps an existing value;
  - unnamed clips are named `instanceN`;
  - attachMovie resolves through imported libraries.
- Gamepad → key-code mapping.
- Per-class persistence of setting values.

**PARTIAL:**
- Bink movies: video plays (logos, FMV_intro, TF_InitialStartup / TF_LoadingScreen underlay, decoded from the
  AssetTools .mkv); movie audio is not played (track layout unidentified); the skip rule is PROVISIONAL.
- The world load blocks after the loading intro (34 frames).
- Filters and blend modes are not drawn (pause backdrop).
- Player data: no profile name or XP ("Player", level 1).
- GetDataStoreFields order.
- The GL release on travel is a census stopgap (Rendering handoff).

**PROVISIONAL:** match-start UI events fire at once (no Gameplay PendingMatch).

**UNKNOWN:**
- the UI_FrontEnd_m 3D scene (not exported; menus sit on black);
- provider column header strings;
- region / `$version` digits;
- the observed TDM tips (authored 26 × the same tip).

**Audio:** Systems' FrontendAudioRuntime drives UI cues, the UI levels' Kismet music / beds (FRONTEND_MX_ORBIT_01,
MP_PARTY_LOBBY_MX, MP_LOBBY_MX), CINE_MUTE_FOR_BINK and prefetch through the IFrontendAudio seam. This was verified in a
read-only merge preview with agents/systems de19ced; on this branch alone the audio calls are traced only.

## FRONTEND PASS 1 — APPLICATION FLOW: BOOT → FRONTEND → LOBBIES → MATCH → RETURN (2026-10-03, agents/frontend)
Sources: RE MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md (f1ca8a5), RE binding decomp (TnOnlineActionScriptBinding),
AssetTools FRONTEND.md + manifests/frontend_*.json (cc9773e). Full table: `docs/FRONTEND.md`.

**CONFIRMED ORIGINAL (script / config / authored data):**
- **Boot:** `[URL] Map=UI_FrontEnd_m`.
- **Frontend Kismet order:** Black-Out (0.00105 s) → MovieLoader_GFX → `enterMovieSequence` (Logo_Activision →
  Logo_Hasbro → Logo_HighMoon → FMV_intro) or `enterFrontEnd` → [FRONTEND START] → SetLoadingMovieFilename →
  OnUIEvent(0) → `FrontEnd_GFX_1`.
- **TnUIController:** states, OnUIEvent dispatch and per-subclass movies.
- **Bridge names and bodies:** OpenPartyLobby, StringToGameTeamStatus (1/2/3/4, default 3), EditGameMode,
  PlayPrivateGame, SetSelectedMapID, BeginLobbyExitCountdown → HostRequestsGameStart, QuitToMainMenu.
- **URLs:**
  - party lobby URL;
  - BuildLobbyURL;
  - match URL. It equals RE 3.1 character for character; the native-appended properties are HIGH.
- **Game lobby:** private = host's choice; SelectRandomMap over compatible, enabled providers; short countdown 10 s.
- **Loading text:** StartLoadingMovie kinds; SetLevelText(FriendlyName, "in <map>"); 3 × EngageText.
- **Pause:** Escape / Start = `|onrelease showmenu` → OnUIEvent(6).
- **Catalog:** map / playlist / settings catalog read at runtime from the manifests. 13 providers in ini order,
  9 playlists in manager order.

**HIGH:**
- the MovieLoader branch (a native shim until the AS2 runtime runs the movie);
- menu order = ini order;
- PickTeam with no reservation = RandomInt(2);
- map directory = MapFilename without `_Base_m`;
- bPauseable false: the MP world keeps running while paused.

**PARTIAL:**
- Bink movies are not decoded: each logo / FMV reports Stopped at once.
- **Presentation (superseded by pass 2):** no GFx movie was drawn yet.
- **Mapping:** the EditGameMode / PlayPrivateGame argument → settings class (GameSettingsCfgList not manifested).
- **Not shown:** online warning prompts.
- **Match URL:** AppendContextsToURL is omitted.
- **Return path:** the match world is released by recreating renderer / audio; the old GL objects leak (Rendering
  handoff).

**PROVISIONAL:**
- Match-start UI events (OnCharacterSelected → OnUIEvent(3)) fire immediately after load. Gameplay has no
  PendingMatch, character select or countdown yet (handoff).

**UNKNOWN:**
- the UI_FrontEnd_m 3D scene (not exported);
- the observed TDM loading tips: the shipped TransGame.int authors the same tip 26 times;
- the Bink / GFx loading composite and close timing.







## SYSTEMS M08i - ABILITY / BUFF SOUNDS (2026-10-06, agents/systems)
* **OnTriggerSound** [CONF RE pass 5 s12]:
  * TnAbility.ServerTriggerAbility plays it after the trigger succeeds: OwnerPawn.PlaySound, replicated to everyone including the owner, positional.
  * Subclasses call the base last, so their early-outs are silent. LocalTriggerAbility plays nothing.
  * A refused press is silent, except when abilities are jammed: TnPlayerController.AbilitiesJammedSound [s12 addendum].
* **TnBuff** [CONF]:
  * Apply: if CanPlaySounds, CreateAudioComponent(ApplySound), attached; re-apply doesn't restart it.
  * Unapply: stop, then PlaySound(UnapplySound, bNotReplicated).
  * Owner death: stop with no unapply.
  * CanPlaySounds = !OnlyPlaySoundOnLocalPlayer || the local player's pawn. The default is True; only TnBuffCloak sets False.
  * Cloak cues are chosen by the buffed pawn's team.
* **Drain** [CONF; per-frame tick HIGH]:
  * TnBuffDrainSource.HealSound: every tick on the drainer's machine while it has ≥1 target.
  * TnBuffDrainTarget.DamageSound: every tick at the victim.
  * Both cues are MaxConcurrentPlayCount 6, KillFarthest.
* **Hover** (TnAcrobaticsManager) [CONF]: _HoverLoopSound from JumpingToHover into Hovering; a 0.5 s fade on leaving; _HoverCooldownSound when Hovering ends.
* **Kill confirm** [CONF RE s13]:
  * NotifyCausedDamage fatal → TellClientToPlayKilledPawnSound → ClientPlaySound: the killer only, 2D [HIGH].
  * Precedence: WasLastHitHeadshot > robot form > CharacterType SoldierJet > SoldierCar > Vehicle.
* **TransformFailedSound** [CONF]: PressTransform plays it (local, at the pawn) with TnBuffTransformDisruptor, or when Transform() fails (already transforming, disabled, no room).
* DeathSound is campaign-only (OnCampaignGameOver), so it isn't wired.
* **Deviations:**
  * No headshot state, so the headshot confirm is unreachable.
  * Ability animation sound notifies aren't played (Gameplay doesn't play those animations).

---

## SYSTEMS M08g - PROFILE VOLUME SLIDERS (2026-10-05, agents/systems)
* **Script** [CONF]: HmPlayerController.UpdateLocalCacheOfProfileSettings -> SetAudioGroupVolume('Dialog', GetDialogVolume()),
  ('SFX', GetFxVolume()), ('MUSIC', GetMusicVolume()); Get* = HmProfileSettings.GetNormalizedPropertyValue = FClamp(slider / 100, 0, 1).
  TnProfileSettings defaults: Music Volume (31) 80, FX Volume (32) 80, Dialogue Volume (33) 80.
* **Native** [CONF RE pass 5 §10, RE 639a66c]: exec 0x82C7E5E8 -> SetGroupVolume 0x827666B0 walks SoundGroupCategoryMappings, finds each
  listed category node and REPLACES its fader (initialised to the config Volume) with the value via SetTarget(v, 0) - immediate; FName
  match (case-insensitive), unknown group = no-op. Each category's channel group is attached to its parent's [CONF]; descendants
  inherit multiplicatively [HIGH, FMOD ChannelGroup].
* **Rebuild:** a device-global per-group scale multiplied into every cue whose category is in the group's subtree. Equivalent to the
  replace because all five listed categories' config Volume is 1.0 (gen_mixer.py asserts it).
* **Mixer presets vs. the slider** [CONF path, RE d832643 pass 5 §10 addendum; HIGH audible]: EnableMixerPreset (0x82772778) and the
  tree re-evaluation (0x8276A868) blend presets into each node's DSP preset slots and never write the group fader, so preset volume and
  the slider multiply - as applied here, on every category including SFX_DRY / DX_* / MUSIC_DRY.
* **Movies:** GetMovieVolume's 'SFX' class volume is the same group value; a running movie follows a slider change.

---

## SYSTEMS M08f — COUNTDOWN / OBJECTIVE / GRENADE AUDIO; ASYNC PREFETCH (2026-10-05, agents/systems)
* **Countdown ticks** [CONF script + CDO]:
  * TnGameReplicationInfo.OnCountdownChange: IsCountdownBelowThreshold (0 ≤ CurrentCountdown ≤ LowCountdownTickThreshold 10) → PlaySound(LowCountdownTickSound = BL_HUD_INTERFACE.CTF_ROUND_TIMER_01).
  * TnGameReplicationInfoMultiplayer.OnObjectiveCountdownChange: ≤ 5 and ≠ −1 → BL_HUD_INTERFACE.EXTINCTION_ROUND_TIMER_01.
* **KOTH** [CONF script + CDO]:
  * MatchStarting → StartIgnoringAnnouncer(AnnouncerMatchStartHysteresisTime 3.0), so the first zone's "hill moved" line is suppressed.
  * The activation's own UpdateClaim is silent (IgnoringTeamChangeAnnouncement).
* **End of match** [CONF RE MP sweep S5]:
  * a winner (normal or forfeit) gets their team's line;
  * a tie gets none;
  * FFA gets no win line.
* **Grenades** [CONF script + data]:
  * TnProjectileGrenadeBase.HitThing: the first impact plays FuseSound (the fuse starts); OnHitThing plays BounceSound on every impact.
  * HmProjectile.HitWall plays BounceSound.
  * Sound names come from the TnProjectileMesh (e.g. Magma Frag: PROJ_IMPT / GRENADE_FOLEY_SHELL_BOUNCE_HEAVY).
* **Prefetch** (rebuild performance, no behaviour change):
  * On a thread-safe backend the worker decodes and warms the device cache; a tick adopts the result (cache hits).
  * Every load / unload path drains pending decodes first, so residency and pinning match the synchronous rule.

---

## SYSTEMS M08e — PER-CHASSIS VEHICLE FX (2026-10-05, agents/systems)
* **Source** [CONF script + data]: TnVehicleFxPlayer.Play / Stop (socket-attached, Color = EnergonColor); TnCarForm Hovering / Driving.UpdateFx, UpdateBoostFx, UpdateJumping / UpdateRolling; TnTruckForm Start / StopNitro (RamFX); TnTankForm.UpdateFx; TnPlaneForm Hovering / Flying.UpdateFx; HoverPhysics.CalculateThrusterLinear / AngularContribution; TnVehicleForm.get_FxAllowed.
* **Data:** character.json `vehicle_fx` + `vehicle.sockets` (31 chassis).
* **Size** [HIGH]: computed in world space. The thruster dot / triple products are frame-invariant; the angular acceleration is taken from the world body rotation.
* **Approximations:** the vehicle rigid-body gravity is assumed to be the pawn's kGravity [HIGH]; cloaking is not wired (no Gameplay state).
* **Verification:** driving verified with a recording runtime only; the on-screen check needs Rendering's runtime merged.

---

## SYSTEMS M08d — VEHICLE / WEAPON / PROJECTILE / BEAM AUDIO BY IDENTITY (2026-10-05, agents/systems)
* **Vehicle form classes** [CONF decompiled TransGame form classes]:
  * Every form clones its blueprint's own HmPlayerVehicleAudioComponent (TnVehicleForm.Initialize).
  * Which component calls each form makes (Car / Truck / Tank / Plane) is ported in `VehicleFormAudio`.
  * The jet's hover boosters are a loop: PlayHoverFx / StopHoverFx.
  * The jet's ascend / descend are held-input Play / Stop pairs (Hovering.UpdateDashing).
  * Car Driving.UpdateRolling plays AscendSound.
  * The tank's special move is PlayOneEightySound.
  * Unknowns: BoosterAmount needs the hover sim's thruster contribution (Gameplay); the tank 180 is not modelled by Gameplay.
* **Projectiles** [CONF script HmProjectile + data TnProjectileMesh]:
  * FlightSound is attached from spawn (ClientSpawnFlightEffect).
  * Explode: FlightSound FadeOut 0.25, then PlaySound(ExplosionSound) at the projectile (SpawnExplosionEffect).
  * The launch is the weapon's WP_Fire, for every fire type.
* **Beam weapons** [CONF script TnWeaponBeam / TnWeaponRepair; HIGH EWeaponEvent indices]:
  * While firing: WP_Looping (event 9).
  * OnPlayFireEffects: a teammate → stop 2 / play 1 (WP_Fire = heal loop); an enemy → stop 1 / play 2 (WP_FireSecondary = damage loop); no target → stop both.
  * StopFireEffects: stop 1 / 2 / 9, then play 12 (WP_LoopingTail).
  * Loops use the authored LoopingFade times.
* **Weapon identity** [CONF data]: fire, impact and victim hit sounds come from the fired weapon's class (robot or vehicle weapon). A vehicle weapon is emitted at the vehicle [HIGH: PlaySound on the owner].
* **Language**:
  * A cold boot → TDM resolves 282 localized waves from `_LOC/int`, 0 skipped.
  * The native GLanguage source on PC (XGetLanguage on 360) remains `WFC_LANGUAGE` (default INT) [documented unknown: the PC OS-language mapping].

---

## SYSTEMS M08c — PLAYTEST AUDIO FIXES (2026-10-05, agents/systems)
Details are in `docs/handoff/SYSTEMS_M08C_AUDIO_HANDOFF.md`.

* **Movie preset ownership** [CONF config + HIGH]:
  * MovieMixerPreset is held while a Bink plays, whichever path started it: the caller's flag OR the movie sound.
  * The Extras script movies previously left it stuck on.
* **AudioComponent lifetime** [HIGH, UE3]:
  * An instance lives until its sound ends.
  * The 10 s tail bound now applies only to backends that cannot report voices.
* **Localized VO** [CONF RE pass4 C + data]:
  * The GLanguage `_LOC` twin is used (content/_LOC/<int|FRA>/); another language is never substituted.
  * The merged extraction had the TDM start line from `_LOC_FRA`.
* **Vehicle component** [CONF script HmVehicleAudioComponent / form classes]:
  * Ported: the SpeedSound loop (Attached / Detached), the TireTread loop, the BoosterSound loop with Stop / BoosterAmount, and the AscendStop / Descend / DescendStop / Roll / OneEighty / Enter / Exit events.
  * Detached uses fade 0.
  * The per-form input mapping is in the handoff.
  * CustomLoopingSound is never called by TransGame script: unported.

---

## MILESTONE 08 SYSTEMS — MULTI-MAP, MODE, CHARACTER / WEAPON AND MOVIE-LANGUAGE AUDIO (2026-10-05, agents/systems)

**Handoff:** `docs/handoff/SYSTEMS_M08_AUDIO_HANDOFF.md`.

### Multi-map (all 10 processed MP maps)
* **Generator:** `gen_level_audio.py` walks every MP map folder in VerticalSlice/Maps, covering each sublevel's
  Main_Sequence. It emits one runtime graph per map into `LevelAudio.inc`. There are no per-map source branches.
* **Graph sources:**
  * SeqEvent_Touch, using the AssetTools zone geometry keyed by event name;
  * SeqAct_AmbientAudioZone, Delay and Gate;
  * GameplayStarted;
  * remote events;
  * gameplay-owned events as `Game:<class>:<output>`.
* **Graph sinks:** PlaySound, PlayerPositional, Flyby, Reverb, Mixer, Play/StopMusic.
* **Pruning:** ops that cannot reach a sound are dropped.
* **Semantics** [CONF native A1, M06]:
  * zone enter makes that zone current globally; a scene input sets the next scene, and the tick fires Ended / Begun;
  * Reset clears without output;
  * Touch is a pawn-edge trigger with MaxTriggerCount / ReTriggerDelay, and death untouches;
  * Delay, Gate and the Mixer preset enable / disable follow the native implementation.
* **Flyby** [PARTIAL]: native trigger, PROVISIONAL motion (start at a distance, pass through the head point).
* **Mixer:** map presets with several categories (e.g. MP_COMPLEX_WATERFALL_DUCK, the GLB_Audio_m radio presets)
  come from the authored rows [CONF data].
* **Validation:** Streets' graph matches the M06 hand-flattened zones over a 60-step walk and 120 s pool dwells. All
  10 maps load, run their bed / zones / reverb, survive death and reset, and unload to baseline (suite, 3 × 10
  cycles).

### Mode audio [CONF script + class defaults]
* Each mode's messages are ported switch-for-switch from the decompiled scripts:
  * TnFlagMessage.GetColoredString: the stinger via PC.PlaySound, plus the announcer line;
  * TnBombMessage: team-specific pickup / detonate lines;
  * TnDominationMessage: point × 10 + type, for points A–E;
  * TnCTFMessage: attacker / defender line;
  * TnRoundBasedGameMessage: time-up and switching-sides lines, plus music (FadeIn / FadeOut 0, Priority 0);
  * TnKingOfTheHillZoneBase: zone change and the defender-changed lines; suppressed when IgnoringAnnouncer, when
    the match is over, or on the first claim.
* The cue names come from the class defaults, written to the shared `__match_messages__` manifest.
* Gameplay drives every one of these; Systems keeps no timers.

### Character / weapon audio
* **Generator:** `gen_character_audio.py` combines the roster chassis, mp_weapons and authored.db into
  `CharacterAudio.inc`: 33 profiles, 53 weapons, 575 cues.
* **What a profile carries:**
  * the robot and vehicle SoundEventSets;
  * the vehicle death sound;
  * every robot clip's sound notifies (AnimNotify_Footstep → FS event, HmAnimNotify_SoundEvent / _Sound; later
    anim sets override earlier ones);
  * the loadout.
* **Optimus equivalence:** the default profile (Truck) reproduces the old hand-made Optimus tables exactly: 26/26
  notifies, landing / take-off / idle, and the vehicle and transform cues.
* **Wired to the profile:** RobotFoley, VehicleAudio, the transform sound, and weapon fire / tail / fine aim.
* **Impacts** [CONF script + data]:
  * **World hit:** HmWeaponMesh.CreateImpactEffects → TnWeaponMesh.GetImpactSound tries, in order:
    1. the surface's weapon-type sound — no physical material authors WeaponTypeSpecificImpactSounds;
    2. PhysMaterial.ImpactSound — only special surfaces such as ForceField and destructibles; per-surface lookup is
       not done here [PARTIAL];
    3. the weapon mesh's DefaultImpactSound, played at the hit point.
  * **Pawn hit:** Transformers have AllowHitEffects false [HIGH: only Vehicle / MatineePawn / SentryPawn set it], so
    the weapon's impact sound does not play. Instead, the victim's TnHitEffectPlayer (SharedHitEffectPlayer, 55
    entries) does the following:
    * picks the entry by DamageType — exact match, then the first parent class (FindEffect);
    * plays its HitSound as an event in the victim's own SoundEventSet (IMPT_DMG_<weapon>);
    * only if the damage type has bCausesBlood;
    * at most once per RetriggerTime (0.1 s) per victim per entry.
  * The generator resolves this rule for 13 hitscan weapons. Shotgun and CarMachineGun have no entry anywhere in
    their class chain, so they play no hit sound, as in the original.
  * The rebuild's damage targets are stand-ins, so they use the default profile as the victim [PROV].
  * Missing data (AssetTools): projectile and melee weapons have no damage types in mp_weapons.
* **Weapon-mesh animation sounds** [CONF data]:
  * Source: the HmAnimNotify_Sound notifies on each weapon's own AnimSet, reached via WEPMESH → AnimatedMesh →
    SkeletalMeshComponent.AnimSets. 43 of 53 weapons have one.
  * The WeaponEventAnims (WP_Fire / WP_Reload / WP_Equip / WP_PutDown) and the IdleAnimation sequence
    (`<Seq>Group` → `<Seq>` [HIGH name rule]).
  * The Ion Blaster's generated table equals the M03 hand-checked one, and a lockstep run plays the same 28 reload
    cues in the same order as before.
  * `WeaponSoundTimeline` follows the held weapon class and uses WeaponMesh's rules: an event anim replaces the
    current clip, plays once, then returns to the looping idle.
  * The visual WeaponMesh stays the Ion Blaster's; only its effect notifies are used.
  * Equip / put-down sounds play when Gameplay calls `World::weaponAnimEvent`.
* **Vehicle-form audio per chassis** [CONF data + script]:
  * Source: each chassis's own HmPlayerVehicleAudioComponent (roster `vehicle.definition`), merged over its
    archetype chain and the class defaults. All 33 chassis have one.
  * Slots: drive gears (MaxSpeed, on/off-load loops and one-shots), reverse, boost and jump-rev loops and one-shots,
    UseJumpRev, the land tables, and the single slots (boost, boost wheels, boost stop, ascend, ram, booster, nitro,
    tire squeal).
  * Tunables: fades, squeal speed, wheels delay, jump-rev time, one-shot spaz time, speed-history length.
  * `VehicleAudio` ports HmPlayerVehicleAudioComponentImpl onto that data:
    * ComputeGear runs at BeginState only;
    * one-shots are gated by EngineOneshotSpazTimer, and skipped when coming from Boosting or JumpReving;
    * an unset slot plays nothing.
  * Optimus is unchanged: the A/B probe `tools/systems/vehicle_audio_ab.cpp` shows the same 92 voice starts / stops
    as the previous hand-entered port on a deterministic drive script.
  * Megatron has no ram, boost-wheels or jump-rev sound, and squeals from 3 mph. Starscream uses its hover-ascend
    and hover-booster slots.
  * Not ported: SpeedSound, CustomLoopingSound (ram alert, turret rotate) and OneEightySound. They were not driven
    before either.
* **Weapon FX by template** [CONF data]:
  * Each weapon class's WEPMESH WP_Fire muzzle and tracer templates and its DefaultSquib are generated per class.
  * World spawns the held class's templates. WeaponFx draws only the three it reconstructs, all shared with the Ion
    Blaster:
    * MuzzleFlash_AssaultRifle_FX and Tracer_AssaultRifle_FX — Assault Rifle (plus its plane and vehicle
      variants), Heavy Pistol and Plane Machine Gun get both;
    * Tracer_AssaultRifle_FX only — Burst Rifle and Heavy MG;
    * Impact_IonBlaster_FX — the Ion Blaster only.
  * Any other template draws nothing; another weapon's FX is never substituted. Each missing template is logged
    once (Integration M08 decision); the weapon's sounds still play [PARTIAL].
  * Unreconstructed templates go through Rendering's generic WfcMapFx runtime (Integration decision; API in
    agents/rendering 38c9ecf). `WeaponFx::setGenericRuntime` takes three callbacks, bound by the host to
    IRenderer::spawnParticleEffect, spawnParticleEffectSegment and setParticleEffectTransform:
    * muzzle: socket position, X forward / Z up, and it follows the socket for 2 s;
    * impact: +X = the surface normal;
    * tracer: start → end.
  * A -1 return or no binding: logged once, nothing drawn. `tools/systems/weaponfx_generic_probe.cpp` checks all of
    this against a recording fake.
  * Colour: none is passed, which is correct. The runtime (agents/rendering M34) resolves component
    InstanceParameter → caller colour → the template's own decoded ColorByParameter DefaultColor → white.
    * The results match the reconstruction and the editor thumbnails: AssaultRifle (51,25,255) = the Ion blue,
      EMP shotgun (255,65,65), Sniper (255,12,12).
    * Confidence: HIGH structural decode; MEDIUM for the single-candidate stream layouts (Rendering).
  * PARTIAL: the runtime does not draw Trail2 ribbons yet. Other weapons' tracers show only their sprite emitters,
    and the Ion tracer stays hand-made.
  * No hand-ports.
* **PARTIAL:**
  * 169 dialogue waves are absent from the extraction (AssetTools).
* `SoundCues::findCue` resolves full asset names to the compiled short names, but only for the exact packages that
  were compiled under a short name.

### Movie audio (RE 433ef9e + follow-up, CONFIRMED native)
* **Tracks:** BinkSetSoundTrack([0, 1, 2, 3, 4, 5 + L]). L comes from GLanguage: FRA 1, ITA 2, DEU 3, ESN 4, RUS 5,
  POL 6; any other language 0. The M07 UNKNOWN is closed: INT is track 5.
  * `audio::movieLanguageSlot`; `WFC_LANGUAGE`.
  * A track index the file does not have plays nothing.
* **Routing** (0x8369A780): 0 FL, 1 FR, 2 SL, 3 SR, 4 LFE, the language track C. This matches the M07 data analysis.
* **Volume** (Function_82CCA028):
  * every track is set to Volume × 65536;
  * the MoviesToAlwaysPlaySound logos use 0xCCCC = 0.8;
  * the other movies use GetMovieVolume (0x82CDDC08) [CONF]: [MoviePlayer] VolumeScalar (absent → 1.0) × the
    device's 'SFX' class volume (the FX Volume option), clamped to [0,1]. FullVolumeMovies is empty.
    The class volume is FX slider / 100, linear [CONF script: UpdateLocalCacheOfProfileSettings →
    SetAudioGroupVolume('SFX', GetFxVolume())]. That the device's 'SFX' lookup returns it unchanged is HIGH.
    `setMovieFxSlider(slider)` / `setMovieSfxVolume(v)`; the default is the profile default 80 → 0.8.
* **Still PROVISIONAL:** the stereo fold-down matrix and the Master-relative level.

### Lifecycle (real device)
* `tools/systems/lifecycle_probe.cpp` runs 40 cycles of: frontend (logo started and skipped, title, party lobby,
  game lobby) → map N (all 10 in rotation, a different character profile each cycle). Each match covers start,
  progress, final stretch, a flag message, death, round reset and end, then returns to the frontend.
* Every cycle returns to: 0 voices, 0 streams, 0 queued events, 0 level cues, the base cue table (63) and mixer
  presets (4), no music, an idle announcer and empty reverb.
* Decoded PCM after each match is ≤ its pre-match value and never exceeds cycle 0 (61.4 → 52.4 MB).
* The peak is ~96 voices (Rust / Seed / Complex beds) and ≤ 246 MB of PCM while a map is up.

---

## MILESTONE 07 SYSTEMS — MOVIE AUDIO, MATCH / ANNOUNCER AUDIO, LIFECYCLE RE-VALIDATION (2026-10-04, agents/systems)

**Trigger:** human playtest — the boot movies (logos, FMV_intro) show, but no sound plays.
**Handoff for Frontend / Gameplay / Integration:** `docs/handoff/SYSTEMS_M07_AUDIO_HANDOFF.md` plus two patches against
integration/milestone-05: `SYSTEMS_M07_frontend_movie_audio.patch` (~25 lines) and
`SYSTEMS_M07_gameplay_match_audio.patch` (~20 lines). Both were verified on a merge preview of this branch onto
integration/milestone-05.

### Boot-movie audio
* **Cause:**
  * The Frontend movie player (Win32Movie.cpp) selects only the video stream.
  * The frontend correctly enables the MovieMixerPreset (CINE_MUTE_FOR_BINK) on the game mix while a Bink is up.
  * Nothing played the movie's own sound.
* **What the sound is:**
  * **The Bink audio tracks** [CONF data] (`tools/systems/bink_tracks.py` on the original `.bik` headers):
    * logos / FMV_intro / campaign FMVs: 10 mono 48 kHz DCT tracks, IDs 0–9;
    * chapter text movies: 6 tracks; credits: 2;
    * the loading / startup Binks: none.
  * **No cue** [CONF data]: no SoundCue exists for the boot movies. The UI_FrontEnd_m Kismet links the movie ops
    only to the next movie and [FRONTEND START].
* **Layout:**
  * **Track 4 = LFE** [CONF data]: 87–100 % of its energy is below 120 Hz.
  * **Tracks 0/1 a front pair, 2/3 the surround pair** [HIGH]: standard order assumed.
  * **Tracks 5–9 = per-language centre channels** [CONF data]: identical in the dialogue-free logos, mutually
    uncorrelated (r ≈ 0) in the dialogue movies. The 6-track text movies have the same 0–4 plus one centre.
* **Which centre is INT: UNKNOWN.**
  * The subtitle-timing and voice-spectrum tests are inconclusive (`movie_lang_*.py`).
  * The native `HmPlayerController.MovieAudioSetup` and the Bink library calls are not traced; the native-table
    entries are filled at run time.
  * Track 5 is used [PROVISIONAL]; the logos are exact. `WFC_MOVIE_LANGSLOT` overrides it.
* **Implemented (Systems):**
  * **`audio::MovieAudioPlayer` (Win32MovieAudio.cpp):**
    * Media Foundation is loaded at run time (no link dependency); every audio stream is decoded on its own thread
      (~0.5 s ahead).
    * Stereo fold-down: L = FL + 0.707 C + 0.707 SL, R likewise, LFE dropped [PROVISIONAL platform matrix].
  * **`IAudio` PCM streams:** mixed after the game mix's Master chain, so CINE_MUTE does not mute the movie
    [HIGH: the Bink player outputs beside FMOD; the preset mutes the game mix]. Level = full scale at the rebuild's
    Master calibration [PROV].
  * **Controls:** `startMovieAudio` / `stopMovieAudio` / `movieAudioClock` on LevelAudioHost, FrontendAudioRuntime
    and World.
* **Mixer:** `[HM_Engine.SoundMixerProperties] UnflushableMixerPresets=CINE_MUTE_FOR_BINK` [CONF config]. A level
  change no longer drops the movie mute; M06 had assumed it did, and this is corrected.
* **`MoviesToAlwaysPlaySound`** (the three logos) [CONF config]: every movie plays its sound; the list concerns the
  console's own music overriding movie sound, which is not modelled.
* **Validation:**
  * **`tools/systems/movie_audio_probe.cpp`** (real device, the full seam):
    * each logo's sound plays (−9 … −13 dB) with the game mix at −96 dB under it;
    * the audio clock is within 70 ms of wall time over 13 s;
    * a skip stops the sound at once;
    * after the chain the game mix and the title music return;
    * the lobbies change music with no overlap;
    * the loading Bink is silent;
    * the movie preset survives a level change;
    * repeated chains leave 0 streams, 0 voices and PCM at its base.
  * **Real executable** (merge preview + patch, cold boot, WFC_FRONTEND_SCRIPT):
    * `movie.audio playing:true` for Logo_Activision, Logo_Hasbro, Logo_HighMoon and FMV_intro;
    * the title music starts at [FRONTEND START] after FMV_intro;
    * no warnings.

### Match / announcer audio (MatchAudio)
* **Recovered** [CONF script: TnAnnouncer, TnGameTypeMessage, TnGameTypeMessageDM.GetEndGameMusic,
  TnGameProgressAnnouncementMessage, TnGameRules.Handle*, TnGameRules_ReportGameProgress*; CONF data: class
  defaults, TnOnlineGameSettings<mode>.Rules, TnWorldInfo.AnnouncerSoundEventSet]:
  * **Start** (switch 0): GameTypeDialog, then GameDescriptionDialog (announcer queue) and GameTypeMusic.
  * **Nearly complete** (switch 1): GameNearlyCompleteMusic.
  * **End** (switch 2): end music at priority 1, by winning team (DM: local winner → AutobotsWin; no winner →
    Tie).
  * **Progress lines:** 30 s / 1 min / 2 min, 25 / 50 points, 1 / 3 / 5 kills.
  * **Announcer:** Team0 OPRIME / Team1 MGTRON voice; one-slot priority queue.
  * **Mode → message class:** TDM / DM / CTF / KOTH / DOM / EXT.
* **TnVersusGameOverMessage** win lines (MP_GameAutobotWin / MP_GameDecepticonWin): PARTIAL (TransContent script
  not decompiled).
* **Data:**
  * The map's announcer set (CHR_ANNOUNCER_DIALOG, 140 events) and the 157 dialogue / mode-music cues are in the
    Streets and Gorge Systems manifests, all streamed (decoded on first play): Streets' resident PCM is unchanged.
  * The announcer cues pan to PanCenter. The stereo fold-down now includes the centre at −3 dB [HIGH]; no compiled or
    map-bed cue sets PanCenter, so nothing else changes.
* **Not authored, not added:** kill feed, kill-streak / score text, kill awards. The HUD movie's own Sound.PlaySound
  names go to `World::playUiSound`.
* **Validation:**
  * **Suite:** announcer voice selection (only the OPRIME / MGTRON wave), queue, music priority and SpazTime, unknown
    events ignored, every mode's message class, 10 × (Streets → Gorge → frontend) with match messages back to the
    baseline.
  * **Real executable** (6 frontend → lobby → Streets TDM → quit cycles): every match plays the TDM line
    (Optimus) → the queued description and DM_START. Every unload is back to 0 instances / 0 voices / 36.5 MB;
    every load is 189 level cues / 97.2 MB.

### Second map
* MP_UND_Gorge (AssetTools audio.json + the Systems manifest) loads through the generic path: 15 emitters, 23 zones,
  6 reverb presets, 13 bank cues + announcer. Its bed plays, and it unloads to the baseline. No Streets branch was
  added.

### Loading "frozen"
* World loading blocks the game thread (~4.8 s). Audio does not need it:
  * the mixer and the movie decoders have their own threads;
  * the loading Binks author no sound;
  * the game mix is muted by CINE_MUTE during the loading movie.
* The frozen picture is presentation (Frontend / Integration); see the handoff for the audio requirements.

### Classification
* **CONFIRMED ORIGINAL:**
  * the movie sound is the Bink audio tracks (10 / 6 / 2 / 0 per movie); LFE track 4;
  * MovieMixerPreset and UnflushableMixerPresets; MoviesToAlwaysPlaySound;
  * the TnAnnouncer / TnGameTypeMessage / progress message script and data;
  * the announcer set; the mode → message map.
* **HIGH:**
  * the surround pair order; per-language centres;
  * the movie stream outside the game mix;
  * the DialogCharacter event filter; the centre fold-down.
* **PARTIAL:**
  * which centre is INT (track 5);
  * the stereo fold-down matrix and the movie level;
  * the TnVersusGameOverMessage win line;
  * synchronous level-bank decode on load.
* **UNKNOWN:**
  * `MovieAudioSetup` / `MovieAudioShutdown` natives (Bink track selection, possible movie volume);
  * the FRONT_END mixer preset activator.

### Test totals
* Suite **566 / 0**.
* wfc_fidelity 194 / 0 / 19.
* Streets in game unchanged (50 / 70 emitters, DEC_ROOM_LOWER, 97.2 MB).

---

## MILESTONE 06 SYSTEMS — FRONTEND / LOADING / LEVEL AUDIO LIFECYCLE (2026-10-03, agents/systems)

**Goal:** Systems provides the original audio for boot → frontend → lobby → loading → match → match reset → leave →
frontend / lobby → another map, from authored data, with one generic level path.

**Sources (read only):**
* RE-Workspace `notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md` (frontend Kismet order, travel timeline,
  MovieMixerPreset) and the decompiled script: `Engine.PlayerController.Kismet_ClientPlaySound /
  Kismet_ClientStopSound`, `HM_Engine.SeqAct_PlayPlayerPositionalSound`, `HmMusicPlayer`,
  `HmAudioCategoryEffectsManager`, `TnPlayerController.OnCampaignGameOver`.
* AssetTools `frontend_audio.json`, `frontend_re_handoff.json`, and `authored.db`: the UI levels' Kismet, cues,
  actors and SoundMixerProperties.
* The Frontend lane's audio seam `frontend::IFrontendAudio` (agents/frontend 9e62074, read via git).

### Level manifests (generic; no level-specific code)
* **`tools/systems/gen_level_audio.py` → `src/game/LevelAudio.inc`:** one JSON manifest per level, compiled in.
  * **UI levels (UI_FrontEnd_m, UI_PartyLobby_m, UI_Lobby_m, UI_CampaignLobby_m):** every Kismet audio op (SeqAct_PlaySound,
    SeqAct_PlayPlayerPositionalSound, SeqAct_Reverb, SeqAct_PlayMusic / StopMusic, and SeqAct_Interp timelines whose event track drives audio)
    with its authored fields and class defaults; the op's triggers (sub-sequence Start / Stop flattened);
    Target / Source Actor positions; the cue trees; the reverb presets (SoundMixerProperties).
  * **Every level:** the per-cue-asset concurrency limits (MaxConcurrentPlayCount / InstanceLimiting).
* **`AmbientAudio::load` merges the two manifests:** the AssetTools map manifest (`<assets>/Maps/<level>/audio.json`: emitters, zones, pools,
  presets, bank) and the Systems manifest. Either may be missing; both unload through the same path.
  * UI_FrontEnd_m: 21 ops, 35 links, 18 cues, 1 reverb preset, 13 actors.
  * Each lobby: 4 ops (music, bed, 2 pools).
* **Removed compiled Streets data:** `CookedCueLimits.inc` / `gen_cue_limits.py` (the two Streets cue limits). Map cue limits now
  come from the level manifest; verified identical (EMIT_FLOOD_LIGHTS 3, EMIT_MONRAIL_IDLE_LP 3, the other 30 = the
  Engine default 5 / kKillFarthest).
* **Master compressor:** now global data (SoundMixer.inc from SoundMixerProperties Master Default: −6 dB / 10 ms /
  50 ms, unchanged values) instead of being read from the Streets manifest.
* **Footsteps:** a query of all 8 797 cooked cues' packages finds no surface audio anywhere, so FS_DEFAULT_* (per-pawn
  SoundEventSet) is the authored behaviour for every map, not a Streets assumption [CONF data, game-wide]:
  * no PhysicalMaterialPropertyBase subclass instance;
  * no SeqAct_SetFootstepMaterialOverride instance;
  * every HmFootstepComponent keeps its class defaults.
* **Only level name left in Systems code:** the slice default `setAudio(a, loadSliceMap = true)` → MP_IAC_Streets
  (and the soak hook's fallback).

### Level Kismet audio (`LevelAudioScript`)
| op | semantics | class |
|---|---|---|
| SeqAct_PlaySound Play | `PC.Kismet_ClientPlaySound`: `SourceActor.CreateAudioComponent(cue, false, true)`, FadeIn(FadeInTime), bAutoDestroy; a NEW component every Play | CONF script |
| SeqAct_PlaySound Stop | `PC.Kismet_ClientStopSound`: the first component on that actor playing the cue and not fading → FadeOut(FadeOutTime) | CONF script |
| no Target | the local PlayerController (the authored no-target cues are 2D beds / the reveal) | HIGH (native Activated) |
| SeqAct_PlayPlayerPositionalSound | Play → RandRange(DelayMin, DelayMax), IsPlaying; one-shot at a random yaw, RandRange(DistanceMin, Max) around the Source Actor (else the listener); Looping; Reset → IsPlaying false | CONF script |
| SeqAct_Reverb | `SoundMixer::activateReverb` (REVERB_TRANS_FRONT_END: Priority 155, fades 0) | CONF native A1 + data |
| SeqAct_PlayMusic / StopMusic | the level's MusicPlayer (HmMusicPlayer port) | CONF script |
| SeqAct_Interp event track | Play from the current position; keys fire in [old, new); a loop wrap fires up to the end, then from 0 (a key at 0 refires every loop) | HIGH (stock UE3), times CONF |

**UI_FrontEnd_m [FRONTEND START]** (either `FsCommand:enterFrontEnd` or `MovieStopped:FMV_intro`, the same 9 targets) [CONF]:
* **Starts:** music FRONTEND_MX_ORBIT_01 (FadeIn 0.25), REVERB_TRANS_FRONT_END, FRONTEND_WHSH_REVEAL (authored with no
  wave: silent), and the **Camera Orbiter** timeline (393.551 s, looping).
* **Timeline events:**
  * AUDIO_START @ 0: AMB_IACON Start / AMB_KAON Stop.
  * AUDIO_KAON_AMB @ 240.575: IACON Stop / KAON Start.
  * Seven FRONTEND_WHSH_DEBRIS_BY_* one-shots @ 2.1 … 389.3 s, on their InterpActors. The actors spin in place
    (PHYS_Rotating, not moved by the matinee), so their positions are static.
* **AMB_IACON:**
  * bed FRONTEND_AMB_ORBIT_01_BED_LP (2D, FadeIn 2 / FadeOut 2);
  * four EMIT_SURFACE_* loops on placed AmbientSounds (FadeIn 3 / FadeOut 6);
  * PP_SPACE_IACON (4–8 s) and PP_SPACE_IACON_03 (6–12 s) pools around AmbientSound_13540.
* **AMB_KAON:** bed ORBIT_02 and two PP_SPACE_KAON pools around AmbientSound_12790.
* **The medley:** no root or wave loop, so it plays once (~380 s) [CONF data].

**UI_PartyLobby_m / UI_Lobby_m / UI_CampaignLobby_m** (SeqEvent_GameplayStarted, at level start) [CONF]:
* **Music:** MP_PARTY_LOBBY_MX (FadeIn 0, FadeOut 0) / MP_LOBBY_MX (FadeIn 0).
* **Bed:** MP_PARTY_LOBBY_AMB_BED_CUST_MENU (FadeIn 3) / MP_LOBBY_AMB_BED.
* **Two positional pools:** party MOAN_01 at 20–25 s and 12–18 s; lobby DECO_SYNTH / GROAN at 12–18 s, kKillNewest.

### Mixer: every category, Master, MUSIC_DRY, movie mute
* **Categories:** all 47 SoundMixerProperties categories carry their Default and preset volumes (previously only
  SFX_WET_VEH_ENGINE + MASTER_WET). A cue's gain = its category's volume × masterScale [CONF values].
* **Master:** masterScale = Master volume / Master Default 0.708. The rebuild's output level stands in for Master's
  Default, so only Master changes are applied [HIGH: Master is the root (sound_group_category_mappings); the
  parent chain between the other categories is native and not applied].
* **MUSIC_DRY:** its Default volume of 0.708 (−3 dB) now applies to the music [CONF]. Weapon / movement / vehicle categories are all
  1.0, so the slice's mix is unchanged.
* **CINE_MUTE_FOR_BINK** [CONF config + data; enable/disable timing HIGH, native movie player]:
  * Engine.MovieSettings MovieMixerPreset; Priority 546, Master volume 0, FadeIn 0, FadeOut 1.
  * `setMoviePlaying(true/false)` enables / disables it.
  * A level change (Flush) drops it [CONF Flush]; whether the native player re-enables it is UNKNOWN.
* **Not applied (activator unknown):** FRONT_END (543: SFX_WET_VEH / SFX_WET_NAV volume 0); no script or authored
  data enables it [UNKNOWN, native?].
* **Out of scope:** OnCampaignGameOver's GameOverMixerPreset (campaign only); HmAudioCategoryEffectsManager radio
  presets (dialog).

### Ownership and lifetime (`LevelAudioHost`; World and `FrontendAudioRuntime` both use it)
* **A level owns its audio:**
  * its WorldInfo HmMusicPlayer;
  * the Kismet components, beds, pools and timelines;
  * its reverb presets, cue bank and decoded samples.
* **unload() (a travel):**
  * the music player stops at once [HIGH: WorldInfo-owned, bStopWhenOwnerDestroyed];
  * every instance stops and every queued event is dropped;
  * the level's cues, samples and presets are released;
  * streamed music is released at once (new `SoundCues::releaseIdleStreams`; previously one tick later);
  * the mixer flushes and the backend environment goes dry.
* **No frontend music under gameplay:** loading the match level destroys the UI level's music player, and Streets authors no music
  [CONF data]. Verified: 0 music instances and the player Stopped through every match stage.
* **Loading:** the Bink movie mutes the game mix (CINE_MUTE). `prefetch(level)` decodes the next level's streamed music during
  the loading screen.

### Frontend contract
**`game::FrontendAudioRuntime` (standalone, no World):** matches the Frontend lane's `frontend::IFrontendAudio` one to one
(`playUiSound`, `stopUiSound`, `uiLevelStarted`, `levelChange`, `tick`):
* **Extras:** `levelEvent(trigger)`, `setMoviePlaying(bool)`, `prefetchLevel(level)`, `setListener(pos)`, `state()`.
* **`uiLevelStarted(level)`:**
  * loads the level's manifests if that level is not loaded;
  * fires the level's frontend-owned music-start trigger once (UI_FrontEnd_m: FsCommand:enterFrontEnd);
  * the lobbies' GameplayStarted ops run on the next tick;
  * repeated reports add nothing.
* **`levelChange()`:** the travel unload.
* **In a match:** `World` exposes the same contract (`loadMapAudio`, `unloadMapAudio`, `levelAudioEvent`, `playUiSound`,
  `stopUiSound`, `setMoviePlaying`, `prefetchLevelAudio`, `setAudioListener`, `tickAudioOnly`, `audioState`) plus
  `resetSystemsForMatch` / `playPickupSound`.
* **Boot:** `setAudio(a, false)` skips the slice's Streets load.

### Validation
* **Suite** (`tools/systems/audio_native_suite.cpp`): **544 pass / 0 fail**. New blocks:
  * **Frontend seam:** 20 × (frontend → party lobby → game lobby → match) through `FrontendAudioRuntime`, in the
    Frontend lane's call order.
    * Authored start each time; a repeated `uiLevelStarted` adds nothing.
    * Baseline at every `levelChange`: 0 instances, 0 queued events, 0 level cues, built-in presets only,
      `Default(1)`, no reverb, music Stopped, no script / pools / timelines.
  * **Frontend timeline:** 6 Camera Orbiter loops (2 760 s) at 30 Hz.
    * Kaon swap at 240.6 s (Iacon bed / emitters fade 2 / 6 s); loop-wrap refire.
    * The medley plays once.
    * Live instances at the same phase of every loop: 2 → 2.
  * **Mixer:** 47 categories; MUSIC_DRY 0.708; CINE_MUTE (Master 0 at once, back over 1 s); Flush drops it.
  * **Lobbies:** the GameplayStarted music, bed and two pools; pool one-shots over 60 s.
  * **Lifecycle, recording backend:** 30 cycles × 7 levels (frontend → party lobby → lobby → Streets with
    shooting / pickup / round reset → lobby → frontend → synthetic map).
    * Baseline after all 210 unloads.
    * Per-stage maxima flat (frontend 11 → 10 live, Streets 57 → 57, lobby 3 → 3).
    * No music instance during any Streets stage.
  * **Real Win32 backend:** 12 cycles × 7 levels; voices 0 and decoded PCM back to the 36.5 MB base after every
    unload. Per-stage peaks identical from the first to the last cycle:

    | level | peak voices | peak PCM |
    |---|---|---|
    | frontend | 9 | 240.8 MB (incl. the 140.8 MB medley) |
    | party lobby | 5 | 99.7 MB |
    | lobby | 5 | 183.5 MB |
    | Streets | 71 | 97.2 MB |
    | synthetic map | 6 | 37.6 MB |
* **Game soaks** (Release; `WFC_LEVELAUDIO_CYCLE="<s>:<level>[@<trigger>],..."` walks frontend@enterFrontEnd →
  party lobby → lobby → Streets → lobby → frontend@MovieStopped:FMV_intro → Streets while the pawn plays):

  | run | load | transitions | errors / warnings | per-level maxima (1st → 2nd half) | PCM after each load | frame |
  |---|---|---|---|---|---|---|
  | robot 48 000 frames | firing, transforms, jumps, round reset every 7 s | 46 | 0 / 0 | not higher, e.g. Streets cues 68 → 57, frontend 20 → 12 | identical for each level every time (frontend 99.9, party 42.3, lobby 76.6, Streets 97.2 MB) | 6.4 → 6.1 ms |
  | vehicle 36 000 frames | boost cycling | 39 | 0 / 0 | flat (Streets 56 → 56) | identical | 5.8–6.1 ms |
  | fast 40 000 frames | a travel every 1.2 s, reset every 3 s | **181** (≈ 26 full cycles) | 0 / 0 | not higher (voices: Streets 96 → 69, lobby 29 → 11) | flat (PCM maxima per level identical) | 11.2–11.8 ms: the 1.2 s load hitches; flat |

  * Fast run: 0 dropped voices.
  * Particles / meshes 0 at the end.
* **Level load cost** (synchronous bank decode, game thread): UI_FrontEnd_m 0.67–0.93 s, UI_PartyLobby_m
  0.15–0.44 s, UI_Lobby_m 0.29–0.44 s, MP_IAC_Streets 0.48–0.83 s. These belong under the loading movie (PARTIAL).
* **Regression:**
  * Streets in game: 50 / 70 emitters, DEC_ROOM_LOWER reverb, 97.2 MB, 32 level cues + 10 presets.
  * wfc_fidelity 194 / 0 / 19.

### Classification
* **CONFIRMED ORIGINAL:**
  * UI levels' authored Kismet audio (ops, fields, links, timeline key times, actor positions);
  * Kismet_ClientPlaySound / Kismet_ClientStopSound and PlayPlayerPositionalSound script;
  * HmMusicPlayer rules; the authored level music;
  * REVERB_TRANS_FRONT_END and CINE_MUTE_FOR_BINK data; MovieMixerPreset config;
  * category Default volumes (MUSIC_DRY 0.708);
  * per-cue-asset limits; no surface footstep audio anywhere;
  * the medley plays once; no loading or match music.
* **HIGH:**
  * no-target PlaySound plays on the player;
  * matinee event firing / loop-wrap semantics; the client-side timeline clock started by the same trigger;
  * Master as the root category;
  * the music player dies with its level; movie mute for the movie's lifetime.
* **PARTIAL:**
  * synchronous level-bank decode on load (UI_FrontEnd_m ~0.75 s, Streets ~0.75 s, lobbies 0.16–0.4 s; the
    original streams during the loading movie);
  * no streaming music decode (prefetch);
  * VolumeMultiplier / PitchMultiplier / bSuppressSpatialization of SeqAct_PlaySound not applied (all authored at
    the defaults: warned if not);
  * the frontend listener (the orbiting camera) must be supplied by the frontend (`setListener`);
  * category parent chain;
  * IsUnpausable / pause; option sliders.
* **UNKNOWN:**
  * FRONT_END mixer preset activator;
  * whether the frontend's audio runs during seamless travel through TransitionMap=UI_FrontEnd_m (muted by the
    Bink anyway);
  * exact crossfade points of the loading movie;
  * native re-enable of the movie mute after a Flush;
  * GetUISound native lookup.

### Handoff
* **Frontend (agents/frontend):** implement `IFrontendAudio` by forwarding to `game::FrontendAudioRuntime rt(audioDevice)`.
  * Also call:
    * `rt.setMoviePlaying(true/false)` around the logos, intro and loading Binks;
    * `rt.levelEvent("MovieStopped:FMV_intro")` is equivalent to the enterFrontEnd path;
    * `rt.prefetchLevel(nextUiLevel)` on the loading screen;
    * `rt.setListener(cameraPos)` each frame.
  * Before a match world is created, call `levelChange()` and destroy `rt` (or keep it for the return; it owns its
    own cue table). The match World is created with `setAudio(a)`.
* **Integration:**
  * World::setAudio still auto-loads MP_IAC_Streets. With the frontend's `setMapName(runtimeDir)`, call
    `setAudio(a, false)` + `loadMapAudio(<level>)`.
  * The frontend recreates the audio device per match: fine (each owner releases everything).
  * Merge preview: this branch onto integration/milestone-04 conflicts in `Application.cpp` (M05 hook, keep both) and
    STATUS.md; onto agents/frontend, FIDELITY.md / STATUS.md only; onto agents/rendering, FIDELITY.md and
    `VehicleFx.cpp` (M05 `clearParticles` vs Rendering's edits — keep both).
* **AssetTools:** an AssetTools-side level manifest may carry a `kismet` section in the same schema, and it would be
  used as-is. A map's audio.json bank entries may carry `MaxConcurrentPlayCount` / `InstanceLimiting`, which win over
  the Systems cue_limits.
* **Next map:**
  * add it to `gen_level_audio.py` (limits; Kismet ops if it has non-zone audio);
  * AssetTools exports `Maps/<map>/audio.json`;
  * no runtime code.

---

## MILESTONE 05 SYSTEMS — RUNTIME LIFECYCLE FOR THE FRONTEND TRANSITION (2026-10-03, agents/systems)

**Goal:** make the non-visual runtime systems robust for frontend → map/mode selection → loading → gameplay →
return / reload. Systems checkpoints: a9ee467 (lifecycle + data-driven map audio), 4ec1506 (frontend audio), 2575207
(map-event checks, soak hooks, integration glue compatibility), plus this documentation commit.

**Sources (read only):**
* AssetTools: `frontend_audio.json`, `frontend_flow.json`, `frontend_loading.json`, `frontend_maps.json`,
  `streets_actor_inventory.json`, `streets_movers.json`, `streets_kismet.json`,
  `vertical_slice_audio_concurrency.json`, `streets_pickup_factories.json`, `authored.db` (SeqAct_PlayMusic of the
  UI levels; HmMusicPlayer defaults).
* RE-Workspace decompiled script: HM_Engine `HmMusicPlayer` / `SeqAct_PlayMusic` / `SeqAct_StopMusic` /
  `HmPlayerController.StopSound`; TransGame `TnSoundActionScriptBinding`; `SeqAct_AmbientAudioZone` /
  `SeqAct_PlayPlayerPositionalSound` `Reset`.
* No new asset recovery or native RE.

**Confidence:** CONFIRMED ORIGINAL · HIGH · PARTIAL · UNKNOWN.
**Audible result:** unchanged. The Streets bed still has 50 of 70 emitters sounding, the same zone / reverb / pool
behaviour, and the same per-voice rules.

### Phase 1 / 5 — map audio lifecycle and match reset
* **World (Systems API):**
  * `loadMapAudio(map)` — `<assets>/Maps/<map>/audio.json`; any loaded map is unloaded first.
  * `unloadMapAudio()` — player-side reset, then a hard stop of everything, AmbientAudio unload (bed, zones, pools,
    map cue bank + samples, map presets), mixer Flush, and the dry environment pushed at once.
  * `resetSystemsForMatch()`, `playPickupSound(factoryClass, receiverPos)`, `tickAudioOnly(dt)`.
* **SoundCues:**
  * `stopAll` — hard stop: voices stopped and queued events dropped.
  * `stopNonMapInstances` — the match reset.
  * `unloadMapCues` — the map bank leaves the table; its samples not shared with the built-in table are released.
* **IAudio:** `release` (Win32 frees the decoded PCM; a released handle refuses to play), `stopAllVoices`,
  `activeVoices`, `residentBytes`.
* **SoundMixer:** map presets are owned by the map. `removeMapPresets` = Flush + forget.
* **AmbientAudio:** `unload`, and `resetMatch` with per-zone scene state.
* **WeaponFx / VehicleFx:** `clearParticles`.
* **Mixer Flush at a level change** [CONF, A1/A4]: only Default stays active and the reverb slot becomes None.
* **Match reset** [HIGH: GameInfo.ResetLevel → Kismet Reset; the Reset bodies are CONF script]:
  * SeqAct_AmbientAudioZone.Reset (IsEntered false, SceneIndexCurrent −1) stops the zone scenes and their pools.
  * SeqAct_PlayPlayerPositionalSound.Reset sets IsPlaying false.
  * The bed keeps playing (AmbientSound has no Reset). The mixer is not flushed, and PlayerController.AmbientAudioZone
    is kept.
  * The (re)spawned pawn's first Touch re-begins the zone scene (pools restart) without re-enabling its preset.
  * Player-side Systems state restarts: VehicleAudio, RobotFoley, VehicleNitro, the vehicle FX instance ids,
    transform / fine-aim / tire-slip / burst flags, and the weapon serials (resynced, so no phantom shot or reload
    animation).
* **Validated (suite):**
  * 12 Streets ↔ synthetic-map load / play / unload cycles return exactly to the baseline: 0 instances, 0 queued
    events, 0 map cues, preset count = built-ins, mixer `Default(1)` / None, 0 voices, no emitters / zones / pools.
  * Loading over a loaded map keeps one bed.
  * Match reset keeps the bed and reverb, stops player sounds and pools, and a re-touch re-begins the scene without
    a duplicate preset enable.
  * Real backend: voices 69 → 0 (Streets) and 11 → 0 (synthetic); decoded PCM 88.8 MB → back to the 28.1 MB base
    every cycle.

### Phase 2 — data-driven map audio
* **Removed Streets-specific source:**
  * The 10 REVERB_TRANS_MP_STREETS_* presets compiled into `SoundMixer.inc` now come from the map's audio.json
    `reverb_presets` (mixer_preset + MASTER_WET DSP). Verified identical to the old compiled values (10 / 10, 0
    mismatches).
  * The two hard-coded Streets cue limits now come from the generic `CookedCueLimits.inc` (67 authored cue-asset
    limits, AssetTools); a map entry's own field wins.
  * The per-instance Streets pickup table is now per **factory class** (`PickupPresentation.inc`).
  * The only map name left in Systems code is the slice default passed to `loadMapAudio("MP_IAC_Streets")` in
    `World::setAudio`.
* **Proof:** a synthetic second map (own cue names, point / line / volume emitters, one zone with its own preset and
  pool, written by the suite) initializes, plays (zone reverb REVERB_FAKE_ROOM, pool, bed) and unloads through the
  same code with no branch.

### Phase 3 — frontend audio (Systems side only; no UI)
* **MusicPlayer** [CONF script port of HmMusicPlayer]:
  * PlayMusic → QueueMusicTrack: same cue → no restart; lower Priority → ignored; same priority + same queued cue →
    ignored; IgnoreSpazTimer.
  * SpazTime 5.0 [CONF class default]; Crossfading (outgoing FadeOut or override, incoming FadeIn); BoredomTime
    restart.
  * StopMusic (SpazTimer kept, per script); stingers (root-priority gate, 0.1 s fade).
* **FrontendAudio:**
  * `playUiSound(name)` = TnSoundActionScriptBinding.PlaySound → GetUISound → a 2D AudioComponent [script CONF;
    name → cue lookup HIGH, native].
  * `stopUiSound(name, fade)` = HmPlayerController.StopSound: the first matching component fades [CONF; "first" =
    oldest instance, HIGH].
  * `frontendTrack(uiLevel)` = the authored SeqAct_PlayMusic tracks [CONF authored]:

    | UI level | Track | Fades |
    |---|---|---|
    | UI_FrontEnd_m | FRONTEND_MX_ORBIT_01 | FadeIn 0.25 |
    | UI_Lobby_m / UI_CampaignLobby_m | MP_LOBBY_MX | FadeIn 0 |
    | UI_PartyLobby_m | MP_PARTY_LOBBY_MX | FadeIn 0, FadeOut 0 |

  * `onLevelChange()` [HIGH: the WorldInfo-owned player dies with the level; bStopWhenOwnerDestroyed].
* **Cue table:**
  * the 16 GFx UI cues (resident; PICKUP_DMG_MULTIPLIER_DECREASE is authored silent);
  * the 3 music cues, **streamed** (decoded on first play or `prefetch()`, released when the last instance ends).
* **Root bLooping timeline** [HIGH, RE A6 0x827881D8 pseudocode]: at LoopEnd the playback time wraps to
  LoopStart and the wave events from LoopStart run again (MP_LOBBY_MX: waves at 0 / 173.6 / 291.4 s, wrap at
  390.7 s). No slice cue uses it, so Streets is unaffected.
* **Authored facts:**
  * FRONTEND_MX_ORBIT_01 has neither a root nor a wave loop, so the frontend medley (≈380 s) plays once
    [CONF data].
  * No loading music is authored (the loading movie's audio is inside the Bink file).
  * Streets authors no SeqAct_PlayMusic, so a match has no music [CONF data].
* **Validated:** 16 / 16 GFx names, StopSound-first, queue / SpazTimer / crossfade, boredom, wrap, stinger,
  level change.
* **Real backend:** the frontend medley costs 91–362 ms of decode / 140.8 MB on first play, 0 ms after a prefetch,
  and is released after stop.
* **PARTIAL:**
  * no streaming decode (whole-file decode; use `prefetch()` on a loading screen);
  * MUSIC_DRY / SFX_DRY_HUD category volumes (options sliders) are not modelled;
  * IsUnpausable (no pause system yet).
* **UNKNOWN / external:**
  * the UI levels' own Kismet audio (UI_FrontEnd_m: SeqAct_PlaySound ×14, PlayPlayerPositionalSound ×4,
    SeqAct_Reverb) needs a UI-level audio manifest (AssetTools); it then loads through `loadMapAudio`;
  * SeqAct_PlaySound → PC.Kismet_ClientPlaySound (native) is not implemented;
  * the native GetUISound lookup.

### Phase 4 — pickup / map-event audio (Systems = audio only)
* **Pickups:** one PickupSound per Gameplay Taken event, on the receiving pawn; Respawned plays nothing (RespawnEffect
  empty); the flag / bomb inventories author no PickupSound [CONF].
* **Placed audio:** every placed AudioComponent belongs to the 40 AmbientSounds; none is on a mover or a mode-gated
  actor. No Kismet audio op passes a game-rule condition. Movers carry no sound [CONF data].
* **No overlap with Rendering:** Systems draws no map / pickup effect (LevelFx removed, M04); the effect-state is
  Rendering's (setMapEffectState).
* **Integration milestone 04 glue:** a03d7f7 calls `pickupFx_.onTaken(<actor name>, ...)`. `onTaken` /
  `pickupSoundFor` accept a factory class OR a placed actor name (`<Class>_<N>`), and World keeps `pickupFx_`, so
  that glue compiles unchanged after merging the class-keyed API.
* **Verified on the merged int-04 + Systems build** (Gameplay WFC_PICKUPTEST with audio enabled in a scratch copy):
  ammo ×2, health ×2 (including the CheckTouching re-take), overshield ×2, each sound once; respawns silent.

### Phase 6 — long-run stability (game soak, Release)
* **robot_soak** (24 000 frames):
  * load: firing, walking, transform every 600 frames, jump every 240, map-audio unload/reload every 10 s
    (15 loads), match reset every 7 s;
  * cues mean by quarter 56 / 52 / 51 / 52; backend voices mean 70 / 66 / 64 / 64 (max 96, **0 dropped**,
    28 steals); resident PCM **97.2 MB flat**; mix 1.4–1.8 ms per block;
  * frame mean by quarter 7.3 / 6.1 / 6.2 / 6.0 ms (no drift); particles / meshes 0 at the end.
* **vehicle_soak** (24 000 frames):
  * load: boost every 150 frames, 11 map reloads, match reset every 9 s;
  * cues 54 / 55 / 54 / 55; voices 69–70 every quarter; PCM 97.2 MB flat; 0 dropped;
  * frame 5.1–5.5 ms.
* **control_soak** (24 000 frames, no lifecycle cycling): bounded the same way; frame 5.9 → 5.4 ms.
* **Integrated build** (merge of this branch onto integration/milestone-04): robot and vehicle 10 000-frame soaks
  with reloads and resets — the second half is no higher than the first; PCM 97.2 MB flat; 0 errors.
* **Queued-event peaks** (up to 65): these are BL_FOLY_IDLES.OPTIMUS_IDLE — 15 authored layers over 2.7 s,
  retriggered while the pawn idles against a wall. Bounded by the cue's MaxConcurrentPlayCount (5 × 15); they drain
  once the pawn moves. Not a leak.
* **Handles:** instance ids grow monotonically; Win32 voice handles carry a 19-bit generation per slot, so a stale
  handle can only alias after ≈524 k reuses of one slot.

### Handoff — exact call sequence for the frontend / integration owner
```
boot:            audio = createAudio(); world.setAudio(audio)          // global cue table (setAudio currently also
                                                                        // loads the slice map: replace with the selection)
                 FrontendAudio fe(world.cues())                         // keep for the whole session
frontend/lobby:  each frame: world.tickAudioOnly(dt); fe.tick(dt)
                 on UI level load: MusicTrack t; if (FrontendAudio::frontendTrack(level, t)) fe.music().playMusic(t)
                 GFx Sound.PlaySound(n) / StopSound(n, f): fe.playUiSound(n) / fe.stopUiSound(n, f)
                 optional, while a loading screen shows: world.cues().prefetch(<next music cue>)
loading a match: fe.onLevelChange()                                     // the UI level's music player goes away
                 world.loadMapAudio(<map>)                              // unloads any previous map audio first
match:           World::tick drives all match audio; Gameplay pickup Taken -> world.playPickupSound(class, receiverPos)
                 (or the integration glue pickupFx_.onTaken(actorName, ...) - both resolve per factory class)
round reset:     world.resetSystemsForMatch()
return to menu:  world.unloadMapAudio(); fe.onLevelChange(); then the UI level's track as above
next match:      world.loadMapAudio(<next map>)
```
**Guarantees after `unloadMapAudio()`:** no cue instance, queued event, map cue, map preset, map sample or voice
remains; the mixer is at Default with no reverb; the backend environment is dry; the Systems FX particles are gone.

**Merge note:** this branch onto integration/milestone-04 conflicts only in `src/core/Application.cpp` (Systems
`WFC_PRESSTRANSFORM_EVERY` hook vs integration `WFC_RAMSELF`; keep both) and STATUS.md.

### Open items
* **AssetTools:** a UI-level audio manifest (UI_FrontEnd_m / lobbies: Kismet PlaySound, pools, reverb, ambience) in
  the audio.json schema.
* **ReVa / native:** the GetUISound lookup; Kismet_ClientPlaySound; whether a listener exists at the level-start
  ambient registration; FMOD equal-priority stealing; whether GameInfo.ResetLevel resets
  PlayerController.AmbientAudioZone.
* **Frontend owner:** call sequence above; pause / IsUnpausable; option-slider volumes (MUSIC_DRY / SFX categories).
* **Gameplay:** call `resetSystemsForMatch()` on round restarts and `loadMapAudio` / `unloadMapAudio` on map change.

---

## MILESTONE 04 INTEGRATION PREVIEW — agents/systems 65daecd (2026-10-03)

Read-only preview for the integration owner. `git merge-tree` computed the merges in memory (no refs, no worktree);
the gameplay merge tree was archived into `work/m4/merge_preview`, resolved, built and run there. Nothing outside
this worktree was touched.

**Merge results** (`git merge-tree --write-tree HEAD origin/<branch>`):

| Merge | Conflicts |
|---|---|
| × integration/milestone-03 356c352 | **STATUS.md only** (documentation). Systems 35145f3 / 757347c / d6932dc are already in it; 8dcb861 and 65daecd are not. |
| × agents/experimental 5120c6f | **clean** |
| × agents/rendering 9035fb3 | FIDELITY.md, `src/game/VehicleFx.cpp`: Rendering's old bb94e40 edit, **already resolved inside integration 356c352** (the Systems material-path version kept), so it does not recur when merging through integration. |
| × agents/gameplay d122ef4 | STATUS.md, `src/core/Application.cpp`, `src/game/PlayerController.h`, `src/game/World.cpp` (3 hunks). All additive. |

**Gameplay resolutions** (verified by building and running):
1. **Application.cpp:** keep Gameplay's frame-gated `WFC_AUTOBOOST` line **and** Systems' `WFC_AUTOBOOST_CYCLE` /
   `WFC_AUTOJUMP_EVERY` test hooks.
2. **PlayerController.h:** keep Systems' `moveForwardInput()` (vehicle EngineLoadState audio) **and** Gameplay's
   `setCameraYaw / setCameraPitch` (they also set viewYaw_/viewPitch_).
3. **World.cpp renderer light-visibility query:** neither side compiles alone (HEAD dropped the `d` / `n`
   segment-march variables that Gameplay's side uses). Use Gameplay's line world with one grid query:
   `const CollisionWorld& lineWorld = weaponCollision_.valid() ? weaponCollision_ : collision_; return
   lineWorld.segmentHit(a, b, t);`. Rendering/Gameplay should confirm that collision world for light visibility.
4. **World.cpp tick start:** keep both: Systems' `WFC_HITCHLOG` block, then Gameplay's `pickupEvents_.clear();
   destructibleEvents_.clear(); mapState_.tick(...)`.
5. **World.cpp controller:** `{ sysprof::Scope sp(sysprof::Ctrl); player_.controller().applyToPawn(*this, dt); }`
   followed by Gameplay's `gameplayRamContacts();`.
6. **Pickup glue,** right after `for (auto& a : actors_) if (a->alive()) a->tick(*this, dt);`:
   ```
   for (const PickupEvent& e : pickupEvents_)
       if (e.type == PickupEvent::Type::Taken && e.factory >= 0 && (size_t)e.factory < pickupFactories_.size())
           pickupFx_.onTaken(pickupFactories_[(size_t)e.factory]->name().c_str(), cues_, atPawn(),
                             core::length(e.receiverPos - listenerPos_));
   ```

**Preview validation** (merged tree, Release build, `WFC_RENDER_DATA` = this worktree's work/render):
* **Builds clean.**
* **Pickup sound via the glue:** Gameplay's `WFC_PICKUPTEST`, with audio enabled in the scratch copy only (the
  test returns before `setAudio`, `Application.cpp:86`; a suggestion for Gameplay), plays every authored
  PickupSound **once per take**, attached to the pawn (owner 0):
  * ammo: 2 takes; health: 2 takes, including the CheckTouching re-take at respawn; overshield: 2 takes;
  * the ammo cue's 0.2 s layer follows the pawn to the next position;
  * respawns play nothing.
* **Harness:** wfc_fidelity 190 / **2 FAIL** / 21. The FAILs are `spread_after_10` / `spread_cap`: stale
  expectations against Gameplay pass 16's RE HUD spread, already retired by Experimental 28e093f. This is not
  merge or Systems breakage.
* **Scripted walk / fire routes from all 24 FFA starts never reached a takeable pickup.** Gameplay `tryGive`
  refuses a pickup that does nothing, and the routes don't cross the factories, so the pickup sound is validated by
  the deterministic test above.

---

## MILESTONE 04 SYSTEMS ADDENDUM — MAP FX OWNERSHIP TO RENDERING; OBJECTIVE BEAM (2026-10-03, agents/systems)

**Sources:**
* Rendering `agents/rendering` 411c970 (read-only): WfcMapFx simulates the 8 Steam_Sm_FX level emitters and
  the pickup effects from the decoded module streams, and owns their script state through `setMapEffectState`.
* RE `00dcb20` `notes/MILESTONE04_STREETS_PICKUP_OBJECTIVE_PRESENTATION.md`.

**Ownership decision (user):** Systems yields the map FX runtime to Rendering.
* **Removed:** `src/game/LevelFx.{h,cpp}` (the Systems Steam_Sm_FX simulation, the M04 Steam_Mat handoff) and
  its World calls. On this branch alone the steam is therefore not drawn until Rendering 411c970 is integrated;
  that is intended, to avoid double steam.
* **PickupPresentation is now the pickup SOUND only:**
  * the authored factory table (gen_pickups.py) plus `onTaken(actor, cues, receiverEmitter, dist)` →
    Inventory.AnnouncePickup PlaySound on the receiving pawn;
  * the effect state (reset / SetPickupHidden / SetPickupVisible / onRespawned / effectState) is removed:
    Rendering's `setMapEffectState` owns it;
  * Respawned events need nothing from Systems (no respawn sound).
* **Integration glue** (World::tick, Gameplay pickup events):
  ```
  for (const PickupEvent& e : pickupEvents_)
      if (e.type == PickupEvent::Type::Taken)
          pickupFx_.onTaken(pickupFactories_[(size_t)e.factory]->name().c_str(), cues_, atPawn(),
                            core::length(e.receiverPos - listenerPos_));
  ```
  Exactly one lane plays the PickupSound: Systems, from its authored table. Gameplay's
  `PickupEvent::pickupSound` must not be played as well.

**Objective highlight beam [CONF, RE 00dcb20]:**
* `ShouldDisplayHighlightFx = True` is authored on `TnWeaponPickupFactory`, and the flag and bomb objective
  factories inherit it. The AssetTools value (already in PickupPresentation.inc) is right; RE's earlier "False"
  was wrong.
* The M04 "authored-data conflict" entry is resolved. Objectives only exist in CTF / EXT (Disabled in other
  modes). This doesn't affect Systems: objective pickups play no beam-related sound.

**Still UNKNOWN (RE 00dcb20 §3 did not reach them):**
* equal-priority FMOD voice stealing;
* whether a listener exists at the native level-start ambient registration.

**Validation:**
* suite 557 pass / 0 fail (the effect-state checks were removed with the code; the authored-data and sound checks
  remain);
* audio-attach 325 / 0 FAIL / 11 KNOWN (all `PP_DECO_MECH_*` zone pools; 0 player-owned);
* wfc_fidelity 194/0/19; collision 0 mismatches; probe 31/0/1;
* sustained fire 5.3–14.3 ms (mean 8.7), no errors.

---

## MILESTONE 04 SYSTEMS — MP_IAC_STREETS WORLD SYSTEMS (2026-10-03, agents/systems)

**Sources:** AssetTools **a23c675** `manifests/mp_iac_streets_complete.json` (counts, presentation-vs-mode
split), `streets_kismet.json`, `streets_movers.json`, `vertical_slice_audio_concurrency.json`, plus
ExtractedAssets `audio.json` / `map_fx.json` / `gameplay.json` / `spawnpoints.json`.
* Read-only RE: `RE-Workspace/notes/MILESTONE03_RUNTIME_SEMANTICS_ASSETTOOLS_7a69756.md` (d50c2a9: P2 pickups,
  P4 concurrency) and the decompiled script for SeqAct_PlayPlayerPositionalSound, PickupFactory and
  TnPickupFactory.
* Read-only coordination: Gameplay `agents/gameplay` 0762f01 `PickupFactory` / `PickupEvent`.

**Confidence:** CONFIRMED ORIGINAL · HIGH · PROVISIONAL · UNKNOWN.

### 1. 70 ambient emitters — native playback (replaces the PROVISIONAL virtualizer)
* **Accounted for:** 70 = 40 point + 13 line + 17 volume (manifest counts). All are looping auto-play cues of
  BL_LVL_MP_IAC_STREETS, and no Kismet op toggles them [CONF].
* **Native model [CONF]:**
  * Each AudioComponent auto-plays once at level start (bAutoPlay), in authored order.
  * Registration uses RegisterInstanceLimiting: global per cue asset, kKillFarthest against the listener, and
    the newcomer is refused when it is the farthest (RE P4).
  * Line and volume emitters re-Play every tick while not playing (A7).
  * Placement (closest point on the segment or oriented box), cull, rolloff, SmartPan, occlusion, mixer
    category and zone reverb are the existing confirmed per-voice rules.
* **Removed (all invented):** the PROVISIONAL global budget (24 voices), the −48 dB audibility gate and the 0.5 s
  virtual↔real fades.
* **Result:** no line/volume cue has more emitters than its limit, so all of those play. Four point cues do, and
  the instances nearest the level-start listener play:

  | Cue | Emitters | Limit |
  |---|---|---|
  | EMIT_FLOURESCENT_LIGHTS | 13 | 5 |
  | EMIT_ENERGON_LIQUID_CRATERS | 12 | 5 |
  | EMIT_LAMP_POSTS | 8 | 5 |
  | EMIT_FLOOD_LIGHTS | 5 | 3 |

  That gives **50 of 70 sounding** throughout (suite and every game run).
* **HIGH:** a point AmbientSound refused or killed at start never restarts. Engine AmbientSound has no
  script or tick that replays its AudioComponent, and WFC's AmbientSound class is script-less.
* **UNKNOWN (native):** whether a listener exists at the native level-start registration; the rebuild registers
  on the first tick with the spawn camera.

### 96-channel priority stealing (new, needed by the always-on bed)
* **Measured problem:** with 50 emitters (~61 voices) always on, sustained fire hit the backend's 96-voice
  ceiling, and new voices were **dropped** (23 in one run), including weapon layers.
* **Fix:**
  * cue tables now carry SoundNodeRoot.Priority and the wave-event OverridePriority / Priority
    (`gen_cues.py`; the table is otherwise byte-identical; map cues are read from audio.json);
  * each voice gets FMOD channel priority `255 − clamp(Priority, −1, 255)` [CONF RE d50c2a9];
  * with all **96 channels** busy (Xe-TransEngine.ini MaxChannels [CONF]), a new sound takes the channel of the
    least important voice. A newcomer less important than every playing voice does not play.
* **Authored priorities:**

  | Cues | Priority |
  |---|---|
  | weapon, vehicle, transform | 200 |
  | footsteps | 25 |
  | map emitters | 15 / 80 / 10 |
  | pools, weapon idle, Optimus idle foley | 0 |

* **HIGH (FMOD Ex internal):** equal priorities give up the quietest voice.
* **Measured:**
  * sustained fire takes 12–13 ambient channels;
  * the only refused starts (3 in one firing run) are Priority-0 cues, as authored;
  * shots, vehicle, transform and footsteps are never refused.

### 2. Zones / reverb — unchanged, re-validated
* **Accounted for:** 9 zones and 10 presets (all known to the mixer; one preset, TRAIN_DEPOT, has no zone, as
  authored). This matches the complete manifest.
* **Behaviour:** local-player Touch edges, last touch wins, no Exit restoration, explicit previous-preset
  Disable, 0.25 s linear fades, and the priority/fade rules. All existing suite checks pass.

### 3. Timed one-shot pools — CONFIRMED (script + Kismet links)
* **11 pools** (audio.json = streets_kismet.json).
* **Wiring:** zone "Scene 0 Begun" → sub-sequence START → pool Play; "Scene 0 Ended" → STOP → pool Stop.
* **SeqAct_PlayPlayerPositionalSound (decompiled):**
  * Play arms DelayRemaining = RandRange(DelayMin, DelayMax);
  * on expiry it plays at RandRange(0, 359)° yaw and RandRange(DistanceMin, DistanceMax) from the listener
    (no Source Actor linked);
  * it uses a WorldInfo AudioComponent at a fixed location, and re-arms while Looping.
* **Class defaults** (verified in authored.db): Looping true, DelayMin 3, DelayMax 5, Distance 2000. Exterior
  overrides only DelayMax 8.
* The existing implementation already matched; nothing changed.

### 4. Map FX state / lifetime (SUPERSEDED by the M04 addendum: Rendering owns the map FX runtime)
* **The 8 Steam_Sm_FX level emitters [CONF]:** bAutoActivate true, CullDistance 300000 UU (never reached), no
  Kismet state changes, and they loop forever. They are simulated by LevelFx as before.
* **Rendering handoff:** on Rendering's material path, LevelFx now passes `FX_Materials_p.Materials.Steam_Mat`
  and the authored particle colour (ColorOverLife / AlphaOverLife). It no longer bakes the Steam_Mat emissive
  itself, so the material logic isn't duplicated; the GL1 fallback is unchanged.
* **Not reproduced:** SecondsBeforeInactive 1.0 (the update pause while unseen, which has no visible effect).
* **Pickup particle systems:** still not simulated; the module-flag semantics stay UNKNOWN (PASS 6).

### 5. Pickup presentation ↔ Gameplay
* **Correction (RE d50c2a9 P2):** PreBeginPlay → InitializePickup → SetPickupMesh → SetPickupVisible, so
  ammo-crate highlight beams are **active from map start**. PASS 6's "inactive until the first respawn" (from
  bAutoActivate=false) was wrong. `reset()` is now the available state.
* **New adapters:** `PickupPresentation::onTaken(actor, cues, receiverEmitter, dist)` (AnnouncePickup + SetPickupHidden)
  and `onRespawned(actor)` (SetPickupVisible), keyed by the gameplay.json actor name.
  * They consume Gameplay's `World::pickupEvents()` (Taken / Respawned).
  * No timers, so the game rules aren't implemented twice.
* **Integration glue** (World::tick, after Gameplay's factories tick; the integration owner adds it when merging
  0762f01):
  ```
  for (const PickupEvent& e : pickupEvents_) {
      const char* a = pickupFactories_[(size_t)e.factory]->name().c_str();
      if (e.type == PickupEvent::Type::Taken) pickupFx_.onTaken(a, cues_, atPawn(), core::length(e.receiverPos - listenerPos_));
      else pickupFx_.onRespawned(a);
  }
  ```
  * Gameplay's `PickupEvent::pickupSound` duplicates the authored cue. Systems plays from its own table, so the
    sound must not also be played by Gameplay.
* **Objective beam:** RESOLVED by RE 00dcb20. Flag and bomb inherit ShouldDisplayHighlightFx **true** from
  TnWeaponPickupFactory, so the AssetTools value is right.

### 6. Moving world sources
* `streets_movers.json` references no sound. No mover sound exists in the code: none for the domes, SkyBeam or
  totems.

### 7. Cohesion / attachment
* **Audio-attach (Experimental spy):** 316 pass / 0 FAIL / 14 KNOWN.
  * All 14 are `PP_DECO_MECH_*` zone pool one-shots, world-fixed by design.
  * **0 player-owned sounds left behind.** Player sounds (footsteps, landing, transform, weapon, vehicle,
    boost) follow their owner; map emitters and pools stay world-owned.
* **Occlusion:** 43–47 of the bed's instances are occluded behind geometry at the spawn (the per-instance line
  check, −6 dB), as designed.

### Validation
* **Suite:** `tools/systems/audio_native_suite.cpp` **586 pass / 0 fail**. New checks:
  * world bed: 70 / 40 / 13 / 17 emitters, 9 zones, 10 presets in the mixer, 11 authored pools;
  * per-cue start limiting (min(n, limit) per cue; the fluorescent lights playing nearest); 50 of 70 at start;
  * line re-play vs point no-restart;
  * channel priority (SHOOT 55 / 60) and stealing on the real backend (priority 55 takes a 240 channel, 250 is
    refused);
  * pickup spawn = available with the beam on, plus the name-keyed adapters.
* **Regression:** wfc_fidelity 194/0/19; collision 0 mismatches; probe 31/0/1 (1 KNOWN Rendering).
* **Frame time** (ms, 120-frame windows: range / mean):

  | Scenario | Range | Mean |
  |---|---|---|
  | idle | 6.1–11.4 | 7.0 |
  | traversal | 4.6–11.5 | 5.7 |
  | firing | 5.4–15.3 | 10.4 |
  | sustained | 4.8–14.2 | 9.2 (PASS 7 6.5–14.1 / 10.0) |
  | transform | 3.8–10.5 | 4.8 |
  | jump | 4.7–10.5 | 6.0 |
  | vehicle | 3.8–8.6 | 4.7 |
  | Boost cycle | 3.9–9.7 | 5.0 |
  | Nitro | 4.4–7.3 | 5.1 |
  | dense region (start 16) idle | 6.5–11.8 | 7.7 |
  | dense sustained | 6.9–13.0 | 8.6 |
  | dense walk | 5.0–11.6 | 6.0 |

  Audio mix 1.3–1.6 ms per 21 ms block (audio thread).
* **Cleanup:** particles/meshes → 0 after every non-firing run; queued events bounded; cues ≈ 51–54 at rest =
  the bed + idle foley. No leaks.

### Remaining
* **UNKNOWN:**
  * whether a native level-start listener exists;
  * FMOD equal-priority / virtual-voice internals;
  * pickup particle module flags;
  * the native particle mesh-material fallback.
* **Gameplay:** merge the pickup-event glue above; vehicle jump.
* **Rendering:** Steam_Mat and pickup FX through the material path.
* **AssetTools / RE:** the objective ShouldDisplayHighlightFx conflict (RESOLVED, RE 00dcb20).

---

## MILESTONE 03 SYSTEMS PASS 7 — VEHICLE LOOP ENABLE CONFIRMED (2026-10-02, agents/systems)

**Source:** `RE-Workspace/notes/MILESTONE03_RUNTIME_SEMANTICS_ASSETTOOLS_7a69756.md` §P1 and its VEHICLE LOOP
HANDOFF (ReverseEngineering commit **d50c2a9**; the handoff cited "d50e2a9", but the repository HEAD is d50c2a9).
This is a narrow correction of PASS 6 (757347c). Only provenance and comments change; runtime behaviour is unchanged.

**CONFIRMED ORIGINAL:**
* **Loop enable:** looping is enabled only by the wave node.
  * `SoundNodeWaveEvent.bLooping` (node+0x34, bit 0x80000000) → `FMOD_LOOP_NORMAL` at channel create
    (0x82768820).
  * WFC sets no loop points and no loop count (infinite), so FMOD loops the FSB region 0…N−1, i.e. the whole
    decoded sample, indefinitely.
  * Neither the cue-root LoopStart / LoopEnd nor the FSB header loop flag is consulted.
* **Rebuild loop enable:** already `VoiceParams::loop = EventDef::loop` (the wave event's bLooping). No code
  derives looping from the cue root or the FSB header; `FsbLoop::headerLoopFlag` is informational only.
* **Loop region:** the whole-sample FSB regions from AssetTools 7a69756 (`VehicleLoops.inc`) are kept. They equal
  FMOD's default region, now CONFIRMED rather than PASS 6's "UNKNOWN loop enable".
* **States and fades:**
  * start, loop and stop are separate cues/components driven by the HmVehicleAudioComponent /
    HmPlayerVehicleAudioComponentImpl states;
  * entering a state fades in over 0.1 s; engine fade-out 0.2 s, boost fade-out 0.15 s;
  * state changes overlap as crossfades;
  * `FadeOut` fades linearly from the current playback position without waiting for a loop boundary; time 0 stops
    immediately; then auto-destroy.

  All of this is already implemented: VehicleAudio and `SoundCues::stop` / `fadeIn`.
* **Removed:** no provisional loop-enable code existed, so nothing was removed. The PASS 6 "UNKNOWN: how FMOD loop
  mode is enabled" entries are resolved.

**UNKNOWN (FMOD internal, per the report):** the XMA decoder seam behaviour at the loop point.

**Validation:**
* **Suite:** 533 pass / 0 fail. New loop-runtime block:
  * the loop flag is per wave event (DRIVE_ONLOAD all looping, BOOST_END none);
  * a loop stopped at an arbitrary position (0.367 s) is at half level 0.1 s into a 0.2 s fade, then stops and
    auto-destroys;
  * a zero fade stops immediately;
  * on the Win32 backend, a looping wave plays past its 2.0 s length, while a non-looping 0.43 s wave ends.
* **Game runs:**
  * hover / engine loop and repeated Boost: START → LOOP → END with 0.15 / 0.2 s fades and the BOOST_END duck;
  * transform out of the vehicle: ONLOAD 0.2 s, squeal 0.5 s, no vehicle cue left.
* **Regression:**
  * audio-attach 238 pass / 0 FAIL / 13 KNOWN (all `PP_DECO_MECH_*` zone pools; 0 player-owned);
  * wfc_fidelity 194/0/19; collision 0 mismatches; probe 31/0/1;
  * sustained fire 6.5–14.1 ms (mean 10.0; PASS 6 6.5–13.1 / 9.7; no runtime change, run-to-run variation);
  * cleanup clean.

---

## MILESTONE 03 SYSTEMS PASS 6 — ASSETTOOLS AUTHORED-DATA HANDOFF (2026-10-02, agents/systems)

**Source:** AssetTools commit **7a69756**, manifests (read only):
* `streets_surface_audio.json`
* `vertical_slice_audio_concurrency.json`
* `vehicle_audio_loops.json`
* `streets_pickup_factories.json`
* `streets_pickup_fx.json`

The pickup activation flow also comes from decompiled script (RE-Workspace `work/script/decomp`, read only):
* `Engine.PickupFactory`, `Engine.Inventory`;
* `TransGame.TnPickupFactory`, `TnHealthPickupFactory`.

No new asset recovery or native RE was done. Builds on Systems 35145f3 (PASS 5).
**Confidence:** CONFIRMED ORIGINAL · HIGH · PROVISIONAL · UNKNOWN.

### Footsteps / landing — CONFIRMED AUTHORED (no longer "fallback")
* **One table for the whole map:** every Streets PhysicalMaterial resolves every footstep slot to the same
  `SoundEvents_Footsteps.FS_DEFAULT_*` events.
  * Materials: Metal (also `Engine.DefaultPhysMaterialName` and both BlockingVolume overrides), Rubber, Water,
    ForceField, and surfaces with no property.
  * Supporting data: `TnPawn.FootstepComp0` defaults, no PhysicalMaterialOverride, 0
    SeqAct_SetFootstepMaterialOverride.
* **Optimus mapping:** `SoundEvents.CHR_OPTIMUS` maps 8 events to `BL_FS_LRG_BOT.*`: WALK, RUN, SCUFF, JUMP,
  JUMP_CHARGED, LAND, HARD_LAND, LAND_HIGH_FALL.
  * JOG, CROUCH_* and DashLand are unmapped and therefore silent, which RobotFoley already does.
* **Consequence:**
  * the cues the rebuild plays **are** the authored Streets footstep and landing sounds;
  * no surface trace is needed, and no surface variants exist (the suite checks that none are in the table);
  * which surface the native trace samples is moot.

Earlier "missing Streets surface table" requests (PASS 2, 3, 5) are resolved.

### Cue concurrency — CONFIRMED AUTHORED
* All 71 manifest cues the rebuild plays match authored MaxConcurrentPlayCount / InstanceLimiting exactly (or the
  inherited Engine.Default__SoundCue 5 / kKillFarthest). This covers 44 table cues, the 3 new pickup cues and the
  map bank, including EMIT_FLOOD_LIGHTS / EMIT_MONRAIL_IDLE_LP = 3.
* No value changed.
* The 7 authored kKillNewest cues (Soundwave / Trypticon impacts) are not played by the slice.
* **Root `PlayMixerPreset = Default`** (137 cues): a no-op in the native mixer. Default is always active, so
  Enable/Disable only bumps its ref-count. It is not applied.
* **UNKNOWN (native; no field defines the runtime):**
  * category `ChannelCountMixerPreset`. **SFX_WET_COMBAT_ROBOT_WPN_SHOOT** (the Ion Blaster SHOOT category)
    authors `MECH_WPN_VOICE_THRESHOLD`, Threshold 8, Time 0. That preset (Priority 481, FadeIn 1.0, FadeOut 0.5)
    ducks SFX_WET_AMB / SFX_DRY_AMB to 0.158 and SFX_WET_NAV to 0.501.
  * How the voice count is measured, the compare, and when it is disabled are not recovered, so it is **not
    implemented** (ReVa request). It can audibly duck ambience during sustained fire in the original.
  * per-owner vs global limits; virtualization; priority stealing.

### Vehicle loops — CONFIRMED AUTHORED regions (loop enable CONFIRMED in PASS 7)
* **Census:** 1929 FSB4 samples, 0 with a custom loop range, 0 with a header LOOP flag.
* **The 7 looping vehicle waves:** loop [0, total−1], the whole sample. All 39 vehicle waves' extracted .wav
  files match their FSB headers exactly (rate, channels, sample count).
  * Waves: ENGINE_MID / ENGINE_BOOST / IDLE (48 kHz stereo), SYNTH_ENERGY_MOD_PAN, TIRE_NOISE_02,
    TIRE_SQUEAL_HEAVY (24 kHz), JET_TURBO_WHINE (36 kHz).
* **`tools/systems/gen_loops.py` → `VehicleLoops.inc`:** asserts each wave against its FSB header;
  `SoundCues::applyLoopPoints()` applies the regions at load. This replaces PASS 5's never-delivered
  `fsb_loop_points.json`.
* **Fix (Win32Audio):** the loop region is now [start, end+1), and the last frame interpolates into the loop start.
  * PASS 5 wrapped at frames−1 without wrap interpolation, which was one frame short per period.
  * Non-looping playback is unchanged.
* **Unchanged:** start/loop/end cue presentation and the native fades (engine 0.2 s, boost 0.15 s).
* **Loop enable (RESOLVED in PASS 7, RE d50c2a9):** the wave event's bLooping → FMOD_LOOP_NORMAL, with no
  loop points or count.
* **UNKNOWN (native):**
  * whether the XMA seek/loop tables shift the start for gapless XMA looping.

  Neither is inferred from waveform content.

### Pickups — presentation (Systems), data CONFIRMED AUTHORED, flow CONFIRMED (script)
* **Data:** `tools/systems/gen_pickups.py` → `PickupPresentation.inc`, the 27 factories:
  * 14 ammo crates (RespawnTime 30), 9 health (60), 1 overshield (120), 2 flag + 1 bomb objectives;
  * per factory: positions, PickupSound, CustomPickupEffect / PickupEffect templates, attachment,
    ShouldDisplayHighlightFx, RequiredGameRuleClass.
* **Pickup sounds:** the 3 PickupSound cues are added to the cue table: HEALTH_PU_AMMO (−16 dB root),
  HEALTH_PU_ENERGON, OVERSHIELD_POWER_UP.
* **`PickupPresentation`, transcribed from script:**
  * **Spawn:** CustomPickupEffect (HealthPickup_FX / OvershieldPickup_FX) is active; the highlight PickupEffect
    (Pickup_FX) is **inactive** (bAutoActivate false).
  * **`SetPickupHidden`:** hides and deactivates the custom effect; deactivates the highlight when
    ShouldDisplayHighlightFx.
  * **`SetPickupVisible`:** the reverse. So the ammo-crate beam first lights up when the crate respawns.
  * **Highlight rendering:** PickupEffect renders only for the ammo crate and objectives (it is in their
    Components). Health and overshield have ShouldDisplayHighlightFx false and the component unattached.
  * **`announcePickup`:** `Inventory.AnnouncePickup` → `Other.PlaySound(PickupSound)`, a sound attached to the
    recipient pawn.
* **Ownership:**
  * The factory actors, touch, Sleeping/respawn timers and game-rule gating are Gameplay's state machine (not
    built yet on any branch). PickupPresentation keeps no timers, so it can't duplicate that state machine.
  * World exposes `pickupPresentation()`; Gameplay calls `announcePickup` + `setPickupHidden` on GiveTo and
    `setPickupVisible` when Sleeping ends.
  * The graybox scaffold pickups are untouched (Gameplay).
* **Particle systems not drawn** (Rendering + Systems): the decoded Pickup_FX / HealthPickup_FX /
  OvershieldPickup_FX streams report per-module flags (`flagA` / `flagB`, modules outside the record list) whose
  semantics pstream does **not assert**.
  * These decide whether modules run at spawn or update, or at all. One case: GlowADD has an infinite lifetime and
    an alpha-over-life of 0 at t=0, so it is invisible unless its ColorScaleOverLife is inactive or on emitter
    time.
  * Drawing them now would invent visuals. They stay UNKNOWN until the flag semantics are recovered (AssetTools /
    ReVa request).
  * `effectState()` already exposes which components the original has active.
  * Mesh note: the overshield emitter references `PROP_NEU_Pickups_p.OvershieldPickup.PROP_NEU_Overshield_STAT`,
    but the extracted mesh is `PROP_NEU_OvershieldPickup_STAT` (AssetTools check).

### Validation
* **Native suite** (`tools/systems/audio_native_suite.cpp`): **523 pass / 0 fail**. New authored-data block:
  * footstep events → table cues; no surface variants;
  * concurrency for 71 cues;
  * 7 FSB loop regions = whole sample;
  * 27 factories: spawn / hidden / visible states, highlight and attachment flags, PickupSound cue present, and
    the sound following the recipient.
* **Regression:** wfc_fidelity 194/0/19; collision 0 mismatches; probe 31 PASS / 0 FAIL / 1 KNOWN (Rendering
  `boost_fx_emitted`).
* **Audio-attach:** 247 pass / 0 FAIL / 13 KNOWN. All 13 are `PP_DECO_MECH_*` zone pool one-shots, world-fixed
  by design (count varies with random pool timing). **0 player-owned sounds left behind.**
* **Game runs:** 13 scenarios, footsteps/landing log only `BL_FS_LRG_BOT` default cues (walk, run, jump, hard land);
  repeated Boost start/loop/end and the BOOST_END duck unchanged; transform-out leaves no vehicle cue. Frame time (ms,
  range / mean): idle 6.4–10.6 / 7.0, movement 4.6–10.5 / 5.5, firing 6.7–13.1 / 10.3, sustained 6.5–13.1 / 9.7
  (PASS 5: 6.9–13.4), hover 3.7–7.7 / 4.4, Boost 3.6–8.0 / 4.6, Nitro 4.3–7.8 / 5.1. Particles/meshes → 0 after
  vehicle runs; queued events bounded.

### Dependencies
* **AssetTools:**
  * pstream module-flag semantics (or ReVa: ParticleModule spawn/update flags in the compiled stream);
  * the overshield mesh name mismatch.
* **ReVa:**
  * ChannelCountMixerPreset runtime;
  * XMA loop tables.
* **Gameplay:** the pickup factory state machine (drives PickupPresentation); vehicle jump.
* **Rendering:** drawing the 3 pickup particle systems through their materials once the module semantics are
  known; the PASS 4 material handoff is unchanged.

---

## MILESTONE 03 SYSTEMS PASS 5 — NATIVE AUDIO RUNTIME SEMANTICS (2026-10-02, agents/systems)

**Source:** `RE-Workspace/notes/MILESTONE03_AUDIO_NATIVE_FIDELITY.md`, follow-up sections A1–A8 (ReverseEngineering
commit **7c4a2e0**, read only), plus `SoundConfig.SoundMixerProperties` and the Streets `audio.json` (read only).
**Supersedes:** the provisional runtime-audio semantics of Systems checkpoint **7b42621** (PASS 4). Everything confirmed
in PASS 4 is unchanged: PreferPlayer pan reference, native spatialization, concurrency, occlusion, attachment, and the
vehicle-FX material/HDR handoff.
**Confidence:** CONFIRMED ORIGINAL · HIGH · PROVISIONAL · UNKNOWN.

### Mixer — `src/game/SoundMixer.{h,cpp}`, generated `SoundMixer.inc` (`tools/systems/gen_mixer.py`)
**CONFIRMED ORIGINAL (A2–A4):**
* **Runtime preset entry:** {Priority, FadeIn, FadeOut, Duration, RefCount, Elapsed}, plus the built-in **Default**
  preset (Priority 0, fades 0.5, Duration −1) that mixer Init enables.
* **Enable (0x82772778):**
  * Elapsed = 0 on every Enable, including re-enabling an active preset.
  * An inactive preset is inserted before the first lower-priority entry, so equal priority goes after existing
    equals and the **earlier-enabled preset wins**.
  * RefCount += 1.
* **Disable(force) (0x827728E0):** force sets RefCount to 1; at RefCount 1 the preset is removed and categories
  retarget; then RefCount = max(0, RefCount − 1).
* **Tick (0x82756180):** for active presets, last → first: Elapsed += dt. A non-Default preset with Duration ≥ 0 and
  Elapsed ≥ Duration gets a non-forced Disable.
  * Duration < 0 is infinite.
  * Duration 0 expires on the next tick.
  * RefCount n expires over n ticks.
* **Selecting each category's value (0x827661C8):** the first active preset, in priority order, that **defines** the
  category wins outright. Otherwise selection falls through, ultimately to Default. There is no adding, multiplying or
  averaging.
* **Fades:** one linear ramp per parameter (0x827560A8 / 0x827560F8), in authored units.
  * **Units:** Volume = **linear amplitude** (clamped to [0,1], never dB-converted); reverb levels in **mB**; times in
    s; frequencies in Hz.
  * **Fade time:** the new preset's FadeIn when its priority is ≥ the current one's, otherwise the outgoing preset's
    FadeOut.
  * **Interruption:** a new target restarts from the **current value** with the full new time, without snapping.
* Replaces PASS 4's per-cue preset list (fade curve, overlap combining and Duration were UNKNOWN placeholders there).
* **Applied categories:**
  * SFX_WET_VEH_ENGINE Volume: VEHICLE_JUMP −18 dB = 0.1258925 and VEHICLE_BOOST_END −4 dB = 0.6309574.
  * MASTER_WET Reverb + Echo: the 10 REVERB_TRANS_MP_STREETS_* presets.

  The backend receives the ramped MASTER_WET values every tick; the mixer ramp is the only fade.

**HIGH:** inside one tick, timers advance before ramps (vtable order AdvanceTimers +0x44 → UpdateCategories +0x48).

**UNKNOWN:**
* the IsPlayerPOV enable flag (device +0xC0 bit 0x08000000); no slice preset uses IsPlayerPOV;
* the per-parameter FMOD clamp constants of non-reverb effects;
* categories other than the two above keep Default. No slice preset defines them, so this has no audible effect.

### Zones / reverb (A1) — CONFIRMED ORIGINAL
* **`SoundMixer::activateReverb` = `SeqAct_Reverb.Activated` (0x82764858):**
  * keeps one global current-reverb slot (0x8374FCCC);
  * a different preset → Enable(new), then **explicitly** Disable(previous, force 0);
  * the same preset → no-op (no Enable, no ref-count change, no timer reset);
  * Flush (level change) resets the slot to None and leaves only Default active.
* **`AmbientAudio` zone touch:**
  * a Touch fires on the frame the local pawn starts overlapping a trigger volume;
  * Zone.Enter (0x827892D8) changes the current zone only when it differs;
  * the previous zone's scene ends (its pool stops), and the new zone's scene starts its pool and the reverb.
* **No exit restoration:** none of the 9 Streets zones links UnTouched. Leaving every volume keeps the last reverb and
  pool, and walking back from an inner volume into a still-overlapping outer one keeps the inner reverb. The PASS 4
  "INFERRED" zone stack is removed.
* **Default (dry):** before the first Touch and after a level load (`AmbientAudio::load` → mixer Flush).
* **Zone geometry checks:** previously every 0.2 s, now every frame, with Touch edges.
* **HIGH:** several Touches in the same frame are processed in zone order, so the last one wins.
* **PROVISIONAL (unchanged):**
  * the pawn is tested as a point 1 m above its origin, not as its collision cylinder;
  * the I3DL2 reverb DSP internals; the preset parameters and the path into the DSP are CONFIRMED.

### Channel modes (A5) — CONFIRMED ORIGINAL
* **k2D:** non-positional, with no WFC distance attenuation, rear attenuation or occlusion.
* **k3D:** positional, pan level 1.0, inverse rolloff, rear attenuation.
* **kSmartPan / PreferPlayer:** positional; pan level = the 2D↔3D amount; distance attenuation and SmartPanGain
  still apply.
* The mode is per cue. No slice cue authors k2D; it is validated with synthetic cues.
* PASS 4's resolveGains already implemented this; this pass only routes rear and SmartPan attenuation through the
  native dB conversion.

**UNKNOWN:**
* FMOD's internal set3DPanLevel mixing law, approximated as equal-power pan × amount (the stereo source is mixed to
  mono);
* FMOD's own 3D rolloff for these channels.

### dB → linear (A8) — CONFIRMED ORIGINAL
* `dBToLinear(x)`: clamp to [−96, 0], with x ≤ −96 giving exactly 0. `SemitonesToRatio(s)`: clamp to ±36, then 2^(s/12).
  Both use double-precision `pow` rounded to float.
* **Used for:** wave-event Volume, random volume variation, rear attenuation, SmartPanAttenuation3D and occlusion
  volume. **Not used for mixer volumes.**
* **Authored positive path:** FOLEY.SHOOT_DRY_FIRE_ELECTRICITY's variation layer (−1…+1 dB) is now capped at
  0 dB, so no boost.
* **HIGH:** the variation is converted separately and multiplied with the event Volume, since the report lists them as
  separate uses.
* **UNKNOWN:** whether the clamp applies to root-level `Volume`, which is not separately re-checked in A8. That path
  stays unclamped; all slice root volumes are ≤ 0 dB, so there is no audible difference.

### Vehicle loops (A6)
**CONFIRMED ORIGINAL, already implemented by PASS 3 VehicleAudio and re-validated:**
* looping is per wave event (bLooping → loop);
* intro, loop and outro are separate cues (BOOST_START → BOOST_LOOP → BOOST_END; DRIVE_ONLOAD / OFFLOAD);
* cue-level LoopStart / LoopEnd are unused;
* stop is an immediate linear fade (engine 0.2 s, boost 0.15 s; fade-in 0.1 s), with no loop-boundary wait;
* a state switch crossfades;
* replaying the same cue is a no-op.

**New: loop-point plumbing.**
* `IAudio::setLoopPoints(sound, startFrame, endFrame)`; Win32Audio stores a per-sample loop region (source frames
  scaled to the output rate).
* (Superseded by PASS 6: the regions come from AssetTools vehicle_audio_loops.json via VehicleLoops.inc;
  `fsb_loop_points.json` was never produced and its loader is removed.)

**UNKNOWN (AssetTools):**
* the exact FSB loop start/end samples (RESOLVED in PASS 6: whole sample, from the FSB headers).
* That fallback is FMOD's behaviour only when a sample has no header loop points, so it is **not** claimed as
  correct for the vehicle loops.
* The extracted .wav files carry no `smpl` chunk (0 of 400 `*_LP` files checked).

**HIGH:** the AudioComponent FadeIn/FadeOut curve is stock UE3 linear (per the report; not re-verified natively).

### Line / volume emitters (A7) — CONFIRMED ORIGINAL
* The PASS 2 placement already matches the native formulas, recomputed every frame:
  * **Line:** the closest point on origin ∓ X·LineLength/2 (X includes DrawScale3D), clamped to the ends.
  * **Volume:** an oriented box, with the listener's local coordinates clamped to ±Radius per axis. Inside the box,
    the source is at the listener.
* The AudioComponent sits at that point, so pan, attenuation, SmartPan distance and occlusion all use it.
* Promoted from PROVISIONAL.

### Validation
**Native-semantics suite** (`tools/systems/audio_native_suite.cpp`; recording backends plus the real Win32 backend):
**173 pass / 0 fail**.
* **Mixer** (real authored presets plus synthetic tables):
  * higher, lower and equal priority activation; per-category fall-through;
  * Duration 1 expiry with the outgoing FadeOut; Duration 0 on the next tick; Duration −1 infinite;
  * re-enable resets the timer, and RefCount 2 expires over two ticks;
  * interrupted fade continues from the current value; forced vs plain Disable; Flush;
  * MASTER_WET ramps linearly in mB, seconds and Hz.
* **Zones:**
  * all 9 Streets zones entered in sequence; exactly one REVERB_* is active after each switch (the explicit-Disable
    path), and each settled Room matches audio.json;
  * re-entering the current zone does nothing (no Enable, no environment update);
  * leaving all volumes keeps the reverb;
  * DEC_ROOM_UPPER (223) → EXTERIOR (182) fades linearly over the outgoing 0.25 s;
  * nested Touch: outer → inner → back to outer keeps the inner reverb (pair EXTERIOR / DEC_ROOM_UPPER);
  * level reset returns to Default (dry).
* **Emitters,** checked against an independent re-implementation of A7 in UE space:
  * line: perpendicular, parallel travel and beyond either end; the source moves with the listener and is never at
    the actor origin;
  * volume boxes, axis-aligned and rotated: inside → at the listener; outside → a corner point, not on a sphere;
  * occluding cues trace to the runtime point; cues authored `EnableOcclusionVolume=False` never trace.
* **Gain:**
  * −96 and −97 dB → silent, so no voice launches;
  * −6 dB → 0.5011872; 0 dB → 1; +3 dB → 1;
  * the authored ±1 dB variation never exceeds the 0 dB level.
* **Channel modes on the Win32 backend:**
  * k2D: atten 1 and pan 0 at 1000 m.
  * k3D: rolloff 0.4, pan ±1, following a moving source.
  * Cull: silent beyond DistanceMax, with no floor.
  * Rear attenuation: −6 dB behind.
  * SmartPan: pan level 0.5 with attenuation kept.
  * PreferPlayer:
    * within 14 m: pan 0, with volume from listener → true source;
    * beyond 14 m: a linear 0.5 s transition to the listener reference;
    * the source never moves.

**Game runs** (`WFC_CUELOG`, `WFC_MIXERLOG`, `WFC_SYSPROF`, `WFC_RENDERSTATS`):
* scenarios: idle, movement, Fine Aim with fire, firing, sustained firing, transform still and moving, jump, hover,
  Boost, repeated Boost (`WFC_AUTOBOOST_CYCLE`, a new test-input hook), Nitro/Ram with Dash, and transform out of the
  vehicle while its audio is active.
* **Repeated Boost:**
  * the BOOST_START, BOOST_LOOP and BOOST_END cues each play;
  * VEHICLE_BOOST_END ducks SFX_WET_VEH_ENGINE over 0.2 s and restores over 3.0 s;
  * loops stop with 0.15 s / 0.2 s fades.
* **Transform out of the vehicle:** ONLOAD stops (0.2 s) and the squeal stops (0.5 s); no vehicle cue remains.
* **Not exercised in game:** vehicle jump (DRIVE_JUMP_START / VEHICLE_JUMP). Gameplay movement has no vehicle jump;
  the preset is covered by the suite.
* **Frame time (ms, 120-frame averages):**

  | Scenario | Range | Mean |
  |---|---|---|
  | idle | 6.1–10.6 | 6.7 |
  | movement | 4.5–10.2 | 5.4 |
  | Fine Aim | 6.9–13.0 | 9.4 |
  | firing | 6.9–13.5 | 10.2 |
  | sustained firing | 6.9–13.4 | 9.6 (PASS 4: 7.2–13.4) |
  | hover | 3.8–7.8 | 4.5 |
  | Boost | 3.7–8.0 | 4.6 |
  | repeated Boost | 3.7–8.0 | 4.3 |
  | Nitro | 4.3–8.0 | 5.0 |

  Zone transition + emitter placement costs 0.005 ms/frame (suite, `-O2`).
* **Cleanup:**
  * after vehicle runs, particles and meshes → 0;
  * queued events stay bounded (idle 1–13, sustained 4–17); orphaned events are dropped;
  * live cues ≈ 25 = the ambient bed.
* **Regression:**
  * wfc_fidelity 194 / 0 / 19;
  * collision seg 0 mismatches;
  * runtime probe 31 PASS / 0 FAIL, 1 KNOWN (`boost_fx_emitted`, Rendering);
  * Experimental audio-attach 240 pass / 0 FAIL / 10 KNOWN (world impacts and zone pools; 0 player-owned left
    behind).

### Dependencies
* **AssetTools:**
  * Vehicle FSB sample-header loop start/end (RESOLVED in PASS 6).
  * Streets physical material → surface footstep and landing sounds (RESOLVED in PASS 6: no surface variation exists).
  * Pending pickup effect mesh/emitter data.
* **Rendering:** unchanged. The 4 material-only vehicle emitters and the HDR material path draw once 5e74895's
  renderer is integrated.
* **Gameplay:** vehicle jump (needed to exercise DRIVE_JUMP_START / VEHICLE_JUMP in game).
* **ReVa:**
  * the IsPlayerPOV flag;
  * the root `Volume` dB clamp;
  * FMOD set3DPanLevel law;
  * FMOD channel rolloff settings.

---

## MILESTONE 03 SYSTEMS PASS 4 — NATIVE AUDIO FIDELITY + RENDERING FX HANDOFF (2026-10-02, agents/systems)

Evidence: RE lane report `RE-Workspace/notes/MILESTONE03_AUDIO_NATIVE_FIDELITY.md` (commit 76bb0a, read-only),
Engine/HM_Engine class defaults in authored.db, and Rendering checkpoint 5e74895 (interface only).
Classification: **CONF** confirmed original · **HIGH** · **PROV** provisional · **UNK** unknown.
This pass supersedes PASS 3 where they differ; in particular the listener-only SmartPan default is gone.

### Spatialization — native `FmodAudioDevice::ComputeSourceSpatialization` (0x82759B08) [CONF]
| Term | Implementation |
|---|---|
| Source position | the AudioComponent / socket's own world location; never moved to the pawn |
| Volume | `Min / ((max(d,Min) - Min) * Rolloff + Min)`, d = listener → source |
| Cull | `max(d,Min) > DistanceMax` → silent (previously held at the DistanceMax level) |
| Rear | `× (1 - (1 - dB2lin(RearAttenuation)) * (-f))` when `f = Front · dir < 0` (previously dB-scaled) |
| Spatialization enum | 0 **k3D (class default)**, 1 k2D, 2 kSmartPan, 3 kSmartPan_PreferPlayer. The 9 unauthored slice cues are k3D (fully 3D), not SmartPan as PASS 3 assumed |
| SmartPan amount | `ds` = listener distance (kSmartPan) or **PreferPlayer reference distance** (type 3); equal 2D/3D distances → step; otherwise linear between them (reversed if 2D > 3D) |
| SmartPanAttenuation3D | gain `1 - (1 - dB2lin(SPA3D)) * amount` — new; authored −3 on vehicle cues, −6 SHOOT_LOW_AMMO, −9 SHOOT_TAIL, 0 SHOOT |
| Secondary category | by distance when EnableSecondaryCategory; no slice root enables it (class default false) → no effect here |
| dB2lin | assumed 10^(x/20) [HIGH, per report] |

### PreferPlayer pan reference (`UpdatePreferPlayerLocation` 0x8275F658) [CONF]
* `pref = P + (L − P) · ramp`, with P = local pawn origin and L = listener. The ramp targets 0 while the camera
  is within MaxPlayerSmartPanRadius 1400 UU of the pawn and 1 beyond.
* `SetTarget` gives a linear rate that reaches the target in SmartPanPreferPlayerTransitionTime 0.5 s;
  overshoot clamps.
* Only the type-3 pan amount uses it; volume and cull keep listener → true source. Always on;
  the PASS 3 `WFC_SMARTPAN_PREFERPLAYER` switch is removed.
* **Measured:**
  * Footsteps, jump, landing, transform and vehicle cues (type 3) now pan 0.00 with the camera at 8–10 m.
  * Their sources stay at the pawn origin, the socket, or AUDIO_ROOT (0.2–1.5 m from the owner).
  * k3D cues (Optimus idle foley, weapon idle notifies) keep full 3D pan −0.19…−0.29 and listener rolloff
    (0.45–0.49).
  * SHOOT stays at gain 1.0.
* [HIGH] P is our pawn mesh origin, which is also where pawn-attached sources sit. UE3 uses Actor.Location for
  both, so their relative distance is the same.

### Concurrency (`USoundCue::RegisterInstanceLimiting` 0x82E767B8) [CONF]
* `Engine.Default__SoundCue`: **MaxConcurrentPlayCount 5, InstanceLimiting kKillFarthest**.
  * None of the slice's 73 cues authors InstanceLimiting, so all are KillFarthest.
  * The 55 that author no count are limited to 5, not unlimited as before (e.g. 12 crater emitters → 5 sounding).
* Policies:
  * 0 = unlimited;
  * kKillOldest stops the oldest registered;
  * kKillNewest refuses the new sound;
  * kKillFarthest walks newest → oldest, keeping the instance at least as far (squared distance to the
    listener) as the new sound, ties → older. It stops that instance, or refuses the new sound when it is
    itself the farthest.
* Stops are immediate. Registration order = instance id. The guessed "steal oldest" is removed.
* Attached cues at one point (SHOOT at the muzzle) tie → the oldest is stopped, matching the native tie rule.

### Mixer presets (P13) — CONF parts only
* **CONF:**
  * the cue's PlayMixerPreset is enabled when its instance plays and disabled when it ends;
  * presets are ref-counted per name, priority-ordered, and re-enabling resets the elapsed timer;
  * data VEHICLE_JUMP (−18 dB) and VEHICLE_BOOST_END (−4 dB) on SFX_WET_VEH_ENGINE.
* **UNK** (not filled by ear):
  * the fade curve (linear over the authored FadeIn/FadeOut times is used as a placeholder);
  * per-category combination of overlapping presets (highest priority is used);
  * Duration > 0 expiry (not applied; the PASS 3 duration hold was removed).

### Reverb / Streets zones (P14)
* **CONF path:** FMOD I3DL2 reverb on MASTER_WET, selected by REVERB_* mixer presets. Ambient-zone Enter →
  SeqAct_Reverb → EnableMixerPreset; fade in/out 0.25 s.
* **CONF priorities:**
  * EXTERIOR 182, NEU_BASE 183, AUTO_ROOM_01 216, DEC_ROOM_LOWER 217, NEU_HALL 218, TRAIN_DEPOT 219,
    AUTO_ROOM_02 220, TRAIN_TUNNEL 221, NEU_STAIRWELL 222, DEC_ROOM_UPPER 223;
  * the enabled zone presets resolve highest-first.
* **Verified** with the real AmbientAudio and audio.json (`work/m3/zonetest`, recording backend), every switch
  at fade 0.25 s:
  * EXTERIOR −800 / 2.65 / −600;
  * NEU_BASE −650 / 2.06;
  * TRAIN_TUNNEL −900 / 4.32 / −800;
  * DEC_ROOM_UPPER −936 / 3.14;
  * AUTO_ROOM_02 −900 / 4.81 / −800.
* **INFERRED (ReVa request):** entering a zone disables the previous zone's preset (the native
  AmbientAudioZone exit path was not recovered).
* **PROV:** the reverb DSP internals (FMOD's SFX-reverb algorithm) and the Echo stage; parameters are CONF.

### Rendering handoff 5e74895 — vehicle FX
* **Materials:** every VehicleFx emitter now carries its cooked ParticleModuleRequired.Material path.
  * Sprite batches pass it as `ParticleBatch::material`.
  * When `IRenderer::evaluatesFxMaterials()`, colours are the authored HDR values (colour × colorMul ×
    colour-over-life × brightness × tint), unclamped, with colorScale 1 and no GL1 stand-ins (intensity 0.1,
    fresnel).
  * Otherwise the previous GL1 fallback runs unchanged.
* **Renderer interface:** Rendering's `Renderer.h` hunk (material field, evaluatesFxMaterials, reticle
  declarations) was applied verbatim so the merge is clean; this branch's renderer returns false until
  integration.
* **Four material-only emitters now spawn** (drawn only through their original graphs, no substitute visual):
  * hover `base_glow_Dup_Dup` (Glow_Mod_MAT);
  * hover `rays_Dup` (Trail_Distort_MAT);
  * ram `dust` (Distortion_Cloud_01_MAT);
  * ram `rays_Dup` (Trail_Distort_MAT, world space).

  LOD-0 values are CONF, roles MED.
* **Per-loop bursts:** EmitterLoops 0 emitters re-fire their BurstList every loop of EmitterDuration (Rings_Dup
  0.2 s, boost loop glow 0.5 s, base_glow 0.5 s, dust U[0.1,0.2] s) [HIGH: UE3 emitter loop semantics].
* **Bounded:** hover live parts ~250 (was ~110), all → 0 after leaving the vehicle.

### Validation
* **Scenarios:** stationary foley, movement, transform while moving, fine aim, jump/landing, hover, boost,
  dash, nitro, firing, ambience, zone transitions (`WFC_SPATIALLOG`, `WFC_AMBLOG`, `WFC_CUELOG`).
* **Attachment:** Experimental audio-attach (local spy build) — 0 player-owned left behind; 10 KNOWN =
  world pools / impacts.
* **Performance and harnesses:**
  * sustained-fire windows 7.2–13.4 ms (unchanged); ~900 RPM unchanged;
  * wfc_fidelity 194/0/19; probe 31/0/1; collision 0 mismatches.
* **Cleanup:** queued events → 0, weapon particles / meshes → 0.

### Requests
* **ReVa:**
  * AmbientAudioZone exit / previous-zone preset disable;
  * mixer applier fade curve and overlap combine;
  * Duration expiry;
  * FMOD k2D / k3D channel mode;
  * SoundNodeRoot LoopStart / LoopEnd;
  * line / volume emitter placement;
  * dB2lin exact formula.
* **AssetTools (standing):** Streets HmPhysicalMaterialProperty FootstepSounds (RESOLVED PASS 6); pickup FX mesh data
  (delivered 7a69756; see PASS 6).

---

## MILESTONE 03 SYSTEMS PASS 3 — SCRIPT-CONFIRMED VEHICLE AUDIO, LANDING RULES, SPATIALIZATION EVIDENCE, AUDIO THREAD (2026-10-02, agents/systems)

New evidence used (read-only):
* the RE lane's decompiled UnrealScript (`RE-Workspace/work/script/decomp`): HmVehicleAudioComponent,
  HmPlayerVehicleAudioComponentImpl, TnCarForm, TnTruckForm, TnAcrobaticsManager, HmFootstepComponent,
  HmPawn, SeqAct_PlayPlayerPositionalSound;
* its notes (`TARGETED_PASS2`);
* `Xe-TransEngine.ini [HM_Engine.FmodAudioDevice]`;
* cooked SoundMixerProperties and MP_IAC_Streets_AUDIO_m.

Classification: **CONF** = confirmed original, **HIGH** = high confidence, **PROV** = provisional, **UNK** = unknown.

### Vehicle audio — now a port of the original script (`VehicleAudio`)
| Behaviour | Original | Class |
|---|---|---|
| Boost start | `PlayBoostSound`: BoostSound (BOOST_START) as a looping component, FadeIn 0.1; **wheels loop only if `_IsOnGround`**, same fade | CONF |
| Boost wheels | stopped (fade 0.15) once `BoostWheelsTimer >= 0.27` and not on ground | CONF |
| Boost end | `StopBoostSound`: fade both 0.15, play BoostStopSound (BOOST_END) | CONF |
| Engine states | Boosting > JumpReving (airborne >= JumpRevTime 0.25) > Forward/Reverse On/OffLoad from MovementDirection (speed > 1 mph, `Velocity . Rotation >= 0`) and `_EngineLoadState` | CONF |
| Engine load | Hovering.UpdateSounds: 1 if stick forward > 0.01, 2 if back < -0.01, else 0 | CONF |
| Engine fades | EngineFadeIn 0.1 / FadeOut 0.2, BoostFadeIn 0.1 / FadeOut 0.15; loops faded in (new `SoundCues::fadeIn`) | CONF |
| Speed parameter | 15-sample moving average of `|Velocity| x 0.0223694` mph | CONF |
| Land | on touchdown, the highest TimeInAirThreshold (0.15 / 2.0) reached; Boosting -> wheels table, else hover | CONF |
| Hover dash | `TnTruckForm.Hovering.DoDash` -> `PlayBoosterSound` = VEH_OPTIMUS_RAM_BOOST_START (was unassigned) | CONF |
| Nitro | `StartNitro` -> `PlayNitroSound` only | CONF |
| Ram alert | no script calls `PlayCustomLoopingSound` -> **removed** (was played at nitro start) | CONF (absent from script) |
| Ram impact | `ClientPlayRammingSound` -> `PlayRamSound` on the truck: **owner-attached** (was world at the hit point) | CONF path / HIGH attach |
| Jump | `PlayAscendSound` on Hovering/Driving jumps | CONF |
| Tire squeal | `_IsOnGround && avg mph >= 20` with `_WheelSlipRatio`: Hovering feeds 0, Driving feeds `CarSimulation.SlipAngle` (Gameplay). Not gated on boost | CONF; driving input pending Gameplay |
| One-shot events | `HmPawn.PlaySoundEvent` -> `PlaySound(cue, bNotReplicated, , bStopWhenOwnerDestroyed=true)` without SoundLocation | CONF script / HIGH attach |

### Mixer presets (PlayMixerPreset) — new
`DRIVE_JUMP_START` → **VEHICLE_JUMP**: SFX_WET_VEH_ENGINE ×0.126 (−18 dB), fade in 0.3, duration 1.0, fade out 1.0, priority 270.
`BOOST_END` → **VEHICLE_BOOST_END**: ×0.631 (−4 dB), 0.2 / 1.0 / 3.0, priority 264.
Values CONF; envelope and priority resolution HIGH. Verified: boost end triggers the duck.

### Robot landing / footsteps
* **[CONF] Landing selection** (TnAcrobaticsManager):
  * `FallDistance = _FallBaseHeight - Height`, where `_FallBaseHeight` is set by `Falling.BeginState` (the
    ledge, or the jump apex: Jumping → FallingFromJump on descent) and on ground.
  * `ForwardSpeed = |Velocity . Rotation|`.
  * First `LandingAnims` match in array order; none while transforming / meleeing.
  * Implemented exactly (the speed was previously horizontal magnitude).
  * Standing and running jumps fall 514 UU → Nav_Land_02 → FS_LAND_HARD.
* **[CONF] Footsteps** (HmFootstepComponent):
  * FootstepType 0 → Walk, 4 → Run, 1 Scuff, 3 Land, 10 HardLand; no speed-based choice.
  * The surface PhysicalMaterial's HmPhysicalMaterialProperty.FootstepSounds override the defaults.
  * None are in authored.db (0 objects), so the component defaults apply. CONFIRMED AUTHORED in PASS 6: every
    Streets surface resolves to these defaults.
* **Landing loudness, proved from data:**
  * The milestone-02 "soft/squishy" landing was the wrong cue (`RELOAD_AIR_RELEASE_THUMP` placeholder,
    Experimental's spy log).
  * The authored FS_LAND_HARD is −5 dB root (main layer), servo −4 (var −3) and groan −4 (var −6), category
    SFX_WET_NAV (1.0), DistanceMin 15 m. Ion Blaster SHOOT is −9 dB root × its distance curve, same
    category gain. A hard landing is therefore authored ~4–6 dB above a single shot.
  * No class or attenuation error was found. Levels unchanged.

### Player-owned spatialization (PRIORITY 1)
* **Evidence:**
  * 32 of the slice's 41 table cues are `kSmartPan_PreferPlayer`; 9 author none (class default enum UNK).
  * `[HM_Engine.FmodAudioDevice] SmartPanPreferPlayerTransitionTime=0.5, MaxPlayerSmartPanRadius=1400.0`
    (also `MaxChannels=96`, `OcclusionCheckInterval=0.25`).
  * The names indicate PreferPlayer SmartPan is measured from the local player while the player is within
    14 m of the listener, which would centre the player's own sounds. The native code is not decoded.
* **Default kept: listener (camera) reference — PROV.**
  * `WFC_SMARTPAN_PREFERPLAYER=1` enables the player-referenced model (ramp 0.5 s, 1400 UU radius) for A/B.
    Verified: footsteps pan −0.29 → 0.00, weapon −0.13 → −0.06.
* **`k2D` cues play non-positional and unoccluded [CONF]:** only `PP_OPEN_ROOMS` (AUTO_ROOM_02 pool).
* **Instrumentation** `WFC_SPATIALLOG=1` (every 0.25 s per attached instance): cue, owner/socket, source,
  owner position, source–owner distance, listener, distance, pan, attenuation, occlusion, channel gains —
  read back from the mixer.
* **Measured** (walk, transform, jump, boost, fire):
  * Sources stay 0–0.3 m from the pawn origin; the weapon sits 2.7–3.9 m out (the muzzle/hand socket);
    vehicle cues sit at AUDIO_ROOT 1.5 m.
  * Camera listener 5–10 m away; own sounds pan −0.29 (Optimus left of centre), vehicle 0.00 (inside its
    2–10 m SmartPan band).
  * Footstep / transform / vehicle distance gain 1.0; weapon idle notifies 0.43 (DistanceMin 4 m).

### Streets environment
* **[CONF] Structure:** no ReverbVolume / AudioVolume actors in Streets; the environment is 9
  SeqAct_AmbientAudioZone + 9 SeqAct_Reverb on 10 TriggerVolumes.
* **[CONF] Zone pools** (SeqAct_PlayPlayerPositionalSound script):
  * first delay `Rand(DelayMin, DelayMax)` on START, re-rolled per play, STOP ends scheduling;
  * position: random yaw 0–359°, `Rand(DistanceMin, DistanceMax)` in the horizontal plane, world-fixed
    component;
  * reference = "Source Actor" variable, else `AudioDevice.Listeners[0].Location`. No variable is linked
    [HIGH], so the reference is now the **listener** (was the pawn).
* **PROV:** the reverb DSP topology is still my reconstruction from authored I3DL2/Echo parameters; FMOD Ex's
  SFX reverb implementation is not recoverable from local data (ReVa request). Fade and wet/dry routing per
  category are CONF.

### Ambient emitters
* **[CONF]** 40 AmbientSound AudioComponents: bAutoPlay, VolumeMultiplier 1, PitchMultiplier 1,
  bAllowSpatialization, bUseOwnerLocation.
* **[CONF] Concurrency:** MaxConcurrentPlayCount 3 on EMIT_FLOOD_LIGHTS (5 emitters) and EMIT_MONRAIL_IDLE_LP
  (from the cooked cues; audio.json lacks it). The virtualizer keeps the 3 most audible.
* **PROV:**
  * shaped-emitter placement (GetLinePoints / GetExtents are native);
  * the 24-voice budget and 0.5 s fades;
  * always-on activation of the 30 shaped emitters (no component data).

### SoundCue nodes (PRIORITY 4)
* **[CONF] Nodes in use:** the slice cues use only SoundNodeRoot (41) → SoundNodeWaveEvent (134) →
  SoundNodeWaveEx (262); Streets uses the same three classes.
* **No other node types exist in the slice:** random, mixer, modulator, concatenator, delay, attenuation and
  distance-crossfade nodes are absent. Their roles are fields of these classes and are implemented:
  * random variant choice and ChanceToPlayNone;
  * volume/pitch variation;
  * event `Time` delays;
  * bLooping;
  * distance attenuation and SmartPan;
  * VolumeCurve over SOUND_DISTANCE (distance crossfade);
  * Envelope.
* **Authored but not modelled:**
  * LoopStart/LoopEnd (8/11 vehicle roots, semantics native: UNK, ReVa);
  * SmartPanAttenuation3D (19), EnableDoppler (15), Priority / OverridePriority / PlayWhenSilent;
  * NonLocalPlayerPitch (remote players only);
  * SecondaryCategory (all None in the slice).

### First-play audio cost (PRIORITY 5)
* **Measured:** cue `play()` 0.05–0.2 ms, wave decode at load only, mixing 0.7–1.4 ms per 21 ms block.
* **The cost was `waveOutWrite` blocking inside the driver on the game thread:** 17–19 ms at device start,
  and **~170 ms** when the 4-block queue had drained during load (driver restart).
* **Fixed:** mixing and submission run on a dedicated audio thread (2 ms pump; game-thread calls take a short
  mutex; `waveOutWrite` outside the lock; wave storage made reference-stable).
* **Game thread now:** no audio stall. The remaining first-frame hitch (~520 ms at tick 4) is Rendering's
  first-use shader/texture builds.
* **Prewarm:** the original loads the level's banks with the map, which is equivalent to our load-time decode;
  no extra preloading.

### Occlusion validation (PRIORITY 6)
* **Rays:** 76–192 occlusion rays/s (4/s per live instance at the authored 0.25 s); Systems cue section
  0.05–0.17 ms/frame.
* **Player-owned:** body-referenced, 2 of ~300 standing shots and 0.8 % while walking into walls muffled.
* **Map emitters:** about half the live instances are occluded at the indoor spawn.

### Cleanup (PRIORITY 8)
* **Suite:** idle, burst, sustained fire (full magazines + reloads), boost/nitro/transform back, transform
  cycling, fire+move+turn.
* **Results:**
  * queued events return to 0;
  * weapon particles / meshes → 0;
  * vehicle parts → 0 after leaving the vehicle;
  * cue instances settle at the ~25 ambient loops;
  * level FX steady at ~30 particles.
* No caps added.

### Performance
* Idle 6.1 ms (audio off the game thread).
* Sustained-fire windows 7.7–13.6 ms.
* Boost / nitro / back 5.2–8.4 ms; transform cycling 5.0–5.9 ms.
* ~900 RPM unchanged.
* wfc_fidelity 194/0/19; runtime probe 31/0/1; collision 0 mismatches.
* Experimental audio-attach (local spy build): 0 player-owned left behind; 11 KNOWN = world pools/impacts.

### Requests
* **ReVa:**
  1. `FmodAudioDevice` SmartPan PreferPlayer code (the reference point and radius use).
  2. SoundNodeRoot LoopStart/LoopEnd playback (is a looping component looped over that region?).
  3. `HmAmbientSoundLineEmitter.GetLinePoints` / `HmAmbientSoundVolumeEmitter.GetExtents` and emitter
     activation.
  4. FMOD SFX reverb / DSPEffectConfig bit map.
  5. MaxConcurrentPlayCount resolution (steal vs reject).
  6. SpatializationType enum order (class default).
* **AssetTools:**
  1. Streets PhysicalMaterial → HmPhysicalMaterialProperty.FootstepSounds (RESOLVED PASS 6).
  2. Cue-level MaxConcurrentPlayCount in audio.json (RESOLVED PASS 6: values match).
  3. (standing) pickup FX mesh data.

---

## MILESTONE 03 SYSTEMS PASS 2 — WORLD SOUND BED, ZONE REVERB, MIXER, LEVEL FX, ATTACHMENT AUDIT (2026-10-02, agents/systems)

### Attached-audio model (reusable; nothing hard-coded per cue)
`SoundCues::Emitter{pos, owner, offset, socket}` with a World resolver (`World::resolveCueOwner`):
| Class | Original mechanism | Owner | Examples | Conf |
|---|---|---|---|---|
| A actor-attached | HmAnimNotify_Sound / HmAnimNotify_SoundEvent (no SocketName), HmPlayerVehicleAudioComponent | `kOwnPawn` (+ world offset, e.g. AUDIO_ROOT +147.25 UU) | transform, footsteps, landing, idle foley, vehicle boost / engine / jump / land / nitro / alert | CONF class / HI attach |
| B socket-attached | notify SocketName / weapon sockets | `kOwnPawn` + bone, `kOwnWeapon` + socket | SHOOT / SHOOT_TAIL at MuzzleFlash, reload / idle notifies on the weapon mesh, fine aim | HI |
| C persistent loops | vehicle audio component loops | attached as A | engine ONLOAD / OFFLOAD / JUMP_LOOP, boost loop, tire squeal | CONF |
| D world one-shots / world loops | impacts at the hit, map AmbientSound / shaped emitters, Kismet PlayerPositionalSound pools | `kWorld` | IMPT_WORLD / IMPT_DMG / RAM_IMPACT, 70 map emitters, PP_* pools | CONF |
| E non-positional | — (no slice cue needs it) | `kUI` | API only | — |

Delayed wave events start at the owner's position at launch time. Voices of attached instances follow the owner
every tick until the voice ends (`IAudio::isPlaying`; a backend that cannot report keeps updating to the
10 s bound). A holstered weapon (mid-transform) resolves to the pawn carrying it.
**Measured with Experimental's `audio-attach.ps1`** (their recording backend built locally against this
branch, same six scenarios; not committed):
* milestone-02: 20 KNOWN "left behind".
* now: **0 player-owned left behind**. The 19 remaining KNOWN are world sounds that must stay put:
  Ion Blaster `IMPT_WORLD` impact layers (MTL_BULLET_IMPT_SHEET_*, ELEC_SPARKBLAST_FLANGE_*) and the
  `PP_CORRIDORS` zone pool (PP_DECO_MECH_*).
* Experimental handoff: classify `IMPT_*` / `PP_*` voices as world.
* `WFC_CUETRACK=1` logs the transform cue position vs the pawn: it tracks within one step (≤0.26 m at
  14 m/s) in both directions.

### SoundCue runtime
* **Node types:** the slice uses only SoundNodeRoot → SoundNodeWaveEvent → SoundNodeWaveEx. That covers 41
  table cues + 32 map cues: weapon, Optimus, vehicle, transform, Streets. No mixer / concat / modulator /
  attenuation nodes exist in WFC cues; the root carries attenuation and the wave event carries randomization,
  delay (Time), looping and curves.
* **Supported now:** root volume/pitch + variation, DistanceMin/Max + RolloffFactor (FMOD inverse), SmartPan
  2D/3D, **RearAttenuation**, Category (wet/dry routing), SoundParameter (distance / speed / tire slip);
  event Time, volume/pitch + variation, ChanceToPlayNone, bLooping, random wave choice, Volume/PitchCurve,
  Envelope; **5.1 pan matrix folded to stereo** (Default__SoundNodeWaveEvent: Center/BackL/BackR/LFE −96 dB;
  rear-only layers −3 dB [MED]).
* **Data-loaded cues:** cues load from the generated table and from a map's `audio.json`.
* **Not used by any slice cue (not implemented):** root DelayMin/Max, LoopStart/LoopEnd (2 map cues carry the
  class-default LoopEnd), Doppler, SecondaryCategory. Occlusion: see "Occlusion" below.

### Attenuation / panning (audit)
* **Source:** each instance's resolved 3D position (owner, socket or world).
* **Listener:** the camera pose given to `IAudio::setListener` each frame (position, forward, right).
* **Gain:** FMOD inverse rolloff `min/(min + rolloff·(d−min))`, flat inside DistanceMin, held past DistanceMax
  (FMOD Ex inverse semantics).
* **Pan:** equal-power `dot(dir, listenerRight)` (sin of the azimuth, gentler than FMOD's speaker-angle pan,
  no exaggerated separation), blended to centre inside SmartPanDistance2D and full beyond SmartPanDistance3D.
* **Rear:** RearAttenuation dB for sources behind the listener.
* **Not world origin, not player-based.** [MED] kSmartPan_PreferPlayer may measure the local player's own
  sounds from the pawn rather than the camera (that would make own sounds more centred); unresolved without
  native RE.

### Mixer / sound class (gain staging)
* **Routing:** voices route to MASTER_WET (every `SFX_WET_*` category; all slice cues) or MASTER_DRY, as in
  `SoundMixerProperties.SoundGroupCategoryMappings`.
* **Category volumes:** all 1.0 on the slice's paths (see pass 1), so no relative category gain.
* **Master compressor:** Master's Default preset, Threshold −6 dB, Attack 10 ms, Release 50 ms, GainMakeup 0.
  Read from audio.json; applied as a hard-knee limiting compressor [MED: DSPEffectConfig bit 32 read as the
  compressor; FMOD Ex compressor ratio not documented].
  * Measured peaks: −19…−15 dBFS idle, −8 dBFS sustained fire, −7 dBFS fire + movement. It does **not**
    engage in these scenarios, so the weapon mix is unchanged.
* **Master level:** Master's own Default volume is 0.708 (−3 dB); the rebuild keeps master 0.5 [PROV] (the
  original's FMOD output scaling is unknown), so the relative mix is what is reproduced.

### Environment / reverb
`AmbientAudio` loads ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/audio.json at runtime.
* **9 Kismet zones:**
  * TriggerVolume polygons, a ray-parity point test on the pawn (camera ignored, as authored), checked every 0.2 s.
  * Entering a zone applies its `REVERB_TRANS_MP_STREETS_*` MASTER_WET preset (Reverb + Echo) with the
    preset's FadeInTime (0.25 s) and starts its PlayPlayerPositionalSound pools (looping random one-shots,
    DelayMin–Max, 20 m from the player, world-fixed, [MED] random horizontal bearing).
  * Leaving without entering another keeps the zone (INFERRED, no on_untouched ops).
  * The default spawn is in DEC_ROOM_LOWER.
* **Reverb DSP [MED structure, CONF parameters]:**
  * send HF shelf at HFReference/RoomHF; 4 early-reflection taps from ReflectionsDelay at Room+Reflections mB;
  * stereo 8-comb + 4-allpass late tank at Room+Reverb mB, comb feedback for the authored DecayTime (RT60),
    DecayHFRatio as in-loop damping, Diffusion as allpass gain, pre-delay ReflectionsDelay+ReverbDelay;
  * Echo: Delay / DecayRatio / WetMix / DryMix;
  * parameters cross-fade over the preset fade.
* **70 map emitters:**
  * 40 point, 17 volume, 13 line, all looping map-bank cues at their authored volume/distances.
  * Volume emitters sound from the nearest point of their box (Radius × actor scale) to the listener, and on
    the listener when inside (the 7 AMB_* room-tone beds); line emitters from the nearest point of their segment [MED].
  * [PROV] voice budget: the 24 most audible play, the rest are virtual; 0.5 s fades in/out.
* **Cost:** mixer 0.7 ms per 21 ms block (1.1–1.4 ms with ~70 voices), about 0.2 ms per frame.
* **Diagnostic:** `WFC_AMBLOG=1` logs zone, emitters, voices, peak, gain reduction and mix cost.

### Occlusion [CONF parameters, MED trace geometry]
* **Parameters:**
  * Engine: `AudioDevice.bEnableOcclusion = true`, `OcclusionCheckInterval = 0.25 s` (Xe-TransEngine.ini).
  * Per cue: `SoundNodeRoot.EnableOcclusionVolume` defaults to true; only the BL_TRANSFORM cues author it off.
  * Per surface: `PhysicalMaterial.AudioOcclusionVolume = -6 dB`, `AudioOcclusionTransitionTime = 0.5 s`. That
    is the class default, used by 61 of 63 materials (one sets the same values explicitly, one 0 / 0);
    `AudioOcclusionPitch` is unset → no pitch change.
* **Implemented:**
  * Every occluding instance re-checks a listener → source line against the collision mesh every 0.25 s
    (staggered), and fades to -6 dB over 0.5 s.
  * New instances start at the current state.
  * The collision mesh carries no physical materials, so the default applies everywhere.
* **[MED] trace geometry:**
  * Attached (player-owned) sounds are tested against the pawn body (mesh origin + 1.5 m) rather than the
    socket. The gun / arm have no collision and the muzzle can poke into walls: testing the socket occluded
    26 % of shots while walking into walls, the body test 0.8 %.
  * The last 0.5 m at the source and 0.25 m at the listener are ignored (floor under the feet, an emitter's
    mounting surface).
* **Effect:**
  * Sustained fire standing: 2 of ~300 shots occluded; the weapon mix is unchanged in normal play.
  * About a third of the map emitter / pool instances are occluded behind walls at the spawn.

### Human-validation aid
With the debug overlay (B or `WFC_DEBUGDRAW=1`), every live sound source draws as a wire cube: green = pawn-attached,
yellow = weapon-attached, blue = world, red = occluded. `WFC_CUELOG` lines now carry the occlusion level.

### Pickup / objective FX — not implemented (request)
`map_fx.json` has 37 more authored particle components on pickup factories:
* `Pickup_FX` ×27 on ammo, health, flag, bomb and overshield factories;
* `HealthPickup_FX` ×9;
* `OvershieldPickup_FX` ×1.

They are not instantiated, because:
* the factories themselves are not in the rebuild (Gameplay owns pickups: placement, availability, respawn);
* their emitters with no material are mesh emitters whose TypeDataMesh / mesh is not in map_fx.json;
* several of their vector distributions are ambiguous between size and scale.

**AssetTools request:** TypeDataMesh (Mesh, bOverrideMaterial) and the per-emitter module class list, if any
survives, for FX_Pickups_p.FX.{Pickup_FX, HealthPickup_FX, OvershieldPickup_FX}.
**Gameplay request:** pickup factory actors from spawnpoints.json, with an availability flag the FX can follow.

### Robot movement / landing (pass-1 work kept; status)
* The landing cue already follows the authored data: LandingAnims by fall height (and horizontal speed) →
  FS_LAND_DEFAULT / FS_LAND_HARD / FS_LAND_HIGH_FALL + groan.
* Gameplay still plays Nav_Land for every landing and exposes no landing-clip state, so Systems evaluates the
  same authored table (handoff below).
* Footstep notifies: only the Strafers master fires, MinWeight-gated, with no idle/walk flicker duplicates.
* "Too loud / disconnected": the old placeholder thump (0.8 linear, linear rolloff) is gone. Footsteps now use
  authored SmartPan 75/150 UU, sit in the zone reverb and pass through the master compressor; no level was
  changed by ear.

### Vehicle sound bed (state map)
| State | Cues (authored) | Notes |
|---|---|---|
| Hover idle / movement | DRIVE_OFFLOAD (no throttle) / DRIVE_ONLOAD (throttle), speed-keyed pitch, 0.2 s EngineFadeOutTime | gear set MaxSpeed 20 / 110 share the cues |
| Normal boost | BOOST_START, BOOST_LOOP (speed), BOOST_END, BOOST_WHEELS after 0.27 s on ground, 0.15 s fade | engine yields to the boost loop [MED] |
| Hover dash | none authored (no dash clip / notify; BoosterSound trigger is native, undecoded) | not invented |
| Ram / nitro | RAM_NITRO_START + VEH_TRUCK_RAM_ALERT (one-shot, cut at nitro end) | RAM_IMPACT via notifyRamHit (world) |
| Jump / air | DRIVE_JUMP_START (AscendSound) + DRIVE_JUMP_LOOP (JumpRev) | |
| Landing | HOVER_ / WHEELS_LAND_LIGHT (>=0.15 s air) / _HEAVY (>=2.0 s) | |
| Transform | BL_TRANSFORM BOT2VEH / VEH2BOT attached; vehicle FX on at 1.8 s of the fold | |
| Tire squeal | VEH_OPTIMUS_TIRE_SQUEAL, parameter = slip angle 0..pi/2 rad (Max 1.57), full volume at 0.425 rad, pitch −1 → +2 st, 0.5 s crossfade, speed >= 20 | **hook only** (below) |
All vehicle loops are attached (AUDIO_ROOT) and move with the truck.

**Tire squeal — prepared, not invented.** Gameplay exposes no slip scalar. Heading vs horizontal velocity measured
in Systems reads 0.3–1.2 rad in straight-line Driving in the current model (velocity not aligned to yaw), so it is
not a usable slip signal. The squeal plays only when Gameplay calls `World::setTireSlipAngle(rad)` each step
(Driving, grounded, >= 20 mph). `WFC_TIRESLIP_DERIVED=1` enables the measured value for diagnostics only.

### Level effects
`LevelFx` reproduces the 8 authored Streets level emitters (`Emitter` actors with `FX_Level_Generic_p.FX.Steam_Sm_FX`,
map_fx.json):
* emitter "Smoke_Dup", Steam_Mat → SmokeBall_CLR translucent, emissive ×0.5;
* LOD0 stream values CONF, roles by the established order MED: spawn U[2,3]/s, life U[1,2] s, size U[6,10] m
  ×1→3, alpha 0→0.3→0, velocity ±(3,1,1) m/s, ±5 m along the emitter X, colour (0.9,0.9,1)→1;
* UE location/yaw → glTF as the other map records.
Verified in fixed-camera stills (`work/m3/steam_sheet.png`). Not reproduced (Rendering): the material's panned
SmokeTile UV distortion and depth-biased (soft) alpha.

### Vehicle FX event completeness
OptimusTruckForm authors exactly BoostFx (BoostSocket_L/R), HoverFX (6 HoverBooster_*), JumpFX (JumpBoostSocket_C/R/L)
and RamFX (RamSocket); TnCarForm / TnTruckForm / TnVehicleForm defaults add none (no dash or landing FX).
* **Driven:** Hover while hovering and from 1.8 s of the to-vehicle fold; Boost while Driving; Jump on take-off,
  killed when the vehicle form ends; Ram for the nitro.
* **Cleanup:** after transforming back to robot all vehicle parts drain to 0.

### Performance (final suite, RX 7900 XTX, WFC path)
* Idle 6.4–6.5 ms (ambient bed, reverb and level FX included).
* Short burst + reload 6.5 ms.
* Sustained fire: firing windows **9.6–13.6 ms** (drawFx 2.4–5.9 ms = Rendering's per-shell light environments).
* Boost / nitro / transform back 6.3–8.7 ms; transform cycling 5.5 ms; fire + move + turn 5.7–8.9 ms.
* ~900 RPM cadence unchanged.
* Leaks: cue instances settle at the ~25 ambient loops; pending events bounded; weapon particles → 0 after
  firing; vehicle parts → 0 after leaving the vehicle; level FX steady at ~30 particles.
* Collision: `work/segtest` 0 mismatches.
* Harness: wfc_fidelity 194/0/19/119/1, runtime probe 31/0/1.

---

## MILESTONE 03 SYSTEMS — AUDIO OWNERSHIP, ROBOT MOVEMENT SOUND, TRANSFORM AUDIO, FIRING COST (2026-10-02, agents/systems)

### Audio source ownership (systemic fix)
Every SoundCue instance now has an **owner**. `SoundCues::Emitter{pos, owner, offset}` with a World resolver:
`kWorld` (fixed position), `kOwnPawn` (pawn mesh origin + offset, e.g. the truck AUDIO_ROOT +147.25 UU),
`kOwnWeapon` (Ion Blaster mesh), `kOwnMuzzle` (MuzzleFlash socket). Attached instances re-resolve their
position **every tick for every voice**: one-shots, loops and *delayed wave events*, which now launch at the
owner's current position. Before this, one-shot voices were frozen where they started. That was the
"transform sound stays behind" defect, and it applied to every non-looping cue. One-shot instances now live
exactly as long as their voices (`IAudio::isPlaying`, default false; Win32 implements it), so a long attached
wave keeps following to its end.

| Source | Original mechanism | Owner now | Conf |
|---|---|---|---|
| Ion Blaster SHOOT / LOW_AMMO / SHOOT_TAIL | TnWeapon WP_Fire / WP_LoopingTail on the weapon | muzzle (attached) | HI |
| Reload / idle weapon notifies | HmAnimNotify_Sound on WEP_IonBlaster_ANIM (no socket) | weapon mesh | CONF class / HI attach |
| IMPT_WORLD / IMPT_DMG, VEH_TRUCK_RAM_IMPACT | impact at hit location | world | CONF |
| Transform BOT2VEH / VEH2BOT | HmAnimNotify_Sound on the Optimus transform clips (no SocketName, bStopWhenActorDestroyed) | pawn | CONF |
| Footsteps / scuffs / jump / landing / idle + pivot foley | AnimNotify_Footstep, HmAnimNotify_SoundEvent, HmAnimNotify_Sound on the robot clips | pawn | CONF |
| Fine aim START / END | TnWeaponIonBlaster WP_StartFineAim / WP_EndFineAim | weapon | CONF cue / HI attach |
| Vehicle boost / engine / jump / land / nitro / alert | HmPlayerVehicleAudioComponent on OptimusTruckForm | pawn @ AUDIO_ROOT | CONF |

**Per-cue SmartPan [CONF]:** `SoundNodeRoot.SmartPanDistance2D/3D` (class default 400/800 UU) is now read per
cue. The player previously used 200/400 UU for every cue. Footsteps author 75/150 UU and FS_LAND_HIGH_FALL
1000/1500, so close movement sounds are placed at the actor instead of mixed nearly centred. Vehicle cues are
200/1000; transform and idle foley use the default 400/800. **The Ion Blaster fire cues author exactly
200/400, so the weapon mix is unchanged.**

**Mixer categories [CONF, `SoundConfig.SoundMixerProperties`]:** 47 categories with DSP presets. Each cue's
`Category` is now emitted into the table (SFX_WET_COMBAT_ROBOT_WPN, SFX_WET_NAV, SFX_WET_COMBAT_TRANS,
SFX_WET_VEH*). Every category on these cues' paths has a `Default` preset volume of **1.0**. The exceptions are
**Master 0.708 (−3 dB)**, MUSIC_DRY 0.708 and SFX_SWORD_HUM 0.501, so the original mixer adds **no relative
category gain** between weapon, movement, transform and vehicle sounds. Reverb lives on `MASTER_WET` zone
presets (map Kismet zones): audited, **not implemented** (out of scope this pass). The rebuild's own master
level 0.5 [PROV] is not the original −3 dB; it is left unchanged to keep the weapon mix.

Still MED: `kSmartPan_PreferPlayer`. Pan and attenuation are measured from the camera listener; the original
may measure the local player's own sounds from the pawn. EnableOcclusionVolume/Pitch (on by default, off on the
transform cues) and Doppler are not modelled.

### Transformation audio [CONF]
* Robot→vehicle: `Transform_ToVehicle_ROBO` HmAnimNotify_Sound **BL_TRANSFORM.OPTIMUS_BOT2VEH @0.125 s of 2.0 s**.
  The cue is −4 dB, 1500–15000 UU, category SFX_WET_COMBAT_TRANS, and layers six wave events:
  servos 0.0, main 0.152, truck land thump (3 variants) 0.642, boost flare 1.440, air release 1.586, boost
  finish 1.674 s.
* Vehicle→robot: `Transform_ToRobot_ROBO` **BL_TRANSFORM.OPTIMUS_VEH2BOT @0.0** (MinWeight 0). The cue is −7 dB:
  servos and a truck light impact (3 variants) at 0.0, main at 0.141.
* Fired by fold progress (notify time / authored length), attached to the pawn. The generic
  `EVENT_IACON_BRIDGE_TRANSFORM_GEARS.wav` placeholder and `World::playSfx` are removed.
* Measured while moving: the delayed layers start at the pawn's current position (VEH2BOT main layer 0.15 s
  later at 358.3,−345.0 vs 360.0,−344.0 at the start). Runtime probe `transform_cue_is_authored`: KNOWN → PASS.
* Vehicle FX: `Transform_ToVehicle_VEH` TnAnimNotify_ToggleVehicleFx enables at **1.8 s** of the 2.0 s fold.
  Hover FX now start there instead of at fold completion; `Transform_ToRobot_VEH` disables them at 0.0.

### Robot movement sound [CONF data; MED where noted]
Chain: AnimNotify on the playing clip → `HmFootstepComponent` default type→event → `Optimus_ROBODEF.SoundEventSet
= SoundEvents.CHR_OPTIMUS` → `BL_FS_LRG_BOT.*` (large-robot footsteps).

| Event | Clip notifies (authored s / length) | Cue | Root dB, distance, SmartPan |
|---|---|---|---|
| run step (kFootstepRun) | Nav_StrafeJog_F 0.091 / 0.513 of 0.767 (B 0.194/0.543, L 0.137/0.523, R 0.132/0.529) | FS_RUN_DEFAULT | −8 (var −3), 1500–15000 rolloff 2, 75/150 |
| walk step (kFootstep) [MED: →WALK] | Nav_StrafeWalk_F 0.238 / 0.855 of 1.133 (B/L/R similar) | FS_WALK_DEFAULT | −12 (var −3) |
| scuff + steps + servo groan | Nav_IdlePivot90_L/R | FS_SCUFF_DEFAULT, FOLEY_FS_GROAN_SERVO_01 (−19) | |
| jump | Nav_TakeOff_01 FS_DEFAULT_JUMP @0 | FS_JUMP | −10 |
| land | Nav_Land kLand @0 | FS_LAND_DEFAULT | −8 |
| hard land | Nav_Land_02 kHardLand @0 | FS_LAND_HARD | −5 |
| high fall | Nav_Land_03 FS_DEFAULT_LAND_HIGH_FALL + kHardLand @0, groan @0.432, Long_Fall_Landing_1_FX | FS_LAND_HIGH_FALL (0 dB, SmartPan 1000/1500) + FS_LAND_HARD | |
| idle foley | Optimus NAV_Idle BL_FOLY_IDLES.OPTIMUS_IDLE @0 (15 events) | OPTIMUS_IDLE | −21, 650 UU |

* **Heavier-landing threshold exists [CONF]:** `TR_Acrobatics_p.SharedAcrobatics.LandingAnims` (MinHeight /
  MinSpeed UU) are {1200,1200} Nav_Land_03, {1000,1200} Nav_Land, {4500,0} Nav_Land_03, {500,0} Nav_Land_02
  and {250,0} Nav_Land. A fall under 250 UU plays no landing anim and so no landing sound.
  [MED] They are tested in array order on apex→touchdown height and horizontal speed. A standing jump
  (514 UU) → Nav_Land_02 → **FS_LAND_HARD**.
* **The old landing sound was not the original.** `WL_GUN_FOLEY/RELOAD_AIR_RELEASE_THUMP.wav` played at 0.8
  linear for every robot landing, with a linear 5–50 m rolloff. That is the "soft/squishy, too loud" sound.
  It is replaced by the authored cues above.
* [MED] Only the Strafers sync master fires notifies (AnimNodeSynch bFireSlaveNotifies default false), gated
  by the master weight vs MinWeight (default 0.25). Non-looping clip notifies are gated by the blend-in
  reaching MinWeight (Idle↔Moving 0.2 s, pivot 0.1 s), so a one-step idle flicker does not fire them.
* Runtime: jog 14 m/s gives two FS_RUN steps per 0.767 s cycle at phases 0.119 / 0.669.
* `Character` gained read-only `locoPhase()` / `locoMasterWeight()` (Gameplay file, additive).

### Vehicle sound bed
The three mechanics stay distinct. **Normal boost** uses BOOST_START / LOOP (speed parameter) / END / WHEELS
(0.27 s ground check). **Nitro** uses RAM_NITRO_START + VEH_TRUCK_RAM_ALERT. **Hover dash** has no authored cue:
no dash clip and no dash notify exist in Optimus_VEH_ANIM, and `BoosterSound` (VEH_OPTIMUS_RAM_BOOST_START, a
7 s cue with LoopStart 6.62 / LoopEnd 7.20) is not referenced in TransGame script, so its trigger is native and
undecoded. It is not assigned to the dash. The engine (ONLOAD / OFFLOAD / JUMP_LOOP, speed-keyed pitch) and
the land cues (hover vs wheels, 0.15 / 2.0 s) are unchanged; all vehicle cues are now attached at AUDIO_ROOT.
Correction to the M02 note: **VEH_TRUCK_RAM_ALERT's wave event is authored non-looping**. It plays once per
nitro and is cut if still sounding when the nitro ends; it never looped forever.

### Vehicle FX
Driven at the authored sockets as before (BoostSocket_L/R, 6 × HoverBooster_*, JumpBoostSocket_C/R/L,
RamSocket) from decoded templates. The only change is the ToggleVehicleFx timing above.
Not reconstructed (authored, but not reachable or not on Optimus):
* Nav_Land_03 `FX_Navigation_p.Long_Fall_Landing_1_FX` @BoosterSocket_R (high falls only).
* Robot dodge `DashPulse_1_FX` (dodge unreachable, Gameplay).
* `Trails_Bumblebee_FX` (sockets not on the Optimus mesh).
The "crude" look of the hover/boost rings is material/blend treatment → Rendering handoff.

### Firing performance [measured, RX 7900 XTX, WFC path, sustained auto-fire]
| | avg frame | Systems hitscan | controller (incl. Gameplay camera ray) | drawFx |
|---|---|---|---|---|
| before (milestone-02 head) | **65–70 ms** (peaks 70) | 21–26 ms | 32–52 ms | 3.5 ms |
| after | **10–11 ms** (idle 6.4–7.1) | 0.02 ms | 0.02–0.06 ms | 3.5–4.9 ms |

* Root cause: `CollisionWorld::segmentHit` tested **every grid cell of the segment's XZ bounding box**. The
  300 m weapon trace and Gameplay's per-shot camera ray each scanned thousands of 2 m cells per call.
* It now walks only the cells the ray crosses (2D DDA, clipped to the grid) and stops at the first cell whose
  exit lies beyond the nearest hit. It is exact against a brute-force all-triangle reference: 20,000 random,
  vertical and axis-aligned segments, 0 mismatches (`work/segtest`).
* The renderer's light-visibility callback no longer marches 2 m pieces.
* Cadence untouched: ~900 RPM, one-shot timer.
* No leaks: particles, mesh parts, cue instances and pending events all drain to 0 after firing (4000-frame run).
* Remaining firing cost is Rendering's: each shell/magazine mesh particle gets its own dynamic light
  environment (computeEnv with visibility traces against 268 lights), ~0.25 ms per mesh part, 3–5 ms with 15
  live. CPU skinning in drawPlayer is 3.2 ms.

---

## MILESTONE 68 — SPRITE OCTAGON / BESTFIT POLYGONS (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Sprite render modes | Quad (quad list), Octagon (8 verts, corners trimmed), BestFit (authored 3-12 vertex polygon, last with Time <= age); corners expand like quad corners, UV = (cell + corner) x cellSize | RE pass 5 s17 + addenda (fill, index lists, draw path) | CONFIRMED | M68: fan-triangulated polygons through the quad corner transform |

## MILESTONE 67 — SPRITE SUBUV (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| SubUV update | Every tick for every live particle; Linear(_Blend) from the SubImageIndex curve with frac interp; Random(_Blend) re-picks on RandomImageTime (lifetime fraction, default 0 = every tick); second cell = next, wrapping | RE pass 5 s16 (update runner 0x1F) | CONFIRMED | M67 (was: Random picked once at spawn, no blending) |
| SubUV blend | The fill writes both cells + interp; ParticleSubUV lerps the two samples | RE s16 (shader lerp HIGH) | HIGH | M67: sprite attribute 6 + matc ParticleSubUV mix |
| SubUVDirect / Select | Direct UV = (pos + size x corner) x scale (texel units HIGH); Select unused in MP | RE s16 | PARTIAL | not applied (10 MP LODs) |

## MILESTONES 65-66 — FIXED-AXIS RIBBONS, SPRITE LOCK-AXIS / VELOCITY MODES (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Beam2 / Trail2 BillboardSettings | Direction other than CameraFacing offsets along a fixed axis (component row or world), Alignment side scales | RE s13 addendum 3 (HIGH, emulated shader) | HIGH | M65; authored on 4 templates, none in MP data; geometry checked numerically |
| Sprite LockAxisFlags | Emitter-level in WFC; local-space LODs use component rows, else world axes; lock 1-6 fixed plane + in-plane rotation; ROTATE_* turn about the axis, rotation ignored | RE s15 + addenda (CPU CONFIRMED, world formulas HIGH) | HIGH | M66: 178 MP emitters; ground burst rings lie flat (VISUALLY VERIFIED) |
| Sprite PSA_Velocity | Width along cross(camera - particle, D); length Size.y, V 0 leading; rotation ignored; stationary / view-aligned collapse | RE s15 | HIGH | M66: width axis was mirrored; stationary fallback removed |

## MILESTONES 63-64 — RIBBON WIDTH, LOCATIONEMITTER / PARTICLE TRAILS; MOLTEN PERFORMANCE (2026-10-06)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Ribbon width | Beam / trail vertex pairs are offset by (2V - 1) x Size x cross(view, dir): full width 2 x Size; we drew Size | RE s13 addendum 2 (Xenos VS emulated) | HIGH | M63: authored widths |
| LocationEmitter / Direct | Payload decoded from the LOD tail; selection, space conversion, inheritance, born-this-frame rule, Direct re-snap per RE s14 | RE s14 (spawn / update runners) | CONFIRMED (sub-frame terms PARTIAL) | M64: Streets 57 / 59 modules bound |
| Trail2 with LocationEmitter | One chain through the emitter's own particles in spawn order, <= 1 per tick, cap MaxTrailCount x MaxParticleInTrailCount | RE s14 | CONFIRMED | M64: shell casings trail smoke (was no ribbon) |
| Molten "performance drop looking down" | Not reproduced on the current build: 2560x1440 look-down sweep (76 spawns x 8 views) median GPU 0.64 ms, max 2.2 ms; real-time CPU frame 1.4-1.8 ms looking down vs 2.0-2.2 forward (fewer draws); no first-use creation after load. Likely the pre-M54 / M58 first-draw stalls on the human's build | WFC_FRAMELOG, WFC_PERFLOG, WFC_RENDERSTATS | HIGH (not reproduced) | No change; WFC_FRAMELOG at the spot if it recurs |

## MILESTONES 61-62 — BEAM / TRAIL UV LAYOUT, GORGE VERTEX LIGHTMAPS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Tiling values | No MP effect authors TextureTile / TextureTileDistance / Sheets / bTilePerParticle (735 Beam2 / Trail2 LODs); CDO TextureTile 1, Sheets 1 | pstream survey; Engine CDOs | CONFIRMED | exported with defaults |
| Beam UV | U along (0 source -> 1 target, per emitted pair incl. noise sub-steps), V 0 / 1 across; TextureTile never applied by the beam fill; no scrolling | RE pass 5 s13 (0x830298E8 UV writes) | CONFIRMED (world side of V 0: UNKNOWN, Xenon VS) | already U = r |
| Trail UV | U = 0 at the head = NEWEST particle (Spawn relinks the new particle as head), growing to the oldest by cumulative distance x TextureTile, clamped; bTilePerParticle per segment | RE s13 + addendum (0x8301A700, 0x83048E40); authored trail textures fade from U 0 (iontrail_01, RingsTrail) | CONFIRMED | M61 / M61b: was index-based from the OLDEST point |
| Gorge vertex lightmaps | Export duplicated 40 of 1576 cooked vertices across two sections | AssetTools a7b9ef0 _WFC_SRCVERT (exact pskx face pairing) | CONFIRMED | M62: samples re-ordered through the cooked index; all 4 sections bind |

## MILESTONES 59-60 — PREWARM REPLAY, BEAM SOURCE / TARGET METHODS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Prewarm across matches | Gameplay prewarms once per chassis when cached; each map load reset the renderer's programs / textures, and requests made before render data loaded were dropped | two-match flow, WFC_RENDERSTATS | CONFIRMED | M59: requests remembered and replayed after every map load. With Gameplay 2f258b6 (all match chassis cached at the match load): no mid-match chassis load, no first-use at transforms |
| Beam source / target | Particle methods never read a particle (BeamMethod Target): Default path. Emitter source = component origin; Emitter target needs a name, else the distribution. Default = distribution at EmitterTime through the component LocalToWorld (raw if bAbsolute); a named Default target reads the instance parameter first. UserSet = SetBeam*Point array, empty -> distribution. Re-resolved every tick unless locked. Tangents: Direct / Emitter = component X; Distribution raw; x Strength. Distance method: source + X x Distance | RE pass 5 s11: ResolveSourceData 0x8302F320 / ResolveTargetData 0x8302F738 | CONFIRMED | M60: repair squib arcs from its Source curve into the centre; TF_Death_Buildup lightning to its Target curve; segment beams unchanged (VISUALLY VERIFIED). Base path Hermite with the tangents (stock, HIGH) |
| Seed / Berth darker than 06b | The maps' authored ColorCorrectionTextures are contrast S-curves (Seed desaturation40: 33->18, 99->82; Berth clut_mp40: 33->21, 99->108), applied since M25 in UE3 order (grade, gamma, LUT); 06b applied none. NOCLUT: Seed median 12 -> 24, Berth 9 -> 23 | CLUT diagonal; toggles at Experimental's spawn | HIGH (human check vs original footage kept) | No change |
| Gorge vertex lightmaps | 1576 cooked samples vs 1616 glTF vertices (two sections, ~40 duplicated at export) | renderer warning | CONFIRMED (export) | AssetTools asked for a per-vertex cooked index |

## MILESTONES 56-58 — BEAM NOISE + SINE WAVE (native beam fill), FRONTEND EMITTER PREWARM (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Beam module binding | Cooked beam LODs keep no Modules array. Each LOD's serialized bytes reference its BeamSource / BeamTarget / BeamNoise / BeamSineWave exports (one unique big-endian index hit each). | RE pass 5 addendum; raw scan, 40 Streets beam LODs | HIGH | build_map_fx `beam_trail.modules`; raw distributions decoded from float bit patterns; defaults = Engine.Default__ParticleModuleBeamNoise / BeamSource / BeamTarget (NoiseRange / NoiseSpeed 50, NoiseTangentStrength 250, Source / Target strength 25) |
| BeamSineWave (WFC addition) | At every interpolated point: offset = sum Dir x Amp x sin(2 pi ((Speed t + d) / Period + Phase)), d = r x length (UU), t = particle age. The offset is rotated by the quaternion taking +Z onto the beam and faded by min(6r, 1) x min(6(1-r), 1). | RE 9i: native fill 0x830298E8 / 0x83029618 (it had never been defined as a function) | CONFIRMED | M57: repair / drain beams' strands twist and are pinned at both ends (VISUALLY VERIFIED) |
| Beam noise generation | N = Frequency (or int(appSRand x (F - Low) + Low)); N + 1 points, Point[i] = NoiseRange(i / (N + 1)). NoiseLockTime < 0 never re-drawn, <= 1e-4 every tick, > 0 on a timer; bSmooth re-draws into a target. | RE 9h: Spawn 0x8301F6F8 / Update 0x8301F928 | CONFIRMED | M56 |
| Beam noise render | Applied only with bLowFreq_Enabled. Offset = point x noise scale (1.0: FrequencyDistance is never authored) x NoiseRangeScale. bSmooth steps toward the target at NoiseSpeed x seconds since the re-draw, snapping within NoiseLockRadius. Curve tangents = normalize(N[i+1] - N[i-1]) x lerp(SourceStrength, TargetStrength, r); NoiseTension / NoiseTangentStrength have no effect. NoiseTessellation Hermite sub-steps. | RE 9i / 9j | CONFIRMED (component-space rotation of the offsets: HIGH, call chain) | M56 / M57: turret repair ray / repair squib render as lightning re-drawn every tick (VISUALLY VERIFIED) |
| Ribbon joins | Beams and trails drew one independent quad per segment, so kinks opened gaps. They are now connected strips: per-point side from the averaged direction, as UE3 shares vertex pairs. | RE 9j (vertex direction normalize(cur - prev)) | HIGH | M56 |
| Frontend first-frame stalls | The title's placed emitters compiled on their first drawn frames (Lightning_Anim_03 45 ms, Lightning_02 texture 43 ms, ...) because M54 skips the map effect prewarm for menus. | WFC_RENDERSTATS; Frontend WFC_FRAMEPROF 269 / 215 ms | CONFIRMED | M58 prewarmPlacedFx: the title's 14 materials in 204 ms under the loading screen; 0 first-use items after the load |
| Streets references after AssetTools 439a8ce | The 14 repaired Streets materials (Pixel_4_NORM / Mask, None-texture inheritance) are not visible at the 5 reference cameras: the captures are pixel-identical before and after the data change. | suite runs 19:18 vs after the 19:32 world.glb | CONFIRMED | References unchanged |
| Vehicle muzzle sockets | Every shot alternates WeaponSocket_Primary / Primary2; the projectile spawns at the flash's socket. | RE 9g (HmWeapon.OnPlayFireEffects / ChangeSocket) | CONFIRMED | Gameplay 8253a62. The flash belongs to Systems' WeaponFx (forwarded). |

## MILESTONES 51-55 — FOCUSED VISUAL-FIDELITY PASS (newest human playtest) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Molten floor "flicker / box" | A hard dark rectangle from the screen centre to one corner on the lit floor, moving with the camera. RainPuddles_Mat (MoltenRainPuddle_MatInst) reads SceneTexture at ScreenPosition + ripple offset. WFC cooks the expression flag as `ScreenAlign` (not `bScreenAlign`): matc ignored it and fed clip-space xy (-1..1) into a [0,1] lookup. | Toggle bisection: DOF / bloom / map FX / character shadows / Canvas tiles unchanged; WFC_PICK on the puddle plane; cooked `MaterialExpressionScreenPosition_16345 {'ScreenAlign': True}` | CONFIRMED | M51: matc honours `ScreenAlign`. wfcSceneColor / wfcScreenUV use UE3's v-down screen UV (ScreenPositionScaleBias (0.5, -0.5)) on the bottom-up GL copy. Box gone (VISUALLY VERIFIED, puddle viewpoints). |
| Molten "performance drop looking down" | Not reproduced. GPU frame 0.3-0.7 ms at the puddle / lava viewpoints (WFC_FRAMELOG), no `wfc spike`. The only measured mid-match hitch was the first transform (M53). | WFC_FRAMELOG, WFC_RENDERSTATS sweeps | UNKNOWN | Ask the human for the exact spot (WFC_FRAMELOG gives per-frame GPU time + draw counts + camera). |
| Orbital Debris sky black | The authored sky is SpaceDome_STATMESH x6 (Spacedome_Nebula_MAT, radius ~26.6 km) plus the Cybertron_2D_Planet card. The 20 km far plane clipped the dome: black in the screen centre, nebula only at the view edges (planar far plane vs a sphere). UE3 uses an infinite far plane. | world.glb dome bounds; captures up1 / up4 69 % / 57 % black | CONFIRMED | M51: far plane covers the loaded world's extent. up1 69 -> 14 % black, up4 57 -> 6 % (remaining black = authored space between stars). Authored nebula + starfield (VISUALLY VERIFIED). |
| Title Cybertron dark craters | BlastBuildings / BlastDebris (2 of 10 planet meshes) cook no lightmap (LightMap None, no OverriddenLightMapResolution, Static channel). The only Static-channel lights are #7 (radius 40 m, 74 m short of both meshes) and #0 (brightness 0.05). The orange specks are emissive. The other 8 meshes' lightmaps and the LowShell override are bound (AssetTools confirms). | lighting.json lights / props; cooked component props | HIGH (authored) | No change. Uncached per-pixel lighting would add only light #0 at 0.05. |
| Projectile cubes | A Gameplay placeholder (World.cpp drawBox). Projectiles are particle-only: FlightEffect PSC = body, ExplosionEffect at hit / HitNormal. | RE projectile_effect_bindings.json; AssetTools weapon.json projectile_visual | CONFIRMED | Renderer API ready (spawn / setTransform / stop). Contract sent to Gameplay. M52: template library adds the class-data templates the level packages lack (Ion Blaster muzzle / impact, A_Plasma_Trail, KamikazeMine, Exp_2500cb, Rumble). Flight bodies render on a moving source (VISUALLY VERIFIED). |
| Scout fires from the left | Gameplay's origin chain is right (posed WeaponSocket_Primary x meshMatrix, no offset). Car4 / Car2 author WeaponSocket_Primary (left gun) and WeaponSocket_Primary2 (R_GunRobo01_XT); vehicle weapons list both as MuzzleFlashSockets (RE: alternating for rockets). Runtime uses only Primary. | character.json sockets; RE pass 5 | HIGH (alternation P: RE tracing) | Gameplay: round-robin per shot. |
| Scout transform "box-like" | Car4 R->V and V->R captured every frame (lockstep): authored fold, no graybox / unposed / stale mesh frame. Gameplay swaps form + skinPose in one tick. | work/m50/xf frames 150-300 | VISUALLY VERIFIED (no rendering defect) | The measured hitch on the first transform was M53. |
| First-transform hitch | 65 ms frame (166 ms tick gap): vehicle program (59 ms) + 3 textures (51 ms) created on its first visible frame | WFC_RENDERSTATS first-use | CONFIRMED | M53: IRenderer::prewarmDynamicMesh; Integration adds the call at Character model assignment. Spike gone in a trial build. |
| Menu / match-start stalls | The effect / weapon prewarm ran on frame 2: PartyLobby 158 programs 1174 ms, Streets 161 programs 1.2 s, both after the loading screen. Unload deleted every program, so revisits recompiled. | WFC_RENDERSTATS, WFC_HITCHLOG, Frontend WFC_FRAMEPROF | CONFIRMED | M54: prewarm at the end of the world upload (16 ms yield slices); none for frontend scenes; linked programs cached across loads by source (LRU 1500); preview bodies prewarm at load. Lobby revisit 39 / 39 reused; no tick hitch after the Streets load; title longest load gap 187 -> 73 ms. |
| High refresh / jitter | Uncapped Streets with fast turning + strafe + fire + transform: CPU frame 1.4-3.5 ms; no GL errors. The only hitches were M53 / M54 (fixed). | WFC_PERFLOG / WFC_HITCHLOG | HIGH | No presentation / interpolation change (the character high-refresh fix is untouched). |
| Beam2 taper | TaperCount = InterpolationPoints + 1; width_i = size x TaperFactor(r) x TaperScale(r), r along the beam, fixed at spawn | RE pass 5 s9 (Function_8302E898) | CONFIRMED (native) | M55: repair beam tapers from 0 at the source (VISUALLY VERIFIED). |
| Trail2 tessellation | TessellationFactor vertex pairs per segment (TessellationFactorDistance does not change the count) | RE pass 5 s9 (Function_83019600) | CONFIRMED count / HIGH Hermite (stock UE3, TessellationStrength) | M55. Noise / sine wave: M56-M57; UV layout M61. |
| AMD stability | 0 GL debug errors, 0 out-of-bounds draws, 0 context resets across every run of this pass (Molten, Debris, Streets real time, frontend flow x2) | KHR_debug callback, subInBounds | HIGH | M43-M45 guards kept. |

## MILESTONE 45 — OUT-OF-BOUNDS SKINNED DRAWS (AMD stability, real defect) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Stale sub-mesh ranges on a reused skinned output | assets::skinPose kept the output's previous subs / uv when only their count matched the new model; a reused output (robot <-> vehicle form, chassis change: equal section counts) carried another model's index ranges (e.g. [2733, +26937) on a 26388-index, 7377-vertex mesh). Every such draw fetched past the index buffer on the GPU: undefined behaviour an AMD driver may answer with a page fault / reset | M43 guard on the release path: 20 rejected sub-meshes after the Optimus spawn + one 364 ms GPU frame | CONFIRMED defect (reset link HIGH, not reproduced as a reset here) | subs / uv always taken from the posed model; release path 0 rejected, 0 long GPU frames |

## MILESTONE 44 — TRAIL2 / BEAM2 RIBBONS, PSC PARAMETERS (vehicle / tracer / beam effects from data) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Beam2 | each live particle draws a camera-facing ribbon source -> target (width = particle size, colour / alpha over life, the emitter's material); live beams capped by the type data's MaxBeamCount (authored 1 on Tracer_RepairBeam_FX: 98 stacked beams before the cap) | pstream TypeDataModule props | HIGH (taper + InterpolationPoints M55; noise + sine wave M56 / M57, CONFIRMED native fill) | WFC_FXTEST ">FX_RepairBeam_p.FX.Tracer_RepairBeam_FX": one electric HealBeam_MAT beam + its source rings |
| Trail2 | trails spawn per distance travelled (UE3 SpawnPerUnit; required-module rate is 0): one particle per 5 UU of source movement, the ribbon follows the source's recent path fading over the particle lifetime; a trail spawned along a segment (tracer) draws one ribbon start -> end | Tracer_AssaultRifle / Tracer_SniperRifle / Trails_Jet_A modules | PARTIAL (tessellation applied in M55; tiling distance, bConnectToSource) | Assault Rifle / Sniper tracer smoke ribbons render from their own materials (Trail_Smoke_10_MAT_INST / Tracer_Smoke_MAT) |
| PSC parameters | IRenderer::setParticleEffectParam(handle, name, rgba): "Color" -> ColorByParameter (priority over the decoded DefaultColor), "Size" -> ParticleModuleSizeMultiplyLife whose LifeMultiplier is a DistributionVectorParticleParameter naming it (M46, RE pass 4: CarHover_A / D_01_FX _9193, hover_plane_FX _476 / _6182; identity mapping, Constant (1,1,1) fallback). Applied per LOD that references the module's export index in its serialized bytes (M47, RE raw scan confirmed: CarHover_A's shared _9193 in all 7 level-0 LODs, no level-1 LOD; build: CarHover_A 7, CarHover_D 9, hover_plane 2, all LOD 0) | RE pass 4 (HoverFX 'Size' = min(1, thrust) x socket scale; BoostFx 'Color' = lerp(EnergonColor, Yellow), alpha 100..255) | CONFIRMED rules (RE) / CONFIRMED data (module) / HIGH (LOD reference by raw index) | hover_plane / CarHover / hover_tank render and scale with Size; Systems wires per socket |
| Vehicle FX ownership | every chassis' authored HoverFX / BoostFx / JumpFX templates and sockets are in the AssetTools roster (e.g. Jet4: hover_plane_FX x5 Hover_* sockets, Afterburner_A_FX, Trails_Jet_A_FX); all FX_Navigation_p templates are in each map's template library (23) | mp_characters.json vehicle.fx | CONFIRMED data | Systems drives them through spawnParticleEffect / setParticleEffectTransform / setParticleEffectParam (only the Optimus hand reconstruction exists today) |

## MILESTONE 43 — AMD STABILITY AUDIT AND DIAGNOSTICS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Driver errors | always-on GL debug output (KHR_debug callback, non-debug context: errors still reported, verified by a WFC_GLDEBUG=selftest invalid enum) + context reset status poll (ARB_robustness / GL 4.5) + GPU frame timer (GL_TIME_ELAPSED ring, never stalls; > 250 ms logged). VISUALCHECK lines carry gpu=ms and glDebug; driver errors fail the verdict | this machine runs the same AMD driver family (26.6.x) | — | 9-match frontend map chain (Streets, Seed, Berth, Gorge, Complex, Rust, Debris, Molten, Streets; persistent renderer): 0 GL debug errors, 0 context resets, 0 out-of-bounds draws, 0 incomplete framebuffers, 0 shader failures; Streets GPU frame 1.5-3 ms |
| Out-of-bounds vertex fetch | no draw validated its index range (sub-mesh past the index buffer / indices past the vertex buffer): a GPU out-of-bounds fetch can page-fault an AMD GPU and reset the driver | code audit | CONFIRMED gap (no occurrence observed) | static and dynamic sub-meshes validated before submission (out-of-bounds ones dropped, logged) |
| Vertex lightmap texture width | (count x 3) RGB32F texture; > GL_MAX_TEXTURE_SIZE would be invalid | max count 6351 (Debris) | guard | not bound above the limit |
| Own bug caught by the new diagnostics | the first GPU timer version could EndQuery without a begun query (GL_INVALID_OPERATION); reported at once by the callback, fixed before commit | gputime.log | — | — |
| GL objects across matches | renderer-owned objects constant (1 buffer / 2 FBOs / 2 RBOs / 1 VAO / 2 programs); live textures +1 per match, all in uiTextures (GFx side) | match.glCensus | CONFIRMED | Frontend handoff |
| Not reproduced | no reset / GL error on this AMD GPU in the chain; the human's resets are UNKNOWN cause. Recommended platform change (not renderer-owned): create the context with WGL_ARB_create_context(_robustness) (robust access + reset notification; a debug context under an env switch) so a reset is reported and recoverable instead of fatal | — | UNKNOWN | — |

## MILESTONE 42 — EVERY MP WEAPON'S AUTHORED MATERIAL (untextured Sniper, grey Scientist) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Sniper (Null Ray) untextured | the render data compiled only weapon materials map geometry referenced (pickups / crates: Ion Blaster, EMP Shotgun, Grenade Launcher, rockets, flak) plus WEP_IonBlaster_MATINST; every other held weapon's material (WEP_SniperRifle_p.WEP_SniperRifle_MAT, Burst Rifle, Assault Rifle, ...) was absent, so the weapon drew its glTF fallback (flat grey). The weapon glb names the original material (extras.wfc_material) and every MP map cooks it | weapon.json / weapon.glb, materials_glsl.json (6 WEP_ before) | CONFIRMED | tools/render/weapon_materials.py (all 54 exported weapon material slots) -> build_render_data @weapons: Streets 57 / 57, Gorge 57 / 57 WEP_ materials compile. Starscream (WFC_CHASSIS=Jet) Sniper: flat grey -> authored metal + cyan emissive (VISUALLY VERIFIED) |
| Autobot Scientist "grey / missing portions" | the grey mass is its primary weapon (Burst Rifle) drawn with the fallback, same cause; the Air Raid (Jet4) body and its 4 materials compile and bind (Norm / SpecPwr / Grunge / CustAB / fractal / Metals cube; MIC Cust_Color_A grey 0.54, B red); frontend preview colours are the palette pick (ResetCharacterFromName randomises, so a grey roll is authentic). Other classes looked complete because their default weapons (EMP Shotgun, Ion Blaster) were among the few compiled | direct preview render vs character-select art; frontend CAC capture; WFC_CHASSIS=Jet4 before / after | CONFIRMED | same fix; Burst Rifle now textured with its cyan emissive |
| AssetTools audit (dc2d0ee) items | (1) TextureCubes not exported: the render data decodes CHR_Metals_CUBEMAP3D faces itself (tex/*_f0..5); (4) Normal_Map = None on 12 Cust MICs incl. Sideswipe (renders correctly): not the Scientist cause | — | noted | — |

## MILESTONE 41 — HUMAN PLAYTEST PASS: TITLE BLACK SHIPS, TITLE VIGNETTE OWNERSHIP (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Title ships / debris solid black | the title's 51 movable InterpActors (transports, debris; UI_FrontEnd_capture_VIG_m / UI_FrontEnd_m) author LightingChannels = {Dynamic} with their own enabled LightEnvironmentComponent (engine default bEnabled False). UE3 lights them with the Dynamic-channel lights (SkyLight 2.0 (142,173,210), PointLight_8444 15.0 cyan r 250 m); the renderer lit every non-lightmapped world submesh with the Static set only (one 5.0 / 40 m point light): near black. Albedo was intact (WFC_ALBEDO), lighting was missing (WFC_LIGHTINGONLY) | cooked components (props_authored lighting_channels_on, LightEnvironmentComponent_7904 bEnabled); debug views | CONFIRMED (data + UE3 channel overlap) | build_lighting component_flags dynamic_channel; such submeshes take the Dynamic-channel environment at their current position each frame. Title near-black 9.9 % -> 2.5 %; the M08b human frame's black transport is lit with its hull detail. Streets suite identical (its 8 Dynamic-only components unchanged on screen). Not a global ambient change |
| Empty-channel components | the title's 41 empty-channel components are glow spheres / light cones / debris cards / SpaceDome with bAcceptsLights False: emissive only is right | props_authored | CONFIRMED | unchanged |
| Title vignette side strips (HUMAN-CONFIRMED) | reproduced on M08b at 1280x720, 1920x1080, 2560x1440 windowed and 2560x1440 fullscreen: darkening covers 87.5 % of the width. The vignette is a GFx asset (FrontEnd_GFX_I24, black, alpha 58 at edges -> 0 centre, scale-9 clip screenSoftEdges_mc); FrontEnd / Hud stages are 1120x720 (14:9). The movie's own Stage.onResize stretches it to Stage.width x Stage.height; the GFx host reported the authored 1120 and sent onResize only to noScale movies | SWF headers, I24 pixels, renderer-only title frame has no vignette, Frontend's AS reading | CONFIRMED | ownership: Frontend GFx host (not renderer viewport / scissor / quad). Fixed in agents/frontend (showAll movies get the visible area as Stage.width / height + onResize; authored art unchanged); recheck pending on a merged preview |

## MILESTONE 35 — VECTOR-CHANNEL PROOF INHERITED BY INSTANCES (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| LogoAUT / LogoDEC_MATINST GLSL error (Integration M08b) | instances of ParticleBase_BW_MAT, whose graph is already proven to use VectorParameter channel outputs (vector_channel_proven.txt: AppendVector(UVandOffset.B, UVandOffset.B) is invalid as vec4 + vec4); matc tested only the instance path, so instances fell back to the full-vector reading (vec4(vec4, vec4)) | Integration m08b_soak wfc.log; matc trace | CONFIRMED | matc applies the proof to the instance's master / chain. Streets: only the two Logo instances change; 0 compile failures |

## MILESTONE 34 — PARTICLE ColorByParameter DefaultColor (per template) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Template colour | the Ion blue Systems used is MuzzleFlash / Tracer_AssaultRifle's own ColorByParameter DefaultColor, not a weapon tint (the weapon's EnergonColor parameter is (255,255,255,A=0) = no override); each template carries its own | Systems a1d38c5 notes; LOD stream bytes | CONFIRMED (Systems / data) | runtime ColorByParameter order: component InstanceParameter, caller colour, decoded DefaultColor, white |
| Decode | ARGB FColor after the module-order list (count = module records, bytes 0..n-1), a counted byte list and 20 bytes [HIGH: reproduces ff 33 19 ff on every AssaultRifle emitter]; other layouts: the stream's single A = 0xFF value after a zero int [MEDIUM]. Streets: 435 / 451 ColorByParameter emitters decoded, every template internally consistent (AssaultRifle (51,25,255), EMP shotgun (255,65,65), Sniper (255,12,12)); EMP red matches its editor thumbnail (the Sniper / AssaultRifle thumbnails are identical, generic) | build_map_fx.py default_color(); work/m34_thumbs.png | HIGH / MEDIUM | WFC_FXTEST without a caller colour: EMP red, Sniper red (was blown-out white), AssaultRifle Ion blue |

## MILESTONE 33 — MATINEE MATERIAL PARAMETERS (lobby faction emblems) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Faction emblems invisible | UI_CharacterCustomization's MaterialInstanceActors (8803 / 16331 Autobot logo / glow, 5865 / 1120 Decepticon) drive their MICs' Highlighted / Opacity through Matinee; compiled with the authored defaults (Opacity 0) the emblems were constant-folded to emissive 0 (additive: invisible) | materials_glsl.json; Frontend matinee evaluation | CONFIRMED | build_materials: every MaterialInstanceActor's MIC compiles with runtime parameters (no map names) + material_instance_actors.json; IRenderer::setFrontendMaterialParam(actor, param, value) routes to the MIC (held until changed; unset = authored). WFC_MATPARAM: Opacity / Highlighted 1 shows both emblems and glows, unset stays invisible (VISUALLY VERIFIED) |

## MILESTONE 32 — RUNTIME PARTICLE TEMPLATES (weapon muzzle / impact FX from data) (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Where weapon FX live | the FX_<weapon>_p packages are empty stubs; every MP weapon template is cooked into each MP map's BASE package (Streets BASE: 130 ParticleSystems, 789 emitters, 784 fully assigned by pstream) | cooked export census (work/m32_ps_index_all.json) | CONFIRMED | build_map_fx.py library(): all map-package ParticleSystems join map_fx_runtime.json systems (Streets +131); their materials compile via build_render_data (@fxlib; Streets 325 -> 405 materials, existing GLSL byte-identical) |
| Spawn API | IRenderer::spawnParticleEffect(tpl, pos, forward, up, color) / spawnParticleEffectSegment(tpl, start, end, color) / setParticleEffectTransform / stopParticleEffect / liveParticleEffects. glTF metres; forward = template +X, up = +Z. Unknown template -> -1, logged once. One-shot effects release when finished; loops until stopped; all released by unloadMapRenderData | WFC_FXTEST (4 templates every 30 frames): handles, live count plateaus at 10 | VISUALLY VERIFIED (Streets) | — |
| Modules added | RotationRate, ColorOverLife (spawn + update), Acceleration, SizeScale, VelocityOverLife (bAbsolute assumed false: multiplier values), SubUV (linear / random by RequiredModule InterpolationMethod), PSA_Velocity sprites (up = velocity, right = view x velocity) | UE3 module semantics | HIGH (VelocityOverLife flag PARTIAL) | — |
| Not yet | Trail2 / Beam2 emitters (tracers, repair / drain beams) are skipped and logged; PMI_Gravity / PMI_Unknown have no decoded payload; ColorByParameter DefaultColor not decoded (the caller passes the weapon colour); LocationPrimitiveSphere StartRadius / VelocityScale undecoded on some templates; VelocityInheritParent = 0 (static spawns) | logs | PARTIAL / UNKNOWN | next |

## MILESTONE 30 — ROBOT / VEHICLE FORM ACROSS ALL MAPS (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Optimus robot + vehicle, 10 MP maps, spawn 7 | 20 / 20 PASS (0 noProgram / noDepth / GL errors), vehicle form active in all 10 vehicle runs; materials, hover FX and lighting present on every map; luma p50 8-44 (graded range) | work/m30/sheet0.png, sheet1.png | VISUALLY CHECKED (no defect found) | — |
| Debris haze | strong brown cast from its WorldInfo CLUT (MP_OrbitalDebris_CLUT) + fog; no volume grade | captures | PARTIAL (no original frame) | — |
| Streets camera inside Ceiling_Arch (Integration M08 near-black frame) | the third-person camera can sit under the arch's curved underside: Ceiling_Arch_STAT's authored simple collision (RB_BodySetup_10993: slab + legs) has no curve, and the original camera trace never tests per-triangle (execTraceCamera -> SingleLineCheck, World | 0x0A000000 BlockCameras, no complex bit; RE via Gameplay). The frame-filling dark view is the original's behaviour; its VISUALCHECK "near-black" is a legitimate camera-in-geometry frame | WFC_SHOTLIST + WFC_PICK: render 0.74 m vs collision 20.9 m; RE | CONFIRMED (authentic) | none |
| Harness note | the agents/rendering exe has no WFC_MAP (map selection lives in the integration code): map audits must use the integration-based build, or every capture is Streets | first m30 run | — | — |

## MILESTONE 28 — TEXTURE LIFETIME FOR A PERSISTENT RENDERER (2026-10-05)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| uploadTexture leak across matches | unloadMapRenderData freed uploadMesh slots but never uploadTexture textures: World::load map textures, Weapon / Vehicle FX textures, mesh material and lightmap textures. Frontend's persistent-renderer chain: +60..105 live textures per match (Streets 114 -> 716 after 9 maps). My M22 "71 textures = the game's own" were these: renderer-only runs never reloaded the World | Frontend chainP census; WFC_RELOADTEST pre-M28: 355 live after 5 reloads | CONFIRMED | unloadMapRenderData releases every texture uploaded since the previous unload unless setTexturePersistent (canvas font pages are). Released handles are never reused; updateTexture refuses them. New IRenderer::setTexturePersistent / releaseTexture / liveTextureCount. WFC_RELOADTEST=60,60: 71 released per cycle, 0 live; after 4 cycles the frame matches a no-reload run (PASS, diff = shot timing). WFC_M28_KEEPTEX = old behaviour (A/B) |
| In-process World reload hang | assets::loadSkinnedGlb loaded into the existing SkinnedModel: nodes.resize kept the old nodes and children / roots were appended, so every reload duplicated each skeleton edge and poseGlobals' DFS walked k^depth paths (reloads got slower, then never finished at reload 4-5). lldb on a RelWithDebInfo build: main thread in assets::poseGlobals <- Character::finalizePose <- World::tick | lldb backtrace work/m26/hang_bt.txt | CONFIRMED ROOT CAUSE (shared asset loader; only a reload into a used model, i.e. WFC_RELOADTEST or any chassis / weapon reload that reuses a model) | M29: a load replaces the model (m = SkinnedModel()). WFC_RELOADTEST=60,60: 9 cycles in 41 s, 71 released / 0 live each, PASS |
| Title reference | re-baselined after UI_FrontEnd's volume grade (M25 verdict above) | suite | — | work/ref_m21/title_scene |

## MILESTONE 26 — DEBRIS SIGN COMPILE, ION BLASTER TRACER SLABS (2026-10-05, overnight)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Debris Megatron_com_Mat GLSL compile error | Panner_1591's Time is a float3 (Clamp(Desaturation(...)) × 1); matc emitted vec3 * vec2. The cooked material carries no compile errors and hundreds of shipped materials rely on the same lenient vector coercion | expression trace; cooked tail has no error strings | HIGH | matc Panner / Rotator truncate a vector Time like HLSL (.xy / .x), counted as type_violations like binop. Only GLSL that previously failed changes. Debris: 0 compile failures |
| Ion Blaster grey slabs (Integration E2E) | WeaponFx (Systems) draws the tracer smoke ribbon at flat 0.35 opacity; the original Tracer_Smoke_MAT multiplies by an across-width mask (1 − 4(v−.5)²)² (0 at both long edges) and an end fade clamp(20u)·clamp(3(1−u)) | matc compile of TransGame FX_Materials_p.Materials.Tracer_Smoke_MAT; repro WFC_AUTOFIRE Streets s0 | CONFIRMED (material graph); GAMEPLAY/SYSTEMS code, not renderer | handed to Systems with the formula; renderer unchanged |

## MILESTONE 25 — POST-PROCESS VOLUME GRADES, CLUT PATH, HARNESS (2026-10-05, overnight)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Map grade | Seed, Berth, Rust, Complex, Gorge, Molten, BrokenHope and Remnant author their grade on one enabled PostProcessVolume that contains every player start, not on TnWorldInfo. Bloom_Scale is 0.10 there (as on Streets' WorldInfo); the renderer used the Default__WorldInfo 1.0, so these maps were over-bloomed and ungraded | AssetTools postprocess.json volumes[] + completeness (Gorge 115/115 starts inside, Berth 120/120, Remnant 4/4) | CONFIRMED (data); "inside" from player-start coverage, the brush is not exported: HIGH for gameplay views | build_lighting: the highest-Priority enabled volume's Settings (over the FPostProcessSettings defaults, FVector {X,Y,Z} -> list) and its CLUT strip become lighting.json postprocess. Streets / Debris (no volume) byte-identical. Non-Streets p50 drops into Streets' range (10-45). Tone formula unchanged (UberPostProcess microcode, CONFIRMED pass 7) |
| CLUT never loaded off the worktree root | lighting.json records the strip path relative to the build's cwd; the renderer opened it verbatim, so a player-route exe (cwd elsewhere) ran every map without its CLUT | log "image: decode failed work/render/.../clut.png" from work/m20/maps | CONFIRMED (renderer bug) | resolved against the map's data dir. Release path now applies MP_Streets_CLUT |
| Title scene desaturated after M25 (Integration M07) | UI_FrontEnd has no CLUT; its PostProcessVolume_15709 (UI_FrontEnd_m, bEnabled) authors Scene_Desaturation 0.5, Bloom_Scale 0.2, Shadows 0.01. Its brush Model_14105 is a box of ±33,294 UU about the origin, and every title camera (-6702, -15212, 237) is inside it, so UE3 gives the title view this grade | brush points read from the cooked Model; postprocess.json volumes[] | HIGH (UE3 volume semantics + geometry); no original title frame yet: HUMAN CHECK | unchanged, data-correct |
| Black-frame rule | Graded dark views (Molten lava cave, Gorge shaft) are 60-65 % below luma 10 while correctly lit; the 60 % rule FAILed them. No real regression was ever caught by it (depth / GL-error / legacy / near-black rules caught them) | 30-view audit + contact sheet | harness | threshold 80 % (a lost world is > 90 %); 30 / 30 audit views now PASS. Molten / Gorge darkness is PARTIAL (no original capture to compare) |
| M11 coverage | Frontend restores its GL state since a96f841, so WFC_M11_INHERITSTATE alone no longer reproduced the leak and the release-path check PASSed it | release_path_check with the switch: PASS | harness | the switch now also injects GL_DEPTH_TEST off; release_path_check: fix PASS (6/6), reproduction FAIL (6/6, noDepth ~1450-1810) |
| Preview body lifetime | bodies are CPU-only and survive scene unloads; nothing freed them | code | — | IRenderer::releasePreviewBody (slot reuse) + previewBodyCount; Frontend LRU-caps at 8, soak plateau 1246 -> 1253 MB |

## MILESTONE 24 — VERTEX LIGHTMAPS ON EVERY MAP (2026-10-05, overnight)
| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Black arches / pillars on Gorge, Rust, Seed | (1) build_lighting rejected FLightMap1D sample blocks whose alpha byte is not 255 ("alignment" check): Gorge / Rust samples carry alpha 0, so 0 of 561 / 635 vertex-lit components parsed. (2) The renderer bound a vertex lightmap only when ONE glTF section covered the whole cooked LOD0 buffer, so multi-section components never bound | header dumps (count × 12 = size, valid ScaleVectors, alpha 0); AssetTools vertex_lightmaps.json agrees byte for byte | CONFIRMED ROOT CAUSE (tooling + renderer) | parser validates size and ScaleVectors instead of alpha: every vertex-lit component of every map parses (Gorge 561, Rust 635, Seed 257, …). Sections index the shared samples at their cumulative offset: Gorge unbound 124 → 4, Rust 381 → 0. Streets lighting.json byte-identical, suite identical. Gorge view 0 black 37 % → 21 % (arch lit). VISUALLY VERIFIED |
| Remaining | one Gorge component whose glTF sections total 1616 vertices against 1576 cooked samples (export differs from the cooked buffer) | renderer log | PARTIAL (not bound rather than guessed) | — |
| Map colour grade | 8 of 9 non-Streets maps grade on a PostProcessVolume containing the player starts (e.g. Gorge clut_mp40 + Scene_HighLights 1.5), not on WorldInfo | AssetTools 992fbf1 postprocess.json volumes[] | CONFIRMED (data) | applied in M25 |

## MILESTONE 23 — FRONTEND SCENES OVER TIME, CUSTOMIZATION, RETURN FROM MATCH (2026-10-05, overnight)
Merge preview: agents/frontend 1af7e74 (Cust_Idle body, pawn hiding, GFx fixes) + agents/rendering 03a08c8, rebuilt render data.

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Title over time | t = 5 / 30 / 60 / 120 / 240 s: the orbit camera sweeps (different framing each), debris fields / ships move, bursts fire (240 s), no GL errors, no unknown actors. RE: the whole title is ONE synchronised 393.55 s loop (Orbiter + 1st / 2nd / 3rd QTR + Plane Fly By start together at FMV_intro Stopped), FOV fixed 45 | captures work/m23/run/t*.bmp, VISUALCHECK | VISUALLY VERIFIED |
| Customization class cameras | Scout / Scientist × Autobot / Decepticon: the class camera frames that faction's pawn, ground-snapped, other pawn hidden; idles resolve through the chooser groups (Sideswipe / Barricade / AirRaid → NAV_Idle, Starscream → Cust_Idle) | captures, preview-body log | VISUALLY VERIFIED |
| Return from match | game lobby 3D layer before vs after a Streets match: same camera, draws (8), materials (6), inherited GL state (depth on); pixel differences come from the authored time-animated BackdropSpaceDome_MAT_INST | WFC_SCENE_ONLY captures | CONFIRMED (no state carried over) |
| All MP chassis | 27 chassis × robot + vehicle all render (54 captures PASS). Warpath's vehicle (Tank3) looked garbled in the close sweep: its bind pose equals Transform_ToRobot_VEH at t 0, i.e. the vehicle form; the close camera cut through the long tank | fixed-time clip samples (WFC_SCENEPREVIEW anim@seconds) | VISUALLY VERIFIED |
| RandomSeed | WFC material uniform "RandomSeed", one float per mesh element (FMeshElement +0xBC) set in the material PS SetMesh 0x82E982E8; the proxy field that fills it is not identified | RE | CONFIRMED (what) / UNKNOWN (source); the wrecked-soldier prop keeps its fallback |

## MILESTONE 20 / 21 — MULTI-MAP FIDELITY: LIGHTMAPS, LIGHT COLOURS, MATERIALS, CHASSIS (2026-10-05, overnight)
Audit: all 10 cooked MP maps built through the generic pipeline (no map names in renderer source), 30 direct-boot captures
(3 spawn views per map) on integration 9e133bb + agents/rendering, WFC_VISUALCHECK.

| Item | Finding | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| "Mostly black" maps (Gorge, Rust, Seed, Berth, Broken Hope, Molten, Complex) | build_lighting found each component's FLightMap2D coefficients by searching for the FIRST component's first light GUID; every component whose light list started with another light was dropped, so its static mesh got no baked light. Gorge 256 of 1137 records, Rust 127 of 1412 | lighting-only / albedo debug views (geometry present, walls unlit); record counts | CONFIRMED ROOT CAUSE (tooling) | FLightMap2D parsed structurally (type at 13, LightGuids.Num at 17, GUIDs, Owner, 3 coefficients, scale / bias). Streets records identical (1795 / 1795), lighting.json byte-identical. Lightmapped draws ×4–×10 on the affected maps; Gorge black 62–72 % → 15–37 %, Rust 35–50 % → 13–21 %. VISUALLY VERIFIED |
| Light / sky / fog colours | cooked FColor serializes DWColor big-endian; the reader lists [B, G, R, A]. build_lighting consumed it as RGB: every tinted light (Streets 260 of 268), every SkyLight lower colour and the height-fog colour had red / blue swapped (Streets fog (234, 91, 116) → actually (116, 91, 234)) | AssetTools authored.db field-named decode ({R:78, G:148, B:186} for the customization SkyLight; Streets fog {R:116, B:234}); RE customization facts; the original Streets level thumbnail's cool grey-violet tone | CONFIRMED | `fcolor_rgb` in build_lighting. Lightmaps unchanged; dynamic lighting / fog corrected. Streets suite re-baselined (work/ref_m21): 0.1–12.6 % per camera, cooler / neutral as the thumbnail |
| World materials | per map, every material the world glTF references is compiled, except Molten's floor (TextureSetSample) / rain puddle (SceneTexture) and the wrecked-soldier prop on Debris / Broken Hope (RandomSeed) | world.glb wfc_material audit | CONFIRMED | TextureSetSample added (TextureSet blueprint ENV_MainTexSet_BluePrint: Color_NormX = Color.rgb + Normal.x, Masks_NormY = Masks.rgb + Normal.y; alpha channels verified as a unit normal xy in the exported intermediates). Molten floors compile. SceneTexture added (M22: resolved opaque scene colour, copied once per frame at first use before translucency, unit 16; Molten rain puddles compile and draw; output mapping as a texture sample is HIGH). RandomSeed (WFC per-primitive value, no data; asked RE) remains [PARTIAL]. The other material failures in the build logs are Streets-only extras the map never draws |
| All MP chassis previews | 27 shipped MP chassis × robot + vehicle: 144 body materials, every MP one resolves to a compiled original. The 17 unresolved belong to six CAMPAIGN-ONLY bodies (generic car soldiers, Frenzy, Rumble, Laserbeak) | offline resolution + 54 renders in the customization room | CONFIRMED | contact sheets work/m20/chassis: correct orientation, floor contact, materials, faction colours |
| Preview idle via chooser groups | SetAnim remaps through AnimSet ChooserGroups, last set first; every playable chassis has a Cust_Idle group (→ Cust_Idle or NAV_Idle; Brawl weighted pair) | RE (xex SetAnim Function_82E3FF48, ChooserGroups from authored.db) | CONFIRMED | `tools/render/build_anim_choosers.py` → _ui/anim_choosers.json; loadPreviewBody resolves through it (weighted random pick: HIGH) |
| Map load / unload hygiene | 6 maps × 3 rounds in one process (renderer-only WFC_MEMCYCLE): live GL objects after every unload identical (71 textures = the game's own, 0 buffers / programs / framebuffers / VAOs); private memory peaks after Remnant (largest) and round 3 ≤ round 2 for every map (Streets 3723 → 3611 MB, Remnant 4238 → 4117 MB) | GL census, process counters | CONFIRMED (no GL leak) / HIGH (no CPU leak: allocator / driver high-water) | — |
| GL object census | `IRenderer::glObjectCensus()` (glIs* over names 1..131072) in WFC_MEMCYCLE | — | — | for the load / unload hygiene runs |

## MILESTONE 19 — PREVIEW PAWN POSE (2026-10-05)
| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Preview animation | TnCharacterScriptBinding.SetupPreviewAnim → IdleNode.SetAnim(PreviewAnim); class defaults IdleNodeName=IdleNode, PreviewAnim=Cust_Idle | decompiled script, cooked Default__TnCharacterScriptBinding | CONFIRMED | `IRenderer::loadPreviewBody(robotGltf, animSets, "Cust_Idle")` + `posePreviewBody(h, t, mesh)`: roster sets loaded, sequence found last set first (UE3 FindAnimSequence), looping |
| Which chassis have Cust_Idle | 13 sets (SequenceName): Jetfire, Optimus, Scattershot, Warpath, Zeta, Ironhide, Onslaught, Skywarp, Soundwave, Starscream, Thundercracker, Breakdown, Deadend. Arcee has only Cust_idle_active; the others (e.g. Sideswipe, Barricade) have none | cooked UI_PartyLobby_m (all MP sets cooked there) | CONFIRMED | VISUALLY VERIFIED: Warpath (12.3 s) and Starscream (9.9 s) animate in the customization room |
| Chassis without Cust_Idle | UE3 SetAnim on a missing sequence clears AnimSeq (warning) and the node outputs the reference pose | stock UE3 AnimNodeSequence; the rest of Robot_ANIMTREE above IdleNode not traced (asked RE) | HIGH (node) / UNKNOWN (tree result) | reference pose (logged) |

## MILESTONE 17 — CUSTOMIZATION CLASS CAMERA, IMAGE CHECK (2026-10-05)
Merge preview: agents/frontend 89df3bc + agents/rendering, -Map Standard render data. Route: party lobby → Create a
Character → first character → Autobot Chassis (clickclip chassisButtonA).

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Class camera chain | fscommand hideDecepticon → Chassis_To_Cam_ID_1 (slot 0, camera id 0) → Scout → "SCOUT - Autobot" (SeqAct_Interp_13491). CameraActor_2082 moves (784, 5252, 235) → (659, 5425, 195); FOV 69.75 → 60 | FLOW log, captures | VISUALLY VERIFIED |
| Preview pawns | Car2 (Sideswipe) / Car4 (Barricade) at the authored PathNodes with original materials, per-owner light environments | captures | VISUALLY VERIFIED (bind pose) |
| Pawn height | the pawns stand at PathNode z 179 with their mesh origin (feet) there. The room's floor is the invisible BSP slab (Invisible_MAT, x −1024..3072, y 3200..7296, top z 0), so the original's OnPreviewPawnTick FindGround puts the feet at z 0: currently 179 UU too high. The class camera's final shot crops the head at 179 and frames the whole body at 0 | bsp.glb, render at the camera's final pose | CONFIRMED (geometry). Renderer support: `IRenderer::sceneGroundHeight(x, y, zFrom)` traces down the level BSP (invisible brushes included): 0.0 under both PathNodes; a body placed there frames as the z 0 render (2 px difference). Frontend sets the slot height from it (FindGround) [PARTIAL until wired] |

## MILESTONE 12 — FRONTEND VIGNETTE SHIPS (2026-10-05)
| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Ship skeletal animation | none: the 17 HmSkeletalMeshActors (7 SoldierJetAut, 10 SoldierJetDec) have no AnimSets; their AnimNodeSequence serializes no properties (no sequence, not playing); the VIG / UI_FrontEnd Kismet has no animation action; no InterpTrackAnimControl in either level | cooked UI_FrontEnd_capture_VIG_m / UI_FrontEnd_m | CONFIRMED | reference (bind) pose is the original look. Bind pose = reference pose, verified per joint: for both ship meshes (SoldierJetAut 34 joints, SoldierJetDec 37), rest-pose world transform × inverse bind matrix = identity to 7.4e-6, so the drawn vertices are UE3's no-animation reference-pose result (M16) |
| Ship motion | InterpTrackMove (26 tracks) | cooked VIG | CONFIRMED | Frontend matinee → `setFrontendActorTransform` (done) |
| Ship / booster scale animation | 7 InterpTrackFloatProp DrawScale tracks: dec01, dec0203 (0.2), djDS01 (0.08 → …), megatronDS (0.02), thrust01, DSbooster, djDSboosters (0.1 → 0.6 / 1.0) | cooked VIG keys | CONFIRMED | `setFrontendActorScale` (relative to the authored DrawScale = gltf_matrix scale); emitters follow pose + scale. VISUALLY VERIFIED with the diagnostic (djDS01 0.47 → 0.08). Frontend's evaluator must export / evaluate the FloatProp keys [PARTIAL until wired] |
| Particle size under DrawScale | sprite / mesh-particle size × the emitter scale: UE3 FillReplayData `Source.Scale` = Component Scale × Scale3D × owner DrawScale × DrawScale3D, rendered as `Particle.Size * Source.Scale` | WFC xex (RE, Ghidra read-only): sprite FillReplayData 0x8300C678 and mesh FillReplayData 0x83052BD8 compute Component Scale(+0x178) × Scale3D(+0x17C) × owner DrawScale(+0x140) × DrawScale3D(+0x144) unless AbsoluteScale (+0xF4 & 0x100000); mesh gates: local-space (scale via LocalToWorld, as implemented) and an ignore-component-scale instance flag (unset in every TypeDataMesh of Streets / Berth / UI_FrontEnd). Matinee DrawScale is live: execSetDrawScale 0x82C48050 → AActor::SetDrawScale writes +0x140. The render-thread Size × Scale multiply: HIGH (stock UE3) | CONFIRMED | sprites: width × scale.X, height × scale.Y; world-space mesh particles: size × per-axis scale (local-space ones already through the instance rows); scale = instance row lengths, so it includes authored DrawScale and matinee scale. Streets unchanged (all 45 instances at scale 1); 41 scaled title / vignette emitters (0.04–1.5) now draw at their authored size. `WFC_FX_NOSIZESCALE` A/B |
| Camera FOVAngle tracks | Title / vignette: all 10 FOVAngle tracks have **no keys** (81-byte objects: PropertyName + TrackTitle), so the FOV is the bound camera's own FOVAngle (CameraActor_6585: 45, already sent). Customization (UI_CharacterCustomization_m, CameraActor_2082, authored 70): the 8 class camera matinees (Scientist / Soldier / Scout / Leader × Autobot / Decepticon) key 70 → 60 (Scout, Leader) or 65 (Scientist, Soldier) over 0.5 s, CIM_CurveAuto with zero tangents | cooked levels | CONFIRMED | `tools/render/build_scene_floatprops.py` → `matinee_floatprops.json` in every map's render data (keyed by SeqAct_Interp / InterpData / group / property); `IRenderer::frontendFloatTracks()` + `IRenderer::evalInterpCurveFloat` (UE3 FInterpCurve::Eval). Verified: Scout - Autobot 70 → 68.44 → 65 → 61.56 → 60 (`WFC_FOVTEST`). Frontend evaluates per frame and passes the FOV to drawFrontendScene [PARTIAL until wired] |

## MILESTONE 11 — REAL RELEASE PATH: MAPS DRAWN WITHOUT DEPTH TESTING AFTER THE MENUS (2026-10-04)
Input: the human recording of `Rebuild\build\release\bin\wfc_rebuild.exe` (M06b): Streets and Berth incomplete. The
human's session log is `F:\Transformers Rebuild\wfc.log` (account OhTruman, 20:50). Full evidence:
`docs/handoffs/M11_RELEASE_PATH_MAP_REGRESSION.md`.

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Root cause | `GLRenderer`'s 3D frame never established depth test / func / scissor / stencil / colour mask / polygon mode (set once in `init()`). The frontend GFx pass left `GL_DEPTH_TEST` disabled, so after the menus every map surface was drawn without depth testing | real Release exe + human profile, same pinned camera: direct boot vs frontend route submit identical draws (1900; 1468 world / 394 BSP); entry state depth=1 vs depth=0; 88 % of pixels differ | CONFIRMED ROOT CAUSE |
| Second owner | Frontend's GFx pass did not restore the state it changed (Experimental bisect b1fce97); fixed in agents/frontend a96f841 (save / restore). Rendering's frame now establishes its own state regardless | Frontend report, Rendering reproduction | CONFIRMED |
| Loading / data | human log: Streets 1,983,988 tris, 2,249 submeshes, 2,239 programs, 2,081 lightmap bindings; Berth 1,292,885 tris, 1,755 programs; render-data root and map dir correct; texture paths absolute | human wfc.log | CONFIRMED: not involved |
| Fix on the real path | integration 681fd29 + Rendering, Release layout, integration render data, human profile: frontend → Streets equals direct boot (apart from HUD / character); Streets → Berth → Streets: identical Streets counts (1641 / 357), Berth 1273 / 288, 0 opaque draws without depth test, 0 GL errors | captures `work/m11/cycle`, `rpc_*` | VISUALLY VERIFIED |
| Resolution / fullscreen | no correlation: cold boots at 1280×720 windowed, 1920×1080 windowed and fullscreen (desktop 2560×1440); runtime switch 1920×1080 → fullscreen → Streets; restart with persisted settings. All draw the full structure, 0 GL errors | `work/m11/r*`, `rtres*` | CONFIRMED (disproved as cause) |
| Long session | one process, 10 alternating Streets / Berth matches: all captures pass, 0 GL errors; loaded Streets 2,867 → 2,940 → 2,945 → 2,950 → 2,950 MB (one +73 MB step, then flat); Berth 2,725 → 2,687 MB | `work/m11/long` | HIGH CONFIDENCE (no growth / leak in Release) |
| Memory after Seed (Frontend: +460 MB, Debug) | reproduced as a one-time high-water mark, not a leak. Renderer-only cycle (WFC_MEMCYCLE): Streets x4 unloads 2,982 -> 3,119 -> 3,126 -> 3,133 MB; alternating Streets / Seed goes up and down and returns to the Streets plateau (3,127 MB). Allocator / driver pooling of freed memory. Fix: large world meshes no longer keep a CPU copy in the shader path (unused there; kept for WFC_PICK and small effect meshes). Load peaks are about 100 MB lower (Streets 3,291 -> 3,194 MB, Seed 3,604 -> 3,480 MB) | `work/m11/seedmem`, MEMCYCLE | HIGH CONFIDENCE (no leak) |
| AMD driver resets | no GL errors and no instability in any single-process run after the fix | runs above | UNKNOWN (not reproduced) |
| Validation | new: opaque draws without depth testing and GL errors FAIL the verdict. `tools/render/release_path_check.sh`: the player route with no data override, structural floors on submitted world + BSP submeshes (Streets 2801, Berth 2059 in every run). It FAILs the reproduced bug (`WFC_M11_INHERITSTATE`) on every capture, title included, and passes the fix. Black / flat heuristics now judge only levels (the lobby dome produced 98 false FAILs) | rpc_good / rpc_broken | CONFIRMED |
| Fullscreen size, HUD scale, movie bars | fullscreen ignores the saved resolution (desktop size); GFx stage scaling | captures | handoff (Frontend / platform) |

## MILESTONE 10 — M06 PLAYTEST VISUAL REGRESSION: ROOT CAUSE, GUARDS (2026-10-04)
Input: human recording of the integrated M06 executable, showing a black Streets world and malformed menu
backgrounds. Full evidence: `docs/handoffs/M06_PLAYTEST_VISUAL_REGRESSION.md`.

| Item | Finding | Evidence | Mark |
|---|---|---|---|
| Root cause | The Release executable (`build/release/bin`) resolved render data to `build/work/render`, which does not exist. Every map and frontend scene silently fell back to the legacy fixed-function renderer | integration 95edd7b rebuilt locally: human path with data correct; the same path without data reproduces every symptom in the recording | CONFIRMED |
| Render data | integration's Streets data equals Rendering's (geometry, lightmaps, LVV, decals, movers, FX byte-identical; materials and lighting differ only in absolute vs relative file paths) | byte compare | CONFIRMED |
| Fix | `renderDataRoot()` searches `work/render` up to 4 levels above the executable; root logged | Release-layout copy finds the data without env; Streets 0 px vs M08 | VISUALLY VERIFIED |
| Silent fallback | ERROR log, `renderDiagnostics().originalPath`, red screen frame when render data was asked for and missing | no-data run: FAIL with reason, red frame visible | VISUALLY VERIFIED |
| Scene transitions | title → lobbies → Streets → match end → lobby → Streets: both matches have identical draw / resource counts (2033 draws, 1641 world, 357 BSP, 161 materials, 0 drawn without a program), the same resources as a direct boot | merge preview (95edd7b + this branch), WFC_VISUALCHECK | CONFIRMED: no leak |
| Inherited GL state | the frontend / GFx host leaves depth test off, blend ONE / ONE-or-ONE_MINUS_SRC_ALPHA, a texture bound. The pipeline sets its own state; image and counts are unchanged | `gl_entry_state` per capture | CONFIRMED harmless (no blind reset added) |
| Title scene inputs | Frontend camera = authored CameraActor_6585 (−6701.84, −15212.47, 237.44; −0.61° / 72.42° / 0.34°; FOV 45). 88k matinee poses applied; 2 unknown names: a camera (not drawn) and Emitter_13640 (a laser emitter carried by a matinee track: map FX do not follow poses yet) | pose counters | CONFIRMED / emitter-follow PARTIAL |
| Character preview | Was not instantiated: no path existed on either side. Now `setFrontendSceneDraw` draws Gameplay-owned bodies in the lobby scene. A roster chassis (Bumblebee) renders with its original materials, the lobby's DirectionalLight_3388 and `setCharacterColors` overrides. Body data is present for all 33 chassis (66 glTF). Posing, animation and the PreviewGuy placement are Gameplay / Frontend's | preview capture, frame report | VISUALLY VERIFIED (draw path) / PARTIAL (bind pose, placement) |
| Lobby background | authored room: SpaceDome, props, 12-tri BSP. Its BSP was never generated (`build_lighting` numpy bug, Integration fix 7536ab1 now applied); generated now. The lobby scene drew `UI_CharacterCustomization` render data (54/162 materials, no chassis) instead of the persistent `UI_PartyLobby` (156/162): the persistent level is now preferred | material counts, scene capture | CONFIRMED / class cameras PARTIAL |
| UI render data | the five UI families were not part of any standard build (a tree without them shows black menus). `-Map Standard` now builds them; the shared character shader package `TR_AllShader_p.xxx` (standalone seek-free) is a material fallback, as the original loads it with the preview character. Streets output is unchanged (0 GLSL changes, suite refdiff 0) | build logs, suite | CONFIRMED |
| Visual regression guard | `WFC_VISUALCHECK`, `tools/render/visual_check.py`, `tools/render/visual_suite.sh` (5 fixed Streets cameras, title scene, human flow) | good build passes all captures; no-data build fails all | CONFIRMED |

## MILESTONE 09 — FRONTEND SCENES, LOADING, ROSTER READINESS, RE OVERNIGHT HUD (2026-10-04)
Inputs:
- RE `OVERNIGHT_2026-10-04_HUD_VEHICLE_FRONTEND_ROSTER_AI.md` (A HUD, D frontend backgrounds, E roster);
- RE `MILESTONE05_PLAYTEST_RE.md` §6 (loading);
- AssetTools `ui_scenes.json`, the UI level exports, and `mp_content/mp_characters.json`;
- Frontend lane requests (scene entry point, render data for the UI families).

| Item | Original | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Minimap / radar / compass | none in shipped MP | RE A9 (UnrealScript, every GFx pool, authored.db, xex strings) | CONFIRMED (absence) | none drawn; any future minimap is a labelled modern extension |
| Title / main-menu background | live level UI_FrontEnd_m + streamed UI_FrontEnd_capture_VIG_m, Matinee camera (CameraActor_6585, FOV 45) | RE D, AssetTools ui_scenes | CONFIRMED | `loadFrontendScene` / `drawFrontendScene`: Cybertron orbit scene with energon rings, debris, station, nebula. VISUALLY VERIFIED from the authored camera. Matinee actors, Kismet-activated emitters and lens flares are PARTIAL (need the Frontend's sequence state) |
| Lobby / customization background | UI_PartyLobby_m / UI_Lobby_m stream UI_CharacterCustomization_m | RE D | CONFIRMED | the customization room is loaded and drawn (space dome from the default camera). The class cameras (15 Matinees), the preview pawn and the CybertronCard Kismet are Frontend-driven [PARTIAL] |
| Campaign lobby | UI_CampaignLobby_m SpaceDome | RE D | CONFIRMED / look UNKNOWN (AssetTools) | drawn |
| Multi-level composition | persistent + streamed levels together | RE D | — | one render-data map at a time (the family with the most scenery) [PARTIAL] |
| Loading movie during blocking loads | Bink on the rendering thread; closes on `CanCloseLoadingMovie` | RE PLAYTEST §6 | CONFIRMED (names) / HIGH (semantics) | `setLoadYield`: a cooperative present between bounded steps (longest 70 ms on UI_FrontEnd). Not a separate render thread [PARTIAL] |
| TextureSample output 0 | RGB float3 (mask R, G, B) | UE3 node outputs; `Append(TexSample, TexSample.A)` in EnergonRing compiles only so | CONFIRMED | matc fixed (EnergonRing materials failed). Streets 231/231 (now 325/325 with the roster) permutations match; sweep median 0 px |
| Character roster readiness | 33 chassis; 4 default classes | RE E, AssetTools mp_characters | CONFIRMED data | character materials from the roster (98, all verified); per-owner light environment / applier colours (`setDrawOwner`); the Optimus path is unchanged (idle robot / vehicle 0 px vs M08) |
| Map-generic render data | — | — | — | packages from map.json sublevels, LM per sublevel, no-BSP / no-fog maps; Streets output unchanged |
| Per-draw uniform cost | — | — | — | uniform locations cached per program: scene submit 4.9 → 3.9 ms (shared, loaded machine) |
| HUD (Hud_GFX) | layout, kill feed, announcements, popups, scoreboard, end message | RE A0–A8 | CONFIRMED | Frontend runs the movie (`docs/handoffs/FRONTEND_INMATCH_HUD.md` updated with the exact values); the Canvas markers are Rendering's |
| Brightness (profile GammaSetting) | DisplayGamma = 2.2 + Lerp(-0.95, 0.95, GammaSetting/100); default 50 → 2.2 | decompiled HmProfileSettings.GetGammaSetting, HmPlayerController → DisplayDataStore "Gamma" | CONFIRMED (mapping) | `setDisplayGamma` drives the scene resolve and Canvas tiles; default unchanged (0 px). Whether GFx / Bink / Canvas simple elements also follow DisplayGamma is UNKNOWN (the decoded Canvas simple-element shader has an InverseGamma×2.2 exponent, not yet applied) [PARTIAL] |
| Character jitter (M05) | — | M08 measurement | — | handed off to Gameplay (unchanged on agents/rendering; the patch is in `docs/handoffs`) |

## MILESTONE 08 — PLAYTEST REGRESSIONS, IN-MATCH HUD OWNERSHIP, CANVAS LAYER (2026-10-04)
Inputs:
- the human playtest of integration milestone 05;
- RE `MILESTONE05_FRONTEND_GAMEPLAY_BLOCKERS.md` §G / §H and `MILESTONE05_GAMEPLAY_UNKNOWNS.md` §5;
- AssetTools `frontend_hud.json` / `future_hud_handoff.json` / `frontend_loading.json`;
- a `git archive` export of integration/milestone-05, built in `work/m08/int05` for measurements. No merge.

| Item | Finding | Mark | Owner / action |
|---|---|---|---|
| Character "interlacing" (robot + vehicle) | Gameplay's obstruction camera (pass 20) stores a world camera position at the 60 Hz tick while the view rotation changes every rendered frame. Above 60 Hz the pawn swims on screen: 0.0095 screen units / frame at 144 Hz, 0 at 60 Hz (which is why lockstep tests passed). Not the renderer: skinning, frame loop, weapon attach and vsync are unchanged | VISUALLY VERIFIED (measured) | **Gameplay**: `docs/handoffs/GAMEPLAY_CAMERA_FRAME_PACING.md` + verified patch (0.00001 at 60 / 144 / 240 Hz, both camera models) |
| Vehicle rear propulsion "open / close" | the boost FX follows the Driving state, as authored (BoostFx). On M05, Driving drops to Hovering 15–31 times per 14 s with boost held; every drop is a PROVISIONAL hull probe's frontal block at floor level, then a 0.5 s drift and a re-boost (≈ 0.6 s cycle) | VISUALLY VERIFIED (logged) | **Gameplay**: `docs/handoffs/GAMEPLAY_BOOST_FX_FLICKER.md`. No renderer smoothing added |
| In-match HUD (clock, team / player score, health, ammo, crosshair, kill / score messages) | Scaleform Hud_GFX (`GameMessage`, `PointEvent`, `RewardAnnouncement`, `GameAnnouncement`; data stores + pushes). Spectate / respawn = MultiplayerRespawn_GFX, end = EndGameStats_GFX: already opened by the Frontend runtime in M05. Hud_GFX itself is not instantiated | CONFIRMED (RE / AssetTools) | **Frontend** GfxHost: `docs/handoffs/FRONTEND_INMATCH_HUD.md`. The renderer reticle stands down (`ReticleState.visible = false`) when Hud_GFX draws its crosshair |
| Kill feed layout / rows / lifetime / fade | Hud_GFX timeline + AS2 behaviour | CONFIRMED owner / details by running the movie | Frontend |
| Radar / minimap | no radar or minimap object in the authored data | UNKNOWN (RE verifying) | nothing drawn |
| Player / objective markers | Canvas `TnObjectiveMarkerTypeSprite.Draw` | CONFIRMED (RE) | **Rendering**: `render::HudMarkers` from the authored setups (`_ui/hud_markers.json`, 40 types; Versus ally 0.08 / 0.04, enemy 0.03125, focus 0.02734 × width, 1.0 s hysteresis, 3000 UU auto-focus, label colours). VISUALLY VERIFIED (`WFC_MARKERTEST`). Gameplay supplies the rule-visible markers |
| Canvas text | UE3 UFont: FFontCharacter table + CharRemap (cooked) | CONFIRMED data | `drawCanvasText`: MarkerFont 371 glyphs (USize advance, VerticalOffset, alpha coverage). Label centring PROVISIONAL |
| Collision report tooling | — | — | `WFC_PICK=1`: authored component / StaticMesh / material / distance / normal of the surface under the crosshair (`IRenderer::pickWorld`, diagnostic only), plus the movement collision world's hit on the same ray ("collision ok" / "differs" / "NONE") |
| Canvas simple elements (text, texture tiles) | engine PS: `oC0.rgb = exp(log(tex × ColorScale + ColorBias) × InverseGamma × 2.2)` (saturated), `oC0.a = tex.a × ColorScale.a + ColorBias.a` | CONFIRMED shader (sc_engine) / runtime `InverseGamma` value UNKNOWN | text drawn display-referred (= InverseGamma 1/2.2); HUD gamma stays PARTIAL until the Canvas gamma value is known |
| Menu background | the UI_FrontEnd_m 3D scene (orbit cameras, energon rings) | CONFIRMED source / not exported | the renderer loads it through `loadMapRenderData` once AssetTools exports it; no substitute |
| Regression | glass (9 views) and steam over time (12) identical to M07; validation captures (idle, walk, jump, fire, vehicle idle / move / boost, both transforms) | VISUALLY VERIFIED | — |

## MILESTONE 07 — STREETS CLEANUP, FRONTEND / NEXT-MAP READINESS (2026-10-03)
Evidence:
- RE-Workspace M05 notes (read-only): `MILESTONE05_GAMEPLAY_UNKNOWNS.md` §5 (Canvas HUD markers) and §6 (pickup factory
  mesh and spin), and `MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md`;
- the agents/frontend a55a4e9 handoff (map unload);
- the latest render_index (pickup_factory_visuals, including the objective rest meshes).

The renderer contract for the other lanes is in `docs/RENDERER_CONTRACT.md`.

| Item | Original (WFC) | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Pickup factory mesh placement | the template mesh is attached to the factory actor with zero offset; actor rotation and DrawScale apply | RE M05 §6 (script) | CONFIRMED (script) / HIGH (native attach) | mesh at the factory transform from render_index, authored rotation kept (was yaw from 0) |
| Pickup spin | the factory actor is PHYS_Rotating at PickupRotationRate only while available; it freezes when taken and resumes from that yaw | RE M05 §6 | CONFIRMED | spin accumulated per factory while available, frozen while hidden (was continuous from map start); local-space FX follow it. VISUALLY VERIFIED available / taken / respawn |
| Health / overshield | no PickupFactoryMesh, no spin | RE M05 §6 | HIGH | only their FX mesh particles draw; render_index "mesh particle" entries are not drawn twice |
| Flag / bomb rest mesh | Code of Power / MP bomb skeletal mesh at rest pose, +150 Z, spinning, CTF / EXT only | render_index (supersedes UNKNOWN) | CONFIRMED data | drawn and gated by the factory's game rule; hidden in TDM. VISUALLY VERIFIED (TDM none / CTF shown). Weapon materials not in the compiled set (glTF fallback) [PARTIAL, CTF/EXT only] |
| HUD markers | Canvas material tiles (`TnObjectiveMarkerTypeSprite.Draw`), materials in UI_HudMarkers_p, per-draw material params | RE M05 §5 | CONFIRMED | `IRenderer::drawMaterialTile`. All 15 marker materials compiled with runtime parameters and verified (231/231 permutations). VISUALLY VERIFIED: base marker friendly / enemy / off-screen arrow, enemy, death, health bar |
| MarkerAlly_MAT | empty expression tree | cooked material | CONFIRMED | draws nothing (ally tags = label + health bar) |
| Canvas gamma | — | canvas tile shader not decoded | PARTIAL | display gamma 1/2.2 applied as in the scene post |
| 2D composition | GFx / Bink / loading presented over the scene | Frontend handoff | — | `drawScreenTriangles` (blend modes, scissor, clamp), `updateTexture`. VISUALLY VERIFIED (fade + panel) |
| Level travel | — | Frontend handoff (renderer leaked the previous map) | — | `unloadMapRenderData` / `Pipeline::release`; in-process cycle VISUALLY VERIFIED (`WFC_RELOADTEST`) |
| Map-agnostic data | — | audit | — | asset roots from WFC_ASSETS / WFC_CONTENT; props, pickups and destructibles from render_index; tools take the map name, AssetTools manifest prefix, inline lightmaps from the ART package. Movers and map FX outputs identical to before |
| KOTH ring colour | `CaptureColor[team]` / `NeutralColor` vector params (MaterialParamNames) | RE M05 §3 | CONFIRMED (script) | not wired: needs a Gameplay hook; KOTH only [PARTIAL] |

**Regression of the playtest categories** (same cameras as M06; VISUALLY VERIFIED unchanged):
- glass: 9 views, 0 px different;
- steam over time: 12 views, 0 px;
- fog sheets: 138 views.

In the fog-sheet views the only differences are:
- the uncoloured steam emitter: the M06 scan predates ColorByParameter;
- one flag mesh, a regression caught and fixed by the rule gate.

Normal play (robot), vehicle form, transformation, firing, pickups and self-tests (shadows 32/32, DLE 20/20) were re-run.

**Still open (original-engine unknowns, documented):**

| Item | Mark |
|---|---|
| Translucent alpha output / blend state | PARTIAL |
| Darkening (modulate) blend with Opacity | UNKNOWN; no map material depends on it |
| HmParticleModuleGravity acceleration | UNKNOWN |
| Steam LOD 1/2 | DirectSet, unused |
| Totem and KOTH team colours | PARTIAL; need Gameplay ownership hooks |
| Canvas gamma | PARTIAL |
| Visible BSP without lightmaps | PARTIAL; RE orientation question open, not visible from gameplay viewpoints |

## MILESTONE 06 — HUMAN PLAYTEST: TRANSLUCENCY, SMOKE, GLASS (2026-10-03)
The human playtest of integration milestone 04 drives this pass. Evidence comes from the shipped Xenon shader caches
(`work/re/sc_streets_art.bin`, `sc_base.bin`, `sc_engine.bin`, disassembled with `tools/render/xenos_dis.py`), the
cooked material graphs and particle streams, and A/B runtime captures. Each A/B pair is pre-M06 behaviour
(`WFC_M05TRANS=1` + the pre-M06 material file) against the new behaviour, from the same camera.

Marks: CONFIRMED ORIGINAL (shipped shader / cooked data) / HIGH (stock UE3) / VISUALLY VERIFIED (A/B capture
inspected) / PARTIAL / UNKNOWN.

**Root causes of the reported defects**
1. **Translucency drawn mid-frame.** World translucent subs were drawn right after the world mesh's opaque subs, before
   the BSP, decals and characters. BSP floors and ramps behind glass or fog sheets then painted over them, so geometry
   under transparent panels appeared in front of them, and dark translucent cards showed through foreground structure.
   The scene-depth copy for DepthBiasedAlpha was also taken before the BSP existed, so soft fades saw no wall behind them.
2. **Additive opacity ignored.** WFC's additive base pass multiplies colour by Opacity, but the rebuild output colour
   unattenuated. The 46 FogSheet_DepthBiased cards (Blue / RED / Purple) draw their opacity from a 25-50 m
   depth-biased fade. Without it they drew at full strength over walls: the distance-dependent curtains.
3. **Particle soft fade collapsed.** The translator read DynamicParameter output 1 as `.x` (Desat1 = 0) and fed it to
   BiasScale. Steam therefore cut hard against geometry: the "static" slab of steam under the fallen panels.
4. **Steam colour missing.** ParticleModuleColorByParameter ('SteamColor') was not implemented, so the steam rendered
   grey-white instead of the authored blue-purple.

| Item | Original (WFC) | Evidence | Mark | Rebuild |
|---|---|---|---|---|
| Translucency pass | all opaque first; translucent primitives back to front by view depth of the bounds origin | stock UE3 FTranslucentPrimSet | HIGH | translucent subs of persistent meshes (world, BSP, props, FX meshes) and sprite batches are queued and drawn after every opaque draw, back to front; scene depth for soft fades is then complete. Decals stay in the opaque phase. VISUALLY VERIFIED (fog / glass / nav A/B) |
| Additive output | oC0.rgb = Color * fog * **Opacity** * SceneColorBiasFactor | Xenon PS of FogSheet_Parent_MAT (ART cache, line 28) and of ENV_ForceField glass (ART cache, lines 57-58) | CONFIRMED ORIGINAL | `wfcTranslucentOut`: additive colour *= Opacity |
| Translucent / additive kill | discard when Opacity < 1/255 | `kill_gt` against 0.00392 in both shaders | CONFIRMED ORIGINAL | applied for translucent and additive |
| DepthBiasedAlpha, unconnected Bias | 0.5 | FogSheet PS: `UniformScalar_3 * 0.5` | CONFIRMED ORIGINAL | matc default 0.5 (was 0) |
| DepthBiasedAlpha BiasScaleInput | uniform input replaces BiasScale; a per-vertex input is not compiled | FogSheet: ScalarParameter 'BiasScale' is the scale uniform; Steam_Mat: `(1 - SmokeBall.b) * 200`, the DynamicParameter is unused | CONFIRMED for both materials / general rule PARTIAL | matc: varying subgraph (DynamicParameter, VertexColor, textures, ...) -> BiasScale constant |
| DynamicParameter outputs | four scalars, output k = Param k+1 | 50 cooked nodes use outputs 0-3 only; ParamNames match (1 = 'DepthBiasScale' everywhere) | HIGH | matc fixed (was a 5-output reading shifted by one). 9 materials recompiled (FogSheets, Steam, Glow_Mod, Tracer_Smoke, muzzle FX) |
| Particle sprite size | corner = (TexCoord - 0.5) * Size: Size is the full width | sprite vertex factory VS (engine cache), lines 18-19 | CONFIRMED ORIGINAL | unchanged (already full width) |
| ColorByParameter | Color = BaseColor = PSC InstanceParameter 'SteamColor' (else DefaultColor white) | FName 'SteamColor' in the compiled Steam LOD stream (offset 298); 7 of 8 Emitters author it; CDO DefaultColor white | CONFIRMED data / FLinearColor(FColor) gamma-2.2 conversion HIGH | implemented; steam is the authored blue-purple, soft-edged and evolving (VISUALLY VERIFIED over 1.6 s) |
| FogSheet distance / angle behaviour | emissive * saturate(PixelDepth * 0.0005 / FadeDistance) * pow(saturate(CameraVector . N), DensityFalloff) | graph + PS | CONFIRMED ORIGINAL | authored: sheets fade out near the camera and when viewed edge-on. Not a defect, and left as is |
| FogSheet / BckSillouhetteSmoke animation | no Time / Panner in FogSheet_Parent_MAT; BckSillouhetteSmoke pans | graphs | CONFIRMED ORIGINAL | FogSheets are intentionally static art; the smoke cards animate through material Time |
| Glass bridge / floor panels | ENV_ForceField_ALL_MAT additive: fresnel + scanline + fence mask; Opacity = clamp(pow(1 - DBA, 4) * 5 + 0.2) | graph + PS | CONFIRMED ORIGINAL | faint 0.2 film with an intersection glow, composited after all opaque geometry, so nothing below can draw over it. VISUALLY VERIFIED |
| Translucent (alpha) output | PS writes oC0.w = SceneColorBiasFactor.y, colour premultiplied-like (Steam_Mat) | Steam_Mat PS | PARTIAL (blend state not in the PS) | unchanged: SrcAlpha / InvSrcAlpha |
| Modulate with Opacity | — | not decoded | UNKNOWN | unchanged (only weapon Glow_Mod uses it; map stains have Opacity 1) |
| HmParticleModuleGravity on steam | enabled (flagA 1); GravityMultiplier CDO 1.0; acceleration native | stream | UNKNOWN | not applied |
| DepthPriorityGroup / sort priority | all 1,952 components SDPG_World; no TranslucencySortPriority | props_authored | CONFIRMED | single world translucency list |

**Before / after (VISUALLY VERIFIED):** `work/m06/fog_ab.png`, `fogscan_top.png` (46 sheets x 3 distances; the worst
cases fs02 / fs40 / fs41 / fs04 show the flat blue veil before, and fs29 shows black polygons through a wall before),
`glass_ab.png` / `glass_crop.png`, `steam_t.png` (grey hard-cut slab -> purple soft steam, frame-to-frame motion),
`nav_ab.png`, `beam_ab.png` (light-ray cones and glow spheres change little), `play.png` / `play_vehicle.png`
(normal play, robot and vehicle).

**Diagnostic false positives (not defects):**
- Nav-node cameras placed on pickup factories sit inside the pickup beam and cube meshes.
- `WFC_ALBEDO` shows unlit additive glass as black (DiffuseColor 0).
- The dark VentWall near FFA start 15650 is authored lighting (lightmap scale 0.05).

---

## MILESTONE 05 — MP_IAC_STREETS NORMAL-PLAY MAP COMPLETION (2026-10-03)
Provenance: AssetTools **883b94b..da67634** (render_index.json: mode_visual_state, tdm_render_state, pickup_factory_visuals,
pickup_fx_components; STREETS_NATIVE_FIDELITY_M05), ReverseEngineering **940aa79** (flagA = bEnabled) and **fc05672**
(MILESTONE04_STREETS_FIDELITY_REMAINING), Gameplay **d122ef4** (map clock / mode / per-actor / pickup state API).
Marks: CONFIRMED / HIGH / PARTIAL / PROVISIONAL / UNKNOWN; VISUALLY VERIFIED = deterministic runtime capture inspected
(not a comparison with the original game). Slice mode is TDM: with no Gameplay rules set, every rule-gated actor is hidden.

**Correction to MILESTONE 04:** content glTFs (FX meshes, totem, KOTH ring, destructible states, ammo crate) carry the same
Y-up local vertex layout as world.glb meshes. Their placement is P*A_ue*P (P = y<->z swap) without a winding flip. M04 drew them
on their side, so the M04 destructible and totem captures were not valid. They are re-verified below.

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Content-mesh placement | UE matrix in UE axes; glTF content Y-up | AssetTools glTF convention; world.glb cross-check | CONFIRMED | `ueRowsToGltf` = P*A*P, no winding flip. Totems, KOTH ring, destructible, crate and FX meshes are upright (VISUALLY VERIFIED) |
| Map clock | Gameplay MapState seconds since GameplayStarted | Gameplay d122ef4 `setMapClock` | CONFIRMED API | movers, totem idle animation and pickup spin are evaluated at `mapTime()` (renderer clock only as fallback) |
| Domes x3 / SkyBeam x3 | 15 deg/s yaw; 9.0022 s Matinee sway (keys 0 / 4.5 / 9 s) | streets_movers.json | CONFIRMED | moving geometry VISUALLY VERIFIED (dome_t1/t2, sky_t0/t45) |
| Mode visibility (TDM) | 19 actors hidden: 4 bases (collide), 3 totems, 5 KOTH zones, 2 flag + 1 bomb factories (Disabled), 4 capture/plant points (no mesh) | render_index tdm_render_state | CONFIRMED | rule-gated plus `setActorHidden`. Audit: 4 bases / 3 totems intentionally invisible; flag/bomb effects intentionally invisible; nothing reported missing |
| KOTH active-zone ring | template ActiveMeshComponent0 pTorus1_STAT, zone DrawScale3D (1,1,2), CaptureZone_Reverse_MAT_INST | render_index | CONFIRMED | drawn only for the zone Gameplay unhides (VISUALLY VERIFIED koth_dm / koth_active) |
| Domination totems | NEU_EnergonTotem_SKEL + Conquest_Ring_MATINST (additive unlit, EnergonColor (1,1,1), intensities 5 / 2); StandBy idle animation | render_index; MIC | CONFIRMED | DOM only; upright and animating (VISUALLY VERIFIED). The saturated white ring is the authored neutral colour. Capture recolour (EnergonColor = CaptureColor[team], RE runtime semantics) needs a Gameplay DOM-ownership hook: not wired [PARTIAL, DOM only] |
| Pickup FX (45 particle components) | 8 Steam_Sm_FX + 37 pickup PSCs; 27 drawn while available, 24 in TDM; 10 Pickup_FX copies on health/overshield never attached | render_index pickup_fx_components; RE 940aa79 | CONFIRMED data / module semantics HIGH | flagA = bEnabled (RE HIGH) makes every emitter determinate, so all are simulated. Audit: emitters 32 drawn / 13 intentionally invisible / 0 unknown, matching AssetTools' 24 + 8 / 10 + 3 |
| Pickup beam | SetPickupVisible -> ActivateSystem, SetPickupHidden -> Deactivate | decompiled script | CONFIRMED | `setMapEffectState("<factory>|custom/highlight")`; ammo light-volume beam measured (pixel diff with FX on vs off) and VISUALLY VERIFIED available -> taken -> respawn |
| Ammo crate mesh | TnAmmoCratePickup.MeshComponentA PROP_NEU_AmmoPickup_STAT, CullDistance 8000, yaw 10000 UU/s, CastShadow false | render_index pickup_factory_visuals | CONFIRMED mesh / attach offset UNKNOWN | drawn at the factory origin and hidden with the custom effect; spin phase continuous from map start [PROVISIONAL]; native attach offset [UNKNOWN, AssetTools] |
| Health / overshield | HealthPickup_FX / OvershieldPickup_FX mesh emitters | decoded systems | CONFIRMED data | energon cube / shield mesh emitters drawn with the mesh material (bOverrideMaterial without a material: fallback rule UNKNOWN, mesh material used) |
| Flag / bomb factories | at-rest visual | — | UNKNOWN (AssetTools) | not drawn (Disabled in TDM anyway) |
| Placeholder content | — | — | — | graybox pickups (WFC_GRAYBOXPICKUPS) and the weapon-test dummy box (WFC_DAMAGETARGET) are no longer spawned in the slice |
| DefaultMaterial BSP | 62 surfaces / 87 nodes explicitly reference EngineMaterials.DefaultMaterial | AssetTools M05 §1; RE fc05672 §1 | CONFIRMED data | drawn as authored. The partly visible ceiling (BRUSH_11694) contributes 255-812 px at luminance 4-7 in its views; the BRUSH_9457 wall is mesh-enclosed from the tested cameras. The DefaultMaterial texture is null in the cooked material [PARTIAL]; no substitute |
| Arch black vertex-lit patch | 30/522 samples zero in all three coefficients; sibling 15855_SMC zero at 28 of the same | RE fc05672 §2 | CONFIRMED authentic | left as authored |
| MonitorScreen family | two panned ScreenText layers, Time-only, Screen_Color, Edge_Burn; no render target / movie / Kismet | RE fc05672 §3 | CONFIRMED | translation unchanged. Verifier fixed: the compiled texture-expression block is at offset 2 mod 4 and is now scanned. Materials 216 / 216 match (was 213). Active and Purple screens scroll (VISUALLY VERIFIED) |
| BSP without lightmaps | 616 nodes LightMapType 0; RE: 26 nodes (~3,694 m2) visible, west perimeter x = -3840 facing -X plus 2 +Y faces at Y = -26624 | RE fc05672 §6 | PARTIAL | runtime: the gameplay viewpoints nearest to them (west team starts x = 19.9 m, north pickups z = -284 m) see placed meshes there, not BSP. Viewed from their front side, the faces render near-black. Question for RE below |
| Culling | no cull distance / LOD / volumes / streaming | AssetTools M05 §2, §6 | CONFIRMED | baked-placement frustum cull vs WFC_NOFRUSTUMCULL: 60 nav views, 0 px different. Back-face (WFC_NOCULL) diffs are only back sides of single-sided light planes / sky dome seen from inside geometry. Winding vs normals agree 99.95% on all 1,952 instances (mirrored min 0.948) |
| Dark spawn wall (FFA start 15650) | VentWall_7168 lightmap scale 0.05 on all coefficients | world.glb extras + lighting.json | CONFIRMED authentic | authored dark lighting, not a defect |

**Questions for RE / AssetTools (exact):**
1. Unlit BSP (fc05672 §6): the 26 "visible" west-perimeter nodes have a front normal of -X in glTF and render only from x < -38.4 m. Every nav/start viewpoint is at x >= 19.9 m. Which viewpoint sees their front side? Is the orientation in glbray's visibility test front-facing?
2. TnPickupFactory.SetPickupMesh native attach offset for the ammo crate (render_index placement PARTIAL).
3. Pickup spin phase: does the factory yaw reset on respawn, or run continuously?
4. Flag / bomb factory at-rest visual (only needed for CTF / EXT).
5. Mesh-emitter bOverrideMaterial with no material set: does it use the mesh material?

## MILESTONE 04 — MP_IAC_STREETS LIVING-WORLD PRESENTATION + CORRECTED MAP (2026-10-03)
Provenance: AssetTools **a23c675** (complete-map manifests: streets_rebuild_diff / movers / kismet / lighting_audit,
render_index, map_fx) and **8d8195e** (StaticMeshCollectionActor component transforms S(Scale*Scale3D)*R*T*Parent,
1906/1906 validated, 478 mirrored); Systems **8dcb861** (pickup script flow, RE d50c2a9 P2); ReverseEngineering
earlier commits for lighting/shadows unchanged. Marks: CONFIRMED / HIGH / PARTIAL / UNKNOWN; VISUALLY VERIFIED =
deterministic capture inspected (not a comparison with the original game).

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Corrected map geometry | 1906 SMCA members with component scale / mirroring | AssetTools 8d8195e | CONFIRMED (data) | render data regenerated from the corrected world.glb; nothing compensated in Rendering; 24-start sweep + 5 targeted areas VISUALLY VERIFIED as coherent |
| Mirrored / non-uniform instances | det < 0 winding, normals by inverse-transpose | engine convention | CONFIRMED | winding restored at bake (existing); normals now cof(M)*sign(det) (were M*n: wrong under non-uniform scale) |
| Decal receivers on SMCA | receiver world matrix = component * parent | 8d8195e props.json | CONFIRMED | decal builder uses the corrected per-component matrix (was the native parent only) |
| Lightmap inventory | 1795 texture (1794 bound + destructible), 28 vertex, 133 none | streets_lighting_audit | CONFIRMED | builder now also scans the BASE package (StaticInterpActor_5249 / 8037 texture, SkyBeam + SMCA_4920 vertex); 1795 / 28 match per component; BASE inline atlases copied from the slice export |
| Lightmap attachment after the geometry fix | bindings unchanged (1755 SMCA) | 8d8195e validation | CONFIRMED | join keys unchanged; no misattachment found in the sweep; one hard-edged black vertex-lit patch (Arch_4096_STAT, SMCA_7201 / 2542_SMC, 22 of 522 authored samples black) left as authored [VISUALLY UNVERIFIED] |
| Rotating domes (3) | PHYS_Rotating 2730 UU/s yaw (15 deg/s) | streets_movers.json | CONFIRMED | per-actor delta M(t) M0^-1 on the baked world (UE3 FRotationMatrix + UE->glTF map reproduce every world.glb node matrix exactly); VISUALLY VERIFIED |
| SkyBeam Matinee (3 actors) | SeqAct_Interp 9.0022 s loop from GameplayStarted, IMF_RelativeToInitial, quat slerp, CurveAuto position | streets_movers.json | track CONFIRMED / UE3 evaluation HIGH | implemented; VISUALLY VERIFIED |
| Objective bases (4) | bHidden; Kismet UnHide under TnGameRules_SingleFlagCTF / ScoreBombingRun | streets_kismet.json | CONFIRMED | resident, hidden unless Gameplay's active rules include either; VISUALLY VERIFIED (DM hidden / CTF shown) |
| Domination totems (3) | NEU_EnergonTotem_SKEL, DeactivatedLoopAnim EnergonTotem_StandBy; Conquest only | render_index.json; mode rule per directive | CONFIRMED data / rule HIGH | drawn + animated only under TnGameRules_ScoreDomination; VISUALLY VERIFIED; lighting via generic dynamic env [PARTIAL] |
| Destructible WallPanelSign | state 0 Base mesh + texture lightmap; 1/2 Chunk02 stump + chunks / debris / FX | streets_destructibles.json | CONFIRMED data | state 0 intact (with its lightmap) and states 1/2 stump VISUALLY VERIFIED at the authored off-map location (placed meshes with authored components were frustum-culled on untransformed bounds: fixed); physics chunk / debris / destruction FX not presented [PARTIAL]; Gameplay drives setDestructibleState |
| Steam_Sm_FX x8 | auto-activate, loop; Steam_Mat; decoded modules | map_fx.json + pstream | CONFIRMED data / UE3 module semantics HIGH | simulated from the decoded stream (Location x2, Lifetime, Size, SizeMultiplyLife, Rotation, Velocity + radial, ColorScaleOverLife, DynamicParameter); VISUALLY VERIFIED. Gravity (WFC HmParticleModuleGravity, native) PARTIAL (not applied); DynamicParameter slot order PARTIAL |
| Pickup FX | Pickup_FX (highlight), HealthPickup_FX, OvershieldPickup_FX | streets_pickup_fx.json | CONFIRMED data | drawn only emitters whose look is invariant under both plausible readings of the unproven pstream flagA / flagB (build_map_fx.py flag_analysis): Health / Overshield GlowMut + the health energon-cube mesh. UNKNOWN (not drawn): ammo highlight beam (SizeMultiplyLife flag), GlowADD x2 (ColorScaleOverLife flag), overshield mesh (SizeMultiplyLife flag) |
| Pickup state | spawn available (ammo highlight active, RE P2); SetPickupHidden: custom SetHidden+Deactivate, highlight Deactivate; highlight attached only for ammo crates / objectives | decompiled script via Systems 8dcb861 | CONFIRMED | setMapEffectState("<factory>|custom/highlight", active, hidden); VISUALLY VERIFIED take / respawn with WFC_PICKUPTEST |
| MaxPeakCount | WFC emitter field, CDO 1 | authored + PeakActiveParticles | HIGH | active-particle cap; 1800-frame runs bounded (33-41 sprites, 9 meshes) |
| Material permutations | — | verify_permutations.py | — | 211 / 214 match: MaterialInstanceConstants without their own static permutation are now verified against the master's compiled resource; unnamed parameter expressions attributed. PARTIAL: MonitorScreen family (3 materials, 11 components) |
| VectorParameter output channel | outputs 1..4 = R/G/B/A | UE3 material types | HIGH | applied only where the full-vector reading violates UE3 type rules: ParticleBase_BW_MAT (2 violations -> 0); 21 other affected materials compile validly either way and keep the current reading [PARTIAL] |
| DeadBodies_Mat_INST | static switch permutation | MIC native | CONFIRMED | verifies (fixed by the earlier numbered-FName switch decode); the AssetTools wrong-material flag predates that fix |
| Map audit (state-aware) | — | audit_map.py | — | static meshes 1937 correct / 4 rule-hidden / 11 unknown; emitters 8 correct / 24 unknown / 13 intentionally invisible; LVV, destructible (intact), decals 25/25, fog, post correct |

---

## MILESTONE 03 PASS 5 — NORMAL-PLAY CHARACTER SHADOW RUNTIME (2026-10-02)
Native provenance: RE-Workspace `notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_RUNTIME.md`, ReverseEngineering commit
**7033f18** (builds on 13c0953 / pass 4, Rendering checkpoint 566a87f). This pass supersedes the pass-4 rows "Projection
gates" (bit 0x4 is the SUBJECT's view relevance, not a light flag), "shadowFactor -> projection strength" and "Resolve /
blur". Character shadows now run in normal play without any opt-in.

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Synthetic projector | one synthetic shadow light per light environment; never the scene light | 0x82DE6598, vtable 0x822DD770 | **CONFIRMED** | `ShadowProjector` per DirectLightEnv (robot, vehicle) |
| Projector update | every proxy build (full and incremental updates): type (1 dir / 2 point / 3 spot), LightToWorld, Radius, ShadowFalloffExponent, cones, Min/MaxShadowResolution, ModShadowColor = shadowFactor; overwritten in place (no crossfade, no second projector) | 0x82CCE0A8, 0x82DDD128 / 0x82DDD240 / 0x82DDD368 | **CONFIRMED** | **APPLIED** (`updateShadowProjector`) |
| Directional projector values | Radius 327680 UU, ShadowFalloffExponent 2.0, cones (0, −1, −1, 0) | 0x82DDD128 | **CONFIRMED** | **APPLIED** (no Streets directional light casts a composite shadow: diagnostic only) |
| No record | shadowFactor within 1e-4 of white (and the earlier 0.05 drop) -> slot flags cleared, no shadow | 0x820A1AE0, 0x82DE6598 | **CONFIRMED** | **APPLIED** |
| Slot creation / registration / interaction linking | how the synthetic light is allocated, added to the scene and linked to the environment's primitives | — | **PARTIAL / UNKNOWN** | structural stand-in: the projector lives in the environment state and is cast for that environment's drawn form |
| Strength | FadeAlpha = 1.0 always; ShadowModulateColor = lerp(1, ModShadowColor, 1) = shadowFactor; all fading inside shadowFactor (0.2 luminance gap x smoothed visibility); no distance / resolution fade | 0x83056A20, 0x82D077F8 | **CONFIRMED** | **APPLIED**; the pass-4 authored-ModShadowColor opt-in substitute and WFC_SHADOWFADEALPHA are removed |
| Mod projection combine | out = lerp(lerp(1, SMC, atten), 1, PCF), atten = spot² (1 − saturate(|d/R|²)^ShadowFalloffExponent) | spot variant microcode | **PARTIAL** (directional / point variants not decoded) | decoded form for every type; directional uses the projector origin as the light position |
| Creation: light side | projector on, ModShadowColor not white (1e-4), light in at least one view | 0x83057028 | **CONFIRMED** | **APPLIED** (point / spot: light sphere vs view frustum; directional: always) |
| Creation: subject side | interaction PROJECTED for CastShadow ∧ bCastDynamicShadow; no ShadowParent; relevant ((relevance & 7) != 0) or visible; initializer type 1/2/3 | 0x82DBFA80, 0x83056A20 | **CONFIRMED** | **APPLIED**; Optimus robot / vehicle mesh components: CastShadow True (MeshComponent), bCastDynamicShadow True, bCastHiddenShadow False (authored.db) |
| IsShadowCast (relevance 0x4) | CastShadow required; hidden / owner-see cases -> bCastHiddenShadow; else within MaxDrawDistance (LODDistanceFactor) | 0x82DD8D58 | **CONFIRMED** | **APPLIED** (`shadowViewRelevance`); CachedCullDistance 0 -> unlimited; hidden forms are not drawn -> no shadow |
| DPG relevance | bit 5 + DPG from the proxy: DepthPriorityGroup, or ViewOwnerDepthPriorityGroup when bUseViewOwnerDPG and the view's owner matches | 0x82C8BCF0 | **CONFIRMED** | **APPLIED**; Optimus meshes: SDPG_World, no view-owner DPG (authored) -> World pass |
| Occlusion / preshadow | per-view shadow occlusion query; a preshadow when the subject is visible (static receivers from the light's static list) | 0x82DEA630, 0x83056A20 | CONFIRMED (existence) / PARTIAL (synthetic light's static list unknown) | not reproduced (no occlusion queries; preshadow casters unknown) |
| Shadow children | ShadowParent children folded into the parent's single shadow | 0x83056958 | **CONFIRMED** (mechanism) | the Ion Blaster mesh has its own LightEnvironment and CastShadow True; whether script sets its ShadowParent is UNKNOWN -> weapon not a caster (unchanged) |
| Skeletal proxy early-out | proxy 0x82C9E5C0 returns 0x8242 (foreground DPG, no shadow bit) under unverified conditions | — | **PARTIAL** | not reproduced |
| Shadow space | perspective for every type; origin = light position (point / spot) or B.Origin − (2R + 300 UU)·axis (directional); pulled back to √2·R; MinLightW 0.1 UU; MaxLightW = Radius or 2(2R + 300); DynamicShadowDepthBias input (0 on both forms) | 0x82E22A38, 0x82DBF678, 0x82DBF728 | **CONFIRMED** | **APPLIED** |
| Frustum fit / ScreenToShadowMatrix | VMX128 bounding fit of the projected corners; texel / half-texel terms | 0x8301CD08, 0x82CFB598 | **PARTIAL / UNKNOWN** | sphere-tangent perspective, depth clamped to [MinLightW, MaxLightW] [PROV] |
| Shadow resolution | clamp((int)(1.0·ScreenRadius), min(Min, Buf − 10), min(Max − 10, Buf − 10)); ScreenRadius = max(0.5·SizeX·P00, 0.5·SizeY·P11)·R / max(clipW, 1 UU); light 0 -> 128 / 1024; Buf 1024; 5-texel border | 0x83056A20 | **CONFIRMED** | **APPLIED** (Streets lights author no Min/MaxShadowResolution) |
| Blur | only if a shadow was drawn; H then V; 6 bilinear clamped taps at ±0.5 / ±1.5 / ±2.5 texels; weights {4, 2, 1}·(4 − s), normalised; horizontal output squared | 0x82DDB070, 0x82E0C8A0, Engine ShaderCache_10303 | **CONFIRMED** | **APPLIED**; GPU == closed form (max error 0 / 255) |
| Blur tie branch | (s(−B) == s(−A)) ∧ (s(+B) > s(−B)) alternative result | decoded swizzle uncertain | **PARTIAL** | off by default; WFC_BLURTIE=1 enables the decoded form |
| Transform | each environment owns its projector; only the drawn form casts | 13c0953 §4, 7033f18 §5 | CONFIRMED mechanism / environment survival across transform UNKNOWN | one projected shadow per frame through r2v / v2r; the hidden form's projector is stale but casts nothing |
| Normal-play result | — | — | — | spawn 18 robot (baked PointLight_4177_LC, ShadowModulateColor 0.098), spawn 21 robot (PointLight_14841_LC, 0.825), vehicle at 18 (0.105); spawns without a composite light: projector off, mask 1 |

## MILESTONE 03 PASS 4 — NATIVE CHARACTER SHADOW MASK, DLAC, BIAS / PCF (2026-10-02)
Native provenance: RE-Workspace `notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_PATH.md`, ReverseEngineering commit
**13c0953** (supersedes the pass-3 UNKNOWN rows for the shadow-mask write path and the DLAC formula). Authored data:
AssetTools commit **7a69756** (`manifests/fineaim_hud.json`, `streets_pickup_fx.json`, `streets_destructibles.json`).

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| ShadowMask target | A8R8G8B8 at the scene buffer size SizeX/f x SizeY/f, f = (SizeX > 960) ? 2 : 1 (640x360 at 720p) | 0x83037390, 0x8302AE28 (factor read from the image this pass) | **CONFIRMED ORIGINAL** | **APPLIED** (RGBA8 + D24S8 stencil buffer) |
| Mask clear / convention | cleared once per build to (1,1,1,1); stores visibility (1 = lit) | 0x83010218 (`*0x8370AE88`) | **CONFIRMED ORIGINAL** | **APPLIED**; no projection -> mask stays 1 (neutral texture) |
| Projection region | z-fail stencil volume of the 8 frustum corners (front zfail Inc, back zfail Dec, Always), then the box drawn where stencil != 0; stencil cleared after each light | 0x8304EF70 | **CONFIRMED ORIGINAL** | **APPLIED** (GL_INCR_WRAP / DECR_WRAP, depth clamp, back faces once per pixel) |
| Mask combination | blend Src = DestColor, Dst = Zero (rgb), alpha Src = Zero, Dst = One: mask.rgb *= projection.rgb, alpha untouched; every shadow multiplies into one mask | 0x82CF8048 | **CONFIRMED ORIGINAL** | **APPLIED** (BlendFuncSeparate) |
| Resolve / blur | resolve, then BlurShadowMask (horizontal, vertical) only if something was drawn | 0x82DEC190, 0x82DDB070 | placement **CONFIRMED ORIGINAL** / kernel **UNKNOWN** | resolve implicit; blur NOT run (no invented kernel) |
| Character read | ShadowMaskTexture .r at screen UV + ShadowMaskTexelOffset (0.5 / mask size) | 0x82DDAEF0 + uber-shader microcode | **CONFIRMED ORIGINAL** | **APPLIED** (point sampled; GL pixel-centre screen UV) |
| DirectLightAmbientContribution | rgb = CubeSum(LightsSH) / (CubeSum(AmbientSH + LightsSH) + 0.001), w = 0; LightsSH = overflow lights + (1 − f) of the crossfaded light; per environment, every update | 0x82CCE0A8, 0x82CED7F8, 0x82CED980, 0x82E2EDA8 | **CONFIRMED ORIGINAL** (copy to +0x1D0 HIGH) | **APPLIED** from the env's ambient cube (Streets: no SH probes; cube vs SH basis PARTIAL as before); measured 0 … 0.95 across spawns |
| ShadowDepthBias | (Res · MaskedShadowsDepthBias / ShadowFilterRadius)² = (1024 · 0.2 / 6)² = 1165.08, parabolic; no slope-scaled / receiver bias; DepthBias (0) unused | 0x82CFB598 | **CONFIRMED ORIGINAL** (CPU) | **APPLIED** in the decoded projection bias term |
| Shipped shadow constants | MaxShadowResolution 1024 (Res clamp [1, 2048]), MaskedShadowsDepthBias 0.2, ShadowFilterRadius 6, DepthBias 0, ShadowFilterQuality 0, bEnableBranchingPCFShadows True | Xe-TransEngine.ini, 0x830101E0 | **CONFIRMED ORIGINAL** | **APPLIED**; shadow buffer Res x Res, per-shadow viewport |
| BranchingPCF | 4 edge taps rotated by RandomAngleTexture, then 12 refining taps when lit is fractional; offsets = native tables x 6 / Res | 0x83711DFC, 0x83711EC0, 0x82CF1A00 | **CONFIRMED ORIGINAL** | **APPLIED** (exact tables) |
| Projection gates | light render flags bit 0x4 and the DPG bit (bit 5 + DPG) | 0x8304EF70 | **CONFIRMED ORIGINAL** (which lights set the bits UNKNOWN) | **APPLIED**; all lights carry both bits; WFC_LIGHTRENDERFLAGS test override |
| Composite light eligibility | baked lights can be the composite light; robot and vehicle identical path | 13c0953 §4 | **CONFIRMED ORIGINAL** | baked PointLight_4177_LC selected at spawn 18 (robot, vehicle, transform, walk) |
| shadowFactor -> projection strength | ShadowModulateColor = lerp(1, ModShadowColor, FadeAlpha) confirmed; the link from the composite record's shadowFactor to that light / FadeAlpha not proven | 0x82D077F8, §1.6 | **PARTIAL / UNKNOWN** | not wired; opt-in path uses the light's ModShadowColor, FadeAlpha 1 (WFC_SHADOWFADEALPHA) |
| Projection shader bias / offset use | in-shader use of ShadowDepthBias / offsets (decoded here earlier from engine microcode; 13c0953 leaves it PARTIAL) | — | **PARTIAL** | decoded microcode form |
| ScreenToShadowMatrix | VMX construction not decoded | 0x82CFB598 | **UNKNOWN** | inverse view-projection + subject-fit matrix [PROV] |
| Projected-shadow creation gates | distance / resolution / cast-flag cuts not traced | — | **UNKNOWN** | none added |
| Downsampled depth for the mask stencil | which depth the mask-sized stencil test uses | 0x83010218 binds SceneDepth | **PARTIAL** | point-sampled scene depth |
| Normal-play status | — | — | — | character projection stays opt-in (WFC_CHARSHADOWS=1) until shadowFactor, the shadow matrix and creation gates are recovered; the default mask is the native "nothing drawn" (1,1,1,1) |
| Ion Blaster fine-aim HUD | no scope / ADS overlay; mc_crosshairIonBlaster in both states; prongs move by spread x 300 stage px, 0.2 s easeout, first value instant; controller at (0.2, 0.2) px; FineAimSpreadModifier 0.5 | AssetTools 7a69756 fineaim_hud.json | CONFIRMED AUTHORED DATA (spread combination HIGH) | first value instant, +0.2 px anchor, HUD spread uses the fine-aim modifier |
| Pickup FX materials | LightVolume_WepPickup_MAT, Glow_Mod_Depth_MAT, ParticleBase_BW_MAT, Basic_Particle_Add_MAT, EnergonCube_Circuits_MATINST, Overshield_MATINST, WEP_Crates_MAT (cooked in TransGame) | streets_pickup_fx.json | CONFIRMED AUTHORED DATA | compiled from TransGame.xxx (fallback package); all match their shipped permutations; emitters are Systems-owned |
| Wall-panel destructible | TnStaticDestructibleActor_14465 at (896, 89968, −352), ~139k UU from the play space; WallPanelSign_MATINST / _EMISSOFF_MATINST states | streets_destructibles.json | CONFIRMED AUTHORED DATA | state materials compiled; actor not moved / not drawn (outside the playable area) |
| TR_AllShader_p.Textures.bubbles | not cooked in the Streets packages; TransGame copy: Texture2D DXT1, SRGB False | TransGame.xxx | CONFIRMED | ENV_EngergonGlass_MAT_INST now uses the real flags (was defaulted SRGB True) |

## MILESTONE 03 PASS 3 — NATIVE LIGHT VISIBILITY, CHARACTER LIGHTING, SHADOW STRENGTH, COLOUR (2026-10-02)
Native provenance: RE-Workspace `notes/MILESTONE03_RENDERING_LIGHTVIS_SHADOW.md`, ReverseEngineering commits
**b52dca9** (LightsVisibilitiesVolume + DirectLightEnv), **c95dadd** (gather, spot intensity, update queue,
transitions), **31f9a9b** (DynamicShadowLuminanceScale shader consumption). Marks: CONFIRMED ORIGINAL / HIGH /
PARTIAL / UNKNOWN.

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| LightsVisibilitiesVolume layout | u8 bHasOctree; recursive node {8 x i32 corner (low 16 bits), i32 hasData, u8 hasChildren, 8 children}; centre, root half, u32 +0x18, finest half; GUID table; u16 corner remap; per-sample count bytes; u32 pair pool | 0x82DF5748 / 0x82DF5590 | CONFIRMED | **APPLIED** (render/LightVisibilityVolume.*) |
| Streets blob | 18153 nodes, centre (19072, −43008, −72448), root half 26729.2, finest half 512, 268 GUIDs (256 bound to level lights by LightGuid), 17487 corners, 3096 samples, 7679 pairs, indices strictly ascending, bit 7 never set | lvv_decode.py / runtime self-check | CONFIRMED (data) | validated at load |
| Count-byte halving | archive flag halves count bytes on load; Streets pool only matches with halved counts | blob arithmetic | CONFIRMED for Streets / PARTIAL flag identity | applied when the halved counts match the pool |
| Octree +0x18 | 0x45800000 (4096.0 as a float); not read by the query | blob | UNKNOWN meaning | ignored (as the query) |
| Count bit 7 | honoured as "invalid" on the face path only | 0x82DDDA08 | PARTIAL meaning | reproduced literally |
| Query | strict root test; child (x>c)<<2|(y>c)<<1|(z>c); corner bit0 X; remap; plain trilinear (0-pair corners renormalized away) or T-junction face blend (6 faces x 4 corners, weight 1/3, finer neighbours); 16-bit fixed-point merge with truncation; renormalize 1/(1−emptyW); outside / empty leaf -> no data | 0x82DDE200 / 0x82DDDA08 / 0x82DDD798 / 0x82E03238 | CONFIRMED | **APPLIED**; C++ == independent Python port at 400 points (163 outside, 130 empty leaves, 77 trilinear, 30 face-blend) |
| Baked vs unbaked | baked lights (bound in the table): volume visibility at the bounds origin, no rays, 0 outside / empty; unbaked: N = clamp(round(15·√lum(colour·Imax) + 0.5), 1, samples) sample rays, 0.03 / 0.01 thresholds, stagger mod 5, ray gate CastShadows && CastStaticShadows, ray ends 15 UU short | 0x82CE1918 / 0x82CC2700 | CONFIRMED | **APPLIED** (no rays for baked lights) |
| Gather | LightAffectsEnv: enabled; light function only with composite shadow; CompositeDynamic -> Dynamic; shared channel; special channels ⊆ env; point/spot RadiusOfInfluence + bounds radius; spot sphere-vs-cone (clamped outer) | 0x82DF5068 / 0x82CC2608 / 0x82DD3EF0 / 0x82E2CDF8 | CONFIRMED (cone test arithmetic HIGH) | **APPLIED**; env channels robot Dynamic+PlayerOnly, vehicle Dynamic (HIGH) |
| IntensityAt | point B·max(0, 1 − (d/R)²)^F; spot point · cone², cones clamped (inner [0, 89]°, outer [inner + 0.001, 1.5543429] rad); directional B | 0x82DC1F60 / 0x82E2CFE0 / 0x82DBEAC0 | CONFIRMED | **APPLIED** |
| Ranking | direct: lum(LightColor/255 · I) · vis (0.3/0.59/0.11), cap TotalLightCount 2, boundary crossfade into ambient; composite: lum without visibility, cap 1, 0.2 runner-up fade, × smoothed visibility, factor 1 − fade, drop within 0.05 | 0x82CDDE70 / 0x82CCB110 / 0x82CD4540 | CONFIRMED | **APPLIED** |
| Updates | mode 1 when the bounds origin moves > DetailScale[DetailMode 2]=3 × 30 UU (×100 squared after 0.1 s unrendered) or settles; mode 2 incremental every tick; full updates through the global queue (deadline frame + 2, FIFO 2 ms budget); first update after attach immediate | 0x82DD17C8 / 0x82CD2538 / 0x82CDD438 / 0x82DBEF90 | CONFIRMED (budget availability PARTIAL) | **APPLIED** |
| Transitions | visibility moves linearly at clamp(|v|·0.002, 0.2, 1)/0.5 per second inside updates only; no between-update interpolation for WFC pawns | 0x82CDDE70 / 0x82CD3840 | CONFIRMED | **APPLIED** |
| DirectLightEnv self-test | 20 checks (RoI boundary, channels, spot cones, queue deadline, first update, transition) | WFC_DLETEST | — | 20/20 PASS |
| DynamicShadowLuminanceScale shader use | mask = ShadowMaskTexture.x; S = (1 − mask)(1 − DSLS); character uber pass: ambient·(1 − DLAC·S) + direct·(1 − S), one mask for the summed lights; per-light pass light·material·(1 − S); before SceneColorBiasFactor, linear, no clamp | Xenos microcode (31f9a9b) | **CONFIRMED ORIGINAL** | **APPLIED** in the uber pass |
| DSLS shipped value | 0 (BSS; no config sets it) -> full native dynamic-shadow strength | 0x8382DDF4 | CONFIRMED | 0 (WFC_DSLS test override, clamped [0, 1]) |
| DirectLightAmbientContribution CPU calculation | SH ratio in proxy build 0x82CCE0A8 (HIGH), formula not recovered | 31f9a9b | **PARTIAL / UNKNOWN** | 0 (inert while the mask is 1); WFC_DLAC test override |
| Shadow-mask write generation | how the composite shadowFactor reaches ShadowMaskTexture | — | **PARTIAL / UNKNOWN** | neutral mask 1.0 (= the unshadowed original); WFC_SHADOWMASKTEST test hook |
| DSLS validation | DSLS 1 bit-identical to the unmasked render; world pixels untouched; DSLS 0 < 0.5 < 1 on characters; default render bit-identical to the previous checkpoint | lockstep matrix (robot/vehicle bright+dark, transform, walk) | — | PASS |
| Projected shadow stages | caster depth VS, branching-PCF projection PS, RandomAngles texture, frustum-bounded projection | engine/BASE microcode | CONFIRMED | implemented; opt-in WFC_CHARSHADOWS (native DepthBias / ShadowDepthBias / PCF offset tables UNKNOWN) |
| Texture gamma | UE3 SRGB textures use Xenos gamma formats: PWL degamma (64/96/192 segments, trunc correction, /1023) | xenia (Source X360GammaToLinear + D3D9 disassembly) | HIGH | **APPLIED** (RGBA16 linear upload; WFC_SRGBCURVE A/B) |
| Lightmap textures | LightMapTexture2D inherits Default__Texture SRGB=True -> PWL | Engine defaults | CONFIRMED | PWL |
| Vertex lightmaps | exp2(log2(max(|c|, 1e-4))·2.2)·LightMapScale[k] per vertex | vertex-lightmap VS microcode | CONFIRMED | **APPLIED** |
| CPU colours | FColor -> FLinearColor through pow(i/255, 2.2) table (light colours, ModShadowColor) | PowOneOver255Table 0x82251110 | CONFIRMED | pow 2.2 |
| HUD textures | UI_GFxHud_p textures SRGB False, composited raw | cooked flags | CONFIRMED | raw |
| Weapon muzzle light | TnWeaponMesh.MuzzleFlashLight = IonBlaster PointLightComponent (Radius 3000, FalloffExponent 75, RadiusOfInfluence 504.7, Brightness 10, colour 255/143/140, bCastCompositeShadow, ModShadowColor (3, 0.05, 0.04)), all light channels | WEP_IonBlaster_p archetype | CONFIRMED data | not driven (Systems/Gameplay owner) |
| DeadBodies_Mat_INST | static switch array with a numbered FName and a stale entry | MIC native tail | CONFIRMED | decoded; permutation now matches (189/203) |

## MILESTONE 03 PASS 2 — RENDERING (2026-10-02, branch agents/rendering)
Marks: **CONFIRMED ORIGINAL** (cooked data / Xenon microcode / shipped ini) · **HIGH** (standard UE3 semantics on
confirmed data) · **PROV** · **UNKNOWN**. Deterministic captures: `WFC_LOCKSTEP=1`; audit set
`bash tools/render/capture_audit.sh` (frame reports in docs/rendering/audit/).

| Item | Original (WFC) | Source | Mark | Rebuild |
|---|---|---|---|---|
| Distortion accumulate | s = 4·Distortion.xy; kill if dot(s,s) − 0.1 < 0; clamp ±255; ×1/255; RG = max(s,0), BA = abs(min(s,0)) | Xenon PS of Ring_Distort_Add_MAT (BASE shader cache; literals 1.25 / −0.15 / 0.1 identify it) | CONFIRMED | **APPLIED** per-material variant, additive, scene-depth tested |
| Distortion apply | uv + (acc.rg − acc.ba)·(0.25, −0.25) (D3D v-down), SceneColor fetch | engine PS (AccumulatedDistortionTexture / SceneColorTexture) | CONFIRMED | **APPLIED** before post |
| Distortion RT | 8-bit UNORM implied by the ±255 / 255 encode | microcode encode | HIGH | RGBA8 |
| Distortion order | after translucency, before post | UE3 frame order | HIGH | applied in endFrame |
| Hover / ram emitter materials | base_glow → Glow_Mod_MAT (modulate), rays_Dup → Trail_Distort_MAT (distortion), ram dust → Distortion_Cloud_01_MAT | ParticleModuleRequired.Material | CONFIRMED | all compiled with distortion / modulate paths; **emitters not spawned (Systems)** |
| Vehicle slots | slot 0 RB_OptimusPrime_Cust2_Mat_INST (9728 tris), slot 1 InteriorAlt_Energon_MAT_INST (1923) | cooked slot table (umodel) | CONFIRMED | matches; full audit docs/rendering/vehicle_material_audit.md |
| Vehicle normal map | VH_Optimus_NORM DXT5; graph reads .a (X) and .g (Y); unpack −1; UseReconstructedNormal = True | texture + graph + MIC switch | CONFIRMED | matches |
| Energon colour | compiled permutations of both Optimus MICs embed only the red EnergonColor default (1.25, 0.05, 0.05) | FMaterialResource | CONFIRMED | red |
| BSP polygon winding | cooked vertex order consistent with the surface normal (Newell: 0 of 889 polygons need flipping) | cooked FModelVertexBuffer + surface normals | CONFIRMED | **FIXED**: 44 polygons had been flipped by a degenerate first-triangle test and culled from above, showing the fog-coloured clear (flat pink / lavender floors) |
| Hidden actors | bHidden InterpActors (RepairNodeB ×2, Base_D, Base_A) | props_authored.json (effective values) | CONFIRMED | not drawn (WFC_SHOWHIDDEN to inspect) |
| No-light components | bAcceptsLights False / empty LightingChannels: 123 unlit FX meshes + AutobotSign ×2, CityBackdrop ×2, SpaceDome | props_authored.json; UE3 channel overlap | CONFIRMED data / HIGH semantics | zero light environment (emissive only) |
| Unlit props without baked lighting | 124 MLM_Unlit FX meshes (light planes, beams, glow spheres, stains) | materials | CONFIRMED | intentionally none |
| Dark spire wall (spawn 10) | baked lightmap at map percentile 42 (dark blue) | lightmap texels | CONFIRMED | authored appearance |
| Character light visibility | LightEnvironmentComponent NormalizedSampleOffsets: robot (0,0,.9) (0,0,−.7) (.7,.7,.7) (−.7,−.7,.7) (.7,−.7,−.7) (−.7,.7,−.7); vehicle (0,0,.7) (±.8,±.8,0); TotalLightCount 2; UpdateDistanceThreshold 30 UU | TransGame Default__TnRobotForm / TnVehicleForm | CONFIRMED data | **APPLIED**: visibility = fraction of samples at bounds origin + offset·extent with a clear path (HIGH semantics); form chosen by material package (_ROBO_p / WEP_ → robot, _VEH_p → vehicle) |
| Lighting channels | robot mesh adds PlayerOnly; Streets lights: 266 all-channel, 2 dynamic-only, none PlayerOnly | TransGame + level lights | CONFIRMED | no change needed |
| Character shadows | modulated projected shadows: SceneColor ×= lerp(lerp(1, ModShadowColor, atten), 1, lit²); 4-tap PCF, receiver depth clamp 0.999; atten = spot² · (1 − sat(abs(d/R)²)^ShadowFalloffExponent); ini ShadowFilterQuality 0, Min/MaxShadowResolution 128/1024, ShadowTexelsPerPixel 1, ModShadowFadeDistanceExponent 0.2, LightEnvironmentShadows True; ~35 lights bCastCompositeShadow | engine PS microcode + Xe-TransEngine.ini + light props | CONFIRMED (projection / filter) / UNKNOWN (shadow light selection) | **NOT IMPLEMENTED**: the WFC LightEnvironmentComponent shadow-light (composite) selection is in default.xex (ReVa request) |
| Light visibility volume | LightsVisibilitiesVolume_2224 (Location (19072, −43008, −72448), DrawScale3D 26729): 744772-byte native blob = octree head (714056 B: BE int32 child indices, 32-byte light bitmasks per node) + 7679 (u16 light index 0..267, u16 visibility in 1/20 steps) pairs, preceded by per-cell even byte counts | cooked native data | CONFIRMED fragments / UNKNOWN cell → pair mapping | **NOT USED**: node record layout + light index order need default.xex (ReVa request); runtime traces kept |
| First-shot hitch | no renderer resource is created during combat on this branch (first-use log empty; render span 6.2 ms on shot frames); the M02 hitch (programFor → texture → decodeImage) is now prewarmed at load | WFC_RENDERSTATS first-use / spike log | CONFIRMED (measurement) | prewarm of all unused compiled materials after frame 1 |
| Build configuration | M03 perf numbers were Debug builds; Release: skinned vertex rebuild 3.13 → 0.24 ms, idle submit 5.75 → 1.93 ms | measurement | CONFIRMED | measure with Release (build/release) |

## MILESTONE 03 — RENDERING FIDELITY (2026-10-02, branch agents/rendering)
Verification tooling: `tools/render/verify_permutations.py` diffs every translated graph's parameter reads against
the parameter list of the material's COMPILED FMaterialResource (uniform expressions in the cooked native tail):
30 → **187 / 202 materials match** (remaining: 10 unknown = unnamed "None" parameters or textures the original
compiler eliminated; 1 differs = DeadBodies_Mat_INST, static switches not decoded). `tools/render/audit_map.py`
writes `work/render/<Map>/map_audit.json` (EXPECTED from cooked levels + AssetTools exports vs ACTIVE from the
renderer's upload dump `WFC_AUDIT_DUMP`).

| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| CameraVector / ReflectionVector space | `CoordinateSpace = CS_World` on 11/9 nodes (vehicle, robot, world reflections) | expression props | CONF | **FIXED**: world-space vectors (were tangent) |
| Cube reflection LOD | WFC `TextureSampleParameterCube.LODBias` input (Reflection_LOD_Scale) | expression props | CONF | **FIXED**: texture(…, bias) |
| Fresnel exponent input | WFC `Fresnel.Exp` input overrides Exponent | expression props | CONF | **FIXED** |
| Static switch decode | MIC native tail arrays validated against real StaticSwitchParameter names | cooked MICs | CONF | **FIXED** (bogus `_DialogEventManager` records rejected) |
| Lazy function inputs | unused material-function inputs emit no code (switch-dependent reads) | compiled permutations | CONF | **FIXED** |
| DepthBiasedAlpha | Alpha·saturate((SceneDepth−PixelDepth)/max((1−Bias)·BiasScale,.001)); WFC BiasScaleInput | UE3 semantics + props | MED | **APPLIED**: scene-depth copy (blit when opaque drawn), view-Z depths |
| PixelDepth | view-space Z (UE units) | UE3 semantics | MED | **FIXED** (was eye distance) |
| ScreenPosition (bScreenAlign false, all 16 nodes) | clip-space position; UE3 infinite-far: w = view Z, z = view Z − near | UE3 semantics | MED | **FIXED** (was fragcoord, z = 0 ⇒ Boostermaterial_02 near-fade made the flame black; light-volume materials affected too) |
| Blend-mode fog | additive: c·fog.a; modulate: lerp(1,c,fog.a); others c·fog.a+inscatter | UE3 base pass | MED | **APPLIED** (additive surfaces no longer gain fog colour) |
| Vehicle / weapon FX materials | emitter materials (ParticleModuleRequired.Material) of bumble_boost_small1_FX, CarHover_A_01_FX, Truck_ram_FX, weapon FX: 27 graphs | cooked ParticleSystems | CONF | **COMPILED** (`tools/render/fx_materials.txt`); mesh emitters + sprites shaded by them (`ParticleBatch.material`, drawMeshFx) |
| MeshEmitterVertexColor | compiled as VectorParameter "MeshEmitterVertexColor" = particle colour | compiled permutations | CONF | particle colour → vertex colour |
| Ram_model_MAT rim | WFC ShaderCode `saturate(pow(saturate(abs(dot(CameraVector(CS_World), Normal))*1.5),2))` | expression props | CONF graph / PROV semantics | reproduced literally (Normal node = tangent normal, as for all other users); brightness depends on view elevation — confirm with microcode |
| Actor-placed props | StaticMeshActor / StaticInterpActor nodes carry only `actor` in world.glb | world.glb, lighting records | CONF | **FIXED**: actor → its single StaticMeshComponent ⇒ 38 submeshes now use their baked lightmaps |
| Vertex lightmaps (LMT_1D) | 24 components (Arch_4096, Core_RoutingWallA, PowerTubeCircle, CoolantSurfaceHole, declogo): bulk FQuantizedDirectionalLightSample (3 × FColor A,R,G,B; per-channel normalized to 255) + ScaleVectors[3] | cooked StaticMeshComponent native tail | CONF layout / PROV gamma | **APPLIED**: decoded pow(b/255, 2.2)·scale through the 3-coefficient directional formula (vertex count matches LOD0 for all 24) |
| BSP elements without lightmaps | 360/540 elements: LightMapType 0, no IrrelevantLights / ShadowMaps / light GUIDs | cooked FModelComponent | CONF data / PROV runtime | dynamic light environment (assumed UE3 uncached interactions) — audit status `unknown` |
| HUD crosshair (Ion Blaster) | Hud_GFX.gfx `mc_crosshairIonBlaster`: 3 × bitmap 400 (32×16, fill-stretched to 30×12) at 0/120/240°, anchor stage (560,360) of 1120×720; prong `_y` eases (0.2 s) to −300·WeaponSpread; tint by target type 0x50B5D5 / 0xFF3333 / white | GFx tags + AVM1 (sprite 404, 621) | CONF | **APPLIED** (`IRenderer::setReticle`); scale mode + easeout curve PROV |
| Fine aim presentation | `NotifyFineAimChanged`: scope only for HeavyPistol/BurstRifle/SniperRifle; Ion Blaster → `hideScope` (crosshair unchanged; prongs follow WeaponSpread) | AVM1 sprite 621 | CONF | matches: no scope for the Ion Blaster |
| Transformed-vehicle / robot materials (mottled grey-pink vehicle, wrong energon after transform) | each form's own MICs | renderer bug | CONF | **FIXED**: dynamic-mesh program cache was keyed by Material* address; the character pose buffer reuses storage across forms, so the vehicle got the robot's programs (robot textures on vehicle UVs). Now keyed by material content |
| FX GL1 stand-ins | Systems' per-mesh intensity (LightCylinder 0.1 = DustPower) double-applied once the real graph runs | — | CONF | `IRenderer::evaluatesFxMaterials()`; VehicleFx drops the stand-in when true (hover light cones visible again) |
| Missing TexCoord channels | UE3 FLocalVertexFactory binds the last available texcoord for missing channels; Light_Cylinder_STAT is cooked with NumTexCoords = 1 | cooked vertex buffer | CONF | **FIXED**: loader duplicates UV0 into UV1; material TexCoord[1] now reads the raw channel (was the lightmap-transformed UV). Hover light-cone plumes (LightVolume graph samples TexCoord1) now render as depth-faded downward plumes |
| Energon colour (Optimus) | UseAutobotEnergonColor = True → A branch EnergonColor (1.25,0.05,0.05) red; blue (0.2,0.1,1.5) is the False branch | MIC static switches + compiled permutations of both Optimus MICs embed the red default only | CONF | red is correct with the all-zero runtime colour set; no Gameplay call site exists (setCharacterColors never called) |
| Debug box in transform stills | World debug overlay (capsule wireBox + aim ray) toggled by an interactive 'B' press during a scripted run | Application input | CONF | interactive toggle ignored in WFC_SMOKE_FRAMES runs (WFC_DEBUGDRAW opt-in) |
| Mesh-particle light environment | UE3 mesh emitters share the particle system's light environment | UE3 design | MED | small dynamic meshes (< 0.5 m) share a per-1 m-cell env, refreshed every 60 frames |
| Light-env visibility traces | per-light traces from the object (static lights/world) | UE3 light env | CONF | memoized per (light, 0.25 m cell); unlit (FX) programs skip the env entirely |

---

## PASS 9 — CHARACTER CUSTOMIZATION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Applier parameters | Cust_Color_A, Cust_COLOR_B, EnergonColor on robot, vehicle, arm, weapon | Default__TnCharacterApplier; native RE (user-supplied) | CONF | runtime uniforms in CHR + WEP MICs |
| Zero semantics | all-zero character colour skips the override; authored value stands | native RE | CONF | **APPLIED** (RGB all zero = skip) |
| Colour set | faction selects the set; no separate team override in this path | native RE | CONF | caller passes the faction's set |
| Optimus data | PrimaryColors/SecondaryColors (0,0,0,255) per faction, EnergonColor (0,0,0,1) | OptimusPrime_PCD_SP / Leader_PCD_MP | CONF | all skipped → MIC values (Cust_A/B per MIC, EnergonColor (1.25,0.05,0.05) compiled Autobot branch) |
| Parameter reads | robot+vehicle MICs read all three; WEP_IonBlaster_MATINST reads Cust_Color_A + EnergonColor; InteriorAlt reads none | compiled MIC uniform expressions | CONF | matches |
| Arm mesh | CP_OptimusArm_SKEL used when no weapon is drawn (early vehicle→robot transform, holster, melee); slots Cust_Mat_INST_B + InteriorAlt | native RE; materials_authored.json | CONF | renders with original MICs (gameplay drives visibility) |
| MP robot mesh | RB_OptimusWeaponArm_SKEL (forearm removed; slot order differs from the slice's campaign RB_Optimus_A_SKELMESH) | materials_authored.json | CONF | renderer supports it; mesh choice is gameplay/asset ownership |

## PASS 8 — VEHICLE MATERIAL + MAP COMPOSITION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Vehicle normal map | `RB_OptimusPrime_Cust2_Mat_INST` `UseReconstructedNormal`=True: X=A, Y=G of DXT5 `VH_Optimus_NORM`, both `*2-1`, Z=sqrt(1-x²-y²) | MIC static params; BASE ShaderCache character PS (`tfetch .yw; mad r.xy, r.zy, 2.0, -1.0`) | CONF | **FIXED** (alpha was left in [0,1]) |
| Vehicle diffuse/customization | CHR master with Cust_Color_A/B, ColorBrightness 0.6, etc. | MIC params | CONF | verified = AssetTools bake |
| EnergonColor (robot+vehicle) | 3 same-named VectorParameters; compiled permutation binds (1.25,0.05,0.05) | MIC compiled uniform expressions | CONF | as compiled (team override at runtime not modelled) |
| Persistent level | `MP_IAC_Streets_Base_m` streams ART + AUDIO only | TransLevels.ini MapFilename; LevelStreamingKismet | CONF | composition complete |
| Post-process | TnWorldInfo: Bloom_Scale 0.1, bEnableDOF, DOF_MaxFarBlurAmount 0.6, FocusFarFalloff 40000, ColorCorrectionTexture MP_Streets_CLUT | BASE TnWorldInfo over Default__WorldInfo | CONF | **APPLIED** (replaces PASS 7 defaults) |
| CLUT | 32³ A8R8G8B8 Texture3D, SRGB=False (Default__Texture3D), applied after gamma: tex3D(c*(N-1)/N+0.5/N) | Xbox 3D tiling (xenia Tiled3D); uber+CLUT PS | CONF | **APPLIED**; strong authored desaturation |
| DOF | gather a' from avg depth; resolve ((1-a)scene+blur.rgb)/((1-a)+blur.a); PackedParameters (Focus, 1/NearFalloff, Exponent, 1/FarFalloff) | decoded PS | CONF shader / MED parameter packing | **APPLIED** |
| Static decals | 25 DecalComponents with cooked receiver geometry (28-byte verts, u16 indices); BSP receivers clipped, static meshes keep whole triangles; UV = 0.5 - M·(P-L) + offset, rows HitTangent·TileX/Width, HitBinormal·TileY/Height | DecalComponent native; decal VS | CONF geometry+UV / MED box clip of static-mesh receivers | **APPLIED** |
| Section materials | umodel glTF section names → original MIC paths (15); 6 null (`dummy_material_N`) | StaticMesh sections | HI | **APPLIED** |
| Prefabs | 16 PrefabInstances → 34 placed StaticMeshActors (already composed) + 1 SteamVent emitter | ArchetypeToInstanceMap | CONF | complete |
| Emitters | 8 `FX_Level_Generic_p.FX.Steam_Sm_FX` + prefab steam vent | ART actors | CONF | **NOT RENDERED** (particle FX) |
| Power-tube "flat planes" | PowerTubeCircle emissive panels (mask R, lavender ×(1+pulse)) | MIC/mask | CONF | authored; renders as designed |

## PASS 7 — ORIGINAL RENDER PATH (2026-10-01, branch agents/rendering)
Specification sources: the ORIGINAL compiled Xenon shader microcode in the cooked `ShaderCache`
exports (disassembled with `tools/render/xenos_dis.py`, bit layouts per xenia's public `ucode.h`),
the cooked material expression graphs, and cooked lighting objects. No values tuned by eye.

| Stage | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Base-pass lighting policy | `FDirectionalTextureLightMapPolicy` (also Vertex/Interpolated variants) | ShaderCache type names, ART pkg | CONF | **APPLIED** |
| Directional lightmap decode | `L = Σ_i dot(N_t,B_i)² · tex_i.rgb · ScaleVector_i`; B0=(0,√(2/3),1/√3) B1=(−1/√2,−1/√6,1/√3) B2=(1/√2,−1/√6,1/√3); no-normal materials use ×1/3 (=(1/√3)²) | base-pass PS microcode + literal pools | CONF | **APPLIED** (3 coefficients) |
| Lightmap colour space | `LightMapTexture2D` inherits `SRGB=True` (Default__Texture); no instance override | Engine.xxx CDOs + texture tags | CONF | **APPLIED** (GL sRGB) |
| Lightmap resolution | atlases cooked at 256² (Xenon `MaxLODSize=512`, top mips absent in dump) | `_LM` pkg sizes, Xe-TransEngine.ini | CONF | as cooked |
| Base-pass output | `Emissive + Diffuse·L·ShadowMask`, ×SceneColorBiasFactor; **no lightmap specular** | base-pass PS | CONF | **APPLIED** (ShadowMask=1) |
| What is baked | Beast bake (`FBeastAreaLightPolicy`); dominant red directional (Brightness 6) LightmapGuid present in 1932 components; no ShadowMap2D textures | component native LightGuids | CONF | data used as-is |
| BSP lightmaps | per ModelComponent element `FLightMap2D`; per-vertex ShadowTexCoord in cooked `FModelVertexBuffer` (36 B verts, indexed by `FBspNode.iVertexIndex`, 889/889 nodes verified) | Model + ModelComponent native layout (decoded) | CONF | **APPLIED** (`bsp.glb`) |
| Materials | UE3 graphs + WFC MaterialFunctions (ENV_*_MF, CHR_*), static switches & TextureSets (Color_NormX/Masks_NormY, alpha UnpackMin −1) per MIC; WFC HLSL ShaderCode | cooked Material/MIC/TextureSet exports (Streets BASE+ART) | CONF | **APPLIED** 168/169 (offline GLSL translation) |
| Old extraction error | `materials.json` mapped decal `Rust_C_CLR` as base colour; real diffuse lived in TextureSets | MIC TextureSetParameterValues | CONF | **FIXED** |
| Cubemaps / flipbooks | cooked Xbox-tiled DXT TextureCube (6 faces) / TextureFlipBook | native tails (decoded) | CONF | **APPLIED** (`xbox_texture.py`) |
| Dynamic-object lighting | WFC UberLight: `Diff·AmbientCube(N)` + per light `(Diff·sat(N·L·0.6778+0.3333)² + Spec·pow(sat(R·L),SpecPower))·Color·sat(1−(d/R)²)^Falloff·spot²·S` | uber-light PS microcode | CONF | **APPLIED** |
| Light environment | WFC `LightEnvironmentComponent`; TnRobotForm/TnVehicleForm TotalLightCount **2**, UpdateDistanceThreshold **30 UU**; dynamic-only SkyLight (Brightness 0.1, LowerBrightness 0) + dynamic fill directional | TransGame.xxx CDOs, ART lights | CONF | **APPLIED**; composition of non-direct lights into the ambient cube = MED |
| Light visibility | `LightsVisibilitiesVolume` (745 KB precomputed per-cell light visibility, leaf records `(lightIdx u16, vis u16)`) | ART native | PARTIAL | **NOT USED**; collision raycasts instead (PROV) |
| Height fog | UE3 4-layer vertex fog: `exp2(FogDistanceScale·max(d−Start,0)·layerFrac)`, cut at ExtinctionDistance; authored Height −57273.8, Density 2e-5, Start 2048, LightColor (234,91,116) × **LightBrightness 0.1** (class default) | fog VS microcode + HeightFogComponent | CONF shader / MED constants (`−Density/ln2`) | **APPLIED** (replaces softened GL_EXP pink fog) |
| Post (superseded by PASS 8: map TnWorldInfo overrides) | bloom gather (4 taps clamp [0,4], tap kept if any channel > Threshold, ×0.25·Scale) + blur; UberPostProcess tone; no PPVolumes, persistent level stub ⇒ WorldInfo defaults (Bloom 1/1, Shadows 0, HighLights 1, MidTones 1, Desat 0, identity CLUT); DisplayGamma 2.2 | DOFAndBloomGather / UberPostProcess PS microcode, Default__WorldInfo, Xe-TransEngine.ini | CONF (blur kernel PROV) | **APPLIED** |
| Character materials | `CHR_Transformer_NormSpec_Cust_E_Mat` with MIC customization, spec-power S-curve, energon pulse, metal cubemap | Streets BASE | CONF | **APPLIED** (replaces provisional GL_LIGHT0) |

**Remaining gaps:** LightsVisibilitiesVolume decode; 24 `FLightMap1D` vertex-lightmapped props;
dynamic shadow mask (characters do not cast/receive dynamic shadows); DirectLightAmbientContribution;
`ENV_ORB_D1_Debris_p SideSupport_MATINST` (master not in Streets packages); `MotionBlurEffect` in
DefaultScenePostProcess not implemented; light-env transition blending (0.5 s) not implemented.

---

## PASS 24 — human playtest fidelity II (2026-10-05)

### Fast-turn stutter: body orientation snapping between 60 Hz steps [measured; HIGH CONFIDENCE cause]
- Playtest: fast left/right camera or steering motion looks stuttery, mostly in vehicle form, also on foot.
- Measured (WFC_HEADJIT: drawn body heading vs camera yaw per render frame, 6 rad/s flicks / full-lock boost steer):
  - 60 Hz render: ≤ 0.05°/frame.
  - 144 Hz: 4.5–4.7°/frame (max 7°).
  - 240 Hz: 2.8–4.6° (max 9°).
  The camera turned every render frame while the body yaw advanced only on simulation steps. The original ticks physics and camera
  in the same variable-length frame, so the body never lags its view.
- Not renderer pacing, not camera position (CAMSYNC unchanged), not input quantisation.
- Fix (presentation only; the simulation is untouched): a draw yaw added to the body mesh and its attachments between steps.
  - View-slaved headings (robot, car / truck hover, tank) add the view yaw change since the last step.
  - Physics-steered headings (boost Driving, jet servo TurnRate 0.5) extrapolate the last step's yaw rate.
  - The boost camera follows the drawn heading.
- After (mean °/frame at 144 / 240 Hz):

  | scenario | 144 Hz | 240 Hz |
  |---|---|---|
  | robot | 0.000 | 0.000 |
  | car hover | 0.010 | 0.004 |
  | truck hover | 0.012 | 0.012 |
  | boost | 0.35 | 0.07 |
  | jet | 0.70 | 0.19 |

  The jet residual also exists at 60 Hz (0.37°): the authentic TurnRate 0.5 servo lagging at flick reversals.

### Transform mesh handoff per chassis [CONFIRMED ORIGINAL: TnAnimNotify_ToggleHidden in each chassis' transform clips]
- Playtest: a short malformed / box-like stage in a Decepticon Scout robot→vehicle transform.
- Cause: every chassis used the Optimus ToggleHidden times (robot hide 0.880, vehicle show 0.396, robot show 0.098, vehicle
  hide 0.663). Barricade (Car4) authors 0.849 / 0.705 / 0.394 / 0.666, so its vehicle mesh appeared 0.31 s early, half-unfolded.
  Sideswipe (0.414 / 0.351 / 0.000 / 0.279), Starscream (0.789 / 0.694 / 0.336 / 0.411) and the tanks differ too.
- Now read per chassis from character.json (Option absent = Toggle_Unhide, Toggle_Hide explicit).
- WFC_XFORMVIS 16/16 (Car2, Car4, Truck, Truck4, Jet, Jet4, Tank3, Tank2; both directions): the target mesh appears and the
  source hides at the authored time (within a step), and no step draws neither mesh. Clip, pose, root, momentum and weapon
  restore (25%) are unchanged.
- The Scout transform has no authored particles (AssetTools: only Starscream authors Trails FX): a mesh swap plus sound, as
  now drawn.

### Tank 180 quick turn [CONFIRMED ORIGINAL: RE TARGETED_PASS5 §1]
- Playtest: the tank special move can cause a rapid 360° manoeuvre.
- Original: VehicleSpecialMove (Shift / RB) on press → CanUseSpecialMove (TimeBetween180s 1.2 s) → RecenterCamera.
  TnQuickTurnCameraBehavior lerps the camera yaw linearly to tank yaw + 180° over 0.3 s; the hull follows the camera through
  TnHoverTankSimulation.UpdateTurn, so it spins 180° in 0.3 s. Not 360°, no repeat while held.
- The rebuild had a PROVISIONAL instant +180° jump of the view yaw. An exact ±π step is ambiguous to the yaw smoothing and the
  hull's remainder() follow, a plausible source of the long-way spin. It is replaced by the original 0.3 s linear camera behaviour
  (+ a quickTurnSerial for the Systems "Tank 180" sound).
- The Soldier preset's Shift ability in ROBOT form is Whirlwind: a 5.9 s spinning melee attack, authentic, which may also be what
  was seen.
- VEHPHYS: 180° reached in 0.28 s; holding Shift 3 s = one turn; a press within 1.2 s is refused.

### Fine aim per weapon [CONFIRMED ORIGINAL: RE pass 5 §3, AssetTools weapon.json fine_aim_camera (OverTheShoulder FOVsByPCS)]
- PC right mouse = ToggleFineAim (toggle); pad LT hold. Ground speed ×0.5; blocked while meleeing / reloading / dodging (as before).
- Camera rows by the held weapon's WeaponPCS (previously every weapon used the generic row):

  | weapon | FOV | orbit | screen X | look yaw / pitch |
  |---|---|---|---|---|
  | Null Ray (SniperRifle) | 20 | 100 | 350 | 6.5 / 3.25 (×0.13) |
  | HeavyPistol / BurstRifle | 30 | 100 | 350 | 9.375 / 4.6875 (×0.1875) |
  | other | 45 | 800 | −50 | 25 / 12.5 (×0.5) |

- One zoom stage only ("10x" is marketing text). Look speed blends over SpeedTransitionTime 0.5 s.
- Magma Frag Launcher: fine aim remote-detonates instead of aiming. Here fine aim is refused; the launcher's grenades explode on
  contact, so there is nothing to detonate [PARTIAL].
- PARTIAL: scope sway wiggle (0.45° at 3 / 10 / 6 Hz), fine-aim orbit-distance smoothing (0.1 s assumed), HUD scope symbol
  (Frontend: showScope long / short / medium).
- WFC_FINEAIMTEST 3/3: Null Ray FOV 20 / look 0.130, HeavyPistol 30 / 0.187, IonBlaster 45 / 0.500, speed ×0.50, toggle off → 80.

### Energon Repair Ray [CONFIRMED ORIGINAL: TnWeaponRepair / TnWeaponBeam script, RepairBeam_WEPDATA, AssetTools 8297bdd]
- Before: the Repair Ray was not simulated (no beam, no heal). Now it is a beam that ticks every FireInterval (0.1 s) along the aim
  over WeaponRange 3500 UU:
  - TnWeaponRepair.ProcessBeamHit: a teammate is healed HealthPerSecond 60 × RepairRateModifier (no buffs: ×1) × Δt, TnHealTypeRepairTeam;
  - otherwise TnWeaponBeam.ProcessBeamHit: the hit actor takes DamagePerSecond 60 × Δt, TnDamageTypeRepairEnemy;
  - ammo 10 / s (clip 100), HeatProperties.HeatMax 0 → no overheat in practice [HIGH].
- HUD state: repairBeam / repairBeamHealing / start / end / target per frame, for the Beam2 ribbon (Rendering) and the
  WP event 1 (heal loop) / event 2 (damage loop) sounds (Systems).
- PARTIAL: PlayerTargeting.GetRepairTarget lock-on (the beam end is pulled to a picked teammate's TargetableLocation) is not
  recovered; the beam follows the crosshair. Heal type segments: healed across segments [HIGH].
- WFC_PARTICIPANTTEST: teammate +60 HP/s from the beam (+ the pawn's own regen when idle), enemy −54 / s, 9 ammo / s, HUD flag.

### Match start countdown [CONFIRMED ORIGINAL: RE pass 5, TnMultiplayerGame]
- Already present: PendingMatch, GRI.ResetCountdown(true, 10) → MatchAutoStartCountdown 10 s, a CountdownTick event each second
  (PreGameCountdown <CurrentGame:CurrentCountdown>), no pawns until InProgress; then everyone spawns and the announcer plays.
- Not restored here: GameCountdownPostProcess (active 6 s, ramp-out 3 s) is a rendering / frontend presentation effect.

### Decepticon Scout (Barricade) transform look [authored data; no fallback frame]
- The per-chassis ToggleHidden times are the authored Car4 notifies: robot hides at 0.849 s, vehicle shows at 0.705 s (R→V);
  robot shows at 0.394 s, vehicle hides at 0.666 s (V→R). Both meshes are drawn in the overlap, as authored.
- The graybox fallback cannot appear mid-transform: the form swap re-samples and re-skins the new mesh in the same tick
  (Character::updateAnimation), and the partner mesh is skinned on the first tick that it is visible. XFORMVIS 16/16.
- So the "box-like" intermediate is the authored fold of Barricade's clips, not a missing pose [HIGH; visual confirmation pending].

### Jet handling values [CONFIRMED ORIGINAL authored blueprints]
- Hover: HoverPlane_Physics accel 2500, max 1500, gravity cancelled (TnHoverPlaneSimulation always hovers: the "floaty" feel),
  Ascend / Descend = Dash ±Z at 1000. Flying: Plane_Physics MaxSpeed 4000, accel 3000, drag 600, return to hover above crash
  speed 3000. All read from character.json; they match RE pass 5 §4. The remaining jet heading jitter (0.4° / frame at 60 Hz)
  is the TurnRate (0.1, 0.5, 0.5) servo itself.

### Scout height [re-confirmed]
- WFC_HEIGHTTEST Car2 / Car4: capsule 1.550 constant, root 0, scale 1, mesh origin 0; idle hips 1.51-1.54 m → jog 1.94-2.32 m.
  This is the authored AnimSet posing; nothing changes the capsule, root or scale. Authentic, unchanged.

### DEV / QA TOOLING [NOT ORIGINAL — never part of a fidelity claim]
- World::qa* API, all no-ops unless the process starts with WFC_QA=1. It is driven by Frontend's separate Win32 QA window (F10).
- qaWeaponIds(vehicle), qaSetLoadout(ids) through the real applyLoadout (restrictions apply, refused ids returned), qaRespawn
  (suicide, no score → the normal respawn wave), qaTeleportToStart(i), qaSetNoclip, qaSetGodMode, qaStatus.
- Map / mode / class / lobby: Frontend drives the real lobby flow.
- WFC_QATEST 7/7 with the gate; without it, every call is refused.

### Plasma Cannon charge [CONFIRMED ORIGINAL: script TransGame.TnChargeWeapon + PlasmaCannon_WEPDATA + Charge1-3 PROJDATA]
- Was: every press fired a Charge1 shot for 1 ammo.
- Now: TnChargeWeapon's states (0 idle, 1 charging, 2 / 3 / 4 = levels 1-3).
  - Press (loaded, TimeSinceLastCharge >= FireInterval 0.15 s) → charging. Level 1/2/3 at ChargeDelay1/2/3 = 0.75 / 2.0 / 3.5 s
    (state → delay mapping HIGH).
  - Release in state 1: no shot. Release at level n: fire mode n-1 → TnProjectilePlasmaCannonCharge<n>.
  - Levels:

    | level | speed | damage | radius | ShotCost (clamped at 0) | trail |
    |---|---|---|---|---|---|
    | 1 | 8000 UU/s | 115 | 1000 UU | 25 | Trail_PlasmaCannon_Sm |
    | 2 | 15000 UU/s | 140 | 2500 UU | 50 | Trail_PlasmaCannon_Med |
    | 3 | 23000 UU/s | 179 | 3500 UU | 100 | Trail_PlasmaCannon_Lrg |

  - Fully charged: ChargeDrainRate 10 clip ammo / s; an empty clip ends the charge (fires).
  - Weapon switch (TryPutDown), reload, melee or overheat ends the charge with no shot; so does a transform [HIGH].
  - After a shot an empty clip auto-reloads.
- HUD: weaponChargeState (0-4) and weaponChargeMessage ("CHARGING" in state 1, "READY" at any level: ChargingMessage /
  FullyChargedMessage).
- PARTIAL: the charge material glow (MaterialGlowAmount 0 / ⅓ / ⅔ / 1) and the charge muzzle events / sounds (WP events 9-12)
  are presentation for Rendering / Systems.
- WFC_CHARGETEST: a 0.3 s tap fires nothing; 1.0 / 2.5 / 4.0 s holds fire 80 / 150 / 230 m/s, 115 / 140 / 179 damage, 25 / 50 / 100
  ammo (+4 drained at full charge) with the Sm / Med / Lrg trail; charge then switch = no shot.

### Grenade spin [CONFIRMED ORIGINAL: script TransGame.TnProjectileGrenadeBase + Default__TnProjectileDataGrenadeLauncher]
- Was: thrown grenade meshes faced their velocity and did not spin.
- Now:
  - bRotationFollowsVelocity false: the grenade keeps its spawn rotation (throw direction).
  - Tick adds RotationRate × dt to the mesh: Pitch −100000 rotator units/s = −549°/s, inherited by the Flak / Flashbang / Heal
    grenade data.
  - OnHitThing at rest zeroes RotationRate.
- The tumble sign in mesh space follows the vehicle pitch convention [HIGH].
- WFC_CHARGETEST: a Flashbang tumbles at −549°/s and stops at rest.

### Weapon start trace (all forms) [CONFIRMED ORIGINAL: script TransGame.TnPlayerPawn.GetWeaponStartTraceLocation, RE]
- Original: the start trace is ViewLoc + ProjectOnTo(Pawn.Location - ViewLoc, view direction), i.e. the point on the third-person
  camera's crosshair ray nearest the pawn, in robot, vehicle and plane form. Instant-hit and beam traces run from there along the
  aim for the weapon range. Fallbacks: no controller → HmPawn.GetPawnViewLocation; AI pawns have their own override.
- Was: robot shots traced from actor + BaseEyeHeight toward the camera-ray hit point.
- Now:
  - robot hitscan and Repair Ray traces start at the projected point along the view direction;
  - projectiles aim at that trace's hit point;
  - the vehicle MG uses the same start (24f).
- Presentation: the hitscan tracer and the Repair Ray ribbon still start at the muzzle.
- Projectiles (24j): Weapon.ProjectileFire spawns at RealStartLoc = GetMuzzleLoc(), the held weapon mesh's MuzzleFlash socket
  (WeaponDef muzzle bone + authored offset, posed, at the hand socket), aimed at the start-trace hit point [CONFIRMED ORIGINAL].
  The held mesh is already per weapon (syncShownWeapon). Pawn eye + 1.5 m is only a fallback when no posed socket exists
  (mid switch, missing mesh).
- Fix: the projectile spawn hook added 1.5 m along the aim to every origin. Vehicle rockets therefore started 3 m ahead of their
  socket (the caller added another 1.5 m). Both offsets are removed: projectiles spawn exactly at the muzzle.
- WFC_RMUZZLETEST 4/4: Thermo Rocket Launcher, Magma Frag Launcher, Fusion Cannon and Plasma Cannon spawn at the socket (the
  distance measured after the spawn tick equals one tick of flight). The muzzle is 0.68 / 2.12 / 2.03 / 1.70 m ahead of the hand.

### Projectile visuals [CONFIRMED ORIGINAL bindings: AssetTools weapon.json projectile_visual, RE projectile_effect_bindings]
- Was: every projectile drew as an orange box marker.
- Now each projectile carries its weapon's authored visual (projectiles[0].projectile_visual), resolved by class id, else provider
  folder, and accepted only when the file's class matches. Lifecycle:
  - spawn: spawnParticleEffect(FlightEffect, pos, forward = velocity, up);
  - each tick: setParticleEffectTransform;
  - impact / fuse: stopParticleEffect (trails finish), then the ExplosionEffect at the hit location, oriented by the hit normal
    (EmitterPool.SpawnEmitter(ExplosionEffect, HitLocation, rotator(HitNormal)));
  - LifeSpan expiry: no explosion.
- FlightEffect is the body (no static mesh) for all but the thrown grenades. Flak / Flashbang / Heal also draw their class-default
  WEP_Grenade_*_STAT mesh.
- Renderer API from agents/rendering (38c9ecf+), detected at compile time: on a tree without it, the FX calls compile out and the
  box marker stays as a non-original fallback. It also stays when a template is missing from the map's FX data.
- PlasmaCannon charge levels and grenade spin: see "Plasma Cannon charge" and "Grenade spin" (24k). The fuse explosion normal is assumed up [PARTIAL].
- WFC_PROJFXTEST 2/2: 11/11 projectile weapons bind a FlightEffect (15 weapons with visuals incl. grenades, 3 body meshes), all
  projectiles end within 12 s. A standalone check (work/pass23/fxcheck) confirms the detection calls spawn / move / stop with
  Rendering's exact signatures.

### Vehicle weapon origin and muzzle alternation [CONFIRMED ORIGINAL: socket data + RE pass 5 §9g script]
- WFC_VSOCKET: WeaponSocket_Primary sits on each chassis' left gun bone (L_GunRobo01_XT) or the tank cannon (C_Cannon_XB),
  inside the vehicle hull; Starscream's is under the wing, 0.8 m below the physics box.
- Correction: firing only from the left was NOT original. It came from using WeaponSocket_Primary alone.
- Original: the vehicle weapon mesh's MuzzleFlashSockets = [Primary, Primary2] (Primary2 on R_GunRobo01_XT) for
  AssaultRifleVehicle, AssaultRiflePlane, RocketVehicle, RocketPlane and HomingRocketVehicle. HmWeapon.OnPlayFireEffects plays
  the flash at CurrentSocket, then HmWeaponMesh.ChangeSocket advances it ((i + 1) % N) once per shot.
  - Projectiles spawn at the shot's socket (Weapon.ProjectileFire RealStartLoc = GetMuzzleLoc() before the advance) and aim at
    the camera-trace hit point.
  - Instant-hit MG: the damage trace starts at TnPlayerPawn.GetWeaponStartTraceLocation = the point on the camera's crosshair
    ray nearest the pawn (ViewLoc + ProjectOnTo(Location - ViewLoc, view dir)), along the aim; only the flash / tracer alternate.
- Chassis without Primary2 (the tanks) and weapons not in the list stay on Primary.
- HudState vehicleShotSerial / vehicleShotSocket / vehicleShotMuzzle let the flash / tracer glue follow the socket.
- WFC_MUZZLETEST 5/5. Sockets 0,1,0,1… with the muzzle alternating sides: Car2 ±0.5 m, Car4 ±0.71, Jet4 ±1.4, Truck3 ±1.21;
  Tank3 on Primary only.
- The integrated muzzle / tracer effects pick templates from the held ROBOT weapon class (Systems' weaponFx(weaponClass_)), not
  the vehicle weapon: reported to Systems / Integration.

## PASS 23 — human playtest fidelity (2026-10-05)

### Fresh match state [CONFIRMED ORIGINAL: RE pass 4 - PRI / Team Score 0, OldPRI.Reset, TnTeamInfo zeroed on seamless travel]
- WFC_SCORETEST 9/9, three consecutive TDM launches with kills / deaths / damage / a timed-out match in between. At launch and
  after the countdown every value is fresh: team 0-0, personal score / kills / deaths 0, all scoreboard rows 0, clock = TimeLimit,
  full clip, InProgress after PendingMatch. The Frontend route uses the same World::launchMatch reset.
- The visible "wrong score" in the playtest was the Hud_GFX GoalScore default (10, read once at load) filling the TDM bars before the
  match's PointsToWin arrived - fixed on the Frontend side (agents/frontend), not Gameplay state.
- GRI values for Hud_GFX: attackingTeamIndex (CTF / EXT, else -1) and currentObjectiveCountdown (EXT fuse, else -1) [CONF CDO
  defaults], competitiveScoreEnabled 0 [HIGH: not authored]. DOM / KOTH objective countdown [PARTIAL].

### Vehicle handling [RE TARGETED_PASS4 §A CONFIRMED ORIGINAL; measured with WFC_VEHPHYS]
- Open [HIGH / human check]: grounded pitch over small steps. A 0.25 m riser pitches the hover body about 0.5° (Experimental
  step_025), because UpdateTurn replaces ω each step and the spring torques act only within that step. Not yet traced:
  whether the original applies the correction before or after PhysX integrates the same tick's spring forces. Asked RE.
- Drop recovery (WFC_DROPTEST): Car2 / Truck3 / Tank3 land at about 18 m/s, compress 0.89 / 1.03 / 1.14 m and recover over
  about 1.5 s without overshoot (Experimental drop10 "instant" = their check catching the fall-through of the rest height).
Human playtest: jumps too high in some situations, violent wall bounces, teetering / rolling about an odd axis, not settling.
Each RE item was compared with the code and measured (Streets, flat run-up into a vertical wall; Sideswipe car, Optimus truck,
Warpath tank).

Defects found and fixed:
1. **UpdateTurn semantics** (cause of the teetering / rolling / not settling).
   - Original: each step the angular velocity is REPLACED by axisAngle(current → upright with view yaw) × mask / dt.
   - mask = (0.05, 0.05, 1) normally; (1, 1, 1) when ShouldUpright (no suspension contacts, or up.Z < 0.01).
   - The rebuild applied nothing on the ground (spring and contact torque accumulated step after step) and only 5% per tick
     in the air (the jump nose-up spin kept turning).
   - Now the original replacement for car / truck. The tank keeps its own rule: no pitch / roll correction while stable on
     the ground, else TurnRate 0.05 (RE C2).
   - Glancing (22°) wall hit, Sideswipe hovering: max tilt 69° → 6.3°, pitch / roll rate 218 → 9.7 °/s. Truck 1.8°.
2. **Boost (Driving) jump never fired.**
   - A provisional overhead-hull guard started its ray at the COM height. That is floor level while driving on the wheels,
     so it read the floor as a ceiling and zeroed v.y in the jump's own step.
   - The probe now starts ≥ 0.1 m above the root.
   - Boost jump apex: car 4.93 m, truck 4.91 m vs RE local (600, 0, 1400) → 1400² / (2 × 1940.4) = 5.05 UU-m.

Verified unchanged (already as the original):
- **Hover jump:** additive world Δv 1200 UU/s, RB gravity −2940 × 0.66, fresh press only, 0.3 s cooldown counted on the
  ground, IsOnTheGround = contacts > 0 and average normal Z > 0.707.
  - Apex 3.81–3.83 m (RE 3.71 + spring). A jump pressed right at landing peaks at 1.8–2.6 m. Holding Jump = one jump.
- **Suspension:** 4 diagonal probes, implicit spring with m / 4, push-only, no force without a hit.
- **Walls:** head-on rebound 0.00 m/s for car, truck and tank. Physmat restitution 0.05 and no script bounce: the rebuild
  removes the into-wall velocity (restitution 0) [HIGH: 0.05 vs 0, PhysX combine untraced].
- **Frontal boost crash (> 0.866 into the wall) drops Driving → Hovering.** Holding Boost re-enters Driving after the drift
  window (A6).

Remaining:
- **Tank glancing wall:** a diagonal probe losing the floor at a wall base tilts the tank up to its stability limit (~30°)
  before TurnRate 0.05 engages. That is the RE tank rule, but the PhysX hull-to-wall contact that might also support it is not
  modelled [PARTIAL].
- **Ramps / terrain:** the probes and springs follow the authored model, but ramp launches were not measured separately
  against a capture [PARTIAL].
- WFC_VEHPHYS 26/26: settle, jump, re-jump, boost jump, held jump, and walls (hover / boost, head-on / 22°) × 3 vehicles.

### Scout body height idle vs locomotion [HIGH CONFIDENCE authentic: RE pass 4 + WFC_HEIGHTTEST measurement]
- Playtest: the Scout looks crouched at rest and much taller while running.
- Measured per tick (WFC_HEIGHTTEST, heights above the feet): capsule centre, mesh origin and root bone (C_Root_Reference_XR) never
  move. Root and hip scale stay 1.000. Only the pose changes:

  | body | idle hips / head (m) | jog hips / head (m) |
  |---|---|---|
  | Car2 Sideswipe | 1.531–1.540 / 2.42 | 1.941–2.322 / 3.51 mean |
  | Car4 Barricade | 1.490–1.520 / 2.63 | 1.941–2.321 / 3.51 mean |
  | Truck Optimus | 1.971–1.988 / 3.37 | 1.865–2.180 / 3.46 mean |

- Cause (RE pass 4): Car2 / Car4 own AnimSets hold only idles and transforms. Jog / walk / sprint come from Shared_ROBO_ANIM, authored
  on Starscream, applied with bAnimRotationOnly = False (translations as authored). The original Scout's hips are about 154 UU idle
  and 194–232 UU jog, the same as measured here. Root bone Z = 0 in every clip, so no root motion.
- Not root translation, scaling, capsule coupling, retarget error or a pivot mismatch. Left as is. A shipped capture would move this
  to CONFIRMED.

### Input details [CONFIRMED ORIGINAL: shipped PC bindings]
- Melee is Q or the middle mouse button.
- A Fire click shorter than one simulation tick (144 / 240 Hz frames) is latched for the next step (one shot attempt); a release
  before the refire still clears it, as StopFire clears PendingFire.
- PlayerController::hudAimState().weaponClass = the held class (TnWeapon<id>, or TnWeaponFlag1Hand / TnWeaponBomb while carrying);
  it was hard-coded TnWeaponIonBlaster (Integration report).

### Weapon switching [CONFIRMED ORIGINAL: HmInventoryManager / HmWeapon / Engine.Weapon script; Xe-TransInput.ini; RE pass 4]
- Playtest symptom: the selected primary appeared, but the player could not switch to the secondary.
- Chain checked:
  - profile / class loadout → Frontend selection (PCD_MP WeaponTypes; slots 0–1 customisable) → applyLoadout;
  - no refusals in the playtest log; both guns are in the inventory (WFC_SWITCHTEST, four classes).
- Defects fixed:
  1. The mouse wheel did nothing. The shipped binding is NextWeapon on wheel up, wheel down, PageUp and PageDown.
     There is no PrevWeapon, so every input cycles forward.
  2. Switching was refused while reloading or while a switch was in progress. Original HmWeapon.TryPutDown:
     - Active: put down now;
     - WeaponReloading: put down now, the reload is abandoned (no RefillClip);
     - WeaponFiring: put down now if MinReloadPct 0.5 of the refire interval has passed, else at the next RefireCheckTimer;
     - WeaponPuttingDown: retarget the pending weapon;
     - WeaponEquipping: put down again once equipped.
- Switching stays blocked while transforming, in vehicle form, during a melee attack, and with DisallowWeaponSwitching
  (Poke). Heavy weapons are dropped when switched away.
- Test WFC_SWITCHTEST 32/32: Scout (Car2), Scientist (Jet4), Soldier (Tank3) and Leader (Truck3), each:
  - wheel / PgUp / PgDn ×4 idle;
  - while moving, jumping (airborne), firing and reloading (reload abandoned, clip unchanged);
  - transform to vehicle and back (active weapon kept), then switch again;
  - HUD weaponId = the active weapon.

## PASS 22 — SELECTED CHARACTERS, CLASSES, VEHICLE FORMS, WEAPONS, MULTI-MAP (2026-10-05, gameplay agent)
Inputs:
- AssetTools per-chassis export `VerticalSlice/Characters/<ChassisId>` (vs_roster_export) and `roster_package.json`;
- RE TARGETED_PASS3 §A (selection → pawn), §C (car / tank / jet simulations, script bytecode), §C6 (FindSpot);
- RE confirmations relayed this pass: specialty health, versus weapon data;
- decompiled TransGame script; authored.db (read-only).

### Selected character → pawn — CONFIRMED chain, no substitute body
- **Chain:** CharacterSelection (Frontend fills it from GameFlow::SelectedCharacter) → team faction (FFA = 1) →
  `resolveChassis` (CharacterData.ChassisTypes[faction], else the class preset) → `Match` chassis check →
  `World::applyChassisToLocalPawn` (TnPawn.ApplyTransformer) → ApplySpecialty → ApplyWeapons.
- **Chassis definition** (`ChassisDef`), read from `character.json` + roster collision:
  - robot / vehicle glb with skeleton and clips;
  - ArmBlueprint mesh + anims;
  - WeaponSocket_Primary / _Secondary and the vehicle weapon socket, via UE → glTF socket math (verified against the
    recovered Optimus matrix);
  - ROBODEF speeds, accel, air control, terminal velocity;
  - acrobatics JumpHeight;
  - momentum blueprint;
  - hover / car / suspension / wheel blueprints.
- **WFC_CHASSISTEST 13 / 13:** all 27 multiplayer chassis load. "Truck" reproduces every hand-entered Optimus
  constant. An unknown id fails.
- **No fallback** (the original has none either: FindChassis failing gives a body-less pawn + log):
  - an unavailable body refuses the spawn, retries every second, and sets `spawnError` (HUD) with a loud log;
  - `drawnChassis` is the spawned body;
  - `chassisFallback` is removed.
- **Visually verified** (screenshots): Sideswipe, Starscream, Warpath and Soundwave skinned, holding their own weapons.
- The direct boot and the harnesses use `Characters/Truck` (RB_OptimusWeaponArm_SKEL, the authored MP mesh).
  `WFC_CHASSIS=<id>` boots or selects any chassis.

### Class behaviour — CONFIRMED (script + authored; RE agrees)
- ApplyCharacter → TnPlayerPawn.ApplySpecialty → TnSpecialty.Apply, when the game's ApplySpecialtyBuffs is true
  (Default__TnGame true; only campaign / survival / campaign-lobby set it false):
  - **SetSpeedMultiplier(SpeedMultiplier):** a per-source factor (TnPawn.UpdateSpeeds multiplies them; fine aim stacks);
  - **InitializeSegmentedHealth(Health_<Class>):** replaces SharedHealth.

| class | segments | HealthMax | overshield | speed × |
|---|---|---|---|---|
| Leader | 5 × 60 | 300 | 200 | 0.95 |
| Scientist | 3 × 60 | 180 | 200 | 0.90 |
| Scout | 4 × 50 | 200 | 200 | 1.00 |
| Soldier | 6 × 55 | 330 | 200 | 0.90 |

- **Correction:** versus does **not** use SharedHealth 550 (Passes 19–21 did). The custom specialty comes from the slot;
  iconic characters use the chassis DefaultSpecialty [HIGH].
- **Iconic specialty:** the iconic preset's CharacterData.Specialty (CONFIRMED RE via AssetTools), not the chassis
  DefaultSpecialty (UI grouping only). Sideswipe = Soldier, Starscream = Soldier, Soundwave = Scientist, Optimus = Leader.

### Abilities — framework CONFIRMED script; Dodge implemented, the rest PARTIAL
- TnAbilityManager:
  - CharacterData.Abilities[0] on Ability0 (Shift), [1] on Ability1 (Ctrl);
  - SpamPreventionTime 1.0;
  - versus skill-data index 0 (TnMultiplayerGame.GetSkillDataIndex) → Cooldown[0], and no resource cost;
  - the cooldown starts at CanStartCooldown;
  - PlayerWalking.CanUseAbilities refuses while reloading or dodging.
- **Dodge** (TnAcrobaticsManager.Dodging):
  - direction from TnPlayerInput.Dodge: |up| ≥ |right| → forward / back, else right / left;
  - PHYS_Flying at Acrobatics DodgeSpeed 3000 for DodgeTime 0.5;
  - EndState clamps to MaxAir / MaxGroundSpeed; a wall hit ends it early;
  - CanDodge requires landing since the last dodge; cooldown 2.0 s.
  - Test: Sideswipe dodges right at 30 m/s, 10.9 m in 0.6 s, refused while cooling.
- **Warcry** (TnAbilityWarcry, CONF):
  - friendlies within AoeRange 3000 UU (FFA: the owner) get TnBuffWarcryIncreaseDamage ×1.1 / 1.2 / 1.3 and
    DecreaseDamageTaken ×0.5 / 0.4 / 0.3;
  - level = Clamp(friendlies − 1, 0, 1), +1 with the ImprovedWarcry skill (skills not applied);
  - BuffTime[0] 15 s; Cooldown[0] 60 s, started after the owner's buff ends (HadAndLostBuffCondition).
  - Test: Optimus with one friendly in range took 40 of 100; cooldown 59.5 s right after the buff.
- **Shockwave** (TnAbilityShockwave, CONF):
  - after Delay 0.25 s, Blueprint[0] Damage 65 within 2500 UU (bDoFullDamage) from the PositionSocket; Cooldown 60 s;
  - the owner is not hurt [HIGH]; the 700000 momentum knock-back is not applied [PARTIAL].
  - Test: 65 damage to an enemy 10 m away, nothing before the delay.
- **Cloaking:**
  - TnBuffCloak BuffTime[0] 20 s;
  - ExposeSelf removes it on firing (TnWeapon.OnPreServerFire) and on damage taken (TnPlayerPawn.TakeDamage);
  - the TDM name-tag label is hidden (TnObjectiveMarkerTypeTransformerVersus DisableLabel);
  - cooldown 15 s after it ends;
  - HUD `cloaked`; the cloak shader belongs to Rendering [CONF script].
- **Whirlwind** is a melee attack (TnMeleeService type 3), so it waits for the melee system [PARTIAL].
- **Hover:**
  - TnAbilityHover → TnAcrobaticsManager JumpingToHover (jump to HoverJumpHeight 500 UU), then Hovering when descending:
    PHYS_Flying, MaxAirSpeed HoverAirSpeed 500 UU/s, HoverDuration 7 s;
  - jumping or expiry → Falling; TnBuffIncreaseDamageDuringHover ×1.4 while hovering;
  - Cooldown[0] 35 s once not hovering.
  - Test: rose 4.9 m, held height, ≤ 5 m/s, fell back; cooldown 31.4 s, 3.6 s after the end.
- **Melee** [CONF RE TARGETED_PASS3 §H; TnMeleeSet authored]:
  - Q → MELEE_WeaponAttack (player pawns never stomp): Melee_EnergonSword_01 / _03 alternately, full body.
  - Assist target: an enemy within 20 m inside the picker cone clamp(4°, atan(3.5 m/d), atan(4.5 m/d)).
    The pawn lunges toward it, yaw only: 25 m/s for 0.25 s, then ×0.3. Ground speed ×0.75 while attacking.
  - Damage sweep at t 0.19 s for 0.35 s: box (250, 250, 350) UU at MeleeSocket_SmallRobot. Clear-trace check;
    one hit per actor per sweep; 150 TnDamageTypeMelee.
  - **Whirlwind** ability → MELEE_Whirlwind: Transform_Whirlwind_ROBO, upper body, ground speed ×1.2.
    Eight 0.4 s sweeps (0.9 … 5.19 s), box (450, 450, 200) at PositionSocket, 85 TnDamageTypeWhirlwind each.
    The trigger fails (no cooldown) unless the melee manager is idle in robot form;
    Cooldown 60 s starts once the whirlwind ends.
  - PARTIAL:
    - melee impulse / momentum and hit reactions are not applied;
    - flag / bomb carrier 9999 attacks, the type-2 melee weapons' own attacks, GunButt combo and per-chassis sweep times
      (the LightMedium shared set is used for all).
  - Tests (WFC_PARTICIPANTTEST 7/7): Q 150 with a 5.4 m lunge; Whirlwind refused during the swing, hits in both
    early windows, cooldown held until the end.
- **Homing lock-on** [CONF RE TARGETED_PASS3 §H2; authored WEPDATA / PROJDATA in WeaponTable.inc]:
  - TnWeaponHoming.Active.Tick for the active weapon (on foot or the vehicle weapon).
  - Target: an enemy within weapon range inside picker index 4 about the crosshair ray: 4°, clamped to cover
    600–700 UU (robots) or 500–700 UU (cars; other vehicle forms use the car picker [PROV]).
    Robots are skipped while CanLockOnToRobots is false (every MP homing weapon).
  - Lock: LockOnTimer reaches LockOnTime → locked; a target change resets it; HoldLockOnTime drops it
    with no target.
  - The shot carries the target only if locked. The projectile homes with HomingForce, switches to
    ClosingForce within ClosingDistance and explodes after ClosingTime; capped at MaxSpeed; stops homing if
    the target dies or becomes a robot.
  - HUD: lockTarget / lockProgress / locked.
  - Test: no lock on a robot; vehicle lock at 0.52 s (0.5 s + frame); the rocket aimed 4 m off at 50 m hits.
### Pass 22 multi-map stress (2026-10-05, build of 1fc54a3; logs work/pass22/logs/maps)

All ten MP maps load with their own KillZ (BASE WorldInfo), hazard volumes, pickups and objectives. WFC_MAPSUITE passes on every map.

| Map | Oracle (authored ReachSpecs arrived) | Tours robot / vehicle (fell) | Transforms under map / KillZ | Chaos (20 × 20 s) |
|---|---|---|---|---|
| Streets | 852/852 | 100/122, 98/122 (0) | 0/380 | 0 under, 0 KillZ |
| Gorge | 1473/1476 | 134/147, 126/147 (0) | 0/380 | 2 under (lower path under a deck), 1 KillZ (chasm) |
| Rust | 1672/1706 | 116/139, 118/139 (0) | 0/380 | 0 / 0, 1 stuck |
| Debris | 349/350 | 53/75, 55/75 (4) | 2/380, both KillZ | 7 KillZ |
| Berth | 1302/1302 | 128/143, 127/143 (0) | 0/380 | 0 / 0 |
| Seed | 972/972 | 109/136, 110/136 (0) | 0/380 | 0 / 0 |
| Remnant | 5600/5916 | 1305/1363, 1242/1363 (0) | 0/380 | 0 / 0, 2 stuck |
| BrokenHope | 5570/6074 | 1132/1196, 1112/1196 (0) | 0/380, 8 refused | 1 KillZ |
| Molten | 887/914 | 87/119, 87/119 (0) | 17/380: 16 KillZ from one start (lava pit edge), 1 under a deck 2.96 m | 1 KillZ |
| Complex | 1317/1322 | 109/130, 111/130 (1) | 0/380 | 2 KillZ |

Classification:
- **Debris:** KillZ 100.0 m sits just under the lowest walkable floor (100.4 m), so any fall off a platform edge dies:
  authentic for the space map. The transform KillZ cases are pawns carried off edges at 15–28 m/s.
- **Gorge / Molten under-floor cases:** a pawn on a lower path beneath a walkable deck, not inside geometry.
- **Remnant / BrokenHope oracle shortfalls:** long jump / air-path ReachSpecs on the two largest maps (6000 runs).
  No falls; not yet broken down [PARTIAL].

- **Splash falloff** now subtracts the victim's collision radius before scaling:
  Dist = max(d − ColRadius, 0), scale 1 − Dist/DamageRadius [HIGH stock UE3 Actor.TakeRadiusDamage].
- **Grenades** [CONF script TnGrenadeBag / TnGrenadeThrower / TnProjectileGrenadeBase + authored; RE §H4]:
  - G ("Throw Grenade") in robot form → TnGrenadeBag.TossGrenade. CanToss = ammo and no FireInterval timer (1.5 s);
    otherwise dry fire.
  - The bag is given without activation: not in the weapon-swap cycle; reserve = MaxAmmoCount (Flak 1, FlashBangs 2).
  - Target: view trace from 10 m to 100 m (hit or end point). GrenadeThrow upper-body clip; spawn after
    TossDelay 0.4 s at MeleeSocket_RightHand; ExposeSelf.
  - Velocity:
    - SuggestTossVelocity(TossStrength 110 m/s) [PROV: native; the exact lower ballistic arc under world gravity];
    - AdjustTossVelocity lerps toward LowPitchSpeed below LowPitchDegrees.Max;
    - Init scales by lerp(SpeedScaleAtMinPitch, AtMaxPitch, pct(pitch, MinPitch, MaxPitch)).
  - Flight: gravity × GravityScale. Impacts reflect × BounceDampening and rest when v² < 500 UU²/s².
    The fuse (RandomInRange(FuseTime)) starts on the first impact; ExplodeWhenHittingPawn grenades detonate on a pawn.
    Explosion = HurtRadius (Flak 325 / 20 m, TnDamageTypeFlakGrenade).
  - The surface normal at a world impact is estimated (floor or reversed travel) [PROV]. Flashbang blind, heal
    grenade healing and kamikaze-mine seeking are not simulated [PARTIAL].
  - HUD grenades (reserve, −1 without a bag).
  - Test (WFC_WEAPONTEST 17/17): spawned at 0.4 s, first impact 0.35 s later, exploded 2.00 s after it; the empty
    bag refused the next toss.
- **Tank cannon** [CONF script + authored VEH_Tank_ANIMTREE; RE §H3]:
  - WeaponPrimary = HmSkelControl_TurretConstrained on C_Cannon_XB, actor space, no constraints.
  - Player DesiredBoneRotation = (view pitch, hull yaw, 0): the cannon only pitches; the hull yaw is camera-slaved.
  - LagDegreesPerSecond 360 applied as a max turn rate [HIGH].
  - Applied as a mesh-space pitch over the animated pose (cannon level at rest) [PROV].
  - Test: 0.300 rad view → 0.300 rad hull-relative cannon, peak 360°/s.
- **Knockback** [CONF RE TARGETED_PASS3 §I]:
  - TnPawn bIgnoreForces: only RequestRespectForcesApplied damage types push: Melee / WeakMelee / Whirlwind,
    Shockwave, AOE*, HeavyTankShell*, ShieldPush*, OmegaAOE*, ExplodeWithForces …
    (* bExtraMomentumZ: Z = max(Z, 0.4|M|)). Weapon, projectile and grenade damage types give none.
  - Momentum / Mass 100. Robot: Pawn.AddVelocity (walking → falling; halve a rising Z above JumpZ).
    Vehicle: ×0.5 linear velocity.
  - Sources:
    - melee normal(victim − attacker) × Impulse: 30000; flag / bomb 80000; Whirlwind 2000;
    - Shockwave 700000 from the origin.
  - Test: melee 3.00 m/s, Shockwave 70.0 m/s, IonBlaster 0, vehicle 1.50 m/s.
- **Flag / bomb carrier** [CONF script TnWeaponFlagBase / TnInventoryManager / TnPawn.Transform; RE §I]:
  - The objective is the held WT_Heavy weapon (Code Of Power / Bomb, HUD heavyWeapon): no gun fire, and
    grenade toss refused (dry fire).
  - Q = the MWT_Flag / MWT_Bomb attack: Melee_Mace _01/_02/_03 sweeps, 9999 damage, impulse 80000, no lunge.
  - Dropped (a pickup at the carrier) on Transform to vehicle (DropHeavyWeapons), on a weapon swap (ChangedWeapon
    TossWeapon) and on death.
  - Pickup is contextual, not on touch (TnWeapon.PickupWhenTouched needs the class already held):
    E ("Interact / Pick Up") → TryPickup → ServerPickup → TnPickupManager.Pickup on a touching factory or dropped
    flag / bomb that passes ValidTouch. HUD pickupPrompt. [CONF RE §J]
  - TnPlayerPawn.CanPickupInventory rejects vehicle form, meleeing (and downed) pawns; defenders are rejected for the flag.
    The pickup line-of-sight recheck is not run [PARTIAL]. Diagnostic participants hold the pickup button (no AI).
  - DropFrom FindSpot box (450, 450, 100) is not run; the drop is at the carrier [PARTIAL].
  - Tests (WFC_CTFTEST 12/12, Streets + Gorge): transform drop, no vehicle re-pick, robot re-pick on a new touch;
    the local carrier's gun is blocked and a swap tosses the flag.
- **Barrier** [CONF TnAbilityBarrier / TnBarrierSpawnable script + authored; RE §I3]:
  - Skill_Barrier anim, then SpawnDelay 0.5 s: wall at Location + (1000, 0, −200) rotated by the pawn, facing it.
  - Collision: the PHYSSYS box (167 × 1736 × 823.5 UU at (−59, 0, 91) about C_Robo01_XT, bone frame taken as
    the actor frame [PROV]) as a dynamic set in the pawn and weapon collision.
  - Blocks pawns, hitscan and projectiles (zero-extent blocking HIGH); takes hitscan and radius damage, not melee.
  - BarrierHealth 1000, DegenRate 15/s; at 0: FadeOutTime 3 s, then gone; destroyed with the owner.
  - Cooldown 20 s once the barrier is gone. Mesh WEP_Barrier_SKEL with Barrier_Equip, drawn by Gameplay.
  - PARTIAL: the flashbang instant break and the TnBuffIncreaseBarrierHealth +500.
  - Test (PARTICIPANT 10/10): up at 0.5 s; shots absorbed (999 → 784) with the target untouched; owner
    stopped at 6.9 m; 60 HP decay in 4 s; cooldown 20 s after the fade.
- **SpawnAmmoCrate (ammo beacon)** [CONF TnAbilitySpawnInventory / SpawnAmmoCrate / TnDroppedPickupAmmoBeacon / Defrag
  script + authored]:
  - Skill_Barrier anim; SpawnDelay 0.5 s; dropped from the owner with TossVelocity (2000, 1200, 0) rotated by the
    owner; falls and lands.
  - BeaconLifespan 60 s; gone with the owner.
  - Each tick, the owner and teammates within 1500 UU with line of sight: current weapon FillReserveAmmo, and
    TnBuffAmmoBeaconIncreaseDamage ×1.15 (BuffTime 1 s, refreshed).
  - Health 100: owner / team damage ignored. Not a pickup.
  - Cooldown[0] 60 s once it is gone. The HUD exposes the mesh position for Rendering
    (PROP_NEU_AmmoPickup_STAT).
  - PARTIAL: FadeOut duration (removal is immediate); skill gifts / grenades (no skills in versus).
  - Test (PARTICIPANT 11/11).
- **Buff killstreaks** [CONF authored TnKillstreak* CDOs (TnAbilityAddBuff BuffTarget / BuffToAdd) + script]:
  - **Orbital Beacon:** team TnBuffSeeEnemyObjectiveMarkers 30 s. SetupEnemyMarker draws enemy markers unless the
    enemy has a Warcry buff.
  - **Orbital Beacon 2.0:** other team TnBuffHardLocked 10 s (marker for the instigator's team) plus 1 damage
    TnDamageTypeFlashBang. HardLocked FloatModifier[0] 1.4 = damage taken (TnPawn._AllDamageModifierSelf) [CONF RE §K].
  - **Health Matrix 2.0:** team TnBuffRefillHealthOnKill 60 s. TnPawn.HandleDied gives the killer
    HealDamage(TnHealTypeHealthPickup = SHT_AddAllSegments).
  - **EMP:** other team TnBuffAbilityJammedKillstreak 30 s. TnBuffAbilityJammed.Apply: CooldownMultiplier 0
    (cooldowns frozen; ready abilities still usable); Cloak / Disguise / Warcry removed; hover falls; whirlwind aborts.
  - HUD seeEnemies / hardLocked / refillOnKill / abilitiesJammed (s left). Test (PARTICIPANT 12/12).
- **Drain** [CONF authored TnAbilityDrain / TnBuffDrainSource Blueprints[0] + RE §J]:
  - Self buff 7 s at caster speed ×0.7.
  - Each tick, every enemy within 2000 UU with line of sight takes 25 DPS (TnDamageTypeDrain); the caster heals
    35 HPS per target (heal type AddHealthToAll [PROV]).
  - Cooldown 60 s after the buff (HadAndLostBuff). Removed by AbilityJammed.
  - Beam FX DrainRay_Beam_FX from MeleeSocket_LeftHand → Rendering (HUD drain).
  - Test (PARTICIPANT 13/13): enemy −50 in 2 s.
- **SpawnSentry** [CONF TnAbilitySpawnSentry / TnSentryPawnAbility / TnAiSentryController + authored Default_TURRETDEF /
  Default_WEPDATA / Sentry_DSYS; RE §J]:
  - Spawn: 0.2 s delay; owner + 375 UU up, clamped by a trace, then settled on the floor; the previous sentry is killed.
  - Body: 135 HP draining over Lifetime 30 s; owner damage ignored; melee kills it; dies with the owner.
  - Aim: closest visible enemy within pitch ±45° (SightRadius 30000); YawPitchControl 270°/s; fires within 3° and
    6000 UU.
  - Weapon: 8 instant-hit (range modifier 1.0 to 8000 → 0.5 at 30000) every 0.12 s, spread 0.1; heat +2 to 100 then
    OverheatDelay 2 s (heat reset [PROV]). Kill credit to the owner.
  - Cooldown 60 s once gone. Mesh WEP_SentryDeploy_SKEL with WEP_DeployedTurret_Activate, drawn by Gameplay.
  - Hit volume: the 200 UU cylinder [HIGH].
  - PARTIAL: flashbang dormancy, Rocket / Repair blueprints, turret pitch on the mesh, 5 s corpse, the 2-sentry claim
    limit (one sentry per owner here).
  - Test (PARTICIPANT 14/14): 16 shots in 2 s, hits of 8, drain 18 HP in 4 s.
- **GuidedMissile** (ability and the Soldier 7-kill Omega Missile streak) [CONF RE §J3 + authored GuidedMissile_PROJDATA /
  GuidedMissile_STRATEGY]:
  - Launch: 1.0 s Skill_GuidedMissile; spawned along the controller rotation with pitch clamped to 3.8°–90°.
  - GuidingMissile: inputs cleared (pawn stops); camera attached at (135, 0, 125) UU in the missile frame, FOV 120.
  - Steering: per-tick camera deltas → LeftRight / UpDown clamp ±1 → lateral ControlStrength 2500; speed held at 2000.
  - Ability press detonates: 10000 within 4500 UU. The owner within 45 m dies to self damage (×0.45).
  - Fuse 30 s; cooldown 45 s once the missile is gone.
  - PROV: chest socket (eye height used), fuse expiry detonation, camera-delta rotator units.
  - Test (PARTICIPANT 15/15 on Streets, Gorge, Debris and Rust).
- **RollerSphere** [CONF TnAbilityRollerSphere / TnRollerMineAbility CDOs, RE §J4, AssetTools ability_physics.json]:
  - Spawn: 0.5 s, at owner + (500, 0, 100) if safe (else retry every 1 s), local velocity 2750 UU/s.
  - PhysX sphere: radius 1.208 m (241.5 UU × scale 0.5), LinearDamping 0.6 (authored PHYSMAT); Friction 0.7 and
    Restitution 0.3 (Engine PhysicalMaterial defaults).
  - PROV: slope acceleration, angular damping, mass.
  - ArmTime 3 s; Fuse 10 s; Health 200 (enemy damage only).
  - Armed contact with an enemy → 135 / 1500 UU (TnDamageTypeRollerMine, no momentum).
  - Owner / team melee kick: +5000 UU/s horizontal.
  - Aura: visible enemies within 1500 UU get speed ×0.75 (1 s robot / 2 s vehicle), refreshed.
  - Gone with the owner; cooldown 60 s once gone. Mesh RollerMineAbility_STAT for Rendering (HUD rollerPos).
  - Test (PARTICIPANT 16/16): 26.4 → 14.5 m/s in 1 s (e^−0.6); safe before arming; armed contact −135.
- **Class-pool abilities** [CONF RE §K]:
  - **HardLock (Mark Target):** the homing-lock pick (picker 4, robots allowed) gets TnBuffHardLocked level 0 for 10 s
    (marker for the team; damage taken ×1.4); no target → fails; Warcry removes it.
  - **AbilityJammer:** projectile 10000 UU/s from offset (0, 175, 25), enemies only → TnBuffAbilityJammed 15 s.
    Abilities are blocked (controller HasDerivedBuff), cooldowns frozen, cloak / warcry / drain stripped.
  - **TransformDisruptor:** projectile 6000 UU/s → TnBuffTransformDisruptor 3 s: forced into the other form,
    transforming disabled.
  - Shots aim through the crosshair. Their life after a miss (3 s) is PROV.
  - Cooldowns 60 s.
  - Test (PARTICIPANT 18/18).
- **Camera obstruction uses simple collision only** [CONF RE MILESTONE04_CAMERA_COLLISION addendum, f150a6a]:
  - execTraceCamera → SingleLineCheck with flags World | 0x0A000000 (0x02000000 = BlockCameras). No complex / per-poly bit.
  - Static meshes are traced through their simple collision (UseSimpleLineCollision / UseSimpleBoxCollision default true),
    which is what collision_pawn.glb holds.
  - Example: in Streets' Ceiling_Arch_STAT (StaticMeshCollectionActor_2508) the curved render underside below the simple
    slab has no collision. A camera can enter it, in the original too (M08 soak frame 20_TDM_508e): authentic, left as is.
- **Class preset grenades** [CONF authored TR_MPPlayerCharacterData_p.<Class>_PCD_MP]: Scout FlashBangs, Scientist HealGrenades,
  Soldier FlakGrenades and Leader KamikazeMines are equipped on the class's chassis. The exported per-chassis on-foot list
  omits them there, so a grenade bag is accepted when it is the selection class's preset grenade; other classes' grenades
  stay refused. Reported by the M08 soak (every preset grenade was refused). WEAPON 19/19.
- **Weapon / spawner killstreaks** [CONF RE §K + authored]:
  - **P.O.K.E. 2.0:** TnWeaponPoke for 20 s (SecondsUntilDeactivated [H]): DisallowWeaponSwitching, ground speed ×1.5.
    Fire or Q = the MWT_Poke attack: Melee_Axe sweep at 0.335 s, 9999 TnDamageTypePoke, impulse 200000, lunge.
  - **Nucleon Shock Cannon:** HeavyRocketTurret (HeavyTurret_Rocket_WEPDATA: 10 rockets, 1.75 s, 120 m/s, 500 / 25 m),
    WT_Heavy: dropped on swap or transform (not re-takeable [PARTIAL]); ground speed ×0.75.
  - **Thermo Mine Re-Spawner:** 15 s buff spawning a kamikaze mine every 2 s at owner + (400, 100, 0).
    Mine: hover 2 s (1 m [PROV within 50–150 UU]), then seeks the closest visible enemy within 2000 UU at 2300 UU/s;
    125 / 500 UU on contact; health 50; life 60 s.
  - All 12 class killstreaks are implemented. Test (PARTICIPANT 21/21 on Streets and Gorge).
- Remaining unimplemented abilities (class pools only): Disguise, DecoyTrap are listed per slot and reported
  unimplemented (log + HUD `implemented = false`) [PARTIAL].
  Skills are not applied in versus (skill-data index 0, no skill effects) [PARTIAL]; killstreaks: see above.
- **Correction:** the Pass 21f contract doc said robot Shift ran a dash. It did nothing until this pass.

### Vehicle forms — CONFIRMED script (RE §C), rigid-body details PROVISIONAL
- **Car** (Car–Car7): the same TnHoverCarSimulation as the truck.
  - Blueprints: HoverCar_Physics (accel 4000, mount radius 100, dash 0.5 s @ 3000); HoverCar_Supension (K 8000,
    rest 200, D 4000); Car_Physics (mass 1500, 4 wheels from TnWheelPhysicsBlueprint LocalPosition / friction).
  - Hover dash along the **dominant stick axis** (TnCarForm.Hovering.DoDash).
  - **Barrel roll:** Shift while boosting → Roll(): v += yawFrame(0, dir·1200, 1000 − vz); RollDuration 0.7, cooldown
    2.0. A full turn completes in VEHTEST.
  - The roll rate (one turn per RollDuration) stands in for the unrecovered PhysX max angular velocity [PROV].
- **Tank** (TnHoverTankSimulation):
  - suspension 4 rays, radius 250, K 20000, D 6000;
  - strafe servo in the camera-yaw frame, only when stable;
  - boost cap 2500 with input forced forward; release → drift 0.5 s; jump every 0.5 s;
  - pitch / roll corrected unless stable on the ground;
  - Shift "180" = view half-turn, cooldown 1.2 s [PROV: TnTurnAroundCameraBehavior timing].
  - Cannon recoil (−750 local X on fire) waits for vehicle weapons [PARTIAL].
- **Jet** (TnPlaneForm Hovering / Flying), gravity cancelled in both modes:
  - **Hover:** servo in the full view frame (mask 1,1,1), accel 2500 × max(drift², |stick|), cap 1500. Ascend / Descend
    (C / V, the shipped binding) → Dash ±Z at 1000. Roll at |stickX| ≥ 0.5, 0.6 s @ 3000.
  - **Flight:** boost held → always thrust along the view (4000 / 3000), quadratic lateral drag (DragCoefficient 600).
    Lean = RLerp((−pitch²·27, yaw²·16, yaw²·77)°, 0.1), with the pitch term fading 45 → 60°. Roll 0.8 s. Release, or
    a hit above 3000, returns to hover.
  - The plane rigid-body mass in the drag formula is unknown (100 used) [PROV]. The motion lean of hover is omitted
    [PARTIAL].
  - **Camera:** HoverPlane ±45° / 9 m / FOV 80; FlyingPlane ±80° / FOV 100 [CONF authored]. Follow-camera behaviour
    [PROV].
- **Vehicle FX and sounds** drawn by Gameplay (VehicleFx: BoostFx / HoverFX / JumpFX / ram) are OptimusTruckForm's, on
  VH_OptimusPrime bones. They now play only for the Optimus chassis (Truck / Truck7). Other chassis' authored sets
  (character.json vehicle.fx, e.g. Starscream Afterburner_D_FX) are left to Rendering [PARTIAL].
- **Hulls:** each chassis' VH_*_PHYSSYS convex hull (BodySetup ConvexElems bounds, C_Reference_XR) from authored data
  [CONF] (VehicleHullTable.inc). Element 0 of VH_Optimus_PHYSSYS reproduces the Pass 17 hull exactly.
- **ChassisOffset:** unset → 0 (no class default authored). The loader first defaulted it to Optimus's 15, which lifted
  the COM of Soundwave and others wrongly.
- **VEHTEST forms:**
  - car rest at the spring L_eq;
  - car dash right 30 m/s;
  - car barrel roll 180° max roll, landing level;
  - tank hover 15 / boost 25 with strafe input ignored / drift after release;
  - jet holds altitude, ascends at 10 m/s, flies 40 m/s along the view, releases to hover.
- **Truck numbers unchanged** (rest 1.2872 m, steering, nitro, ramp sweep, boost continuity).

### Transform clearance — UWorld::FindSpot order (RE §C6, CONFIRMED native)
- Robot-extent overlap test against world geometry.
- Depenetration candidates: Z, then X, then Y at 1.0 extent, then 0.5; then the ±X±Y±Z diagonals at 0.5. The first
  clear spot wins; refuse only when none is clear.
- A vertical push never lands on a surface above the start.
- VEHTEST clearance: refuse / fit / displace pass.

### Weapons — selection → inventory → mesh → firing → kill feed are one weapon
- **Data:** versus uses **MultiplayerData** (TnMultiplayerGame.DesiredWeaponDataType = 3, CONFIRMED RE). It falls back
  to PlayerData when a weapon has none (TnWeapon, CONFIRMED). Generated `WeaponTable.inc` (52 weapons) from
  authored.db + mp_weapons.json + cooked weapon sockets.
  - mp_weapons.json's "player WEPDATA" block is the SP set; reported to AssetTools.
- **Inventory** (TnCharacterApplier.ApplyWeapons / CreateWeapons): CharacterData.WeaponTypes in order, first active.
  - Iconic → the chassis preset.
  - Custom → the selection's list, validated against the chassis' TnDataProvider_Weapon restrictions. A disallowed
    pick is refused and reported (`loadoutRefused`), never replaced.
  - VehicleWeapons are kept and reported.
- **Swap Weapons** (PgUp / PgDn): PutDownTime + EquipTime from WEPDATA; no firing in between.
  - **Instant-hit weapons simulate:** damage, interval, NumShotsToFire pellets, falloff, spread, reload.
  - **Projectile, melee and grenade weapons** are equipped and shown but flagged `weaponSimulated = false` [PARTIAL].
- **Presentation:** the active weapon's own mesh and AnimSet at the chassis socket, with its MuzzleFlash socket.
  - Ion Blaster particle FX are drawn only for the Ion Blaster; other weapons expose their FX template names
    [PARTIAL, Rendering].
  - Weapon sounds stay the Ion Blaster's [PARTIAL, Systems].
- **WFC_WEAPONTEST 9 / 9:**
  - the table reproduces IonBlaster_WEPDATA;
  - iconic loadouts for Truck / Car2 / Jet / Tank3 / Truck4;
  - Car2 AssaultRifle refused (ChassisRestriction Jet / Tank);
  - Shotgun 8 pellets;
  - swap timing to ShortSword (not simulated);
  - firing spends the active weapon's ammo with its damage type.
- **Visually verified:** Sideswipe holding the Neutron Assault Rifle.

### Code of Power (CTF) and Countdown to Extinction (EXT) — all six versus modes on the shared framework
- **Rounds** (TnGameRules_RoundsBase, CONFIRMED):
  - GRI.Rounds = PointsToWin for CTF (default 2), TimeLimit per round (default 300);
  - the round timer replaces the match clock (RunGameTimer false);
  - at 0 → CurrentRound++ → EndGame(none, "Score") after the last round, else BetweenRounds 5 s → RestartRound (every
    player respawns, no death counted);
  - RoundEnded / RoundStarted events.
- **SingleFlagCTF:** first attacker RandomInt(2), alternating each round.
  - SetupRoundStart: the attackers' capture point _Active; the defenders' flag factory in Pickup, the other asleep.
  - Mercy rule on the last round (the last attacker already leads → end).
  - A capture does not end the round.
- **Flag:**
  - defenders can't take it (ValidTouch);
  - capture = carrier inside the active capture point's ObjectiveVolume → ScoreObjective(1): +1 team, +10 personal
    (IndividualScore 10); the flag goes straight home;
  - carrier death → dropped flag: AutoReturnTime 30; defenders touching it drain ReturnFlagTime 10 at dt × count,
    recovering +dt with none; attackers re-take it; falling below KillZ sends it home.
- **Bomb:**
  - neutral factory; anyone takes it; GRI.AttackingTeam = holder team;
  - plant on the ENEMY TnBombPlantPoint (ObjectiveVolume) → FuseTime 15;
  - defenders inside accumulate DefuseTime 5 (reset when none) → the bomb drops at the point (DefuseBombSpawnClass);
  - detonation → ScoreObjective(planter, 1), HurtRadius DetonateDamage 9999 / DetonateRadius 5000 UU (AOE), bomb home,
    factory WaitAfterScoreTime 5;
  - PointsToWin 3, TimeLimit 900.
  - All values come from authored TnBombPlantPointBase / TnDroppedPickupFlagBase / factory defaults [CONF].
- **Teams:** flag factories, capture points and plant points carry authored DefenderTeamIndex (byte default 0).
  Clusters filter TNGT_CTF / TNGT_EXT.
- **Touch shapes:** factory CylinderComponent 200 / 100 UU; dropped pickup TouchCylinder = CylinderComponent default
  22 UU [HIGH]; pawn cylinder 2 m [PROV for non-Optimus chassis].
- **PARTIAL:**
  - the carrier's TnWeaponFlag1Hand / TnWeaponBomb weapon swap (the carrier keeps its weapon);
  - carrying in vehicle form is allowed [UNKNOWN];
  - flag / bomb messages are logged (switch numbers), not presented;
  - XP events are not implemented.
- **WFC_CTFTEST 10 / 10:**
  - round-1 activation;
  - defender refused, attacker capture +1 / +10, flag home;
  - drop + defender return in 10 s;
  - round timer → 5 s break → attackers swap → match end by Score;
  - EXT: holder sets the attacking team; plant / defuse 5 s / drop at the point;
  - detonation +1 team, +10 (+1 blast kill) personal, blast kills within 50 m, factory sleeps.
- **Assists** now divide by the victim's HealthMax (its class blueprint), not 550.

### Camera settings (RE TARGETED_PASS3 §G2)
- `PlayerController::setLookSettings(CameraSensitivity 0–100, InvertY_Robot, InvertY_Car, InvertY_Plane, InvertY_Tank)`.
- Orbit speed follows Lerp(0.03, 0.20, s/100) relative to the default 30 [CONF curve; absolute mouse rate PROV].
- Invert per form: car and truck share InvertY_Car.
- Frontend calls it on Settings commit and at match start.

### Vehicle cameras per chassis — CONFIRMED authored
- Each chassis' HmCameraStrategySet (roster cameras) supplies hover / driving / flying strategy values: anchor, orbit
  distance, pitch range and FOV (CameraTable.inc, tools/gameplay/gen_camera_table.js).
  - Truck 1.85 / 9.5 m (drive 2.15 / 10.5);
  - car 1.25 / 5.25 m;
  - tank 1.75 / 8.25 m, pitch −10..25°;
  - jet hover 1.5 / 9 m ±45°, flying ±80° FOV 100.
- The truck row reproduces the existing constants exactly.
- CAMSYNC at 144 Hz: Sideswipe and Starscream show no separation (robot 0.0002°).

### Killstreaks — framework CONFIRMED script; 4 of 12 effects implemented
- **Counting:** PRI._CurrentKillStreak += kills (AddKills); death resets it (AddDeaths → KillStreakEnded).
- **Earning:** UpdateKillstreakRewards(count) → FindKillstreak(Specialty, count) → AcquireKillstreak (stack, no duplicates).
  Each class has three streaks at 3 / 5 / 7 kills (TnDataProvider_Killstreak).
- **Triggering:** B (TriggerKillstreak) fires the newest; RequiresRobotForm streaks in vehicle form transform first and
  then trigger (Deferred). ClientGameEnded clears the stack.
- **Implemented:**
  - Overshield Matrix: team OverShieldPickup heal;
  - Ammo Matrix: team FillReserveAmmo + TnBuffLockAmmoClip 10 s;
  - Energon Recharger: regen × FloatModifier 2 for 30 s;
  - Intercooler: ability cooldown × 5 for 30 s.
  - Buff values come from the authored TnBuff* defaults [CONF]; the exact buff hooks are HIGH.
- **PARTIAL** (acquired and triggered, effect reported unimplemented): Orbital Beacon 1 / 2, P.O.K.E. 2.0, Thermo Mine
  Re-Spawner, Health Matrix 2.0, Nucleon Shock Cannon, Electromagnetic Pulse, Omega Missile.
- **Test:** WFC_PARTICIPANTTEST 5 / 5. Custom Soldier: 3 kills → Ammo Matrix; B refills and locks the clip (32 stays 32);
  death resets the streak.
- **HUD:** killStreak, killstreaks[] (newest last), killstreakImplemented, regenBuff, fastCooldownBuff, ammoLockBuff.

### Projectiles and vehicle weapons
- Projectile weapons fire their WeaponProjectiles[0] class with its MultiplayerData TnProjectileData: InitialSpeed, Damage,
  DamageRadius, DamageType [CONF authored]. Examples: TankShell 20000 / 170 / 2500; RocketVh 18000 / 55 / 1500.
- Flight is straight. Homing lock-on (TnProjectileDataHoming HomingForce / ClosingForce) is PARTIAL: fired straight.
- On any hit: HurtRadius with stock UE3 linear falloff [HIGH]. Teammates are filtered; the instigator is never hit by its
  own shot in flight.
- **Damage taken** is scaled by the victim form's DamageMultiplier (ROBODEF 1.0; VEHDEF e.g. 0.75 tank, 0.8 jet, 0.9 car)
  and, for self damage, by SelfDamageMultiplier 0.45 [CONF data; HIGH placement in TakeDamage].
- **Vehicle form fires** its CharacterData.VehicleWeapons[0] (projectile or hitscan). Origin = the chassis' vehicle
  WeaponSocket_Primary (bone × socket, CONF); aim = the camera aim point. The tank cannon turret rotation
  (UpdateCannonRotation) is not animated [PARTIAL].
- Repair rays are flagged unsimulated (they heal) [PARTIAL]. Projectile meshes and trails are drawn as a small box
  marker until Rendering draws them [PROV presentation].
- WFC_WEAPONTEST 12 / 12: Warpath's TankCannon shell flies and hits for 131; self damage 49.8 ≤ 170 × 0.45 × 0.75.

### Non-local participant pawns (bot-ready architecture, RECONSTRUCTION EXTENSION boundary)
- Participants (MatchOpponent) are full pawns: the same chassis, specialty, loadout, movement, transformation, damage,
  death / respawn and objective paths as the local pawn. Their inputs come only from `setIntent` (harnesses); no AI.
- WFC_PARTICIPANTTEST 4 / 4. TDM assists now use the victim's class HealthMax.

### HUD objective markers for every mode
- HudGameState.objectives now lists every active-in-mode objective: DOM nodes, KOTH zones, CTF flag factories (active at
  home) and capture points (active for the attackers), the EXT bomb factory and plant points, each with its team.
  Tags carry `label` (false when cloaked).

### HUD state additions
`selectedChassis`, `drawnChassis`, `specialty`, `spawnError`, `weaponId`, `weaponIcon`, `weaponSimulated`,
`weaponSwitching`, `inventory[]`, `activeWeapon`, `vehicleWeapons[]`, `loadoutRefused[]`. `segmentCount` follows the class.

### Multi-map
- World loads any processed MP map (`WFC_MAP`, or the launch URL's map). Everything is read through the shared contract:
  world / collision / gameplay / physics / navigation.
- KillZ comes from the persistent level's WorldInfo: Streets −750 m, Gorge −75 m, Rust −5.1 m, Debris +100 m.
- Streets' rotating domes are restricted to Streets.
- Gorge, Rust and Debris load with their own starts, pickups, objectives and destructibles.
- **Hazard volumes** (AssetTools maps/<Map>/hazard_volumes.json): convex PhysicsVolume brushes with DamagePerSec / DamageType.
  Stock UE3 pain applies: DamagePerSec × PainInterval on entry and every PainInterval (1 s default) [HIGH].
  TnDamageTypeInstantKillAi forces Died only for AI pawns (TnAiPawn.TakeDamage); for players it is 2000 damage, which is
  lethal [CONF].
- Harnesses take the map's paths and KillZ. `WFC_MAPSUITE` runs per map: TDM launch, 12 respawns on floor and clear,
  KillZ death, hazard damage, pickups, second match.

### Stress per chassis (Streets, fixed 60 Hz)
| chassis | WFC_CHAOS | WFC_XFORMTEST |
|---|---|---|
| Truck (Optimus) | 60 starts: 0 under map, 0 KillZ, 0 stuck | 0 / 1520 under map |
| Car2 (Sideswipe) | 30: 1 under, 0 KillZ, 1 stuck (before the hull / ChassisOffset fix) | 0 / 760 |
| Jet (Starscream) | 30: 0 / 0 / 0 | 0 / 760 (1 forced back to vehicle) |
| Tank3 (Warpath) | 30: 0 under, 0 KillZ, 1 stuck | 0 / 760 (1 refused, 7 forced back) |
| Truck4 (Soundwave) | 30: 6 under + 1 KillZ before the fixes → **1 under (4 frames, vehicle under a deck its 1.42 m hull top clears), 0 KillZ** after | 0 / 760 |

- Weapons and loadouts: WFC_WEAPONTEST 10 / 10.
- TDM 43 / 43, modes 21 / 21, chassis 13 / 13.
- CAMSYNC 60 / 144 / 240 Hz unchanged (robot 0.0003°).

---

## PASS 21e — TRANSFORM CLEARANCE, ROSTER CONTRACT, HUD STATE COMPLETION, REGRESSION GUARDS (2026-10-04, gameplay agent)
Inputs:
- RE OVERNIGHT_2026-10-04 §A2, A5, B3, E, F;
- Rendering M08 (HUD ownership: Hud_GFX presents, Gameplay supplies state).

Handoffs:
- `docs/handoffs/GAMEPLAY_FRONTEND_HUD_CONTRACT.md`: HUD state / events, character selection, settings and input;
- `docs/handoffs/GAMEPLAY_BOT_READINESS.md`.

### Transform clearance — CONFIRMED ORIGINAL behaviour, PROVISIONAL geometry test
- **Before vehicle -> robot**, `PlayerController::tryBeginTransform` (TnPawn.Transform -> MoveToSafeTransformationLocation
  -> FindSpotAwayFromPawns with the target extent):
  - the robot cylinder must fit at the floor under the vehicle, or at one of 16 nearby spots (1 m / 2 m rings, floor
    needed, no wall in between);
  - **no spot -> refused**: `cantTransformCount` pulse = HUD NotifyCantTransform (`mc_cantTransform`) +
    TransformFailedSound (`BL_TRANS_POWER.TRANSFORM_DISABLED`, TnPlayerController default, CONF);
  - **a displaced spot** moves the collision at once; the meshes slide back over 0.5 s (OffsetMeshes).
- **After the fold**, InRobotForm.BeginState: MoveToSafeLocation; still stuck -> **ForceIntoForm(vehicle)** [CONF].
- **Fit test** [PROV; whether the native search tests world geometry or only pawns is PARTIAL in RE]: five vertical
  columns (the centre from above MaxStepHeight, four at 0.7 r from 1.8 m) clear up to 4 m. An earlier variant that tested
  the offset columns from 0.4 m refused 69 / 1520 stress transforms on ordinary slopes and stairs, so it was rejected.
- **Validation:**
  - VEHTEST clearance: refused deep under a 3 m ceiling, fits on open floor, displaced to z 3.0 when 0.5 m inside the
    ceiling edge;
  - WFC_XFORMTEST 0 / 1520 under the map, 0 refused, 2 forced back to vehicle;
  - **OPEN:** the 16 "low overhang" cases happen after the fold, while the stress test keeps driving the robot forward
    under props. That is robot walking, not the transform.

### Character roster contract (`src/game/CharacterRoster.h`) — CONF data
- `Match::selectCharacter` stores a `CharacterSelection`:
  - custom or iconic;
  - one of four specialties;
  - a stable chassis UniqueId.
- At spawn, `resolveChassis(selection, team faction)` picks the body:
  - custom -> the specialty default per faction (Ironhide / Soundwave, Air Raid / Starscream, Sideswipe / Barricade,
    Warpath / Brawl);
  - DM forces Decepticon.
- Spawning waits for `hasSelectedCharacter` (CheckReadySpawn). The local player is pre-selected as iconic Optimus
  (`Truck`) [RECONSTRUCTION DEFAULT until the frontend selection screen]. Only the Optimus pawn resources load; the other
  32 chassis need AssetTools ROBODEF / VEHDEF exports.

### HUD runtime state — completed against the HUD list
- Added:
  - `weaponName`;
  - damage direction: `damageTakenCount` pulse, instigator location `lastDamageFrom`, view-relative
    `lastDamageBearing` [PARTIAL: the Hud_GFX indicator call is not traced];
  - `cantTransformCount`.
- Kill feed rows now live **5 s + 1 s fade** (Hud_GFX, RE A2) instead of LocalMessage.Lifetime 3 s.
- FFA result text is empty (TnFreeForAllGameOverMessage, A5).
- WFC_TDMTEST 41 / 41: feed expiry at 6 s, HUD damage event and weapon identity.

### Vehicle input — verified against RE (no change needed)
- RMB / LT: fine aim in robot form only; Boost (held) in vehicle form.
- Shift: Dash while hovering, Nitro while Driving (steer × 0.3).
- Driving has Accelerator fixed at 1 (no throttle).

### Boost-continuity status (after 21d)
- Rendering repro (`WFC_VEHDROPLOG`, 840 frames): starts 1 / 4 / 7 / 18 / 21 -> 4 / 2 / 1 / 0 / 35–38 drops. Counts
  vary slightly run to run (real frame time).
- **Every** drop is a near-vertical face (|n.y| ≤ 0.4).
- Start 21 is the truck held against a wall below 2 m/s, re-boosting into it after each 0.5 s drift. Whether the original's
  RB contact report re-fires at that speed is **UNKNOWN** (PARTIAL). It was not tuned.
- Floor seams and steps up to 0.3 m: 0 drops (VEHTEST guard).

### Modes (six recovered)
| Mode | State |
|---|---|
| TDM | complete loop, CONF rules (WFC_TDMTEST 41 / 41) |
| DM | FFA rules (Decepticon bodies, draw on tie, empty result text) |
| Conquest (DOM) | CONF bytecode rules (WFC_MODEPLAYTEST) |
| Power Struggle (KOTH) | CONF bytecode rules (WFC_MODEPLAYTEST 21 / 21) |
| Code of Power (CTF) | PARTIAL: map state only; needs the carried-objective weapon system (flag return 30 s / 10 s defender drain, rounds) |
| Countdown to Extinction | PARTIAL: map state only; same dependency (fuse 15 s, defuse 5 s, dropped bomb 30 s) |

### Regression guards (protecting the human-reported bugs)
| Bug | Guard |
|---|---|
| High-refresh pawn / camera separation | WFC_CAMSYNC 60 / 144 / 240 Hz (robot 0.0003° at all rates; the M05 bug gave 1.28°) |
| Transform under the map | WFC_XFORMTEST 0 / 1520; WFC_CHAOS 0 / 60 |
| Single-frame boost drop | VEHTEST boost continuity, 0 drops on steps ≤ 0.3 m; `WFC_VEHDROPLOG` repro |
| Ramp snagging | VEHTEST ramp sweep (hover and boost climb 20–50°) |
| Transform clearance | VEHTEST clearance (refuse / fit / displace) |

Wide Streets traversal: oracle 852 / 852, robot tour 100 / 122, vehicle tour 98 / 122, 0 falls; sweep 984 runs, 0
KillZ; coherence 0 missing collision.

---

## PASS 21d — BOOST-STATE FLICKER, VEHICLE CONTACT, HIGH-REFRESH GUARD (2026-10-04, gameplay agent)
Inputs:
- Rendering M08 handoffs `GAMEPLAY_BOOST_FX_FLICKER.md` and `GAMEPLAY_CAMERA_FRAME_PACING.md`;
- RE MILESTONE05_PLAYTEST_RE §1–2 and the OVERNIGHT_2026-10-04 note §B.

### Boost exhaust open / close (human-reported; diagnosed by Rendering) — FIXED at the source
- **Cause (mine, Pass 20).**
  - The frontal-collision drop (Driving.OnRigidBodyCollision: contact normal · forward > 0.866 -> Hovering, CONF) was
    judged by the travel direction, not the contact normal.
  - Any block of the low hull probe while moving forward (including grazing contacts) ended boost. That was followed by
    the 0.5 s drift and a re-boost, so the BoostFx restarted about every 0.6 s.
  - Rendering logged 15–31 drops per 14 s on open floor at M05.
- **Fix.** The drop needs the blocking face's normal (into the obstacle) within 30° of forward. The exhaust FX stays bound
  to the real Driving state; nothing is smoothed.
- **Re-run of Rendering's repro** (starts 1 / 4 / 7 / 18 / 21, boost held 14 s, `WFC_VEHDROPLOG=1`): 2 / 1 / 3 / 0 / 22
  drops.
  - **Every** remaining drop is a near-vertical face (|n.y| ≤ 0.34) of a real obstacle: crates and batteries near
    (139, −622); a wall at start 21, where the truck sits pressed against it at 0 m/s and re-boosts after each drift.
  - These are authentic frontal impacts.
- **Regression guard (VEHTEST).** Boost across 0.05 / 0.1 / 0.2 / 0.3 m steps: **0 drops**. A 0.5 m riser reaches the
  0.45 m hull probe, so it is a frontal hit (RE §2.2: boost lips contact the hull, C).

### Vehicle contact (ramps / angle changes)
- **Hover** already matches RE §2.1 (CONFIRMED ORIGINAL): 4 diagonal 250 UU rays, a normal-weighted implicit spring, the
  45° ground test, yaw-only orientation on contact, upright only when airborne or upside down, (up.Z)² strafe.
- **The stops were my Pass 20 hull probes:** faces of 45–60° were treated as walls. Now faces with |n.y| > 0.5 (under 60°)
  don't stop the hull; a rigid-body box meeting a sloped face is pushed up it, which the chassis / spring code reproduces
  [PROV]. Near-vertical faces still block.
- **Boost (Driving).** The body settles onto the support slope instead of level, so the recovered BoostScale (fades to 0
  between forward.Z 0.5 and 0.866, CONF) sees climbs, and gravity acts along the slope [PROV]. The per-wheel
  TnWheelAssembly suspension (K 120000, D 8000, rest 30 + radius 45 UU, CONF) is **not** modelled: the wheel mount
  heights relative to the mesh origin are unknown (RE / AT request).
- **VEHTEST ramp sweep** (3 m ramps; hover 15 m/s, boost 25 m/s):
  - hover 20 / 35 / 50° climb, 65° stops;
  - boost 20–50° climb with the body pitching 19–31°; 65° is a frontal hit -> Hovering.
  - **OPEN:** hover at 35° launches about 6 m and tilts 69° (the same before this pass). It comes from the recovered spring
    response to fast compression on a steep face. I did not tune it; the RB hull contact the rebuild approximates is the
    likely difference.
- **Regressions.**
  - WFC_XFORMTEST 0 / 1520 under the map; the overhang cases are unchanged (15).
  - Map oracle 852 / 852; vehicle tour 98 / 122 (was 97).
  - WFC_CHAOS: 0 under the map, 0 KillZ, 0 stuck, 4 prop entries.
  - VEHTEST hover / steering / nitro unchanged.

### High-refresh pawn / camera separation — regression guard
- The fix is PASS 21a (camera per render frame); Rendering's independent diagnosis and patch agree.
- **WFC_CAMSYNC** (on-screen character offset jitter per frame):

| scenario | 60 Hz | 144 Hz | 240 Hz |
|---|---|---|---|
| robot run + turn | 0.0003° | 0.0003° | 0.0002° |
| hover drive + turn | 0.055° | 0.011° | 0.004° |
| boost | 0.106° | 0.023° | 0.009° |

- The per-tick cache (M05) gave 1.28° at 144 Hz.
- The vehicle residual falls with the refresh rate: it is the truck's own motion, not pacing.

---

## PASS 21c — CONQUEST (DOM) AND POWER STRUGGLE (KOTH) ON THE SHARED MATCH FRAMEWORK (2026-10-04, gameplay agent)
Sources:
- RE MILESTONE05_PLAYTEST_RE (28debca) §3;
- RE's decompiled TnDominationPointBase, TnKingOfTheHillZoneBase, TnGameRules_ScoreKills / ScoreObjectives /
  ScoreKingOfTheHill / ScoreDomination / ReportGameProgressBase / Points, TnGameObjective;
- authored defaults (authored.db).

Test: `WFC_MODEPLAYTEST` **21 / 21** (DOM + KOTH + TDM afterwards); `WFC_TDMTEST` 39 / 39.

### Shared framework (one Match, not one engine per mode)
- **Launch.** `World::launchMatch` accepts TDM, DM, DOM and KOTH. CTF and EXT are refused because their rules are not
  implemented.
- **Per-mode settings** (`MatchSettings::forMode`, CONFIRMED authored):
  - DOM / KOTH: PointsToWin 400, TimeLimit 900;
  - `TeamScoreAmount` 0: ScoreKillsMP authors none, so in DOM / KOTH a kill is +1 personal and 0 team
    (`AddScore(1, TeamScoreAmount)`, CONFIRMED bytecode);
  - ReportGameProgressPoints instead of Kills.
- **Spawn clusters.** `ActiveGameTypes` filter (TNGT): the 6 TDM-only clusters don't register in DOM / KOTH.
- **Objective spawn modifiers.** Active KOTH zone All −50 / d within 5000; owned DOM node Friend +1 / d [CONF authored].
- **Objective scoring entry points.**
  - `Match::scoreObjective` (ScoreObjectives: `AddScore(IndividualScore, Score)`, then ReportGameProgressPoints at
    50 / 25 left).
  - `scoreTeamObjective` (DOM) [HIGH: the TnTeamGame override is not in the decompiled set; RE §3 states +1 team / 3 s].
  - `addPersonalScore` (DOM capture).
  - Each reaches the same score-limit end.
- **Objective membership.** The pawn inside the objective's authored ObjectiveVolume brush (physics.json TriggerVolume
  planes). TnGameObjective.PostBeginPlay -> `ObjectiveVolume.SetAssociatedActor` [CONF]; the volume forwards touches
  [HIGH stock UE3]; the cylinder-vs-brush overlap is approximated by the pawn location [PROV].

### Conquest (DOM) — CONFIRMED bytecode
- **TnDominationPointBase.Tick / UpdateOccupiers / UpdateScoring:**
  - with no occupants -> capture 0;
  - attackers = living occupants not on the defending team;
  - a neutral node is claimed as if owned by the other team;
  - with no defender present, capture += dt × attackers (restarted when the claiming team changes);
  - at **CaptureTime 20 s** -> SetTeam, **PersonalScoreAmount +2** for each capturer, timers reset,
    TnDominationMessage (switch + 10 × PointNumber);
  - an owned node -> **+1 team every 3 s** (ScoreInterval 3, ScoreAmount 1).
- **Verified:**
  - a solo capture is pending at 19.5 s and done at 20 s, +2 personal;
  - +1 team after 3 s;
  - a defender present holds progress;
  - an enemy alone recaptures in 20 s;
  - two attackers capture in 10 s;
  - the score-limit end;
  - 3 totems visible (DOM state from Pass 19).
- **PARTIAL.** No points announcements for DOM (it scores through ScoreTeamObjective, not ScoreObjective). The capturing
  announcement throttle (15 s) is not emitted.

### Power Struggle (KOTH) — CONFIRMED bytecode
- **Zones.** Inactive until **MatchStarting**, which picks the initial zone.
- **Active.Tick:**
  - UpdateClaim: one team, contested 254, or none 255;
  - every ScoreInterval 1 s, if uncontested and owned, each living pawn in the zone -> Game.ScoreObjective(PRI, 1),
    i.e. **+1 personal (IndividualScore 1) and +1 team**;
  - ActiveTimeLeft 60 s -> ActivateNewZone (unvisited cycle).
- **CheckEndGame.** Every zone deactivates at the end.
- **Verified:**
  - no zone before the start, one after it;
  - +5 / +5 over 5 s alone;
  - contested gives no score;
  - rotation to another zone after 60 s;
  - the end at the limit with zones deactivated.
- **PARTIAL.** The KOTH hill dialog / message switches are logged, not presented (Systems / Rendering).

### HUD
- `HudGameState::objectives`: marker type ("Domination" / "KingOfTheHill"), NodeID, owner (255 / 254), active, capture
  progress (NormalizedCaptureTime), BeingCaptured, KOTH time left, position.
- Match events add `PointsLeftAnnouncement` (switches 4 / 3).

### Not implemented (evidence present, mechanics missing in the rebuild)
- **CTF (Code of Power).** Rounds (TnGameRules_RoundsBase), the flag as a carried weapon, capture-point activation per
  attacking team, the mercy rule.
- **EXT (Countdown to Extinction).** The bomb as a carried weapon, plant / 15 s fuse / 5 s defuse, HurtRadius 9999.
- Both need a carried-objective weapon / inventory system first. Their map state (Pass 19) and the RE specs
  (RE PLAYTEST §3, GAMEPLAY_UNKNOWNS §4) are ready.

---

## PASS 21b — MATCH HUD STATE, KILL FEED, MATCH END, REGEN (2026-10-04, gameplay agent)
RE: MILESTONE05_PLAYTEST_RE (28debca) §3 / §9; TnDeathMessage decompile; authored LocalMessage / damage types.
Test: `WFC_TDMTEST` **39 / 39**.

### Kill feed (Gameplay owns the events; presentation owns text and colour)
- **CONFIRMED (TnDeathMessage.GetColoredString).**
  - Switch 1 -> `DamageType.SuicideMessage(victim)`, otherwise `DamageType.DeathMessage(killer, victim)`.
  - `\`k` / `\`o` take the killer / victim names, each coloured friendly / enemy for the viewer
    (TnMessageHelpers.GetColorForPRI).
- **HIGH (stock GameInfo.BroadcastDeathMessage).** Switch 1 when the killer is none or the victim itself.
- **Lifetime.** Engine.LocalMessage.Lifetime is 3.0 s; TnDeathMessage authors no override.
- **`KillFeedEntry`.** time, messageSwitch, killer / victim player, both teams, DamageType class:
  - `TransGame.TnDamageTypeIonBlaster` for Ion Blaster kills;
  - `Engine.DmgType_Suicided` for suicides;
  - `Engine.DmgType_Fell` for KillZ [HIGH: stock WorldInfo.KillZDamageType].
- `Match::killFeed()` returns the live entries, `killHistory()` the whole match; `HudGameState::killFeed`.

### HUD / match state (`World::hudState()`, no drawing)
- Added:
  - `spectating` (dead >= MinRespawnDelay 3.0 s, CONF RE E7);
  - `timeLimit`, `faction` (DM resolves every player to the Decepticon faction, CONF RE §3 / §7);
  - `endReason` ("Score" / "" / "Forfeit"), FFA `winnerPlayer` (an equal top score is a draw, CONF);
  - `matchOverTimeLeft` (15 s);
  - the kill feed;
  - scoreboard rows (name, team, score, kills, deaths, assists, alive, local).
- Existing fields are unchanged: health / segments / ammo / clock / countdown / scores / tags / result.
- **Verified.**
  - The clock does not run in PendingMatch.
  - The clock and score are frozen in MatchOver.
  - The second match starts from zero.
- **PARTIAL.** The FFA result text ("You won" / "You lost" / "Draw"): TnFreeForAllGameOverMessage strings were not read.

### Health regeneration — CONFIRMED (RE §9)
- 20 HP/s after 2.0 s without damage, up to the top of the current segment.
- The robot blueprint's parameters apply in both forms; the truck blueprint's 12 HP/s / 7 s is authored but unread.
- Applied to every live pawn.
- Test: 400 -> nothing for 2 s -> 425 (segment top), not 550.

---

## PASS 21a — M05 "INTERLACED" CHARACTER REGRESSION + PRE-MATCH PRESENTATION (2026-10-04, gameplay agent)

### Character / vehicle "interlacing" (human-reported M05 regression) — FIXED (owner: Gameplay)
- **Cause (mine, Pass 20).**
  - The third-person camera position was moved into the fixed 60 Hz simulation step and cached (`camLoc_`), while the
    camera rotation still updates per render frame (`handleInput`).
  - The integrated build renders at about 130 fps on the playtest machine (no swap interval is set), so most frames run
    0 simulation steps. Each of those frames paired a stale camera position with a fresh rotation.
  - The view swung around the pawn every frame, so the character and the truck appeared to separate and jitter.
  - The animation, the assets and the renderer were not involved: Character / SkinnedModel are unchanged since M04
    except `respawnReset`; dynamic meshes are drawn immediately, not through the new translucency queue.
- **Fix.**
  - The camera position is again evaluated per render frame from the current rotation and pawn location, exactly as
    before Pass 20.
  - The RE obstruction model (`WFC_CAMRE`) keeps its per-step smoothing state as a camera-space offset applied with the
    current frame's rotation.
- **WFC_CAMSYNC (render N Hz against the 60 Hz simulation, turning and moving).** Jitter of the character's on-screen
  offset per frame (mean |second difference|):

| scenario | 144 Hz per-tick cache (M05) | 144 Hz per-frame (fixed) | 75 Hz M05 / fixed |
|---|---|---|---|
| robot run + turn | 1.276° (max 1.91°) | **0.0003°** | 0.972° / 0.0002° |
| hover truck + turn | 1.189° (max 1.92°) | **0.011°** | 0.923° / 0.038° |
| boost | 0.649° (max 4.14°) | **0.023°** | 0.375° / 0.080° |

- VISUALLY VERIFIED: pending a human on the integrated build. The numeric cause and fix are confirmed.
- **Integration note.** No swap interval is set anywhere, so the frame rate is uncapped. Gameplay is correct at any
  rate now; frame pacing belongs to Frontend / Rendering.

### Pre-match presentation (human-reported) — FIXED
- **Integrated M05 capture** (countdown): no Optimus, but the Ion Blaster drawn floating at the world-load DM spawn,
  with the hidden pawn frozen mid-fall.
- **Original [CONF RE bootstrap §2 / §5.2].** `ShouldSpectateOnLogin`: there is no pawn before the start, and
  PendingMatch spawns nobody.
- **HIGH (stock UE3).** GameInfo.Login creates the controller at FindPlayerStart and it spectates from there.
- **Now.**
  - `World::startLocalMatch` takes the login start from the spawn manager (team start of the initial cluster; the
    SpawnIterator is consumed like the original).
  - The controller views from it at the start's rotation (`PlayerController::setSpectatorView`).
  - The pawn is parked at rest there, and neither the pawn, the weapon nor the vehicle FX are drawn while there is no
    pawn.
  - On the spawn the view returns to the third-person camera, and Optimus appears at his team start
    (TnTeamPlayerStart_*, initial cluster 7810 / 4159).
- **PARTIAL.** The death / spectate camera (Death strategy, MinRespawnDelay 3.0 s spectating) is not reproduced: the
  camera stays at the death location.

---

## PASS 20c — ADVERSARIAL MOVEMENT HARDENING (2026-10-03, gameplay agent)
- **WFC_CHAOS** (Phase 3): from 60 nav points, 20 s each of seeded random play through the real input path
  (71,940 ticks, 355 transform presses, 312 jumps, 307 boosts). Checks:
  - UNDER THE MAP: a walkable BSP (level-shell) floor 0.3–3 m above the pawn;
  - inside / under a prop;
  - KillZ;
  - stuck (< 0.5 m in 5 s with move input).
- **Robot knee probe [PROV].** 0.55 m height (above MaxStepHeight 0.35), 0.7 m reach, walkable faces skipped.
  - It stops the robot body walking into raised BSP blocks, crates and supports 0.6–2 m tall (no probe covered 0.35–2 m).
  - The short reach leaves stairs to the centre-point ground model: a 35° stair 0.7 m ahead is about 0.49 m high.
  - The native cylinder sweep with step-up is not reproduced.

| run | under the map | KillZ | stuck | inside / under a prop |
|---|---|---|---|---|
| before the knee probe | 8 (old detector) | 0 | 1 | — |
| knee probe 0.9 m | 1 | 0 | 2 | 7 |
| **knee probe 0.55 m** | **0** | **0** | **1** | **3** |

- **Regression.**
  - Authored ReachSpec oracle 852 / 852 (robot + vehicle).
  - Visible components crossed where the hull is smaller than the mesh: 33 (was 43 at the start of Pass 20).
  - TRAVERSE 160 runs, 0 falls.
  - WFC_TDMTEST 30 / 30.
  - VEHTEST unchanged; harness 179 / 2 known / 21.
- **Remaining reported locations.**
  - Robot wedged between BlockingVolume_6729 / _7015 and the TrainTrack at (221.6, −724.8, −441.3).
  - Three prop-entry cases.
  - Transform under a low overhang: 15 / 1520 stress cases (54 before the knee probe; RE question, PASS 20a). WFC_XFORMTEST: still 0 / 1520 under the map, 0 KillZ.

### Open RE requests (narrow)
1. `MoveToSafeTransformationLocation` / `FindSpotAwayFromPawns(target extent)`: does it resolve world geometry, and
   when does `NotifyCantTransform` fire? (Transform under a 1.95–2 m overhang.)
2. The native cylinder physWalking step-up / encroachment against simple hulls, to replace the robot probes.
3. The TraceCamera exact flag word and the AABB sweep result when starting inside geometry (camera model, PASS 20a).
4. Segmented-health regeneration rules; death animation / Death camera strategy timing.

---

## PASS 20b — MP_IAC_STREETS TDM SESSION RUNTIME (2026-10-03, gameplay agent)
RE: MILESTONE05_FRONTEND_MATCH_BOOTSTRAP §3, §5, §6 and MILESTONE05_GAMEPLAY_UNKNOWNS §3, §5, §6. Test:
`WFC_TDMTEST` — **30 / 30 checks** through World (real pawn, hitscan, pickups, map state).

### Launch contract (Frontend / Integration -> Gameplay)
- `MatchLaunch::fromURL` takes the original StartLevel URL, e.g.
  `MP_IAC_Streets_Base_m?…?GameModeTag=TDM?PointsToWin=40?TimeLimit=900.00…`. Missing keys keep the TnOnlineGameSettings
  defaults.
- `World::launchMatch(MatchLaunch)`:
  - validates the map: only MP_IAC_Streets is loaded, anything else is rejected;
  - applies the mode's authored world state (`MapState::setMode`, exact HasRule gates, Kismet UnHide);
  - resets the map as a fresh level load (map clock 0, objectives and KOTH re-initialised, destructibles to state 0,
    pickups back to Pickup);
  - starts the match.
- Runtime: `WFC_MATCH_URL=<url>` or `WFC_MATCH=TDM|DM`.
- Other modes set map state only; their match rules are not implemented (by design for this pass).
- **CONFIRMED.** A new match = MatchOver -> ReturnToGameLobby -> ServerTravel, i.e. a fresh level.

### Combat foundation
- **CONFIRMED.**
  - TnPlayerPawn.TakeDamage discards teammate damage except TnDamageTypeAOE.
  - Damage reaching the pawn enters its DamageHistory, used for ScoreAssists (first other damager, damage / HealthMax).
  - Lethal damage -> Game.Killed(instigator).
  - Segmented health 550 + overshield 550; the overshield part is consumed first.
  - Ion Blaster InstantHitDamage 15 with range falloff, unchanged.
- **Death / respawn (CONFIRMED / HIGH).**
  - The dead pawn is removed from simulation and drawing until RestartPlayer.
  - RestartPlayer = a fresh TnPlayerPawnMultiplayer: robot form, any fold cancelled, default vehicle state, HealthMax, no
    overshield, Ion Blaster 50 / 150 (InitialReserveAmmoCount), at the chosen start with its authored yaw, after the 5 s
    wave delay.
  - Death does not reset pickup timers; only StartMatch Resets factories.
- **UNKNOWN / PARTIAL.**
  - Segment regeneration.
  - Death animation / ragdoll and the Death camera strategy (the pawn is simply hidden).
  - PlayerRestartDelay (no script reader).
  - The downed state (TnSkillCanBeDowned) is not modelled.

### Test participants (separated from shipped behaviour)
- `MatchOpponent`: a static synthetic participant with a Match slot, the robot cylinder and segmented health. It
  spawns / dies through Match.
- It exists only under `WFC_TDMTEST` / `WFC_MATCHTEST` or the explicit diagnostic `WFC_MATCH_OPPONENTS=N` (drawn as
  boxes). No AI.

### HUD state output (no drawing)
- `World::hudState()` -> `HudGameState`, named after the original bindings:
  - health / HealthMax / overshield / active segment;
  - clip / reserve ammo;
  - form / transforming;
  - TimeToRespawn;
  - match state / GRI game status (2 / 3 / 5), pre-match countdown, RemainingTime / ElapsedTime;
  - GoalScore, team scores, TeamID, personal score / kills / deaths / assists;
  - Winner and the result text ("Your team won" / "Your team lost" / "Tie game").
- **TDM player tags** (TnObjectiveMarkerTypeTransformerVersus): hidden for self and the dead; allies labelled; enemy
  markers disabled without the reveal buffs [CONF RE §5].
- Match events (`World::matchEvents()`): countdown, start, spawn, kill, progress announcements (switches 0–2, 5–7),
  nearly complete, end, return.

### Map / mode state
- TDM:
  - totems, KOTH zones, flag / bomb factories and objective bases are hidden (the actors remain);
  - the 24 ordinary pickups (no rule gate authored) stay active;
  - TnTeamPlayerStart for the player's team;
  - ScoreKillsTDM present.
- KOTH rotation now follows RE §3 [CONF]: 60 s; candidates are zones not yet active this cycle; the cycle resets when all
  have been active; no back-to-back repeat.

### Validation (all on the corrected Streets data)
- WFC_TDMTEST 30 / 30.
- WFC_MATCHTEST: TDM to 40, clock tie at 125 s with announcements 120 / 60 / 30, DM to 20.
- WFC_MODETEST: KOTH visited cycle.
- Harness 179 / 2 known / 21.
- VEHTEST, TRAVERSE (160 runs, 0 falls), PICKUPTEST (30.02 / 60.02 / 119.99 s) unchanged.
- Firing sim 3.1 ms.

---

## PASS 20a — BOOST->ROBOT FALL-THROUGH, VEHICLE/ROBOT WALL PROBES, MATCH CORE, CAMERA (checkpoint) (2026-10-03, gameplay agent)
RE: TARGETED_PASS 1e, MILESTONE05_GAMEPLAY_UNKNOWNS (§1, §2, §6), MILESTONE05_FRONTEND_MATCH_BOOTSTRAP §5, MILESTONE04_CAMERA_COLLISION (990f3e7).

### Boost -> robot under-map (human-reported) — FIXED
- **Cause.**
  - The robot was given its full collision cylinder (half-height 2.0 m) on frame 0 of the vehicle->robot fold, from the
    shared actor location.
  - While boosting, the truck's wheels are down and its root is at floor level. The vehicle actor is only 1.22 m above
    the floor, so the robot's feet started 0.78 m inside the floor.
  - The airborne floor search only looked 0.35 m (MaxStepHeight) above the feet, so it missed that floor and the robot
    fell through.
  - In hover (root 1.24 m up) the feet start 0.46 m above the floor, which is why normal transforms worked.
- **Fix (CONFIRMED ORIGINAL semantics).**
  - TransformingToRobot.UpdateCylinderSize lerps the half-height from the vehicle's to the robot's by
    RemainingTimeAsFactor (1 -> 0).
  - The floor is resolved against that growing cylinder's bottom, so the floor pushes the actor up as the cylinder
    grows.
  - Visuals stay on the continuous actor location (the robot mesh hangs CollisionHeight below it), and the smooth
    transform is unchanged.
  - The vehicle cylinder half-height = the mesh bounds half-height, 1.22 m [HIGH].
- **Falling sweep [HIGH: UE3 physFalling MoveActor sweep].** While falling, the floor search starts from the previous
  step's bottom, so a floor crossed within one step is landed on.
- **Stress (WFC_XFORMTEST, 20 nav points x 4 headings x 19 cases).**
  - Cases: stationary, hover, max hover, boost, boost + nitro, boosted turn, boost jump, and a transform-time sweep
    during boost.
  - Before: 656 / 1520 under a floor, 186 below KillZ.
  - After: **0 / 1520 under the map, 0 KillZ**.
  - 54 end on a real floor under a low overhang (see the next item and UNKNOWN).

### Vehicle wall probe — FIXED (found by the stress test)
- The vehicle reused the robot wall probe at 2.0 m above its root. That is above the truck hull top (1.85 m), so the
  truck drove through obstacles lower than about 2 m: train-coach BlockingVolume_11128, Small1_Box2 crates.
- **Now:** three probes across the hull height (root −0.35 .. +1.85 m). The low probe sits above the wheel or spring
  clearance and ignores walkable faces, so ramps and floors don't block [PROV approximation of the RB box contact].
- **Robot head probe (3.6 m):** the 4 m cylinder no longer walks under overhangs lower than its top (for example an
  ArchTop01 hull 1.95 m above the floor). There is still no low probe: the centre-point ground model owns steps and
  stairs.
- **Results.**
  - Authored ReachSpec oracle: still **852 / 852** (robot + vehicle).
  - Visible components crossed where the hull is smaller than the mesh: 43 -> 36.
  - VEHTEST unchanged: rest 1.287 m, 0.5 m bumps at 15 m/s, jump +3.80 m, dash 30 -> 15.

### Segmented health and pickup acceptance (CONFIRMED ORIGINAL, RE §6)
- Health = TnSegmentedHealth (TR_Health_p.SharedHealth): segments [175, 125, 125, 125], so HealthMax 550; Overshield 550.
- Health pickup only below HealthMax (SHT_AddAllSegments, full heal). Overshield only while normalized overshield < 1
  (Health = HealthMax + 550). Ammo crate only when not AmmoMaxed.
- Pickup.ValidTouch rejects a touch through a wall (FastTrace) and re-checks after 0.5 s.
- Segment regeneration: **UNKNOWN** (not read; none applied).

### Local match core (src/game/Match.*, World::startLocalMatch) — RE bootstrap §5
- Launch-independent: `World::startLocalMatch(MatchSettings)`, opt-in with `WFC_MATCH=TDM|DM`. Free play is unchanged.
- **CONFIRMED.**
  - InitGame: GoalScore = PointsToWin (TDM 40, DM 20); TimeLimit 900 s (TimeLimits[1]).
  - PendingMatch: a 10 s countdown with no spawns, then StartMatch.
  - Factories Reset() at StartMatch.
  - InProgress: a 1 s GRI timer; time announcements at 120 / 60 / 30 s (60 -> GameNearlyComplete).
  - ScoreKills: +1 to the killer and +1 to the team. No score for a suicide (DmgType_Suicided or self) or an environmental
    death; deaths always count.
  - TrackKillsMP kills; ScoreAssists = first other damager, damage / HealthMax.
  - ReportGameProgressKills at 5 / 3 / 1 left.
  - CheckScore -> EndGame("Score"); the clock -> EndGame(""); a tie means no winner, with no overtime.
  - MatchOver for 15 s, ScoreKill a no-op, then ReturnToGameLobby (host handoff).
  - TnRespawnHelperWave: 5 s respawn, initial spawn immediate.
  - PickTeam: the smaller team; a tie is random.
- **Spawning (CONFIRMED).**
  - Initial clusters 4159 (Decepticon) and 7810 (Autobot); round-robin, up to 4 picks.
  - Initial lock 15 s, then re-scoring every 0.1 s; switch on a delta of 5 or 10 s uptime.
  - Modifiers: friend +1/d, enemy −3/d, tombstone −5/d within 5000 UU.
  - The cluster faction is the team of its first spawn point; authored _CenterPoint.
- **PARTIAL.** IsSafeSpawnLocation (native) is approximated as no live player within 4 m. The DM FFA start choice is the
  first safe FFA start. Objective / KOTH / DOM spawn modifiers are not registered in TDM. Tombstone lifetime.
- Tests: `WFC_MATCHTEST` (TDM to 40, TDM clock tie, DM to 20, local pawn spawn / suicide / 5 s respawn / return).

### Camera obstruction (RE 990f3e7) — implemented, opt-in
- TnThirdPersoncollision (robot: rise-then-horizontal fallback, 1 s smoothing window, 2000 / 500 UU/s) and
  TnAvoidClipping (vehicle: origin offset (−200, 0, 75), two ranked candidates, always smoothed).
  - The ray uses the zero-extent world, the box tests use the non-zero-extent world; movers are ignored.
  - The near-plane box sweeps are **approximated by rays [PARTIAL]**.
- **WFC_CAMTEST (15 nav points x 8 headings x 5 scenarios; % of frames).**

| model | camera behind visible geometry (robot / hover backing) | camera under a floor | near-plane contact | one-frame pops (robot) |
|---|---|---|---|---|
| default (provisional pull-in) | 9.6 / 11.0 % | 0.2–1.2 % | 0.8–2.5 % | 22 |
| RE algorithm | 16.8 / 24.5 % | 1.9–3.0 % | 2–6 % | 0 |

- The default stays the provisional pull-in, which the human likes. The RE model runs with `WFC_CAMRE=1` until the box
  sweep is exact.

### UNKNOWN / RE requests
- **Transform under a low overhang (54 stress cases).** The truck (hull top 1.85 m) fits under a 1.95–2.0 m slab, and the
  4 m robot then stands under it. RE §1 names `MoveToSafeTransformationLocation` -> `FindSpotAwayFromPawns(target
  extent)`. Whether that resolves world geometry, or the original refuses the transform (HUD `NotifyCantTransform`
  exists), needs a narrow RE trace.

---

## PASS 19 — MP_IAC_STREETS WORLD STATE + CORRECTED WORLD/COLLISION (AssetTools 8d8195e) (2026-10-03, gameplay agent)
Data: AssetTools 8d8195e. StaticMeshCollectionActor transforms were corrected (S·R·T × CachedParentToWorld; 1,906/1,906
validated, 0 stale collision matrices). world.glb, collision_pawn.glb, collision_weapon.glb, physics.json, props.json, map.json
and render_index.json were regenerated together.

Gameplay reads them at every start (collision is built from collision_pawn.glb and collision_weapon.glb at load; there is no
cache). Every measurement below ran on the corrected files. RE: MILESTONE04_STREETS_RUNTIME_SEMANTICS (0ab03b2) and
MILESTONE04_STREETS_PICKUP_OBJECTIVE_PRESENTATION (00dcb20). Tests: `WFC_MAPTRAVERSE=1`, `WFC_MODETEST=1`, `WFC_TRAVERSE=1`.

### Match mode / rule set (CONFIRMED ORIGINAL)
- A mode is its authored `TnOnlineGameSettings<tag>.Rules` list (authored.db). For example:
  - DM = ScoreKillsDM, TrackKillsMP, ReportGameProgressTime/Kills;
  - CTF adds SingleFlagCTF, ScoreFlags, ReportGameProgressTimeCTF;
  - EXT adds ScoreBombingRun;
  - DOM adds ScoreDomination + ReportGameProgressPoints;
  - KOTH adds ScoreKingOfTheHill.
- Every world-state gate is an exact rule-class match (`MapState::hasRule`). This covers the Kismet SeqCond_GameRuleActive UnHide of the 4 objective bases, the factories, capture/plant points, totems and KOTH zones. There is no combined or fake mode.
- `World::setMatchMode` is applied before load. Application takes it from `WFC_GAMEMODE` until a front end exists; DM by default.

### Presentation sync (Gameplay → renderer, every frame)
- `setActiveGameRules` (the authored class paths), `setMapClock` (MapState clock), `setActorHidden` and `setMapEffectState`:
  - **setActorHidden** covers the 4 bases, 3 totems, 5 KOTH zones and 3 objective factories.
  - **setMapEffectState** covers the 24 pickup factories (custom effect / highlight beam) and the flag/bomb beam (Pickup = on; Disabled = hidden).
- The three map-state hooks mirror the Rendering lane's interface verbatim.
- `setMapClock` is new. Rendering currently animates movers from its own wall clock, so the drawn domes can drift from the moving collision Gameplay simulates.
  - **HANDOFF:** evaluate movers, totems and KOTH at `setMapClock` time.

### Movers
- Domes: PHYS_Rotating at 2730 UU/s, unchanged (CONFIRMED).
- **SkyBeam (corrected):**
  - The track has `bUseQuatInterpolation`, so the rotation is SlerpQuat between the bracketing Euler keys with a linear alpha (UE3 GetKeyTransformAtTime). Pass 17 interpolated Euler angles on CurveAuto tangents.
  - IMF_RelativeToInitial uses InitialTM = authored Rotation only. Pass 17 conjugated by the scaled authored matrix, which distorted the delta for the non-uniform DrawScale3D.
  - Check: InitialRot reproduces the world.glb placement of all three actors (residual 0.00000).
  - StaticInterpActor_5249: 0° → 11.35° (2.25 s) → 22.70° (4.5 s) → 0° (9 s).
- Moving collision: domes 15810/7381/8114 and SkyBeam dome 5249 (the cones and bases carry none, as authored).

### Objectives
Same table as PASS 18, now rule-gated, plus:
- **KOTH:** the Active zone rotates after the authored `ZoneActiveTime` 60 s (TnKingOfTheHillZoneBase CDO) [HIGH: authored constant and the ActiveTimeLeft field; the timer body is not traced].
- **KOTH zone visual:** `ActiveMesh` (FX_Mesh_p pTorus1_STAT) is shown only while the zone is Active. Gameplay pushes hidden state; drawing the torus is a Rendering handoff.
- **Objective cylinders** (totem, zones, factories) have no authored blocking flags, so no blockers are added. Hidden totems keep a non-blocking cylinder.

### Player start
- Start class follows the mode: TnFreeForAllGame (DM) → TnFreeForAllPlayerStart; TnVersusGame modes → TnTeamPlayerStart [HIGH].
- The spawn yaw is the start's authored Rotation. The invented face-the-centroid heuristic is removed.
- Which start TnSpawnPointManager picks (cluster scoring, InitialSpawns) is UNKNOWN: index 0 [PROV].

### Collision sources audited
- **TnForcedDirVolume ×4:** PhysicsVolume subclass whose CDO has bBlockActors and COLLIDE_BlockNonZeroExtent.
  - Streets instances: bBlockPawns, ArrowDirection (0,0,−1), ExitSpeed 1500, at UE Z −64000 (about 80 m above the floor).
  - These are sky caps, correctly included as pawn blockers. The push script is not traced.
- **Objective bases:** absent from both collision GLBs (authored CollideActors false) — correct.
- **"No pawn collision" props:** 260 visible props have pawn collision "none" because they author BlockNonZeroExtent = false (pawns pass, weapons blocked). Examples: 45 GS_SupportB columns, Building_ONE_Middle ×7, OmegaWall bases, the wrecked tank. This is authored; BlockingVolumes are the pawn blockers there.
- **No test geometry in normal play:** DamageTarget only with WFC_TESTDUMMY. Debug boxes appear only with B / WFC_DEBUGCAM or the no-assets graybox fallback.

### Traversal against authored data (WFC_MAPTRAVERSE, corrected data)
| check | result |
|---|---|
| Nav points (123) on floor, inside bounds, above KillZ | 123/123 |
| **ORACLE:** all 426 authored TnReachSpecs (R_WALK, path sizes 250–1210 UU), walked as robot + driven as hover truck | **852/852 arrived, 0 falls, 0 floor gaps** (0.5 m samples) |
| TOUR through all 123 nav points (coverage, not an oracle; y −727 … −699 m, ~1.7–1.9 km per form) | robot 100/122, vehicle 103/122 legs; every blocked leg stops at visible geometry or an authored volume and is logged with actor names |
| Boost + jump sweep, robot run + jump, 123 points × 4 headings | 984 runs, 0 below KillZ, 0 outside the collision bounds |
| Transform robot → vehicle → robot after settling (984) | 960 normal; 24 height changes > 0.6 m (see KNOWN DIFFERENCES) |
| Visible components crossed by the pawn | 49 authored no-pawn-collision; 49 crossings outside the component's authored hull (the visual mesh extends past the simple hull); **0 missing / displaced collision** |
| Point-in-convex-hull re-check of the crossings (collision_pawn.glb triangles, per convex piece) | all outside their hull except one hover-truck edge graze (13 cm, Small1_Box2 StaticMeshCollectionActor_12707 at (137.9, −713.2, −630.3)) |
| BSP | render and collision triangles identical (2460); one ramp face at (134.0, −717.7, −426.7) crossed by the chest segment 3× (movement edge case on a 27° ramp, not data) |

### Visual vs physical disagreement (authored, reported)
- **Interior room walls** (Wall_Base_Straight / Wall_Top_Straight / Corner2, ENV_IAC_Interior_1_p):
  - The authored collision box matches the render bounds except a strip about 0.7 m deep on one face.
  - The robot's chest reaches into that lip, and the 3rd-person camera (0.3 m in front of collision) can sit inside it.
  - Same matrix for render and collision (8d8195e validation): authored BodySetup, not an export defect.
- **Large props whose simple hull is smaller than the mesh** (crossing distance outside the hull):
  - bld_2048x4096x4096_thru 19.6 m;
  - Wall_Base_Corner2 11.0 m;
  - PROP_IAC_SideSupp01 6.4 m;
  - TrainCoach_Open 2.8 m;
  - craterDebris 2.2 m;
  - SpireBase / PillarBuilding / GiantPillar 0.1–1.4 m.
- **Camera** (UNKNOWN original camera trace extent): over 308k frames, the camera segment crossed visible geometry on 72 components authored BlockCameras and 51 authored camera-transparent ones. The camera traces the pawn collision world [PROV].

---

## PASS 18 — BOOST STEERING + STREETS MODE STATE (RE a1666c2 / 0ab03b2) (2026-10-03, gameplay agent)
Sources: `RE-Workspace/notes/MILESTONE03_VEHICLE_BOOST_STEERING.md` (RE a1666c2) and
`MILESTONE04_STREETS_RUNTIME_SEMANTICS.md` (RE 0ab03b2). Measured with `WFC_VEHTEST=1` and `WFC_MODETEST=1`.

### Boost steering — recovered control logic (CONFIRMED ORIGINAL)
- **Steering source:** TnPlayerInput.GetNormalizedTurn = aTurn (XboxTypeS_RightX).
  - HmPlayerInput radial deadzone 0.25 over (aTurn, aLookUp), rescaled (|v| − 0.25)/0.75; no temporal filter.
  - Driving.UpdateSimulationInputs: Steering = sign(s)·s²; × SteeringScale (Nitro 0.3 for 3 s).
- **Left stick X:** RollControl only. The truck cannot barrel roll (RollDuration 0); it drives UpdateLeveling (|RollControl| > 0.1).
- **Camera:** in boost the camera yaw follows the truck's yaw (TnDrivingOrbitRotation), OrbitSmoother 0.25 s. The right stick does not rotate the camera.
- **Wheel/tire laws:**
  - front wheels steer up to 25°, rear 0;
  - per wheel, F = clamp(−v_lateral(wheel frame) · 0.0015 · Load, ±2·(M/4)|g|), applied along body +Y at the wheel;
  - yaw comes only from the torque (inertia 58.9e6);
  - ground angular damping 5·(1−|s|)²;
  - air control 12 rad/s² / 2600 unchanged.
- **Removed:** the provisional fixed yaw rate (180°/s at full lock, instant) and the 8 s⁻¹ lateral grip.

### HIGH CONFIDENCE
- Static per-wheel Load = (M/4)·|g| (no load transfer). Wheel positions relative to the COM: axles ±130 UU, track ±126 front / ±137 rear.

### PROVISIONAL
- No load transfer and no wheel suspension: the contact point is treated at ground level.
- Tire roll torque is not applied (the body settles on its wheels).
- The PhysX damping integration form `ω *= max(0, 1 − c·dt)` is assumed (UNKNOWN in RE).
- The root-vs-COM velocity offset is ignored.

### PC input translation [PROV]
| Xbox path | Original role in boost | PC |
|---|---|---|
| Right stick X (aTurn) | boost steering (camera yaw in hover) | **mouse X** (the PC camera-yaw axis): mouse rate / 1200 px/s = stick deflection, 0.05 s rate average, no deadzone |
| Left stick X (aStrafe) | RollControl only (hover: strafe) | **A/D** (unchanged): no steering in boost |
| LT | Boost | right mouse button |
| RB | VehicleSpecialMove (hover dash / Nitro) | Shift |
| Pad present | radial 0.25 deadzone on the right stick, no filter | — |

### Measured (WFC_VEHTEST, from straight-line speed)
| u0 (m/s) | input | yaw rate 0.1/0.25/0.5/1/3 s (°/s) | slip @1 s | RE model |
|---|---|---|---|---|
| 30 | 1.0 | 53.8/115.1/146.6/146.6/122.0 | 51° | 48/101/137/108/98, 36° |
| 30 | 0.5 | 12.3/22.3/28.6/30.5/32.5 | 6.3° | 12/21/27/28/27, 6° |
| 30 | 0.25 | 2.9/4.8/5.7/5.9/5.9 | 1.1° | — |
| 10 | 1.0 | 20.6/51.5/95.5/140.4/121.5 | 26° | 18/46/82/112/97, 20° |
| 30 | 1.0 Nitro | 15.3/29.2/40.3/47.1/49.4 | 9.7° | (0.3× steering) |

- The RE table is MODELLED (a planar sim of the same laws), not recovered. The rebuild keeps boost acceleration and drag active during the turn, which raises the full-lock rates.
- Raw stick → steering: 0.3 → 0.004, 0.5 → 0.111, 0.75 → 0.444, 1.0 → 1.0 (deadzone + square).
- Stick release: 146.6 → 28.7 °/s in 0.25 s → 0.1 in 1 s (damping + aligning; no snap).
- Left stick only: 0.01° in 1 s. Boost release → Hovering, drift 0.5 s, yaw back on the view.
- Hover, jump, dash, suspension and boost speed are unchanged (same VEHTEST values as PASS 14).

### Streets mode state (CONFIRMED ORIGINAL; RE 0ab03b2)
| actor | CTF | EXT | DOM (Conquest) | KOTH | DM / TDM |
|---|---|---|---|---|---|
| 4 objective bases | shown | shown | hidden | hidden | hidden |
| Flag factories ×2 | Active (+ "Flag" marker) | Disabled | Disabled | Disabled | Disabled |
| Bomb factory | Disabled | Active (+ "Bomb") | Disabled | Disabled | Disabled |
| FlagCapturePoint ×2 | Active (marker only while _Active) | inert | inert | inert | inert |
| BombPlantPoint ×2 | inert | Active, marker added (display needs AttackingTeam) | inert | inert | inert |
| Domination totems ×3 | hidden (collision kept, touch ignored) | hidden | **visible, Active, "Domination" marker** | hidden | hidden |
| KOTH zones ×5 | hidden | hidden | hidden | 1 random Active (shown, "KingOfTheHill"), rest Inactive | hidden |

- Disabled = SetHidden + SetCollision(false,false).
- The totem idle animation (DeactivatedLoopAnim) runs in every mode (`animClock`).
- Default mode is DM (WFC_GAMEMODE selects).
- **UNKNOWN:** what triggers KOTH rotation (`activateNewKothZone()` API only), the FlagCapturePoint `_Active` driver, the flag/bomb factory marker add timing **[PROV]**, and the friendly/enemy/contested marker presentation (HUD movie side).

### Objective marker handoff
`MapState::objectives()` carries:
- the hard-coded marker class and type string ("Domination", "KingOfTheHill", "BombPlantPoint", "FlagCapturePoint", plus factory "Flag" / "Bomb");
- the authored MarkerString;
- markerAdded (mode gate + state) and markerShouldDisplay (per-type rules).

This feeds the future `_global.UpdateMarker(id, dist, sx, sy, sz, type, desc)` path. No HUD was built.

---

## PASS 17 — MILESTONE 04 STREETS WORLD STATE, AssetTools a23c675 (2026-10-03, gameplay agent)

| Item | Authored evidence | Conf | Rebuild |
|---|---|---|---|
| Collision worlds | collision_pawn.glb (non-zero extent: BSP, 71 BlockingVolumes, 4 TnForcedDirVolumes, authored simple hulls) / collision_weapon.glb (zero extent: 34 weapon-blocking volumes) | CONFIRMED (flags/geometry), HIGH (UE3 rules) | **APPLIED**: movement 101k tris (was collision.glb render geometry, 1.85M), hitscan / line checks / visibility on the weapon world |
| KillZ | BASE TnWorldInfo KillZ −75000 UU | CONFIRMED | −750 m (was collision bounds − 25 m) |
| Truck hull | VH_Optimus_PHYSSYS box x −310..338, y ±154, z −35..185 UU | CONFIRMED | Replaces the PROV wall-probe radius (1.75 m → hull extent along the travel direction), minimum clearance (0.6 m → hull bottom −0.35 m) and top (2.44 m mesh bounds → 1.85 m) |
| Rotating domes | StaticInterpActor_15810/7381/8114 PHYS_Rotating Yaw 2730 UU/s (15°/s), collide + block | CONFIRMED | `MapState` movers: world-space pose about the pivot; triangles split into moving collision sets (pawn + weapon) |
| SkyBeam | GameplayStarted → "StartBeam" → SeqAct_Interp_3464 (loop 9.0022 s), EulerTrack CurveAuto, IMF_RelativeToInitial, on 5249 (collides) / 13497 / 10471 | CONFIRMED (data), HIGH (Euler vs quat interpolation, ≤20°) | Same clock as the domes. `worldDelta` per mover for Rendering. 5249 has moving collision. PosTrack (≤0.008 UU) not applied |
| Objective bases | 4 InterpActors bHidden, UnHide via SeqCond_GameRuleActive CTF / EXT, non-colliding | CONFIRMED | `MapState::modeVisibleActors()`: hidden in DM (default) / TDM / KOTH / DOM, visible in CTF / EXT (WFC_GAMEMODE) |
| Objectives / HUD signals | Flag/bomb factories, capture/plant points, domination points, KOTH zones; MarkerType / MarkerString / RequiredGameRule | CONFIRMED (future_hud_handoff) | `MapState::objectives()` with marker fields and activeInMode. No scoring |
| Wall panel collision | Base (intact) / Chunk02 (destroyed, settled) pieces | CONFIRMED (meshes/states) / PROV (per-poly, hulls not extracted) | Moving sets switched by state. No authored reset (state 2 terminal) |
| Player starts | 84 (60 team + 24 FFA), 12 clusters | CONFIRMED | WFC_START / WFC_START_ACTOR select a start; F6/F7 cycle them (test only, not a WFC binding) |
| Test dummy | — (rebuild instrumentation) | — | Only with WFC_TESTDUMMY=1 |

**Traversal (WFC_TRAVERSE=1, fixed 60 Hz):**
- 20 starts (one per cluster plus a spread) × robot and vehicle × 4 headings: 160 runs, 0 falls below KillZ, 0 snags. Every short run was against a wall within 4.5 m.
- 32 transforms at the run end points with no fall-through.

---

## PASS 16 — RUNTIME SEMANTICS, RE d50e2a9 (2026-10-02, gameplay agent)
Source: `RE-Workspace/notes/MILESTONE03_RUNTIME_SEMANTICS_ASSETTOOLS_7a69756.md` (RE commit d50e2a9). It corrects
AssetTools §2: TnPickupFactory SetPickupVisible/Hidden, IsReadyToPickup, GiveTo, TakePickUp and GetRespawnTime have bytecode.
Measured with `WFC_PICKUPTEST=1` (slice world, fixed 60 Hz), `WFC_HUDLOG=1` and the frame log.

| Item | Native/script (d50e2a9) | Conf | Rebuild | Measured |
|---|---|---|---|---|
| Factory states | 'Pickup' (visible, ammo crate PHYS_Rotating Yaw 10000) → valid Touch → GiveTo → the same frame enters 'Sleeping' (SetPickupHidden); collision kept, touches ignored; exactly RespawnTime; → 'Pickup' (SetPickupVisible). Actor never destroyed. Availability = !bPickupHidden | CONFIRMED | **APPLIED** | Ammo 30.02 s, health 60.02 s, overshield 119.99 s. Overlap while sleeping ignored |
| Touch semantics | Touch = overlap begin. TnHealthPickupFactory.SetPickupVisible → CheckTouching | CONFIRMED | **APPLIED** (was a per-tick overlap test) | Standing on the factory at respawn: health re-taken at once; ammo/overshield not re-taken until a new touch |
| Sounds | PickupSound plays on the receiving pawn (AnnouncePickup); no respawn effect/sound (RespawnEffectTime 0) | CONFIRMED | Event `receiverPos` + sound on Taken only | — |
| Highlight beam | PickupEffect.ActivateSystem in SetPickupVisible / DeactivateSystem in SetPickupHidden, only if ShouldDisplayHighlightFx (true only for TnAmmoCratePickupFactory) | CONFIRMED | `beamActive()` / event `beamActive` | Ammo beam 1 while available; health/overshield beam 0, custom FX 1 while available |
| ValidTouch / PickupQuery | ValidTouch: !bHidden, controller, line of sight; TnGame.PickupQuery not traced | PARTIAL | "nothing to gain" rejection stays **[PROV]** | — |
| HUD spread | NotifyWeaponSpreadChanged(raw), sent when it changes by > 0.002; raw = CurrentSpread × CurrentAirborneMultiplier × (fine aim ? 0.5 : 1) + Data.Spread (0) | CONFIRMED | `hudNotifies()` per HUD tick; `Character::effectiveSpread()` also drives the hitscan cone | Fine aim 0.080→0.040. Filter verified (WFC_HUDLOG) |
| Weapon/aim notify | NotifyCurrentWeaponChanged(class) + NotifyFineAimChanged(0/1), sent together when either changes | CONFIRMED | **APPLIED** | — |
| Spread model | IncrementSpread +0.005/shot; CooldownSpread every tick −(Max−Min)·dt/Cooldown (whole range in 2 s); Ion Blaster MP 0.08–0.18 | CONFIRMED | **APPLIED** (was "snap to Min after 2 s idle") | 10-shot burst 0.095 → back to 0.080 in ~0.3 s |
| Airborne | TnWeaponSpreadModifier AirborneMultiplier 2.0, ramp up 0.25 s, land ramp down 0.5 s (hover 0.1 s n/a) | CONFIRMED values / HIGH linear ramp | **APPLIED** (robot form) | Jump: 0.080 → 0.160 in 0.25 s; back over 0.5 s after landing |

**Harness note (Experimental):**
- `weapon.spread_after_10` (expects 0.13) and `weapon.spread_cap` (expects 0.18 after 2.5 s of fire) encode the superseded no-recovery-while-firing model.
- Under per-tick CooldownSpread at 15 shots/s the net bloom is +0.025/s: 0.10 after 10 shots, and the cap is reached after ~4 s.
- The check expectations need updating to d50e2a9; Gameplay did not edit the harness.

---

## PASS 15 — AUTHORED-DATA HANDOFF, AssetTools 7a69756 (2026-10-02, gameplay agent)
Sources: `AssetTools/manifests/fineaim_hud.json`, `streets_pickup_factories.json`, `streets_pickup_fx.json`,
`streets_destructibles.json` (commit 7a69756). Placement data comes from the slice's existing `gameplay.json` and
`physics.json` (no new extraction). Measurements come from `WFC_PICKUPTEST=1`, which runs the loaded slice world
at the fixed 60 Hz step.

| Item | Authored evidence | Conf | Rebuild |
|---|---|---|---|
| Ion Blaster fine-aim presentation | No special reticle or scope. HasFineAimScope unset (false); NotifyFineAimChanged shows scopes only for HeavyPistol/BurstRifle/SniperRifle; mc_crosshairIonBlaster stays in both aim states | CONFIRMED AUTHORED DATA | No scope/ADS asset is expected. `PlayerController::hudAimState()` exposes weaponClass (TnWeaponIonBlaster), EHudAimType (0/1), spread, crosshairVisible and TTFH_None |
| Fine-aim visible change | Prongs move to spread × 300 px (eased 0.2 s); FineAimSpreadModifier 0.5; PerShotSpreadModifier 0.08–0.18, +0.005/shot, cooldown 2 | CONFIRMED (HUD/data) / HIGH (native spread combination) | hudAimState.spread = bloom × 0.5 in fine aim. Measured 0.105→0.150 while firing; 0.090 at the cap in fine aim |
| Camera in fine aim | TnPCS_FineAim (no authored props) | — | Unchanged native camera (PASS 14): FOV 45, orbit-space offset |
| Pickup factories | 14 TnAmmoCrate (RespawnTime 30), 9 TnHealth (60), 1 TnOverShield (120). Touch cylinder r200/h100, COLLIDE_TouchAll | CONFIRMED AUTHORED DATA | `PickupFactory` actors at the authored gameplay.json placements. The graybox near-spawn pickups are removed |
| Objective factories | Flag ×2 / Bomb ×1, RequiredGameRuleClass CTF / BombingRun | CONFIRMED AUTHORED DATA | Not instanced: those modes are out of scope |
| Payloads | Health AddedHealth 50; AmmoCrate ValidWeaponTypes Primary/Secondary/Vehicle; OverShield no authored amount | CONFIRMED (health, types) / native (amounts) | Health +50. Ammo refills the reserve to MaxAmmoCount **[PROV amount]**. Overshield sets a granted flag only **[PARTIAL]** |
| Factory states | Pickup ↔ Sleeping; SeqEvent_PickupStatusChange; TakePickUp/GiveTo/ValidTouch native | CONFIRMED (states/events) / native (bodies) | One PickupEvent per transition (Taken/Respawned, available flag, authored PickupSound). A pawn with nothing to gain does not consume **[PROV ValidTouch]** |
| Pickup FX/meshes | Health/OverShield CustomPickupEffect auto-active while available; ammo crate mesh + inactive Pickup_FX | CONFIRMED / HIGH | Not drawn by Gameplay. Rendering/Systems consume `pickupFactories()` / `pickupEvents()` |
| Wall panel | TnStaticDestructibleActor_14465, WallPanelSign: state 0 health 20 → 1 (damage/touch/kismet) → 2 after 10 s. No damaged state. Initial state 0 | CONFIRMED AUTHORED DATA (initial state HIGH) | `Destructible` at its authored location (8.96, −3.52, 899.68 m) with the Base-piece damage/touch box. One DestructibleEvent per transition; meshes/FX/cues stay with Rendering/Systems |
| Wall panel placement | ~1400 m from the player starts, only actor above Z −50000 | CONFIRMED (positions) | Kept authored. Its absence from the playable view is not a reconstruction failure |

Measured with WFC_PICKUPTEST:
- **Ammo crate:** taken once (reserve 10→250), respawned after 30.02 s.
- **Health:** taken once (30→80), respawned after 60.02 s.
- **Overshield:** taken once (grant 0→1), respawned after 119.99 s.
- **Events:** exactly one Taken and one Respawned per cycle. Full health/ammo leaves the pickup available.
- **Wall panel:** 15+15 damage → destroyed → settled 10.00 s later, position unchanged.

**Superseded by 7a69756** (kept in older rows for history):
- "missing Ion Blaster ADS scope/reticle" and the ADS/spread-visualization TODO: there is no ADS scope; the crosshair + spread is the presentation.
- FIDELITY PASS 11 robot-camera "shoulder offset ... PROV semantics" row: resolved in PASS 13/14.
- "missing static destructible / visible destructible geometry" (harness KNOWN `missing.static_destructibles`, PLAYTEST-01): the single placed instance is authored far outside the play space. Experimental should retire that KNOWN.
- STATUS "Footsteps deferred (no clear footstep asset)": superseded. Streets surface audio is recovered (AssetTools 7a69756) and owned by Systems.
- Graybox pickup scaffold ("pickups near spawn for visual life"): replaced by the authored factories.

---

## PASS 14 — NATIVE RE MILESTONE 03 RECONCILE (2026-10-02, gameplay agent)
Source: `RE-Workspace/notes/MILESTONE03_VEHICLE_NATIVE_FIDELITY.md` (native RE 76bb0a). Measurements from
`WFC_VEHTEST=1`, which runs the real 60 Hz vehicle step on generated geometry (src/game/VehicleTests.cpp).

| Item | Native report | Rebuild | Measured |
|---|---|---|---|
| P1 suspension | 4 COM-relative probes ±130.8, body-down rays 250, implicit spring K/m, B/m, m=M/4, g = −dir.Z·GetGravityZ (RB −1940.4), push-only, cos-scaled, RB damping 0 | **APPLIED**; spring gravity corrected from world −2940 to RB −1940.4 | Rest COM 1.2872 m = native L_eq 128.7 UU (mass link M=2500 stays **PARTIAL** per report) |
| P1 bumps / drop | — | springs only, no ride-height target | 0.25 m step @15 m/s: COM 1.075–1.575 m, pitch −1.8..4.0°; 0.5 m: 0.863–1.857 m, −3.0..8.1°; 10 m drop: impact 17.1 m/s, min COM 0.60 m (PROV hull clearance), settles 1.287 m |
| P2 attitude | grounded pitch/roll = springs + UpdateRoll; yaw = camera each tick; upright 5%/tick only airborne/upside down | **APPLIED** (Pass 13) | — |
| P2 visual lean | TnAccelerationAnimBlend: m = ClampLength(v,2000)/2000 × max(0,up.Z); child0 = 1−|m|, dirs max(0,±sign·m²/|m|) | **APPLIED** (was velocity/1500 per axis PROV); ADD_Nav_Hover_VEH additive kept | — |
| P3 hover velocity | local X/Y toward stick×1500, one ClampLength 3000·(1−drift/0.5)²·up.Z²; no hover grip model | **APPLIED** | Coast-down 15→0 m/s in 0.500 s forward and sideways |
| P3 boost tires | F = clamp(−v_lat·coeff·scale·Load, ±2(M/4)|g|) | cap **APPLIED**; coefficient **PROV** (not recovered) | — |
| P3 drift turn | heading = camera; authority ramps | **APPLIED** | Camera +90° after boost release: yaw 90° at once, travel heading 0.2° @0.15 s → 11.9° @0.6 s |
| P4 hover jump | +1200 world Z additive, local ω −1, 0.3 s ground cooldown | **APPLIED** | vy +12.00, apex +3.80 m over rest (ballistic 3.71 + spring push), horizontal kept |
| P4 boost jump | local (600,0,1400), ω (0,−2,0) | **APPLIED** | +6.0 fwd, +14 up (13.68 after one tick of g), pitch rate 1.8 rad/s after air damping |
| P5 dash | body-local (1,0,0); mask (1,1,1); 100000; exit snap fwd 1500; refuse/cancel unstable | **APPLIED** | Stick right ignored: tick 2 = 29.8 fwd / 0.08 lat (one 30 Hz tick = two 60 Hz ticks), 30.0 during, exit 15.0 fwd |
| P6 camera offset | orbit-space translation, full camera rotation, X toward pawn, Y right, Z up; CurveAutoClamped cubic; C2 smoother (T/2) | **APPLIED**; curve now Hermite with flat end/extremum tangents | Fine aim at level pitch: camera 7.35 → 9.16 m from the actor (X +150 → −50), no lateral change |
| P7 hand | HandSkelControl R_Arm04_Hand_XB scale 0.1, strength 0/1 instant; ShouldEquipHand rules | **APPLIED** | Shrunk with the gun drawn; full size during R→V and V→R before the restore; shrinks on the restore tick |
| P8 ram | TnPawn victims only (mass ≤ 1000, other team); robot RammedReaction (falling, dir·5000+base for 0.5 s, then (0,0,baseZ)); vehicle AddVelocity ×0.5 | victim rule **APPLIED** (the DamageTarget dummy is not a TnPawn and is no longer rammed); robot reaction + vehicle AddVelocity implemented | WFC_RAMSELF: 50 m/s + base for 0.5 s, horizontal 0 after. The slice has no pawn victims; masses **PARTIAL** |
| P9/P10 visibility | notifies 0.8796 / 0.3958 / 0.0984 / 0.6634 s, ÷ Rate (4 downed), final state at BeginState | **APPLIED** (exact times, Rate constant 1; no downed state) | Vehicle hidden from 0.6634 s; clip geometry untouched |

---

## PASS 13 — VEHICLE BODY, VEHICLE CAMERA, TRANSFORM HANDOFF (2026-10-02, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Hover support | 4 TnSuspension rays from COM + Normal(1,1,0)×185 at 90° steps, along body −Z, length 250; TnSpring implicit (K=10000/m, B=4000/m, m=Mass/4); force along body up × Dot(up,N) | TnHoverCarSimulation.UpdateSuspension/CalculateSuspensionLocation/InitializeSuspension, TnSpring.Update/CalculateSpringVelocity/Reset, HoverTruck_Suspension | CONF | **APPLIED**; COM 1.36 m (was a fixed 1.85 m ride height = misread SuspensionRadius) |
| Body mass/COM/inertia | 2500; (−47,0,20)−(0,0,15); 2.27e7/4.64e7/5.89e7 | TnCarSimulation.InitializeFromBlueprint, Truck_Physics, OptimusTruckForm.ChassisOffset | CONF | **APPLIED** |
| Roll/pitch | UpdateRoll: local angular accel X = RightLeft − ωz; uprighting 0.05/tick only with no contact or upside down; damping 0 | UpdateRoll/UpdateTurn/Activate | CONF (sign HIGH; per-tick factor at 30 Hz PROV) | **APPLIED** |
| Vehicle jump | Hovering: on ground (N.Z>0.707), interval 0.3 s, +1200 Z, ω(0,−1,0). Driving: local (600,0,1400), ω(0,−2,0), air control 2600/12, pitch-forward −25°/3 | Hovering/Driving.UpdateJumping, TnHoverCarSimulation/TnCarSimulation.Jump, UpdateAirControl | CONF | **APPLIED** |
| Hover dash | Truck: forward only; refused if unstable; local all-axis strafe to 3000 then one-tick decel to 1500 | TnTruckForm.Hovering.DoDash, UpdateDash | CONF | **APPLIED** (Pass 12 dominant-axis superseded) |
| Boost acceleration | Lerp(2500, Drag(Max), v/Max) + ExtraBoost (8×, to 0.5·Max, ≤5000) × BoostScale; Drag = v²·g_RB/6000² | UpdateBoost/CalculateExtraBoostAcceleration/CalculateDragAcceleration | CONF | **APPLIED** (tire steering PROV) |
| Driving exit on impact | Frontal contact (N·fwd > 0.866) → Hovering | Driving.OnRigidBodyCollision | CONF | **APPLIED** (normal approximated by blocked travel) |
| Driving steering | Steering = sign(s)·s² of GetNormalizedTurn (look X) | PlayerInCarForm.SetLocalInputs, Driving.UpdateSimulationInputs | CONF input / PROV yaw rate | **APPLIED** |
| Vehicle camera | HoverTruck: anchor 185, orbit 950, FOV 80, pitch −20..30, orbit smoother 0.1, offset Z (45,0,120) over ±25°. Truck: 215/1050/85, nitro 100 & 650, yaw = pawn, pitch chase 3/s, smoother 0.25 | CAM_Driving_Strategies_p, TnDrivingOrbitRotationCameraBehavior, HmOrbitSmootherCameraBehavior | CONF | **APPLIED** |
| Hover yaw source | Controller rotation = camera rotation (after smoothing) | PlayerInVehicleForm.PlayerMove | CONF | **APPLIED** |
| Camera smoothing | HmC2Smoother: ω = 4/SmoothTime, Padé exp | HM_Engine bytecode | CONF | **APPLIED** (FOV, offsets, rotation) |
| Robot camera offset | Offsets[3] vectors: (150,300,150) (150,300,−35) (150,300,150); FineAim (−50,300,80)/(−50,300,−35)/(−50,300,80) | raw property data (static array) | CONF values / interpolation PROV | **APPLIED** (Pass 11 lateral-only reading superseded) |
| Strategy blend | TransitionTime of the new strategy: OTS 1.5, Hover 1.5, Truck 1.0 | strategy objects | CONF / blend curve PROV | **APPLIED** |
| Transform visibility | ToVeh: robot hide 0.880, vehicle unhide 0.396; ToRobot: robot unhide 0.098, vehicle hide 0.663 | AssetTools notifies (EVIDENCE_TARGETED_PASS 5a–5d) | CONF | **APPLIED**, both meshes on the shared clip time |
| Shared actor location | RB placed at pawn Location + vehicle mesh translation (−bounds centre) | TnVehicleForm.OnActivate/CalculateCylinderBounds | CONF | **APPLIED** (vertical only) |
| Weapon on V→R | Restore at 25%, usable +0.2 s; drawn on the robot mesh | TARGETED_PASS2 §7 | CONF | **APPLIED** + firing requires the drawn gun |
| Arm mesh | CP_OptimusArm_SKEL when no weapon (R→V fold, V→R before 25%), ARM_Equip / ARM_Unequip, WeaponSocket_Secondary | TARGETED_PASS2 §9, character.json | CONF | **APPLIED** (13b); HandSkelControl PROV/not applied |
| Fine-aim offset | Lateral Y 300 in all rows; fine aim changes orbit X (+150 → −50) and Z ends (150 → 80) | raw OffsetCurvesByPCS + TnLocationOffset/HmOrbitUpdateLocationRotation bytecode | CONF | **APPLIED** (no lateral change by design) |
| Landing clip | SharedAcrobatics.LandingAnims {1200,1200}_03, {1000,1200}Land, {4500,0}_03, {500,0}_02, {250,0}Land | authored data (Systems handoff) | CONF data / MED semantics | **APPLIED** (13b) |
| Wall contact | physWalking slide: velocity into the wall removed, displacement velocity | stock UE3 | HIGH | **APPLIED** (13b, single-ray probe PROV) |
| Ram | AttemptToRam during nitro: once per target, 300 to AI robots | TnTruckForm bytecode + OptimusTruckForm | CONF | **APPLIED** vs damage targets (13b); knock-back n/a |

---

## PASS 12 — RECONCILED WITH NATIVE RE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Vehicle→robot entry | Local player keeps full velocity and enters falling | native RE | CONF | **APPLIED** (replaces Pass 11 ground snap) |
| Robot→vehicle entry | Velocity clamped 3500 UU/s written into the RB, no reprojection; starts at robot yaw | native RE + OnActivate bytecode | CONF | **APPLIED** |
| Hover steering authority | Fades in over 0.5 s: accel × (1 − Drift/0.5)², Drift started by Hovering.BeginState | native RE + CalculateDriftScale/Drift bytecode | CONF | **APPLIED** (also after leaving Driving) |
| Hover control frame | View yaw: Hovering.DoUpdate → HoverSimulation.Update(..., ViewRotation.Yaw); UpdateTurn matches the yaw; UpdateStrafe toward (fwd, right) × 15 m/s, ClampLength accel 30 m/s² × drift × stability | bytecode | CONF | **APPLIED** (supersedes Pass 7 travel-direction steering; stability scale = 1 on flat ground) |
| Normal boost | Boost held → TnCarForm.Driving (Truck_Physics MaxSpeed 3000, MaxAcceleration 2500); not while drifting; release → Hovering | Hovering/Driving.UpdateBoosting bytecode; Truck_Physics | CONF (state, values) / PROV (steering, throttle) | **APPLIED** with wheels pose (HoverToBoost → Idle_Wheels; BoostToHover on exit) |
| Hover dash | Dash while hovering: dominant input axis (local), DashSpeed 3000, 0.5 s, accel 100000; cooldown TimeBetweenDashes 2.0 | Hovering.DoDash/UpdateDashing, UpdateDash, get_TimeBetweenDashes | CONF | **APPLIED** |
| Nitro / ram | Dash while driving: 3 s, speed ×1.5, steering ×0.3, cooldown 8 s, stop on leaving Driving | Systems checkpoint (script literals) | CONF | speed/steering **APPLIED**; ram collision not implemented; state duplicated with Systems VehicleNitro (unify at integration) |
| Dash binding | VehicleSpecialMove: PC Shift ("Ability0 \| VehicleSpecialMove"), pad RightShoulder → PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput | bindings + bytecode | CONF | **APPLIED** (Systems' temporary Q superseded) |
| Input latching | Fire held flag persists; reload on release of a tap < 0.3 s; jump/dash edge latched until consumed; transform immediate | native RE | CONF | **APPLIED** |
| Weapon restore | _RestoreWeaponTransformFractionRemaining 0.75 = restore at 25% elapsed (vehicle→robot); usable after EquipTime 0.2 s | native RE + ini | CONF | **APPLIED** (usable at 0.48 s of 1.13 s fold); visible gun waits for the 50% mesh handoff [PROV] |
| Fine aim on transform | Transforming to vehicle ends it | native RE | CONF | **APPLIED** (wish cleared) |
| Locomotion play rate | Walk/jog at 1.0×; the original shows the same stride mismatch | native RE | CONF | no compensation (as built) |

---

## PASS 11 — TRANSFORM MOMENTUM, ROBOT SPEED, FINE AIM (2026-10-01, gameplay agent)

Evidence: UnrealScript bytecode decoded from `TransGame.xxx` with `work/pass11/ue3dis.py` (validated on
`TnPawn.UpdateSpeeds`), shipped bindings (`Xe-TransInput.ini`), character/vehicle/camera content
objects (`Optimus_ROBODEF`, `HoverTruck_Physics`, `TruckTransformerMomentum`, `OverTheShoulder_STRATEGY`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Velocity at transform start | Untouched. `TnPawn.Transform` → `Transforming.BeginTransformation` → `TnTransformation.Execute` never zero velocity | bytecode | CONF | **FIXED** (rebuild zeroed velocity in beginTransform) |
| Input during a fold | Movement keeps working; only `IsAbleToFire`, `PreWeaponSwitch`, `StartTransform` check IsTransforming | bytecode (all 20 get_IsTransforming call sites) | CONF | **FIXED** (rebuild locked all input); fire/reload stay blocked, jump blocked [PROV] |
| Movement form switch | `BeginTransformation`: _CurrentForm = TargetForm + target movement capabilities at fold START | bytecode | CONF | **APPLIED** (moveForm(); mesh still hands off mid-fold) |
| Robot→vehicle handoff | `TnVehicleForm.OnActivate`: Velocity = ClampLength(pawn Velocity, 3500); RB rotation = pawn rotation | bytecode; `kMaxTransformSpeed` 3500 | CONF | **APPLIED** |
| Vehicle→robot handoff | `TnRobotForm.OnActivate` leaves velocity; robot `CalcVelocity` continues it | bytecode | CONF | **APPLIED** |
| Momentum preservation | Above max speed: MaxAccel = AccelRate/(1+P); P from TruckTransformerMomentum OnGround 7/3/1, InAir 100/100/5 (fwd/neutral/back; fwd = input within 30° of velocity); InAir set while falling OR transforming | `CalculateMomentumPreservation`, `CalculateMaxAcceleration` bytecode | CONF | **APPLIED** |
| Robot velocity model | Desired = Normal(input)·max(450, ‖input‖·MaxSpeed); no input → 0 (ground) / keep (air); local-frame per-axis accel clamp; no speed cap | `TnPawn.CalcVelocity` / `CalculateDesiredFlatVelocity` bytecode | CONF | **APPLIED** (AirControl application PROV) |
| Character movement values | BaseGroundSpeed 1400, AccelRate 12000, AirSpeed 1200, AirControl 0.4, TerminalVelocity 6000, Collision r200/h200, JumpHeight 500 (SharedAcrobatics) | `Optimus_ROBODEF` via `TnPawn.ApplyTransformer` bytecode; identical on 6 playable ROBODEFs | CONF | **APPLIED** (replaces Passes 2/10 class defaults) |
| Robot fast movement | No sprint key exists. Full input = 14 m/s jog (the "missing fast movement"); partial pad input ≥4.5 m/s walk; vehicle→robot keeps vehicle/boost speed (momentum run, ≤35 m/s handoff) | bindings + above | CONF | **APPLIED** |
| Boost input | RightMouseButton / LeftTrigger are bound to both FineAim and Boost; robot OnStartBoost empty; vehicle states boost | `Xe-TransInput.ini`, bytecode | CONF | **APPLIED** (RMB/LT boost in vehicle; Shift kept as alias) |
| Truck physics | DashSpeed 3000, DashDuration 0.5, SuspensionRadius 185 (MaxLinearSpeed default 1500) | `VEH_SHARED_p.HoverTruck_Physics` | CONF | **APPLIED** |
| Fine aim input | PC RMB = ToggleFineAim; pad LT = FineAim (hold) | `Xe-TransInput.ini` | CONF | **APPLIED** |
| Fine aim rules | Robot only (PlayerWalking); blocked while meleeing/reloading/dodging; wants persist → resumes after reload | `TnFineAimManager.Tick/Start/Stop`, `PlayerWalking.CanFineAim` bytecode | CONF | **APPLIED** (also blocked while transforming [PROV]) |
| Fine aim movement | SetSpeedMultiplier(0.5) → 7 m/s ground (air too) | `_GroundSpeedMultiplier`, UpdateSpeeds | CONF | **APPLIED** (excess speed bleeds through momentum preservation) |
| Fine aim camera | FOV 80→45 (SmoothTime 0.1), exit 0.4; look speed 25/12.5 vs 50/25; orbit distance unchanged for Ion Blaster; shoulder curve unchanged | `OverTheShoulder_STRATEGY` behaviours | CONF values / PROV smoothing curve | **APPLIED** |
| Fine aim weapon | Spread × FineAimSpreadModifier 0.5; weapon events 15/16 (sounds) | `TnWeapon.Start/StopFineAim`, WEPDATA | CONF | spread **APPLIED**; sounds not wired |
| Fine aim upper body | No fine-aim pose (WeaponPose slot plays weapon PoseName only) | `TnWeaponPoseSlotLogic` bytecode | HI | none (aim offset continues) |
| Fine aim target snap | TnOrbitRotateToTarget FineAim: snap, speed 10; _TargetSnapTimeout 1.0 | strategy + ini | CONF | NOT implemented (no target system) |
| Robot camera | Anchor actor+200 UU; orbit 800; DefaultFOV 80; pitch ±75; shoulder offset [150,300,150] by pitch, SmoothTime 0.3; third-person collision | `OverTheShoulder_STRATEGY` | CONF values / PROV offset semantics + collision rules | **APPLIED**; traces aim through the crosshair point |
| Support/step query | groundHeight(nearY = body, stepUp = 35 UU) | — | fix | **FIXED** (pre-existing double count: robots stepped 0.7 m, vehicles could snap to decks 4.4 m higher) |

---

## PASS 10 — LOCOMOTION BLEND, GROUND SPEED, STEP HEIGHT (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Moving-state structure | TnVelocityAnimBlend(Walking, Jogging) over TnStraferAnimBlend(F,B,R,L) of `Nav_Strafe{Walk,Jog}_*` (OneHanded) | Robot_ANIMTREE | CONF | **APPLIED** |
| Walk→jog speeds | MinSpeed 450 / MaxSpeed 1200 UU/s (class default 100/1000 overridden) | `TnVelocityAnimBlend_10449` | CONF | **APPLIED** (linear weight; PROV formula) |
| Clip ground speeds | walk ≈3.5 m/s, jog ≈12.1 m/s (planted-toe stance speed) | robot.glb measurement (`work/pass8/stride.js`) | HI | Explains 1200 = jog speed |
| Direction weights | from local velocity; `_BlendSpeed` 0.2 | `Default__TnStraferAnimBlend` | CONF (value) / PROV (max(0,±dot) normalized, eased) | **APPLIED** |
| Phase sync | "Strafers" AnimNodeSynch group (all 16 strafe sequences, RateScale 1) | `AnimNodeSynch_581` | CONF | **APPLIED** (shared phase, master = highest weight) |
| Idle↔Moving transition | AmpCrossFadeCondition 0.2 s | IdleToMoving / MovingToIdle | CONF | **APPLIED** (was PROV 0.15) |
| Robot ground speed | GroundSpeed = _BaseGroundSpeed (=GroundSpeed 550 at PostBeginPlay) × Π speed multipliers; Ion Blaster GroundSpeedMultiplier 1.0 | TnPawn.PostBeginPlay / UpdateSpeeds bytecode; Default__TnWeaponData | CONF | **SUPERSEDED (Pass 11):** ApplyTransformer later calls set_BaseGroundSpeed(ROBODEF.BaseGroundSpeed = 1400) → 14 m/s |
| MaxStepHeight | 35 UU = 0.35 m (WalkableFloorZ 0.7, MaxFallHeight 3400) | `Default__TnRobotForm._MovementCapabilities` | CONF | **APPLIED** step 0.35 m (was PROV 0.6); deterministic A/B shows no new snagging. WalkableFloorZ not yet enforced. |
| Idle clip names | Tree defaults `NAV_Idle_01` / `AI_Nav_Idle_Pose_05` / `ADD_NAV_Idle` resolved per character by choosers | Robot_ANIMTREE idle branch | — | Optimus set lacks them; `NAV_Idle` kept |

---

## PASS 9 — AUTHORED AIM OFFSET PROFILE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Active profile | `Default` for the Ion Blaster (`ActiveProfileProperty`=WeaponTypeObserved; other profiles are TwoHandedMelee, TwoHandedMeleeCharge, TwoHandedGun) | Robot_ANIMTREE `TnAnimNodeAimOffset_14979` | HI | **APPLIED** |
| Profile bones | Lumbar01, Lumbar02, Neck01, Head, L Clav/Shoulder/Elbow/Hand, R Clav/Shoulder/Elbow | `AimComponents[].BoneName` | CONF | **APPLIED** (replaces the whole-upper-body local deltas of Passes 7–8) |
| Cell offsets | 9 rotations (+ translations on R_Arm01_Clav) per bone, baked from `Shooting_Aim_{L,F,R}_{D,C,U}` (`AnimName_*`) | `AimComponents` | CONF | **APPLIED**, baked at load by the verified rule: rot = Gp·(L_cell·L_C⁻¹)·Gp⁻¹, pos = Gp·(t_cell−t_C), Gp = parent model rotation in that cell. Matches the shipped quaternions to 0.01° mean / 0.07° worst and translations to 0 mm (UE→glTF: q `(−x,−z,−y,w)`, v `(x,z,y)·0.01`). |
| Application | Model-space rotation/translation about each bone's pivot, parent first; bilinear between cells | UE3 AnimNodeAimOffset (consistent with the bake: reproduces every cell pose from the centre pose) | HI | **APPLIED** (slerp per axis, weight = aimW). |
| Ranges | Profile H [−1,1], V [−1,0.8]; `RemapPawnAimRange` from PawnAimOffsetRange H [−1,0.85], V [−0.7,1]; pawn aim = angle/90° | Default profile | CONF (values) / PROV (centre-preserving remap) | **APPLIED**. Barrel pitch at aim −69/−34/0/+34/+69° = −42/−21/+3/+27/+50°. Replaces the Pass 7 barrel-pitch calibration. |
| Pivot arm swing | `Nav_IdlePivot90_*` swings the gun forearm ≈50° mid-step (root-discarded chest ±7°) | clip data; tree layers the aim offset over the turn unchanged | HI | Kept as authored. |

---

## PASS 8 — RECOIL, TURN IN PLACE, SHIPPED ANIM TREE (2026-10-01, gameplay agent)

Primary evidence: `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` (cooked in `MP_IAC_Streets_BASE_m.xxx`)
plus class defaults in `TransGame.xxx` / `HM_Engine.xxx`, read with AssetTools `ue3pkg`/`props`
(read-only; dumps in `work/pass8/`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Upper-body blend root | `AnimNodeBlendMultiBone` start bone `C_Spine01_Lumbar01_XB` (plus a left-arm branch at `L_Arm01_Clav_XB`) | Robot_ANIMTREE | CONF | Matches the Pass 7 mask root (PROV → **CONF**). |
| Recoil bones | RightHandRecoil → `R_Arm02_Shoulder_XB`; LeftHandRecoil → `L_Arm01_Clav_XB`; SpineRecoil → `C_Spine02_Lumbar02_XB` | Robot_ANIMTREE `SkelControlLists` | CONF | **APPLIED** (spine + right hand; the Ion Blaster defines no left-hand recoil). |
| Recoil values | Spine: 0.8 s, RotAmp [500,1000,0], Freq [10,10,0], all ERS_Zero. Hand: 0.5 s, RotAmp [2000,500,−2000], Freq [15,10,10], Yaw ERS_Random, LocAmp X −8 UU @10 | `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides; struct default `Default__HmSkelControlRecoil` (0.33 s, zeros) | CONF | **APPLIED** (`game/Recoil.h`), restarted per shot (`TnRecoiler`). |
| Recoil update law | sin-wave per axis × smoothstep(TimeToGo/Duration), mesh-space in the aim frame (`bBoneSpaceRecoil` false) | UE3 GameSkelCtrl_Recoil (reconstructed; native not decompiled) | MED | **APPLIED**. UE rotator → model-axis mapping (yaw/roll sign) PROV. Measured: +12° muzzle climb under sustained fire. |
| Turn-in-place values | Threshold 4096 UU (22.5°), TransitionBlendTime 0.1 s, PercentageToAllowAbort 0.5, RotTransitions ±16384/±32768; player `RRO_Discard` | `Default__TnAnimTurnInPlace`, `Default__TnAnimTurnInPlacePlayer` | CONF | **APPLIED**. |
| Turn clips | Rt_90/Rt_180 → `Nav_IdlePivot90_R`, Lt_90/Lt_180 → `Nav_IdlePivot90_L` (WS_ONE_HANDED) | TnWeaponAnimChooser in Robot_ANIMTREE | CONF | **APPLIED**. Clips author ~81° on `C_Root_Reference_XR` over 0.53 s. |
| Turn trigger / unwind | Inferred: legs planted (UnwindLowerBody), trigger when \|offset\| ≥ RotationOffset − threshold, unwind RotationOffset along the clip's root-yaw curve | Field names + values; native logic not recovered | PROV | **APPLIED**. Chosen over "trigger at 22.5° and unwind the offset" because it keeps the feet consistent with the authored step. |
| Aim offset yaw columns | Leg/aim difference feeds the aim offset horizontally (`TnAnimNodeAimOffset.TurnInPlaceOffset`) | TransGame class layout | HI | **APPLIED** (Pass 9: through the authored profile ranges). |
| Aim offset interpolation | InterpSpeed 12 | `Default__TnAnimNodeAimOffset` | CONF | **APPLIED** (FInterpTo-style). |
| Aim offset authored profile | `TnAnimNodeAimOffset` "Default" profile | Robot_ANIMTREE | CONF | **APPLIED in Pass 9** (see below). |
| Incoming transform clip | Matched pair `Transform_ToVehicle_ROBO` ↔ `Transform_ToVehicle_VEH` | clip durations (1.967 s both) | HI | **FIXED** (was SuperBoost_Veh, 0.8 s). |

---

## PASS 7 — ANIMATION LAYERS & MESH FACING (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Mesh forward axis | Skeletal meshes face model **+X** (UE convention; umodel `(x,z,y)` keeps X). | `Shooting_Aim_F_C` barrel dir in model space = (1.00,−0.06,−0.02) | CONF | **FIXED** `kMeshYawOffset=+π/2` (Pass 6's 0 left robot/truck side-on, gun 90° off the reticle). Verified: chase cam sees back/rear; muzzle along aim yaw. |
| Upper-body aim offset | `Shooting_Aim_*` 9-pose grid points the gun at the aim. | `robot.glb` category `aim`; weapon.json `owner_animations.aim` | HI | **APPLIED**: F column (body faces aim yaw → yaw offset 0), delta from F_C on `C_Spine01_Lumbar01_XB` subtree, driven by controller pitch. |
| Aim pitch → grid mapping (superseded by Pass 9 authored ranges) | UE3 AimOffset normalized pitch range | Calibrated from the poses: barrel pitch D −47.6° / C −3.3° / U +72.2° | CONF-derived | **APPLIED**: piecewise-linear so barrel pitch ≈ aim pitch (measured −27/+7/+42° at −34/0/+34°). |
| Reload while moving | Owner reload clip over locomotion | weapon.json `owner_animations.reload_robot` = `Shooting_Reload_IonBlaster_ROBO` | HI (clip) / PROV (mask) | **APPLIED**: upper-body slot (mask root `C_Spine01_Lumbar01_XB` PROV), full body when standing, mesh-space per-bone blend (UE3 `AnimNodeBlendPerBone` default). |
| Slot / aim / additive ease time | — | — | GUESS | `kSlotBlend=0.15 s` **PROVISIONAL**. |
| Vehicle move animation | Directional single-frame hover poses + additive hover bob | `vehicle.glb` `Nav_Hover_{Pose,F,B,L,R}_VEH`, `ADD_Nav_Hover_VEH` (additive=true) | HI (clips) / PROV (weights) | **FIXED** (was looping the one-shot `Nav_BoostToHover_VEH` transition). Weights = local velocity / MaxLinearSpeed per axis. |
| Vehicle turn rate | **~π rad/s** | `TnHoverCarSimulationBlueprint.AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AiMaxAngularSpeed is AI-only (truck: 20); player steering rate PROV |
| Robot gameplay idle | Character-specific nav idle | `NAV_Idle` from `Optimus_ROBO_ANIM`; `Cust_Idle` = customization screen | MED | **FIXED** (was `Cust_Idle`, first of category). |
| Jump / land | Take-off once → descent loop → land | `Nav_TakeOff_01`, `Nav_Jump_Descent`, `Nav_Land` | MED | **APPLIED**: take-off non-looping; land after ≥0.3 s airborne (`kLandMinAirTime` PROV), skipped when moving. |
| Weapon recoil | see Pass 8 | weapon.json + archetype | CONF | **APPLIED in Pass 8**. |
| Turn in place | see Pass 8 | Robot_ANIMTREE | CONF | **APPLIED in Pass 8**. |

Additive-clip convention: `ADD_*` clips are stored as **deltas** (identity-ish quats, zero
translations at rest), so `samplePose(additive=true)` starts from identity and `addPose` composes
`base * delta` in bone-local space.

---

## PASS 7 — WEAPON LAYERING, RECOIL, WEAPON MESH, FX, SOUNDCUES (Systems agent, 2026-10-01)

> **Integration note (integration/milestone-01):** the recovered data in this pass is kept as
> provenance. In the merged build, the *Reload layering*, *Recoil controls/defs/evaluation* and
> *Upper-body aim offset* (PASS 7b) rows are implemented by **Gameplay's** code (Passes 7–9), not
> by the Systems implementation described here. Both branches recovered the same Robot_ANIMTREE /
> RecoilDef values. The Systems `AimOffset.h`, `updateUpperBody`/`evalLayered` and shot-serial
> recoil trigger were not merged, to avoid a second aim offset and double recoil per shot.
> Remaining value difference: Systems reads the owner-anim slot blend as **0.1/0.1 s [CONF]**
> (`TnWeaponOwnerAnimator` CDO), while Gameplay's reload slot uses `kSlotBlend` 0.15 s [PROV].
> This is left for the Gameplay owner to reconcile. The weapon mesh, sockets, notifies, FX, SoundCues
> and audio rows below are all live in the merged build.
Recovered from cooked packages with AssetTools `objtree`/`typed_props` (read-only); decoders in
`tools/systems/`. Owner animation, recoil, weapon-mesh, FX and cue data come from
`WEP_IonBlaster_p` / `FX_*_p` / `BL_WPN_*` exports cooked into `A1_IAC_Base_m` / `MP_IAC_Streets_BASE_m`,
plus class defaults (CDOs) in `TransGame.xxx` / `HM_Engine.xxx`.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Reload layering | Reload plays in Robot_ANIMTREE's **UpperBodyCustom** AnimNodeSlot, fed through AnimNodeBlendMultiBone_4930 (InitTargetStartBone **C_Spine01_Lumbar01_XB**, PerBoneIncrease 1.0: spine + arms + head = 1, hips/legs = 0). Legs keep locomotion. | `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF | **APPLIED** (bone-local blend, as AnimNodeBlendMultiBone). Verified: base `Nav_StrafeJog_F` @5.5 m/s while upper = `Shooting_Reload_IonBlaster_ROBO`. |
| Reload owner anim | `ReloadAnimation = Shooting_Reload_IonBlaster_ROBO`, non-additive, BlendIn/Out **0.1/0.1 s** | `IonBlaster_WEPDATA.TnWeaponOwnerAnimator_10029` + `Default__TnWeaponOwnerAnimator` | CONF | **APPLIED**. Slot releases at reload end (1.5 s) and blends out while the 1.633 s clip finishes. |
| Fire owner anim | none (`FireAmmoAnimations` empty for the Ion Blaster); firing drives skel-control recoil only | same | CONF | APPLIED |
| Equip/overheat anims | `ADD_Shooting_equip_weapon` (CAMT_UpperBody, additive, 0.2/0.3 s); overheat Start/Loop/End additive | same | CONF | not yet (no equip/overheat gameplay) |
| Recoil controls | HmSkelControlRecoil: **RightHandRecoil** on R_Arm02_Shoulder_XB, **SpineRecoil** on C_Spine02_Lumbar02_XB, LeftHandRecoil on L_Arm01_Clav_XB | Robot_ANIMTREE SkelControlLists | CONF | **APPLIED** (left hand: no Ion Blaster RecoilDef, idle) |
| Recoil defs | Spine: 0.8 s, RotAmp (500,1000,0), RotFreq (10,10,0), RotParams all Zero. RightHand: 0.5 s, RotAmp (2000,500,-2000), RotFreq (15,10,10), Y=Random, LocAmp X=-8 UU @10 | `TnWeaponMesh` CDO merged with `IonBlaster_WEPMESH` | CONF | **APPLIED** |
| Recoil evaluation | RecoilDef layout identical to UE3 GameSkelCtrl_Recoil: restart per shot, smoothstep(TimeToGo/Duration) x Amp x sin(phase + TimeToGo x Freq), aim space | struct identity with UE3 | HI | **APPLIED** (rotation about the bone origin in model aim space) |
| Camera recoil / shake | RecoilCameraParams (FOV +0.085, pitch 0.1 deg, 0.05 s, delay 0.25 s); CameraShake (0.17, roll 0.15, falloff 0.1/0.06 s) | `IonBlaster_WEPDATA` | CONF | **not applied**: camera is owned by the Gameplay agent |
| Weapon mesh | 34-joint skeletal mesh with its own AnimSet: WP_Fire -> `IonBlaster_Fire`, WP_Reload -> `Shooting_Reload_IonBlaster_AP`, idle `IonBlaster_Idle`; HmAnimatedMesh BlendOutTime 0.2 s | `IonBlaster_WEPMESH.WeaponEventAnims`, `HmAnimatedMesh_9126` | CONF | **APPLIED** (was a static mesh) |
| Weapon sockets | MuzzleFlash -> C_Robo04_XT; ShellSocket -> C_Robo15_XT (+loc/rot); MagSocket -> C_Robo01_XT (+loc/rot) | `WEP_IonBlaster_SKEL` SkeletalMeshSockets | CONF | **APPLIED**; muzzle/tracer origin = MuzzleFlash socket (replaces the geometric barrel tip) |
| Event timing (AnimNotifies) | Fire: Shell_AssaultRifle_FX @0.005 ShellSocket. Reload_AP: ANIM_RELOAD_01 @0.000, Reload_AssaultRifle_FX @0.034 MuzzleFlash, ANIM_RELOAD_02 @0.137, Magazine_IonBlaster_FX @0.174 MagSocket. Idle: IDLE_01 @0.022, IDLE_02 @2.751 | `WEP_IonBlaster_ANIM` (HmAnimNotify_Sound / HmAnimNotify_PlayEffect) | CONF | **APPLIED** for sounds and effects (shell/reload/magazine FX: PASS 7c) |
| Muzzle flash | `FX_AssaultRifle_p.FX.MuzzleFlash_AssaultRifle_FX`, local space at MuzzleFlash | WEPMESH.MuzzleFlashes | CONF | **APPLIED**: Long (MuzzleFlash_Side_02, velocity-aligned, burst 10, life 0.15-0.2, 1.9-2.1 x 3.5-5 m, +1.9 m, 9 m/s), Top (smokeball_02 star), Sparks (SparksSheet 2x2 SubUV). Omitted: Glow_Mod (modulate), distortion ring, BackSteam/BackJet (alpha <= 0.05) |
| Tracer | `Tracer_AssaultRifle_FX`: Bolt (Bolt_ADD_MAT, PSA_Velocity, burst 1, life 0.6, 2 x 5-7 m, 15000 UU/s) + Trail2 smoke ribbon (Tracer_Smoke, life 0.9) | WEPMESH.TracerTemplates | CONF | **APPLIED**; bolt removed at the impact point; smoke ribbon = Tracer_Smoke_MAT opacity graph (width mask W, length mask L, cloud noise; Rendering matc, M08) **CONF graph**, **PARTIAL**: the second cloud sample is approximated by its mean and DynamicParameter desaturate is not applied (**PROV**); u = 0 at the trail head / hit end is **HIGH** (stock UE3 Trail2 fill) |
| Impact squib | `Impact_IonBlaster_FX`: GLOW_Dup (MuzzleFlash2, burst 10), Sparks_bolts (Spark_Tail, burst 20, 1.5-10 m/s), Smoke (SmokeThin, burst 4, x5 growth); rules 60 %, max 5 live, 25 m max distance | WEPMESH.DefaultSquib / TnWeaponMesh CDO | CONF | **APPLIED**; surface normal approximated by -shot dir (collision query returns no normal) |
| Effect colour | native colour constant `ff 33 19 ff` read as ARGB = (51,25,255) blue-violet; EnergonColor param (255,255,255,A=0) = no override; SwitchableColorScaleOverLife A/B chosen by Team | LOD streams, WEPMESH params, editor thumbnails | HI | APPLIED (team A) |
| FX module roles | WFC compiles modules into a native per-LOD stream; distributions decode exactly (type/op/n/chunk + BE float table), but **which module each belongs to is inferred from order** (Lifetime, StartRotation, ..., AlphaOverLife, StartSize, SizeMultLife, Velocity, ColorOverLife, Location) | stream analysis | MED | used as above; x4 / x2 overbright assignment MED |
| Fire cue | `BL_WPN_GUN_ION_BLASTER.SHOOT`: root -9 dB, Distance 4000-35000 UU, MaxConcurrent 4; layers by SOUND_DISTANCE: HEAD (<=200 UU), HANDCANNON_LR (400-4000), NRG_DISTANT (4000-8000), shell drops (+0.474 s) | cooked SoundCue / SoundNodeRoot / SoundNodeWaveEvent | CONF | **APPLIED** (generated table) |
| Low ammo / tail / impacts | SHOOT_LOW_AMMO (ammo <= 5), SHOOT_TAIL (WP_LoopingTail, 3000-12000 UU), IMPT_WORLD (-12 dB), IMPT_DMG (-9 dB, rolloff 2), max 6 | same | CONF | **APPLIED** |
| Reload / idle cues | ANIM_RELOAD_01 (3 events), ANIM_RELOAD_02 (12 timed events to 1.33 s, Distance 1000 UU, rolloff 2), IDLE_01/02 (-21 dB) | same | CONF | **APPLIED** via AnimNotifies (replaces the single provisional clip-reload wav) |
| Attenuation model | DistanceMin/Max + RolloffFactor = FMOD Ex inverse rolloff (banks ship as .fsb) | field names + FMOD banks | HI | APPLIED |
| SOUND_DISTANCE for own weapon | `kSmartPan_PreferPlayer`: parameter measured from the owning player | inferred from the name | MED | APPLIED |
| Concurrency | MaxConcurrentPlayCount -> steal the oldest instance | - | MED | APPLIED |
| Mixer categories / reverb | SFX_WET_COMBAT_ROBOT_WPN(_SHOOT) category levels, WET reverb sends, occlusion | not extracted | - | **not applied**; master level 0.5 still PROV |
| Dry fire | `BL_WPN_FOLEY.SHOOT_DRY_FIRE_ELECTRICITY` (WP_NoAmmoFire) | cue table present | CONF | not triggered (needs a trigger-on-empty event from PlayerController, owned by Gameplay) |

Open items for the Gameplay agent: the robot's idle base clip is currently `Cust_Idle` (the
customisation-screen showcase idle, first entry of category `idle`), which turns the body and
points the gun away from the aim; `NAV_Idle` / `Nav_Idle_Pose` is the gameplay idle. Upper-body
aim offset: applied in PASS 7b. Mesh yaw offset: see PASS 7b (should be +pi/2).

### PASS 7b — upper-body aim offset (Systems agent)
- **Space and axes verified from data:** recomputing the `Shooting_Aim_F_U` vs `Shooting_Aim_F_C` mesh-space
  delta in robot.glb reproduces the authored Spine01 CU rotation exactly (0.0926), and the Spine02 delta
  (0.318) is the sum of the Spine01 + Spine02 increments. The authored rotations are therefore **mesh-space
  increments applied in hierarchy order**. The UE -> glTF quaternion mapping is (x,y,z,w) -> (-x,-z,-y,w).
- **Tree order [CONF]:** the AimOffset node sits under the `UpperBodyCustom` slot's source (via
  TnAnimTurnInPlaceRotator "UnwindLowerBody"), so it is: locomotion -> aim offset -> reload slot -> recoil.
  A playing reload replaces the aimed spine/arm rotations, as in the original.
- **Pawn aim input [MED]:** X = 0 (the body already faces the aim yaw); Y = camera pitch / 90 deg (UE3 pawn
  aim convention), remapped piecewise-linearly through PawnAimOffsetRange -> profile range. The exact
  TnAnimNodeAimOffset remap and the active-profile choice (`WeaponTypeObserved`; Default for the Ion
  Blaster) are not decompiled.
- **Mesh facing bug found (Gameplay-owned, not changed here):** the skeleton faces **+X** in model space
  (eyes are +X of the head, left clavicle at -Z, gun forearm along +X in NAV_Idle, StrafeJog_F and
  Shooting_Aim_F_C), but `core::config::kMeshYawOffset = 0` renders the mesh as if it faced -Z. The robot
  is therefore drawn rotated 90 deg from the aim; measured barrel heading = aim - 97..102 deg. With the
  renderer's rotateY convention the correct value is **kMeshYawOffset = +pi/2**. The recoil aim frame
  now uses the measured +X forward, so it is correct either way.

### PASS 7c — shell, magazine and reload FX (Systems agent)
Spawned by the weapon AnimNotifies at their authored times and sockets (see the Event timing row).

| Effect / emitter | Original data | Conf | Rebuild |
|---|---|---|---|
| Shell_AssaultRifle_FX "Shell" (ShellSocket, every shot @0.005) | mesh `FX_GrenadeLauncher_p.GrenadeAmmo_STAT` (WEP_GrenadeLauncher_MATINST -> WEP_GrenadeLauncher_CLR), burst 1, life 1.0, StartSize (0.75,0.3,0.3), spin U[(-1,-1,-1),(5,5,1)] turns/s | CONF | **APPLIED** as a mesh particle |
| shell ejection velocity | not on the Shell emitter; the paired ShellGlow emitter (same socket) carries U[(300,-300,100),(1000,-300,300)] UU/s | MED | used for the shell mesh |
| Shell "SMOKE" | SmokeCoolDepth, burst 4, life U[0.5,0.75], size U[50,100] UU growing x3, alpha 0.1 -> 0, velocity U[(100,-300,-300),(300,300,300)] UU/s | CONF (role order MED) | **APPLIED** |
| Magazine_IonBlaster_FX "Shell" (MagSocket, reload @0.174) | mesh `FX_IonBlaster_p.IonBlaster_Mag_STAT` (IonBlaster_Mag_MATINST -> WEP_IonBlaster_CLR), burst 1, life 3.0, StartSize 1, spin U[-1,1] turns/s, velocity (200,300,300) UU/s | CONF (velocity role MED) | **APPLIED** |
| Magazine "Smoke_Dup_Dup_Dup" | as Shell SMOKE, alpha 1 -> 0, colour 1 -> 0.1 | CONF | **APPLIED** |
| Reload_AssaultRifle_FX "GLOW_Dup_Dup" (MuzzleFlash, reload @0.034) | flareball01 (smokeball_01), local space, burst 10, life U[0.2,0.5], size U[20,35] UU x (1.5 -> 0.1), alpha peak 0.2, brightness curve 20 -> 1 | CONF data / MED roles (size and velocity from uniform-curve tables) | **APPLIED** |
| Reload "Smoke_Dup" | SmokeCoolDepth, SpawnRate 20/s for 0.75 s following the muzzle, life U[0.5,0.75], size U[100,200] UU x3, alpha peak 0.25, grey 0.83-0.90, -50 UU behind the muzzle | CONF | **APPLIED** (continuous emitter) |
| Gravity / ground contact for the mesh particles | no acceleration or collision module authored | - | **PROV**: world pawn gravity (-29.4 m/s^2) and rest on the collision floor |
| Omitted | ShellGlow / GLOW (Glow_Mod_MAT modulate), Shimmer (distortion), BackSteam / BackJet (alpha <= 0.05), Blaster_Trail ribbons on shell and magazine, the "Trail" emitters | - | not rendered |

Asset loading: umodel `.gltf` + `.bin` meshes are now accepted by `assets::loadGlb` (text glTF branch).

### PASS 8 — vehicle boost presentation (Systems agent)
Source: `TR_Optimus_VEHDEF_p.OptimusTruckForm` (TnTruckFormBlueprint) and its
`HmPlayerVehicleAudioComponent_6670`; sockets from `character.json` (VH_OptimusPrime_SKEL); FX from
`FX_Navigation_p.bumble_boost_small1_FX` (cooked in A1_IAC_Base_m); cues from `BL_VEH_OPTIMUS_PRIME`
via `SoundEvents_Vehicles_Trans.Veh_Optimus_Prime_SoundSet`.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Boost FX binding | `BoostFx` = BoostSocket_L / BoostSocket_R -> `bumble_boost_small1_FX` | CONF | **APPLIED** |
| Boost sockets | BoostSocket_L on L_Robo23_XT (0,-35,0) UU yaw -90 deg; BoostSocket_R on R_Robo23_XT (0,35,0) yaw +90 deg: the two exhaust stacks behind the cab | CONF | **APPLIED** (bone x socket, UE -> glTF via vs_common) |
| Ignition burst (EmitterLoops 1) | "thruster": 3 Boostermesh_02 cones, life U[0.3,0.5], scale U[(4,1,2),(5,1,1)] x size curve 0 -> 2.06 -> 1, colour (1,0.6,0.05); "Cone_thrust_Dup": bulletshape mesh, life U[0.3,0.4], scale (50,37.5,15) x 21-entry growth, colour (5,1.5,0.05) x 3; "Particle Emitter_Dup": SphereGlow sprite 30 UU x growth, colour (5,1.5,0.05) x 3, +25 UU | CONF data / MED roles | **APPLIED** |
| Looping while held | "loopcone": Boostermesh_02 at U[3,4]/s after a 0.1 s first-loop delay, life 1.0, scale U[(4,1,2),(5,1,1)] x U[1,1.1], colour (1.07,0.59,0.16); "Particle Emitter_Dup_Dup": SphereGlow at 20/s after 0.2 s, life U[0.4,0.6], 30 UU growing to x3.08, spin U[-0.75,0.75] turns/s, colour (0.8,0.8,0.3) | CONF data / MED roles | **APPLIED** |
| Alpha over life | 0 -> 1 at 20 % of life, linear to 0 (every emitter) | CONF | APPLIED |
| Local space / deactivation | every emitter bUseLocalSpace + bKillOnDeactivate | CONF | **APPLIED**: particles ride the sockets (turning, jumping); release kills them at once |
| Materials | Boostermaterial_02_MAT (additive, two-sided; LightBeam_Falloff_01 + DiffClouds + Spot), bumble_boostcone_MAT (additive, SphereGlow_01 x 1.2), Basic_Particle_Add_MAT (additive, SphereGlow_01) | CONF | APPLIED with the primary texture only (LightBeam_Falloff_01 for the cones) **PROV** |
| HDR colour | colours > 1 (x3 colour scale) | CONF | approximated by hue-preserving normalisation + GL x2/x4 overbright **PROV** |
| Boost audio | BoostSound Auto_Boost_Start -> VEH_OPTIMUS_BOOST_START (5 timed events); BoostLoops Auto_Boost_Loop -> VEH_OPTIMUS_BOOST_LOOP (5 looping layers from 0.44 s, volume/pitch curves on `Optimus_Prime_Speed` (Max 120) + time envelopes); BoostStopSound -> VEH_OPTIMUS_BOOST_END; BoostWheelsSound -> VEH_OPTIMUS_BOOST_WHEELS (50 % ChanceToPlayNone) after BoostWheelsGroundCheckDelay 0.27 s if grounded; BoostFadeOutTime 0.15 s | CONF | **APPLIED** |
| Speed parameter units | `Optimus_Prime_Speed` in mph (curves put nominal pitch at 33 = 15 m/s cruise) | MED | APPLIED |
| START cue curves | VolumeCurve/PitchCurve on an event of a cue with no root SoundParameter | MED | fed the speed parameter |
| Cue loop region | root LoopStart/LoopEnd | - | not used; looping layers loop their whole wave |
| Boost activation | presentation follows the movement code's boost condition (vehicle form + boost held, not transforming), so stationary boost shows the effect | MED | the 0.3 s DashDuration-vs-sustained question belongs to Gameplay movement |
| Material / emissive changes on boost | none authored on OptimusTruckForm (only overshield / defrag materials) | CONF (absence) | n/a |
| Camera feedback | not on the truck form or its audio component; camera behaviours belong to Gameplay | - | not applied |
| Related | HoverFX / JumpFX: PASS 9. RamFX + nitro: PASS 10. Engine / jump / land audio: PASS 11 | CONF data | done (tire squeal open) |

Cue system extension (used by the boost cues): `bLooping` wave events, VolumeCurve/PitchCurve keyed by the
root SoundParameter (SOUND_DISTANCE or speed), Envelope volume/pitch curves over playback time,
ChanceToPlayNone, live parameter/position updates and fade-out stop. The cue table is now generated
by `tools/systems/gen_cues.py` into `src/game/SoundCues.inc` (weapon cues regenerated unchanged).

### PASS 9 — hover thrusters and jump boosters (Systems agent)
Same source object as PASS 8 (`OptimusTruckForm`): `HoverFX` (6 x HoverBooster_* -> `CarHover_A_01_FX`)
and `JumpFX` (JumpBoostSocket_C/R/L -> `Jump_FX`). Implemented in the generic `VehicleFx` (which now also
carries the PASS 8 boost tables unchanged).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Hover sockets | HoverBooster_LFront/RFront on L/R_Wheel01_XB (scale 3), _LBack/RBack on Wheel02, _LBack2/RBack2 on Wheel03 (scale 2/2.5/2.5); +-13 UU, rotation (180, +-90, 0): emission axis down/out from the wheels | CONF | **APPLIED** incl. socket scale (positions, sizes, meshes scale with the socket) |
| Hover looping emitters | Rings_Dup (Ring_Distort_Add -> Ring_CLR, 3/s + 1, life U[0.35,0.5], 120 UU shrinking to 0.5, spin, colour (1,0.5,0.25)); lightcone_Dup (Light_Cylinder_STAT mesh, 10/s, life U[1.5,2], scale U[(0.3,0.3,0.075),(0.25,0.25,0.1)] x 1 -> 1.2 -> 1, alpha 0.5, brightness flicker 0.8-1.33, colour (1,0.1,0.1) x 2; NOT bKillOnDeactivate) | CONF data / MED roles | **APPLIED** |
| Hover one-shot on activation | Sparks_bolts (Spark_MAT, burst 10 + 100/s for 0.3 s, velocity U[(600,-50,100),(1200,50,400)] UU/s, colour (2.5,2,2) x 3); ElectroRing (lightningring_01, 20/s for 0.2 s, colour (1,0.5,0.2) x 3); Pulse (Boostermesh_03, 3-4/s for 0.5 s, scale (0.8,4,4), colour (2,0.1,0.1)) | CONF data / MED roles | **APPLIED** |
| Hover active state | no flag authored; the audio component's Hover vs Boost/Wheels land sounds and the boost wheels peel-out imply: hover thrusters whenever in vehicle form except while boosting | MED | **APPLIED**: vehicle form, not boosting, not transforming |
| Jump FX | 12 emitters, all EmitterLoops 1 / 0.5 s (0.3 / 0.2 s for sparks / electro ring), bKillOnDeactivate: glow bursts (SphereGlow 80-120 UU x5, colour (4,2,1) x 3), booster smoke (20/s, 9-10 m/s down), Boostermesh_03 thruster streaks / bases (burst 3, growth 0 -> 3 -> 1), Boost_Circuit energon cones, Spark_MAT burst 30 + tail sparks, electro ring (6,4,2) x 3, all 50 UU below the socket (socket X points straight down) | CONF data / MED roles | **APPLIED** |
| Jump trigger | not in data; the vehicle jump (JumpLinearSpeed 12 m/s) | MED | take-off edge with vy > 2 m/s; ends early only if the vehicle form ends |
| Light cylinder material | LightCylinder_Rays_MAT_INST -> LightVolume_Base_MAT: view-dependent volumetric (SideViewV, NearFade, DepthBias, dust panners); instance DustPower 0.1 | CONF params | **PROV**: drawn with LightBeam_Falloff_01 x DustPower 0.1 (shader graph not evaluated) |
| Spark_MAT | no texture; procedural streak from texture-coordinate math | CONF (absence) | **PROV**: generated soft-streak texture |
| Rings trailing (150,0,0) | role undecided (location vs velocity) | MED | used as velocity (as an offset it puts rings 4.5 m from the wheels) |
| Omitted | hover base_glow (Glow_Mod_MAT modulate), rays_Dup (Trail_Distort distortion); every material's secondary panning cloud/energon layers | - | not rendered |

### PASS 10 — truck nitro / ram (Systems state + FX + audio; movement effect owned by Gameplay)
Source: TransGame.TnTruckForm compiled UnrealScript (TransGame.xxx) — function/state names and float
literals in the getters' bytecode — plus `OptimusTruckForm.RamFX` and the audio component / sound set.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Trigger | state **Driving** (on wheels = boosting): `UpdateNitro` starts the nitro on the **DASH** input (`_Dashing`) when the cooldown allows; `Driving.EndState` calls `StopNitro`; in state **Hovering** dash is a plain hover dash (`DoDash`) | CONF (script structure) | **APPLIED** in `VehicleNitro`: driving = vehicle form + boost held, not transforming |
| Nitro duration | `get_NitroDuration` = **3.0 s** x NitroDurationModifier (1.0) | CONF | **APPLIED** |
| Speed scale | `get_NitroSpeedScale` = **1.5** x modifier | CONF | **exposed** (`speedScale()`), **not applied** — Gameplay owns movement |
| Steering scale | `get_NitroSteeringScale` = **0.3** x modifier | CONF | **exposed** (`steeringScale()`), **not applied** — Gameplay owns handling |
| Cooldown | `get_TimeBetweenNitros` = **8.0 s** x modifier, starting on activation | CONF (native RE) | **APPLIED** |
| Max ram mass | `get_MaxRamMass` = 1000 | CONF | exposed constant (ram collision is Gameplay's) |
| StartNitro side effects | RamFX on RamSocket, NitroForceFeedback (3 s), nitro camera state | CONF | RamFX **APPLIED**; force feedback / camera not (no rumble path; camera = Gameplay) |
| RamFX | `Truck_ram_FX` on RamSocket (C_Body_XB (380,0,-40) UU, scale (1,1.5,1.5)): Ram_STAT wedge mesh, 20/s, life 1.0, alpha 0.35, colour (2,1.8,1.3), -250 UU; dust + rays are distortion (omitted) | CONF data / MED roles | **APPLIED** |
| Ram_model_MAT | emissive = 2 x (vertex colour x c)^2, c = saturate(pow(1-N.V, FresnelExponent 2) x FresnelScaleUp 1.5) x 2 x lerp(A x L1, L1, 0.4) over four panning Flame_Tile layers | CONF graph | fresnel rim applied per vertex (squared); panning layers = one static Flame_Tile **PROV** |
| Nitro audio | NitroSound Auto_Ram_Nitro -> `VEH_OPTIMUS_RAM_NITRO_START` (7 events); CustomLoopingSound Auto_Ram_Alert -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_ALERT` | CONF | **APPLIED** at nitro start; the alert plays once **MED** (component-level looping not decoded) |
| Ram impact audio | RamSound Auto_Ram_Impact -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_IMPACT` (from AttemptToRam) | CONF | hook `World::notifyRamImpact(pos)` for Gameplay's ram collision |
| BoosterSound | Auto_Ram_Boost -> `VEH_OPTIMUS_RAM_BOOST_START` | CONF mapping | in the cue table; trigger not decoded, not played |
| Dash input | abstract `platform::Button::Dash` | - | ~~**PROV** temporary key **Q**~~ → **Shift** in the merged build (Gameplay Pass 12 [CONF]: Shift = "Ability0 \| VehicleSpecialMove"); Q removed at integration/milestone-02 |

**Value conflict to resolve in Gameplay (documented, not changed here):** the rebuild's vehicle boost uses
`Default__TnHoverCarSimulationBlueprint` DashSpeed 5000 UU/s / DashDuration 0.3 s (core::config
kVehicleBoostSpeed / kVehicleDashTime). Optimus's truck actually references `VEH_SHARED_p.HoverTruck_Physics`
(TnHoverCarSimulationBlueprint) with **DashSpeed 3000 UU/s, DashDuration 0.5 s**, SuspensionRadius 185 UU,
and, for the Driving (wheels) state, `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint) with MaxSpeed
3000 UU/s, MaxAcceleration 2500, JumpLinearVelocity (600,0,1400), Mass 2500. The hover DASH is a separate
mechanic from the nitro (Hovering.DoDash vs Driving nitro). Systems did not modify any vehicle movement value.

### PASS 11 — vehicle engine audio (Systems agent)
Source: `OptimusTruckForm.HmPlayerVehicleAudioComponent_6670` and `Veh_Optimus_Prime_SoundSet`; cues generated
from `BL_VEH_OPTIMUS_PRIME` (tools/systems/gen_cues.py).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Drive loops | DriveSounds gears (MaxSpeed 20, 110): OnLoadLoops Auto_Engine_Gear_1_OnLoad -> `VEH_OPTIMUS_DRIVE_ONLOAD` (2 looping layers), OffLoadLoops -> `VEH_OPTIMUS_DRIVE_OFFLOAD` (3 looping layers incl. idle); one-shots map to None; ReverseSound maps to the same cues | CONF | **APPLIED**; both gears share the cues, so gear selection is audible only through the speed curves |
| Speed response | every layer's volume/pitch curves on `Optimus_Prime_Speed` (mph, Max 120) | CONF curves / MED units | APPLIED |
| On-load vs off-load | native component logic not decoded | MED | on-load while throttle input is held, off-load otherwise |
| Transitions | EngineFadeOutTime 0.2 s | CONF | APPLIED (outgoing loop fades 0.2 s) |
| Airborne | JumpRevSounds UseJumpRev, Auto_Jump_Loop -> `VEH_OPTIMUS_DRIVE_JUMP_LOOP` | CONF | APPLIED while airborne |
| Jump start | AscendSound Auto_Jump_Start -> `VEH_OPTIMUS_DRIVE_JUMP_START` | CONF | APPLIED on take-off |
| Landing | HoverLandSound {0.15 s: `HOVER_LAND_LIGHT`, 2.0 s: `HOVER_LAND_HEAVY`}, BoostLandSound {0.15 s: `WHEELS_LAND_LIGHT`, 2.0 s: `WHEELS_LAND_HEAVY`} by time in air | CONF | APPLIED (wheels variant while boosting); the robot-form placeholder thump no longer plays in vehicle form |
| Boost | the boost loop cue carries its own engine layers | MED | the drive loop yields to `VEH_OPTIMUS_BOOST_LOOP` while boosting |
| Tire squeal | TireSquealSoundParameter / Auto_Tire_Squeal_Default -> `VEH_OPTIMUS_TIRE_SQUEAL`, crossfade 0.5, TireSquealSpeedMin 20 | CONF data | **not applied** (needs a lateral-slip signal from Gameplay's vehicle handling) |

### VEHICLE MECHANICS — normal boost vs hover dash vs ram/nitro (consolidated, Systems checkpoint)
Three distinct mechanics in the original; do not conflate them. Values marked CONF are authored data or
compiled-script literals; "current rebuild" is what the movement code uses today (Gameplay-owned, unchanged
by Systems).

| Mechanic | Original trigger / state | Authored values | Current rebuild | Owner |
|---|---|---|---|---|
| **Normal boost** (drive on wheels) | holding Boost switches TnCarForm from state **Hovering** (HoverBlueprint) to **Driving** (CarBlueprint, wheels on the ground); `get_IsBoosting` / `set_BoostingInput`. Original PC binding: **right mouse in vehicle form** (same button as robot Fine Aim) | `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint): MaxSpeed **3000 UU/s (30 m/s)**, MaxAcceleration **2500**, Mass 2500, JumpLinearVelocity (600,0,1400). Presentation (CONF): BoostFx bumble_boost_small1_FX on BoostSocket_L/R; BOOST_START / BOOST_LOOP / BOOST_END cues, BoostFadeOutTime 0.15 s, BoostWheelsGroundCheckDelay 0.27 s | movement: Sprint (Shift) held, top speed `kVehicleBoostSpeed` 50 m/s reached over `kVehicleDashTime` 0.3 s (taken from the hover-sim **class default** DashSpeed/DashDuration, not Truck_Physics). Presentation: Systems PASS 8 | movement + binding: Gameplay; FX/audio: Systems |
| **Hover dash** | DASH input while **Hovering**: `TnTruckForm.Hovering.DoDash` -> TnHoverCarSimulation dash (`_DashTimeRemaining`, TimeBetweenDashes) | `VEH_SHARED_p.HoverTruck_Physics` (Optimus's actual hover sim): **DashSpeed 3000 UU/s (30 m/s), DashDuration 0.5 s**, SuspensionRadius 185 UU; class defaults (`Default__TnHoverCarSimulationBlueprint`): DashSpeed 5000, DashDuration 0.3, MaxLinearSpeed 1500, accel 3000, JumpLinearSpeed 1200, SuspensionRadius 200 | **not implemented as a separate mechanic**; its class-default values currently drive the normal boost (see above). Systems reads no hover-dash state | Gameplay |
| **Ram / nitro** | DASH input while **Driving** (boosting on wheels): `TnTruckForm.Driving.UpdateNitro` -> `StartNitro`; leaving Driving (`EndState`) -> `StopNitro` | script literals: NitroDuration **3.0 s**, NitroSpeedScale **x1.5**, NitroSteeringScale **x0.3**, TimeBetweenNitros **8.0 s**, MaxRamMass **1000**; StartNitro: RamFX (Truck_ram_FX on RamSocket), NitroForceFeedback 3 s, nitro camera state; NitroSound VEH_OPTIMUS_RAM_NITRO_START, CustomLoopingSound VEH_TRUCK_RAM_ALERT, RamSound VEH_TRUCK_RAM_IMPACT | state/timer/cooldown + RamFX + audio: **Systems** (`VehicleNitro`, abstract `Dash` action, PROV key Q); speed/steering scales **exposed, not applied**; nitro camera + force feedback not applied | state/FX/audio: Systems; speed, steering, handling, ram collision, camera, final binding: Gameplay |

Input notes: `Dash` is an abstract action (no PC binding recovered; temporary **Q**, PROVISIONAL). Right mouse is
deliberately left unbound by Systems so Gameplay can map it per the original: **robot form = Fine Aim,
vehicle form = Boost**.

### Native RE confirmation (Systems, 2026-10-01) — weapon cadence + vehicle states
**Ion Blaster cadence [CONF, native RE]:** one-shot refire timer reset to zero after each shot; fires when
elapsed **> 0.065 s**; fractional overshoot discarded; at most one shot per simulation tick. At 60 Hz this is a
shot every 4th tick = **900 RPM**, the original runtime cadence. `Weapon` now uses exactly this timer
(standalone check: 900 shots/min, every gap 4 ticks). Experimental's `systems-1-fire-interval-remainder.patch`
(923 RPM) is **not applied**: it would make the rebuild faster than the original.

**Vehicle states [CONF, native RE]** (movement math stays with Gameplay):

| State | Input | Behaviour | Values |
|---|---|---|---|
| Normal Boost | **LT / RMB held** | Driving (wheeled) behaviour; returns to Hover when released | top speed ~**3000 UU/s**, accel ~**2500**, special low-speed acceleration (Truck_Physics; TnCarForm CarLowSpeedBoost* modifiers) |
| Hover Dash | **RB / abstract Dash**, Hovering only | forward burst | **3000 UU/s** for **0.5 s**, **2 s** cooldown (HoverTruck_Physics DashSpeed/DashDuration; TimeBetweenDashes) |
| Ram / Nitro | **RB / abstract Dash** while Driving with Boost already active | top speed x1.5 (~**4500 UU/s**), steering x0.3 | **3 s**; **8 s** cooldown from activation; ends immediately when Boost is released; no authored ram animation (RamFX + nitro/ram cues are the presentation) |
| Ram collision | during Nitro only | **one hit per target per Nitro** | OptimusTruckForm: RamDamageToPlayerRobots **175**, ToAiRobots **300**, ToPlayerVehicles **175**, ToAiVehicles **300** (class defaults 50/100/50/100); ExtraRamZVelocity **7000 UU/s** (default 1000); MaxRamMass **1000**; all x TnTruckForm modifiers 1.0 |

Systems side: `VehicleNitro` (state, 8 s-from-activation cooldown, ends on Boost release, `registerRamHit`
one-hit-per-target gate, authored damage/momentum constants exposed) and `World::notifyRamHit(target, pos)`
(gate + ram impact cue). Not applied by Systems: speed/steering scales, hover dash, ram damage/impulse.
Input: LT/RMB = Boost (Gameplay mapping; robot form RMB = Fine Aim); RB = abstract `Dash` (temporary PC key **Q**,
PROVISIONAL). The Dash action is consumed by Systems only to start the Nitro while Driving; Gameplay reads the
same `Button::Dash` for the hover dash.

> **Integration note (integration/milestone-02):** the provenance above is unchanged. In the merged
> build, Gameplay Pass 12 implements all three mechanics in `CharacterMovement` (Driving 30 m/s /
> 2500, hover dash 30 m/s × 0.5 s with a 2 s cooldown, nitro 3 s ×1.5 speed ×0.3 steering with an
> 8 s cooldown, ending on Boost release). It also owns the Dash input: **Shift** / pad RB, latched in
> `PlayerController`. The Systems-side `VehicleNitro::update` timer and its `Q`/World Dash latch
> were replaced by `VehicleNitro::follow(vehicleState().nitroRemain > 0)`, so RamFX, the nitro cues
> and the ram-hit registry track Gameplay's single state machine. Boost presentation (afterburners,
> boost cues) follows Gameplay's Driving state. The values are identical on both sides, so
> nothing was re-tuned. Ram collision is not implemented in either branch; the `notifyRamHit`
> hook is unused.

---

## PASS 6 — PLAYER-CONTROL, ANIMATION & WEAPON PRESENTATION (2026-10-01)
Driven by replaying the exe (runtime observation overrides headless smoke). Priority order as
the player reported it.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Body facing | Robot faces the **aim/camera (mouse) yaw every frame**; WASD = move dir relative to it (strafe shooter). User confirmed: "camera facing = character facing; WASD = move direction." | Observed original + user | HI | **APPLIED** — `CharacterMovement` robot `setYaw(faceYaw)` always; removed the stand-then-walk snap. `face·toCam≈−0.9` verified. |
| Mesh yaw offset | ~~Extracted meshes align to the rebuild's −Z-forward yaw with no extra rotation.~~ **Superseded by Pass 7:** meshes face model +X; offset is +90°. | ~~Runtime geometry check~~ (only tested yaw math) | — | ~~`kMeshYawOffset=0`~~ → **+π/2** (Pass 7). |
| Vehicle facing | Faces its **travel direction** (steering), not the aim. | Observed | MED | **APPLIED** `yaw=atan2(-vx,-vz)` when moving. |
| Directional locomotion | `Nav_Strafe{Jog,Walk}_{F/B/L/R}` chosen by travel dir **relative to facing**. | `robot.glb` clip set (category `run`/`walk`) | HI | **APPLIED** (dot of velocity with facing fwd/right). Verified strafe-R → `Nav_StrafeJog_R`. |
| Upper-body aim offset | `Shooting_Aim_{F/L/R}_{C/D/U}` 9-pose grid points the gun at the reticle. Robot_ANIMTREE `TnAnimNodeAimOffset_14979`, profile **Default**: 11 bones (spine chain, head, both arms) x 9 authored rotations (L/C/R x U/C/D) baked from `Shooting_Aim_*`; ranges H [-1,1] V [-1,0.8]; RemapPawnAimRange from pawn H [-1,0.85] V [-0.7,1] | `robot.glb` category `aim`; `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF (data) / MED (remap) | ~~NOT YET~~ **APPLIED in Gameplay Pass 7/9** (the implementation in the merged build). Systems PASS 7b recovered the same profile independently (aim 22.9 deg -> barrel 21.6-23.8 deg); that implementation was not merged. |
| Transform pairing | Robot & vehicle transform clips share a duration (ToVehicle **1.97 s**, ToRobot **1.13 s**) — one fold authored per mesh, played **in sync**. | `robot.glb`/`vehicle.glb` clip durations | HI | **APPLIED** — outgoing mesh → midpoint → partner mesh resumed at same normalized time (was sequential = the "crack"). Weapon holstered through the fold. |
| Transform cross-fade point | exact visibility/alpha handoff curve | — | GUESS | `kTransformHandoffFrac=0.5` **PROVISIONAL**; cross-mesh pop minimised, not removed. |
| Muzzle origin | Ion Blaster **barrel tip** (MuzzleFlash socket). Socket transform not extracted; used geometric tip. | `weapon.glb` frontmost vertex slice | CONF-derived | **APPLIED** — weapon-local (2.063,0.017,0.141) m → +2.07 m forward of the hand; tracer+flash leave the barrel. |
| Reload anim | `Shooting_Reload_IonBlaster_ROBO` (full-body); `WeaponReloadAnimTime` 1.5 s (clip 1.633 s). | `robot.glb` category `reload`; `weapon.json` | CONF | **APPLIED** full-body one-shot while reloading. Additive `ADD_Shooting_Reload_*` (reload on the move) = PARTIAL. |

**Lighting (#6/#7/#8) — superseded by PASS 7 (original render path).** World still uses only **coeff0**
of the 3-coefficient directional baked lightmap (fixed-function can't apply the directional basis
per-pixel); character uses a provisional `GL_LIGHT0` not sampled from the world. True fidelity
(3 coeffs · normal, gamma/sRGB, env-probe character lighting) needs a GL2+ shader path — a large,
isolated effort, not cut into this pass to avoid leaving the build broken.

---

## PARAMETER TABLE (recovered so far)

| Property | Original value | Source | Conf | Rebuild status |
|---|---|---|---|---|
| World gravity (pawn) | DefaultGravityZ **−2940 UU/s²** = −29.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo]` | CONF | **APPLIED** (was 22) |
| RB physics gravity scale (vehicles) | **0.66** → −19.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo] RBPhysicsGravityScaling` | CONF | applied to vehicle form |
| Transformation blend-in | **0.115 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendInTime` | CONF | **APPLIED** (crossfade) |
| Transformation blend-out | **0.25 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendOutTime` | CONF | **APPLIED** (crossfade) |
| Camera default FOV | **75° horizontal** (SmoothTime 0.4) | `Xe-TransCamera.ini [AnimatedFovCameraBehavior TnFovCameraBehavior] DefaultFOV` | CONF | **SUPERSEDED (Pass 11):** robot strategy instance OverTheShoulder TnFovCameraBehavior DefaultFOV 80 |
| Fine-aim (ADS) FOVs | 35 / 45 / 55 (close/med/far POI) | `Xe-TransGame.ini [TnPointOfInterest]` | CONF | **SUPERSEDED (Pass 11):** these are point-of-interest focus FOVs; fine aim uses TnPCS_FineAim FOV 45 (applied) |
| Fine-aim ground-speed mult | **0.5×** | `Xe-TransGame.ini [TnFineAimManager] _GroundSpeedMultiplier` | CONF | **SUPERSEDED (Pass 11):** APPLIED via SetSpeedMultiplier |
| Camera pawn-cylinder padding | R=30, H=30 UU | `Xe-TransCamera.ini [TnCamera]` | CONF | n/a (no camera collision yet) |
| Camera pawn fade start | 200 UU | `Xe-TransCamera.ini [TnCamera] PawnFadeStartDistance` | CONF | not yet |
| Ion Blaster fire interval | **0.065 s**; runtime cadence **~900 RPM** (one-shot timer reset to 0 per shot, fires when elapsed > 0.065 s, overshoot discarded, max one shot per tick) | `weapon.json` + native RE | CONF | **APPLIED** (Systems native-RE pass); Experimental's 923-RPM remainder patch deliberately NOT applied |
| Ion Blaster damage | **15** (InstantHit) | `weapon.json gameplay.InstantHitDamage` | CONF | correct ✓ |
| Ion Blaster magazine | **50** | `weapon.json MaxAmmoClipCount` | CONF | correct ✓ |
| Ion Blaster max reserve | **250** | `weapon.json MaxAmmoCount` | CONF | correct ✓ |
| Ion Blaster initial reserve | **150** | `weapon.json InitialReserveAmmoCount` | CONF | **FIXED** (was 250) |
| Ion Blaster reload time | **1.5 s** | `weapon.json WeaponReloadAnimTime` | CONF | **FIXED** (was 1.8) |
| Ion Blaster range | **30000 UU = 300 m** | `weapon.json WeaponRange` | CONF | **FIXED** (was 400) |
| Ion Blaster damage falloff | 1.0× ≤5000 UU → 0.5× @30000 UU | `weapon.json RangeDamageModifiers` | CONF | **APPLIED** |
| Ion Blaster per-shot spread | 0.08→0.18, +0.005/shot, 2.0 s cooldown | `weapon.json PerShotSpreadModifier` | CONF | **APPLIED** |
| Ion Blaster fine-aim spread | 0.5× | `weapon.json FineAimSpreadModifier` | CONF | **SUPERSEDED (Pass 11):** APPLIED |
| Ion Blaster equip/putdown | 0.2 / 0.5 s | `weapon.json EquipTime/PutDownTime` | CONF | not yet |
| Weapon socket | WeaponSocket_Primary, bone R_Arm03_Elbow_XB | `character.json sockets` | CONF | attached (rotation approx) |
| Robot ground speed | **550 UU/s = 5.5 m/s** | `TransGame.xxx Default__TnPlayerPawn.GroundSpeed` | CONF | **SUPERSEDED (Pass 11):** 14 m/s (Optimus_ROBODEF BaseGroundSpeed 1400) |
| Robot accel | **2048 UU/s² = 20.48 m/s²** | `Engine.xxx Default__Pawn.AccelRate` | CONF | **SUPERSEDED (Pass 11):** 120 m/s² (ROBODEF AccelRate 12000) |
| Robot air control | **0.70** | `Default__TnPlayerPawn.AirControl` | CONF | **SUPERSEDED (Pass 11):** 0.4 (ROBODEF) |
| Robot air speed | **1500 UU/s = 15 m/s** | `Default__TnPlayerPawn.AirSpeed` | CONF | **SUPERSEDED (Pass 11):** 12 m/s (ROBODEF AirSpeed 1200) |
| Robot max jump height | **625 UU = 6.25 m** | `Default__TnPawn._WorkingMovementCapabilities.MaxJumpHeight` | CONF | **SUPERSEDED (Pass 11):** 5.0 m (SharedAcrobatics.JumpHeight 500; JumpZ formula from ApplyTransformer; measured 5.12 m) |
| Pawn cylinder radius | **175 UU = 1.75 m** | `Default__TnPawn._WorkingMovementCapabilities.CylinderRadius` | CONF | **SUPERSEDED (Pass 11):** 2.0 m (Optimus_ROBODEF Collision) |
| Pawn cylinder half-height | **200 UU = 2.0 m** (full 4.0) | `…CylinderHeight` | CONF | **APPLIED** |
| Pawn base eye height | **80 UU = 0.8 m** (above centre → 2.8 m above feet) | `Default__TnPawn.BaseEyeHeight` | CONF | **SUPERSEDED (Pass 11):** Default__TnTransformer.BaseEyeHeight 150 → eye 3.5 m (trace origin); camera anchor 4.0 m (strategy Offset Z 200) |
| Robot walk pct | 0.5 (walk = 0.5× ground) | `Engine.xxx Default__Pawn.WalkingPct` | CONF | n/a (keyboard jogs) |
| Robot max fall speed | uncapped in movement | `…_WorkingMovementCapabilities.MaxFallSpeed=100000` | CONF | no terminal cap |
| HeightFog colour | **(234,91,116)** warm red-pink | `MP_IAC_Streets_ART_m HeightFogComponent.LightColor` | CONF | **APPLIED** (was blue-grey) |
| HeightFog density / start | 2e-5/UU (0.002/m) / 2048 UU | `…HeightFogComponent.Density/StartDistance` | CONF | APPLIED (GL_EXP, softened) |
| Vehicle max speed | **1500 UU/s = 15 m/s** | `Default__TnHoverCarSimulationBlueprint.MaxLinearSpeed` | CONF | **APPLIED** (was guessed 32) |
| Vehicle acceleration | **3000 UU/s² = 30 m/s²** | `…MaxLinearAcceleration` | CONF | **APPLIED** |
| Vehicle dash/boost speed | **5000 UU/s = 50 m/s** | `…DashSpeed` | CONF | **SUPERSEDED (Pass 11):** 30 m/s (HoverTruck_Physics DashSpeed 3000) |
| Vehicle dash duration | **0.3 s** | `…DashDuration` | CONF | **SUPERSEDED (Pass 11):** 0.5 s (HoverTruck_Physics) |
| Vehicle hover height | **200 UU = 2.0 m** | `…SuspensionRadius` | CONF | **SUPERSEDED (Pass 11):** 1.85 m (HoverTruck_Physics SuspensionRadius 185) |
| Vehicle jump speed | **1200 UU/s = 12 m/s** | `…JumpLinearSpeed` | CONF | APPLIED |
| Vehicle turn rate | **~π rad/s** (180°/s) | `…AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AI-only field; player steering rate PROV |
| Vehicle terminal velocity | 8000 UU/s = 80 m/s | `Default__TnTransformer.TerminalVelocity` | CONF | n/a (fall uncapped) |

---

## STREET COLLISION (RECOVERED this pass)
- **Authored collision participation recovered from props.json:** of 1952 placed static
  meshes, **1693 BLOCK** (CollideActors+BlockActors true) and **259 do not** (decorative:
  deco spheres, etc.). These flags come from the original ART/BASE actor/component properties.
- **UE3 collision model:** the player cylinder (non-zero extent) collides against each static
  mesh's collision. Default `UseSimpleBoxCollision=true` routes the player to the mesh's
  simplified BodySetup collision; architectural meshes commonly set per-poly. We reproduce the
  **per-poly path** (render geometry of blocking props) — faithful for the structural
  floors/walls/ramps that use it, an over-approximation for small simple-collision props.
- **Extraction extended:** new `AssetTools/scripts/wfc/vs_collision.py` regenerates
  `collision.glb` = pristine base (BSP-solid + blocking-volume hulls, kept as
  `collision_base.glb`) **+ 1693 blocking props** (instanced; file 5.46 MB → ~1.85 M world
  collision tris baked at load). Non-colliders excluded; oversize guard (>2000 m) excludes
  skydome/background shells (0 hit).
- **Verified:** player stands on the street floor at the authored FFA spawn (Y −724.5) and at
  4 spawns spanning ~180×164 m; auto-walk is now correctly **blocked by building walls** it
  previously passed through. Collision bounds Y [−800..−329].
- **Remaining:** simple-collision (BodySetup convex/box) extraction for props that use it;
  vehicle RB collision channel; moving-platform (InterpActor) dynamic collision (baked at rest
  position); exact MaxStepHeight/slope (pawn exe defaults).

## MAP / WORLD (investigated)
- `world.glb` is composed from **all three sublevels** (ART/AUDIO/BASE): BSP 2460 tris
  (ART only) + **1952 placed static meshes** + spawns/objectives. It is NOT a thin subset.
- Collision extent spans ~740 m (X) × 451 m (Z) in gltf metres (BSP-solid + blocking-volume
  convex hulls). Auto-walk forward from the FFA spawn is blocked by a wall after ~23 m — a
  building/volume ahead, not the map edge.
- **Known gaps (why it can look smaller/simpler than the original):**
  1. **Static-mesh collision not extracted** (`collision.json` note) — the 1952 prop meshes
     have no collision, so floors/ramps built from static meshes are not walkable and some
     structures are pass-through. Highest-value map task: build collision from prop render
     meshes where `collide/block=true` (data is in `props.json`).
  2. **PrefabInstance (16)** and **HeightFog** actors are not composed into `world.glb`
     (unhandled in `vs_map.py`). Prefabs hold modular set-dressing; missing them thins detail.
  3. Lighting is **baked into lightmaps** (StaticLightCollectionActor) which were **not
     extracted** — the single biggest reason the scene reads flatter than the original.

## LIGHTING / MATERIALS (investigated + partially fixed)
- Washed-out cause identified: the fixed-function renderer used global-ambient 0.35 + light-
  ambient 0.35, flooring every surface at ~0.70 brightness (no contrast). **Fixed [PROV]:**
  ambient lowered (global 0.14 / light 0.10), warm directional key, separate **specular**
  term (metallic highlights).
- **HeightFog RECOVERED [CONF]** from `MP_IAC_Streets_ART_m HeightFogComponent_11010`:
  LightColor **(234,91,116)** warm red-pink, Density **2e-5/UU = 0.002/m**, StartDistance
  2048 UU. Applied (GL_EXP, density softened to 0.0014/m for the play-area scale; clear colour
  warmed to match). This replaced the earlier **guessed blue-grey** fog.
- **BAKED LIGHTMAPS — FULLY RECOVERED, DECODED, AND RENDERED [CONF].** Streets is now lit by
  its original authored baked lightmaps instead of the stand-in directional/ambient.
  - **Serialization CRACKED (file_version 511, licensee 144).** After a component's tagged
    props the native `FStaticMeshComponentLODInfo` block is **big-endian**:
    `… [LightMapType=2] [LightGuids.Num: byte] [LightGuids: Num × FGuid(16)]`
    `[lead int32=0] 3 × ([tex: BE int32 export index][ScaleVector: 3 BE floats][1.0])`
    `[CoordinateScale: 2 BE floats] [CoordinateBias: 2 BE floats]`.
    It is a **directional lightmap** (3 coefficient textures); LightGuids[0] is a shared
    dominant-light GUID. The **texture is a positive export index into the ART package's own
    export table** (a seekfree forward-export whose name matches the `_LM` atlas) — this solved
    the "no imports / GUID" blocker.
  - **Values recovered:** per prop instance — atlas (coeff-0 `LightMapTexture2D`),
    CoordinateScale (e.g. 0.0625 = 1/16), CoordinateBias, and the coeff-0 ScaleVector (HDR).
    `AssetTools/scripts/wfc/vs_lightmap.py` parses all **1793 lightmapped components** and joins
    them to props (**1755/1952 props → lightmap**, 20 atlases used). Atlases decoded to PNG via
    umodel (1024² DXT1) in `…/MP_IAC_Streets/lightmaps/`.
  - **Rendering:** `vs_map.py` now emits TEXCOORD_1 + per-instance lightmap node extras;
    `world.glb` carries them; the loader attaches them to submeshes; the renderer draws
    lightmapped submeshes **unlit** then **multiplies by the atlas** (blend DST_COLOR·ZERO,
    UV1 via a texture matrix = uv1·CoordinateScale + CoordinateBias), with the **HDR ScaleVector
    applied via GL_COMBINE RGB_SCALE 4×**. 1962 submeshes lit. A/B toggle: `WFC_NOLIGHTMAP=1`.
  - **Verified** across spawn 0 / region 6 / region 18: correct per-region baked shadows and
    coloured bounce (purple, green/teal, warm), high contrast, bright lit doorways; no wrong-
    atlas / UV-flip / seam artefacts. Colour space: textures sampled as-is (sRGB-ish), modulate
    then HDR-scale — matches WFC's moody baked look.
  - **Remaining [PROV]:** only coeff-0 of the 3 directional coefficients is used (no per-pixel
    normal reconstruction — a shader refinement); ScaleVector >4 clamps (rare); BSP surfaces
    have no static-mesh lightmaps (BSP lightmaps are a separate path, not yet done).
  - **Pipeline ordering:** run `vs_lightmap.py` → `vs_map.py` (writes world.glb + base
    collision.glb) → `vs_collision.py` (restores full prop collision). vs_map resets
    collision.glb, so vs_collision must run last.
- **Emissive RECOVERED + APPLIED [CONF]:** Optimus's authored emissive textures
  (`textures/*_emissive.png`, the glow mask — blue optics/energon/Autobot vents on black) are
  loaded (derived as the `_basecolor`→`_emissive` sibling) and drawn as an **additive
  self-illumination pass** (GL_ONE/GL_ONE, unlit, depth-write off). Result: Optimus's eyes and
  energon details glow blue (iconic WFC look), from original data, not invented. Applies to
  robot/vehicle/weapon. [PROV] emissive *intensity* scale not recovered (drawn at 1×).
- Still missing: **normal/specular maps** (roles extracted; need a programmable GL path),
  map/environment emissive (different naming), tone-mapping / bloom / DOF.

## CONFIRMED ORIGINAL (authored data)
- Streets map = three sublevels **BASE** (gameplay: 24 FFA + 60 team starts, 58 blocking
  volumes, pickups, objectives) + **ART** (visual: BSP 2460 tris, 34 StaticMeshActors,
  16 PrefabInstances, 25 decals, HeightFog) + **AUDIO** (40 AmbientSound + 30 Hm spatial
  emitters). Source: `maps/MP_IAC_Streets_*_m.json`.
- Gravity, transform blend times, camera FOV, Ion Blaster stats — see table.

## HIGH-CONFIDENCE RECONSTRUCTION
- GLB skinning / animation sampling; material base-colour; collision ground query.

## PARTIALLY CONFIRMED
- Vehicle movement: forms (TnTruckForm/TnCarForm) are **modifier** layers (=1.0) over
  compiled base values; WFC vehicles use RB thruster/suspension/hover physics. Base
  constants live in `default.xex` — NOT yet recovered.
- Map completeness: 1952 props + BSP composed, but **PrefabInstance (16)** and **HeightFog**
  not yet composed; **static-mesh collision not extracted** (only BSP-solid + blocking hulls).

## WEAPON SOCKET (RECOVERED this pass)
- `WeaponSocket_Primary` relative transform (character.json): loc_ue [-40,0,0],
  rot_ue [pitch 0, yaw 31311 = 172°, roll 5461 = 30°]. Converted to a gltf-space socket
  matrix (via `vs_common.ue_rot`/`ue_to_gltf_matrix`) and applied — the Ion Blaster now holds
  with the correct orientation (barrel forward along the arm), not translation-only.

## CAMERA (partial)
- Recovered: pivot/eye height 2.8 m [CONF] (CollisionHeight 200 + BaseEyeHeight 80 UU).
  Flying-cam pitch range ±60°, first-person rotation ±45°, flying AnchorOffset [0,0,220 UU].
- STILL [PROV]: ground third-person **follow distance** — WFC selects it from an orbit-distance
  list (`TnLocationOffsetCameraBehavior.CurrentOrbitDistanceIndex`); the distances live in a
  camera data asset / `DefaultCamera.ini` that was not extracted. Keeping 9 m. Mouse
  sensitivity, ground-cam pitch limits still provisional.

## VEHICLE PHYSICS (RECOVERED this pass)
- WFC ground vehicles **hover**; Optimus's truck uses **`Default__TnHoverCarSimulationBlueprint`**
  (TransGame.xxx). The `TnCarForm`/`TnTruckForm` CDOs only carry `=1.0` modifiers; the base
  values live on the *Simulation* blueprint classes (which I'd missed earlier). Recovered +
  applied: max speed 15 m/s, accel 30 m/s², dash/boost 50 m/s (0.3 s burst), hover height 2 m,
  jump 12 m/s, turn ~π rad/s, terminal 80 m/s. Verified: vehicle hovers 2 m above the street
  and cruises ~13.6→15 m/s. (Non-hover `TnCarSimulationBlueprint` = MaxSpeed 500 UU; unused.)
- Remaining: reverse speed + braking/damping constants (via LinearDamping, not a single value);
  per-axis turn rate; exact dash-as-impulse vs the accel-ramp approximation; velocity transfer
  at transform (currently zeroed).

## PROVISIONAL / APPROXIMATE (rebuild guesses — NOT evidence)
- ~~Camera follow distance (9 m); ground-cam pitch limits~~ → 8 m / ±75° CONF (Pass 11). Mouse sensitivity still PROV.
- ~~MaxStepHeight (kStepUp 0.6 m)~~ → 0.35 m CONF (Gameplay Pass 10, TnRobotForm; Systems also found no override of the Engine.Pawn default 35 UU).
- Audio master level 0.5 and mixer-category / reverb treatment (weapon cue radii now CONF, Systems PASS 7; non-weapon transform/land cues still generic falloff).
- Locomotion crossfade time (0.15 s) for non-moving transitions (idle↔moving is CONF 0.2 s, Pass 10).

## AUDIO (investigated + fixed)
- Original Streets audio = 40 `AmbientSound` + 17 `HmAmbientSoundVolumeEmitter` +
  13 `HmAmbientSoundLineEmitter` (spatial bed) with SoundCues; weapon/transform cues are
  FMOD SoundCues. Exact per-cue attenuation radii/reverb not yet extracted.
- "Too loud / not spatial" cause: the mixer played every cue at full volume, mono, no
  attenuation. **Fixed:** master level 0.5 [PROV], and a 3D path (distance attenuation +
  equal-power stereo pan vs the camera listener) via `IAudio::playAt`/`setListener`. Fire is
  positioned at the muzzle; land/transform/reload at the pawn.
- Remaining: the ambient emitter bed, reverb (MASTER_WET zone presets), interior/exterior treatment,
  occlusion. Cue radii/falloff/variation/concurrency, per-cue SmartPan and owner attachment are done
  (see MILESTONE 03 SYSTEMS).

## NOT YET IMPLEMENTED
- Normal/specular/emissive materials; lightmaps/baked lighting; HeightFog; post FX (bloom/DOF).
- ADS/fine-aim mode; camera recoil/shake; per-shot spread visualization.
- Prefab geometry; static-mesh collision; spatial/reverb audio.

---

## OPEN QUESTIONS → EVIDENCE NEEDED
- Robot GroundSpeed/JumpZ/AccelRate: read `TnPawn`/robot-form class defaults in `default.xex`
  (Ghidra/ReVa) or measure root motion from `Nav_*` clips.
- Camera follow distance/offset: HM camera behavior assets (cooked packages) or exe.
- Vehicle base speed/accel/turn: `TnCarForm`/`TnTruckForm` compiled defaults in exe.
- Lighting: lightmaps not extracted; dominant directional light direction/colour from map.
- Remaining unimplemented abilities (in no iconic preset; class pools only): DecoyTrap, HardLock, Disguise, AbilityJammer,
  TransformDisruptor, MarkTarget … are listed per slot and reported unimplemented [PARTIAL].