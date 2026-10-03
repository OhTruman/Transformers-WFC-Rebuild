# Milestone 05 end-to-end gate

```
.\tools\fidelity\m05-e2e-gate.ps1 -Root <merged tree or ab.ps1 export> [-OutDir <dir>] [-Cycles 3] [-MatchSeconds 6]
```

The gate needs a tree with `build-release\bin\wfc_rebuild.exe` (Release), and optionally
`build\bin\wfc_frontend_tests.exe` and `work\render`. Build one without touching any branch with:

```
.\tools\fidelity\ab.ps1 -Ref <commit> -Name <n> -Exe
```

then run `tools\render\build_render_data.ps1` inside the export. Output goes to `M05-GATE.md`, `report.json`, the
`run1_first_launch\`, `run2_second_launch\` and `run3_loops\` folders, and `sheet_first_launch.png`.

## What it runs (product hooks only, no product changes)
| run | env | covers |
|---|---|---|
| unit | `wfc_frontend_tests.exe` (exit = failures) | catalog / flow / UI unit layer of the frontend lane |
| 1 first launch | fresh folder (no `wfc_profile.ini`), `WFC_BOOT=frontend`, `WFC_FRONTEND_SCRIPT` = main menu → `Online.OpenPartyLobby GTS_TeamGame` → `EditGameMode/PlayPrivateGame TDM` → `SetSelectedMapID 508` → `BeginLobbyExitCountdown` → match → `showmenu` → `Game.QuitToMainMenu` → main menu; `WFC_FLOWLOG`, `WFC_AMBLOG`, `WFC_MUSICLOG`, `WFC_SMOKE_FRAMES` (huge, never reached: turns on the per-frame pawn log) | boot, intro chain, every screen, URLs, selection, countdown, team, loading text, launch parameters, pause, quit, return |
| 2 second launch | run 1's profile copied | profile persistence, logos skipped |
| 3 loops | `-Cycles` full loops in one process, `WFC_SKIPINTRO` | memory / handles at each return, stale state, screens per cycle, fresh player state, load time, audio PCM |

The gate reads two sources:
- the product's own trace: the `WFC_FLOWLOG` JSON lines, plus `wfc.log`;
- evidence from **outside** the process: window captures (PrintWindow) taken when the trace changes state, and
  process memory / handles sampled once a second.

## Layers: "data exists" vs "the user experiences it"
| layer | meaning | example |
|---|---|---|
| DATA | authored data, catalog, unit tests | intro `.mkv` files exist |
| STATE | the product's trace reaches the state with the expected values | `movie.play Logo_Activision` in order |
| PRESENTED | the user can see / hear it, from evidence outside the trace | `movie.unavailable` (no decoder: KNOWN); window capture of the menu is black (KNOWN) |
| MATCH | TDM lifecycle (Gameplay) | SKIP until Gameplay logs the match lifecycle |
| LIFETIME | repeated loops | +1.5 GB private memory per return |

A check passes only in its own layer: a STATE PASS never implies PRESENTED. Statuses:
- PASS;
- FAIL: contradicts CONFIRMED/HIGH evidence (`m05/expectations.json`);
- KNOWN: a documented gap or provisional adapter, with its owner;
- INFO;
- SKIP: the feature or hook does not exist yet; the check is ready for it;
- HUMAN: see `HUMAN-CHECK-M05.md`.

**Capture control.** The in-game frame is always drawn by the 3D renderer. If it captures as a uniform image, window
capture failed for that run (window occluded or minimised, or the desktop locked), and every screen check is SKIP
rather than judged. Run the gate with the product window visible on an unlocked desktop.

**Input focus is HUMAN by design.** The `WFC_AUTO*` scripted inputs are injected after the frontend's focus gate, and
the window polls `GetAsyncKeyState`. Real key input could only be produced by typing into the foreground window of a
shared machine, which the gate will not do. Proposal for the frontend lane: a scripted-input hook applied before the
focus gate. With it, `pause_input_focus` becomes automatic: walk speed before the pause > 1 m/s, displacement during
the pause < 0.3 m.

## Expectations
`m05/expectations.json` gives each value with its source and confidence:
- RE `notes/MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md`;
- AssetTools `manifests/frontend_*.json`;
- Systems FIDELITY M05.

Only CONFIRMED and HIGH values can FAIL.

## Ready for the upcoming branches
| incoming | what changes in the gate |
|---|---|
| Gameplay TDM (PendingMatch, scoring, respawn, MatchOver) | `MATCH.*` turn from SKIP into checks once the match logs its lifecycle. Proposed protocol: `protocols/RUNTIME-EVENTS.md`; `match-validator.ps1` parses it. The UI events 4 / 5 / 9 in the flow trace (Spectating / respawn / EndGameStats) are checked against RE 1.4. `pending_countdown` expects 10 s. Return after the match expects `UI_Lobby_m` with `MapId=508` after 15 s. |
| Systems FrontendAudio wiring | `PRESENTED.menu_music` expects `MUSIC play FRONTEND_MX_ORBIT_01` in the frontend, `MP_PARTY_LOBBY_MX` / `MP_LOBBY_MX` in the lobbies and none in the match. `LIFETIME.audio_pcm_returns_to_base` reads the AMB `pcm=` / `map=` fields. |
| Rendering map unload | `LIFETIME.memory_per_cycle` should drop from about +1.5 GB per cycle to near 0 (PASS below 30 MB). `memory_peak` FAILs above 6 GB. |
| Frontend GFx presenter / video decoder | `PRESENTED.*_screen` turn from KNOWN (black window) into HUMAN (something drawn: judge it). `intro_movies_played` passes when there is no `movie.unavailable`. |
| per-level resource counters | `LIFETIME.map_collision / render_assets / frontend_movies / event_queues / timers` are SKIP until the product logs one counter line per `level.begin` (proposal in `protocols/RUNTIME-EVENTS.md`). |

## Merge preview for integration (git merge-tree, no refs touched; 2026-10-03)
| merge | result |
|---|---|
| integration/milestone-04 a03d7f7 + agents/frontend a55a4e9 | fast-forward to a55a4e9 |
| + agents/systems da125b6 onto frontend | conflicts: FIDELITY.md, STATUS.md, src/core/Application.cpp, src/game/World.cpp |
| + agents/rendering onto frontend | conflict: FIDELITY.md only |
| + agents/gameplay onto frontend | clean (already contained) |
| systems + rendering | conflicts: FIDELITY.md, src/game/VehicleFx.cpp |

`Application.cpp` and `World.cpp` are where the frontend's match load/unload and Systems' `loadMapAudio` /
`unloadMapAudio` / `resetSystemsForMatch` meet. The Systems FIDELITY M05 call sequence says where they belong.
