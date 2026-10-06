# Validation ledger

One row per gate run, newest first. Later gates read this to select relevant tests (the "changed since" column) and
to reuse evidence for unchanged systems. The tiers are defined in `VALIDATION-TIERS.md`.

| date | tier | integration commit | previous validated | changed since (files) | verdict | evidence |
|---|---|---|---|---|---|---|
| 2026-10-06 | FAST | `9c159066` integration/milestone-08g | `175a6348` | gameplay 18, other 15, systems/audio 16, frontend 10, rendering 7, assets 6 | 2 product FAIL = display-mode-change seam (Rendering, not proven new); GoalScore + CurrentGame reads answered; else no obvious breakage | `results/fast-9c15906/` |
| 2026-10-05 | TARGETED map sweep | `fdffa7f` integration/milestone-08c | `175a6348` | - | 0 compile fallbacks on 10 maps; Debris source lightmaps missing; Gorge VLM unbound; reverb presets missing; Seed / Berth dark (06b → M08b darkening, M08c unchanged; human check) | `results/mapsweep-fdffa7f/` |
| 2026-10-05 | AUDIT (static) | `175a6348` integration/milestone-08b | - | - | 0 P0, 5 P1, 6 P2, 7 P3, 1 UNKNOWN (map compile sweep) | `results/audit-m08b/AUDIT.md` |
| 2026-10-05 | FAST | `175a6348` integration/milestone-08b | `ed91718` (M07) | gameplay 29, other 8, frontend 7, rendering 5, systems/audio 5, assets 5 | **NO OBVIOUS BREAKAGE** - PASS 52 / KNOWN 1 (Trail2 / Beam2 ribbons, Rendering) / PARTIAL 5 / UNKNOWN 1 / 0 product FAIL | `results/fast-175a634/` |
| 2026-10-05 | dry run (calibration) | `681fd29` integration/milestone-06b | - | - | calibration of the M07 suites (not a verdict) | `results/m07-dryrun/` |
| 2026-10-04 | presentation (by Integration) | `ed91718` integration/milestone-07 | 06c | - | 22 pass / 6 fail / 2 partial / 1 unknown; old gate copy, four expectations since corrected | Integration `work/m9/present` |

## Reusable evidence: what each system was last proven with
Rerun a system only if it changed since the commit listed here.

| system | last proven | commit | tier |
|---|---|---|---|
| world through the frontend route (no world loss), GL state after overlays | presentation route + map switch + render state (normal) | 175a634 | FAST |
| render-state detector blindness (negative controls) | both defences off -> detected | 681fd29 (M07 dry run) | TARGETED |
| all 33 chassis spawn with their own bodies | `WFC_CHASSIS` per chassis | 1216e80 (Gameplay 22a) | TARGETED |
| class presets through Choose Character | characters runtime | 681fd29 (fallback seen) - **re-prove on a build with WFC_CHARSELECT + 22a** | TARGETED |
| every launchable map: mechanics / match / audio | m07-maps | 681fd29 (06b) | TARGETED |
| modes TDM / DM / DOM / KOTH | m07-modes | 681fd29 (06b); CTF / EXT not implemented then | TARGETED |
| memory plateau over map cycles | m07-soak / WFC_MEMCYCLE | not run on M07+ | FULL |
