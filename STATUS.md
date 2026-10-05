# WFC Rebuild — Playable Vertical Slice Status

_Updated as work proceeds. Build: `powershell -ExecutionPolicy Bypass -File build.ps1`
→ `build/bin/wfc_rebuild.exe`. Fidelity audit + provenance: `FIDELITY.md`._

## INTEGRATION MILESTONE 07 (2026-10-05) — the selected character, end to end — branch `integration/milestone-07`

**Playtest executables (plain launch, no environment variables):**
- Release: `F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe`
- Debug: `F:\Transformers Rebuild\Rebuild\build\bin\wfc_rebuild.exe`

Render data: `F:\Transformers Rebuild\Rebuild\work\render` (regenerated with the final Rendering tools for the
standard set plus all 10 MP maps).

### Lane heads merged (newest stable pushed heads; Experimental validation only)
| lane | head | content in this milestone |
|---|---|---|
| agents/frontend | 4982493 | 4b2fae1 / df79c6f preview pawn visibility, grounding, GFx collector crash fix; 79b47cd preview colours (real palettes, random slot colours, Change Form, Cust_Idle); 8f4c729 / 89eddba selection contract + full CharacterSelection; 1af7e74 colour picker input (RegisterLeftStickCallback); 2c2f5cc preview reset on leaving the room, customize soak; 4982493 per-movie GFx collection, preview body LRU |
| agents/gameplay | 1216e80 | Pass 22a: the selected chassis spawns from the AssetTools per-chassis export (27 MP chassis), no Optimus substitution; spawn refused + logged if a body cannot be built; TnSpecialty health / speed |
| agents/rendering | 833daa3 | M15–M25: preview pose (Cust_Idle), sceneGroundHeight, lightmap records + FColor channel order, TextureSetSample, vertex lightmaps on every map, scene texture, PostProcessVolume grades + CLUT on the player route, releasePreviewBody |
| agents/systems | e37c489 | M08: every MP map's Kismet audio graph, character / weapon audio profiles, objective / round messages, movie volume |
| agents/experimental | 734bde4 | not merged (presentation gate used for validation) |

**Recorded for the next integration (after the freeze):**
- agents/gameplay 44cb835: vehicle forms, 52-weapon loadouts, per-chassis hulls, CTF / EXT, 5-argument look settings;
- agents/frontend 92dd833 / 3c2febd / 3c0c1f9:
  - popup closed on travel;
  - look settings → Gameplay;
  - WFC_PERSISTENT_RENDERER, inert by default. Keep the recreate default until match uploads are released.
- agents/rendering after 833daa3: matc Panner float3 fix (the Debris compile error).

### Conflicts and resolutions
| merge | files | resolution |
|---|---|---|
| rendering c22e356 / 309facc / a76540e / 833daa3 | FIDELITY.md | both kept |
| systems 5bf5731 | gen_level_audio.py, audio_native_suite.cpp, LevelAudio.inc | Systems' version (it now enumerates every MP map itself, superseding the M06 integration list); the regenerated LevelAudio.inc is byte-identical to Systems' |
| gameplay 1216e80 | World.cpp, Application.cpp, STATUS.md | Gameplay's per-chassis body (applyChassisToLocalPawn, member texture cache) on the integration multi-map loader (Maps/<mapName_>, loading-screen yields kept, resolveTexture yields); WFC_MATCH keeps the loaded map and takes WFC_CHASSIS |
| frontend 89eddba | Application_Frontend.cpp | Frontend's fillFullSelection is the one selection mapping (the integration forwarding is dropped) |
| systems e37c489 | World.cpp, FIDELITY.md, STATUS.md | integration map-audio load + Systems' validation hooks; both includes |

**Integration seams (code written here):**
- On the local spawn, the selection's colours for the spawned faction go to the renderer (draw owner 0, TnCharacterApplier Cust_Color_A / Cust_COLOR_B; black stays the material default). Trace: `match.pawnBody`.
- `World::applyChassisToLocalPawn` selects Systems' character audio profile for the spawned chassis (log `character audio: Car (CHR_BUMBLEBEE / Veh_Bumblebee_SoundSet)`).

### Create a Character → match, end to end (cold boot, real menus, frames)
| run | path | result |
|---|---|---|
| e2e1 (Release, 60 Hz, intro) | intro → title → Multiplayer → Create a Character → Scout → Autobot chassis Speedster → **Runner** (Car, Bumblebee) → colour picker (RT palette, cursor, Accept) → back ×3 (saved) → Private Match TDM **Streets** → choose Scout → spawn → move / fire / transform → scoreboard → pause / resume → Quit → confirmation → party lobby → TDM **Seed** → same → party lobby → title | selection `chassis=Car body=available`; spawned body `Car` with colours 26,88,1 / 139,239,100 on both maps; audio CHR_BUMBLEBEE; 841 / 841 frame checks PASS |
| e2e2 (Release, **144 Hz**, boost) | starts from e2e1's saved characters (**persistence: Scout still Car**) → Soldier → Defender (Tank3, Warpath; the next Autobot soldier is campaign-locked, as in the original) → colour → TDM **Berth** → **Gorge** → title | spawned `Tank3` with the picked colours on both maps; audio CHR_WARPATH; 804 / 804 PASS |
| map chain ×2 (Release) | Streets, Seed, Berth, Gorge, Complex, Rust, Orbital Debris, Molten, Streets, then the same eight again | 17 / 17 matches with the selected body (default Scout = Car2, Sideswipe); 0 spawns refused; 3334 / 3334 PASS |

Preview vs match: the same roster mesh, the same chassis id and the same colours (preview: Frontend's sRGB→linear;
match: the same conversion). Verified side by side for Runner (green) and Warpath (red / yellow).

### Memory (private MB at match load / after unload; per-match renderer reset is the default)
| build | sequence | loaded | after unload |
|---|---|---|---|
| Release ×2 cycles | Streets, Seed, Berth, Gorge, Complex, Rust, Debris, Molten, Streets, … , Streets | cycle 1: 2379, 2910, 2389, 2623, 2697, 3232, 2786, 2976, 2774; cycle 2: 3132, 2602, 2751, 2805, 3220, 2783, 2927, 2804 | Streets 2227 → 2678 → 2685; Rust 3044 → 3073 |
| Debug | Streets, Seed, Streets, Seed, Streets | 2417, 3022, 2608, 2995, 2606 | 2325, 2813, 2516, 2814, 2546 |

- **Pattern:** a high-water rise during the first cycle through new maps (peak at Rust), then a plateau. Second-cycle loads are within ±50 MB of the first-cycle values (Rust 3232 → 3220, Debris 2786 → 2783, Molten 2976 → 2927; Streets 2774 → 2804).
- **Verdict:** no accumulating map leak (HIGH CONFIDENCE).
- **Cross-check:** Frontend's census (3c0c1f9) agrees. With the per-match renderer reset, GL textures stay bounded (+1 per new map, the map thumbnail). A persistent renderer would grow by ~60–100 textures per match until match uploads are released (next integration).

### Automated results (final binaries)
| suite | result |
|---|---|
| Debug / Release clean build | exit 0 / exit 0 |
| Frontend tests | 79 / 0 (Debug and Release) |
| Fidelity harness | 191 pass, 0 FAIL, 22 known (Debug and Release) |
| Gameplay CHASSISTEST | 13 / 13 (Debug and Release) |
| TDM / mode play | 43 / 43; 21 / 21 |
| Transform stress | 0 / 1520 under the map |
| Chaos | 0 under the map / 0 KillZ / 0 stuck |
| Camera jitter (60 / 144 / 240 Hz) | 0.0003 / 0.0003 / 0.0002° |
| Systems audio suite | 599 / 0 (the suite now needs CharacterAudio.cpp) |
| Movie audio probe | OK |
| Plain launches | original path; withheld render data → error + VISUALCHECK FAIL |
| Rendering release_path_check | PASS: Streets / Berth / Streets, noDepth 0, glErr 0, both Streets visits identical (1639 world / 357 BSP) |
| Experimental presentation gate (734bde4, Debug) | 22 pass / 6 fail / 2 partial / 1 unknown (was 20 / 7 / 2 / 1 / 1 skip at 06c). charselect.body_resolves now PASS; route vs direct now runs and passes. The 6 FAILs are retired stale expectations (Quit → main menu; keyboard rebinding; Back from Create a Character → main menu ×2; preview "model loaded" log pattern — frames show the posed preview bodies) |
| Rendering visual suite vs the M06c reference | 7 / 11. The 4 differences are intended: M21 FColor order + M25 grade (Streets refdiff 0.04–0.21), Sideswipe instead of Optimus in the route frames, title grade. Draw counts unchanged |

### Remaining visible discrepancies (top)
1. Every chassis still holds the Ion Blaster: per-chassis loadouts, vehicle forms (car roll, tank, jet flight) and abilities are in Gameplay 44cb835, next integration.
2. Ion Blaster tracer smoke draws as hard-edged grey slabs. Systems' WeaponFx ribbon lacks the original Tracer_Smoke_MAT width mask; reported to Systems.
3. Title / customization scenes are more desaturated since M25: UI_FrontEnd PostProcessVolume desat 0.5 / bloom 0.2. HIGH, not CONFIRMED; human check against an original title capture.
4. Orbital Debris: one material fails to compile (Megatron_com_Mat|LM); fixed on agents/rendering after the freeze.
5. Energon (team) colour on the match pawn stays the material default [PARTIAL]. Decepticon-faction bodies are exercised by the chassis test and the preview, not by a frontend match: offline private matches put the player on Autobots.
6. Fullscreen display-mode change, and the multi-lane GPU contention: a display-driver reset (event 4101) hung test runs while four lanes' GL processes ran; the game does not recover from GL context loss (0x0507).

### Human playtest checklist
- Create a Character: each class, Autobot and Decepticon chassis, Change Form, both colours with stick / arrows / WASD + LT / RT, save, leave and reopen.
- Private Match TDM Streets: the spawned body matches the preview (robot and vehicle form, colours); move, fire, transform, boost, transform back; scoreboard; pause / resume; Quit → confirmation → party lobby.
- A second map (Seed / Berth / Gorge): the selected body again; no leftovers from the previous map.
- Title screen look vs the original (desaturation, bloom).
- HUD at 1280×720 and fullscreen; intro movies and their audio; account name in lobby / kill feed.
- High refresh (144 / 240 Hz monitor): robot and vehicle camera smoothness.

## INTEGRATION MILESTONE 06c (2026-10-05) — stabilization baseline: frontend → world rendering — branch `integration/milestone-06c`

**Playtest executables (plain launch, no environment variables):**
- Release: `F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe`
- Debug: `F:\Transformers Rebuild\Rebuild\build\bin\wfc_rebuild.exe`

Both select `F:\Transformers Rebuild\Rebuild\work\render` at runtime: the log says `wfc: render data root …` and
`shader path active: 325 materials, 1975 lightmapped components, 268 lights` for Streets.

### Lane heads merged in this pass
| lane | head | content |
|---|---|---|
| agents/frontend | 89df3bc | a96f841 GPU-state save / restore around the UI pass; f5ada69 Hud_GFX noScale + native interp / setColor + movie letterbox; 9aa28ea pause close; 74b84d8 fullscreen at the saved resolution; 89df3bc customization cameras + matinee FOV / DrawScale |
| agents/rendering | b0d47b2 | ab851f5 each 3D frame establishes its GL state + depth / GL-error guards + release_path_check.sh; 412713c no CPU copy of large world meshes; 359da41 / 504730f preview-pawn path, standard UI render data; M12–M14 vignette DrawScale, matinee FOV, particle size × emitter scale (CONFIRMED in the xex) |
| agents/gameplay | 1a4e36b | explicit chassis fallback, per-form look settings |
| agents/systems | 0f293c7 | unchanged (M07 audio) |
| agents/experimental | 734bde4 | validation only (presentation gate), not merged |

**Recorded for the next full integration (not merged in this pass, per instruction):**
- agents/frontend df79c6f / 4b2fae1: Create a Character preview pawn visibility, grounding, the GFx collector crash fix;
- agents/rendering e014f45: `sceneGroundHeight`, used by df79c6f.

### The frontend → world regression (two causes, both fixed)
| cause | owner fix | mark |
|---|---|---|
| Render data root resolved to build/work/render for the Release exe → legacy fallback | Rendering 398b732 (M06b) | INTEGRATION REGRESSION, FIXED |
| The UI pass (GfxRendererGL) left depth test / culling off; from b1fce97 the HUD ran it every frame, so the world drew without depth testing on the frontend route only (sky / smoke over architecture) | Frontend a96f841 (restores what it changes) + Rendering ab851f5 (each 3D frame establishes what it needs) | ROOT CAUSE CONFIRMED (Experimental bisect b1fce97, Rendering reproduction), FIXED, VISUALLY VERIFIED |

**Evidence (final binaries):**
- **Rendering release_path_check** (frontend → Streets → lobby → Berth → lobby → Streets, no overrides): PASS. Every
  capture has noDepth = 0 and glErr = 0; Streets 2801 submitted draws (1641 world, 357 BSP) on both visits; Berth 2059.
- **Rendering visual suite:** 11 / 11. The Streets pinned cameras are identical to the post-fix reference (refdiff
  0.000); the frontend-route match frames differ from it by 0.002.
- **Before / after a96f841** (frontend-route match frame, the same scripted path): 54 % of pixels changed. The
  smoke / fog veil over the architecture is gone; direct-boot cameras are unchanged (refdiff 0.000).
- **Frontend vs direct:** the same submission on both routes (Rendering M11: only the depth state differed; now
  noDepth = 0 on the route). Experimental's route-vs-direct image comparison was skipped because its route script
  stopped on the Quit confirmation (stale test, below).

### Memory: Streets ↔ Seed (private MB, product trace at each load / unload)
| build | sequence | loaded | after unload |
|---|---|---|---|
| Release | Streets, Streets, Seed, Streets, Seed, Streets, Seed | 2354, 2314, 2894, 2475, 2840, 2479, 2848 | 2230, 2300, 2668, 2396, 2672, 2399, 2700 |
| Debug | Streets, Seed, Streets, Seed, Streets, Seed, Streets | 2350, 2927, 2528, 2908, 2533, 2913, 2535 | 2241, 2760, 2480, 2749, 2480, 2771, 2473 |

- **Pattern:** a one-time rise at the first Seed visit, then a plateau (±20 MB) in both builds.
- **Seed's unload** now releases 150–230 MB (Rendering 412713c: no CPU copy of large meshes; previously ~45 MB).
- **Verdict:** no accumulating map-resource leak in Debug or Release (HIGH CONFIDENCE; allocator / driver pooling plateau,
  as Rendering measured). Closed.

### Other checks
| area | result |
|---|---|
| Intro | cold boot from the first movie: startup Bink → Activision at 0.47 s (no freeze on its last frame), Hasbro, High Moon, FMV, each with its Systems audio; 16:9 movies pillarboxed over black in a 2000×800 window (not stretched) |
| HUD scale | Hud_GFX noScale, laid out from the real window size each frame: verified at 1280×720 and 2000×800 windowed (anchored corners, normal size); 1920×1080 / 2560×1440 fullscreen verified by Frontend; human check |
| Account / player name | offline account "IntegrationTest" (field limit: 15 of 17 typed characters) shows the original "account created" confirmation and becomes the identity in the party lobby, game lobby and kill feed [PC ADAPTATION, no Xbox Live identity] |
| Integrated loops | cold boot → intro → title → Multiplayer → TDM → Streets → character selection → pause / resume → Quit → confirmation → party lobby → Streets → leave → Seed → leave → Streets …: 7 matches, 1420 / 1420 frame verdicts path=original PASS |
| Suites | frontend 74 / 0; harness 191 / 0 FAIL / 22 known; TDM 42 / 42; DOM / KOTH 21 / 21; transform stress 0 / 1520; chaos 0 / 0 / 0; camera jitter 0.0003 / 0.0003 / 0.0002° at 60 / 144 / 240 Hz; audio suite 566 / 0; movie probe OK; withheld render data → error + VISUALCHECK FAIL (no silent fallback) |

### Experimental presentation gate (734bde4), Debug: 20 pass / 7 fail / 2 partial / 1 unknown / 1 skip
| check | classification |
|---|---|
| route completes (FAIL) | **stale test**: expects Quit → main menu directly; the original shows the Leave Lobby confirmation and returns to the party lobby (Frontend 5c53986, CONFIRMED script) |
| settings controls rebind (FAIL) | **stale test**: the shipped PC menus have no rebinding (the controls pages are the original reference pages) |
| customization / back_forward soft lock (4 FAIL) | **stale test**: Back from Create a Character returns to the party lobby, and a further Back opens the original Quit Game confirmation. The gate expects the main menu; the screens respond. Preview pawns render (both factions shown together until Frontend df79c6f, next integration) |
| body resolves (FAIL) | **real, known**: Jet4 selected, Optimus drawn (Gameplay explicit RECONSTRUCTION FALLBACK; no other pawn resources yet) |
| world detail a09 (PARTIAL 0.143 vs 0.15) | borderline on one moving frame; human check |
| credits (PARTIAL) | screen present; the movie plays and returns (verified separately) |
| no movement under menus (UNKNOWN) | harness limitation: scripted input enters after the menu gate |
| route vs direct (SKIP) | not run (route stopped on the confirmation) |

### Remaining visible discrepancies (top)
1. The selected character is not the drawn body (Optimus for every chassis).
2. Create a Character: both faction preview pawns visible at once; framing provisional (fixed in Frontend df79c6f, next
   integration).
3. Lobby / title scenes: some authored dark; one title laser / Matinee effect partial.
4. Non-Streets maps: material / collision PARTIALs from M06 (Molten floor material, Debris shader, Escalation
   materials, Gorge / Molten edge cases).
5. Fullscreen now changes the monitor mode (saved resolution); alt-tab restores the desktop [PC ADAPTATION, human check].

### Human checks
- Streets through the full frontend route: architecture, BSP, glass, smoke, decals, sky, fog, lightmaps, pickups,
  movers, Optimus, weapon, HUD.
- HUD at fullscreen 1920×1080 / 2560×1440 and after a live resolution change.
- Intro sound / picture sync and the pillarbox on a wide monitor.
- Account creation with the real keyboard.
- Create a Character navigation; Quit confirmations.
- Seed and Berth through the frontend.

## INTEGRATION MILESTONE 06b (2026-10-04) — human-playtest regression containment — branch `integration/milestone-06b`

**Next human playtest — plain launch, no environment variables needed:**
- Release: `F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe`
- Debug: `F:\Transformers Rebuild\Rebuild\build\bin\wfc_rebuild.exe`

Both find the render data in `F:\Transformers Rebuild\Rebuild\work\render` by themselves; the log's first lines name the
root. If no data is found, the frame is visibly red and the log has an ERROR; there is no silent legacy fallback.

### Root cause of the black world and malformed menus (INTEGRATION REGRESSION, reproduced and fixed)
- **Cause:** the renderer's default render-data root was `<exe>/../../work/render`.
  - Correct for `build\bin` (Debug).
  - Wrong for `build\release\bin`, the exe given for the M06 playtest: it resolved to `build\work\render`, which does
    not exist.
  - Every map and every frontend 3D scene silently fell back to the legacy fixed-function renderer: world mostly black
    and untextured, Optimus / HUD / FX drawn, malformed menu backgrounds, grey panels.
- **Why automation missed it:** every automated run since M01 set `WFC_RENDER_DATA` explicitly, so the testing did not
  match a human launch.
- **Reproduction:** a plain Release launch (no environment) logged "render data not found … legacy renderer", and its
  Streets frame matched the recording. Fixed, it logs "shader path active: 325 materials, 1975 lightmapped components,
  268 lights".
- **Fix:**
  - Rendering's `renderDataRoot()` (agents/rendering 398b732: searches up to 4 levels above the exe, logs the root,
    visible failure). The integration's own equivalent patch was superseded by the owner's.
  - The integration test runners no longer set `WFC_RENDER_DATA`.
- **Not the cause** (checked):
  - the frontend scene / movie / loading teardown: the same flow renders correctly once the root is found;
  - resolution changes: 1600×900 flows render correctly; render targets follow the frame size;
  - generic-map conversion: Streets uses its shipped runtime index.

