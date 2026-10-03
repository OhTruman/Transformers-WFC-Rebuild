# WFC frontend lane: boot → menus → lobbies → match → return

Branch `agents/frontend`. Specification: the shipped Xbox 360 game. Behaviour sources:
- RE `notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md` (RE-Workspace f1ca8a5), cited below as **RE n.n**;
- RE `work/script/decomp/TransGame.TnOnlineActionScriptBinding.txt` (binding bodies);
- AssetTools `docs/FRONTEND.md` and `manifests/frontend_*.json` (AssetTools cc9773e).

## 1. Architecture

```
main ─ Application ──┬─ (legacy) direct boot ─────────────── runMatch()            [unchanged automation path]
                     └─ frontend boot ─ runFrontend() ─┬─ FrontendRuntime.update/draw (frontend levels, loading)
                                                       ├─ loadMatch(MatchLaunch)  -> World.setMapName/setMatchMode/load
                                                       ├─ runMatch() + FrontendRuntime.updateInMatch (pause, UI events)
                                                       └─ unloadMatch()           <- travel away from the match
FrontendRuntime ─┬─ Catalog        maps / playlists / settings classes / localization  (AssetTools manifests, at runtime)
                 ├─ GameFlow       levels + travel URLs + loading + lobby GRI + Online/Game bridge
                 │    └─ UIController  TnUIController state machine (OnUIEvent codes, OpenUI movies)
                 ├─ IMoviePresenter  draws movies, runs their ActionScript   (next step: src/ui/gfx)
                 ├─ movie player     SeqAct_MoviePlayer Bink movies          (stub: no decoder yet)
                 ├─ ScriptDriver     automation: issues the original bridge calls
                 └─ native shims     AS logic not executed yet, each logged with provenance
```

Source files: `src/frontend/{Url,Catalog,UIController,GameFlow,FlowTrace,FrontendRuntime}.*`,
`src/core/Application_Frontend.cpp`, `src/ui/`, `tests/FrontendTests.cpp`.

### The boundary
- **Movies talk to the flow only through the original surface.** That surface is ExternalInterface bridge calls
  (`GameFlow::call("Online.OpenPartyLobby", {"GTS_TeamGame"})`) and fscommands (`GameFlow::fsCommand`). The names are
  the shipped ones, so the AS2 runtime, the automation driver and the native shims are interchangeable callers.
- **Selected mode / map are lobby state, not UI state.**
  - `EditGameMode` / `PlayPrivateGame` set the SettingsDataStore current settings and `PublishGameInfo`.
  - `SetSelectedMapID` → `HostRequestMapID` → `SetMapId`.
  - The lobby turns them into the match URL exactly as `TnGameLobbyGame.StartLevel` does.
- **Match launch request = the travel URL.** `MatchLaunch` carries the parsed URL plus what the match reads back
  from it: `PointsToWin`, `Rounds`, `TimeLimit`, `GameModeTag`, `GameTeamStatus`, `MapId`, and the lobby's team pick.
  The application maps it onto the existing World API (`setMapName`, `setMatchMode`, `load`). No mode logic lives in
  the UI.
- **Gameplay state → UI** goes through `GameFlow::onUIEvent(code)`, the `TnUIController.OnUIEvent` codes (RE 1.4).
- **Return:** `Game.QuitToMainMenu` → `ClientTravelToMap("UI_FrontEnd_m")`. The application releases the match world
  (`wantsWorldUnload`) and the flow re-enters the frontend level.

### Minimal changes outside the lane
- **World** (Gameplay): `setMapName(dir)` / `mapName()`. The 8 hard-coded `Maps/MP_IAC_Streets` paths now use it;
  the default is unchanged.
- **Application** (shared):
  - frontend boot selection;
  - `run()` split into `run()` → `runMatch()`;
  - in-match hooks: Escape = `|onrelease showmenu`, flow tick, return exit, and input withheld while a movie has
    focus;
  - an overlay draw call.
  
  The legacy path is byte-for-byte the old behaviour.
