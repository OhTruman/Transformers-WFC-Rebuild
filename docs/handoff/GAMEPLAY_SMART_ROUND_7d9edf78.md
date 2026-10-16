# Smart round on integration 7d9edf78: tip-over cost and squad follow reversals

Gameplay, 2026-10-16 05:42. **No code is handed off.** None of the four candidate fixes passed, so the shipping Smart default stays as it is. Classic was byte-exact in every arm that ran it (48/48 SUMMARY lines per arm).

## How it was measured

- **Runs:** WFC_NAVQTEST, the same env as `GAMEPLAY_NAVQ_BASELINE_069a0a8f.md`. Null render, muted, one exe at a time, about 5 s per run.
- **Build check:** 7d9edf78 reproduces the 069a0a8f baseline lines exactly (16/16 sampled runs, both AIs).
- **New noise gate:**
  - Per column, z = delta / sqrt(SE_ref² + SE_arm²) over per-seed values.
  - Script: `work/pass29/navq_cmp.js`, which now prints z.
- **Why the old gate was dropped:** the old "|mean(s7-12) - mean(s1-6)|" reference came out implausibly small on some columns (Debris slow 0.07 %), so arms were flagged that were inside the per-seed spread.
- **Seed extension:** the baseline was extended to seeds 13-24 (`work/pass29/c09`).

## 1. The tip-over gate's cost is within noise

Self-righting on (the shipping default) against `WFC_BOTTIP=-1` (off), Smart, 24 seeds:

| Map | slow % | reversals | off-mesh % | stucks |
|---|---|---|---|---|
| Streets | 6.31 → 6.19 (z −0.1) | 5.04 → 4.93 (z −0.1) | 5.02 → 5.42 (z 0.4) | 1.74 → 1.70 (z −0.1) |
| Molten | 2.00 → 1.79 (z −0.7) | 3.88 → 3.11 (z −1.5) | 1.95 → 1.96 (z 0.1) | 0.74 → 0.60 (z −1.6) |
| Debris | 4.37 → 3.48 (z −1.5) | 3.13 → 2.74 (z −0.9) | 2.58 → 1.99 (z −1.1) | 1.10 → 0.96 (z −0.9) |
| Gorge | 4.55 → 5.52 (z 1.5) | 4.65 → 5.30 (z 1.2) | 5.11 → 5.81 (z 0.9) | 1.54 → 1.57 (z 0.1) |

- **The baseline's own spread:** the same build moves this much between seed groups 1-12 and 13-24.
  - Gorge stucks: 1.44 vs 1.69.
  - Gorge off-mesh: 5.20 vs 6.42 %.
  - Streets off-mesh: 4.91 vs 5.94 %.
- **The reported regressions are the same size as that spread.** The 12-seed deltas were Gorge stucks 1.26 → 1.44 and Streets off-mesh 3.85 → 4.91.
- **Across the 24 seeds no column is significant.**
  - Gorge slow % is the largest at z 1.45.
  - Molten and Debris lean better with righting on.
- **Recommendation:** keep the shipping self-righting. There is no movement cost to win back.

## 2. Squad follow reversals: diagnosis and the four arms

### Diagnosis

Diagnostics ran on a scratch copy only. The switches were WFC_FOLLOWLOG, WFC_STUCKLOG, WFC_INTENTLOG and REVDIAG.

- **Two mechanisms coincide with most of Smart's reversals.** Within 1 s before a reversal:
  - A **barrier slide** had acted in 20-51 % of cases, depending on the map.
  - A **local-avoidance push** around another pawn had acted in 39-54 %.
- **A traced barrier case** (Gorge seed 3, p4):
  - A wedge slot sat beside two deployed barriers.
  - The barrier slide sends a bot that meets a wall end-on to the wall's "nearer end", which is behind it.
  - So the member walked in for 0.2 s, backed off for 0.3 s, and repeated.
- **The passing-side mechanism:**
  - The avoidance push sums up to 2 per pawn sideways (at most about 63°).
  - Its side flips when a pawn crosses the line, which gives a zig-zag of more than 120°.
- **The 15 m switch:** members flip between their wedge slot behind the leader and the leader's destination ahead, at exactly squadRegroupM.

### Arms tried (Smart-only, Classic byte-exact)

| Arm | Change | Result |
|---|---|---|
| bar (v1) | Barrier slide picks the open way toward the corner (along / across) and skips a corner inside a wall | Molten stucks z 2.7; Debris slow / off-mesh / stucks worse |
| bar2 (v2) | Overrides only when the Classic slide points back, toward a walkable direction | Debris slow z 2.9, off-mesh z 2.9, stucks z 2.2 |
| passside v1 | Avoidance keeps its side 1 s; sideways push capped at 1 | Follow reversals down (Debris z −2.1, Gorge z −1.8); Streets / Molten slow and stucks up |
| passside v2 | Side hold only, no cap | Follow reversals up on Streets / Molten / Gorge; Debris z 2.1-2.6 worse |
| bar2 + passside v2 | Both | Within noise everywhere except Debris follow z −2.8 and Molten stucks z 2.5 |
| wedge hysteresis (24 seeds) | Members keep the wedge out to squadRegroupM + 6 m | No column significant; follow +0.58 on Gorge (z 1.3) |

- **The correlation was not causation.** Removing the flip did not remove the reversals.
- **No arm improves follow reversals without costing movement somewhere else.** So nothing ships.
- **The rest of the follow rise is formation following itself.** The slot moves as the leader turns or stops. That would need a different formation design, such as path-based slots; trail following was already tried and measured worse on Streets.

### Other observations

- **Tipped stucks** are 8-9 % of stuck events on Streets / Gorge. Most come within 1 s of tipping, when the stuck recovery jump fires. The total is within noise (above), so I did not act on it.
- **The barrier slide's backward move from a wall end also affects Classic.** It is left as is, because Classic must stay byte-exact.

## Files

- **Arms:** `work/pass29/c02`-`c10`.
- **Diagnostic copies:** `work/int_7d9_wk` (diagnostic edits `work/edit_*diag.js`) and `work/int_7d9_fx*`.
- **Kept off-patch:** none of these diagnostics is in a patch.