### Other playtest findings
| finding | cause | resolution | mark |
|---|---|---|---|
| Frontend screens over live gameplay (pause menu stayed after Resume) | UI controller did not run the close callback when a screen closed itself | Frontend 6fb19f8: scripted UI EndStates (the integration's interim fix superseded) | VISUALLY VERIFIED (resume frames) |
| Choose Character closed at match start with no character → body-less match / soft lock | UseInGameLobby (`!PRI.HasSelectedCharacter`) ignored | Frontend 6fb19f8: Choose Character stays until chosen; first spawn → OnRespawn → InGame (integration interim hold removed) | CONFIRMED script (Frontend) |
| Extras → Movies / Credits soft lock | `Game.PlayMovie` unhandled; the menu removes its input in MovieStarted and only gets it back from `_global.MovieEnded` | Frontend 732a5b2 (MovieEnded handshake); verified: Credits plays with Systems audio, skip, `MovieEnded`, menu responds | CONFIRMED script / VISUALLY VERIFIED |
| Account / rename text entry did nothing | no WM_CHAR path, no input TextFields | Frontend 9dc70ec: TextPrompt_GFX input fields; verified: "Tester" typed and the local account created | PC ADAPTATION (local accounts) |
| Second boot skipped the intro | the rebuild persisted HasWatchedIntroMovie | Frontend 3e9db97: a session flag in the original | CONFIRMED (Frontend RE); corrects the integration's earlier "persisted = original" note |
| Kill-feed rows overlap; HUD bars / clock do not animate | `HmObjectInterpolator.addInterp` (the HUD movie's native tween) unhandled | open | PRODUCT FAIL (Frontend) |
| 3D character preview absent; colour customization incomplete | preview pawn not implemented (setFrontendPreviewCharacter handoff) | open | PARTIAL (Frontend / Rendering) |
| Selected body is not the in-game character | only Optimus' pawn resources load | open | RECONSTRUCTION FALLBACK (Gameplay / AssetTools) |
| Keyboard layout shows categories but no rebinding | the shipped PC menus have no rebinding (Frontend audit) | none (not a 1:1 feature) | FUTURE PC EXTENSION |
| Lobby backgrounds look flat | the customization room draws only its dome; class cameras / preview pawn are Frontend-driven | open | PARTIAL |
| TDM description text malformed while scrolling | not reproduced in scripted runs | open | human check |
| Hang in SwapBuffers (2 of 2 runs at 1600×900, with 3–4 other lanes' GL processes on the GPU) | the main thread blocks inside the AMD driver's SwapBuffers; another lane's process froze the same way | not isolated | UNKNOWN (environment vs. resolution) — human check at higher resolutions |

### Process note
During this pass the integration session stopped every `wfc_rebuild` process on the machine, not only its own. That
included two processes belonging to other lanes (an Experimental or Rendering run may have been interrupted and needs to
be repeated). From then on only the session's own processes were stopped, by PID.

### Lane heads in this build
| lane | head | notes |
|---|---|---|
| agents/rendering | 398b732 | renderDataRoot() root fix (owner's version taken over the integration's), visible failure, visual checks |
| agents/frontend | 4401c73 | 732a5b2 Extras MovieEnded; 9dc70ec text entry; 3e9db97 intro session flag; 6fb19f8 Choose Character ownership / UI EndStates; 88f19df Create a Character; 4401c73 lobby ticker; 5c53986 return routing |
| agents/gameplay | e258179 | unchanged since M06 |
| agents/systems | 0f293c7 | unchanged since M06 |
| agents/experimental | d7959e1 | validation only, not merged |

Integration's own M06b changes that remain:
- the pause-close fix in `UIController::onCurrentUIClosed` (also missing on the Frontend head);
- test runners that no longer set `WFC_RENDER_DATA`.

The integration's interim versions of the root fix, the Choose Character hold and `Game.PlayMovie` were replaced by the
owners' versions.

### Validation (clean Debug / Release of this build; no environment overrides)
| check | result |
|---|---|
| render-data root chosen at runtime | Release: `build\release\bin\..\..\..\work\render` = `F:\Transformers Rebuild\Rebuild\work\render`; Debug: `build\bin\..\..\work\render` (same folder) |
| renderer verdict, plain launch (WFC_VISUALCHECK) | `path=original`, 325 materials, 1975 lightmapped components, 268 lights (both exes) |
| render data withheld (WFC_RENDER_DATA → missing folder) | `[error] wfc: render data not found …`, `VISUALCHECK path=legacy -> FAIL`: a missing folder can no longer pass as a loaded map |
| Rendering visual suite (5 pinned Streets cameras, title scene, title → lobbies → Streets → lobby → Streets) | **11 / 11 PASS**, all `path=original`; the Streets cameras are pixel-identical (refdiff 0.000) to the first passing run; match 1 = match 2 = 2033 draws / 1641 world / 357 BSP (no leak) |
| frontend unit | 68 / 0 |
| wfc_fidelity | 191 / 0 FAIL / 22 known |
| TDM / DOM-KOTH | 41 / 41; 21 / 21 |
| transform stress / chaos (Streets) | 0 / 1520 under the map; 0 under / 0 KillZ / 0 stuck |
| high-refresh camera (144 Hz) | 0.0003° |
| audio suite / movie probe | 566 / 0; repeated chains end with 0 streams / 0 voices |
| Extras Credits | plays with its audio, skip, `_global.MovieEnded`, the menu responds |
| Accounts text entry | "Tester" typed, the local account created |
| pause → resume (InGame and PausedSpectating) | the pause movie closes (`ui.close`, `gfx.movieClosed`); the live match is visible |

## INTEGRATION MILESTONE 06 (2026-10-04) — branch `integration/milestone-06` — first multi-map, audio-complete build

**Executables:**
- Release: `F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe`
- Debug: `F:\Transformers Rebuild\Rebuild\build\bin\wfc_rebuild.exe`

A plain launch cold-boots through the intro movies (with their own audio) into the shipped PC frontend. Direct boot
(`WFC_BOOT=match`, `WFC_MAP=<map>`) and every harness are explicit debug options. Render data is per worktree:
`tools\render\build_render_data.ps1 -Map <map>` for each map in `work\render` (all 10 cooked MP maps and the 5 UI
levels were generated for this build).

### Lane heads merged (fetched 2026-10-04; integration/milestone-05 1e14900 was the base)
| lane | head | conflicts | resolution |
|---|---|---|---|
| agents/frontend | 9e67bf8 | none (based on M05) | PC frontend, 3D menu scenes, settings, character selection, Hud_GFX / scoreboard / results, load yields |
| agents/gameplay | e258179 | World.cpp (draw), STATUS | Gameplay's dead / pre-spawn gating of weapon and vehicle FX inside the M04 sysprof scopes; the reticle push (M03 glue) kept, hidden while dead |
| agents/systems | 0f293c7 | FIDELITY, STATUS | both kept; both M07 handoff patches applied (below) |
| agents/rendering | 4215359 | WfcPipeline.cpp, Application.cpp, FIDELITY | Rendering's `yieldLoad()`; its `WFC_FRONTENDSCENE` diagnostic in `run()` before the frontend / `runMatch` split (Frontend's integrator notes) |
| agents/experimental | bf53035 | — | validation only, not merged; its tools run from `git archive` exports in `work/` |
| AssetTools | assettools/checkpoint 5dd9e62 (production map pipeline, 10 cooked maps) | consumed | read only |
| RE | MILESTONE05 blockers, OVERNIGHT 2026-10-04 (HUD, no minimap) | consumed through the lanes | — |

### Ownership resolutions
- **Movie audio (two implementations):** Frontend 8b6466e decoded Bink audio in the frontend (Media Foundation →
  WAV cache → one device voice); Systems M07 decodes and streams it (`MovieAudioPlayer`).
  - By ownership Systems decodes and plays; the frontend only says when (first video frame / end / skip /
    underlay release) through `IFrontendAudio::startMovieAudio / stopMovieAudio`.
  - Removed: `frontend/MovieAudio.*`, the WAV cache, the prefetch thread and `IMoviePlayer::decodeAudio`. There is one
    decoder.
  - Kept: Frontend's per-thread COM / MF start-up and its `movie.audioStart` event (Experimental's gate).
- **Match audio:** the Systems patch is applied in Gameplay's own event loop (`World::tickMatch`).
  - MatchStarted → mode announcement, description and music (local team chooses the Optimus / Megatron voice);
    GameNearlyComplete → final-stretch music; Time / Kills / Points-left → announcer switches 0–7; MatchEnded → end
    music and the winning-team line.
  - No second timer, score or state machine.
- **In-match HUD:** Frontend runs Hud_GFX fed by `World::hudState` (health segments, overshield, ammo, team scores, clock,
  kill feed, spectate / respawn, results); Rendering owns the Canvas markers. No minimap (RE: none shipped, CONFIRMED).
- **Character selection → Gameplay:** in a frontend-launched match the local player starts unselected
  (`Match::requireCharacterSelection`), so Gameplay's CheckReadySpawn waits for the CustomTransformers selection, which
  reaches `Match::selectCharacter` (type, specialty, iconic chassis). Gameplay resolves the body (e.g. Scout / Autobots →
  `Car2` Sideswipe; logged on `MATCH spawn ... chassis=`). **The drawn pawn is still Optimus**: only Optimus' pawn
  resources load (Gameplay RECONSTRUCTION FALLBACK, needs ROBODEF / VEHDEF exports and per-chassis animation / vehicle
  work).

### Multi-map path (one generic path, no per-map code)
| piece | was | now | check |
|---|---|---|---|
| Frontend selectability | runtime `render_index.json` (Streets only) | + AssetTools production index `manifests/maps/<map>/render_index_generic.json` | 8 TDM maps selectable; Escalation SV-only; Fortress / Havoc / Tranquillity disabled (not cooked) |
| Gameplay KillZ | Streets' -75000 UU for all | the map's BASE TnWorldInfo (physics.json) | Streets -750 m unchanged; Gorge -75, Seed -15, Rust -5.1 m … |
| Gameplay rotating movers | three hard-coded Streets domes | the map's AssetTools movers manifest | Streets reproduces the three domes exactly |
| Renderer render index | runtime index (Streets) | + `tools/render/build_render_index.py` (generic → runtime pickup visuals) | `--check`: 14 / 14 Streets ammo crates equal the hand-validated index |
| Render tools manifests | `<last token>_<kind>.json` | + `next_map/<MAP>_<kind>.json` | movers / pickup FX for the generic maps |
| Systems map audio | manifests for Streets + Gorge | every MP map with `audio.json` (generator list from data) | UI / match / Streets / Gorge manifests byte-identical; 8 maps added |
| Render data tooling | — | lit BSP fixed for every map (numpy truth value; Level of the BSP package), decals without serialized placement skipped | Streets `bsp.glb` byte-identical to M05; every map's BSP triangle count = AssetTools audit |

### Multiplayer map readiness (registry: 13 maps; 10 cooked in the dump)
Columns: LOADS, STRUCTURALLY PLAYABLE (WFC_CHAOS 20 starts × 20 s, WFC_XFORMTEST 760 transforms), VISUALLY PLAYABLE (lit
WFC path, materials compiled), MODE READY (TDM: frontend-launched match to the score limit with spawn / kills / respawn /
end / return), then what is PARTIAL / BLOCKED.

| map | LOADS | STRUCT. PLAYABLE | VISUALLY PLAYABLE | MODE READY (TDM) | PARTIAL | BLOCKED |
|---|---|---|---|---|---|---|
| MP_IAC_Streets (reference) | yes | yes: chaos 0 under / 0 KillZ / 0 stuck; xform 0 / 1520 | yes: 325 / 325 materials, 107 / 107 smoke / glass / ramp measurements = Rendering lane | yes (human-verified reference) | — | — |
| MP_UND_Gorge | yes | yes, 1 chaos run under a BSP floor (1.43 m), xform 0 / 760 | yes: 295 / 298, BSP 1824 tris, 9 fans animate | yes (×4 in the soak) | fans rotate in render only (Roll axis); DOM totems / KOTH rings not converted; 3 DES_IAC materials | — |
| MP_IAC_Seed | yes | yes: 0 / 0 / 0; xform 0 / 760 | yes: 353 / 355 | yes | 1 of 8 movers render-only | — |
| MP_IAC_Berth | yes | yes: 0 / 0 / 0 | yes: 305 / 307 (looks washed out: human check) | yes | 1 decal without placement skipped | — |
| MP_UND_Complex | yes | yes: 0 / 0 / 0 | yes: 291 / 293 | yes | — | — |
| MP_IAC_Rust | yes | yes, 1 stuck | yes: 389 / 391 | yes | 1 decal skipped | — |
| MP_ORB_Debris | yes | yes, 2 stuck | mostly: 303 / 306, 1 material shader compile error (Megatron_com_Mat) | yes | 24 Matinee movers render | — |
| MP_KON_Molten | yes | partial: xform 1 / 760 ended under the map (2.85 m) | partial: 328 / 333, floor materials use the unsupported MaterialExpressionTextureSetSample | yes | floor material, 1 transform fall-through | — |
| MP_ESC_BrokenHope (Escalation) | yes | yes, 1 stuck | partial: 288 / 341 (character / wreck materials) | n/a | — | Survival (SV) mode not implemented (Gameplay) |
| MP_ESC_Remnant (Escalation) | yes | yes, 2 stuck (KillZ authored 0) | partial: 302 / 355 | n/a | — | SV not implemented |
| MP_KON_Fortress / MP_ORB_Havoc / MP_ESC_Tranquillity | — | — | — | — | — | **not cooked in the dump** (source data absent) |

DOM / KOTH on non-Streets maps: Gameplay's objective logic is generic, but the totem / ring visuals are not converted from
the generic index (PARTIAL). CTF / EXT: not implemented (Gameplay). All TDM maps were also launched from the frontend in
one process (Berth → Complex → Rust → Debris → Molten), each to the score limit and back.

### Builds
- Clean Debug (`build.ps1 -Jobs 2 -Clean`, 43 s) and clean Release (`build\release`, 56 s): 0 errors. The one warning
  is Rendering's pre-existing unused `reading` (WfcMapFx.cpp).
- The final validation below ran on these clean binaries.

### Validation (clean binaries)
| area | result |
|---|---|
| frontend unit (Debug / Release) | 59 / 0 |
| wfc_fidelity (Debug / Release / map) | 191 / 0 FAIL / 22 known; map 193 / 0; collision 9 / 0 |
| TDM rules (WFC_TDMTEST) | 41 / 41 |
| WFC_MATCHTEST | TDM to 40, clock tie, DM to 20 |
| WFC_MODEPLAYTEST (DOM / KOTH) | 21 / 21 |
| **boost → robot / transform stress (WFC_XFORMTEST)** | **0 / 1520 under the map, 0 KillZ** (Streets); 0 / 760 on 9 of 10 maps; Molten 1 / 760 |
| **WFC_CHAOS** (Streets, 60 × 20 s; 327 boosts, 340 transforms) | **0 under the map, 0 KillZ, 0 stuck** |
| map sweep / traverse (Streets) | 984 boost / jump runs + 984 transforms 0 below KillZ; 160 runs 0 falls |
| **high-refresh character (WFC_CAMSYNC)** | robot: 0.0003° at 60 / 144 Hz, 0.0002° at 240 Hz (the M05 per-tick camera measures 1.28° at 144 Hz); hover 0.011° / boost 0.023° at 144 Hz |
| Streets smoke / fog cards / steam / glass / ramps (Experimental playtest-regressions vs the Rendering-lane reference) | **107 / 107 measurements within 5 %** |
| Rendering self-tests (shadow, DLE, LVV 400 / 0, light visibility, verify_permutations 325 / 325, audit_map, reload test) | pass |
| Systems audio_native_suite | 566 / 0 |
| Systems movie_audio_probe (real device) | every intro movie plays its own sound with the game mix at -96 dB under it; skip and end stop it; no frontend music during the chain; repeated chains end with 0 streams / 0 voices |
| runtime probe | 30 / 1 FAIL → fixed (legacy-renderer crash, below) |
| legacy renderer (`WFC_LEGACYRENDER`, vehicle) | runs (was an access violation) |

### Cold boot and audio (product traces)
- **Chain:** Activision → Hasbro → High Moon → FMV_intro, each with `movie.audioStart` (Systems stream; "10 tracks @ 48 kHz,
  centre track 5") and `movie.audioStop` (no stream left, 0 voices).
- **Mute:** `CINE_MUTE_FOR_BINK` mutes the game mix while a movie plays.
- **Title music:** `FRONTEND_MX_ORBIT_01` starts only after FMV_intro. The looping startup / loading Binks stay silent
  (no authored audio).
- **Sync:** the audio clock at the movie's end trails the video position by 0.35 / 0.10 / 0.12 / 0.13 s (Activision /
  Hasbro / High Moon / 128.7 s FMV). The offset is constant, with no drift; it is a start offset. Lip sync is a human check.
- **Skip:** `ui:Accept` during Hasbro stopped its sound in the same frame (clock 9.07 s vs video 9.15 s; 0 streams after);
  the next movie started with its own sound.
- **Language track:** centre track 5 is used for English [**PROVISIONAL**, not CONFIRMED ORIGINAL: the native selection
  `HmPlayerController.MovieAudioSetup` / BinkSetSoundTrack is UNKNOWN; `WFC_MOVIE_LANGSLOT` overrides].
- **Match audio** (every map tested):
  - start: the TDM announcement and description as Optimus (`DialogCharacters.OPRIME`, Autobot team) and
    `BL_LVL_MP_MX.DM_START`;
  - kills-left lines;
  - end: `DM_END_AUTOBOTS_WIN` + the win line.
  - Lobby music on return: `MP_LOBBY_MX`.

### Lifecycle / soak (one process each)
- **8 matches, cold boot with intro, Streets / Gorge alternating** (`WFC_LIFECYCLE=3`, scripted through the shipped
  menus):
  - private MB after unload: Streets 2648 → 2747 / 2740 / 2740, Gorge 2732 / 2758 / 2759 / 2755. The first match settles,
    then flat;
  - peak 2.99 GB (M05: 3.5 GB);
  - audio after every unload: 0 voices, 0 instances, 0 level cues, PCM 36.5 MB (identical each cycle);
  - GL left after Rendering's unload: textures 72 (Streets) / 60 (Gorge) each cycle, released by the census;
  - handles at each return 720 → 740 (+3 per match, see failures).
- **Final clean-build soak (4 matches, same order):** identical picture, peak 2.98 GB.
- **Five more maps in one process:** Berth → Complex → Rust → Debris → Molten, each to the score limit and back to the
  lobby. Memory after unload follows the largest map loaded so far (Rust 3.15 GB, then Debris 2.83 GB): no per-match
  growth.
- **Gameplay state each match:** spawn at authored team starts, the selected character resolved, kills / deaths / respawn
  (4.98 s), score limit, end, 15 s return to the lobby, second match. HUD values (health segments, team score bars,
  ammo, clock, respawn timer) are pushed from `World::hudState`.

### Input / settings
- **Logical UI bindings** [PC ADAPTATION of the console controls]: Enter = A, Escape = B, arrows = D-pad, F3 = Start,
  Tab = Select / scoreboard; pad buttons as authored.
- **Scripted runs through the shipped movies' own ActionScript:**
  - Down + Enter on the main menu → Multiplayer → `Online.OpenPartyLobby`;
  - Escape in the party lobby → `Game.QuitToMainMenu`;
  - a mouse click on `multiplayerBtn_mc` → the same call.
  - Physical keyboard / pad input on a shared desktop is a human check.
- **Brightness:** profile `GammaSetting=85` → DisplayGamma 2.865 (decompiled mapping), applied at boot, kept through the
  Streets map load (Rendering 4215359) and re-applied to the renderer recreated after each match (Gorge loaded with it).
  The look is a human check.
- **Display settings:** `[PCSettings]` (1280×720, windowed, VSync) are Frontend's.

### Failures and open items (by kind)
**Real product failures (owner):**
- **Frontend:** kill-feed rows overlap. Two Hud_GFX `GameMessage` rows are drawn at the same position instead of
  stacking; the data is Gameplay's and correct.
- **Gameplay:**
  - Gorge: 1 chaos run 1.43 m under a BSP floor;
  - Molten: 1 / 760 transforms under the map (2.85 m);
  - stuck runs: Rust 1, Debris 2, Remnant 2, BrokenHope 1.
- **Gameplay / AssetTools:** only the Optimus pawn loads, so the selected character's body is not drawn (the resolved
  chassis is).
- **Rendering:**
  - Molten floor materials (MaterialExpressionTextureSetSample unsupported);
  - Debris `Megatron_com_Mat` shader compile error;
  - Escalation character / wreck materials (53 per map);
  - DOM totems / KOTH rings / destructible meshes on generic maps (index conversion PARTIAL);
  - 72 / 60 textures per match left to the frontend's GL census.
- **Frontend / Rendering:** handles +3 per match (720 → 740 over 8 matches; memory flat). Owner to be found.
- **Fixed at integration** (lane defects found by the merged-build runs):
  - lit BSP missing on every map (Rendering tool);
  - legacy-renderer crash (Rendering M09);
  - in-match loading underlay (M05).

**Stale tests (retired / updated):**
- audio_native_suite manifest count (7 → derived from data);
- frontend selectability test (now includes the AssetTools generic index);
- Experimental `menu_music` / match-validator regexes (reported in M05).

**Test-harness issues:**
- the script set the lobby map before the lobby movie's own initialization (fixed in the runner; the product follows RE
  C4 / C7);
- `audio.baseline` is not emitted on the Frontend pass-3 load path (the post-unload state is used instead).

**Source-data absences:** Fortress, Havoc and Tranquillity are not cooked in the dump.

**Service dependent:** online play / matchmaking / accounts / leaderboards / challenges / XP progression (results show
offline "LEVEL 0 / 500 to next level").

**Not implemented (lane scope):** Survival (Escalation) mode; CTF / EXT; bots; per-chassis pawns.

### Human checks
1. Cold boot: intro movies sound right and in sync (constant 0.1–0.35 s start offset measured); title music after FMV.
2. PC navigation with the real keyboard, mouse and an Xbox controller; pause (hold W, press Esc: the robot stops while
   the world runs); Tab scoreboard.
3. Character select: choose a non-default character; the resolved chassis is logged, but the drawn body is Optimus.
4. Streets: smoke, fog, steam, glass and ramps (numbers match the Rendering lane); boost → robot against walls / ramps;
   vehicle hover and ramp contact.
5. High-refresh play at 144 / 240 Hz: robot and vehicle coherence (measured 0.0003°).
6. Gorge and the other maps: look (Berth looks washed out; Molten floor), lighting, fan / orrery movers, pickups.
7. HUD: score / time / ammo / health / overshield update; kill feed (overlap defect); match-start / respawn / results
   screens.
8. Brightness slider in Settings across a map change.
9. Long session: memory plateaus around 2.7–3.2 GB after unload, depending on the largest map played.

## INTEGRATION MILESTONE 05 (2026-10-04) — branch `integration/milestone-05` — one exe: frontend → TDM Streets → return → again

**Exe for Experimental's final gate and the human playtest:**
`F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe` (Release). Debug: `build\bin\wfc_rebuild.exe`.
A plain launch boots the original intro and frontend; no internal commands are needed. The direct-to-match boot stays
an explicit debug option (`WFC_BOOT=match`, or the existing harness variables).

### Lane heads (fetched 2026-10-04; all pushed heads merged, none newer)
| lane | head | merged | conflicts | resolution |
|---|---|---|---|---|
| agents/frontend | 08ef880 | yes (first) | none | Scaleform frontend, intro movies, lobbies, loading, match launch / return |
| agents/gameplay | d541c78 | yes | World.cpp, World.h, STATUS.md | Gameplay's match tick, dead-player gating and camera collision kept with the M04 sysprof scopes; Frontend's `mapName_` kept beside Gameplay's Match members |
| agents/systems | 3d295ec | yes | Application.cpp, World.cpp, FIDELITY, STATUS | both diagnostic hooks kept; Systems' `setAudio(a, loadSliceMap)` with `loadMapAudio(mapName_)` instead of the hard-wired Streets |
| agents/rendering | 187e6e9 | yes | FIDELITY.md | VehicleFx auto-merged (Systems `clearParticles` + Rendering edits both present); no renderer file conflicted, so no old renderer behaviour could be restored |
| agents/experimental | 6447fc1 | **no** (validation only) | — | its M05 gate and visual tools were run from `git archive` exports under `work/m5/exp*` |
| AssetTools | runtime data under ExtractedAssets (Streets 8d8195e / render_index da67634; Gorge world + collision + gameplay + audio, no render export) | consumed | — | render data regenerated with the merged tools |
| RE | `MILESTONE05_FRONTEND_GAMEPLAY_BLOCKERS.md` (85e340c) | consumed | — | see the table below |

### Integration glue (connecting owners; documented in each commit)
- **Frontend → Gameplay:**
  - `Application::loadMatch` launches Gameplay's match from the frontend's StartLevel URL (`World::launchMatch`):
    mode, PointsToWin and TimeLimit come from the URL (RE D1–D3); the map is the catalog selection's runtime directory;
    teams come from Gameplay's `PickTeam` (RE E4 offline).
  - Frontend's PROVISIONAL immediate-BeginGame adapter is removed.
- **Gameplay → Frontend UI events** (`routeMatchToFrontend`, per tick):
  - load → default character selected → PreGameCountdown during PendingMatch (RE D5; 10.13–10.22 s measured);
  - MatchStarted → UI event 3;
  - local death + 3.0 s → 4 (RE E7);
  - wave respawn → 5 (4.98 s measured; RE 5.0 s);
  - MatchEnded → 9;
  - ReturnToLobby (+15 s) → new `GameFlow::returnToGameLobby` (`UI_Lobby_m?...?MapId=<id>?listen`, RE F4 / F6).
- **Match values for the movies:** `frontend::MatchValues` carries Gameplay's `hudState` into the data stores:
  `<CurrentGame:IsCountingDown / CurrentCountdown / GoalScore / Teams / Players>` and
  `<PlayerOwner:Score / TeamID / TimeToRespawn>`. Previously these returned lobby values or "0" in a match. Assists are
  not shown (RE F2).
- **Systems:**
  - the frontend drives `FrontendAudioRuntime` (music, UI sounds, movie mute);
  - a match load calls `setAudio(a, false)` + `loadMapAudio(<selected map>)`;
  - an unload calls `unloadMapAudio()`.
- **Rendering:**
  - `unloadMatch` calls `IRenderer::unloadMapRenderData()` (RENDERER_CONTRACT.md);
  - Frontend's GlCensus stays as a fallback; it releases 72 textures per cycle that sit outside the map data.
- **Frontend fix: the loading underlay is released in the match.** `updateMoviePlayer` ran only in the frontend
  loop, so the last frame of `TF_LoadingScreen` (black with a "LOADING..." spinner) was composited over the 3D world for
  the whole match. Experimental saw the same on agents/frontend 08ef880. Verified fixed with product-side shots:
  countdown, in game, spectating, end game, lobby return.
- **Generalization:**
  - no Streets literals remain on the launch path: `startLocalMatch` gameplay.json, `fromURL` `_Base_m` strip,
    `launchMatch` map check, `setAudio`, `WFC_MATCH`;
  - a map is selectable when it is cooked AND has the runtime world AND the AssetTools render export
    (`render_index.json`);
  - **Gorge stays in the catalog but is listed disabled**: no render export, not validated by Gameplay or Rendering.
    It becomes selectable automatically when AssetTools exports its render data. No Gorge work was done.
- **Harness boot routing:** WFC_XFORMTEST / MATCHTEST / CAMTEST / CHAOS / TDMTEST / MATCH / MATCH_URL / RELOADTEST
  imply the direct boot again. After the frontend became the default, they would have booted the menu.
- **Validation-only hooks (opt-in, not normal behaviour):**
  - `WFC_LIFECYCLE=<goal>`: one Gameplay diagnostic opponent; kills and deaths go through `applyMatchDamage`, so
    Gameplay's own rules run;
  - Experimental RUNTIME-EVENTS `MATCH` lines;
  - FlowTrace `audio.baseline / loaded / unloaded`.
- **Stale tests retired:**
  - frontend selectable-maps test and fixture now include the render export;
  - harness `spread_cap` uses Experimental's RE d50e2a9 check;
  - `spread_after_10` is a known deviation owned by Experimental.

### RE M05 corrections consumed
| RE | behaviour | where |
|---|---|---|
| B2 / B3 / B4 | mode order TDM, DM, DOM, CTF, EXT, KOTH; rows clamp; TDM Autobalanced / 15 min / 40 | authored frontend data (frontend tests) |
| C1–C4, C7 | enabled + compatible maps in TransLevels order; `loopNavigation`; index 0; reset after return | lobby movie + catalog (Experimental verified the keys on 08ef880) |
| C8 / D4 / D5 | 10 s countdown; PendingMatch → InProgress; PreGameCountdown during pending | Gameplay + glue (measured 10.14 s) |
| D1–D3 | match URL and rule list | frontend URL → `MatchLaunch::fromURL`; `MATCH init` rules = RE |
| E3 / E4 | `PickTeam(255)` offline before StartMatch | Gameplay |
| E7 / E8 | death → 3.0 s → event 4; 5.0 s wave → event 5; PlayerRestartDelay dead | Gameplay + glue (4.98 s) |
| F1–F4, F6 | kill +1 player and team, no suicide score; no assists on the board; tie; MatchOver 15 s → ReturnToGameLobby; reload per match | Gameplay + glue |
| G | HUD data from data stores / pushes | `MatchValues` |
| I1 / I2 / I5 / I6 | health full heal, overshield +550, respawn 30 / 60 / 120 s, factories reset at StartMatch | Gameplay (TDMTEST, PICKUPTEST) |
| **I3** | **regen after 2.0 s, 20 HP/s, segment-limited** | **NOT consumed**: Gameplay marks it UNKNOWN; not added at integration |

### Builds
- Clean Debug (`build.ps1 -Jobs 2 -Clean`) and clean Release (`build\release`): **0 errors**.
- Both were rebuilt incrementally after the final glue. The one warning is Rendering's unused `reading` (WfcMapFx.cpp).

### Validation (final binaries)
| area | result |
|---|---|
| frontend unit (Debug, Release) | 59 / 0 |
| **Experimental M05 e2e gate** (4b03c2b tools, 3 cycles, final exe) | **52 pass / 1 FAIL / 2 known / 20 skip / 6 human**. Window capture worked: the in-game capture is drawn. `pending_countdown` 10.14 s, `hud_visible_in_game`, memory_per_cycle +7.0 MB, handles stable all PASS. The FAIL is a gate regex issue (below). |
| TDM rules (WFC_TDMTEST) | 30 / 30, including a second in-process match |
| WFC_MATCHTEST | TDM to 40; clock tie at 125 s with 120 / 60 / 30 announcements; DM to 20 |
| **transform stress (WFC_XFORMTEST)** | **0 / 1520 under the map, 0 KillZ** |
| **WFC_CHAOS** (60 × 20 s: 355 transforms, 307 boosts, 312 jumps) | **0 under the map, 0 KillZ**, 1 stuck, 3 prop entries (the Gameplay baseline) |
| traversal / map sweep | 160 runs 0 falls; 984 boost/jump runs + 984 transforms 0 below KillZ |
| boost cycle + transform every 70 frames (6000 lockstep frames) | 170 transforms, min y −725.0, 0 fell out |
| vehicle / pickups / modes (VEHTEST, PICKUPTEST, MODETEST) | complete; 24 factories, destructible cycle |
| wfc_fidelity (Debug, Release, map) | 191 / **0 FAIL** / 22 known (was 190 / 2 / 21); collision 9 / 0 / 2 |
| Rendering: shadow self-test, DLE, LVV query, light visibility, verify_permutations, audit_map, WFC_RELOADTEST | all exit 0 |
| **smoke / steam / glass / ramps** (Experimental `playtest-regressions.ps1` on the merged exe vs its Rendering-lane 187e6e9 reference) | **107 / 107 measurements within 5% of the Rendering lane**: the playtest fixes are preserved. Steam is the soft blue-purple from the M06 fix |
| render data | regenerated with the merged tools: 231 / 231 materials, 45 map-fx components, movers 0 error |
| Systems audio_native_suite (documented build line, repo root) | **544 / 0** |
| runtime probe (Debug) | 31 / 0 / 3 known (M04: 26 / 4) |
| Experimental match-validator on the lifecycle log | 9 / 3. All three are validator issues (below) |

### Lifecycle and soak (one process, scripted through the shipped menus, `WFC_LIFECYCLE=3`)
Each cycle runs:
1. cold boot through the intro chain (4/4 movies played);
2. main menu → Multiplayer → party lobby → TDM → game lobby → Streets;
3. loading screen ("Team Deathmatch in Streets") → 10 s countdown → spawn;
4. kills and team score → death → spectating at 3.0 s → respawn at 5.0 s;
5. score-limit end → EndGameStats → game lobby with MapId=508 after 15 s → next match.

Repeated **8 times** (pre-fix exe) and **6 times** (final exe) with no restart, 0 warnings, 0 errors, clean exit.

| resource (final exe, 6 cycles) | per cycle | verdict |
|---|---|---|
| private memory after unload | 2809, 2813, 2786, 2848, 2839, 2854 MB | flat. The previous +1.58 GB per return is gone |
| private memory with the match loaded | 3422 → 3441 MB | flat (±10 MB) |
| peak private | 3.49 GB (gate: 3.33 GB) | bounded |
| audio after unload | voices 0, instances 0, level cues 0, PCM 177.3 MB = the pre-load baseline, every cycle | returns to baseline |
| map audio loaded | 32 level cues, PCM 238.0 MB, every cycle | constant |
| GL objects after the renderer unload | textures 72, buffers / programs / FBOs 0, every cycle | constant |
| match load | 4.7 s cold, then 3.0 s | constant |
| player state at each match start | ammo 50 / 150, robot form (gate) | fresh |
| frontend state after return | no stale mode / map / HUD (gate) | fresh |

### Known PARTIAL / UNKNOWN (owners)
- **Gameplay:**
  - health regeneration (RE I3) is UNKNOWN to Gameplay and not implemented;
  - `launchMatch` accepts only TDM and DM (other modes run map state only);
  - the frontend's team index is not passed (offline PickTeam, RE E4);
  - no character-select screen: the default character is selected at load.
- **Frontend:**
  - EndGameStats' experience panel shows "undefined / +NaN" (no profile / XP service; the `Customize.*` bridge calls
    are unhandled, PARTIAL);
  - the Scaleform HUD movie (Hud_GFX: clock and score) is not drawn in play, only the renderer reticle. Ownership is an
    open Frontend / Rendering handoff;
  - the UI_FrontEnd_m 3D scene behind the menus is black (not exported).
