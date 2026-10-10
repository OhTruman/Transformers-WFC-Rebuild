# Per-map 300 fps verdict + fix loop — 2026-10-09 / 10 (Experimental)

Target (user, via Integration): every TDM map at 64 players, p90 <= 3.33 ms, at 1080p and 3D 2160p, overview and real play,
without perceived-quality loss. Harness: `capacity-stress.ps1` (frontend-launched TDM, 2 x 60 s matches per process, match 2
reported; real play = WFC_PLAYERBOT=1, 150 s), WFC_SLOWFRAME=3.33 on every row (slow-frame cause = dominant time bucket;
`> 2 MB upload` is a heavy-combat flag, not a cause - 8112fa4). 2160 rows = WFC_RENDERSIZE=3840x2160 into a 1080 window (the
desktop is 2560x1440). Cameras: `cam-sweep.ps1`, heaviest *representative* usable view per map (most structure among
candidates within 5 % of the most world draws); Molten and Gorge picked by hand, notes in work\permap\cams-50x\PICK_NOTE.txt.
Every timed window was announced through Integration with all lanes holding; first-row start times were checked against the
other lanes' activity (one stalled window - an orphaned foreign exe - measured nothing until it was gone).

Baseline 37eec59 = the playtest build. "Head" = the 09c head at the time of that map's window (builds listed below).
Full rows (20 / 64 players, both builds, slow-frame causes): `per-map-tables.md`.

## Latest status (64 players, p90 ms; best / latest build measured for that row)

| map | overview 1080 | overview 2160 | real play 1080 | real play 2160 | limiter where it misses |
|---|---|---|---|---|---|
| 501 Seed | **3.03 MET** (2d6a027) | **3.28 MET** (2d6a027) | 2.74 MET (2a7bfc0) | 3.56 (345b8cc; p99 6.14) | GPU (shadow passes fixed; rest GPU) |
| 502 Berth | 2.87 MET (f662513) | 2.87 MET (2a7bfc0) / 3.04 MET (072cee3) | 2.26 MET | 3.41 (f662513, before the shadow scissor) | GPU |
| 503 Complex | 2.94 MET (072cee3) | **3.21 MET** (345b8cc) | 2.38 MET | **3.06 MET** (345b8cc) | GPU |
| 504 Rust | 4.34 (a19dc53) | 4.29 (a19dc53) | 3.72 (b898c75) | 3.92 (b898c75) | GPU - world geometry volume (2.1 M verts); occlusion cull planned |
| 507 Debris | 2.89 MET | 2.78 MET | 2.62 MET | 2.75 MET | - |
| 508 Streets | 2.58 MET | 2.87 MET | 2.40 MET | 2.95 MET | - |
| 509 Molten | 2.62 MET | 3.16 MET | 2.49 MET | 3.56 (345b8cc) | GPU - eye-level translucency (0.85 ms) |
| 510 Gorge | 3.55 (a19dc53) | **3.32 MET** (345b8cc) | 3.15 MET | 3.63 (345b8cc) | GPU at 4K; 1080 overview GPU 48 % / chars 45 % |

37eec59 for contrast (64 players overview 1080 / 2160): Seed 5.10 / 7.23, Berth 2.75 / 4.06, Complex 4.56 / 5.73, Rust
6.57 / 6.82, Debris 2.83 / 2.94, Streets 3.29 / 3.34, Molten 6.40 / 6.06, Gorge 3.98 / 7.06.

## A/Bs run for the fix loop (all on a quiet machine, SLOWFRAME)

