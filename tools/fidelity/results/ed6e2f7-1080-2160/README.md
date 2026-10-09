# 09c ed6e2f7 vs beb9545 - 1080p + 2160p (2026-10-08 20:13-21:20, one Experimental window)

ed6e2f7 = fbe0162 (getenv caching in render / frame loop / game / frontend, texture-bind cache, original-texture DDS loader,
scoreboard lifecycle, barrier walk-out, PLAYERBOT hunt bias) + the "opaque" DDS entry handling that its own
build_texture_formats.py needs (fbe0162's exe + ed6e2f7's tool would mis-render those textures, so ed6e2f7 was verdicted).
Render data generated after AssetTools' 18:35 flipbook PNG fix; texture_formats.json from ed6e2f7's tool. Streets, verified
overview cam, PGO, second match, InProgress only, rendered size verified per row (2160p = WFC_RENDERSIZE=3840x2160 into a
1920x1080 window). The beb9545 baseline render data predates the flipbook fix (texture content only).

## Overview cam - p90 (target <= 3.33 ms), % of frames <= 3.33, CPU submit
| | beb9545 1080p | ed6e2f7 1080p | beb9545 2160p | ed6e2f7 2160p |
|---|---|---|---|---|
| 20 (10 v 10) | 2.12 (99.9 %) | **2.13** (99.9 %) | 2.43 | **2.35** (99.9 %) |
| 64 (32 v 32) | 3.34 (89.9 %), submit 1.78 | **2.68** (99.3 %), submit 1.33 | 3.76 (63.1 %), submit 2.05 | **3.24** (92.3 %), submit 1.31 |

Stalls: one 99 ms frame in ed6e2f7 2160p 64p match 2 - "GPU frame spike 97.2 ms (cpu 2.6): characters+caller 95.4 ms" at a
Truck4 respawn next to a Barrier wall (first-use cost; sent to Rendering). Otherwise no frame > 33 ms in the overview rows.

## Real play (PLAYERBOT, follow cam, 150 s matches - the pilot now fights: 71-83 target samples, 0-3 kills per run)
| | 1080p m1 / m2 p90 | 2160p m1 / m2 p90 |
|---|---|---|
| 20 | 1.87 / **1.57** | 2.47 / **2.40** |
| 64 | 2.64 / **2.68** (99.4 %) | 2.68 / **3.43** (87.2 %) |
32 v 32 at 2160p in real play varies with combat (2.68 ms in one match, 3.43 in the other).

**Verdict (300 fps = p90 <= 3.33 ms):** 1080p - MET everywhere (32 v 32 overview 2.68, real play 2.68). 2160p - 10 v 10 MET;
32 v 32 worst-case overview MET (3.24, was 3.76), real play 2.68 / 3.43 (one combat-heavy match 0.1 ms over).

## DDS (original textures) checks
- Visual guard, same build, DDS vs WFC_NODDS=1, native size, overview cam, 21 match-step-aligned frames: PASS, 0 flagged,
  max 0.055 % of pixels over threshold (minified detail). DDS confirmed active ("original texture blocks: 6010 textures
  indexed" only in the DDS run); no PNG-fallback warnings.
- Private bytes, 64 p frontend, DDS on vs off (one run each): load 4883 vs 5006 MB (match 1), 5067 vs 5112 MB (match 2);
  after unload 4742 vs 4925 / 4951 vs 5104 MB - DDS on is ~45-180 MB lower.
