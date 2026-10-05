# WFC frontend lane: boot → menus over live scenes → lobbies → loading → match HUD → results → return

Branch `agents/frontend` (pass 4, 2026-10-04, based on integration/milestone-06: the human-playtest correctness pass).
The shipped game is the specification.
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
| Intro | Logo_Activision / Hasbro / HighMoon, FMV_intro, full screen with their audio (Systems device), **on every launch** (HasWatchedIntroMovie is a per-process flag, §13) | video CONFIRMED; audio track layout HIGH, language track PARTIAL |
| Title / main menu | **live UI_FrontEnd_m scene** (+ the streamed battle vignette) under FrontEnd_GFX's PC menu: Campaign / Multiplayer / Escalation / Settings / Extras / Accounts / Exit Game; the authored mouse cursor | CONFIRMED structure; drawn by Rendering from the UI render data (§4); without that data the menu sits on black (never the raw level) |
| Multiplayer | PartyLobby_GFX over the **UI_CharacterCustomization_m** room; the local player listed | CONFIRMED |
| Private match | Choose a Game Mode → Host Options (authored values) → GameLobby_GFX (map thumbnail, Start Game, countdown) | CONFIRMED |
| Loading | LoadScreen_GFX over the TF_LoadingScreen Bink, **animating through the whole world load** | CONFIRMED content; presentation HIGH (§5) |
| Match start | CustomTransformers_GFX "Choose Character" (the player's 4 characters); it stays up until a character is chosen, then PreGameCountdown (before the start) and InGame on the spawn | CONFIRMED flow and data (§14) |
| In match | **Hud_GFX**: segmented health / overshield, ammo, crosshair, team score panel, clock, kill feed, announcements; **InGameStats** scoreboard on ShowScores | CONFIRMED movie; feeds §7 |
| Pause | PauseMenu_GFX, multiply-tinted over the live match | CONFIRMED |
| Match end | EndGameStats_GFX "Experience Earned" (per-specialty levels from the authored LevelTable, 0 XP offline), then return to the lobby | CONFIRMED flow; XP values offline |
| Quit / Back | "Quit Game?" (TnQuitMessageBox) → private match or game lobby → **party lobby**; party lobby → front end; Exit Game asks first | CONFIRMED (§15) |

### Screen classification (keyboard / mouse audit, screenshots in the pass)
| Screen | State | Notes |
|---|---|---|
| Main menu, Multiplayer, Private Match, Host Options, Game Lobby, Pause, Results | **WORKING** | original movies and data |
| Settings (Graphics / Audio / Controls) | **WORKING** | values from the profile; Controls → Mouse/Keyboard Layout is the original read-only reference card (§3) |
| Choose Character / Create a Character | **WORKING**; preview pawn **PARTIAL** | the player's characters: rename, weapons, abilities, chassis, colours, reset, saved (§8); the preview pawn has no owner yet |
| Teletraan I → Challenges | **WORKING** | 121 authored challenges; progress needs the stats service (0 offline) |
| Teletraan I → Leaderboards | **SERVICE DEPENDENT** | the screen opens; the online stats archive is absent |
| Friends List | **SERVICE DEPENDENT** | "No Friends Online..." |
| Find Match | **SERVICE DEPENDENT** | the playlist list with authored names; matchmaking needs the online service |
| Accounts (PC) | **WORKING (PC ADAPTATION)** | the original's Demonware accounts are SERVICE DEPENDENT; offline: local account names typed in the original prompt, sign in = player name (§9) |
| Escalation | **PARTIAL** | the lobby and Escalation Info work; ESC maps are not exported (map "UNAVAILABLE") |
| Campaign | **PARTIAL** | Solo / Co-op menu and the first-boot Brightness prompt work; the campaign itself is out of scope |
| Extras | **WORKING** | Concept Art; Movies and Credits play full screen with their audio and return to the menu (no soft-lock); campaign movies locked on a fresh profile (authored A1..D5Difficulty = -1); Unlockable Characters opens |

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
- **Rebinding.** The shipped menus have **no key rebinding** (CONFIRMED: no binding calls in any movie; §3 Controls).
  Gameplay keys are Gameplay's table. The logical-action layer is the extension point for future PC rebinding
  (**FUTURE PC EXTENSION**).
- **Text entry.** Input TextFields (TextPrompt_GFX: account names, character renaming): typed characters, caret,
  Backspace / Delete / Left / Right / Home / End, Enter / Escape through the prompt's Key listener, mouse click focus
  (§16).
- **Full-screen movies** (intro chain, Extras Movies / Credits) take all input while they play and until the skip key
  is released.

## 3. Settings
| Option | Origin | Runtime owner |
|---|---|---|
| Brightness (first-boot prompt + menu) | ORIGINAL (GammaSetting 50) | Rendering `setDisplayGamma` (2.2 + Lerp(-0.95, 0.95, Gamma/100)) |
| FX / Dialogue / Music Volume 80, Subtitles | ORIGINAL | stored; Systems volume API: pending |
| Vibration, Scheme A/B, Invert Y ×4, Camera Sensitivity 30 | ORIGINAL | stored; Gameplay camera API: pending |
| Controls → Mouse/Keyboard Layout | ORIGINAL PC SKU: a **read-only** reference card per form (Robot / Car / Truck / Tank / Jet) | `Console.GetKeyDescription` from `TnPlayerInput.KeyDescriptions` (Xe-TransInput.ini keys, TransGame.int texts, per-form overrides; MapInputKeyForController) |
| Resolution, Fullscreen, Texture Quality, VSync, Commit | ORIGINAL PC SKU (`PCSettings.*`) | window resolution / borderless fullscreen (PC ADAPTATION), VSync (swap interval); texture quality: stored, no renderer control |
| Antialiasing | `HmInterfacePCSettings` declares Get/Set/List, but **no shipped menu calls them** | not shown |
| FOV, refresh rate, key rebinding, mouse sensitivity, quality presets | not in the shipped menus | **PC EXTENSION / FUTURE PC EXTENSION**; not added to the original menu |

- Values persist in `wfc_profile.ini`: `[ProfileData]` holds the original fields (all 74 `Default__TnProfileSettings`
  fields and defaults, `data/frontend/profile_defaults.json`), `[PCSettings]` the PC SKU fields, `[Identity]` the local
  name and `[Accounts]` the local accounts.
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
  `drawFrontendScene` / `unloadFrontendScene`, `setFrontendActorTransform`, `setActorHidden`, `setMapEffectActive` and
  `setLoadYield` (integrated in M06). Frontend supplies the scene identity (UI level family), the authored camera,
  matinee time, actor poses, visibility (matinee event keys → SeqAct_ToggleHidden) and emitter toggles
  (InterpTrackToggle); Rendering draws.
- **Missing render data.** The scene needs the UI render data in `work/render`: build it once per worktree with
  `tools\render\build_render_data.ps1 -Map Standard` (Streets + the five UI families; agents/rendering 359da41, after
  the merge) or `-Map UI_FrontEnd` / `-Map UI_PartyLobby` per family. Without it nothing is drawn under the menus and
  the log names the command. The old interim path (raw world.glb with no original materials) was the playtest's **malformed
  background** (white sky dome, opaque rainbow rings) and is no longer used when Rendering's API exists.
- **Verified.** With the render data, the M06 title shows the orbit nebula, energon ring, galaxy and the vignette's
  ships under the original menu.
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
- **Bindings.** `Customize.*` follows the decompiled TnCharacterScriptBinding (**CONFIRMED**):
  - `GetCharacterChassis(name, FactionFilter)` = `ChassisTypes[FactionFilter]` (it returned both, so no weapon's
    ChassisRestriction matched: Weapon Slot 2 was empty and Back committed "undefined");
  - `GetCharacterWeaponTypes` inserts `MeleeWeapons[0]` at index 2;
  - colours: black means "the palette swatch" → `GetPixelColor(palette, x, y)` sampled from the palette PNGs
    (autobotPalette_0-4, decepticonPalette_5-9), packed `0xRRGGBB;palette;x;y`;
  - `CommitCharacter` (rename, weapons, abilities, upgrades, chassis + colours), `ResetCharacter`, `ClearCharacter`,
    `WriteCustomizationFile` (the rebuild's file: `wfc_characters.ini`, **PC ADAPTATION**; loaded at start).
  - The presets' colour / palette / coordinate data come from `data/frontend/character_presets.json`
    (`tools/frontend/export_character_presets.py`, authored.db) - the roster package has no colours.
  - The collections `<TnMenuItems:Weapons|Abilities|Skills|Chassis|Challenges>` come from the authored
    `TnDataProvider` objects (278).
- **Flow.** The match's WaitingOnGameStart shows "Choose Character" until a character is chosen (§14). Selection runs
  `GameFlow::selectCharacter(name, type, edited chassis per faction)`. Automation auto-selects the first character
  unless `WFC_CHARSELECT=1`.
- **Ownership.** Frontend owns the selection UI and the player's character data; Gameplay owns the authoritative
  selected character / body (it spawns the team's chassis; today its pawn is still Optimus); Rendering owns drawing
  the preview pawn. `Customize.UpdatePreviewCharacter` / `TransformPreviewCharacter*` are forwarded through
  `FrontendRuntime::previewHook` (`customize.preview` trace) - no owner yet (**PARTIAL**, handoff).
- **Unlocks.** No unlock rules beyond the authored lock flags are inferred.

## 9. Local identity
- `LocalProfile::playerName()`: the signed-in local account, else `[Identity] Name` in `wfc_profile.ini`, else
  `WFC_PLAYERNAME`, else "Player" (**PC RECONSTRUCTION FALLBACK**; no Xbox Live / Demonware identity).
- **Accounts (PC).** The original's accounts are Demonware online accounts bound to the product key
  (`TnAccountActionScriptBinding`: CreateOnlineAccount, Login) - **SERVICE DEPENDENT**. Offline the rebuild keeps local
  account names (**PC ADAPTATION**): Create (the original New Account prompt), Delete, Sign In / Sign Out; the signed-in
  name is the player name and signs in again at the next launch. `GetAccountNames` returns the comma-separated string
  the movie splits (**CONFIRMED** AS2).
- Shown by the player lists and the kill feed.

## 10. Architecture (modules)
```
Application (glue) - frontend boot, loadMatch (loadYield presentation, scene release), runMatch (HUD feed, scoreboard),
                     unloadMatch; FrontendSceneGL; display hooks; Systems audio adapter (FrontendAudioRuntime)
frontend::FrontendRuntime - GameFlow (levels, travel, lobby, UIController, LocalProfile, selection), DataStores,
                     Catalog (manifests + shipped config + providers), CharacterRoster, FrontendScene, HudController,
                     ScriptDriver, bridge (Game / Online / Customize / PCSettings / Account / Console / Stats / Sound),
                     popup (TnUIController message box), local accounts
ui::GfxPresenter    - flow movies, HUD, scoreboard, movie-opened overlays, message box, cursor, loading movie, video;
                     mouse / keys / typed text; navReport (harness)
gfx::Player / avm1  - SWF8 + AS2 runtime (mouse, variable text, input text fields, Flash 8 action order)
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
| `WFC_FRONTEND_SCRIPT` | as before, plus `ui:<Action>`, `mouse:x,y`, `click:x,y`, `clickclip:<clip path>`, `type:<text>` (%XX escapes), `vk:<code>`, `navcheck:<label>`, `showmenu`; `quit` leaves without the Exit Game box |
| `WFC_GFX_MISSINGFN=1`, `WFC_GFX_OPTRACE=<path>`, `WFC_GFX_CLASSLOG=1` | VM diagnostics: calls on undefined, an opcode trace of one timeline, class binding / attachMovie / removeMovieClip |
| `tools/frontend/nav_stress.sh` + `nav_check.js` | navigation stress harness (§17) |
| `tools/frontend/export_profile_defaults.py`, `export_character_presets.py` | regenerate `data/frontend/profile_defaults.json` / `character_presets.json` (authored.db read-only) |
| `wfc_gfxdump <movie>` | prints bridge calls with arguments; `WFC_DUMP_SHAPE=<id>` prints a shape's fills |
| `tools/frontend/export_frontend_scenes.py` | regenerates `data/frontend/scenes.json` (authored.db read-only) |

## 12. Handoffs
- **Rendering:**
  - preview pawns: wired by detection to `setFrontendSceneDraw` / `actorMatrix` / `loadContentMesh` (agents/rendering
    504730f). Per `PreviewRequest::Slot` (roster robot glTF, authored spawn point - Autobot PathNode_16191, Decepticon
    PathNode_7537 - linear colours) the scene adapter draws the body with `setDrawOwner(1 + slot)`. Verified in a merge
    preview (Soldier: Warpath and Brawl in original materials). Open: bind pose and ground snap (Gameplay), robot /
    vehicle form, and the per-chassis customization camera (`SeqVar_TnCustomizationCameraId`, Frontend's next item -
    CameraActor_2082 crops the pawns);
  - the UI render data belongs in the standard per-worktree setup (`build_render_data.ps1 -Map UI_FrontEnd` and
    `UI_CharacterCustomization`); the vignette ships still draw in bind pose;
  - texture quality; once Hud_GFX's crosshair is confirmed, disable the reconstruction reticle.
- **Gameplay:**
  - draw the selected character's body: `Match::selectCharacter` already carries the chassis per faction of the
    (edited) character - the remaining Optimus pawn should be replaced, or made an explicit, logged fallback;
  - the first spawn of the local player now drives the UI to InGame (OnRespawn); keep spawning only after the selection;
  - default PC keyboard bindings should match the shipped reference card (`Console.GetKeyDescription`): Shift Ability 1
    (Flip / Ram / Quick Turn / Roll), Ctrl Ability 2, MMB or Q Melee, Space Jump, B Look At / Kill Streak, E Interact /
    Pick Up / Revive, F Change Form, G Grenade / Detach Turret, R Reload, Tab Scoreboard, Esc Pause, RMB Fine Aim /
    Detonate / Speed Boost, LMB Fire, wheel / PgUp / PgDn Swap Weapons, C / V Hover Up / Down;
  - camera sensitivity / invert Y per form from `LocalProfile`; the damage type in kill events; XP / point events.
- **Gameplay reply (agents/gameplay 1a4e36b, Pass 21f, not merged here):** the selected chassis cannot be drawn until
  AssetTools exports ROBODEF / VEHDEF for the other 32 chassis; the Optimus fallback is now explicit (`MATCH spawn ...
  drawn=Optimus fallback=missing ROBODEF/VEHDEF export`, `HudGameState` selectedChassis / drawnChassis /
  chassisFallback); `PlayerController::setLookSettings(sensitivity, invertRobot, invertVehicle)` and
  `KillFeedEntry::damageType` exist. After the merge the frontend should pass `LocalProfile` CameraSensitivity /
  InvertY_* to setLookSettings and use damageType for kill-feed icons (integrator / next frontend pass).
- **Systems:** user volume categories (FX / Dialogue / Music) from `LocalProfile`.
- **AssetTools:** preset colours in the roster package (today exported from authored.db by Frontend); the ESC maps.
- **RE:** the Bink language-track selection; `SetWeaponCrosshair` native mapping; `ColorToHexColor` digit case.

## 13. Intro chain persistence
`SetHasWatchedIntroMovie` / `HasWatchedIntroMovie` and the controller-assignment tick (0x82726E18) write and read one
zero-initialised `.data` global (0x83757450); nothing saves or loads it (**CONFIRMED**, native decompile of the
RE-Workspace snapshot, read-only). Shipped WFC therefore plays the logos and FMV_intro on every launch; within a
session, returns to the frontend skip them. The rebuild had saved the flag in `wfc_profile.ini` (playtest: "the
cinematic did not replay") - it is now a session flag; an old `HasWatchedIntroMovie=` line is ignored.

## 14. Match entry and input ownership
- `UpdateUiController` creates the match UI controller with `UseInGameLobby = !PRI.HasSelectedCharacter()` - true for
  a new match PRI - and `WaitingOnGameStart.OnBeginGame` only goes InGame when `!_InGameLobby` (**CONFIRMED**). The
  rebuild ignored the flag: at match start Choose Character closed and the UI went InGame with no character.
- Now: Choose Character stays until a character is chosen; before the start the countdown follows; after the start only
  the "CharacterSelectedInGame" notification; the local player's first spawn sends OnRespawn (5) → InGame, and
  `WaitingOnGameStart.EndState` closes the screen.
- Every TnUIController state's EndState as scripted: screens close with their state; InGame / Spectating hide the HUD
  (hidden under the pause menu); Paused closes and unpauses.
- Gameplay receives input only in UI state InGame with the scoreboard closed (`Application`), so no menu shares input
  with player movement.

## 15. Return routing and confirmations (CONFIRMED script)
| From | Action | Original | Rebuild |
|---|---|---|---|
| Game lobby (before a match) | Back → "Quit Game? / Are you sure you want to leave the game?" → Yes | QuitGame(0) → QuitToPartyLobby | party lobby |
| Choose Character / in match | Pause → Quit → same box → Yes | QuitToPartyLobby (listen server) | party lobby |
| Match completed | EndGameStats → continue | the game lobby (PlayAgain) | game lobby |
| Party lobby | Back → box → Yes | QuitToFrontEnd (IsInPartyLobby) | front end |
| Main menu | Exit Game → "Warning / Do you want to exit the game?" → Continue / Cancel | ConsoleCommand quit | exit / stay |
- The box is the generic `TnUIController.ShowPopupUI` message box: `UI_GFxShared_p.MessagePrompt_GFX_1` with focus,
  `_global.DisplayMessage(Title, Message, Buttons, IconType)`, answered by `MessageBox.OnA..OnY`; Yes keeps it with the
  waiting icon and "Quitting..." until the travel. A popup whose movie cannot open is closed, never orphaned.

## 16. Text entry
The PC SKU enters names through `HmUtility.DisplayTextPrompt` → TextPrompt_GFX (loaded into the calling movie): an
input TextField focused with `Selection.setFocus('inputText_mc.label_txt')`; Enter / Escape via its Key listener; the
text goes to the callback (`_global.createUserAccount`, `_global.PCKeyboardTransmit` → `KeyboardDone` →
`CommitCharacter`). The runtime gained editable fields: WM_CHAR text, caret (blinking, per-character positions),
insert / Backspace / Delete / Left / Right / Home / End, `maxChars`, `onChanged`, two-way `variable`, click focus,
`Selection.setFocus` path strings (resolved against the calling timeline), `getCaretIndex` / `setSelection`.

## 17. Navigation stress harness
`tools/frontend/nav_stress.sh <run dir> [menu cycles] [match cycles]` drives a cold frontend through Settings
(Graphics), Accounts (the name prompt, typed), Extras (Concept Art viewer, Movies + a movie, Credits), Multiplayer /
Private Match, then private matches (Choose Character, InGame, Pause / Resume, Quit → party lobby → front end).
After every transition a `navcheck:` step logs the UI state, open movies and overlays, the focus target and its input
owner (`_global.currentMenu`, visible), modal state, AS heap, display nodes, GL shape cache and process memory.
`tools/frontend/nav_check.js` asserts: the script reaches its end (no soft-lock), at most one focused overlay, no modal
outside the quit / prompt checks, a visible input owner on every menu screen, the main menu fully restored after each
cycle, Choose Character owning the match start, nothing over gameplay in InGame, and bounded resources across cycles.

## 18. Presentation contracts (pass 5)
- **GL state.** The UI pass (`GfxRendererGL::begin .. end`) restores every GL state it changes (enables, masks, blend,
  viewport, clear values, program, VAO, array buffer, textures, framebuffers). Without it the in-match HUD left depth
  testing off and the frontend-launched world lost its architecture (first bad b1fce97, fixed a96f841).
  `WFC_GFX_NO_GLRESTORE=1` reproduces it. Frame order: world → frontend scene → UI pass → present.
- **Movies.** Full-screen movies (startup, logos, FMV, Extras) letterbox over black; the 3D scene is not drawn under
  them. The startup movie stays up until the logo chain starts while the title scene loads behind it (load yields).
  A new movie starts at frame 0.
- **HUD.** Hud_GFX runs with `Stage.scaleMode noScale` (1:1 pixels, viewport-sized Stage, onResize) and its native
  `HmObjectInterpolator.addInterp` / `HmActionScript.setColor` (menus' AS interpolator semantics).
  `WFC_GFX_IGNORE_NOSCALE=1` restores the old stage mapping for comparison.
- **Script steps** `display:<w>,<h>,<0|1>` (runtime display mode change); `key:<code>` types into a focused input field.