- **Rendering / Gameplay:**
  - 72 textures per cycle are released by GlCensus rather than their owner (constant, no leak);
  - the PendingMatch camera views the world before the pawn spawns: framing is a human check.
- **Memory retention:** after the first match the lobby keeps about 2.8 GB private (first boot 0.65 GB). It does not
  grow per cycle; likely allocator / driver retention.
- **Experimental tool issues (reported, not product defects):**
  - gate `menu_music` expects `MUSIC play FRONTEND_MX_ORBIT_01`, but the product logs the package-qualified
    `BL_LVL_HUD_INTERFACE.FRONTEND_MX_ORBIT_01`. The sequence frontend → party → game lobby → frontend is correct;
  - match-validator `init_first` matches "Death**match message**" case-insensitively in a FLOW line;
  - `score_monotonic` counts each team's reset against one restart;
  - `restart_resets` expects `score=0` lines.
- **Experimental R5 (UI selects Gorge):** Gorge is disabled in the list by design for this milestone (above).

### Human visual judgement
1. Intro, menus and lobbies: look, sound and navigation, including map-selector wrap.
2. The countdown overlay over the PendingMatch camera.
3. In play: robot and vehicle feel, boost, boost → robot, firing, pickups, the HUD gap, death → Spectating timer,
   EndGameStats, return to the lobby, then a second match without restarting.
4. Pause: hold W and press Esc. The robot stops while the world runs (gate HUMAN).
5. Smoke, fog sheets, steam, glass and ramps at the reported viewpoints. The numbers match the Rendering lane; the look
   is a human judgement.
6. Gorge is shown disabled.
7. A long session: private memory should plateau (about 2.8 GB in the lobby, 3.5 GB in a match).

## FRONTEND PASS 6 (2026-10-05, branch `agents/frontend`): Create a Character preview, selection contract, soak
**Player-visible fixes** (details and labels: FIDELITY pass 6):
- **Preview colours** were wrong for every character: the preview read the faction argument as the primary colours,
  sampled placeholder palette bitmaps, and never gave new characters the original's random palettes (all green). Fixed.
