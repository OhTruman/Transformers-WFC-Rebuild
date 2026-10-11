# M1 re-verdict on 09c 5aa2d22 (2026-10-11, window 06:00-08:00, Experimental)

5aa2d22 = ad4e6c5 + Gameplay 1e9e583 / 7faf62b (bot fixes) + Rendering fa34606 (byte-identical distortion cut). Release build, PGO, no LTO
(work/ab/m9c_5aa2d22). Memory: commit 89-90 / 114 GB, nonpaged 23.28 -> 23.35 GB (driver leak, +0.4 GB since the ad4e6c5 verdict), C: 17.0 GB free.

## 1. regress-8map ad4e6c5 -> 5aa2d22 (lockstep, 64p real play, 3D 3840x2160, best valid of 2)

| map | base | head | delta | verdict |
|---|---|---|---|---|
| 501 | 2.35 | 2.21 | -0.14 | PASS (head r2 UNKNOWN: binds -3.4 %) |
| 502 | 2.26 | 2.15 | -0.11 | PASS |
| 503 | 2.39 | 2.26 | -0.13 | PASS |
| 504 | 2.91 | (2.70) | - | UNKNOWN - sim diverges at step 1431 |
| 507 | 2.37 | (2.58) | - | UNKNOWN - sim diverges at step 3928 |
| 508 | 2.27 | (2.15) | - | UNKNOWN - sim diverges at step 9377 |
| 509 | 3.25 | 3.12 | -0.13 | PASS |
| 510 | 2.80 | 2.74 | -0.06 | PASS |

- 504 / 507 / 508: head is A/A-identical. The first base-vs-head SIMHASH difference is exactly where Gameplay 7faf62b's
  "jet wedged ... landing within 3 m of the mesh" fires in head (504 p17 t 23.85, 507 p9 t 65.47, 508 p29 t 156.29; t x 60 = step). Base
  never logs it. This is an intended Classic behaviour change: Gameplay's "Classic identical 5/5" held because the path did not fire in those
  runs. These maps go by the free-running verdict. The head p90s in brackets come from a different match.
- 501-503 were first UNKNOWN on workload parity (REGRESSION-orig-dynparity.md): same match, but dyn draws -5..-9 per frame. Cause (Rendering,
  by code reading; Experimental, from the logs): outside WFC_LOCKSTEP_EXACT a pawn-occlusion result that is not yet READY counts as visible.
  The cut frees ~0.12 ms of GPU, so more results are ready and more hidden pawns are skipped:
  - occlusion skips x1.4-2.1 (501 58956 -> 79528 / 89317, 502 33389 -> 66247, 503 15297 -> 31941);
  - pawns tested equal within 0.03 % (501 247607 / 247627);
  - about 2.7 dyn draws fewer per extra skipped part, on all 5 same-match maps.
  - "dyn + skips" does NOT balance (502 still -3.6 %), so that sum was not used as the rule.
- New rule (lockstep-ab.ps1): a dyn mismatch is excused only when every other count is in parity, the pawns tested agree within 0.5 % and dyn
  moves against the skips. The excuse is printed in the row note. Re-scored from the same logs (-ReportOnly; no new runs).

## 2. Free-running verdict: the 3 cells ad4e6c5 missed (64p, 150 s, 3D 3840x2160 in a 1920x1080 window)

| cell | r1 | r2 | ad4e6c5 |
|---|---|---|---|
| Gorge overview 4K | 3.20 MET | 3.26 MET | 3.35 / 3.32 |
| Gorge real play 4K | **3.41 MISS** | 3.21 MET | 3.38 / 3.53 |
| Rust real play 4K | 3.21 MET | 3.11 MET | 3.01 / 3.42 |

- Gorge rp r1 miss: 100 % GPU-bound, 87.3 % of frames under 3.33 ms. Avg slow frame 3.62 = render 1.65 (world 0.40, chars 0.40, fx 0.43,
  transl 0.38) + outside 1.97; gpu 2.61.
- No contamination; rendered 3840x2160 checked.
- Bots:
  - Gorge rp off-mesh FAILs are the known no-way traps.
  - NEW Rust trap (rp r2, broken 1): p1 Pushrod (a Classic bot) as a robot off-mesh on a ledge at (-185.2, 46.5, -52.5) to the match end.
    It keeps finding paths (nopath 0), so the stranded suicide (which needs "no way found") never fires.

## 3. Real-window rows (Integration addition): 2560x1440, not 3840x2160

The display offered no 3840x2160 mode (m1r-5aa2d22-dispmodes*.txt):
- DISPLAY1 (7900 XTX) runs at 2560x1440 @ 144;
- its mode list, including EDS_RAWMODE, stops at 1920x1080 @ 60 - the generic list Windows reports without the monitor's EDID (monitor off
  or asleep).

So, per Integration, these rows use the user's actual desktop:
- win = Fullscreen=1 at 2560x1440 (borderless, no mode switch; client 2560x1440 at (0,0) checked with GetClientRect);
- rs = the same 3D size in a 1920x1080 window.

Gorge and Streets real play, 64p, 150 s, WFC_GPUSTAMPS in every row. The desktop mode was checked after each fullscreen row and stayed at
2560x1440 @ 144 (m1r-5aa2d22-desktop.txt). GPU ms are the means of the timed match's 1200-frame stamp reports (rows/gpustamps-match2.txt).

| map | rows (r1 / r2) | p90 ms | GFx UI GPU ms | tonemap into window | frame start -> present GPU |
|---|---|---|---|---|---|
| Gorge | win 1440 | 2.65 (r1 contaminated) / 2.60 | 0.707 / 0.692 | 0.034 | 1.68 / 1.70 |
| Gorge | rs (1080 window) | 2.79 / 2.64 | 0.590 / 0.592 | 0.021 | 1.60 / 1.60 |
| Streets | win 1440 | 2.31 / 2.34 | 0.737 / 0.710 | 0.034 | 1.57 / 1.57 |
| Streets | rs (1080 window) | 2.05 / 2.17 | 0.620 / 0.624 | 0.021 | 1.37 / 1.37 |

- The real window costs +0.10..+0.12 ms of GFx UI and +0.013 ms of tonemap at 1440 (1.78x the 1080 pixels).
- Whole-frame GPU: +0.09 ms on Gorge, +0.20 ms on Streets.
- A linear-in-pixels estimate (not measured) for a true 2160p window: about +0.4 ms GFx UI over the 1080-window rows. The "4K" cells above
  measure the UI at 1080.
- All 8 rows MET at 1440 (p90 2.05-2.79).
- Gorge win r1 is contaminated by a foreign python.exe started 07:07:31 (rows/real2560x1440-510-win-r1-CONTAMINATED.txt), so r2 is the valid
  Gorge win row.

## 4. Untimed: Gorge LOCKSTEP_EXACT capture nondeterminism (lockdet/)

Local diagnostic build (5aa2d22 + an env-gated per-frame occlusion log; never committed), lockr2's flow on 510 with WFC_DISTFULL=1:
- 9 runs (base / ASYNCSTEP=0 / NOPAWNOCCLUSION, 3 each) gave 25/25 identical captures.
- The occlusion log shows the GL_ANY_SAMPLES_PASSED result differing for a bit-identical query box with every earlier frame identical:
  - base r1 vs r2 / r3: frame 750, o143;
  - sync r1 vs r2: frame 756, o137.
- So the occlusion state (and the FX relevance / prep skip it feeds) is not reproducible even under EXACT. Passed to Rendering as a post-M1 item.
