# Per-match memory growth on 09c ed6e2f7 (2026-10-08, for Systems' attribution)

match-repeat-memory: frontend-launched private TDM (3 v 4 bots), 12 matches per process, private bytes after load / after unload.

| run | after-unload private, match 1 -> 12 | later-half slope | GL live textures |
|---|---|---|---|
| plain ed6e2f7, Streets x12 (CONTROL, no profiler) | 4627 -> 5047 MB | **33 MB / match** | 70 -> 93 (+2 / match) |
| ed6e2f7 + Systems' AllocProf (a6be565 files), Streets x12 | 4869 -> 5456 MB | 24-28 MB / match | 70 -> 93 |
| same, Streets / Debris / Molten cycle x12 | 4601 -> 5875 MB | **52 MB / match** | 70 -> 97 |

Audio PCM flat in all runs. The profiler adds a fixed ~230 MB but not the slope (its tables are static + one 16 MB VirtualAlloc
and never use operator new - Systems). In the lobby between matches operator-new live grew ~+9 MB / match (allocprof_same.txt).
Older builds (8c2b6e3 / fa973d3) measured flat with the same harness, so this is a regression.
Attribution (Systems, from these samples): game::bindMeshOf (GpuSkin.h) - a static bind-mesh cache keyed by SkinnedModel* that
is never erased while models reload per match at new addresses; multi-map cycles load more distinct models -> faster growth.
Fix routed to Gameplay / Rendering; Experimental re-runs both halves as the fix check.
