# WFC frontend lane: boot → menus over live scenes → lobbies → loading → match HUD → results → return

Branch `agents/frontend` (pass 3, 2026-10-04, based on integration/milestone-05). The shipped game is the specification.
WFC shipped on PC, and its Scaleform movies contain the PC SKU's own branches (`HmUtility.Platform == 'WIN'`). The
rebuild presents those by default, so most "PC behaviour" here is authored, not invented.

Sources:
- RE notes: `MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md`, `MILESTONE05_PLAYTEST_RE.md`,
  `OVERNIGHT_2026-10-04_HUD_VEHICLE_FRONTEND_ROSTER_AI.md`, the script decompilation, the AS2 dumps.
- AssetTools: `manifests/frontend_package/` (`ui_scenes.json`, `loading_handoff.json`, `mp_registry.json`),
  `manifests/hud/`, `manifests/mp_content/roster_package.json`, the UI level exports `VerticalSlice/Maps/UI_*`, and
  `authored.db` (read-only).
- Shipped config: `TransCustomization.ini`, `TransWeapons.ini`, `TransChallenges.ini`, `TransGame.int`, `UIText.int`,
  `GFxUI.int`.

Labels: **CONFIRMED** (CONFIRMED ORIGINAL), **HIGH**, **PC ADAPTATION**, **PARTIAL**, **PROVISIONAL**, **UNKNOWN**,
**SERVICE DEPENDENT**.

## 1. What the player sees

| Stage | Presentation | Status |
|---|---|---|
| Boot | `TF_InitialStartup` underlay, `MovieLoader_GFX` | CONFIRMED |
| Intro | Logo_Activision / Hasbro / HighMoon, FMV_intro, full screen with their audio (Systems device) | video CONFIRMED; audio track layout HIGH, language track PARTIAL |
| Title / main menu | **live UI_FrontEnd_m scene** (+ the streamed battle vignette) under FrontEnd_GFX's PC menu: Campaign / Multiplayer / Escalation / Settings / Extras / Accounts / Exit Game; the authored mouse cursor | CONFIRMED structure; scene shading PARTIAL (interim renderer path in this tree; the full path is Rendering's, see §4) |
| Multiplayer | PartyLobby_GFX over the **UI_CharacterCustomization_m** room; the local player listed | CONFIRMED |
| Private match | Choose a Game Mode → Host Options (authored values) → GameLobby_GFX (map thumbnail, Start Game, countdown) | CONFIRMED |
| Loading | LoadScreen_GFX over the TF_LoadingScreen Bink, **animating through the whole world load** | CONFIRMED content; presentation HIGH (§5) |
| Match start | CustomTransformers_GFX "Choose Character" (4 class presets from the roster), then PreGameCountdown | CONFIRMED flow and data |
| In match | **Hud_GFX**: segmented health / overshield, ammo, crosshair, team score panel, clock, kill feed, announcements; **InGameStats** scoreboard on ShowScores | CONFIRMED movie; feeds §7 |
| Pause | PauseMenu_GFX, multiply-tinted over the live match | CONFIRMED |
| Match end | EndGameStats_GFX "Experience Earned" (per-specialty levels from the authored LevelTable, 0 XP offline), then return to the lobby | CONFIRMED flow; XP values offline |
| Return | the game lobby, Back → frontend, the title scene again | CONFIRMED |

### Screen classification (keyboard / mouse audit, screenshots in the pass)
| Screen | State | Notes |
|---|---|---|
| Main menu, Multiplayer, Private Match, Host Options, Game Lobby, Pause, Results | **WORKING** | original movies and data |
| Settings (Graphics / Audio / Controls) | **WORKING** | values from the profile; runtime owners for volume / camera / gamma pending (§8) |
| Create a Character / Choose Character | **WORKING (selection)**, **PARTIAL (editing)** | presets from the roster; customization edits are not persisted yet |
| Teletraan I → Challenges | **WORKING** | 121 authored challenges; progress needs the stats service (0 offline) |
| Teletraan I → Leaderboards | **SERVICE DEPENDENT** | the screen opens; the online stats archive is absent |
| Friends List | **SERVICE DEPENDENT** | "No Friends Online..." |
| Find Match | **SERVICE DEPENDENT** | the playlist list with authored names; matchmaking needs the online service |
| Accounts (PC) | **SERVICE DEPENDENT** | Demonware online accounts: none listed; the local name comes from the profile identity |
| Escalation | **PARTIAL** | the lobby and Escalation Info work; ESC maps are not exported (map "UNAVAILABLE") |
| Campaign | **PARTIAL** | Solo / Co-op menu and the first-boot Brightness prompt work; the campaign itself is out of scope |
| Extras | **PARTIAL** | Concept Art / Movies / Unlockable Characters / Credits open; their content is not audited |

