# M1 full verdict 65e856f (TIMED overnight slots A-D, 2026-10-11/12)

Head 65e856f, WFC_BOTAI=Classic pinned, 150 s rows, t64, WFC_SLOWFRAME=3.33. Per slot: 2 maps x {4K = RenderSize 3840x2160 in a
1920x1080 window, 1080} x {ov = FixedCam pick cam, rp = player bot} x 2 reps (rep 1 of every cell before rep 2), plus one real
2560x1440 FULLSCREEN real-play row per map. One discarded warm-up row opens each slot. Script `slots_65e856f.sh`, log `slots.log`,
per-row capacity.csv in `rows/`, desktop mode after each fullscreen row in `desktop.txt`.

## Slot A - Gorge (510) + Rust (504), 20:37-22:53: 18 / 18 MET

| map | cell | r1 p90 | r2 p90 | avg fps r1 / r2 |
|---|---|---|---|---|
| 510 | ov 4K | 2.62 | 2.62 | 411 / 411 |
| 510 | rp 4K | 2.67 | 2.48 | 460 / 552 |
| 510 | ov 1080 | 2.40 | 2.41 | 491 / 489 |
| 510 | rp 1080 | 2.40 | 2.28 | 563 / 601 |
| 510 | fs 1440 | 2.37 | | 555 |
| 504 | ov 4K | 2.88 | 2.88 | 390 / 388 |
| 504 | rp 4K | 2.74 | 2.71 | 492 / 500 |
| 504 | ov 1080 | 2.85 | 2.84 | 390 / 391 |
| 504 | rp 1080 | 2.04 | 2.15 | 750 / 740 |
| 504 | fs 1440 | 2.19 | | 621 |

Warm-up 510 rp 4K r0 2.88 (discarded). Every row: spawned 64/64, ended by Time, no CONTAMINATED.txt. Fullscreen rows: client
2560x1440 at window rect (0,0)-(2560,1440); desktop after each = 2560x1440 @ 144 Hz. Memory start / end: commit 93/115 GB both,
nonpaged 24.52 -> 24.57 GB, C: 14.5 GB free both.

Rust ov is the slowest cell (2.84-2.88 at both resolutions, so not resolution-bound at the pick cam); headroom to 3.33 is 0.45 ms.

## Attribution (between A and B)

The in-script stop in `attrib_65e856f.sh` did not take (taskkill on the msys bash PIDs left the slot loop alive; it went on into
509 ov 4K r1). Stopped by PID with Stop-Process at 22:53:59 (shells, harness, exe); rows 509 rp r0 and 509 ov r1 discarded.
The attribution rows were restarted with `attrib2_65e856f.sh` (same rows, no stop phase), which then restarts slots B C D.

### Attribution result (22:54-23:31): CONFIRMED - the HUD shadow cache carries most of the real-play gain

Gorge rp 4K 300 s, off = WFC_NOHUDSHADOWCACHE=1, order off / on / off for drift. All rows 64/64, ended by Time, uncontaminated.

| row | p90 | p50 | p99 | under 3.33 | avg fps |
|---|---|---|---|---|---|
| off1 | 3.08 | 2.47 | 3.62 | 96.6 % | 405 |
| on1 (control) | 2.80 | 2.21 | 3.56 | 97.9 % | 460 |
| off2 | 3.22 | 2.68 | 3.83 | 93.6 % | 376 |
| off pooled | 3.15 | 2.58 | 3.74 | 95.2 % | 234096 frames |

- Turning the cache off brings p90 from 2.80 (control; the earlier 3 reps were 2.68-2.84) to 3.15 pooled: the cache is worth about 0.35-0.40
  ms in real play, against 0.08 for everything at 510 in lockstep. That fits the reading that the cache hits when several rendered frames
  share one sim step.
- The off arm is still 0.15 below 9c3560e's 3.30 mean. That matches the remaining Rendering items (lockstep -0.04..-0.11), within the
  rep spread seen on 9c3560e (3.14-3.41).
- off2 is 0.14 above off1, with the on control between them in range. That is drift of the size seen between reps before, not a sign of an
  environmental cause: nonpaged 24.58 -> 24.59 GB, C: 14.5 -> 14.6 GB.

## Slot B - 509 + 507, 23:31-01:47: 18 / 18 MET

| map | cell | r1 p90 | r2 p90 | avg fps r1 / r2 |
|---|---|---|---|---|
| 509 | ov 4K | 2.23 | 2.23 | 510 / 511 |
| 509 | rp 4K | 2.60 | **3.20** | 497 / 458 |
| 509 | ov 1080 | 2.13 | 2.15 | 551 / 546 |
| 509 | rp 1080 | 1.88 | 2.00 | 694 / 679 |
| 509 | fs 1440 | 2.12 | | 631 |
| 507 | ov 4K | 2.30 | 2.29 | 511 / 514 |
| 507 | rp 4K | 2.33 | 2.42 | 547 / 524 |
| 507 | ov 1080 | 2.30 | 2.28 | 517 / 521 |
| 507 | rp 1080 | 2.18 | 2.18 | 571 / 556 |
| 507 | fs 1440 | 2.28 | | 549 |

Warm-up 509 rp 4K r0 2.77 (discarded). Every row: 64/64, ended by Time, uncontaminated. The desktop was 2560x1440 @ 144 after both
fullscreen rows. Memory: commit 93 -> 94/115 GB, nonpaged 24.59 -> 24.64 GB, C: 14.6 -> 14.5 GB.

509 rp 4K r2 at 3.20 is the closest any row has come to 3.33. Its p50 (2.07) matches r1 (2.00), but p99 is 3.83 against 3.31, and 4604 frames
(all GPU-bound) are over 3.33, against 728 in r1. The ov cells are steady (2.23 / 2.23), so this is a real-play tail: the player route and
fights differ per rep (determinism probe b9053ad), and in r2 the camera spent longer in a GPU-heavy view. It is not a contamination or
drift sign; the next row, 509 ov 1080 r2, is back at 2.15. If this map needs more margin, a 300 s x3 pooled rp row at 509 would settle it.
