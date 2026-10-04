# WFC frontend lane: boot → original menus → lobbies → loading → match → return

Branch `agents/frontend`. The shipped Xbox 360 game is the specification. Behaviour sources:
- RE `notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md` (RE-Workspace f1ca8a5), cited as **RE n.n**;
- RE `work/script/decomp/TransGame.TnOnlineActionScriptBinding.txt` (binding bodies);
- RE `work/q/gfx/*.as.txt` (readable AS2 dumps, used for understanding; the runtime executes the movies' own bytecode);
- AssetTools `docs/FRONTEND.md`, `manifests/frontend_*.json` (cc9773e), `manifests/authored.db` (read-only);
- Systems `game::FrontendAudioRuntime` / World level-audio contract (agents/systems de19ced).

## 1. What runs

**The shipped Scaleform movies run unmodified.** The rebuild contains a SWF 8 / AS2 runtime (`src/ui/gfx`), and the
menus run their own ActionScript. Nothing in the menu logic is re-implemented natively.

| Stage | Movie (original) | What the player sees / does |
|---|---|---|
| Boot | `MovieLoader_GFX` | Its AS calls `Game.HasWatchedIntroMovie` and sends `enterMovieSequence` or `enterFrontEnd` |
| Intro | Bink `Logo_Activision` / `Logo_Hasbro` / `Logo_HighMoon`, `FMV_intro` | the shipped movies (AssetTools .mkv) full screen, over the GFx layer; Stopped fires at the end; A / Start / B skips (PROVISIONAL); no movie audio (PARTIAL) |
| Title | `FrontEnd_GFX` | localized TRANSFORMERS WFC logo (ExternalTextures), "Press START button" |
| Main menu | `FrontEnd_GFX` `mc_menuMain360` | Campaign / Multiplayer / Escalation / Settings / Extras; footer hints with gamepad icons |
| Multiplayer | `PartyLobby_GFX` | Find Match / Private Match / Create a Character / Teletran I / Matchmaking / Friends List, MOTD ticker, team panel |
| Private Match | `mc_menuMultiplayerGameModes` | the 6 versus modes from `<TnMenuItems:GameModes>` |
| Host Options | `mc_menuMultiplayerGameModeSettings` | Team Balancing / Map Selection / Time Limit / Points to Win (authored values) → Create Game |
| Game lobby | `GameLobby_GFX` | mode title + rules ("Kill 40 enemies…"), map thumbnail + name, Start Game / Select Map, "Waiting for host to start game", "MATCH STARTS IN n" |
| Loading | `LoadScreen_GFX` over the `TF_LoadingScreen` Bink (boot: `TF_InitialStartup`) | looped Bink underlay, "TEAM DEATHMATCH"; "in Streets" + 3 engage tips are set but animate in during the blocking load |
| Match | Streets runtime | the Gameplay / Rendering / Systems match |
| Pause | `PauseMenu_GFX` | "TEAM DEATHMATCH ON STREETS", Resume / Choose Character / Settings / Friends List / Quit Game |
| Return | `Game.QuitToMainMenu` | travel to `UI_FrontEnd_m`; the frontend comes back (intro skipped: watched) |

Every step is reachable with the original controls only:
- Keyboard: arrows, Enter = A, Esc = B, F1 = X, F2 = Y, F3 = Start, F4 = Back, PgUp/PgDn/Home/End = LB/RB/LT/RT.
- Gamepad: XInput buttons mapped onto the same key codes.

## 2. Architecture

```
Application ─ frontend boot ─ runFrontend(): FrontendRuntime.update/draw  ─┐ loadMatch(MatchLaunch) ─ runMatch() ─ unloadMatch()
                                                                            │   World.setMapName / setMatchMode / load
FrontendRuntime ─┬─ Catalog      maps / playlists / settings / localization (AssetTools manifests + authored settings defaults)
                 ├─ GameFlow     levels, travel URLs, loading, lobby GRI, Online/Game bindings, settings data store values
                 │   └─ UIController   TnUIController states (OnUIEvent codes, OpenUI movies)
                 ├─ DataStores   <CurrentGame:*>, <TnMenuItems:*>, <TnGameSettings:*>, <PlayerOwner:*> (+ change callbacks)
                 ├─ bridge()     ExternalInterface.call router (Game / Online -> GameFlow, DataStores, Sound -> audio seam)
                 ├─ IFrontendAudio  -> Systems game::FrontendAudioRuntime (compiled in when integrated)
                 ├─ IMoviePresenter -> ui::GfxPresenter (runs + draws every movie the flow has open)
                 └─ ScriptDriver automation (same bridge calls / key presses as a player)
ui::GfxPresenter ─ GfxHost (resource mapping) ─ gfx::Player (display list + AVM1 VM) ─ GfxRendererGL (stencil fills, MSAA)
```

| Module | Content |
|---|---|
| `src/ui/gfx/MovieDef` | SWF 8 / GFx tag parser: shapes 1–4, morph shapes, DefineFont2/3, edit / static text, sprites, frame labels, PlaceObject2/3 (clip events, filters skipped), exports / imports, DoAction / DoInitAction, GFx DefineExternalImage |
| `src/ui/gfx/Avm1*` | AS2 VM: SWF4–7 actions, DefineFunction2 registers / preloads, closures, with / try / throw, super, prototypes, getters / setters (addProperty), watch, ASSetPropFlags, mark-sweep GC |
| `src/ui/gfx/Avm1*` built-ins | Object, Function, Array, String, Number, Boolean, Math, Date, Error, Key, Stage, Mouse, Selection, System, MovieClipLoader, TextFormat, Color, Sound, setInterval / setTimeout, flash.geom.*, flash.filters.* (stored), flash.external.ExternalInterface, flash.display.BitmapData.loadBitmap |
| `src/ui/gfx/Display`, `Text` | timelines (Flash 8 order), attach / duplicate / remove / swapDepths / loadMovie, masks (clipDepth + setMask), HTML text subset layout with the embedded DefineFont3 outlines; GFx extensions (setImageSubstitutions, shadow*, verticalAlign, textAutoSize) |
| `src/ui/GfxHost` | movie objects → .gfx; `../_Shared/*.swf` imports → cooked movies; gfxfontlib → `Fonts_EFIGS` + `GFxUI.int [Fonts]` aliases; ExternalTextures; `$File.Section.Key` translation |
| `src/ui/gl/GfxRendererGL` | stencil-winding fills (nonzero), nested masks in the stencil high nibble, gradients (linear / radial / focal), bitmap fills, strokes, glyph outlines, inline images; 8× MSAA target composited premultiplied (also over the match for pause / end menus) |
| `src/ui/gl/GlCensus` | releases the GL objects a match created when travelling away (stopgap; see handoffs) |

### The frontend / runtime boundary
- **Movies talk to the engine only through the original surface.**
  - `ExternalInterface.call("Online.OpenPartyLobby", …)` and the other bridge calls.
  - Data stores (`DataStores.ReadValue('<CurrentGame:SelectedMapID>')`, collections, change callbacks).
  - fscommands.
  - The automation driver uses the same calls and real key presses.
- **Selections are lobby state.**
  - Mode: `EditGameMode` / `PlayPrivateGame` select the settings object.
  - Host options: `WriteValue('<TnGameSettings:TimeLimit>', '15 minutes')` and the other fields.
  - Map: `SetSelectedMapID` → `HostRequestMapID`.
  - `StartLevel` builds the match URL from them.
- **The match launch request is the travel URL.** `MatchLaunch` carries the parsed URL plus what the match reads back:
  `PointsToWin` / `Rounds`, `TimeLimit`, `GameModeTag`, `GameTeamStatus`, `MapId`, and the lobby team. The application
  maps it onto the existing World API: `setMapName` (catalog runtime directory), `setMatchMode`, `load`.
- **Gameplay state → UI** goes through `GameFlow::onUIEvent(code)`.
- **Audio:** the frontend reports level starts / changes / triggers. Systems decides what is authored and plays it.
- **No Streets assumption in the path.**
  - Maps are the 13 `TnDataProvider_MapInfo` providers.
  - "Selectable" = cooked + rebuild runtime data on disk. AssetTools' `MP_UND_Gorge` export appeared automatically.

### Changes outside the lane (minimal)
- World: `setMapName(dir)`, used by the 8 former `Maps/MP_IAC_Streets` paths.
- Application:
  - frontend boot; `run()` → `runMatch()`;
  - in-match hooks: Esc = `|onrelease showmenu`, flow tick, return, input to the focused movie, overlay draw;
  - `shutdownFrontend()` at the start of `shutdown()`.
- Platform: `UiKey` bitmask in `InputFrame` (keyboard + XInput buttons); `platform::IMoviePlayer`
  (`Win32Movie.cpp`, Media Foundation; links mfplat / mfreadwrite / mfuuid / ole32).
- CMake: globs `src/frontend`, `src/ui`; `WFC_SOURCE_DIR`; targets `wfc_frontend_tests` (ctest `frontend`) and
  `wfc_gfxdump`.

## 3. Provenance

| Item | Rebuild | Provenance |
|---|---|---|
| Boot map, Kismet order, MovieLoader branch, intro chain order | as authored | CONFIRMED (the branch now runs the movie's own AS) |
| HasWatchedIntroMovie | set at boot (TnFrontEndGame) and by FrontEnd_GFX; persisted in `wfc_profile.ini` | CONFIRMED calls; storage medium rebuild-only |
| TnUIController states, OnUIEvent, subclass movies | as RE 1.4 | CONFIRMED |
| Menus, lobbies, loading, pause | the shipped movies run their own AS2 | CONFIRMED content and behaviour |
| AS2 / GFx runtime semantics | Flash 8 player rules; GFx extensions used by the movies | HIGH: clean-room runtime. Where Flash semantics were ambiguous: plain-call `this` = calling timeline, function `_parent` = defining timeline, `var x;` keeps values, unnamed clips `instanceN` |
| Key mapping | KeyListener codes 13 / 27 / 112–117 / 33–36 / arrows | codes CONFIRMED (AS); pad → code mapping HIGH |
| Platform | `$version` "XBOX360 …" selects the 360 menus | CONFIRMED (AS); the version digits are UNKNOWN |
| Region / language | INT / NA (TM logo) | PARTIAL: the region of the dump is UNKNOWN |
| Bridge calls Online.* / Game.* | RE bodies; session / reservation steps bypassed | CONFIRMED; bypass = offline reduction |
| Settings classes, host options, defaults, LobbyGameClass, TeamType, NumRequiredPlayers | `data/frontend/game_settings.json`, exported from authored.db | CONFIRMED authored |
| GetDataStoreFields order | mapping order (contexts, then properties) | PARTIAL |
| Settings values persist per class for the session | as the data store keeps its settings objects | HIGH |
| Lobby | host's choice, short countdown 10 s, PickTeam RandomInt(2), StartLevel URL | CONFIRMED / HIGH (RE 2.4) |
| Match URL | equals RE 3.1, built from the chosen host options | CONFIRMED; native append HIGH; contexts omitted (PARTIAL) |
| Loading text | SetLevelText(FriendlyName, "in <map>") + 3 tips | CONFIRMED |
| Loading timing | the movie's spinIn intro (34 frames @30) plays, then the blocking world load | PARTIAL: no threaded load (the screen freezes on its last frame) |
| Full-screen movies | Media Foundation decode of the AssetTools .mkv (H.264); SeqAct_MoviePlayer movies over the GFx, the loading Bink looped under LoadScreen_GFX; files `<name>_NA_INT` → `<name>_INT` → `<name>` | video CONFIRMED content; layering HIGH; skip rule PROVISIONAL; region variant PARTIAL; audio not played (PARTIAL: 10 mono tracks, layout unidentified) |
| Player list | one local PRI; name "Player" (`WFC_PLAYERNAME`), level 1 everywhere | PARTIAL: no profile / XP service |
| Provider column headers (`GetCollectionColumnHeaderByTag`) | empty | UNKNOWN: in no shipped .int |
| Match start UI | WaitingOnGameStart → CustomTransformers → PreGameCountdown → InGame, immediately | state machine CONFIRMED; timing PROVISIONAL adapter (Gameplay has no PendingMatch) |
| Pause | Esc / Start release → OnUIEvent(6) → PauseMenu_GFX; the world keeps running | CONFIRMED / HIGH |
| Frontend scene behind the menus | black | UNKNOWN to the rebuild: UI_FrontEnd_m is not exported |
| GFx filters, blend modes | parsed / stored, not drawn | PARTIAL (e.g. the pause backdrop draws without its blend) |
| Audio | Systems FrontendAudioRuntime: UI cues, level music / beds, CINE mute, prefetch; level change stops music | Systems provenance; verified in a merge preview |

### Authored-data notes
- **TDM loading tips.** `TransGame.int [TnOnlineGameSettingsTDM]` (and the TDM class default in authored.db) authors
  the same tip 26 times. The rebuild shows it. What players saw is UNKNOWN; this needs a capture.
- **Map providers.** 13 providers, of which 10 are cooked; the 3 DLC providers are disabled. Selectable = runtime data
  exists:
  - Streets;
  - Gorge, since AssetTools exported it. Gameplay with Gorge has not been validated by this lane.

## 4. Running

| Command / variable | Effect |
|---|---|
| `build\bin\wfc_rebuild.exe` | frontend boot (default) |
| `WFC_BOOT=match` | legacy direct boot. It is also implied by the existing automation variables (`WFC_SMOKE_FRAMES`, `WFC_SHOTLIST`, `WFC_GAMEMODE`, `WFC_MAP`, `WFC_PICKUPTEST`, `WFC_TRAVERSE`, `WFC_MAPTRAVERSE`, `WFC_LOCKSTEP`, `WFC_DEBUGCAM`, `WFC_STARTVEHICLE`) |
| `WFC_BOOT=frontend` | force frontend boot |
| `WFC_FRONTEND_NOGFX=1` | flow only: no movies; native MovieLoader shim |
| `WFC_FRONTEND_AUTOPLAY=TDM,508` | scripted canonical path through the bridge calls |
| `WFC_FRONTEND_SCRIPT="…"` | steps: `wait:level=` / `wait:ui=` / `wait:frontend` / `wait:loading=0\|1` / `wait:movie=` / `wait:t=`; `call:Iface.Fn,args`; `fscommand:movie,cmd`; `key:<flash code>`; `shot:<file.bmp>`; `dump:<movie substring>`; `showmenu`; `uievent:n`; `snapshot:why`; `quit` |
| `WFC_SKIPINTRO=1` | treat the intro as watched |
| `WFC_FLOWSEED=n` | deterministic random choices |
| `WFC_FLOW_TIMEOUT=s` | frontend loop guard |
| `WFC_FLOWLOG=path` | JSON-lines trace |
| `WFC_PLAYERNAME=` | the local player name |
| `WFC_NO_GL_RELEASE=1` | disable the GL census release (A/B) |
| `WFC_NO_VIDEO=1` | no movie decoding (each movie reports Stopped at once, as in headless tests) |
| `WFC_FRONTEND_MANIFESTS=dir` | AssetTools manifest directory |
| `build\bin\wfc_gfxdump.exe <movie object> [frames] [frame:keycode,…]` | runs one movie headless and prints its display tree. `WFC_DUMP_PRESCRIPT="…"` drives the flow first |
| `tools/frontend/export_game_settings.py` | regenerates `data/frontend/game_settings.json` from authored.db (read-only) |

## 5. Validation hooks (Experimental)
- **`wfc_frontend_tests`** (ctest `frontend`): 59 headless checks.
  - catalog, URL, UIController table;
  - boot → TDM / Streets → URL equals RE 3.1 → pause → quit → frontend;
  - catalog extensibility.
- **Trace events** (`WFC_FLOWLOG`, one JSON object per line with `t` / `seq` / `ev`):
  - flow: `boot`, `travel`, `level.begin`, `loading.start` / `engageText` / `close`, `ui.state`, `ui.open` / `ui.close`,
    `ui.hud`, `bridge`, `bridge.unhandled`, `fscommand`, `movie.*`;
  - lobby / settings: `lobby.publishGameInfo`, `settings.write`, `gamelobby.*`;
  - match: `match.launch`, `match.loaded` (seconds, privateMB), `match.glRelease`, `match.unloaded` (privateMB),
    `match.quit`;
  - full-screen video: `movie.play` / `open` / `firstFrame` / `finished` (position, skipped) / `unavailable`;
  - movies: `gfx.movie` / `gfx.movieClosed` / `gfx.loadingMovie` / `gfx.key` / `gfx.dsCallback` / `gfx.externalTexture`;
  - audio: `ui.sound`, `audio.uiLevel` / `levelChange` / `levelEvent` / `moviePlaying` / `prefetch`;
  - data stores: `datastore.write` / `unhandled` / `unknownHeader`;
  - `snapshot`, `exit`, `timeout`.
- **Soak:** 4 full cycles (frontend → party lobby → private TDM → game lobby → Streets → Pause → Quit → frontend).
  Private memory is flat: ~3.0 GB loaded, ~2.5 GB after unload (it grew 1.6 GB per cycle before the GL release). The
  frontend renders correctly after the 4th return.

## 6. Handoffs
- **Gameplay:**
  - TnMultiplayerGame PendingMatch (10 s) → InProgress sends `onUIEvent(3)`; MatchOver sends `(9)`;
    spectate / respawn send `(4)` / `(5)`;
  - read `GoalScore` / `TimeLimit` / team from `MatchLaunch`;
  - provide `<CurrentGame:*>` match values (CurrentCountdown, Teams scores, Players kills / deaths) for the HUD,
    PreGameCountdown and EndGameStats movies;
  - replace the PROVISIONAL adapter in `Application::runFrontend`.
- **Rendering:**
  - `IRenderer` resource release / map unload, which replaces `GlCensus`;
  - the UI_FrontEnd_m scene (orbit camera, energon rings, nebula) once exported;
  - the HUD movie (Hud_GFX): the GFx runtime here can run it. Decide ownership with Rendering, which owns the reticle
    today.
- **AssetTools:**
  - carry the settings-class fields (`LocalizedSettings*`, `PropertyMappings`, LobbyGameClass, TeamType,
    NumRequiredPlayers) in a manifest; the rebuild then reads it instead of `data/frontend/game_settings.json`;
  - export UI_FrontEnd_m;
  - the provider column header strings, if they exist anywhere in the dump;
  - the GameSettingsCfgList names.
- **RE:**
  - native GFx pad → key mapping;
  - Bink + GFx loading composite and close timing;
  - `$version` string;
  - region of the dumped build;
  - GFx blend / filter behaviour on the pause backdrop.
- **Systems:** none blocking. The seam is consumed exactly as documented. Merge-preview resolutions:
  `Application.cpp` keeps both `WFC_RAMSELF` and `WFC_PRESSTRANSFORM_EVERY`; `World.cpp` `setAudio` takes Systems'
  version with `loadMapAudio(mapName_)`.
- **Video audio (Systems / AssetTools / RE):** the movies carry 10 mono FLAC tracks of unidentified layout
  (AssetTools open item; tracks 0/1 are the loudest pair). `[Engine.MovieSettings] MoviesToAlwaysPlaySound` lists the
  three logos. Playing them belongs on the Systems device (no second sound engine); the platform decoder can expose PCM
  once the layout is known.
- **Platform:** `platform::IMoviePlayer` has a Win32 Media Foundation implementation only.
