# Fix proposals for the owning workstreams

These patches are **not applied to any product branch** by the Experimental agent.

## Status after the native RE evidence (2026-10-01)
| Patch | Status | Why |
|---|---|---|
| `gameplay-1-jump-integration.patch` | **Intent CONFIRMED; patch stale** | RE STEPUP_LEDGE #4: `physFalling` integrates trapezoidally (0.05 s substeps), so the apex is exactly JumpHeight at any frame rate. Gameplay Pass 12 (21862a2) still overshoots: 5.14 m @60 Hz, 5.29 @30, 5.07 @120 vs 5.00. The patch no longer applies to the rewritten `CharacterMovement.cpp`; re-implement the same `p.y += 0.5*(vOld+vNew)*dt` (or 0.05 s substeps). |
| `gameplay-2-step-height.patch` | **Superseded** | Pass 11 fixed the double count. RE STEPUP_LEDGE #2 gives the step as an instant pop up to **37 UU** (35 + 2), not 35. Pass 12 climbs 0.35 m and enters a 0.36 m ledge. |
| `gameplay-3-wall-probe-slide.patch` | **Intent CONFIRMED; patch stale** | RE STEPUP_LEDGE #1: slide = delta projected on the wall + 2 UU push-out, up to 3 sweeps, with velocity rebuilt from the displacement. Pass 12 still sticks (0.30 vs 9.9 m/s on a 45° push). Ledges between 0.36 and 2 m are still entered. Use as a reference implementation only. |
| ~~`systems-1-fire-interval-remainder.patch`~~ | **WITHDRAWN (deleted)** | RE TARGETED_PASS #6: the original refire timer is non-looping and re-armed, uses a strict `Rate < Count`, and **discards overshoot**. The original cadence is therefore 0.0667 s ≈ **900 RPM** at 30/60 Hz. The rebuild's 900 RPM is correct; carrying the remainder (923 RPM) would have been a fidelity regression. |

To score any future patch: `ab.ps1 -Ref <branch> -Patch <file>` then `diff-reports.ps1 <branch> <branch+patch>`.
`ab.ps1` stops with "patch did not apply" when a branch has moved on; regenerate the patch against the new head.

## History (pre-RE validation, kept for reference)
Gameplay 1+2+3 on agents/gameplay 21902b6 / 8045676: 109 → 114 pass, 7 → 2 known, 0 FAIL.
The Streets `--map` sweep was unchanged (137.6 vs 138.4 m, no fall-through).
