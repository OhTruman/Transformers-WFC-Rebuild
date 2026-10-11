# regress-8map: base 09c 47726bc vs head 09c 6b243ff (merge batch 2026-10-10, window 21:45-22:05)

64p TDM real play (WFC_PLAYERBOT), lockstep, 3D 3840x2160, seed 1234, steps 600-9600, 2 runs per arm, better-of-2 p90, BLOCK above +0.15 ms.
Contamination watchdog on every row: no CONTAMINATED.txt.

| map | base p90 | head p90 | delta | verdict |
|---|---|---|---|---|
| 502 Berth | 2.34 | 2.34 | +0.00 | PASS |
| 503 Complex | 2.47 | 2.47 | +0.00 | PASS |
| 507 Debris | 2.53 | 2.54 | +0.01 | PASS |
| 501 Seed | 2.78 | (2.23) | - | UNKNOWN: different match from step 891 |
| 504 Rust | 2.62 | - | - | UNKNOWN: different match from step 8612 |
| 508 Streets | 2.30 | - | - | UNKNOWN: different match from step 3634 |
| 509 Molten | 3.19 | - | - | UNKNOWN: different match from step 5662 |
| 510 Gorge | 2.58 | - | - | UNKNOWN: different match from step 2333 |

Head p90 is not comparable on the UNKNOWN maps (a different match); those maps need the free-running verdict.

## Why the matches differ
- Both builds are deterministic. Base r1 = r2 and head r1 = r2 over every SIMHASH step on all 8 maps.
- All bots are Classic in both arms (log `bots: pN AI Classic`), so the Smart AI changes (flank / squad) are not the cause.
- The head loads TnForcedDirVolumes on every map (log `mapstate: N TnForcedDirVolumes`: Seed 4, Berth 1, Complex 6, Rust 5,
  Debris 22, Streets 4, Molten 9, Gorge 3). The base has none.
- 01c02d9 also removes those brushes from pawn movement collision; they were solid for every form before.
- That is the only sim change for Classic in the batch, so it is the most likely cause; no log line marks a touch, so this is not proven.
- Gameplay's Classic identity check (Streets, a1ee934) found no pawn reaching a slab in its configs.
  This seed reaches a volume on 5 of 8 maps, including Streets at step 3634.

## Memory pressure (driver pool leak; no restart)
See mem.txt. Commit 87-88 / 113 GB and nonpaged pool 22.75 GB, flat across the window. Base absolute numbers sit at or below the
earlier 47726bc lockstep A/A (Gorge 2.58 here vs 2.70-2.74), so no inflation was seen.