- **Colour picker** could not be moved from keyboard or pad (the movie's left-stick callback was not driven); now pad
  stick, arrows and W / A / S / D; LT / RT change palette; Accept commits. Mouse: only the palette arrows are clickable,
  as in the original PC movie.
- **Change Form** shows the vehicle; **idle animation** (Cust_Idle, where the chassis has it) through Rendering.
- **Other faction's robot** hidden when a chassis menu opens; **class cameras** per chassis; robots stand on the floor.
- **Intermittent crash** in Create a Character (GFx collector freeing removed clips' children) fixed.
- **Stale preview** state no longer carries into the next visit to the room.

**Selection handoff:** `GameFlow::SelectedCharacter` is the single contract (class, slot, per-faction chassis / body
availability / colours, weapons, vehicle weapon, melee, abilities, skills), filling Gameplay's full
`game::CharacterSelection` (merged by Integration in milestone-07 as the only mapping). A missing body is reported,
not substituted.

**Soak** (`tools/frontend/customize_soak.sh`): 6 cycles (each: all four classes, both factions, Change Form, a colour edit) + 3 private matches with
in-match selection and reopening: 197 checks, no AS errors, no use of collected objects (WFC_GFX_GCCHECK), AS heap at
room entry 10127-10197 and on the title 2265 every cycle, AS timers 3, preview cache bounded; memory 1246 MB at the 2nd
room visit and 1253 MB at the 6th, matches flat at 3.4-3.9 GB. A second run (4 cycles + 2 matches) after the per-movie
collector fix: 132 checks PASS, peak AS heap between collections 92k (was ~199k with the shared counter).

- **GFx collector** ran on a counter shared by all movies, so one movie took every collection; now per movie.
- **Posed preview bodies** cached LRU (8) and released through Rendering's releasePreviewBody when present.

**Commits:** 79b47cd, 8f4c729, 1af7e74, 89eddba, 2c2f5cc, and this checkpoint.

## FRONTEND: customization camera per chassis (2026-10-04, branch `agents/frontend`)
- The Create a Character camera now moves to the class camera of the chosen chassis when a chassis menu opens, and reverses when it closes. This is the original Kismet driven by `CustomizationCameraId`, with the FOV 70 -> 60 / 65 track.
- Matinee DrawScale tracks (title vignette ships / boosters) are exported and evaluated; Rendering's `setFrontendActorScale` receives them when present.
- Tests 74 / 0. Verified on screen in a merge preview with Rendering's data: class cameras, other pawn hidden, pawns on the floor (df79c6f).
- Fixed an intermittent Create a Character crash (GFx collector freed removed clips' children; about 1 in 3 runs, 0 in 4 after).

## FRONTEND PASS 5 (2026-10-04, branch `agents/frontend`): world loss, viewport, HUD presentation
**Frontend-launched world loss: fixed (a96f841).** First bad commit b1fce97 (Experimental bisect). The UI pass left
`GL_DEPTH_TEST` / `GL_CULL_FACE` disabled; from b1fce97 it ran every match frame (the HUD movie), so Streets drew
without depth testing. The UI pass now saves and restores the GL state it changes; the HUD is unchanged.

**Other playtest findings:**
- **Boot movies uncovered at the sides:** in a viewport other than 16:9 (e.g. a maximised window) the 16:9 Binks
  showed the live title scene in the bars. Full-screen movies now letterbox over black, and the scene is not drawn
  while one plays.
- **First boot item frozen:** the startup movie closed before the title scene's synchronous load, so its last frame
  stood still for the load. It now stays up full screen through that load (presented by load yields) until the logos
  start, and a newly opened movie starts at frame 0 (Activision began 0.25 s in).
- **HUD ammo / health too large:** Hud_GFX's native interp / setColor were unhandled (start states, white); also
  noScale stage handling. Fixed; the HUD is the authored one at 1280×720, 1920×1080, fullscreen 2560×1440, after a
  runtime resolution change and after a restart.
- **TDM lobby showed "Player":** a created account was not signed in. The original result box now shows; a new account
  is signed in when none is (PC ADAPTATION). The game lobby lists the typed name.

**Validation (Debug, frontend route):**
- Reproduction matrix: A cold boot → Multiplayer → Private Match → TDM → Streets; B Streets → quit → party lobby →
  Streets; C Streets → Seed of Corruption → Streets; D character selection → gameplay with the HUD: complete world and
  active HUD in every match; pause / resume clears; quit returns to the party lobby, then the title.
- Same exe with `WFC_GFX_NO_GLRESTORE=1`: the world loss reproduces (sky and smoke through the walls).
- Experimental presentation gate (734bde4 tooling, this build): every world check PASSES (route vs direct boot detail
  ratio 0.76, was 0.28; spawn / moving frames; Streets reference cameras unchanged; pause cleared). The other FAILs are
  gate expectations that predate pass 4's CONFIRMED routing (Quit / Back show the confirmation box and return a match
  to the party lobby; the Controls page has no rebinding), automation typing (fixed: key events now type into a
  focused field), the party-lobby scene without render data in this tree, and the selected body (AssetTools /
  Gameplay).
- Navigation stress harness: 3 menu cycles + 2 private matches, 87 checks PASS (no soft-lock, menus restored, AS heap
  2265 / 76 nodes / 48 shapes identical per cycle, memory flat ~1.5 GB).
- `wfc_frontend_tests` 69 / 0.

**Handoffs:**
- Integration: the GL state contract (FIDELITY); merge a96f841 first (world fix, self-contained).
- Rendering: memory after Seed then Streets is ~460 MB higher than the first Streets visit (world / renderer
  retention).
- Experimental: update the gate's route model (quit box → party lobby; Back from the party lobby → box → title) and
  drop the rebinding expectation (not in the shipped menus).

## FRONTEND PASS 4 (2026-10-04, branch `agents/frontend`): human-playtest correctness pass
Based on integration/milestone-06 (fast-forwarded with the user's approval). Details: `docs/FRONTEND.md` §13-§17.

**Playtest findings → cause → fix:**
- **Cinematic not replayed after restart:** the rebuild saved HasWatchedIntroMovie; the original's flag is per process
  (native global, never saved). Session flag now; intro chain on every launch.
- **Malformed menu background:** with no UI render data, Frontend's interim path drew the raw world.glb (opaque white sky
  dome, rainbow rings). It now draws nothing and names `build_render_data.ps1 -Map UI_FrontEnd`; with the data,
  Rendering's scene (nebula, ring, galaxy, ships) is correct.
- **Extras Movies / Credits soft-lock:** Game.PlayMovie was unhandled, so MovieEnded never gave input back. Full-screen
  play with Systems audio, MovieEnded on end / skip / missing file; the movie draws opaque over the menu.
- **Accounts / renaming typing:** the runtime had no editable text fields. Input TextFields added (typing, caret,
  Backspace / Delete / arrows / Home / End, Enter, Escape, click focus); local accounts (PC ADAPTATION).
- **Keyboard configuration:** the original is a read-only reference card; it now shows the original descriptions per
  form. No rebinding (not in the shipped menus).
- **Scrolling TDM / lobby text corrupted:** TextField `_width` was stale until drawn, so the ticker overlapped messages.
- **Create a Character:** empty Weapon Slot 2 and "undefined" weapons (chassis faction filter), colours, rename, reset,
  saving - now as scripted; preview pawns drawn through Rendering's scene-draw hook (detected; merge preview verified).
- **Menu over gameplay / Choose Character:** match start closed Choose Character with no character (InGameLobby flag);
  now it stays until chosen and InGame comes with the spawn; EndStates close screens / hide the HUD; Pause -> Resume no
  longer leaves the pause movie drawn and focused over live gameplay (found by the stress harness).
- **Quit returned too far:** QuitGame(0) returns a private match or game lobby to the party lobby (was the front end);
  confirmations as scripted (Quit Game?, Exit Game?).

**Validation:**
- `wfc_frontend_tests` 69 / 0 (new: quit confirmation / routing, match-start ownership, pause hides the HUD).
- Live checks with screenshots: two cold boots with an old profile (intro plays both times); Extras Movies / Credits
  (play, skip, menu input back, locks); Accounts (type, edit keys, Enter, Escape, click focus, sign in → lobby name);
  Create a Character (Weapon Slot 2 list, rename + restart, chassis screen, reset); ticker over 29 s; game lobby,
  party lobby and Exit Game boxes and their destinations; Choose Character before / after the match start.
- Navigation stress harness (`tools/frontend/nav_stress.sh`): 5 menu cycles + 2 private matches, 133 checks: no
  soft-lock, the main menu fully restored every cycle, no orphaned modal, AS heap 2263 / 76 display nodes / 48 cached
  shapes identical every cycle, memory 2,147 -> 2,105 MB. It found one real bug: Pause -> Resume left the pause movie
  drawn and focused over gameplay that already had input (fixed; rerun 64 checks PASS). One earlier run stopped at a
  Windows display-driver reset (event 4101) shared with two other sessions' game instances; not a frontend fault.
- Preview pawns verified in a merge preview with agents/rendering 504730f (original materials; bind pose, camera crop).

**Handoffs:** Gameplay (selected body: explicit Optimus fallback until ROBODEF / VEHDEF exports, agents/gameplay 1a4e36b;
look settings and kill-feed damage type to wire after that merge; posed preview bodies), Rendering (UI render data
`-Map Standard`), Systems (volume categories), AssetTools (preset colours, ROBODEF / VEHDEF).
Next frontend item: the per-chassis customization camera (SeqVar_TnCustomizationCameraId).

## FRONTEND PASS 3 (2026-10-04, branch `agents/frontend`): PC build presentation, live scenes, HUD, settings, selection
Details, screen classification and handoffs: `docs/FRONTEND.md`. Based on integration/milestone-05 (fast-forwarded with
the user's approval).

**Playtest issues addressed:**
- **Movie audio:** the intro movies play with sound.
- **Keyboard / mouse:**
  - Enter / Escape / arrows drive every menu, and the mouse clicks the original buttons;
  - the PC SKU has no Press START gate, and the console presentation accepts Space as Start;
  - controller unchanged.
- **Back:** the "Back returns to Press START" bug is fixed (recovered `ShouldShowStartScreen` rule).
- **Dead buttons:** Settings, Escalation and Campaign open (`ProfileIsReady` callback).
- **Black backgrounds:**
  - the title renders over the UI_FrontEnd_m scene with the authored camera loop, and the lobbies over the
    customization room;
  - the full Rendering presentation is ready on merge (verified in a merge preview);
  - Settings / pause backdrops now tint the scene (blend modes) instead of covering it in cyan.
- **Loading:** animates through the whole load (worst frozen step 23.4 s → ~2 s), then the match.
- **Player name:** shown in the lobbies (variable-bound text), from a local identity.
- **Settings:** functional; the original values persist; resolution / fullscreen / VSync apply.
- **Character selection:** "Choose Character" with the four class presets from the roster package; the selection
  reaches the flow.
- **HUD:** the original Hud_GFX in matches (health, ammo, crosshair, score, clock, kill feed, announcements), plus the
  scoreboard (Tab / Back) and the results screen with real data.

**Validation:**
- `wfc_frontend_tests` 59/0.
- Keyboard and mouse paths, and a screen audit of every main-menu and lobby destination, with screenshots.
- Soak: 3 full cycles (frontend scene → lobbies → private TDM → loading → Choose Character → HUD, scoreboard, kills → results → lobby → frontend), no errors. Memory loaded 2,443 / 2,393 / 2,412 MB, after return 2,320 / 2,296 / 2,285 MB (flat). Loading presented 171–175 frames per load; the longest frozen step is 1.76 s.

**Not yet (handoffs in docs/FRONTEND.md §12):**
- Gameplay spawning the selected chassis; owners for the volume / camera settings (gamma: wired to Rendering's
  setDisplayGamma on merge);
- the lobby preview pawn; skeletal animation of the vignette ships (Rendering);
- XP / point events; kill-feed weapon icons (damage type);
- persistence of character customization.

## FRONTEND PASS 2 (2026-10-03, branch `agents/frontend`) — original Scaleform frontend, private match, return
Details and handoffs: `docs/FRONTEND.md`.

**Cold boot plays the intro chain** (Activision, Hasbro, High Moon logos, FMV_intro) **and then the shipped menus,
which drive the flow.** A SWF 8 / AS2 runtime with a GL renderer runs the cooked GFx
movies unmodified. Playable with keys or pad:
1. title → Press START → main menu → Multiplayer;
2. party lobby → Private Match → Team Deathmatch → Host Options (authored values) → Create Game;
3. game lobby (Streets thumbnail, rules) → Start Game → "MATCH STARTS IN 10…0";
4. loading screen (TF_LoadingScreen Bink under LoadScreen_GFX, "TEAM DEATHMATCH") → Streets TDM;
5. Esc → original Pause menu → Quit Game → frontend.

**Validation:**
- `wfc_frontend_tests` 59/0.
- Four-cycle soak: the frontend works after every return.
- Memory is flat after the GL release (~3.0 GB loaded / ~2.5 GB after unload; previously +1.6 GB per cycle).
- Merge preview with agents/systems builds and plays the authored frontend / lobby music and UI cues (two mechanical
  conflicts; resolutions in docs/FRONTEND.md).

**Not yet:**
- movie audio (video of the logos, FMV_intro and the loading Binks plays; Media Foundation);
- the UI_FrontEnd_m 3D backdrop (not exported);
- a threaded level load;
- GFx filters / blend modes;
- Gameplay match lifecycle (a PROVISIONAL adapter starts the match at once);
- HUD movie ownership.

## FRONTEND PASS 1 (2026-10-03, branch `agents/frontend`) — boot → frontend → lobbies → match → return
Base: integration/milestone-04 (a03d7f7). Details, provenance and handoffs: `docs/FRONTEND.md`.

**Default boot is now the frontend.**
- The executable boots `UI_FrontEnd_m`, then runs the authored chain: MovieLoader → intro chain → FrontEnd UI.
- **Multiplayer:** `Online.OpenPartyLobby(GTS_TeamGame)` → party lobby → `EditGameMode` / `PlayPrivateGame(TDM)` →
  game lobby (host's choice) → `SetSelectedMapID(508)` → `BeginLobbyExitCountdown` → 10 s countdown → team pick →
  match URL → loading ("Team Deathmatch" / "in Streets" + 3 tips) → the existing Streets runtime in TDM.
- **Return:** Escape = ShowMenu → Paused (PauseMenu_GFX_1) → `Game.QuitToMainMenu` → back to the frontend. Launching
  again works; tested TDM then DM in one process.
- **Legacy direct boot is unchanged:** `WFC_BOOT=match`, or any existing automation variable
  (`WFC_SMOKE_FRAMES`, `WFC_GAMEMODE`, …).
- **Catalog:** maps / modes / playlists / localization are read from the AssetTools manifests. Streets is the only
  provider with rebuild runtime data; the other 12 are disabled (HasRequiredAssets). A new map's runtime directory
  enables it with no code change; this is tested.

**Not yet:**
- **(Superseded by pass 2.)** No GFx presentation: frontend screens were black. The menu flow was driven
  by the original bridge calls via `WFC_FRONTEND_AUTOPLAY=TDM,508` or `WFC_FRONTEND_SCRIPT`.
- **No video decoding.**
- **No Gameplay match lifecycle.** A PROVISIONAL adapter starts InGame immediately.

**Validation:**
- `wfc_frontend_tests` 59/59;
- scripted runs: boot → TDM Streets (240 match frames); TDM → pause → quit → DM relaunch → exit;
- clean Debug build (existing warning in WfcMapFx.cpp only).

## INTEGRATION MILESTONE 04 (2026-10-03) — branch `integration/milestone-04` — first full-map Streets playtest build
Purpose: one playable Release build containing all current MP_IAC_Streets work on the corrected world.
Integration only: no new features, no RE, no tuning. Base: integration/milestone-03 (356c352).

**Human playtest executable:** `F:\Transformers Rebuild\Rebuild\build\release\bin\wfc_rebuild.exe`.
- Release build, configured from this tree; render data in `work\render`.
- Default mode is DM; `WFC_GAMEMODE=TDM|CTF|KOTH|EXT|DOM` selects the others.
- A Debug build is at `build\bin\wfc_rebuild.exe`.

### Merged owner heads (`--no-ff`, in this order)

| Order | Branch | Head | Merge commit | Conflicts |
|---|---|---|---|---|
| 1 | agents/gameplay | d122ef4 | 73147fc | `World.cpp` (2 hunks), `STATUS.md` |
| 2 | agents/systems | 9dbe3ba | 6fd53f3 | `STATUS.md` (code merged cleanly; one interface adapted, below) |
| 3 | agents/rendering | 7aadc3c | abedbc0 | `Renderer.h`, `World.cpp` (2), `SkinnedModel.h/.cpp`, `Application.cpp`, `FIDELITY.md` |

- Follow-up integration commit `4af1ad4`: shared `WFC_PICKUPTEST` name.
- agents/experimental (5120c6f) was **not** merged. Its tools were run read-only from an isolated copy
  (`work/m4gate`).

### Conflict and interface resolutions (owner semantics)
- **Light-visibility query (`World.cpp`):** Gameplay's choice of the weapon collision world for
  zero-extent line checks, combined with milestone-03's single exact DDA `segmentHit` (Systems
  firing-cost fix). The 2 m march is no longer needed.
- **`World::tick` start:** Systems' `WFC_HITCHLOG`, then Gameplay's `mapState_.tick()`.
- **Pickups (Gameplay → Systems, adapted):** Systems reduced `PickupPresentation` to the sound only
  (`onTaken`); effect state is now Rendering's `setMapEffectState`. The milestone-03 bridge
  (`setPickupHidden/Visible`) was replaced by Systems' documented glue: Taken plays the PickupSound once
  on the receiving pawn; Respawned plays nothing. Systems' `LevelFx` is gone, so steam and pickup FX are
  simulated only by Rendering.
- **`Renderer.h`:** both branches carried the identical map-state interface; Rendering's
  `setDestructibleState` was added.
- **Game rules:** Gameplay's match mode supplies the authored rule set (`gameRulesForMode`).
  Rendering's interim `WFC_GAMERULES` (documented "until Gameplay does") was retired.
- **Placeholders:** Gameplay's authored factory/destructible loading. Both owners' opt-in hooks are
  kept: `WFC_GRAYBOXPICKUPS`, and the test dummy via `WFC_TESTDUMMY` or `WFC_DAMAGETARGET`. Nothing
  placeholder spawns in normal play.
- **`loadAnimationsByName`:** both branches added it. One implementation is kept (Gameplay's, using the
  shared `parseClips`; it returns the clip count) and serves Gameplay's Optimus arm and Rendering's
  totems. Rendering's duplicate and its unreachable `.gltf` branch were removed.
- **Destructible presentation (Gameplay → Rendering, adapted):** Rendering draws intact/stump from
  Gameplay's HmDestructionState, but nothing called `setDestructibleState`.
  `World::syncMapPresentation` now pushes it every frame, except while Rendering's `WFC_DESTRUCTSTATE`
  diagnostic forces a state.
- **`WFC_PICKUPTEST` (4af1ad4):** Gameplay's `=1` measurement mode exited `init` before Rendering's
  `=<factory>,<take>,<respawn>` diagnostic could run. Gameplay's mode now runs only for a value
  without a comma.
- **Kept:** Rendering's evidence-scoped translator channel reading. `vector_channel_proven.txt` lists
  `ParticleBase_BW_MAT` only, proven by UE3 type rules; the other ~22 world materials keep the
  full-vector reading.

### Data: the runtime consumes the corrected current Streets data
- Map data is read directly, every launch, from
  `F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets`:
  `world.glb`, `collision_pawn.glb` (movement), `collision_weapon.glb` (traces and light visibility),
  `physics.json`, `gameplay.json`, `spawnpoints.json`, `navigation.json`, `audio.json`, `render_index.json`.
  There is no copied world and no collision cache.
- That data is the corrected AssetTools 8d8195e export (`Scale*Scale3D*R*T × CachedParentToWorld`).
  Checked on disk: all 1906 collection members use the corrected transform source; 479 are mirrored and
  907 non-uniformly scaled. `render_index.json` is AssetTools da67634.
- Render data was regenerated from scratch (the old copy was kept as `work/render_m03_old`):
  216/216 materials, 25 decals, 1975 lightmapped components, 6 movers, 45 map-FX components.
  It is identical to agents/rendering's own generation: GLSL for all 216 materials, plus byte-identical
  BSP, decals, CLUT, LVV, movers and map FX.

### Builds
- Clean Debug (`.\build.ps1 -Jobs 2 -Clean`) and clean Release (`build\release`): 0 errors.
- One warning, pre-existing and identical on agents/rendering: unused variable `reading` in `WfcMapFx.cpp`.

### Automated results (merged tree)

| Suite | Result |
|---|---|
| `wfc_fidelity` (Debug and Release, this tree) | 190 / **2 FAIL** / 21 known; identical to milestone-03, no check changed |
| Experimental 5120c6f harness on the merged tree | **337 pass / 0 FAIL / 9 known** (stale spread checks retired upstream) |
| Collision (`--only collision --map`) | 9 / 0 / 2 known |
| `WFC_MAPTRAVERSE` | ORACLE 852/852 authored ReachSpec runs (robot + vehicle), 0 falls, 0 floor gaps |
| `WFC_MAPTRAVERSE` tour | robot 100/122, vehicle 103/122 legs, 0 falls (= Gameplay Pass 19) |
| `WFC_MAPTRAVERSE` sweeps / coherence | 984 boost/jump sweeps, 0 KillZ; **0 missing/displaced collision** |
| `WFC_TRAVERSE` | 160 runs, 0 falls, 1 snag (identical on agents/gameplay alone) |
| `WFC_VEHTEST` | rest COM 1.287 m (native), stops 0.5 s, dash 30 m/s, jumps as Gameplay Pass 14 |
| `WFC_PICKUPTEST` | respawn 30.02 / 60.02 / 119.99 s (authored 30/60/120); destructible settles 10 s |
| `WFC_MODETEST` | per-mode objective/totem/KOTH state, authored rule sets |
| Shadow self-test | 32/32 |
| DLE | 20/20 |
| LVV | C++ vs Python 400 points, 0 mismatches |
| `verify_permutations` | **216/216** |
| Map audit | 2399 correct / 23 intentionally invisible / 360 unknown (BSP without authored lightmaps); 1948 meshes, 25/25 decals, 32 emitters drawn + 13 intentionally invisible, LVV and destructible active (= Rendering M05) |
| Audio native suite | **557 / 0** |
| Runtime probe | 31 / 0 / 3 known |
| Experimental M03 gate (5120c6f) | **702 pass / 0 FAIL / 13 known / 543 info** |

Gate detail: 0 regressions in audio attachment, transformation, vehicle, map completeness, fine aim and
other. Map-completeness improvements:
- movers wrong-effect 12 → 0;
- props wrong-material 124 → 0;
- level emitters, pickup FX, pickups and destructibles not-instantiated → 0.

Experimental `m04-world-audio` and `m04-captures` ran. `m04-ambient` crashed inside its own script
(PowerShell null array), so it has no result.

### Normal-play smoke test (Release, DM, chase camera unless noted)
Scripted input; stills, logs and A/B sheets are in `work/m4/smoke/`.
- **Coverage:**
  - 10 traversal routes from spread authored starts: 6 robot, 3 vehicle, 1 robot→vehicle.
  - 30 s of game time each; 0 falls; architecture, interiors, ramps, stairs and platforms crossed.
- **Reverb/zones:** 8 different zones entered with reverb switches (exterior, Decepticon room
  upper/lower, train tunnel, auto rooms 01/02, neutral hall, stairwell).
- **Ambient bed:** 70 emitters, 50 sounding (authored per-cue limits).
- **Voices:** vary 65–96; they reach the 96-channel budget, where priority stealing applies
  (≤4 refusals per run). Not a leak: counts fall back.
- **53 fixed views** (domes, SkyBeam area, steam, pickups, 10 map areas × 4 directions): corrected
  large-scale and mirrored architecture, interiors, skyline, decals, baked lighting, fog, CLUT.
- **Merged vs agents/rendering alone,** same views: mean pixel difference 0.00–1.81/255 (only animated
  content differs).
- **Animation over 4.5 s (lockstep):**
  - the dome visibly rotates (the difference is confined to the dome);
  - steam spawns and plumes;
  - the SkyBeam region changes (motion detected; the beam shaft itself is not isolated in these views).
- **Mode visibility:** the CTF objective base is hidden in DM and TDM and shown in CTF.
- **Pickup in play:** ammo crate visible → walked over (PickupSound attached to the pawn) → gone →
  back after 30 s.
- **Firing and vehicle FX visible:** muzzle, tracer, impact, steam, hover distortion rings,
  afterburners, boost plumes.
- **Character shadows are default-on:** on/off A/B shows darkening on Optimus at some locations; it is
  absent where the native light-environment gates don't create a shadow.
- **No placeholder or diagnostic content in normal play.** The magenta/green locator only appears with
  `WFC_DEBUGCAM`; the debug overlay only with `WFC_DEBUGDRAW` or the **B** key.

### Performance (Release, RX 7900 XTX)
- **Spawn-view medians:**
  - idle 3.25 ms; firing 3.30; sustained fire 3.30; fine aim 2.40;
  - hover 3.56; boost 3.43; vehicle move 1.41; both transforms 1.35–1.43 (moving).
- **Traversal routes:** robot 2.72 median (max 3.26), robot firing 2.02 (max 4.21), vehicle boost 2.80
  (max 3.42).
- **Compared with milestone-03:** equal or faster everywhere.
- **Particles and audio:** particles drain to 0 after fire; cues return to the ~52 bed. No leaks.
- **Hitches:** one startup hitch (~350 ms, tick 2–3); otherwise ≤ 1 per 40 s route.
- Experimental's `perf-release` first flagged boost at 12–14 ms during its first ~5 s, outside the
  renderer. It did not reproduce: a rerun of the same tool gave a 3.43 ms median with 0 spikes. The
  original run followed directly after a 40-minute capture job. Its remaining "check" flags compare the
  spawn view with milestone-03's open-view numbers.

### Stale or provisional test expectations (not product regressions)
- **This tree's (older) harness:**
  - `weapon.spread_after_10` and `spread_cap`: retired upstream (Experimental 28e093f).
  - Vehicle hover-height (1.85 m), boost-top-speed and jump-apex expectations predate the native hover
    rigid body.
  - `missing.prefab_instances`, `missing.static_destructibles`, `unrendered.decals`,
    `unrendered.level_emitters`, `boost_presentation_emitted`: contradicted by current presentation.
  - The 9 KNOWNs in Experimental's own harness are its current list.
- **Runtime probe:** `boost_fx_emitted` reads the weapon-FX counter only.
- **Experimental tooling:** `m04-ambient.ps1` crashes (null array).

### Genuine product regressions
- **None found against milestone-03** in any suite, A/B or route.
- The `WFC_TRAVERSE` snag at `TnTeamPlayerStart_10894` (vehicle, dir 1) and the coherence hull notes
  reproduce on agents/gameplay alone (Gameplay behaviour on the corrected world, not a merge effect).

### Remaining player-visible Streets discrepancies
- **Chase camera sinks into or passes through geometry** at several spots (provisional camera
  collision). MAPTRAVERSE: 72 components with authored BlockCameras were viewed through.
- **Authored collision hulls smaller than visuals:** the pawn passes into 49 visible props, by
  authored data. A further 49 have no pawn collision at all.
- **Unlit BSP:** 360 BSP elements have no authored baked lighting. Some areas are very dark, and some
  west-perimeter DefaultMaterial BSP faces are PARTIAL.
- **One rotating dome renders almost black from the north-east,** the same on agents/rendering
  (Rendering to check).
- **Character shadows are subtle** and location-dependent: the frustum fit and screen-to-shadow terms
  are still PARTIAL.
- **SkyBeam:** motion is present, but the beam's own visibility needs the human eye.
- **Pickups:**
  - ammo-crate attachment offset is native UNKNOWN;
  - crate spin phase is PROVISIONAL (continuous from map start);
  - some particle semantics are PARTIAL.
- **Audio:**
  - 20 of 70 ambient emitters are silent by authored per-cue limits;
  - under heavy fire plus ambience the 96-voice cap steals voices.
- **Provisional vehicle behaviour:** hull clearance and the boost tire coefficient.
- **Gameplay KNOWNs:** a vehicle-start snag (1 of 160 runs), robot jump apex 5.14 vs 5.0 m, step-up
  0.35 vs 0.37 m.

## INTEGRATION MILESTONE 03 (2026-10-02) — branch `integration/milestone-03`
End-of-milestone integration only: no new features, no new RE, no tuning. Base: integration/milestone-02
(e8036f6). Evidence sources used by the branches (not merged): AssetTools 7a69756, ReverseEngineering 7033f18.

### MERGED COMMITS
Merged with `--no-ff` in this order:

| Order | Branch | Head | Merge commit |
|---|---|---|---|
| 1 | agents/gameplay | 0762f01 | 5826fd9 |
| 2 | agents/systems | d6932dc | 300561b |
| 3 | agents/rendering | 19cd213 | 752a46f |

agents/experimental (425eb1a) was **not** merged; its tools were only run read-only from an
isolated overlay (see AUTOMATED RESULTS).

### CONFLICTS
- **Gameplay:** none.
- **Systems:** `PlayerController.h`, `Application.cpp`, `World.cpp` (2 hunks), `STATUS.md`.
- **Rendering:** `VehicleFx.cpp` (2 hunks), `FIDELITY.md`.
- `Collision.cpp`: both Gameplay and Systems carried the same DDA segment walk (4965ec4). It merged
  once, with no conflict.

### OWNER RESOLUTIONS
- `PlayerController.h`: Gameplay's camera setters (now also setting `viewYaw_`/`viewPitch_`) plus
  Systems' read-only `moveForwardInput()` (engine-load audio).
- `Application.cpp`: Gameplay's `WFC_AUTOBOOST=N` (from frame N; `=1` keeps the old behaviour) plus
  Systems' `WFC_AUTOBOOST_CYCLE` / `WFC_AUTOJUMP_EVERY` test hooks.
- `World::tick`: Gameplay's pickup/destructible event clears plus Systems' hitch log. Systems' profiler
  scope wraps Gameplay's `applyToPawn`; Gameplay's `gameplayRamContacts()` is kept.
- `VehicleFx.cpp` (Systems owns emitter state/lifetime, Rendering owns shading): Systems' implementation
  of Rendering's own FX-material handoff (5e74895) is kept. It passes per-emitter `ParticleModuleRequired`
  material paths, unclamped HDR colour when `evaluatesFxMaterials()`, and spawns the material-only
  distortion/modulate emitters. Rendering's earlier per-texture `kTexMaterials` table is superseded by
  it and dropped. The renderer interface (`ParticleBatch::material`, `evaluatesFxMaterials`, name
  resolution for full paths or short names) is unchanged.
- `STATUS.md` / `FIDELITY.md`: every owner section is kept; nothing was deleted.

**Interfaces adapted so that one owner's behaviour reaches another owner's code:**
1. **Pickups (Gameplay → Systems).** Gameplay's `PickupFactory` state machine raises `PickupEvent`s.
   Systems' `PickupPresentation` expects `SetPickupHidden` / `SetPickupVisible` / `AnnouncePickup` calls.
   Neither side called the other, so pickup sounds and effect state were silently lost after a clean
   merge. `World::tick` now forwards each event to Systems' API after the actors tick:
   - Taken → hidden + PickupSound on the recipient pawn;
   - Respawned → visible.
   The two tables are joined by authored actor name (both come from AssetTools 7a69756). Neither
   owner's logic changed.
2. **HUD crosshair (Gameplay → Rendering).** Rendering's `setReticle` computed its own spread
   (bloom × fine aim). Gameplay owns the recovered TnHUD observers: raw effective spread = bloom ×
   airborne multiplier × fine aim 0.5, re-notified only when it moves by more than 0.002.
   `World::draw` now feeds the reticle from `PlayerController::hudAimState()` plus the last notified
   spread (new read-only `hudSpread()`). Rendering still draws the authored `mc_crosshairIonBlaster`,
   with no scope.

The **material-translator R/G/B/A vector-output fix is not included.** It was never committed on
19cd213, and all 211 generated shaders are identical to Rendering's own output.

### BUILD
- Clean Debug: `.\build.ps1 -Jobs 2 -Clean` → `build\bin`.
- Clean Release: CMake `-DCMAKE_BUILD_TYPE=Release` → `build\release\bin`.
- Both builds: 0 warnings / 0 errors, producing `wfc_rebuild.exe` and `wfc_fidelity.exe`.
- Both CMake caches point at `F:/Transformers Rebuild/Rebuild` on integration/milestone-03, not at
  any agent worktree.
- Render data was regenerated (`tools\render\build_render_data.ps1`): 211/211 materials, BSP, 25 decals,
  CLUT and `lvv_0.bin`. Output is equivalent to agents/rendering's own: identical GLSL for all 211;
  `bsp.glb` / `decals.glb` / `clut.png` are byte-identical; the JSON differs only in path spelling.

### AUTOMATED RESULTS
Merged tree (Release unless noted):

| Suite | Result |
|---|---|
| `wfc_fidelity` (Debug and Release) | 190 pass / **2 FAIL** / 21 known / 119 info / 1 skip |
| `wfc_fidelity --map` | 192 / 2 / 21 / 135 / 0 |
| `wfc_fidelity --no-assets` | 133 / 2 / 20 / 37 / 18 |
| Collision (`--only collision --map`) | 9 / 0 / 2 known |
| Audio native suite (`tools/systems/audio_native_suite.cpp`) | **533 pass / 0 fail** |
| Shadow self-test (`WFC_SHADOWSELFTEST`) | 32/32 |
| DLE test (`WFC_DLETEST`) | 20/20 |
| LVV C++ vs Python query (`lvv_query_check.py`) | 400 points, 0 mismatches; blob structure OK |
| Material permutations (`verify_permutations.py`) | 197/211 match the compiled permutations; 14 differ (FX/monitor parameter sets) |
| Map audit (`audit_map.py`) | 2282 correct / 3 incorrect / 49 not rendered / 403 unknown; 25/25 decals; active dump byte-identical to agents/rendering's |
| Vehicle tests (`WFC_VEHTEST`, measurements) | rest COM 1.287 m (native 1.287), stop 0.500 s (native 0.5), dash 30 m/s, hover jump +3.80 m, boost jump 6/14 |
| Pickup tests (`WFC_PICKUPTEST`, measurements) | respawn 30.02 / 60.02 / 119.99 s (authored 30/60/120); touch vs CheckTouching per RE; destructible settles 10 s |
| Runtime probe (merged tree's version) | 26 pass / 4 FAIL / 4 known (see notes) |

Harness notes:
- The harness is **identical, check by check, to the agents/gameplay 0762f01 head**. Every change since
  milestone-02 is Gameplay's recovered behaviour against older expectations:
  - 2 FAILs;
  - wall slide and weapon restore 25%: KNOWN → PASS;
  - 6 vehicle checks: PASS → KNOWN.
- `WFC_VEHTEST` / `WFC_PICKUPTEST` exit 1 by design (measurement modes stop `Application::init`).
- The audio suite's header build line lacks `src/game/PickupPresentation.cpp` (added in Systems Pass 6).
  It was built with it added.

Runtime probe notes:
- The 3 reload-presentation FAILs come from a frame-count test design. The Release exe runs at 700–830 fps
  facing a wall, so 1500 frames is ~2 s and the magazine never empties. With the Debug exe the reload
  section passes 5/0.
- `rendering.no_gl_errors`: `ParticleBase_BW_MAT` fails to compile (`vec4 t7 = vec4(t6, t6)`). That is
  the deliberately reverted translator vector-output issue; the material uses its glTF fallback.

**Experimental M03 gate** (`m03-gate.ps1`) ran from Experimental 425eb1a's `tools/fidelity` against this
merged tree, in an isolated copy (`work/m3gate`, not committed). Result: **468 pass / 5 FAIL / 26 known /
501 info**.
- The 5 FAILs:
  - the 2 stale spread checks;
  - `vehicle_visual` and `perf_counters` reports missing (tool errors: PowerShell parameter type /
    missing `counts.txt`);
  - `r2v_chase_moving.no_camera_pop` (2 frames with a > 1 m camera jump).
- The same camera pop and mesh-switch timing reproduce on the agents/gameplay 0762f01 head, so they are
  Gameplay-owned and not a merge effect.
- Gate improvements: 0 audio-attachment regressions (39 improved), 0 vehicle regressions,
  0 fine-aim regressions, sustained-fire perf improved.
- The gate's build step fails on CMake's harmless "unused-cli" stderr under `ErrorActionPreference Stop`.
  It was rebuilt manually and run with `-SkipBuild`.
- Experimental's `tools/fidelity/CMakeLists.txt` needs `SoundMixer.cpp` + `PickupPresentation.cpp`
  (Systems added them to the harness).

### PERFORMANCE
Release, RX 7900 XTX, WFC path, steady-state windows of 120 frames:

| Scenario | ms/frame |
|---|---|
| Idle | 3.8–4.1 |
| Movement (jog/strafe) | 1.0–1.5 |
| Firing | 4.0–4.1 |
| Sustained firing (4 magazines + reloads, 6000 frames) | 3.75–4.6 |
| Fine Aim + fire | 3.1–4.2 |
| Robot → vehicle (moving) | 0.9–1.7 |
| Hover | 4.3–4.5 |
| Boost | 1.2–1.7 |
| Vehicle movement | 1.2–1.8 |
| Vehicle → robot (moving) | 1.1–2.1 |

- Movement and vehicle runs face away from the dense spawn view, which accounts for the lower costs.
- **Shooting-FPS regression resolved:** Debug sustained fire is 8.5–9.0 ms against 62–68 ms at milestone-02.
  The fixes came from the branches: the DDA segment walk, light environments shared by mesh particles,
  and Systems' firing-cost fix.
- **Shadow cost:** included in all figures above (default-on).
- **Particles:** 120–260 live while firing; back to 0 after firing stops.
- **Audio:** no leaks. Cue instances return to the 25–27 ambient bed in every run. Gate audio-attach
  (attachment suite, 12 scenarios): 120 pass / 0 FAIL / 123 info; no offending cues.
- **Hitches:** one ~350 ms startup gap at tick 2–3 in every run. Sustained fire has 4 sporadic 31–92 ms
  game-thread gaps, 3 of them after ammo was exhausted. **No transform spikes** (0 hitches in both
  transform runs).
- The gate's Debug-profiler `idle.steady 6.06 → 10.61 ms` flag uses an instrumented Debug variant, not
  this measurement.

### KNOWN STALE EXPERIMENTAL ASSERTIONS
Product code was **not** changed to satisfy any of these.
- `weapon.spread_after_10` (expects 0.13) and `weapon.spread_cap` (expects 0.18). They encode the
  superseded no-recovery-while-firing model. Native per-tick CooldownSpread gives 0.10 after 10 shots and
  reaches the cap at ~4 s (Gameplay Pass 16, RE d50e2a9).
- `movement.vehicle_hover_height` / `original_vs_rebuild.movement.vehicle_hover_height` (expects 1.85 m).
  Native hover support is springs only, with no ride-height target; rest COM is 1.287 m (Gameplay
  Pass 13/14).
- `movement.vehicle_boost_top_speed`, `boost.physics_boost_speed` (expect a flat 30 m/s). Native boost =
  Lerp(2500, Drag) with drag v²·g/6000², and the tire coefficient is still PROV. Experimental to
  re-baseline.
- `movement.vehicle_jump_apex` (3.71 m ballistic). Native hover jump adds the spring push: +3.80 m over
  rest in VEHTEST.
- `orientation.vehicle_heading_follows_camera_turn` (0°, measured 2°). Hover yaw follows the smoothed
  controller rotation; Experimental to re-check against the native smoother.
- `missing.static_destructibles`. The single authored WallPanelSign is placed, but authored far outside
  the play space (Gameplay Pass 15).
- `unrendered.decals`. 25/25 decals render (Rendering Pass 8; map audit).
- `unrendered.level_emitters` / `boost_presentation_emitted` / probe `boost_fx_emitted`. Systems spawns
  level FX and vehicle boost/hover/ram FX; the probe reads the weapon-FX counter only.
- `missing.prefab_instances`. Prefabs are composed (Rendering Pass 8).
- Missing Ion Blaster scope/ADS, missing shoulder offset, missing footstep surface asset, graybox pickups:
  all contradicted by authored data (no scope for the Ion Blaster; shoulder offset recovered; Streets
  surface audio recovered by Systems; authored pickup factories).
- Runtime-probe reload checks: they should be time-based, or run under `WFC_LOCKSTEP`, not frame-count
  based.

### CONFIRMED ORIGINAL BEHAVIOR PRESERVED
- **Gameplay 0762f01:**
  - native hover rigid body, suspension, jumps, drift, dash and nitro;
  - authored transform visibility windows and weapon restore at 25% (now PASS); hand shrink;
  - ram response and contacts;
  - pickup state machines with 30/60/120 s respawn, ammo-crate highlight beam gate, health
    CheckTouching;
  - raw effective spread × airborne × fine aim 0.5, and the 0.002 HUD gate (now also driving the
    renderer's crosshair);
  - no ADS/scope.
- **Systems d6932dc:**
  - native SmartPan/PreferPlayer, attachment, Streets reverb zones, mixer presets, line/volume emitters,
    cue gain;
  - concurrency;
  - vehicle start/loop/end with `bLooping` as the loop source;
  - whole-sample FSB loop regions (7/7);
  - no loop-boundary wait on stop;
  - 0 leaks.
- **Rendering 19cd213:**
  - default-on character shadows (no `WFC_CHARSHADOWS`): LightEnvironment synthetic projector, native
    composite light, gates and two-pass blur (self-test 32/32);
  - DLE (20/20) and LVV;
  - vehicle material fixes;
  - hover/boost/distortion FX through their original graphs;
  - authored Ion Blaster crosshair;
  - pickup/wall-panel materials;
  - BSP winding, vertex lightmaps and decals.
- **Robot / vehicle transform shadow:** the renderer tags character draws by material package. Gameplay's
  arm mesh and both meshes in the fold go through the shared robot/vehicle shadow path.

### PARTIAL / PROVISIONAL / UNKNOWN
Left as the owners labelled them:
- **Rendering:**
  - shadow frustum fit, the remaining ScreenToShadow texel terms, synthetic-light allocation internals,
    the blur tie case (`WFC_BLURTIE`, off);
  - particle mesh material fallback;
  - 4 InterpActors not rendered (2 repair nodes, 2 objective-pickup bases), the same on agents/rendering;
  - 14 permutation mismatches.
- **Gameplay:**
  - overshield amount/duration and ammo pickup amount where not authored;
  - vehicle hull clearance, the boost tire coefficient, ram victim masses;
  - the robot→vehicle-moving camera pop (2 frames);
  - v2r single-mesh switch at 0.683 s against the authored 0.098–0.663 s both-visible window
    (transform capture KNOWN).
- **Systems:** FMOD decoder seam internals, mixer fade curves/Duration, pickup particle systems not drawn
  (module flags UNKNOWN), 13 `PP_DECO_MECH_*` zone-pool KNOWNs.

### POST-PLAYTEST ITEMS
- **Rendering:** the material-translator R/G/B/A vector-output fix (~22 world materials change).
  `ParticleBase_BW_MAT` compile failure (`vec4(t6, t6)`) is a known instance.
- **Experimental:**
  - retire or update the stale assertions above;
  - fix `vehicle-visual.ps1` (switch parameter) and the `perf-counters` `counts.txt` path;
  - make `m03-gate.ps1` tolerate CMake stderr warnings;
  - add the two Systems sources to the harness CMake list;
  - make the runtime-probe reload scenarios time-based.
- **Gameplay:** the r2v-moving camera pop (2 frames) for review against the native camera strategy switch.
- **Systems:** add `PickupPresentation.cpp` to the audio suite's documented build line.
- **Systems/Gameplay:** nobody calls `notifyRamHit` yet (no pawn victims in the slice).

## GAMEPLAY PASS 22a (2026-10-05) — selected chassis spawns (no Optimus substitution)
- ChassisDef: per-chassis definition from AssetTools Characters/<ChassisId>/character.json + roster_package.json (collision); robot/vehicle glb, arm, sockets, ROBODEF/acrobatics/momentum, hover/car/suspension/wheel blueprints. WFC_CHASSISTEST 13/13: 27/27 MP chassis load; Truck reproduces every Optimus constant.
- Movement reads the pawn chassis (robot speeds/jump/collision; vehicle hover/drive/suspension/wheels). Car hover dash = dominant stick axis (TnCarForm.Hovering.DoDash); tank boost in hover sim (PROV); car roll + tank 180 + jet flight PARTIAL (natives requested from RE).
- Spawn: Match chassis check (no fallback: spawn refused + spawnError), ApplyTransformer (models/rigs/sockets) + ApplySpecialty (TnSpecialty speed x and Health_<Class> segments, ApplySpecialtyBuffs CONF). Versus health corrected: Leader 5x60, Scientist 3x60, Scout 4x50, Soldier 6x55, overshield 200.
- CharacterSelection carries Frontend GameFlow::SelectedCharacter fields (per-faction chassis, colours, loadout lists). WFC_CHASSIS=<id> boot option. TDMTEST 43/43, MODEPLAY 21/21, CAMSYNC unchanged.
- Open: weapons per loadout (Ion Blaster only), camera set per chassis, vehicle FX sockets per chassis, opponents as full pawns.

## GAMEPLAY PASS 21f (2026-10-04) — Frontend playtest follow-ups
- Selected body: spawn reports selectedChassis/drawnChassis/chassisFallback and logs "MATCH spawn ... drawn=Optimus fallback=missing ROBODEF/VEHDEF export" (no hidden placeholder).
- Match::requireCharacterSelection (identical to integration M06); TDMTEST 42/42 covers the spawn gate.
- PlayerController::setLookSettings(sensitivity, invertRobot, invertVehicle) for LocalProfile values.
- Contract doc: shipped PC Controls card with per-action implementation status; kill-feed damage type field.

## GAMEPLAY PASS 21e (2026-10-04) — transform clearance, roster contract, HUD state completion
- Vehicle->robot refused with NotifyCantTransform + TransformFailedSound when the robot cannot fit; displaced spot; post-fold ForceIntoForm(vehicle).
- CharacterRoster.h: selection before spawn, team faction, specialty default bodies, stable chassis IDs (Optimus default).
- HUD: weapon, damage direction, cant-transform pulse; kill feed rows 5 s + 1 s fade; FFA result empty.
- Handoffs: docs/handoffs/GAMEPLAY_FRONTEND_HUD_CONTRACT.md, GAMEPLAY_BOT_READINESS.md (bots = RECONSTRUCTION EXTENSION, not implemented).
- Open: CTF / EXT need a carried-objective weapon system; wall-pressed boost re-drop UNKNOWN; per-wheel suspension needs mount heights.

## GAMEPLAY PASS 21d (2026-10-04) — boost-state flicker fixed, vehicle contact, high-refresh guard
- Boost exhaust open/close: the frontal drop now uses the contact normal. Repro drops are real obstacles only; VEHTEST guard shows 0 drops on steps <= 0.3 m.
- Hull probes no longer stop the truck on 45-60 deg faces; boost body follows the slope (BoostScale engages).
- WFC_CAMSYNC 60/144/240 Hz guard; XFORM, oracle, chaos green. Details: FIDELITY.md PASS 21d.

## GAMEPLAY PASS 21c (2026-10-04) — Conquest and Power Struggle playable (shared match framework)
- DOM: 20 s capture per attacker, defender holds, +2 capture, +1 team / 3 s per node (bytecode).
- KOTH: zone from MatchStarting, +1 personal & team per pawn per second uncontested, 60 s rotation, end deactivation.
- Kills in objective modes: personal +1, team 0 (ScoreKillsMP). ActiveGameTypes cluster filter; objective spawn modifiers.
- CTF / EXT not implemented (need carried-objective weapons). WFC_MODEPLAYTEST 21/21, TDMTEST 39/39.

## GAMEPLAY PASS 21b (2026-10-04) — match HUD state, kill feed, match end, regen
- Kill feed events in TnDeathMessage form (switch, killer, victim, teams, damage type, 3 s lifetime).
- HUD state: spectating at 3 s, time limit, faction, end reason, MatchOver countdown, scoreboard rows.
- Regeneration 20 HP/s after 2 s (segment-limited), RE confirmed.
- WFC_TDMTEST 39/39. Details: FIDELITY.md PASS 21b.

## GAMEPLAY PASS 21a (2026-10-04) — M05 interlacing regression fixed; pre-match presentation
- Cause of the "interlaced" Optimus / truck: Pass 20 cached the camera position per 60 Hz step while the rotation is per render frame (~130 fps). Camera evaluated per frame again.
  - WFC_CAMSYNC: 1.28 deg -> 0.0003 deg on-screen jitter at 144 Hz.
- Countdown: no pawn / weapon drawn; the controller spectates from its login start (team start); spawn at the team start.
- Details: FIDELITY.md PASS 21a.

## GAMEPLAY PASS 20c (2026-10-03) — adversarial movement hardening
- WFC_CHAOS (60 starts x 20 s random play): 0 under the map, 0 KillZ, 1 stuck, 3 prop entries.
- Robot knee probe 0.55 m / 0.7 m [PROV]; oracle 852/852; transform under-overhang cases 54 -> 15.
- Open RE requests listed in FIDELITY.md PASS 20c.

## GAMEPLAY PASS 20b (2026-10-03) — Streets TDM session runtime
- Launch contract: original StartLevel URL -> World::launchMatch (map check, mode world state, fresh-level reset).
- Combat: team filter (AOE only), DamageHistory/assists, kill credit, segmented health, fresh-pawn respawn.
- HUD state (World::hudState) incl. player tags; KOTH rotation per RE §3.
- Test-only MatchOpponent participants; WFC_TDMTEST 30/30 (incl. second match in-process).
- Details: FIDELITY.md PASS 20b.

## GAMEPLAY PASS 20a (2026-10-03) — boost->robot fall-through fixed, wall probes, match core, camera (checkpoint)
- Human bug fixed: boost -> robot no longer drops under Streets.
  - Recovered cylinder-size lerp + swept falling floor check.
  - WFC_XFORMTEST: 0/1520 under the map, 0 KillZ (was 656 / 186).
- Truck hull wall probes (it passed through objects under 2 m); robot head probe (overhangs).
  - Authored path oracle still 852/852.
- Segmented health 550 + overshield 550; RE pickup acceptance; FastTrace touch rejection.
- Local TDM/DM match core: World::startLocalMatch, WFC_MATCH; WFC_MATCHTEST.
- RE camera obstruction implemented behind WFC_CAMRE (WFC_CAMTEST shows more visible clipping than the default; default kept).
- Details: FIDELITY.md PASS 20a.

## GAMEPLAY PASS 19 (2026-10-03) — Streets world state + corrected world/collision (AssetTools 8d8195e)
- Corrected 8d8195e world.glb, collision GLBs and physics.json, consumed fresh at load (no collision cache).
- Authored traversal: 852/852 TnReachSpec runs (robot + hover truck), 0 falls, 0 floor gaps.
- 984 boost/jump sweeps: 0 KillZ falls, 0 bound exits.
- Coherence audit: 0 missing/displaced collision; authored hull-smaller-than-mesh cases listed in FIDELITY.md.
- Match mode = authored TnOnlineGameSettings rule set (exact HasRule gating). World::setMatchMode.
- Spawn class per mode, authored start yaw.
- Gameplay pushes rules, actor hidden state, pickup FX state and the map clock to the renderer (setMapClock is new: Rendering handoff).
- SkyBeam: quat slerp + rotation-only InitialTM (validated). KOTH 60 s ZoneActiveTime rotation [HIGH].
- Tests: WFC_MAPTRAVERSE (oracle + tour + sweeps + coherence + camera). WFC_MODETEST adds rules + SkyBeam checks.
- Details: FIDELITY.md PASS 19.

## GAMEPLAY PASS 18 (2026-10-03) — boost steering + Streets mode state (RE a1666c2 / 0ab03b2)
- Boost steering now uses the recovered input path:
  - right-stick X (PC: mouse X), radial 0.25 deadzone, s·|s|, Nitro 0.3;
  - 25° front wheels and the tire lateral-force law, with yaw from torque and damping 5·(1−|s|)²;
  - the fixed yaw rate and lateral grip are removed;
  - the left stick is RollControl only.
- Streets mode table:
  - objective bases shown in CTF/EXT;
  - flag/bomb factories Disabled outside CTF/EXT;
  - capture/plant points inert outside their mode;
  - totems visible only in Conquest;
  - one Active KOTH zone.
- Markers: type strings for the future HUD.
- Tests:
  - WFC_VEHTEST boost-steering table;
  - WFC_MODETEST per-mode state;
  - WFC_STEERSTICK test hook.
- Details: FIDELITY.md PASS 18.

## GAMEPLAY PASS 17 (2026-10-03) — Milestone 04 Streets world state (AssetTools a23c675)
- Collision: movement uses the authored collision_pawn.glb; hitscan and visibility use collision_weapon.glb; KillZ −750 m.
- Truck hull extents now drive wall probe, clearance and ceiling.
- MapState (single clock from GameplayStarted):
  - rotating domes and the SkyBeam Matinee, with moving collision;
  - mode-dependent objective bases (WFC_GAMEMODE, default DM);
  - objective objects with marker data for the future HUD.
- Wall panel collision switches with its state.
- Test spawns: WFC_START / WFC_START_ACTOR, F6/F7 cycle the 84 authored starts.
- The test dummy only appears with WFC_TESTDUMMY=1.
- WFC_TRAVERSE=1 traversal test: 160 runs, 0 falls, 0 snags.

## GAMEPLAY PASS 16 (2026-10-02) — RE d50e2a9 runtime semantics (narrow)
- Pickups:
  - touch is an overlap begin; health re-checks overlapping pawns on respawn (CheckTouching);
  - sleeping keeps collision but ignores touches;
  - the highlight beam is only for ammo crates; the ammo crate rotates while available;
  - events carry the visual state and the receiving pawn's position for the pickup sound.
- Weapon spread:
  - linear per-tick recovery (whole range in 2 s);
  - airborne ×2 ramp (0.25 s up / 0.5 s down);
  - fine aim ×0.5;
  - the same effective spread drives the hitscan cone.
- HUD: TnHUD notify calls (spread > 0.002 filter; weapon class + fine aim together) via `PlayerController::hudNotifies()`.
- Harness: `weapon.spread_after_10` / `spread_cap` now fail against their superseded expectations (see FIDELITY PASS 16); Experimental should update them.

## GAMEPLAY PASS 15 (2026-10-02) — AssetTools 7a69756 authored-data handoff
- Fine aim:
  - the Ion Blaster keeps its authored crosshair (no scope or ADS);
  - Gameplay exposes `PlayerController::hudAimState()` (weapon class, standard/fine aim type, spread, crosshair visibility, target type);
  - the native camera is unchanged.
- Pickups:
  - 24 authored Streets factories (14 ammo crate / 9 health / 1 overshield) from gameplay.json;
  - authored respawn times, touch cylinder, health +50;
  - one event per state transition via `World::pickupEvents()`;
  - objective factories not instanced (CTF/Bombing modes).
- Wall panel: authored destructible state machine (20 health → destroyed → settled after 10 s) at its authored, out-of-play location.
- Graybox near-spawn pickups removed.
- Test modes:
  - `WFC_PICKUPTEST=1` (pickup respawn and destructible validation);
  - HUD fields added to the frame log.
- Details and superseded assumptions: FIDELITY.md PASS 15.

## GAMEPLAY PASS 14 (2026-10-02) — native RE Milestone 03 vehicle reconcile
Implements only what MILESTONE03_VEHICLE_NATIVE_FIDELITY.md confirms; provenance and measurements are in FIDELITY.md
PASS 14. Changes:
- spring gravity is the rigid-body GetGravityZ (−1940.4), giving rest COM 1.287 m = native L_eq;
- TnAccelerationAnimBlend hover pose weights;
- CurveAutoClamped camera offset curve;
- HandSkelControl hand shrink (0.1, instant);
- ram victims restricted to TnPawns, with robot RammedReaction and vehicle AddVelocity ×0.5 implemented;
- exact notify times with transform Rate.
WFC_VEHTEST=1 runs the deterministic handling measurements (rest, coast-down, 0.25/0.5 m bumps, 10 m drop,
hover/boost jump, dash, drift turn).
Still PARTIAL/PROV:
- hover RB mass link (M=2500);
- boost tire coefficient;
- hull contact (min clearance, ceiling probe);
- ram victim masses (no pawn victims in the slice).

## GAMEPLAY PASS 13b (2026-10-02) — Milestone 03 handoffs (Experimental + Systems)
- Experimental's "weapon usable before a visible gun" and "vehicle jump missing" were measured on milestone-02, before
  049f614. With Pass 13 the gun is drawn at 0.28 s and first usable at 0.50 s. Firing requires the drawn gun. The
  vehicle jump exists (hover + driving).
- Fine-aim offset semantics (raw OffsetCurvesByPCS): Y (lateral) = 300 UU in every row, so there is no lateral
  shift between default and fine aim (Experimental's ~0.05 m is expected). The 2 m difference is orbit-space X
  (+150 → −50: the camera moves 2 m back along the view; TnLocationOffset writes X = −OrbitDistance) [CONF].
- Arm: CP_OptimusArm_SKEL + OptimusArm_ROBO_ANIM loaded (text glTF + name-matched anims), attached at
  WeaponSocket_Secondary (R_Arm03_Elbow_XB, pitch 180), TnArmAttachment rules:
  - shown whenever the robot mesh is displayed with no drawn weapon (all of R→V, V→R before the restore);
  - ARM_Unequip (0.8 s) once the gun is drawn, then detached.
  HandSkelControl not applied (PROV).
- Landing: SharedAcrobatics.LandingAnims table picks Nav_Land / Nav_Land_02 / Nav_Land_03 by fall height and speed.
  None below 250 UU; a standing jump gives Nav_Land_02.
- Stop flicker (idle/walk for 1–2 frames): caused by the wall block zeroing velocity and parking one probe radius
  out, so the next, slower step crept forward. The wall block now advances to the gap, removes only the velocity
  into the wall and slides the rest of the step along it (re-probed for corners, no back-slide), with physWalking's
  displacement velocity. The harness wall-slide checks now pass (181/0/21).
- Ram: World::gameplayRamContacts → notifyRamHit during nitro (once per target per nitro), 300 damage (AI robot).
- CollisionWorld::segmentHit is Systems' validated grid walk verbatim. Gameplay only adds a normal-returning
  overload (suspension contact normals).

## GAMEPLAY PASS 13 / MILESTONE 03 (2026-10-02) — vehicle body, vehicle camera, transform handoff, firing cost
Branch agents/gameplay, fast-forwarded to integration/milestone-02 (e8036f6) first; clean build OK.
Driven by the Milestone 02 human playtest. Evidence: TransGame/HM_Engine bytecode (work/pass13/vehdis.txt,
camdis.txt, pcdis.txt via work/pass11/ue3dis.py) and authored data (VEH_SHARED_p, CAM_Driving_Strategies_p).

- **Hover = rigid body on four springs** (TnHoverCarSimulation.UpdateSuspension + TnSuspension/TnSpring):
  - mounts at SuspensionRadius 185 around the COM (Pass 7–12 wrongly used 185 as a ride height);
  - rays along body −Z, RestingLength 250;
  - implicit spring with Stiffness 10000 / Damping 4000 and per-spring mass Mass/4;
  - Truck_Physics mass 2500, COM (−47,0,5), inertia 2.27e7/4.64e7/5.89e7;
  - world gravity in the spring and RB gravity ×0.66 on the body;
  - result: the COM settles at 1.36 m.
  Pitch and roll now come from the springs and terrain, plus UpdateRoll (strafe input − yaw rate):
  - about 7° transient (2° held) when strafing;
  - about 4° banking into turns;
  - curbs and ledges tilt the body.
  Uprighting applies only with no contact. Yaw tracks the smoothed camera (PlayerInVehicleForm.PlayerMove).
- **Vehicle jump** (Hovering.UpdateJumping):
  - requires the ground (contact normal Z > 0.707), with a 0.3 s interval;
  - +1200 UU/s vertical, nose up 1 rad/s;
  - verified about 4.2 m rise, spring landing and rebound.
  Driving jump: local (600,0,1400) plus nose up 2 rad/s, air control, pitch-forward limit −25°.
- **Hover dash is forward only** (TnTruckForm.Hovering.DoDash overrides the car's dominant-axis dash):
  - refused while unstable (>30°);
  - local Z velocity is cancelled during the dash;
  - it ends with an immediate 100000 UU/s² decel to 1500.
- **Normal boost** = TnCarSimulation.UpdateBoost/Drag:
  - Lerp(MaxAccel, Drag(MaxSpeed), v/MaxSpeed) + ExtraBoost, so 30 m/s is the emergent limit;
  - the truck drops onto its wheels;
  - a frontal wall hit returns to Hovering (OnRigidBodyCollision 0.866).
  Steering = look-X input (GetNormalizedTurn), sign·s². Tire model PROV.
- **Nitro** unchanged in rules (×1.5 speed, ×0.3 steering, 3 s / 8 s). Ram collision is still not implemented.
- **Vehicle camera strategies** (HoverTruck_Optimus / Truck_Optimus; OverTheShoulder for the robot):
  - anchor, orbit, FOV and pitch-range per strategy;
  - HmC2Smoother (decoded) for FOV, offsets and orbit rotation;
  - strategy blends of 1.5 / 1.5 / 1.0 s;
  - nitro FOV 100 and orbit 650 (in 0.5 s, out 2.0 s);
  - Driving yaw locked to the truck, pitch chase at rate 3;
  - Wiggler3.
- **Robot camera offset corrected:** TnScreenSpaceOffsetByPitch Offsets is a static array of three vectors.
  - Default: (150,300,{150,−35,150}).
  - Fine aim: (−50,300,{80,−35,80}).
  Pass 11 read only the first vector.
- **Transform handoff:** the authored ToggleHidden notifies replace the 50% mesh swap.
  - Both meshes are drawn on the shared clip time: to vehicle 0.396–0.880 s, to robot 0.098–0.663 s.
  - Both meshes hang off the shared actor location: robot cylinder centre / vehicle bounds centre
    (TnVehicleForm.CalculateCylinderBounds).
  - The robot→vehicle "flattened robot freezes then the truck appears" came from the robot clip reaching its
    folded pose at 0.88 s while the truck was only shown from 1.0 s.
- **Weapon on vehicle→robot:**
  - the gun is attached to the robot mesh, drawn from 0.098 s;
  - restored at 25% of the fold (0.28 s), usable at +0.2 s (0.48 s);
  - firing also requires the gun to be drawn that step, so no gunless shot is possible.
- **Firing performance:** the gameplay traces were 23–34 ms/frame during sustained fire (AABB cell scan of two
  300 m rays per shot). The grid-walk traversal gives identical hits (0/3000 mismatches vs brute force) and
  about 0.1 ms. WFC_PERFLOG=N logs it.
- **Fine aim state** for presentation: `PlayerController::fineAimState()` (wanted, active, FOV blend, FOV).
- **Not done:**
  - CP_OptimusArm_SKEL attachment (raw umodel glTF, not loaded by the runtime);
  - the vehicle hull is approximated (min clearance, ceiling probe, wall probe);
  - ram collision;
  - wheel/tire steering.

## SYSTEMS MILESTONE 08 (2026-10-05) — every MP map, mode, character and movie-language audio
- **All 10 processed MP maps** (BrokenHope, Remnant, Berth, Rust, Seed, Streets, Molten, Debris, Complex, Gorge) go
  through one generic runtime. There are no per-map source branches.
  - Each map's Kismet sound graph comes from its own manifest: touch volumes, ambient zones / scenes, delays, gates,
    mixer presets, flybys, music, remote events and gameplay-owned `Game:` events.
  - The shared announcer and match cues are loaded per level.
  - Streets' graph matches the hand-flattened zones.
- **Mode audio:** flag / bomb / domination / CTF / round / KOTH messages are ported from the decompiled script.
  - Gameplay calls `world.matchAudio().*Message(...)`; there are no Systems timers.
  - The flag stingers and the round time-up / switching-sides music are included.
- **Characters / weapons:** 33 roster chassis and 53 weapon classes use authored audio profiles. Optimus is no longer
  hard-coded; the default profile reproduces him exactly.
  - Gameplay calls `setPlayerCharacterAudio(chassis)` and `setPlayerWeaponAudio(class)`.
  - In game, Bumblebee, Megatron, Starscream and the Heavy Pistol play their own sounds, with 0 missing cues.
  - Per-weapon audio:
    - world impacts use the weapon's DefaultImpactSound;
    - target hits use the victim's HitEffectPlayer HitSound, with retrigger and the damage type's bCausesBlood;
    - reload, idle, equip and holster sounds come from each weapon's own AnimSet.
    - The Ion Blaster is unchanged.
  - Vehicle form: each chassis's own HmPlayerVehicleAudioComponent (gears, one-shots, slots, tunables). Optimus is
    identical to the previous port (A/B probe, 92 / 92 events).
- **Movies (RE-confirmed):** language track 5 + L from GLanguage (`WFC_LANGUAGE`), speaker routing, the logos at 0.8
  volume, and other movies at the FX slider / 100 (`setMovieFxSlider`; default 80 → 0.8).
- **Lifecycle:** a 40-cycle real-device soak (`tools/systems/lifecycle_probe.cpp`): frontend (logo skip, title,
  party, lobby) → map N → frontend, rotating all 10 maps and the character profiles.
  - Every cycle returns to 0 voices, 0 streams, 0 level cues and the base mixer presets / cue table.
  - Decoded PCM never grows (61 → 52 MB).
- **Validation:** suite 609 / 0; wfc_fidelity 194 / 0 / 19; movie probe OK.
- **Handoff:** `docs/handoff/SYSTEMS_M08_AUDIO_HANDOFF.md`; FIDELITY.md MILESTONE 08.

## SYSTEMS MILESTONE 07 (2026-10-04) — boot-movie audio, match / announcer audio
- **Silent boot movies, fixed in Systems** (+ a ~25-line Frontend patch):
  - the movies' sound is their Bink audio tracks (10 mono: 5.1 with per-language centres);
  - `audio::MovieAudioPlayer` decodes and streams them beside the game mix, which CINE_MUTE mutes;
  - the movie preset is now unflushable (config);
  - verified on the real executable: all four boot movies play their sound.
- **Match audio:** `MatchAudio` ports TnAnnouncer + TnGameTypeMessage + progress announcements. Covered: the start
  dialogue and music, final stretch, end music by winner, 30 s / 1 min / 2 min and kills / points remaining.
  - Gameplay hookup patch ~20 lines.
  - The announcer voice follows the local team (Optimus / Megatron).
- **Second map:** MP_UND_Gorge loads through the generic path.
- **Lifecycle:** 6 real-executable match cycles, back to 0 voices / 36.5 MB every time.
- **Validation:** suite 566 / 0; wfc_fidelity 194 / 0 / 19.
- **Handoff:** `docs/handoff/SYSTEMS_M07_AUDIO_HANDOFF.md` + the two patches; FIDELITY.md MILESTONE 07.

## SYSTEMS MILESTONE 06 (2026-10-03) — frontend / loading / level audio lifecycle
- **Generic level manifests:**
  - `gen_level_audio.py` → `LevelAudio.inc`, merged with the AssetTools map manifest; one path for every level;
  - the UI levels' authored Kismet audio, including the frontend's Iacon / Kaon camera timeline;
  - the lobbies' music, bed and pools;
  - per-cue-asset limits for every level;
  - `CookedCueLimits.inc` removed; the Master compressor is global data.
- **Mixer:** all 47 categories (MUSIC_DRY 0.708 now applied); `CINE_MUTE_FOR_BINK` movie mute.
- **Lifetime:** `LevelAudioHost` (the level's music player, Kismet sounds, bank, presets; unload releases
  everything, including streamed music, at once); no frontend music under gameplay.
- **Contract:**
  - `FrontendAudioRuntime` mirrors the Frontend lane's `IFrontendAudio` seam 1:1 (standalone, no World);
  - World exposes the same calls in a match; `setAudio(a, false)` skips the slice map.
- **Validation:**
  - suite 544 / 0 (30 + 12 real-device lifecycle cycles, 20 seam cycles, 6 orbit loops);
  - game soaks 46 + 39 + 181 level transitions, 0 errors, no growth;
  - wfc_fidelity 194 / 0 / 19.
- **Handoff and classification:** FIDELITY.md MILESTONE 06.

## SYSTEMS MILESTONE 05 (2026-10-03) — runtime lifecycle for frontend → loading → match → return
- **Map audio lifecycle:**
  - `World::loadMapAudio` / `unloadMapAudio` / `resetSystemsForMatch` / `tickAudioOnly`;
  - unload leaves 0 instances / queued events / map cues / map presets / map samples / voices;
  - verified over 12 Streets ↔ synthetic-map cycles (suite) and 15 in-game reloads (soak).
- **Data-driven:** map reverb presets come from audio.json, cue limits from the generic cooked-cue table, pickup
  sounds per factory class; no Streets branch remains in Systems code.
- **Frontend audio:**
  - `MusicPlayer` (HmMusicPlayer port);
  - `FrontendAudio` (GFx UI sounds by name, the authored UI-level tracks, level change);
  - the 16 UI cues plus 3 streamed music cues with prefetch.
- **Map events:** one PickupSound per take on the receiving pawn; no mover or mode-gated audio; the integration-04
  glue stays compatible.
- **Soak:** robot / vehicle / control 24 000 frames — voices, cues and queues bounded; PCM 97.2 MB flat; 0 dropped
  voices; no frame-time drift.
- **Validation:** suite 505 / 0; wfc_fidelity 194 / 0 / 19.
- **Handoff:** the exact frontend / integration call sequence is in FIDELITY.md (MILESTONE 05).

## SYSTEMS MILESTONE 04 INTEGRATION PREVIEW (2026-10-03)
- **Merges:** clean into integration 356c352 (STATUS.md only) and experimental; Rendering's VehicleFx conflict is
  already resolved in integration.
- **Gameplay d122ef4:** 5 additive conflict hunks, resolved and built in `work/m4/merge_preview`.
  - The pickup-sound glue plays each PickupSound once per take (Gameplay PICKUPTEST with audio).
  - Resolutions are in FIDELITY.md.
- **Harness:** 2 stale HUD-spread expectations, retired by Experimental 28e093f.

## SYSTEMS MILESTONE 04 ADDENDUM (2026-10-03) — map FX runtime yielded to Rendering
- **LevelFx removed:** Rendering 411c970 (WfcMapFx) simulates and draws the 8 steam emitters and pickup effects.
- **PickupPresentation:** now the pickup sound only (`onTaken`); effect state is Rendering's `setMapEffectState`.
  The integration glue is in FIDELITY.md.
- **Objective beam:** confirmed for flag/bomb (RE 00dcb20); the conflict is resolved.
- **Validation:**
  - suite 557/0; audio-attach 325/0/11 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained 5.3–14.3 ms.

## SYSTEMS MILESTONE 04 (2026-10-03) — MP_IAC_Streets world systems (AssetTools a23c675)
- **Ambient bed:** all 70 emitters play natively (auto-play once at level start, per-cue kKillFarthest
  registration, line/volume re-play). 50 of 70 sound, because 4 point cues have more emitters than their limit.
  The PROVISIONAL 24-voice budget, audibility gate and fades are removed.
- **96 channels:** priority stealing from authored Priority (255 − Priority). Sustained fire no longer drops
  weapon voices; only Priority-0 cues are refused when full.
- **Zones / pools:** 9 zones, 10 presets, 11 pools re-validated against the complete manifest and the
  decompiled SeqAct_PlayPlayerPositionalSound.
- **Steam FX:** LevelFx hands Steam_Mat and the authored colour to Rendering's material path.
- **Pickups:**
  - ammo beam active at map start (RE P2 correction);
  - `onTaken` / `onRespawned` adapters for Gameplay's PickupEvents; the integration glue is in FIDELITY.md.
- **Movers:** no authored mover sounds, none added.
- **Validation:**
  - suite 586/0; audio-attach 316/0/14 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained 4.8–14.2 ms; dense region 6.5–13.0 ms; no leaks.

## SYSTEMS MILESTONE 03 PASS 7 (2026-10-02) — vehicle loop enable confirmed (RE d50c2a9)
- **Loop enable:** `SoundNodeWaveEvent.bLooping` → FMOD_LOOP_NORMAL, with no loop points or count, so the whole
  FSB sample loops (CONFIRMED). The rebuild already behaved this way; only comments and provenance change.
- **Start / loop / stop:** the cues, crossfades and fades (0.1 in, 0.2 engine / 0.15 boost out, from the current
  position, 0 = immediate) are unchanged.
- **Validation:**
  - suite 533/0 (new loop-runtime block); audio-attach 238/0/13 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained fire 6.5–14.1 ms.

## SYSTEMS MILESTONE 03 PASS 6 (2026-10-02) — AssetTools 7a69756 authored-data handoff
- **Footsteps / landing:** the default `FS_DEFAULT_*` → `BL_FS_LRG_BOT` cues are the authored Streets sounds
  (one footstep table across all Streets physmats); no surface variants exist.
- **Concurrency:** 71 cues match authored values (no change). The `MECH_WPN_VOICE_THRESHOLD` channel-count duck
  on the SHOOT category stays UNKNOWN (native runtime).
- **Vehicle loops:**
  - FSB header regions = whole sample, applied from `VehicleLoops.inc`;
  - seamless wrap fix in Win32Audio;
  - loop enable: resolved in PASS 7 (wave event bLooping).
- **Pickups:**
  - `PickupPresentation`: 27 authored factories, script-confirmed effect activation (highlight off at spawn, on
    after the first respawn) and PickupSound attached to the recipient;
  - Gameplay drives it (no Systems timers);
  - the particle systems are not drawn: module flag semantics UNKNOWN.
- **Validation:**
  - suite 523/0; audio-attach 247/0/13 (0 player-owned);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - frame times (ms, range / mean): idle 6.4–10.6 / 7.0, movement 4.6–10.5 / 5.5, firing 6.7–13.1 / 10.3,
    sustained 6.5–13.1 / 9.7, hover 3.7–7.7 / 4.4, Boost 3.6–8.0 / 4.6, Nitro 4.3–7.8 / 5.1; particles/meshes → 0 after vehicle runs.

## SYSTEMS MILESTONE 03 PASS 5 (2026-10-02) — native audio runtime semantics (RE 7c4a2e0, supersedes 7b42621 provisionals)
- **Mixer** (`SoundMixer`):
  - native Enable / Disable / ref-count / priority insertion (equal priority: the earlier wins);
  - per-category first-defining-preset selection;
  - linear ramps in authored units (volume as linear amplitude);
  - FadeIn up / FadeOut down; restart from the current value;
  - Duration expiry (< 0 infinite, 0 on the next tick).
- **Zones:**
  - SeqAct_Reverb's explicit enable-new / disable-previous on a global reverb slot;
  - Touch edges with the last touch winning; no exit restoration (no Streets zone has UnTouched);
  - Default before the first touch and after a level load.
- **Channel modes, dB conversion, emitters:** k2D / k3D / SmartPan confirmed; native dBToLinear (−96 → 0, never above
  0 dB); line and box emitter placement promoted to CONFIRMED.
- **Vehicle loops:** behaviour confirmed. FSB loop regions delivered in PASS 6 (whole sample); previously the exact
  samples stay UNKNOWN pending AssetTools.
- **Validation:**
  - native suite 173/0;
  - audio-attach 240/0/10 (0 player-owned left behind);
  - wfc_fidelity 194/0/19; probe 31/0/1;
  - sustained fire 6.9–13.4 ms; cleanup clean.

## SYSTEMS MILESTONE 03 PASS 4 (2026-10-02) — native audio fidelity + Rendering FX handoff
- **Native spatialization** (RE report 76bb0a):
  - inverse rolloff with a hard cull at DistanceMax;
  - linear rear attenuation;
  - k3D as the class default;
  - SmartPan mix with SmartPanAttenuation3D;
  - **PreferPlayer pan reference** (pawn origin within 1400 UU, 0.5 s linear ramp). Pan only: the emitters
    stay at their true positions and volume uses listener distance.
- **Concurrency:** native rules, with class defaults max 5 / kKillFarthest.
- **Mixer presets:** enabled on cue play and disabled on stop (ref-counted); fade curve and overlap combine
  stay UNKNOWN.
- **Reverb:** REVERB_* priorities; zone switches verified (0.25 s, table values).
- **Vehicle FX** (Rendering 5e74895): original material names and unclamped HDR colour on the material path;
  4 material-only emitters spawned (hover base_glow / rays, ram dust / rays); per-loop bursts.
- **Validation:**
  - 0 player-owned sounds left behind;
  - sustained-fire windows 7.2–13.4 ms; leak suite clean;
  - wfc_fidelity 194/0/19; probe 31/0/1.

## SYSTEMS MILESTONE 03 PASS 3 (2026-10-02) — script-confirmed vehicle audio, landing rules, audio thread
- **Vehicle audio:** a port of the decompiled HmVehicleAudioComponent / HmPlayerVehicleAudioComponentImpl:
  - wheels loop only if grounded at boost start; jump-rev after 0.25 s airborne; reverse load from back input;
    0.1 s fade-ins; 15-sample speed;
  - hover dash = BoosterSound (RAM_BOOST_START); ram impact attached;
  - **ram alert removed** (no script plays it).
- **Mixer presets:** VEHICLE_JUMP (engine −18 dB) and VEHICLE_BOOST_END (−4 dB).
- **Landing:** fall height from where the descent begins and ForwardSpeed `|v · facing|`, per TnAcrobaticsManager.
  The landing cue level is as authored; the earlier report came from the placeholder.
- **Spatialization:** FmodAudioDevice PreferPlayer evidence (1400 UU, 0.5 s) recorded; the listener-based
  default is kept as PROVISIONAL with `WFC_SMARTPAN_PREFERPLAYER` A/B; `WFC_SPATIALLOG` instrumentation;
  `k2D` cues play 2D.
- **Zone pools:** reference = listener (script).
- **Ambient:** MaxConcurrentPlayCount 3 on flood lights and monorail.
- **First-play audio:** game-thread stalls (17 / 170 ms) were waveOutWrite blocking; mixing and submission
  now run on an audio thread.
- **Validation:**
  - perf: idle 6.1 ms; sustained-fire windows 7.7–13.6 ms; leak suite clean;
  - wfc_fidelity 194/0/19; runtime probe 31/0/1; audio-attach 0 player-owned left behind.
- **Handoffs:**
  - Gameplay: CarSimulation.SlipAngle → `World::setTireSlipAngle`; landing-clip state; ram collision → notifyRamHit.
  - Rendering: first-use program/texture prewarm (~520 ms first frame); per-shell light environments.

## SYSTEMS MILESTONE 03 PASS 2 (2026-10-02) — world sound bed, zone reverb, mixer, level FX
- **Attachment:**
  - Reusable owner/socket model; all 20 of Experimental's player-owned "left behind" sounds are fixed
    (their recorder, same scenarios).
  - The remaining 19 KNOWN are world impacts / zone pools, which must stay world-fixed.
- **Streets world audio from audio.json:**
  - 70 map emitters (point / volume / line, virtualized to the 24 most audible);
  - 9 Kismet reverb zones (MASTER_WET reverb + echo with preset fade);
  - zone one-shot pools;
  - Master compressor (−6 dB / 10 / 50 ms).
- **SoundCue runtime:** RearAttenuation, 5.1 pan fold-down, wet/dry category routing, data-loaded map cues,
  tire-squeal parameter, UI owner.
- **Level FX:** the 8 authored Steam_Sm_FX emitters.
- **Tire squeal:** authored cue, curves and gating ready; plays only once Gameplay supplies `World::setTireSlipAngle`.
- **Occlusion:** the original -6 dB / 0.5 s PhysicalMaterial occlusion, 0.25 s line checks; player-owned
  sounds are tested against the pawn body.
- **Validation aid:** debug overlay (B) draws every live sound source, coloured by owner, red when occluded.
- **Not done:** pickup / objective FX (37 components); AssetTools + Gameplay request in FIDELITY.md.
- **Validation:**
  - Perf: idle 6.5 ms; sustained fire 9.6–13.6 ms; mixer about 0.2 ms/frame.
  - Leak suite clean; wfc_fidelity 194/0/19; runtime probe 31/0/1.
- **Handoffs:**
  - **Gameplay:** slip-angle scalar; landing-clip state (Nav_Land_02/_03); ram collision → notifyRamHit.
  - **Rendering:**
    - per-shell light environments;
    - Steam_Mat soft alpha + UV distortion;
    - hover/boost ring materials.
  - **Experimental:** classify IMPT_* / PP_* voices as world in audio-attach.

## SYSTEMS MILESTONE 03 (2026-10-02) — branch `agents/systems` (synced to integration/milestone-02 e8036f6)
- **Firing cost 65–70 → 10–11 ms/frame.** The `CollisionWorld::segmentHit` grid walk (exact, brute-force
  verified) fixes the weapon trace, Gameplay's per-shot camera ray and the renderer's light-visibility rays.
  ~900 RPM cadence untouched.
- **Audio ownership:** cue instances attach to pawn / weapon / muzzle and follow them every tick (one-shots
  and delayed wave events included); world impacts stay world-fixed. Per-cue SmartPan distances from the
  cooked SoundNodeRoot.
- **Transform audio:** the original BL_TRANSFORM.OPTIMUS_BOT2VEH / VEH2BOT at their notify times, attached
  to the pawn. Generic gears wav removed. Vehicle FX enable at 1.8 s of the to-vehicle fold.
- **Robot movement sound:** the original BL_FS_LRG_BOT footsteps / scuffs / jump / land / hard land / high
  fall, idle and pivot foley, from the authored clip notifies and the LandingAnims height table. Replaces
  the RELOAD_AIR_RELEASE_THUMP placeholder landing.
- **Fine aim:** BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_START / END on the weapon.
- Diagnostics: `WFC_SYSPROF=1` (Systems CPU sections, live FX/cue counts), `WFC_FOLEYLOG=1`, and
  `WFC_CUELOG` now logs owner, position and stops.
- Validation:
  - `wfc_fidelity`: 194 pass / 0 FAIL / 19 known / 119 info / 1 skip (unchanged).
  - `runtime-probe.ps1`: 31 pass / 0 FAIL / 1 known (was 30/0/4; `transform_cue_is_authored` now passes).
- **Handoffs:**
  - **Rendering:** mesh-particle light environments (one per PSC, not per shell); hover/boost ring
    material treatment.
  - **Gameplay:**
    - play Nav_Land_02 / _03 per LandingAnims;
    - stop-transition idle↔walk flicker;
    - call `World::notifyRamHit` from ram collision.

## INTEGRATION MILESTONE 02 (2026-10-02) — branch `integration/milestone-02`
Integration and stabilisation only, no new features. Branched from milestone-01 (e62250e).
Each branch was merged with `--no-ff`, one at a time, then built and checked with the harness.

| Order | Branch | Head | Conflicts |
|---|---|---|---|
| 1 | agents/experimental | c9592f0 | none |
| 2 | agents/rendering | 13128bb | `Renderer.h` (Systems particle structs vs Rendering `CharacterColors`: kept both), `STATUS.md` |
| 3 | agents/gameplay | 21862a2 | `STATUS.md`, `FIDELITY.md` (sections kept from both sides; PROVISIONAL list combined) |
| 4 | agents/systems | b8fed05 | `Input.h`, `Win32Window.cpp`, `STATUS.md` |

**Cross-branch resolutions (ownership rules):**
- **Dash input:** Gameplay's mapping is authoritative. **Shift = Dash** (pad RB); **RMB** = Fine Aim
  in robot form and Boost in vehicle form. Systems' duplicate `Dash` enum, temporary **Q**
  binding and World-side Dash latch were dropped. That latch also latched every frame whenever
  `WFC_AUTODASH` was set, which clashed with Gameplay's frame-number hook.
- **Vehicle state:** Gameplay's `CharacterMovement` owns Driving, the hover dash, the nitro timers
  and cooldowns, and the speed/steering scales. Systems' `VehicleNitro::update` timer was replaced
  by `follow(vehicleState().nitroRemain > 0)`. RamFX, the nitro/alert cues and the ram-hit
  registry now track Gameplay's single state machine. Boost FX/audio follow Gameplay's Driving
  state instead of raw button state. Values are identical on both sides (3 s, ×1.5, ×0.3, 8 s).
- `Character::boneWorld` (Systems, vehicle FX sockets) now includes Gameplay's transform
  `meshOffset()`, matching `draw()`.
- Recoil, aim offset and upper-body layering: still exactly one implementation (Gameplay's), and
  recoil fires once per shot (`PlayerController` → `notifyFired`).
- **Ion Blaster cadence:** Systems' native-RE one-shot timer (strict `>`, overshoot discarded,
  ~900 RPM). The 923 RPM patch is not applied; Experimental withdrew it.

**Render data:** `tools\render\build_render_data.ps1` regenerated `work/render/MP_IAC_Streets`:
175/175 materials, 25 decals (`decals.glb`), `clut.png`, the post-process block, BSP, 268 lights
and 78 atlases. Output is byte-identical to agents/rendering's own output, apart from the
embedded worktree path. `slot_materials.json` is `{}` (0 default-material slots) on both.

**Validation (`.\build.ps1 -Jobs 2 -Clean`):** `wfc_rebuild.exe` and `wfc_fidelity.exe` build
with 0 compiler warnings.
- `wfc_fidelity` (latest Experimental harness): **194 pass / 0 FAIL / 19 known / 119 info / 1 skip**.
  `--map`: 196/0/19/135/0. `--no-assets`: 139/0/16/37/18.
- The same harness on milestone-01 (`ab.ps1`) gives 125/0/71/118/3. vs that baseline:
  **0 REGRESSED**, 49 FIXED, and the new checks (fine aim, form switch at t=0, map content,
  vehicle materials) pass. The Systems merge changed no check (identical to post-Gameplay).
- `runtime-probe.ps1`: 30 pass / 0 FAIL / 4 known.
- Runtime scripted runs (logs and stills in `work/fidelity/m2shots/`):
  - Robot jog 14 m/s forward/back/diagonal with directional clips; facing tracks the camera.
  - Jump about 5 m.
  - Fine aim: FOV 80→45; blocked during reload; resumes after.
  - Sustained fire: 200 shells, 3 magazine drops.
  - Reload while jogging: slot 1.0, legs keep `Nav_StrafeJog_F`.
  - Transformations both ways, stationary and moving:
    - robot→vehicle carries 14 → 15 m/s with heading continuous;
    - vehicle→robot carries 15 → 14.7 m/s until a wall;
    - the movement form switches at t=0.
  - Hover 1.85 m, camera-relative strafe 15 m/s.
  - RMB Boost: Driving on wheels, 30 m/s.
  - Shift hover dash: 30 m/s × 0.5 s.
  - Shift while boosting: nitro 3 s above 30 m/s, with RamFX and nitro cues once. Releasing Boost
    ends it immediately.
- Rendering: WFC path, CLUT grade, bloom, far DOF, robot/vehicle WFC materials, boost afterburners.

**Performance (RX 7900 XTX, WFC path):**
- Idle 6.1 ms; jog 4.7 ms; boosting 3.9 ms.
- **Sustained fire 62–68 ms** (Milestone 01: 40–46 ms).
- On the same renderer, firing costs +20.8 ms/frame on both agents/gameplay head and the merged
  build, so the increase is inherited, not a merge error. Gameplay now also traces the camera ray
  per shot. The Milestone 01 light-environment/mesh-particle interaction still applies. Not fixed
  (owners: Gameplay, Systems, Rendering).

**Known issues carried into the playtest:**
- Vehicle→robot: the weapon becomes usable and visible at the mesh handoff (~50% of the fold),
  not the confirmed 25% + 0.2 s equip. KNOWN `weapon_restore_frac_elapsed_to_robot` (Gameplay).
- Vehicle-specific camera incomplete; no vehicle shoulder offset (Gameplay, documented).
- Ram collision is not implemented in any branch; `World::notifyRamHit` exists but nothing calls it.
- Original transformation sounds are not present in any branch: the generic gears wav still
  plays (KNOWN `transform_cue_is_authored`).
- Harness KNOWNs remain:
  - jump apex 5.14 vs 5.0;
  - step-up 0.35 vs 0.37;
  - wall slide;
  - low-obstacle penetration;
  - vehicle jump;
  - dodge clips unreachable;
  - zero-step fire tap;
  - missing prefab/destructible actors;
  - level emitters.
- Two harness notes predate other branches' work. `unrendered.decals` says there's no decal pass,
  but the decals render. The probe's `boost_fx_emitted` reads weapon-FX counters only; vehicle FX
  log `VFX parts=26–28` while boosting. These are for Experimental to update.
- Robot/vehicle energon hue (AssetTools bake blue vs compiled red constant): KNOWN, Rendering.
- `LightMapTexture2D_882/_5049.png` decode warnings are pre-existing (legacy path only).

## INTEGRATION MILESTONE 01 (2026-10-01) — branch `integration/milestone-01`
Integration only: no new features. All four agent checkpoints were merged with `--no-ff`, one at a
time, building and running the fidelity harness after each merge.

| Order | Branch | Head | Conflicts |
|---|---|---|---|
| 1 | agents/experimental | 6dc6523 | none |
| 2 | agents/rendering | dd21838 | `.gitignore` (kept both rule sets) |
| 3 | agents/gameplay | 5dd1b09 | `STATUS.md`, `FIDELITY.md` (kept all sections from both sides) |
| 4 | agents/systems | ca9cd25 | `Character.cpp/.h`, `Recoil.h` (add/add), `SkinnedModel.cpp`, `Renderer.h`, `STATUS.md`, `FIDELITY.md` |

**Duplicate Gameplay/Systems work, resolved per tools/fidelity/CHECKPOINT.md:** both branches
implemented the reload upper-body slot, the TnAnimNodeAimOffset "Default" profile and
HmSkelControlRecoil, with identical recovered data. The merged build keeps **Gameplay's**
implementation, because locomotion, turn-in-place, transform and hover depend on it. Systems'
`AimOffset.h`, `updateUpperBody`/`evalLayered`, shot-serial recoil trigger, second `setAimPitch`
call and parallel `LocalPose` API were not merged. `WeaponMesh` was ported onto Gameplay's
`samplePose`/`blendPose`/`skinPose`, with the same semantics. Recoil fires once per shot
(`PlayerController` → `notifyFired`). Everything unique to Systems is kept: the animated Ion Blaster
and its sockets, AnimNotifies, original FX (muzzle flash, tracer, impact, shells, reload
flare/smoke, magazine drop), SoundCues, the audio voice API, and the glTF+.bin loader.
`Renderer.h` keeps Rendering's `loadMapRenderData`/`setVisibilityQuery` **and** Systems'
`drawParticles`. `GLRenderer::drawParticles` now restores the caller's `GL_FOG` state instead of
force-enabling it. Fixed-function fog stays off in the WFC shader path.

**Validation (clean build, `.\build.ps1 -Jobs 2 -Clean`):** `wfc_rebuild.exe` and `wfc_fidelity.exe`
build with 0 compiler warnings.
- `wfc_fidelity`: **110 pass / 0 FAIL / 6 known / 51 info / 1 skip** (exit 0). `--map`: 112/0/6/67/0.
  `--no-assets`: 79/0/6/25/10.
- vs the Experimental baseline (main code, 100/0/16): 0 REGRESSED, 10 KNOWN→PASS (Gameplay fixes).
- vs agents/gameplay head: identical. vs agents/systems head: 0 REGRESSED, 9 KNOWN→PASS.
- Runtime, with scripted `capture.ps1` runs (logs and PNGs in `work/fidelity/shots/int_*`):
  - World loads; WFC shader path active (168/169 materials, 1973 lightmapped components,
    268 lights, fog). Optimus, the vehicle and the Ion Blaster render through their WFC materials.
    The `WFC_LEGACYRENDER` fallback works.
  - Walk/strafe pick the directional clips. Facing follows the aim with the back to the chase cam
    (`face.toCam -0.91`).
  - Transformation works both ways (to vehicle → hover, to robot). The vehicle drives and boosts,
    and stops at walls.
  - Fire: SoundCue layers, impact cues, recoil, `IonBlaster_Fire` and muzzle/tracer/impact/shell
    FX all run. A full 50-round mag dump gives 50 shell notifies, then auto-reload.
  - Reload on the move: upper slot weight 1 over `Nav_StrafeWalk_F`, reload cues, flare/smoke
    @0.034, magazine drop @0.174 at MagSocket.
- Render data must be generated per worktree:
  `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1` (→ `work/render`).

**Known issues carried into the playtest (none introduced as harness FAILs):**
- **Frame time while firing:** about 6 ms standing → 40–46 ms during sustained fire (WFC path).
  - About 20–28 ms of this is inherited from Systems: the agents/systems head alone shows
    +20 ms while firing on the legacy renderer.
  - About 8–12 ms is a merge interaction: each moving shell/magazine mesh particle is drawn as a
    dynamic object. It misses Rendering's 4–8 slot light-environment cache, so it re-traces
    visibility rays for every candidate light each frame. Measured with visibility traces
    disabled: 34 ms.
  - Not fixed here, because the fix changes FX lighting behaviour. Owners: Rendering and Systems.
- `collision.max_step_height` is 0.70 m effective, against 0.35 m CONF. Gameplay's constant is 0.35,
  but it is still applied twice (Experimental patch #2, not applied). Jump apex, wall slide,
  low-obstacle and fire-interval KNOWNs remain; they are Experimental patches 1–4 for the owners.
- Reload/owner-anim slot blend: Gameplay `kSlotBlend` 0.15 s [PROV] vs Systems-recovered
  0.1/0.1 s [CONF]. For Gameplay to reconcile.
- Systems' fixed-function particles composite into Rendering's linear HDR target without
  sRGB→linear conversion. They may read slightly brighter/flatter than the original. Visual check
  in playtest.
- `SHOOT_TAIL` can fire mid-burst when a frame hitch exceeds 2× FireInterval (Systems
  edge-detect on wall-clock time).
- `LightMapTexture2D_882/_5049.png` in ExtractedAssets fail to decode for the legacy lightmap path.
  This predates the merge; the WFC path uses its own regenerated atlases.

## RENDERING PASS 9 (2026-10-01) — CHARACTER CUSTOMIZATION PATH (TnCharacterApplier)
- Reconciled native RE evidence + AssetTools `materials_authored.json`: the applier pushes
  `Cust_Color_A` / `Cust_COLOR_B` / `EnergonColor` onto robot, vehicle, separate arm and weapon;
  an all-zero colour skips the override (authored value stands); faction selects the colour set; no
  separate team override in this path. Optimus character data is all-zero, so the authored MIC
  values stand — identical to the previous render (verified). The faction energon constants equal
  the master's three `EnergonColor` switch constants; the Autobot branch compiles to (1.25,0.05,0.05).
- Character/weapon materials now compile those parameters as runtime uniforms; when skipped, each
  same-named expression keeps its own authored value (UE3 duplicate-parameter semantics). World MICs
  that also declare `Cust_Color_A` stay constant (the applier only targets character meshes).
- New `IRenderer::setCharacterColors(CharacterColors)` (default no-op → authored); applied only to
  dynamic (character/weapon) draws. Gameplay ownership unchanged: nothing calls it yet.
- Raw umodel meshes (`CP_OptimusArm_SKEL`, the MP robot `RB_OptimusWeaponArm_SKEL`) resolve their
  original materials by section material name — verified rendering with the original MICs.
- Shared `world.glb` was regenerated by AssetTools with all section materials resolved; the PASS 8
  slot remap is now a no-op for current data (kept for older extractions).
- Verification hooks: `WFC_CHARCOLORS="pr,pg,pb;sr,sg,sb;er,eg,eb"`, `WFC_TESTMESH="file.glb|x,y,z|yaw"`.

## RENDERING PASS 8 (2026-10-01) — VEHICLE MATERIAL FIX + STREETS COMPOSITION AUDIT
- **Optimus vehicle regression fixed.** Root cause: the vehicle MIC enables `UseReconstructedNormal`
  (normal X in the alpha of DXT5 `VH_Optimus_NORM`, Y in green); its cooked `UnpackMin` covers R,G,B
  only, so X stayed in [0,1] and every vehicle normal was tilted (lighting, specular and the
  reflection that feeds emissive all wrong). The original compiled character PS unpacks both
  channels `(A,G)*2-1`; the translator now does the same. Robot unaffected (DXT1 RGB normal map).
  Diffuse/customization path verified identical to the AssetTools bake (`WFC_ALBEDO` A/B).
- **Authored post-process applied** (persistent level = `MP_IAC_Streets_BASE_m`, TransLevels.ini):
  Streets CLUT `ENV_MPCLUT_p.MP_Streets_CLUT` (32³ volume, decoded), Bloom_Scale 0.1, far DOF
  (max 0.6, falloff 40000 UU). PASS 7 wrongly assumed engine defaults (the BL_LVL stub misled it).
- **25 static decals recovered** from cooked receiver geometry (landmark chevrons/corners, additive
  unlit, 2105 tris) → `decals.glb`.
- **15 mesh sections** left on the default grey material by the extractor now use their original
  materials (`slot_materials.json`); 6 are null in the original data.
- Mirrored-instance winding restored (1 prop).
- Streets composition verified complete: BASE streams only ART + AUDIO (both composed); all 34
  prefab-instance actors and all 1952 props present; prop placement cross-checked against decal
  receiver geometry.
- New diagnostics: `WFC_ALBEDO`, `WFC_GLTFMATERIALS`, `WFC_SKIPMAT`, `WFC_NODOF`, `WFC_NOCLUT`,
  `WFC_NODECALS`.

## RENDERING PASS 7 (2026-10-01) — ORIGINAL WFC RENDER PATH (shaders, materials, lighting, post)
Branch `agents/rendering`. The runtime now renders Streets and Optimus through a GL 3.3 shader path
whose every stage was recovered from the original game data/binaries (details + provenance:
FIDELITY.md "PASS 7"). The legacy fixed-function path remains as an automatic fallback.
- **Materials from the original graphs:** `tools/render/matc.py` translates the cooked UE3 material
  expression graphs (master + WFC MaterialFunctions + MIC params, **static switches**, **TextureSets**,
  WFC HLSL `ShaderCode` snippets) into per-instance GLSL — 168/169 materials (world, BSP, Optimus
  robot/vehicle, Ion Blaster). Fixes the old extraction's biggest error: world materials were drawn
  with their *decal* texture (`Rust_C_CLR`) as diffuse; the real diffuse/normal/masks live in TextureSets.
- **Directional lightmaps, all 3 coefficients**, decoded from the original Xenon base-pass shader
  microcode (`tools/render/xenos_dis.py`): `L = Σ dot(N_t,B_i)² · LM_i · Scale_i`, sRGB-decoded atlases.
- **BSP lightmaps:** BSP rebuilt from the cooked `FModelVertexBuffer` (ShadowTexCoord) split per
  ModelComponent element with its own lightmap (180 lit elements, 2460 tris) → `bsp.glb`.
- **Normal maps / specular / gloss / emissive / reflections:** all as authored by the graphs; cubemaps
  and flipbooks decoded natively from Xbox-tiled data (`tools/render/xbox_texture.py`).
- **Dynamic-character lighting:** WFC UberLight decoded from microcode (ambient cube + wrapped²
  diffuse + Phong spec), light environment built from the 268 authored lights (TotalLightCount 2,
  0.3 m update threshold from TnRobotForm/TnVehicleForm), dynamic-only SkyLight, raycast visibility.
- **Fog / post:** UE3 height fog (authored component, LightBrightness 0.1 default), HDR target,
  decoded bloom gather + UberPostProcess (WorldInfo defaults), DisplayGamma 2.2.
- **Performance:** ~200 fps (RX 7900 XTX) standing and walking; VBOs, per-submesh frustum culling.
- **Render data:** generated into `work/render/<Map>` (untracked) by
  `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1` — run once per worktree
  before launching. Missing data ⇒ legacy renderer.
- **New env vars:** `WFC_LEGACYRENDER`, `WFC_RENDER_DATA=dir`, `WFC_LIGHTINGONLY`, `WFC_NOBLOOM`,
  `WFC_NOFOG`, `WFC_RENDERCAM=x,y,z,yaw,pitch`, `WFC_RENDERSTATS`, `WFC_DUMPSHADER` (`WFC_NOLIGHTMAP` kept).
- **Still open:** LightsVisibilitiesVolume (precomputed light visibility, format partly decoded) not
  used; 24 vertex-lightmapped props unlit by lightmap; dynamic shadows (ShadowMask) = 1;
  DirectLightAmbientContribution = 0; 1 material with an absent master.

## FIDELITY PASS 12 (2026-10-01): RECONCILED WITH NATIVE RE; THREE VEHICLE MECHANICS; INPUT LATCHES
Inputs: confirmed native RE notes (transformation, locomotion, fine aim, input, weapon restore) and the
Systems checkpoint "VEHICLE MECHANICS" (agents/systems 3700965), plus new bytecode (TnCarForm Hovering/
Driving, TnHoverCarSimulation Update/Strafe/Turn/Dash/Drift).
- **Vehicle->robot now enters FALLING with full velocity** [RE]. Pass 11 snapped it to the ground and
  absorbed the drop with a mesh offset; that is removed. Verified: 15 m/s kept, a 1.85 m drop over
  ~0.2 s, then jogging at 14.
- **Robot->vehicle:** velocity is written to the vehicle unchanged (≤35 m/s, no reprojection); the
  vehicle starts on the robot yaw; hover steering authority fades in over 0.5 s
  (DriftScale = (1 − t/0.5)², from Hovering.BeginState → Drift). Verified 14 → 15 m/s as authority returns.
- **Hover mode corrected:** the hover truck faces the VIEW yaw and STRAFES in that frame
  (Hovering.DoUpdate passes the view yaw; UpdateTurn matches it; UpdateStrafe: 15 m/s, 30 m/s²
  clamped radially). Pass 7's "face the travel direction at π rad/s" was wrong for hovering.
- **Three vehicle mechanics, not conflated:**
  - Normal boost: hold RMB / pad LT → Driving (wheels; Truck_Physics 30 m/s, 25 m/s²); blocked during
    the drift ramp; release → Hovering + drift. Animation: Nav_HoverToBoost_VEH → Nav_Idle_Wheels_VEH,
    then Nav_BoostToHover_VEH on exit.
  - Hover dash: Dash while hovering → 30 m/s along the dominant input axis for 0.5 s (100000 UU/s²);
    cooldown 2 s.
  - Nitro: Dash while driving → speed ×1.5 (45 m/s) and steering ×0.3 for 3 s; cooldown 8 s; ends on
    leaving Driving.
- **Dash binding recovered:** Dash = VehicleSpecialMove = **Shift** (pad RightShoulder);
  PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput. (Systems used a provisional Q.) Shift
  is no longer a boost alias.
- **Input latches [RE]:** the fire held flag persists; reload fires on release of a tap < 0.3 s; jump
  and dash edges stay latched until a simulation step consumes them; transform fires on press.
- **Weapon restore [RE]:** vehicle→robot restores the weapon at 25% elapsed (0.75 remaining) and it is
  usable after the 0.2 s equip. Verified usable at t = 0.48 s of the 1.13 s fold.
- **Fine aim:** transforming to the vehicle ends it (the wish is cleared); during vehicle→robot the
  control form is the robot.
- **Locomotion:** no play-rate compensation; clips stay at 1.0× (the original has the same stride
  mismatch) [RE].
- Regression: jump 5.12 m, turn in place, recoil, reload on the move, fine aim 7 m/s / FOV 45, step
  routes unchanged; clean build.
- **Known:** the weapon becomes usable at 25%+0.2 s, but the robot mesh (and so the visible gun)
  appears at the 50% mesh handoff [PROV]. Driving steering/throttle is PROV (wheel physics not
  recovered). The vehicle camera strategy (Truck_Optimus_CAMSET) is not recovered (the robot camera
  is still used). Ram collision is not implemented. Nitro state is duplicated with Systems'
  VehicleNitro: unify at integration.

## FIDELITY PASS 11 (2026-10-01): TRANSFORM MOMENTUM, ROBOT RUN SPEED, FINE AIM (player-control pass)
Driven by the integrated human playtest. Evidence: UnrealScript bytecode decoded from TransGame.xxx
(`work/pass11/ue3dis.py`), shipped input bindings, Optimus/truck/camera content objects.
- **Transform hard-stop: root cause found and fixed.** The rebuild did two non-original things:
  it zeroed velocity in `beginTransform`, and it locked all input during the fold. In WFC nothing on
  the transform path touches velocity, and no movement code checks IsTransforming. The target form
  becomes the movement form at the START of the fold (`BeginTransformation`). Robot→vehicle hands
  the velocity (≤35 m/s, `kMaxTransformSpeed`) and the rotation to the vehicle's rigid body
  (`TnVehicleForm.OnActivate`). Vehicle→robot keeps the velocity, and the robot's `CalcVelocity`
  preserves overspeed with the TruckTransformerMomentum InAir set while transforming.
  Verified on the real executable: running 14→15 m/s into the vehicle; cruising 15→14 m/s into the
  robot; boosting 30 m/s → robot 28.7 m/s after the fold, then a momentum run down to 14; strafe and
  diagonal folds continuous; standstill both ways clean.
- **"Missing fast movement" = the robot's real run speed.** `TnPawn.ApplyTransformer` applies the
  character definition (Optimus_ROBODEF): GroundSpeed 14 m/s, AccelRate 120 m/s², AirSpeed 12,
  AirControl 0.4, jump 5 m, collision r2.0 m. Passes 2/10 had used class defaults (5.5 m/s). WFC
  has no robot sprint key: full input runs at 14 m/s on the jog clips; partial pad input walks at
  ≥4.5 m/s; vehicle→robot carries vehicle/boost speed.
- **Fine aim (robot only):** RMB toggles, pad LT holds. Speed ×0.5 (7 m/s), FOV 80→45 in 0.1 s
  (exit 0.4 s), look speed ×0.5, spread ×0.5. Drops during reload and resumes. In the vehicle,
  RMB/LT = boost.
- **Camera from the robot strategy:** FOV 80, orbit 8 m, anchor 4 m, pitch ±75°, over-the-shoulder
  offset curve, basic camera collision, and traces aimed through the crosshair.
- **Truck:** boost 30 m/s / 0.5 s, hover 1.85 m.
- **Fixed pre-existing bug:** the ground/step query counted step-up and hover twice (robots stepped
  0.7 m; vehicles could snap onto decks 4.4 m above).
- Regression: jump 5.12 m, turn in place, recoil +12°, reload layer, the step A/B identical on 12
  routes; clean build.
- **Known issues:** foot slip is higher at the new speeds (≈1.1–1.8 m/s at 14 m/s, ≈1.6–2.0 m/s at
  7 m/s; no playback-rate scaling evidence); the vehicle hover/steering model is PROV; fine-aim
  target snap and fine-aim sounds are not implemented.

## FIDELITY PASS 10 (2026-10-01): LOCOMOTION BLEND FROM THE SHIPPED TREE; SPEED + STEP HEIGHT CONFIRMED
- **Moving state rebuilt to match Robot_ANIMTREE:** TnVelocityAnimBlend (450→1200 UU/s) mixes a
  walk and a jog TnStraferAnimBlend. Each strafer weights F/B/R/L by the travel direction relative
  to the facing, eased over `_BlendSpeed` 0.2. All 8 sequences are phase-locked like the "Strafers"
  AnimNodeSynch group (shared phase, highest-weight clip sets the rate). Idle↔Moving crossfade
  0.2 s (AmpCrossFadeCondition). Replaces "pick one F/B/L/R clip + 0.15 s crossfade".
- **Clip ground speeds measured:** walk ≈3.5 m/s, jog ≈12.1 m/s. The tree's 1200 UU/s MaxSpeed
  equals the jog's authored speed, so the blend is speed-matched by design.
- **Robot ground speed 5.5 m/s confirmed from script:** `TnPawn.PostBeginPlay` runs
  `_BaseGroundSpeed = GroundSpeed`, and `UpdateSpeeds` sets `GroundSpeed = _BaseGroundSpeed ×
  Π SpeedMultiplierFactors` (bytecode decoded). TnPlayerPawn GroundSpeed 550 × Ion Blaster
  GroundSpeedMultiplier 1.0. So the player's Moving state is ≈87% walk / 13% jog, as authored.
- **MaxStepHeight 35 UU (0.35 m) applied** (`TnRobotForm._MovementCapabilities`; was PROV 0.6).
  A deterministic A/B (`WFC_NOMOUSE`) on 12 routes shows no new snagging.
- Diagnostics: `WFC_NOMOUSE` (ignore live mouse in tests), `WFC_STEPUP=m` (A/B override).
- Regression: reload on the move, transforms both ways, turn in place, recoil +12°, jump 6.36 m,
  vehicle hover; clean build.

## FIDELITY PASS 9 (2026-10-01): AUTHORED AIM OFFSET PROFILE (TnAnimNodeAimOffset "Default")
- **The aim offset is now the shipped profile, not a pose-derived approximation.** The Ion Blaster
  uses the `Default` profile (selected by `WeaponTypeObserved`). It drives 11 bones (spine chain,
  head, both arms) with 9 cells each, baked from `Shooting_Aim_{L,F,R}_{D,C,U}`.
- **Bake rule cracked and verified:** for each bone/cell, rotation = Gp·(L_cell·L_centre⁻¹)·Gp⁻¹
  and position = Gp·(t_cell − t_centre), where Gp is the parent's model-space rotation in that
  cell. UE → glTF conversion: rotation `(−x,−z,−y,w)`, position `(x,z,y)·0.01`. Against the
  authored AimComponents: mean 0.01°, worst 0.07°, translations 0 mm (`work/pass8/verify_aim.js`).
  So the runtime bakes the profile from the clips (no asset data committed) and applies it the
  UE3 way: bilinear cells, each bone rotated/moved in model space, parent first.
- **Input ranges recovered:** profile H [−1,1] / V [−1,0.8]; RemapPawnAimRange from pawn aim
  (fraction of 90°) H [−1,0.85] / V [−0.7,1]. The remap is centre-preserving (PROV reading).
  Measured barrel pitch at aim −69/−34/0/+34/+69° → −42/−21/+3/+27/+50°: the gun trails the
  camera at the extremes, as the authored ranges imply.
- Turn-in-place yaw columns: the barrel stays within 5–13° of the aim while the legs lag up to 67°.
  During a pivot step the gun arm swings ≈50° and back. That swing is authored in
  `Nav_IdlePivot90_*` (root-discarded chest ±7°, forearm −50°), and the shipped tree layers the
  aim offset over it unchanged, so it is kept.
- Regression: recoil +12°, reload on the move, transforms both ways, jump 6.41 m; clean build.

## FIDELITY PASS 8 (2026-10-01): WEAPON RECOIL, TURN IN PLACE, FULL AIM GRID (shipped anim tree)
Gameplay agent. Evidence: shipped `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` and class defaults, read
read-only from cooked packages (`work/pass8/dump_animtree.py` → `work/pass8/dump_*.json`).
- **Weapon recoil (was NOT YET):** a reconstruction of `HmSkelControlRecoil` (UE3
  `GameSkelCtrl_Recoil`), restarted per shot (`TnRecoiler`). Bones come from the tree's
  SkelControlLists: SpineRecoil → `C_Spine02_Lumbar02_XB`, RightHandRecoil → `R_Arm02_Shoulder_XB`.
  Ion Blaster values = `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides. A/B
  (`WFC_NORECOIL`): during sustained fire the barrel climbs +12° (6.8° → 19.1°) with ±3° random yaw.
- **Turn in place (was NOT YET):** `TnAnimTurnInPlace` / `TnAnimTurnInPlaceRotator` ("UnwindLowerBody").
  Standing, the legs keep their world yaw while the torso follows the aim through the aim
  offset's L/R columns. At 22.5° short of a transition's 90°/180° (`TransitionThresholdAngle` 4096),
  `Nav_IdlePivot90_{L,R}` plays with root rotation discarded (`RRO_Discard`) and unwinds the
  offset along the clip's own root-yaw curve. Blend 0.1 s, abort after 50%. Verified: a slow pan
  holds the legs to 67°, then a 90° step returns the offset to ≈0; a fast pan (143°/s) chains pivots.
- **Aim offset is now the full 3×3 grid:** the yaw columns are calibrated from the poses
  (barrel L −90.7° / R +81.2°), and the inputs interpolate at the authored `InterpSpeed` 12.
- **Fixed (pre-existing):** robot→vehicle picked `Transform_ToVehicle_SuperBoost_Veh` (0.8 s) as
  the incoming clip. It now pairs `Transform_ToVehicle_VEH` by name (matched 1.97 s fold).
- Diagnostics: `WFC_AUTOTURN=rad/s`, `WFC_NORECOIL`, `WFC_LOGEVERY=N`; the frame log adds
  legYaw/aimYawN/turn/recoil.
- Regression: jump 6.39 m, transforms both ways, reload on the move, vehicle hover; clean build.

## FIDELITY PASS 7 (2026-10-01): ANIMATION LAYERS, MESH FACING CORRECTED, VEHICLE HOVER POSES
Gameplay agent (`agents/gameplay`). Verified by runtime screenshots + numeric logs (`work/pass7/`).
- **Mesh facing was 90° off; now fixed (+90°).** The authored straight-ahead aim pose
  `Shooting_Aim_F_C` points the Ion Blaster barrel along model **+X** (logged at load:
  `barrel dir (model space) 1.00 -0.06 -0.02`), UE's forward axis. Pass 6's `kMeshYawOffset=0`
  left the whole robot (and truck) side-on to the chase camera, with the gun 90° right of the
  reticle. Pass 6's `face·toCam` check only tested the yaw math, never the mesh. Restored
  `kMeshYawOffset=+π/2`. Now: the chase cam sees the robot's back and the truck's rear; the muzzle
  points along the aim yaw (`MUZZLE` log); `WFC_FACELOG` measures the real mesh +X.
- **Bone-space pose layering** (`assets::LocalPose`: sample / blend / mesh-space per-bone blend /
  additive / skin). Locomotion crossfades are now bone-space, not vertex lerps.
- **Upper-body aim offset (was NOT YET):** the authored `Shooting_Aim_F_{D,C,U}` poses, applied as
  a delta from F_C on the `C_Spine01_Lumbar01_XB` subtree, driven by camera pitch. The grid is
  calibrated from the poses' own barrel pitch (D −47.6°, C −3.3°, U +72.2°). Verified in profile
  at pitch −0.6/0/+0.6: the barrel measures ≈−27°/+7°/+42°.
- **Reload on the move (was PARTIAL):** the authored `Shooting_Reload_IonBlaster_ROBO` now plays as
  an upper-body slot over locomotion, blended in mesh space like UE3 `AnimNodeBlendPerBone`, so
  the torso stays forward over the strafe clips' turned hips. Full body when standing still.
- **Vehicle animation fixed:** moving used to loop `Nav_BoostToHover_VEH`, a one-shot
  transition. It now blends the authored directional poses `Nav_Hover_{Pose,F,B,L,R}_VEH` by local
  velocity, plus the additive `ADD_Nav_Hover_VEH` hover bob.
- **Vehicle turn rate applied:** the truck steers toward its travel direction at the recovered
  π rad/s (`AiMaxAngularSpeed`) instead of snapping.
- **Robot idle:** `NAV_Idle` (Optimus's own gameplay idle) replaces `Cust_Idle`, the
  customization-screen idle that was being picked as first-of-category. Take-off plays once;
  `Nav_Land` plays on touchdown after ≥0.3 s airborne.
- Regression: jump 6.36 m, transforms both directions, vehicle hover 2 m, no idle drift; clean build.
- New diagnostic: `WFC_FIXPITCH=rad` pins camera/aim pitch. The frame log adds yaw/aimW/aimN/reloadW.
- **Still open (gameplay):** ~~turn-in-place~~, ~~weapon recoil~~ (done in Pass 8), boost/dodge
  clips, camera tuning.

> **Integration note (integration/milestone-01):** Systems Passes 1–2 below describe the Systems
> branch's own upper-body slot, aim offset and recoil code. Gameplay Passes 7–9 implemented the same
> features independently, so the merged build keeps **Gameplay's** implementation
> (`Character.cpp` RobotRig, `Recoil.h`, recoil triggered once per shot from `PlayerController`).
> Systems' `AimOffset.h`, `updateUpperBody`/`evalLayered` and its second recoil trigger were not
> merged. Everything else in the Systems passes (animated weapon mesh, notifies, FX, SoundCues) is
> in the merged build. See INTEGRATION MILESTONE 01.

> **Integration note (integration/milestone-02):** the Systems sections below mention a temporary
> `Q` Dash binding and a Systems-run nitro timer. In the merged build, **Shift** is the only Dash
> binding (Gameplay, CONF). Gameplay's movement code owns the nitro/hover-dash timers, cooldowns and
> scales. `VehicleNitro` follows Gameplay's `vehicleState().nitroRemain` for RamFX, the nitro cues
> and the ram-hit registry. Boost presentation follows Gameplay's Driving state. See INTEGRATION
> MILESTONE 02.

## SYSTEMS NATIVE-RE UPDATE (2026-10-01)
- Ion Blaster cadence = original ~900 RPM: one-shot refire timer reset to 0 per shot, fires when elapsed
  > 0.065 s, overshoot discarded, max one shot per tick (standalone check: 900 shots/min). Experimental's
  923-RPM patch is NOT applied.
- Nitro: cooldown starts on activation [CONF]; ends on Boost release [CONF]; ram hits gated to one per target
  per nitro (`VehicleNitro::registerRamHit`, `World::notifyRamHit`); authored ram damage 175/300/175/300 and
  ExtraRamZVelocity 7000 UU/s exposed for Gameplay.
- Confirmed vehicle states (Boost LT/RMB, Hover Dash RB 3000 UU/s 0.5 s 2 s cooldown, Nitro RB while
  boosting) recorded in FIDELITY.md "Native RE confirmation".
- Gameplay handoff: `PlayerController` clears `wantFire_` after every fixed step but sets it once per render
  frame, so a frame that runs 2 steps drops the second step's shot (measured ~4.4 ticks/shot in-game vs 4.0).
  A held trigger should stay set for every step.

## SYSTEMS CHECKPOINT (2026-10-01) — end of round
- Branch `agents/systems`, clean build. Implemented this round: weapon layering / recoil / aim offset,
  animated Ion Blaster + notifies, weapon FX (muzzle, tracer, impact, shell, magazine, reload), weapon
  SoundCues, vehicle boost / hover / jump / ram FX, boost + nitro + engine / jump / land audio.
- Vehicle mechanics provenance (normal boost vs hover dash vs ram/nitro): FIDELITY.md "VEHICLE MECHANICS".
- Deliberately NOT done (Gameplay-owned): tire squeal (needs a lateral-slip signal), nitro camera change,
  nitro speed/steering scaling, hover dash, final Dash / RMB bindings (RMB: robot Fine Aim, vehicle Boost).

## SYSTEMS PASS 7 (2026-10-01) — VEHICLE ENGINE AUDIO
- Optimus's authored engine audio in vehicle form: off-load (idle/coasting) and on-load (throttle) drive
  loops, the jump-rev loop while airborne and the jump-start one-shot, and hover/wheels light/heavy landing
  cues by time in air; 0.2 s engine fades; all layers follow the mph speed parameter. The drive loop yields
  to the boost loop while boosting. Tire squeal not done (needs a slip signal from Gameplay).
- Read-only `PlayerController::throttleHeld()` added for the on/off-load choice.

## SYSTEMS PASS 6 (2026-10-01) — TRUCK NITRO / RAM (state, FX, audio)
- New abstract input action `Dash` (**PROV** temporary key **Q**). DASH while boosting on wheels starts
  the authored nitro: **3 s**, cooldown **8 s**; `RamFX` (rim-lit flame wedge on RamSocket) runs for its
  duration; `VEH_OPTIMUS_RAM_NITRO_START` + `VEH_TRUCK_RAM_ALERT` play at start. Releasing boost stops it.
- For Gameplay (read-only): `World::vehicleNitro()` -> `nitroActive()`, `ramActive()`, `timeRemaining()`,
  `cooldownRemaining()`, `speedScale()` (1.5 while active), `steeringScale()` (0.3 while active);
  `World::notifyRamImpact(pos)` plays the ram impact cue. Systems does **not** change speed or steering.
- Documented, not changed: Optimus's truck physics blueprints differ from the rebuild's dash values
  (HoverTruck_Physics DashSpeed 3000 / DashDuration 0.5 vs current 5000 / 0.3) — see FIDELITY PASS 10.
- Test: `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 WFC_AUTODASH=1 WFC_BOOSTLOG=1` (NITRO lines).

## SYSTEMS PASS 5 (2026-10-01) — HOVER THRUSTERS + JUMP BOOSTERS
- Vehicle form now shows Optimus's authored hover thrusters (`CarHover_A_01_FX` on the six wheel
  HoverBooster sockets, with their socket scale): red light cones and orange rings looping, plus a
  spark / electro-ring / pulse burst whenever hover engages. Hover switches off while boosting (the truck
  drops to its wheels) and when transforming.
- Vehicle jumps fire `Jump_FX` on JumpBoostSocket_C/R/L: a 0.5 s burst of downward thruster cones,
  energon cones, glows, booster smoke, sparks and electro rings.
- `VehicleFx` replaces `VehicleBoostFx` as one data-driven system for boost / hover / jump.
- Verified: idle hover, boost (hover off, boost unchanged), jump take-off, transform out; robot weapon FX unchanged.
- PROV: light-cylinder intensity (DustPower 0.1 stands in for the volumetric shader), procedural spark
  texture, ring velocity reading. Open: drive/jump/land engine audio (RamFX: SYSTEMS PASS 6).

## SYSTEMS PASS 4 (2026-10-01) — VEHICLE BOOST PRESENTATION
- Holding boost in vehicle form now shows Optimus's authored afterburner (`bumble_boost_small1_FX` on
  BoostSocket_L/R, the two exhaust stacks): ignition burst of thruster cones + bullet cone + glow, then
  looping cones (3-4/s) and glows (20/s) attached in local space; release kills them (bKillOnDeactivate).
- Original boost audio: VEH_OPTIMUS_BOOST_START, the speed-driven VEH_OPTIMUS_BOOST_LOOP (5 looping
  layers, mph parameter), VEH_OPTIMUS_BOOST_END with the 0.15 s fade, and the 0.27 s grounded wheels peel-out.
- Verified: accelerating, stationary (against a wall), airborne after a vehicle jump, and transforming
  out while holding boost (effect + loop stop, END plays). Robot fire/reload FX unchanged.
- Diagnostics: `WFC_BOOSTLOG=1` (state, particle count, mph, socket positions). Test:
  `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 [WFC_AUTOWALK=1]`.
- Open: ram FX, drive/jump/land engine audio (hover + jump FX: SYSTEMS PASS 5).

## SYSTEMS PASS 3 (2026-10-01) — SHELL, MAGAZINE AND RELOAD FX
- Every shot ejects the authored shell mesh (GrenadeAmmo_STAT) from ShellSocket with a vent smoke puff;
  the reload vents a blue flare + 0.75 s smoke stream at the muzzle (@0.034 s) and drops the Ion Blaster
  magazine mesh from MagSocket (@0.174 s, 3 s life) with a smoke puff. Values from the cooked
  ParticleSystems (FIDELITY PASS 7c); gravity/ground contact for the meshes is PROV (none authored).
- `WFC_ANIMLOG` now also prints live particle / mesh / impact counts.

## SYSTEMS PASS 2 (2026-10-01) — UPPER-BODY AIM OFFSET
- Robot_ANIMTREE `TnAnimNodeAimOffset` (profile Default, 11 bones x 9 authored rotations) now aims the
  spine, head and arms at the camera pitch, between locomotion and the reload slot (original tree order).
  Space/axes verified against the Shooting_Aim_* clips. Measured: aim 22.9 deg -> barrel 21.6-23.8 deg while
  jogging and firing; +0.9 / 0 / -0.9 rad screenshots show the gun raised / level / lowered.
- **Found, for Gameplay:** the robot mesh faces +X in model space but `kMeshYawOffset = 0` draws it as if it
  faced -Z, so Optimus renders 90 deg off the aim (barrel heading = aim - ~100 deg). Fix: `kMeshYawOffset = +pi/2`
  (evidence in FIDELITY PASS 7b). Not changed here (Gameplay-owned).
- Diagnostics: `WFC_AIMPITCH=<rad>` forces the aim pitch (camera untouched); `WFC_ANIMLOG` prints the aim
  values plus barrel pitch/yaw vs aim.

## SYSTEMS PASS 1 (2026-10-01) — WEAPON LAYERING, RECOIL, ANIMATED WEAPON, ORIGINAL FX + SOUNDCUES
Systems-agent branch `agents/systems`. All values recovered from cooked data (details and
confidence in FIDELITY.md PASS 7; decoders in `tools/systems/`).
- **Reload on the move**: reload plays in the original `UpperBodyCustom` slot masked from
  `C_Spine01_Lumbar01_XB` (AnimNodeBlendMultiBone), blend 0.1/0.1 s; the legs keep the strafe/jog
  clip (no more glide). Verified: base `Nav_StrafeJog_F` at 5.5 m/s + upper `Shooting_Reload_IonBlaster_ROBO`.
- **Recoil**: HmSkelControlRecoil port on SpineRecoil / RightHandRecoil with the Ion Blaster's
  authored RecoilDefs (restart per shot, decaying sinusoid in aim space).
- **Animated weapon**: the Ion Blaster is its 34-joint skeletal mesh playing its own
  Fire / Reload_AP / Idle anims with authored sockets; the muzzle is the MuzzleFlash socket.
- **Event timing**: weapon AnimNotifies drive reload/idle sounds at the authored times.
- **FX**: muzzle flash, tracer bolt + smoke trail, impact squib rebuilt from the cooked
  ParticleSystems (original textures, blend modes, bursts, lifetimes, sizes, velocities,
  colour/alpha/size curves, squib rules). Replaces the yellow line + box placeholder.
- **Audio**: original SoundCues: layered fire (near/mid/distant by distance), low-ammo, tail,
  impact, reload, idle; dB/semitone variation, timed events, concurrency, FMOD inverse rolloff.
- Diagnostics: `WFC_ANIMLOG` (base/upper/recoil/weapon clip), `WFC_NOTIFYLOG`, `WFC_CUELOG`.
- **Not done / handed off:** camera recoil + shake (camera owned by Gameplay), dry-fire trigger,
  mixer/reverb. The idle base clip
  `Cust_Idle` (showcase idle) should be `NAV_Idle`; that is Gameplay's locomotion selection.

## FIDELITY PASS 6 (2026-10-01) — INTERACTIVE PLAYER FIXES (orientation, locomotion, muzzle, reload, transform)
Runtime observation (replaying the exe) drove this pass, not headless smoke. Fixed, in the
player's priority order:
- **#1 Orientation/facing** — the robot now faces the **camera/aim (mouse) direction every
  frame** (`CharacterMovement`: robot `setYaw(faceYaw)` always; vehicle faces its travel dir).
  The old "only set yaw while moving" caused the body to **snap right** when you rotated the
  camera while standing then walked. Confirmed numerically: `face·toCam ≈ −0.9` (back to the
  chase cam). Mesh yaw offset is **0** (the earlier "90° bug" was a `WFC_FIXYAW` diagnostic
  artifact — the diagnostic set camYaw *after* faceYaw was latched; fixed its ordering too).
  WASD = move direction, mouse/camera = facing — the WFC strafe-shooter control model.
- **#2 Locomotion** — directional strafe clips: pick `Nav_Strafe{Jog,Walk}_{F/B/L/R}` by the
  travel direction **relative to facing** (dot of velocity with facing fwd/right), replacing the
  hardcoded `_F`. Verified: strafe-right → `Nav_StrafeJog_R`, forward → `_F`.
  (Upper-body aim-offset `Shooting_Aim_*` grid + start/stop/turn-in-place still PROVISIONAL.)
- **#3 Transformation** — robot and vehicle each carry a transform clip of **matching duration**
  (ToVehicle 1.97 s, ToRobot 1.13 s): one physical fold authored per mesh. Was played
  **sequentially** (robot fully → hard cut at full-fold mismatch → vehicle fully = the "crack",
  ~3.1 s). Now the outgoing mesh plays to the **midpoint**, then hands off to the partner mesh's
  clip **resumed at the same normalized time** — one continuous fold (verified frames 35→69→135:
  robot folds → matched mid-fold → clean vehicle). Weapon holstered for the whole transform.
  (Cross-mesh pop is minimised, not eliminated — true alpha cross-fade/visibility point is PROV.)
- **#4 Muzzle** — tracer + flash now originate at the **barrel tip** (weapon-local gltf
  (2.063, 0.017, 0.141), the frontmost vertex slice of `weapon.glb`), transformed by the weapon
  world matrix — **2.07 m forward** of the hand attach, so shots leave the gun, not the fist.
- **#5 Reload** — `Shooting_Reload_IonBlaster_ROBO` now plays (full-body, one-shot) while the
  weapon's reload timer runs. Verified pose + `reloading=1`. (WFC's additive `ADD_Shooting_
  Reload_*` upper-body overlay, to reload on the move, needs additive blending — PARTIAL.)
- Diagnostics added (env-gated): `WFC_AUTOSTRAFE/AUTOBACK`, `WFC_AUTORELOAD`, `WFC_FACELOG`,
  `WFC_MUZZLELOG`; frame log now prints `reloading`.
- **Still open:** #6/#7/#8 lighting cohesion (only coeff0 of 3 directional coeffs used; character
  lighting; materials), #10 camera tuning, and a magenta-material prop near spawn (nolightmap view).

## FIDELITY PASS 5 (2026-10-01) — BAKED LIGHTMAPS RECOVERED, DECODED, AND RENDERED
**Streets is now lit by its original authored baked lightmaps.** The licensee-144 lightmap
serialization was cracked: directional lightmap (3 coeff textures), texture = BE export index
into the ART export table (seekfree forward-export → `_LM` atlas), then CoordinateScale/Bias.
- `vs_lightmap.py` decodes all **1793 lightmapped components** → **1755/1952 props** mapped
  (atlas + CoordinateScale/Bias + HDR ScaleVector), 20 atlases (PNG via umodel, 1024² DXT1).
- `vs_map.py` emits TEXCOORD_1 + lightmap node extras → `world.glb`; renderer draws lightmapped
  submeshes unlit × atlas (UV1·scale+bias via texture matrix) × HDR ScaleVector (GL_COMBINE 4×).
  **1962 submeshes lit.** A/B: `WFC_NOLIGHTMAP=1`.
- Verified across spawn 0 / region 6 / region 18: per-region baked shadows + coloured bounce
  (purple/green/warm), bright lit doorways, high contrast — authentically WFC; no artefacts.
- Regression: collision 1.85 M tris, vehicle hover, robot, emissive glow all intact; exit 0.
- **Pipeline order:** vs_lightmap → vs_map → vs_collision (vs_map resets collision.glb).

## FIDELITY PASS 4 (2026-10-01) — lightmap atlases recovered; emissive glow applied
- **Lightmap ATLASES RECOVERED:** umodel decodes the 360-tiled `MP_IAC_Streets_ART_m_LM.xxx` →
  **78 LightMapTexture2D** (1024² PF_DXT1 RGB), preserved in `…/MP_IAC_Streets/lightmaps/`.
  Confirmed real baked lighting. **Component→atlas binding still BLOCKED** (seekfree GUID
  resolution; no instance imports; mixed-endian native block). Exact layout + blocker in
  FIDELITY.md. **No guessed lightmap mapping applied.**
- **EMISSIVE GLOW APPLIED:** Optimus's authored `*_emissive.png` glow masks are loaded and
  drawn as an additive self-illumination pass — blue glowing eyes/energon/Autobot vents (iconic
  WFC look), from original data. Robot/vehicle/weapon. Verified via screenshots.
- **Regression:** vehicle hover (Y −722.5 = ground+2 m) + cruise ~15 m/s + collision (1.85 M
  tris) + robot all intact after the renderer change; clean build, exit 0.

## FIDELITY PASS 3 (2026-10-01) — vehicle physics recovered; lightmaps investigated
- **Optimus VEHICLE PHYSICS RECOVERED** from `Default__TnHoverCarSimulationBlueprint`
  (TransGame.xxx) — WFC vehicles **hover**. Applied: max speed **15 m/s** (MaxLinearSpeed 1500),
  accel **30 m/s²** (3000), dash/boost **50 m/s** (DashSpeed 5000, 0.3 s burst), hover height
  **2 m** (SuspensionRadius 200), jump **12 m/s** (1200), turn ~π rad/s, terminal 80 m/s.
  Verified: vehicle floats 2 m above the street, cruises ~13.6→15 m/s. Boost on Sprint.
  (The Form CDOs only held `=1.0` modifiers; base values live on the *Simulation* blueprints.)
- **Baked lightmaps — investigated deeply, BLOCKED.** 78 LightMapTexture2D atlases exist;
  umodel can decode the 360 textures; meshes already carry TEXCOORD_1. Blocker: the per-
  component lightmap binding is native-serialized (licensee 144) with **GUID-based** texture
  refs (0 imports) and unresolved scale/bias float offsets. Precise findings in FIDELITY.md.
- **Robot movement sanity:** jump re-measured 6.41 m (analytically 6.25; no regression);
  GroundSpeed/accel/air/jump/capsule all intact.

## FIDELITY PASS 2 (2026-10-01) — recover from the executable + extend extraction
Ghidra/ReVa is live with `default.xex`; pawn/vehicle CDOs read from cooked packages
(`Game Dump/TransGame/CookedXenon/{TransGame,Engine}.xxx`) via `ue3pkg`+`props`.
- **Street collision RECOVERED.** props.json already carries authored block flags: **1693 of
  1952 props block**, 259 decorative don't. New `AssetTools/scripts/wfc/vs_collision.py`
  regenerates `collision.glb` = BSP + blocking volumes + **1693 blocking props** (instanced,
  5.46 MB → 1.85 M world tris). Player now stands on the street floor and is blocked by real
  building walls (previously walked through). Verified across 4 spawns (~180×164 m).
- **Robot movement RECOVERED** from `Default__TnPlayerPawn`/`Default__TnPawn`:
  GroundSpeed 5.5 m/s, AccelRate 20.48 m/s², AirControl 0.70, AirSpeed 15 m/s — all applied.
- **Jump/collision RECOVERED:** MaxJumpHeight 6.25 m (JumpZ derived 19.17 m/s; **measured
  6.39 m**), capsule radius 1.75 m / half-height 2.0 m (4 m tall), eye height 2.8 m.
- **Weapon socket rotation RECOVERED** (rotator yaw 172°/roll 30° → gltf matrix); applied.
- **Vehicle base physics**: still in a data asset (not parsed) — provisional.
- **Camera distance**: orbit-list driven (data asset not extracted) — provisional (9 m).
- **HeightFog RECOVERED**: LightColor (234,91,116) warm red-pink, Density 2e-5/UU, StartDistance
  2048 UU (from `MP_IAC_Streets_ART_m HeightFogComponent`) — applied (was guessed blue-grey).
- **Lightmaps**: 78 LightMapTexture2D in `MP_IAC_Streets_ART_m_LM.xxx`; per-component binding is
  native-serialized + 360-tiled textures — path documented in FIDELITY.md, #1 remaining visual gap.
- Fidelity table + provenance: `FIDELITY.md`.

## RENDERING MILESTONE 24 / 25 (2026-10-05, agents/rendering) — vertex lightmaps, volume grades
- **Render data must be regenerated** (tool changes in build_lighting.py): FLightMap1D samples now parse on every map (M24), and the PostProcessVolume grade lands in lighting.json postprocess (M25). Streets output is byte-identical.
- The renderer resolves clut.png against the map data dir, so the CLUT applies on the player route.
- Harness: the black-frame threshold is now 80 %, and WFC_M11_INHERITSTATE injects the depth-test leak. Release-path check: fix PASS, reproduction FAIL. 10 maps × 3 views PASS. Streets suite identical to ref_m21.
- API: `releasePreviewBody(h)` and `previewBodyCount()`.

## RENDERING MILESTONE 20 / 21 (2026-10-05, agents/rendering) — multi-map fidelity
- All 10 cooked MP maps build and render through the generic path. 30 captures pass, with 0 materials without a program,
  0 draws without depth testing and 0 GL errors.
- "Mostly black" maps root cause: build_lighting dropped most FLightMap2D records (a GUID search instead of a structural
  parse). Fixed; Streets is byte-identical, and Gorge / Rust / Seed / Berth / Broken Hope gain 4–10x lightmapped draws.
- Light, sky and fog colours had red and blue swapped (cooked FColor is B,G,R,A), CONFIRMED via authored.db. Fixed;
  Streets dynamic lighting and fog are re-baselined (work/ref_m21).
- Material translator: TextureSetSample added, so Molten's floors render. SceneTexture and RandomSeed remain open.
- Every MP chassis preview renders with original materials. Preview idle goes through the AnimSet chooser groups.
- Rebuild the render data (`-Map Standard` and each MP map) to pick these up.

## RENDERING MILESTONE 12 (2026-10-05, agents/rendering) — vignette ships
- The ships have no skeletal animation in the original (CONFIRMED from the cooked level), so the bind pose is correct.
- Their missing animation is Matinee DrawScale. `setFrontendActorScale` now applies it to ships and emitters, and
  emitters follow matinee poses.
- Frontend needs to evaluate the 7 DrawScale FloatProp tracks.
- Camera FOV tracks:
  - the title's tracks have no keys, so the camera FOV of 45 is already right;
  - the customization class cameras zoom 70 -> 60 / 65 over 0.5 s. The keys are exported to render data, and
    `frontendFloatTracks()` + `evalInterpCurveFloat` are provided for Frontend.
- Particle sprite and mesh sizes now scale with the emitter's scale (UE3 Source.Scale, HIGH CONFIDENCE). Streets is
  unchanged; the scaled title emitters draw at their authored size.

## RENDERING MILESTONE 11 (2026-10-04, agents/rendering) — real Release path: maps without depth testing
- **Root cause:** after the frontend menus, every map was drawn with `GL_DEPTH_TEST` disabled.
  - The GFx pass left it off (Frontend, fixed in a96f841).
  - Rendering's frame never re-established it.
  - Loads and data were complete; the human's log proves it.
- **Fixed:** `GLRenderer::beginFrame` establishes the frame's GL state.
- **Verified on the real Release layout:** with the human profile, Streets → Berth → Streets, three resolutions, a runtime
  fullscreen switch, and a 10-match single-process session. 0 GL errors throughout.
- **Guards:**
  - an opaque draw without depth testing or any GL error FAILs the verdict;
  - `tools/render/release_path_check.sh` checks the player route with structural floors. It fails the reproduced bug and
    passes the fix.
- **Integration:**
  - merge agents/rendering;
  - rebuild Debug and Release;
  - `build_render_data.ps1 -Map Standard`;
  - run `release_path_check.sh`.

## RENDERING MILESTONE 10 (2026-10-04, agents/rendering) — M06 playtest visual regression
- **Root cause:** the Release executable (`build/release/bin`) found no render data, so every map and menu scene silently
  used the legacy fixed-function renderer: black Streets, giant grey sphere and rainbow tori behind the menu.
  - Reproduced exactly from integration 95edd7b.
  - The integrated code and render data are correct.
- **Fixed:**
  - `renderDataRoot()` searches above the executable;
  - legacy fallback is an ERROR plus a red screen frame;
  - `IRenderer::renderDiagnostics()`.
- **Guards:**
  - `WFC_VISUALCHECK` writes per-capture JSON verdicts and periodic VISUALCHECK log lines;
  - `tools/render/visual_check.py` and `visual_suite.sh` cover fixed Streets cameras, the title scene and the human flow.
- **Transition audit:** frontend ↔ match shows no state or resource leak. Both matches in a cycle are identical, and the
  inherited GL state is harmless.
- **Lobby / preview (follow-up):**
  - `setFrontendSceneDraw` + `ueActorMatrix` give the preview pawn a draw path;
  - `-Map Standard` builds the five UI families;
  - lobby scenes now use the persistent level's data, so chassis materials compile;
  - the `build_lighting` BSP fix from Integration is applied.
- **Open:**
  - preview pawn pose, animation and placement (Gameplay / Frontend);
  - map FX following matinee poses (one title emitter);
  - multi-level scene composition.

## RENDERING MILESTONE 09 (2026-10-04, agents/rendering) — frontend scenes, loading, roster readiness
- The menus' live 3D levels render through `loadFrontendScene` / `drawFrontendScene`:
  - the title Cybertron scene is VISUALLY VERIFIED from the authored camera;
  - render data builds for all 5 UI families.
- `setLoadYield` keeps the loading movie presenting during map loads (longest blocking step 70 ms).
- The previous map is always released before a new load.
- Minimap: none, CONFIRMED absent in the original.
- HUD handoff updated with RE's exact Hud_GFX layout and kill-feed timing.
- Roster:
  - character materials come from the AssetTools roster (98, verified);
  - `setDrawOwner` gives per-character light environments;
  - roster materials are excluded from the prewarm (8.8 s → 0.18 s).
- Fixes:
  - TextureSample RGB output (matc);
  - missing FX distributions no longer abort;
  - the legacy path unbinds buffers;
  - per-draw uniform locations are cached.

## RENDERING MILESTONE 08 (2026-10-04, agents/rendering) — playtest regressions, HUD ownership, Canvas layer
- **Character jitter (M05):** caused by Gameplay's camera frame pacing, measured. Fix patch handed off:
  `docs/handoffs/GAMEPLAY_CAMERA_FRAME_PACING.md`.
- **Boost exhaust open / close:** Driving state flicker from Gameplay's provisional hull probes, measured. Handoff:
  `docs/handoffs/GAMEPLAY_BOOST_FX_FLICKER.md`.
- **HUD ownership:**
  - Hud_GFX (clock, scores, health, ammo, crosshair, kill / score messages) is Frontend's GfxHost;
  - the Canvas markers are Rendering's (`render::HudMarkers`, `drawCanvasText` with the original MarkerFont);
  - radar is UNKNOWN (no asset);
  - `docs/handoffs/FRONTEND_INMATCH_HUD.md`.
- **Diagnostics:**
  - `WFC_RENDERHZ` (deterministic display rate);
  - `WFC_SHOTEVERY=<dir>,<from>,<to>`;
  - `WFC_CAMLOG`;
  - `WFC_MARKERTEST`;
  - `WFC_PICK` (authored surface under the crosshair, for collision reports).
- **Render data:** `tools/render/build_hud.py` → `<render root>/_ui` (fonts, marker setups); part of
  `build_render_data.ps1`.

## RENDERING MILESTONE 07 (2026-10-03, agents/rendering) — Streets cleanup, frontend / next-map readiness
- Contract for the other lanes: `docs/RENDERER_CONTRACT.md`.
- Level travel: `IRenderer::unloadMapRenderData()` releases every GPU object. `WFC_RELOADTEST=<frame>` runs an in-process
  cycle.
- HUD: `IRenderer::drawMaterialTile()` (UE3 Canvas material tiles with per-draw params). The 15 UI_HudMarkers_p materials
  are compiled and verified (231/231). `WFC_TILETEST=1`.
- 2D: `drawScreenTriangles()` (alpha / premultiplied / additive / multiply / opaque, scissor) and `updateTexture()`, for
  GFx, Bink, loading and fades. `WFC_SCREENTEST=1`.
- Pickups (RE M05 §6):
  - factory meshes at the factory transform;
  - spin only while available (frozen when taken);
  - flag / bomb rest meshes gated to CTF / EXT.
- Map-agnostic: no absolute paths or Streets names in `src/render`. Props, pickups and destructibles come from
  render_index. The tools take the map name.

## RENDERING MILESTONE 06 (2026-10-03, agents/rendering) — human playtest translucency / smoke / glass
- UE3 translucency pass: every translucent primitive is drawn after all opaque geometry, back to front. This fixes glass
  and fog sheets being overdrawn by BSP, dark cards showing through ramps, and soft fades computed against incomplete depth.
- Shipped Xenon PS semantics:
  - additive colour x Opacity;
  - Opacity < 1/255 killed;
  - DepthBiasedAlpha default Bias 0.5;
  - BiasScaleInput only when uniform.
- The distance-dependent blue / purple / red FogSheet curtains are gone.
- Particles:
  - DynamicParameter output index fixed;
  - steam uses its authored 'SteamColor' (ColorByParameter);
  - steam has soft intersections and visibly animates.
- Diagnostics: WFC_M05TRANS (pre-M06 behaviour for A/B), WFC_IMMEDIATETRANS. Self-tests: shadows 32/32, DLE 20/20.

## RENDERING MILESTONE 05 (2026-10-03, agents/rendering) — Streets normal-play completion
- Consumes Gameplay d122ef4 state: setMapClock (movers / totem / pickup spin), setActiveGameRules, setActorHidden, setMapEffectState; KOTH active ring.
- Content meshes placed P*A*P (corrects M04's sideways totems / destructible / FX meshes).
- Pickups from AssetTools render_index: 14 ammo crate meshes + beams, 9 health + 1 overshield effects, available / taken / respawn; flag/bomb Disabled in TDM. Graybox pickups and the test dummy are no longer spawned in the slice (WFC_GRAYBOXPICKUPS / WFC_DAMAGETARGET).
- Particle flags: flagA = bEnabled (RE 940aa79); all 45 particle components resolved (32 drawn, 13 intentionally invisible).
- verify_permutations 216/216 (texture-expression block, 2-byte aligned scan). Audit 2399 correct / 23 intentionally invisible / 360 unknown (BSP without lightmaps; RE: 26 visible nodes).
- Self-tests: shadows 32/32, DLE 20/20. Release perf: idle 4.4 ms, transform 4.5, vehicle 4.6, hover 4.3 (firing 10.1, simulation cost outside the renderer).
- Diagnostics: WFC_SHOTLIST=<file>/WFC_SHOTDIR (many views per run), WFC_NOFRUSTUMCULL.

## RENDERING MILESTONE 04 (2026-10-03, agents/rendering)
- Streets regenerated from AssetTools 8d8195e (corrected SMCA transforms); normals by inverse-transpose; decals on corrected receivers; lightmap inventory 1795 texture / 28 vertex matches per component.
- Living world: rotating domes, SkyBeam Matinee, rule-gated objective bases + Conquest totems, intact destructible, 8 steam emitters, flag-invariant pickup emitters with script pickup state (setMapEffectState / setActiveGameRules / setActorHidden / setDestructibleState).
- Tools: build_movers.py, build_map_fx.py (in build_render_data.ps1); state-aware audit_map.py; verify_permutations 211/214.
- Diagnostics: WFC_DEBUGCAM=at:x,y,z,tx,ty,tz, WFC_GAMERULES, WFC_PICKUPTEST, WFC_DESTRUCTSTATE, WFC_MAPFX_ALLACTIVE, WFC_NOMAPFX, WFC_NOMOVERS, WFC_FX_FLAGREADING (experimental).

## RENDERING MILESTONE 03 PASS 5 (2026-10-02, agents/rendering)
- Character shadows in normal play (ReverseEngineering 7033f18): per light-environment synthetic projector copying the composite light, ModShadowColor = shadowFactor, FadeAlpha 1, native creation / relevance / DPG gates, native origin / push-back / W range / resolution, native 6-tap darkness-weighted blur.
- WFC_SHADOWSELFTEST 32/32, WFC_DLETEST 20/20. Diagnostics: WFC_NOCHARSHADOWS, WFC_SHADOWTEST=<light>, WFC_SUBJECTRELEVANCE=<hex>, WFC_BLURTIE, WFC_MASKDUMP.
- Still PARTIAL / UNKNOWN: synthetic-light registration, frustum fit + ScreenToShadowMatrix, directional / point projection-shader variants, blur tie branch, preshadows, weapon ShadowParent.

## RENDERING MILESTONE 03 PASS 4 (2026-10-02, agents/rendering)
- Native ShadowMask (ReverseEngineering 13c0953): RGBA8 at scene/2 (SizeX > 960), cleared to 1, z-fail stencil frustum, multiplicative DestColor x Src (alpha untouched), read as .r + half texel by the character pass.
- DirectLightAmbientContribution from the light environment (CubeSum ratio); BranchingPCF native tables; ShadowDepthBias 1165.08; projection gates (flag 0x4 + DPG bit).
- WFC_SHADOWSELFTEST 18/18, WFC_DLETEST 20/20. Character projection still opt-in (WFC_CHARSHADOWS) — shadowFactor link, shadow matrix, blur kernel, creation gates UNKNOWN.
- AssetTools 7a69756: Ion Blaster fine-aim HUD (no scope; instant first spread, fine-aim spread), pickup FX + wall-panel materials compiled (TransGame fallback package).

## RENDERING MILESTONE 03 PASS 3 (2026-10-02, agents/rendering)
- Native LightsVisibilitiesVolume decoder + query (ReverseEngineering b52dca9); Streets blob validated, C++ == Python port.
- Native DirectLightEnv for robot / vehicle (b52dca9 + c95dadd): gather, baked/unbaked visibility, ranking, composite shadow, update queue. WFC_DLETEST 20/20.
- DynamicShadowLuminanceScale consumption in the character uber shader (31f9a9b); shipped DSLS 0; mask production and DLAC CPU formula UNKNOWN (neutral mask).
- Xenos PWL degamma for SRGB textures; vertex-lightmap decode from microcode.
- Character shadows: all non-native stages implemented, opt-in WFC_CHARSHADOWS pending native bias/offsets.
- Tools: lvv_decode.py, lvv_query_check.py, perf_suite.sh; env WFC_DSLS / WFC_DLAC / WFC_SHADOWMASKTEST / WFC_LVVDUMP.

## RENDERING MILESTONE 03 PASS 2 (2026-10-02, agents/rendering)
- Distortion pass from Xenon microcode; hover rings refract; Trail_Distort / Distortion_Cloud / Glow_Mod ready for Systems' emitters.
- Vehicle material audit: docs/rendering/vehicle_material_audit.md. Render audit captures: tools/render/capture_audit.sh (docs/rendering/audit/).
- Flat pink/lavender floors = 44 mis-wound BSP polygons (fixed). Hidden actors not drawn; no-light components emissive-only.
- Character light visibility uses the authored robot/vehicle sample offsets (fractional visibility).
- Prewarm removes first-use builds; perf must be measured in Release (Debug inflates CPU costs ~10x).
- Diagnostics: WFC_LOCKSTEP, WFC_FRAMEREPORT, WFC_NODISTORTION, WFC_NOCULL, WFC_SHOWHIDDEN, WFC_SKIPMAT comp:, tools/render/pick_material.py.

## RENDERING MILESTONE 03 (2026-10-02, agents/rendering)
- Material translation verified against compiled permutations: 187/202 match (`tools/render/verify_permutations.py`).
- Vehicle/robot: CS_World camera/reflection vectors, cube LOD bias, Fresnel Exp; vehicle + robot MICs match their compiled permutations.
- Boost/hover/ram FX shaded by their original emitter materials (27 FX graphs compiled; soft depth fade, panners, blend-mode fog).
- Streets audit: `python tools/render/audit_map.py MP_IAC_Streets work/render/MP_IAC_Streets` (needs a `WFC_AUDIT_DUMP` run) → map_audit.json.
- Fixed: actor-placed props lightmaps (38 submeshes), vertex lightmaps (24 components), ScreenPosition, PixelDepth.
- Fixed: mottled grey/pink vehicle after transform (program cache keyed by Material* reused across robot/vehicle pose buffers).
- Fixed: hover light-cone plumes (TexCoord1 = UV0 on single-UV meshes); debug overlay toggle ignored in scripted runs; shell/mesh-particle light envs shared per 1 m cell.
- Energon red on Optimus confirmed from compiled permutations (blue is the False branch).
- HUD Ion Blaster crosshair from Hud_GFX.gfx (spread-driven prongs); no scope in fine aim (per HUD script).
- Renderer cost of shooting: light-env visibility memo + no env for unlit FX (env 3.5 ms → 0.4 ms/frame); the remaining ~55 ms/frame while firing is outside the renderer (simulation).
- Captures: `bash tools/render/capture.sh <outdir>`; env: `WFC_RENDERSTATS`, `WFC_AUDIT_DUMP=<file>`, `WFC_NOVERTEXLM`.

## FIDELITY PASS 1 (2026-10-01) — recover original WFC behaviour from authored data
Evidence root: cooked UE3 config `ExtractedAssets/config/Coalesced_ini/.../Cooked/*.ini`,
map metadata `ExtractedAssets/maps/*.json`, asset metadata `VerticalSlice/**/*.json`.
- **Gravity [CONF]** −29.4 m/s² (WorldInfo.DefaultGravityZ −2940); vehicles −19.4 (RB ×0.66).
- **Transformation [CONF+fix]** now **crossfades** (blend-in 0.115 s / blend-out 0.25 s from
  TnTransformation); was a hard cut at every clip boundary → the jaggedness. Cross-skeleton
  mesh handoff is still an inherent cut.
- **Camera FOV [CONF]** 75° horizontal (Xe-TransCamera TnFovCameraBehavior), converted to
  vertical by aspect; was a guessed 70° vertical.
- **Ion Blaster [CONF]** damage 15, interval 0.065, mag 50, reserve max 250 confirmed;
  **fixed** initial reserve 150, reload 1.5 s, range 300 m + 1.0→0.5× falloff, per-shot spread.
- **Lighting/materials [fix, PROV]** washed-out cause = ambient floored brightness at ~0.70;
  lowered ambient + warm key + specular + exp2 fog. (Baked lightmaps not extracted.)
- **Audio [fix, PROV]** master 0.5 + 3D attenuation/pan (`playAt`/`setListener`); was full-
  volume mono.
- **Map [investigated]** Streets composed from all 3 sublevels (BSP + 1952 props). Gaps:
  static-mesh collision not extracted (confines walkable area), 16 PrefabInstances + HeightFog
  not composed, lightmaps not extracted. See FIDELITY.md.

## DONE (verified via screenshots / logs)
- **P1 Scale / placement / camera.** World is authored in **metres** (no global scale
  applied). Optimus ≈ 4.4 m. Huge ±4800 bounds were the **skydome**, not a unit mismatch.
  Player spawns at the authored FFA start (363.5, −723.6, −341.8), ground-snapped onto the
  collision surface, facing the play-area centroid. Third-person follow camera
  (dist 9 m, height 3.2 m, mouse look, near 0.1 / far 20000). **Framing bug fixed:** a
  leftover `cam.y < 0.5 → 0.5` clamp was launching the camera ~720 m into the sky at the
  map's negative Y; removed. Optimus is now clearly framed on the Streets.
- **P2 Collision.** Flat-ground hack removed. Loads `collision.glb` (3,495 tris) into a
  uniform XZ grid (2 m cells). Downward ground raycast + grounding + gravity; horizontal
  wall blocking via segment tests; respawn when falling below kill-Z. Auto-walk test: player
  walks across real geometry staying grounded at Y≈−724.5.
- **P3 Skeletal animation.** Full glTF skin path: 106-joint skeleton, inverse-bind
  matrices, JOINTS_0/WEIGHTS_0, animation samplers/channels (T/R/S), LINEAR/STEP/CUBICSPLINE
  interpolation, quaternion slerp, hierarchical node transforms, **CPU skinning** each step
  into a dynamic mesh. State machine (ROBOT: idle/walk/run/jump/fall; VEHICLE:
  idle/move) keyed off the **authoritative `extras.category`** strings in the GLBs.
  Additive (`ADD_`) clips are skipped for base poses. Debug log: form / anim name / anim time.
- **P4 Transformation.** ROBOT↔VEHICLE via the paired transform clips
  (`transform_to_vehicle` → handoff → `vehicle_transform_to_vehicle`, and the reverse).
  **Separate skeletons, no interpolation** between them; skeleton/mesh handoff at the end of
  the outgoing clip; input locked for the whole transition; settles into the new form's idle.
  Verified: ROBOT → Transform_ToVehicle_ROBO → VEHICLE → …_Veh → Nav_Hover_Pose_VEH.

- **P5 Materials / textures.** glTF materials parsed (baseColorTexture + baseColorFactor);
  meshes split into per-material submeshes with UVs on both the static baker (map) and the
  skinned loader (characters). PNGs decoded via a platform adapter (GDI+) behind
  `platform::decodeImage` → `render::ImageData` → `IRenderer::uploadTexture` (GL texture),
  cached by URI. Fixed-function GL modulates the texture by the directional+ambient light.
  Verified: Optimus red/blue paint + back wheel; Streets wear their rust/metal textures.
  24 textures loaded, 0 failed. (Normal/emissive/specular deferred — not straightforward in
  GL 1.1 fixed-function.)

- **P6 Ion Blaster.** `weapon.glb` loaded + textured, held at `WeaponSocket_Primary`
  (bone `R_Arm03_Elbow_XB`, node 42) — the weapon follows the bone's world transform each
  frame (robot form only). Hitscan while trigger held: 15 dmg, 0.065 s interval, 50-round
  mag, 250 reserve, auto-reload on empty + manual reload (R), ~1.8 s. Ray vs collision mesh +
  vs damageable targets (AABB). Yellow tracer (muzzle→impact) + muzzle-flash box. A practice
  **DamageTarget** dummy spawns ahead of the player. Verified: weapon in hands, tracer fires,
  ammo 50→25 under auto-fire.
- **P7 Audio.** `audio::IAudio` abstraction + Win32 `waveOut` polling mixer (decodes PCM WAV,
  nearest-resamples to 48 kHz stereo, mixes overlapping one-shot voices; silent no-op if no
  device). Cues loaded & wired by gameplay event (edge-detected in `World::tick`): fire
  (Ion Blaster), reload (gun foley), transform (gears), land (thump). Gameplay never touches
  Windows audio directly. Footsteps deferred (no clear footstep asset located).
- **P8 Dev tools.** Screenshot capture (kept). Runtime debug overlay toggle (B / env
  `WFC_DEBUGDRAW`): world/collision bounds, player capsule, aim ray, weapon-socket marker.
  Window-title HUD now shows FPS, position, speed, form, grounded/airborne, current anim +
  time, HP, ammo/reserve, reload + DBG state. STATUS.md maintained; headless verification
  knobs documented below.

## CURRENT
- P1–P8 complete and screenshot-verified. Polish candidates below.

## NEXT (polish, optional)
- Weapon socket rotation (currently translation-only offset; orientation is approximate).
- Walk clip picks a strafe-jog; prefer a forward locomotion clip by name.
- Normal/emissive/specular maps (needs a programmable path; GL 1.1 fixed-function can't).
- Footstep audio; smoother transform-handoff frame (config-driven fraction < 1.0).

## BLOCKERS
- None. (Headless smoke runs ~real-time, so long clips need proportionally more
  `WFC_SMOKE_FRAMES`; not a product blocker.)

## CONTROLS
- WASD move, mouse look, Space jump, LMB fire (hold = auto), RMB fine aim (robot, toggle) / boost (vehicle, hold),
  Shift dash (vehicle: hover dash; nitro/ram while boosting) [CONF], R reload (tap), F transform,
  C free/capture cursor, B debug overlay, Esc quit.

## ASSET PATHS (root = `F:/Transformers Rebuild/ExtractedAssets/VerticalSlice`, override `WFC_ASSETS`)
- Map:        `Maps/MP_IAC_Streets/world.glb`, `collision.glb`, `spawnpoints.json`
- Optimus:    `Characters/Optimus/robot.glb` (skin+75 clips), `vehicle.glb` (skin+13 clips),
              `character.json`
- (P6) Weapon: `Weapons/IonBlaster/…`

## DEV / TEST ENV VARS
- `WFC_SMOKE_FRAMES=N`  headless; run N frames then exit.
- `WFC_SHOT=path.bmp`   capture a screenshot on the final smoke frame.
- `WFC_DEBUGCAM=front|top`  diagnostic camera + a bright beacon on the player.
- `WFC_AUTOWALK=1`      hold Forward (scripted locomotion test).
- `WFC_AUTOFIRE=1`      hold the trigger (scripted weapon test).
- `WFC_AUTOTRANSFORM=F` trigger a transform at frame F.
- `WFC_DEBUGDRAW=1`     enable the debug overlay from start (same as toggling B).
- `WFC_FIXYAW=rad` / `WFC_FIXPITCH=rad`  pin the camera (= aim) yaw / pitch.
- `WFC_STARTVEHICLE=1`  start in vehicle form. `WFC_AUTOSTRAFE/AUTOBACK/AUTORELOAD/AUTOJUMP=1`
  scripted inputs; `WFC_FACELOG`/`WFC_MUZZLELOG` facing/muzzle diagnostics.
- `WFC_ASSETS=dir`      override the asset root.
- `WFC_ANIMLOG=1` / `WFC_NOTIFYLOG=1` / `WFC_CUELOG=1` / `WFC_BOOSTLOG=1`  weapon layering / AnimNotify / SoundCue / vehicle boost logs.
