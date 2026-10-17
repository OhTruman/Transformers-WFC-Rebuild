# Transforming actors + dying-vehicle collision: re-baseline, 2026-10-17

Patch: docs/handoff/GAMEPLAY_TRANSFORMING_ACTORS.patch, against integration 104b31cc (scripts: docs/handoff/transact/).

## What changes the sim

- **Dying-vehicle bodies** (every map where a vehicle dies): VehicleDying keeps a blocking body until the gib (car / tank U(0.3, 0.75) /
  U(0.2, 0.5) s, jets U(4, 5) s).
- **Transforming actors** (only the 3 maps with them):
  - Rust: both doors animate open at level start in every mode (authored).
  - BrokenHope / Remnant in versus (Integration option c): doors open from the start, BrokenHope's bridges retracted (no collision).
  - Closed doors in Escalation (not in the rebuild yet) block nav cells and recompute the nav pieces.

## NAVQ (WFC_SIMHASH), base int_tab (104b31cc) vs new int_ta

- **4 user maps, 12 seeds x both AIs** (work/pass29/ta/navq): every run diverges (vehicle deaths).
  - Health: Classic travel 44.9 -> 44.8 %, speed 15.77 -> 16.02, slow 5.1 -> 4.5 %, reversals 3.02 -> 2.56, off-mesh 3.92 -> 3.31 %.
  - Smart: travel 39.1 -> 39.5 %, speed 15.01 -> 14.80, slow 3.2 -> 3.9 %, reversals 3.38 -> 3.68, off-mesh 2.43 -> 3.21 %.
  - Tables: *_SIMHASH_old_vs_new.txt, *_NAVQ_REFERENCE_new.txt.
- **Door maps, 6 seeds x both AIs**, final rule (work/pass29/ta/navq3 and navq4):
  - BrokenHope: byte-identical to base (SIMHASH 0 diff; the open doors never touch a path, no vehicle death in these runs).
  - Remnant Smart: travel 66.3 -> 65.5 %, reversals 0.99 -> 0.94; Classic equal.
  - Rust: Smart reversals 4.37 -> 5.84 (n = 6; its doors animate open as authored, plus the dying-vehicle bodies).

## TRANSACTTEST (WFC_MAP=...)

- BrokenHope 3/3: 13 doors open with their doorways clear; 15 bridge boxes retracted with no collision.
- Remnant 2/2: 12 doors open.
- Rust 3/3: the level-start doors open; the dying-vehicle body blocks until the gib.
- Streets 2/2.
- The dying-vehicle check is skipped on BrokenHope / Remnant: the harness opponent gets no start there.

## Draw

World::transformingActors() is agreed with Rendering (glb, clip, matrix, t / length, hidden; fixed order per level). The patch merges in
the same head as Rendering's door draw.