- **CMake:** globs `src/frontend`, `src/ui`; adds the `wfc_frontend_tests` target (ctest `frontend`).

## 2. Flow as implemented (provenance)

| Step | Rebuild behaviour | Provenance |
|---|---|---|
| Boot map | `[URL] Map=UI_FrontEnd_m`; initial loading movie `TF_InitialStartup` | CONFIRMED config |
| TnFrontEndGame | WaitingForController → controller assigned → None. Login / online states bypassed | CONFIRMED script; bypass = offline reduction |
| Kismet | GameplayStarted → Black-Out interp (0.00105 s) → OpenMovie `MovieLoader_GFX_1` | CONFIRMED authored |
| MovieLoader | `Game.HasWatchedIntroMovie` → `enterMovieSequence` / `enterFrontEnd` | names CONFIRMED; branch HIGH (native shim until AS2 runs) |
| Intro chain | Logo_Activision → Logo_Hasbro → Logo_HighMoon → FMV_intro, each on the previous Stopped | CONFIRMED authored; **playback PARTIAL: no decoder, each stops at once** |
| HasWatchedIntroMovie | set when the controller is assigned; persisted to `wfc_profile.ini`; read at the next boot | CONFIRMED call; persistence medium rebuild-only |
| FRONTEND START | SetLoadingMovieFilename `UI_GFxLoading_p.LoadScreen_GFX`; `TnSeqAct_OpenFrontEnd` → OnUIEvent(0) → FrontEnd → OpenUI `FrontEnd_GFX_1` | CONFIRMED |
| Frontend scene | orbit cameras, fireworks, music, reverb, energon materials: logged only | UNKNOWN to the rebuild (UI_FrontEnd_m not exported) |
| Multiplayer button | `Online.OpenPartyLobby("GTS_TeamGame")` → StringToGameTeamStatus (FFA 1, Single 2, Team 3, Campaign 4) → ConfirmOpenPartyLobby → `UI_PartyLobby_m?game=TransContent.TnPartyLobbyGame?listen` | CONFIRMED script. CheckOnlineWarningPart0–2 prompts: PARTIAL (not shown) |
| Party lobby | Ready (session steps bypassed); TnUIControllerPartyLobby InLobby → `PartyLobby_GFX_1` | CONFIRMED; bypass |
| Mode | `Online.EditGameMode(name)` → PublishGameInfo; `Online.PlayPrivateGame(name)` → HostOnlineGame → BuildLobbyURL + `?listen` | CONFIRMED script. **Name → settings-class mapping PARTIAL** (GameSettingsCfgList not in the manifests) |
| Public playlist | `Online.PlayPlaylist(id)` hosts the playlist game: the lobby waits for 4 players, intermission 45 s | PARTIAL (search bypassed) |
| Game lobby | TnGameLobbyGameTeam: private → host's choice, LobbyStatus 3; `OnEnterLobbyFromMap(-1)` → SelectRandomMap over compatible, enabled maps | CONFIRMED script |
| Map | `Online.SetSelectedMapID(id)` (ignored unless host's choice) → SetMapId → prestream | CONFIRMED |
| Start | `Online.BeginLobbyExitCountdown` → HostRequestsGameStart → short countdown 10 s (1 s ticks) → FinalCountdown → PickTeam → StartLevel | CONFIRMED; PickTeam with no reservation = RandomInt(2): HIGH |
| Match URL | `MP_IAC_Streets_Base_m?PlaylistId=-1?GamerRegion=0?PointsToWin=40?Game=TransContent.TnVersusGame?GameModeTag=TDM?GameTeamStatus=3?GameRules=?MaxPlayers=10?StatsWriters=?LobbyGameClassName=TransContent.TnGameLobbyGameTeam?IconicMode=0?TimeLimit=900.00?listen?MapId=508` | equals RE 3.1; native-appended part HIGH; AppendContextsToURL omitted (PARTIAL) |
| Loading | StartLoadingMovie: party/game lobby = empty text + 3 base tips; map = SetLevelText("Team Deathmatch", "in Streets") + 3 × EngageText | CONFIRMED script |
| Match load | map dir from MapInfo (`MP_IAC_Streets_Base_m` → `Maps/MP_IAC_Streets`); mode → `World::setMatchMode`; blocking load under the loading screen | dir mapping HIGH; blocking load rebuild-only |
| Match UI | TnUIControllerMultiplayer WaitingOnGameStart → (GRI) InGameLobbyUI `CustomTransformers_GFX_1` → OnCharacterSelected → `PreGameCountdown_GFX_1` → OnUIEvent(3) → InGame, HUD visible | state machine CONFIRMED. **The event timing is a PROVISIONAL adapter**: no Gameplay PendingMatch or character select yet |
| Pause | Escape / Start release → ShowMenu → OnUIEvent(6) → Paused, `PauseMenu_GFX_1`; world keeps running; input goes to the movie | CONFIRMED binding and state; bPauseable false HIGH |
| Quit | `Game.QuitToMainMenu` → OnUIEvent(11) → travel `UI_FrontEnd_m`; MovieLoader now takes `enterFrontEnd` | CONFIRMED |

### Authored-data notes
- **TDM loading tips.** The shipped `TransGame.int [TnOnlineGameSettingsTDM]` authors all 26 `EngageText` entries as the
  same string ("Don't get fooled. Decoy Traps can be destroyed by shooting them."). The rebuild follows the data, so a TDM
  map load shows that tip three times. Whether the original showed it this way is UNKNOWN; this needs a capture.
- **Map selection today.** Only Streets has rebuild runtime data, so it is the only enabled provider for every versus
  mode. The other 12 providers are listed and disabled: `IsProviderDisabled = !HasRequiredAssets`. The rebuild's
  equivalent of HasRequiredAssets is "cooked, and `Maps/<dir>/world.glb` exists".

## 3. Running

| Command / variable | Effect |
|---|---|
| `build\bin\wfc_rebuild.exe` | frontend boot (default) |
| `WFC_BOOT=match` | legacy direct boot. It is also implied by any existing automation variable: `WFC_SMOKE_FRAMES`, `WFC_SHOTLIST`, `WFC_GAMEMODE`, `WFC_MAP`, `WFC_PICKUPTEST`, `WFC_TRAVERSE`, `WFC_MAPTRAVERSE`, `WFC_LOCKSTEP`, `WFC_DEBUGCAM`, `WFC_STARTVEHICLE` |
| `WFC_BOOT=frontend` | force frontend boot even with automation variables |
| `WFC_MAP=<dir>` | direct boot: runtime map directory |
| `WFC_FRONTEND_AUTOPLAY=TDM,508` | scripted canonical path (mode tag, MapId) |
| `WFC_FRONTEND_SCRIPT="step;step"` | scripted steps (grammar in `FrontendRuntime.cpp`): `wait:level=…`, `wait:ui=…`, `wait:frontend`, `wait:movie=…`, `wait:t=…`, `call:Iface.Fn,args`, `fscommand:movie,cmd`, `showmenu`, `uievent:n`, `snapshot:why`, `quit` |
| `WFC_SKIPINTRO=1` | treat HasWatchedIntroMovie as true |
| `WFC_FLOWSEED=n` | deterministic RandRange / RandomInt / tip picks |
| `WFC_FLOW_TIMEOUT=s` | stop the frontend loop after s seconds (automation guard) |
| `WFC_FLOWLOG=path` | JSON-lines flow trace |
| `WFC_FRONTEND_MANIFESTS=dir` | AssetTools manifest directory (default `F:/Transformers Rebuild/AssetTools/manifests`) |

`WFC_SMOKE_FRAMES` keeps its meaning in the match: N match frames, then exit.

## 4. Validation hooks (for Experimental)
- `build\bin\wfc_frontend_tests.exe` (ctest `frontend`): 59 headless checks.
  - Covers catalog, URL, the TnUIController transition table, boot → TDM/Streets → URL equality with RE 3.1 → pause →
    quit → frontend, and catalog extensibility (a synthetic `MP_IAC_Rust` runtime directory makes Rust selectable).
  - Exit code = failures.
- `WFC_FLOWLOG` events. Every line carries `t` and `seq`.

| event | fields |
|---|---|
| `boot` | `map`, `watchedIntro`, `seed` |
| `travel` | `url`, `server`, `from` |
| `loading.start` | `kind`, `title`, `message`, `bink`, `gfx`, `tips` |
| `loading.engageText` | `text` |
| `loading.close` | `kind`, `level` |
| `level.begin` | `level` (FrontEnd / PartyLobby / GameLobby / Match), `map`, `url` |
| `ui.state` | `from`, `to`, `controller`, `level` |
| `ui.open` / `ui.close` | `movie` |
| `ui.hud` | `visible` |
| `gfx.open` / `gfx.close` | `movie` |
| `fscommand` | `movie`, `cmd`, `arg` |
| `bridge` | `fn`, `args`, `level` |
| `bridge.unhandled` | `fn` |
| `movie.play` / `movie.stopped` / `movie.unavailable` | `movie` |
| `lobby.publishGameInfo` | `tag`, `settings` |
| `gamelobby.game` | `tag`, `settings`, `private`, `mapSelectionMethod` |
| `gamelobby.map` | `mapId`, `map`, `name`, `hasRequiredAssets` |
| `gamelobby.countdown` / `gamelobby.countdownTick` | `value` |
| `gamelobby.finalCountdown` | `team` |
| `gamelobby.startLevel` | `url` |
| `match.launch` | `url`, `runtimeDir`, `mode`, `goalScore`, `timeLimit`, `team` |
| `match.loaded` | `seconds` |
| `match.quit` / `match.unloaded` | — |
| `shim` / `adapter` | `what`, `provenance` |
| `snapshot` | full state |
| `exit` / `timeout` | — |

- The window title shows `level / controller / state / movie / loading / mode / map / countdown` in frontend levels.

## 5. Handoffs (required from other lanes)
- **Gameplay:**
  - TnMultiplayerGame PendingMatch (MatchAutoStartCountdown 10 s) → StartMatch → InProgress, which calls
    `GameFlow::onUIEvent(3)`; MatchOver calls `(9)`; spectating / respawn call `(4)` / `(5)`.
  - Read `GoalScore` / `TimeLimit` / team from `MatchLaunch` (RE 5.1).
  - Replace the PROVISIONAL adapter in `Application::runFrontend`.
  - Expose GRI values for the `<CurrentGame:*>` data store: CurrentCountdown, team scores.
- **Rendering:**
  - An `IRenderer` map-unload / resource release for level travel. Today the renderer is recreated on return and the
    previous map's GL objects are leaked.
  - The UI_FrontEnd_m scene (orbit camera, energon rings), once AssetTools exports it.
- **AssetTools:**
  - Add LobbyGameClass, TeamType, the PointsToWin / Rounds default and NumPrivateConnections per settings class to
    `frontend_modes.json`. These now come from RE 4 tables in `Catalog.cpp`.
  - Add the `TnDataStore_GameSettings` GameSettingsCfgList names (the EditGameMode argument → class mapping).
  - Export UI_FrontEnd_m (frontend scene) as a map.
- **RE:**
  - MovieLoader branch body (HIGH now).
  - The PartyLobby_GFX custom-match AS path: the exact strings passed to EditGameMode / PlayPrivateGame.
  - The Bink + GFx loading composite and its close timing (PARTIAL).
- **Systems:** frontend music / ambience / UI sounds (RE handoff table: FRONTEND_MX_ORBIT_01, FRONTEND_AMB_ORBIT_*,
  MP_PARTY_LOBBY_MX, MP_LOBBY_MX).
