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