## 2. Input (CONFIRMED key map, logical actions)
- **One layer.** Every frontend action is a logical UI command (`platform::UiKey`), bound to named keys and pad buttons
  in `platform::UiBindings`, with optional overrides in `wfc_input.ini` `[UI]` (e.g. `Accept=Enter,Space`,
  `Pad.Start=Start`). Menus consume the commands as the movies' own key codes (KeyListener → navigate* / buttonA / B ...).
- **Defaults** follow `GFxUI.KeyMap`: A Enter, B Escape, X F1, Y F2, Start F3, Back F4 (+ Tab for ShowScores), LB / RB
  PgUp / PgDn, LT / RT Home / End, D-pad arrows; pad = Xbox layout. **CONFIRMED** (RE §C).
- **Platform identity.** `$version` = `WIN` (the PC SKU, default) or `XBOX360` (`WFC_PLATFORM`). On WIN the shipped
  movies skip Press START, use `mc_menuMainPC`, show clickable Back / XP buttons instead of pad hints and expose the PC
  graphics options. **CONFIRMED** (AS2 branches).
  - Console presentation: Space also sends Start (**PC ADAPTATION**).
  - `ShouldShowStartScreen` follows the script: once Press START is passed, returning to the frontend lands on the
    main menu (**CONFIRMED**; this fixes the playtest's "Back returns to Press START").
- **Mouse.** Flash 8 button semantics in the runtime:
  - button-mode clips, including inherited `HmButton` handlers;
  - shape / `hitArea` / mask hit tests;
  - rollover / press / release / drag events, Mouse listeners, `_xmouse`.
  - `Cursor_GFX` (TnUIController.MouseCursorUI) is the pointer; the OS cursor is hidden over the window.
- **In match.** ShowScores (Select: pad Back, Tab, F4) toggles the scoreboard with input focus; Escape / Start =
  `showmenu` (pause). A menu with focus releases mouse-look; closing it recaptures.
- **Rebinding.** Gameplay keys are Gameplay's table. The logical-action layer is the extension point for future PC
  rebinding (**FUTURE PC EXTENSION**).

## 3. Settings
| Option | Origin | Runtime owner |
|---|---|---|
| Brightness (first-boot prompt + menu) | ORIGINAL (GammaSetting 50) | stored; Rendering gamma: pending |
| FX / Dialogue / Music Volume 80, Subtitles | ORIGINAL | stored; Systems volume API: pending |
| Vibration, Scheme A/B, Invert Y ×4, Camera Sensitivity 30 | ORIGINAL | stored; Gameplay camera API: pending |
| Resolution, Fullscreen, Texture Quality, VSync, Commit | ORIGINAL PC SKU (`PCSettings.*`) | window resolution / borderless fullscreen (PC ADAPTATION), VSync (swap interval); texture quality: stored, no renderer control |
| FOV, refresh rate, rebinding, mouse sensitivity, quality presets | not in the shipped menus | **PC EXTENSION / FUTURE PC EXTENSION**; not added to the original menu |

- Values persist in `wfc_profile.ini`: `[ProfileData]` holds the original fields, `[PCSettings]` the PC SKU fields, and
  `[Identity]` the local name.
- `LocalProfile::onApplied` is the seam that owners subscribe to.

## 4. Live frontend scenes
- **Kismet / camera (Frontend).** `tools/frontend/export_frontend_scenes.py` exports each UI level's matinees, actors,
  remote events and triggers from authored.db (read-only) → `data/frontend/scenes.json`. `frontend::FrontendScene`
  follows the UI level:
  - `enterFrontEnd` starts Primary Camera, Camera Orbiter (393.55 s loop) and Fade In;
  - the remote event `StartFireworks` plays the vignette's 1st / 2nd / 3rd QTR and Plane Fly By;
  - the lobby movies' fscommands drive the customization room.
  - The camera pose is evaluated each frame (UE3 interp curves, RelativeToInitial, hard attachment), and so is every
    matinee-moved actor's world pose. **CONFIRMED** data; evaluation **HIGH**.
- **Drawing (Rendering).** `core::FrontendSceneGL` uses Rendering's `IRenderer::loadFrontendScene` /
  `drawFrontendScene` / `unloadFrontendScene`, `setFrontendActorTransform` and `setLoadYield` (agents/rendering 004327c
  / 8f94d68) when the tree has them (compile-time detection). Otherwise it uses an interim path through the public
  `IRenderer` map API: world.glb + lightmaps, legacy shading, no particles.
- **Verified.** In a merge preview with Rendering, the title shows the orbit nebula, ring galaxy and debris under the
  original menu.
- **Lifetime.** The scene's GL objects are released before a match loads (the renderer is recreated after each match).
- **GFx over the scene.** The default framebuffer is copied into the GFx target, so SWF blend modes act on the scene:
  - multiply: Settings / pause backdrops;
  - add: glows;
  - also screen / lighten / darken / subtract.
  - Per item, not Flash group compositing: **PARTIAL**.

## 5. Loading
- **Original** (RE playtest §6): the loading Bink plays on the render thread while the game thread blocks, and closes
  on `CanCloseLoadingMovie` (complete game context).
- **Rebuild.** Long load loops call `core::loadYield(where)` between complete steps: World load stages and textures,
  Rendering's submesh loop (`setLoadYield` after the merge). Inside each yield the application presents one frame:
  window messages, the LoadScreen_GFX animation and the Bink underlay. The flow is not ticked; the load is not faked.
  - Streets: the longest frozen interval dropped from 23.4 s to 1.9 s (the world.glb parse); about 177 presented frames.
  - `match.loaded` reports `loadingFrames` / `maxFrameGapMs` / `maxGapAt`.
  - The screen closes when the match world is loaded (HIGH), and no loading overlay remains in the match (M05 fix kept).

## 6. Movie audio
- **Layout.** The Bink movies have 10 mono FLAC tracks: 0 / 1 front bed (correlated), 2 / 3 surrounds, 4 LFE, 5–9
  dialogue centres per language (uncorrelated speech, identical on the logos, aligned with the INT subtitles).
- **Mix.** Track 5 is used for INT (PARTIAL: which track the native player picks is an RE item). Folded 5.1 → stereo,
  cached in `wfc_cache/movies`.
- **Playback.** Played from the first video frame as a 2D voice on the Systems device (no second engine); stops on skip
  or end. Queued intro movies decode in the background.

## 7. In-match HUD, scoreboard, results
- **Hud_GFX** (TnHUD.HudMovie): open for the match, shown in the UI states InGame / Spectating (RE A8). It receives the
  movie's own `_global` notifications on change, from Gameplay's `hudState()`:
  - segmented health (175/125/125/125), overshield;
  - clip / reserve with capacities, weapon, form, spectating.
  - **Kill feed:** `GameMessage` from the TnDeathMessage templates (`TransGame.int` DeathString / Suicide), names
    coloured local white / teammate 50B5D5 / enemy F03C3C, neutral words FF9333.
  - **Announcement:** `GameAnnouncement` with the mode name at match start.
  - **Clock / score:** the movie's data-store bindings.
  - **Crosshair:** SetWeaponCrosshair(2) for the IonBlaster is PROVISIONAL (native mapping UNKNOWN).
  - **No minimap / radar** (none shipped, CONFIRMED).
- **Scoreboard:** InGameStats_GFX with Gameplay's full roster rows (`<CurrentGame:Players>`); `TextField.variable`
  cells.
- **Results:** EndGameStats_GFX with `GameOverMessage`, per-specialty LEVEL 0 / "500 to next level" / 0 XP (offline,
  as the original without a stats archive).

## 8. Character selection
- **Roster.** `frontend::CharacterRoster` reads AssetTools `roster_package.json`: 33 chassis by RE id, display names,
  class / faction, locks, glTFs, plus the four class presets with per-faction chassis and loadouts.
- **Fresh profile.** One character per specialty: Scout, Scientist, Soldier, Leader (`Default<Class>`, slots unlock
  at 5 / 10). **CONFIRMED**.
- **Bindings.** `Customize.*` (TnCharacterScriptBinding) serves the list, details and selection. The collections
  `<TnMenuItems:Weapons|Abilities|Skills|Chassis|Challenges>` come from the authored `TnDataProvider` objects (278).
- **Flow.** The match's WaitingOnGameStart shows "Choose Character". Selection runs
  `GameFlow::selectCharacter(name, type, chassis per faction)` → pre-game screen. Automation auto-selects the first
  preset unless `WFC_CHARSELECT=1`.
- **Gameplay hand-off.** Gameplay should spawn the team's chassis from `flow.selectedCharacter()` and gate the spawn on
  it. Today Gameplay still spawns Optimus.
- **Unlocks.** No unlock rules beyond the authored lock flags are inferred.

## 9. Local identity
- `LocalProfile::playerName()`: `[Identity] Name` in `wfc_profile.ini`, else `WFC_PLAYERNAME`, else "Player" (**PC
  RECONSTRUCTION FALLBACK**; no Xbox Live identity).
- Shown by the player lists (now visible thanks to `TextField.variable`) and the kill feed.

## 10. Architecture (modules)
```
Application (glue) - frontend boot, loadMatch (loadYield presentation, scene release), runMatch (HUD feed, scoreboard),
                     unloadMatch; FrontendSceneGL; display hooks; Systems audio adapter (FrontendAudioRuntime)
frontend::FrontendRuntime - GameFlow (levels, travel, lobby, UIController, LocalProfile, selection), DataStores,
                     Catalog (manifests + shipped config + providers), CharacterRoster, FrontendScene, HudController,
                     MovieAudio, ScriptDriver, bridge (Game / Online / Customize / PCSettings / Account / Stats / Sound)
ui::GfxPresenter    - flow movies, HUD, scoreboard, movie-opened overlays, cursor, loading movie, video; mouse / keys
gfx::Player / avm1  - SWF8 + AS2 runtime (mouse, variable text, Flash 8 action order)
ui::GfxRendererGL   - stencil fills, masks, blend modes over the backdrop, MSAA
platform            - UiBindings, Win32 window (keys, pad, pointer, display modes, VSync), Media Foundation movies
core::LoadYield     - cooperative presentation during synchronous loads
```

## 11. Running
| Variable | Effect |
|---|---|
| `WFC_PLATFORM=XBOX360` | console presentation (Press START, pad hints) instead of the PC SKU |
| `wfc_input.ini` `[UI]` | UI binding overrides |
| `WFC_CHARSELECT=1` | automation waits for a real character selection |
| `WFC_NO_FRONTEND_SCENE=1` | menus without the 3D scene |
| `WFC_SCENE_ONLY=1`, `WFC_GFX_EMPTY=1` | diagnostics: scene layer alone / empty GFx layer |
| `WFC_NO_VIDEO=1` | no movie decoding |
| `WFC_CACHE=dir` | movie-audio cache directory |
| `WFC_LOADSHOTS=prefix` | capture loading frames 10 / 60 / 120 from inside the load |
| `WFC_FRONTEND_SCRIPT` | as before, plus `ui:<Action>`, `mouse:x,y`, `click:x,y`, `clickclip:<clip path>` |
| `wfc_gfxdump <movie>` | prints bridge calls with arguments; `WFC_DUMP_SHAPE=<id>` prints a shape's fills |
| `tools/frontend/export_frontend_scenes.py` | regenerates `data/frontend/scenes.json` (authored.db read-only) |

## 12. Handoffs
- **Rendering:**
  - Your frontend-scene API is integrated; matinee actor poses are sent via `setFrontendActorTransform`.
  - Still open: `setActorHidden` from matinee visibility tracks (to be exported); emitter toggles
    (`setMapEffectActive`); the preview pawn (`setFrontendPreviewCharacter` mapped onto drawDynamicMesh + setDrawOwner,
    as you proposed); gamma from the profile; texture quality.
  - Once Hud_GFX's crosshair is confirmed, disable the reconstruction reticle (`World` → `setReticle`).
- **Gameplay:**
  - spawn the selected character's chassis for the player's team and gate the spawn on the selection;
  - camera sensitivity / invert Y per form from `LocalProfile`;
  - the damage type in kill events (kill-feed weapon icons);
  - XP / point events for `PointEvent`.
- **Systems:**
  - user volume categories (FX / Dialogue / Music) from `LocalProfile`;
  - optionally take movie audio as a cue-free stream API instead of WAV cache files.
- **AssetTools:**
  - matinee visibility / emitter-toggle tracks for the UI levels;
  - the ESC maps for the Escalation lobby.
- **RE:**
  - the Bink language-track selection (BinkSetSoundTrack);
  - `SetWeaponCrosshair` native mapping;
  - provider column header strings.
- **Integrator (merge notes with agents/rendering):**
  - `WfcPipeline.cpp`: take Rendering's `yieldLoad()`; it reaches `core::loadYield` through `setLoadYield`.
  - `Application.cpp`: Rendering's `WFC_FRONTENDSCENE` diagnostic block belongs in `Application::run()` before
    `if (frontend_) { runFrontend(); return; }`.
