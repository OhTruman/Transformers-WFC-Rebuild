# Window 2026-10-11 00:10-00:25: 15008e2 renderer like-for-like on the 5 maps that were UNKNOWN in regress-6b243ff

All runs read the installed dry6 nav (ExtractedAssets, 23:27). Plain WFC_LOCKSTEP (no LOCKSTEP_EXACT / ASYNCSTEP). No contamination.

## regress: head 15008e2 + WFC_NOFORCEDDIR=1 + WFC_NOSTRANDSUICIDE=1 vs base 47726bc (64p real play 4K, 2 runs per arm)
| map | base p90 | head p90 | delta | verdict |
|---|---|---|---|---|
| 501 Seed | 2.94 | 2.94 | +0.00 | PASS |
| 504 Rust | 2.91 | 2.90 | -0.01 | PASS |
| 508 Streets | 2.23 | 2.24 | +0.01 | PASS |
| 509 Molten | 3.02 | 3.02 | +0.00 | PASS |
| 510 Gorge | 2.88 | 2.88 | +0.00 | PASS |

SIMHASH and workload are identical on all 5 maps, so with the regress-6b243ff PASSes (Berth, Complex, Debris) all 8 maps pass
for the renderer from 47726bc to 15008e2.
This also answers the stall question: plain-WFC_LOCKSTEP timing in 15008e2 (after the LOCKSTEP_EXACT split) shows no cost on any
of the 5 maps. The lockstep timing rows are production-like, and base 47726bc stays a valid baseline.

## stall row (Gorge, 6b243ff vs 15008e2 + NOSTRANDSUICIDE, forced-dir volumes on in both): INVALID, a different match
- The arms diverge at SIMHASH step 3172. Each arm is self-identical (new r1 = r2).
- 15008e2's Classic sim with volumes on differs from 6b243ff beyond stranded suicide. The log shows `stranded off the mesh ...
  walking to the mesh` recovery, or a volume revision; this pass does not tell which.
- The row's numbers (p90 2.69 vs 2.79; draws 1172 vs 985) compare different matches and are not a renderer delta. The regress
  table above supersedes it.

Memory: commit 88 / 113 GB, nonpaged 22.93 GB (flat), C: 20.6 GB free (mem.txt).