| fix | A/B | result |
|---|---|---|
| level BSP multi-draw (64ebdf0) | Seed 64, WFC_NOBSPMDI | 1080 p90 5.42 -> 3.60; 4K unchanged (GPU-bound) |
| per-robot shadow scissor (2a7bfc0) | WFC_SHADOWFULL | Seed 4K 6.85 -> 3.66, Berth 4K 3.97 -> 2.87 MET, Seed 1080 4.07 -> 3.53 |
| skinned-palette trim (072cee3) | WFC_SKINFULLROW, 2 reps | no 4K regression (GPU per slow frame identical); 1080 slightly better; the single-run 4K 4.07 was noise |
| uniform location cache + partial shadow clear (c7bd3e9) | vs 072cee3 / 2a7bfc0 | Seed 1080 3.25-3.35, chars CPU 1.61 -> 1.06 ms |
| depth prepass (Rust) | WFC_NOZPREPASS | no-prepass -0.2 ms GPU at 1080, +0.2 at 4K (vertex vs fill) -> per-bucket prepass |
| original-order projected shadows (2d6a027) | Seed 64, WFC_SHADOWINCREMENTAL | 4K p90 3.64 -> 3.28 MET, 1080 3.38 -> 3.03 MET (GPU 2.56 -> 2.28 / 2.26 -> 1.79) |
| vertex-lightmapped draws batched (a19dc53) | Rust 64, WFC_NOVLMMDI | p90 4.63 -> 4.34 (1080), 4.90 -> 4.29 (4K) |
| Rust GPU buckets | WFC_GPUBUCKETS | world MDI 0.64 / 0.99 ms of ~2.7-2.9 ms GPU; top buckets = big decal / trim materials |

Builds: 37eec59 (playtest), f662513, 64ebdf0, 2a7bfc0, 072cee3, c7bd3e9, b898c75, a19dc53, 2d6a027 (all 09c Release + PGO, render
data of 37eec59 by junction - tools/render differences between them are packaging scripts only).

## Retractions / harness fixes during the series
- "Seed 1080 remainder = > 2 MB upload spikes (46 %)" RETRACTED: the classifier ranked upload size before time buckets
  (8112fa4); recomputed, Seed 1080 was chars render CPU 59 % / GPU 41 %.
- cam-sweep: `-Heights 40,100` through `powershell -File` parsed as 40100 and N1 formatting added a sixth camera field (5ef943b);
  heaviest-view rule refined to "most structure within 5 % of the heaviest" (6b7576f).
- 64ebdf0 "wfc_fidelity link error" was my harness CMake overlay, not the product (8d65304).

## 2026-10-10 follow-ups (c17eee7 occlusion cull, c969c84 prepack + adaptive cull)
- c17eee7 occlusion cull (2 reps): Seed MET every row (overview 4K 3.05 / 3.21, real play 4K 3.29 / 3.24); Gorge overview 4K
  3.40 vs 3.35 off; Gorge real play 4K on 3.46 / 3.53 vs off 3.17 / 3.11 (cull path +0.4 ms); Rust ~flat. OCCSTATS (ea63917,
  maps verified): the cull removes 59 % (Rust overview) / 26 % (Gorge overview) / ~48 % / ~21 % (real play) of frustum-visible
  triangles - without a timing gain on Rust's overview, so Rust is not bound by those world triangles. CPUPROF: Rust overview
  main thread 93 % in present / SwapBuffers (GPU-bound queue wait).
- c969c84 (prepack + adaptive cull) REGRESSION: prepack no gain on Rust; the cull path blocks the CPU in drawMdi (culled-count
  readback): world-pass CPU 0.54-0.62 -> 2.9-3.05 ms per slow frame. Rust real play 4K at 150 s: c17eee7 3.72 / 3.72 vs c969c84
  5.06 / 5.29; Gorge real play 4K cull on 5.3-5.9 vs off 3.45-3.72; Seed real play 4K 4.46 (was MET). Rendering restores
  c17eee7's cull as the default (readback opt-in) - re-time pending.
- FFA 64 (345b8cc, Streets): 64 / 64 spawns, results screen x 2, second match, p90 2.62-2.86 MET; open defect: bots stranded off
  the nav mesh (cell -1, no-path to 535) - capacity-stress now has an `offmesh` check.
- Retractions this day: lane-build OCCSTATS (loaded Streets - hook missing), "overview cams cull nothing" (draws are pre-cull).
- 2040e9a (Rendering 6cf1a53: c17eee7's cull restored as default, readback opt-in, prepack kept) vs c17eee7, real play 64p 4K at
  150 s, 2 reps: Rust 3.55 / 3.43 vs 3.55 / 3.45; Seed 3.02 / 3.30 MET vs 3.32 / 3.06; Gorge 3.62 / 3.17 vs 3.56 / 3.49;
  world-pass CPU 0.40-0.60 ms on both - the c969c84 regression is fixed.
