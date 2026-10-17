# Hover / self-righting: result and decision (package 7 doc entry)

Gameplay, 2026-10-17. Edits in `docs/handoff/hover/` (on af4d0731), applied in this order:
1. `edit_hover_faithful.js`;
2. `edit_navq_tip.js` (the NAVQ TIP yardstick);
3. `edit_hover_ai_path.js`;
4. `edit_hover_smart_steady.js`.

The RE source is `GAMEPLAY_HOVER_RE.md`; the decisions are Integration's, 2026-10-17.

## What changes

- **The four-probe hover model is faithful for every vehicle.**
  - It was verified against RE: COM rest heights car 1.090 m = 109.0 UU, truck / Optimus 1.287 m = 128.7 UU (VEHTEST rest rows).
  - The authored suspension values match.
  - **Fix:** the tank's spring force had the car's normal factor; the original applies max(0, F).
- **Classic bots run the original AI hover path** (the fidelity reference; a proven gap, user-delegated decision): RotateToTargetOrientation
  gated by the absolute world |pitch| / |roll| > 30° (IsUnstable), rate-limited by AiMaxAngularSpeed / Accel, RB angular damping 0.1,
  and **no** player UpdateTurn pitch / roll pull and **no** UpdateRoll.
- **Smart bots keep the steadier righting on top of the faithful springs** (the player's 5 % pull, UpdateRoll, the contact-relative
  gate). **This is a deliberate Smart improvement over the original:** the faithful path gave Smart about 3× more tilts past 30°, a look
  regression for the AI that exists to look and play better.

## Measured (NAVQ, null render, 24 seeds × 4 maps; tipped = the share of hover-vehicle time past 30°)

| Map | AI | tipped % base → final | longest tip s base → final | stucks / bot-min base → final |
|---|---|---|---|---|
| Streets | Classic | 5.19 → 2.14 | **213.8 → 9.0** | 1.99 → 1.40 |
| Molten | Classic | 1.13 → 1.75 | 13.9 → 12.8 | 0.87 → 0.93 |
| Debris | Classic | 1.50 → 2.40 | 9.8 → 14.0 | 1.06 → 1.06 |
| Gorge | Classic | 1.69 → 1.57 | **93.1 → 5.1** | 1.65 → 2.06 (sd 1.55) |
| Streets | Smart | 0.84 → 1.00 | 2.0 → 4.2 | 1.42 → 1.31 |
| Molten | Smart | 0.67 → 0.77 | 4.6 → 7.4 | 0.79 → 0.87 |
| Debris | Smart | 0.89 → 0.76 | 5.4 → 1.8 | 1.08 → 0.82 |
| Gorge | Smart | 0.72 → 0.91 | 1.8 → 2.5 | 1.21 → 1.52 (sd 0.91) |

- Off-mesh changes are within one standard deviation everywhere.
- The intermediate "faithful for Smart too" arm, for the record: Smart tipped 2.1-3.1 %, episodes about 2×, longest 4.5-10 s.
- Full tables: `docs/handoff/hover/tip_summary_24seeds_split.txt` (base / faithful-for-both / final).

## Identity

- **Classic SIMHASH changes** (a deliberate fidelity change). One re-baseline, batched with the squad item.
- Smart changes slightly too (the tank force; the shared springs).
