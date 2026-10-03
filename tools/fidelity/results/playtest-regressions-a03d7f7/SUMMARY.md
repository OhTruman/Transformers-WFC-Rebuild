# Long validation pass: integration/milestone-04 a03d7f7 (+ Rendering candidate b21b65d)

Validation and tooling only. No product source was changed. Product builds were isolated `ab.ps1` exports:
- `m4int` = a03d7f7;
- `m4ren` = a03d7f7 + agents/rendering b21b65d, composed with `git merge-tree` (no refs touched).

Status words: PASS / FAIL / KNOWN / INFO / HUMAN (human check required, see `HUMAN-CHECK.md`).

The machine was shared with another lane's game process, so the absolute timings are unreliable. Every visual result
below is an A/B comparison within one build: with vs without the material, or with vs without map FX.

## Phase 1: playtest regression suite (`playtest-regressions.ps1`)
| item | result | evidence |
|---|---|---|
| Smoke veil at distance | **HUMAN** | BckSillouhetteSmoke covers 56–100% of the view at 30–60 m; steam covers 47–100% at 30 m. On Rendering b21b65d the steam veils drop (e.g. 62% → 32%) and steam turns purple and soft. |
| Smoke changing up close | **HUMAN** | Steam covers 100% of the view at 2–6 m, so the 2 m / 6 m ratio stays flat. Whether it feels like walking into steam is a person's call. |
| Red/purple smoke | **HUMAN** | FogSheet *Blue* contributes red hue in many views (`*.hue`). |
| Static particles | **HUMAN** | Fog sheets nearly static (motion < 0.5). Rendering states fog sheets are static by design and BckSillouhetteSmoke pans. |
| Glass depth ordering | **HUMAN** | Light glass floors contribute 0% from above and 59–80% from below (one-sided?). Unchanged on b21b65d. The film may sit below the 2-luma threshold. |
| Geometry in front of transparent floors | **HUMAN** | Same glass views. Rendering's M06 pass draws all opaque geometry first, then translucent back to front. |
| Black geometry behind ramps | **INFO** | No near-black view among the ramp views or the 525 sweep shots. |
| Boost → robot falling under the map | **FAIL**, reproduced | See phase 3. |

Files: `p1_*_report.json`, `p1_sheet_*.jpg`, `p1_rendering_b21b65d_*`.

## Phase 2: map visual sweep (`streets-sweep.ps1`)
- 525 shots on a03d7f7, from navigation nodes, edge samples and a 12 m × 3 m grid, with 4 views each plus sky
  views and targeted mover / FX / character views.
- Every category is covered. No near-black views.
- Before/after on Rendering b21b65d: `p2_sweep_rendering_vs_int04.csv` and `p2_sweep_sheet_changed.jpg`. The changes
  concentrate in the steam / fog views.

## Phase 3: transform stress (`transform-stress.ps1`): **FAIL**
756 runs = 84 starts × 9 scenarios. Results: 561 PASS, 68 INFO legal large drops, **127 FAIL fell_through**.

| scenario | fell through | legal large drops |
|---|---|---|
| robot→vehicle (stationary, walk, airborne) | 0 | 0 / 10 / 14 |
| vehicle→robot stationary | 0 | 0 |
| vehicle→robot drive | 0 | 9 |
| vehicle→robot **boost** | **40 / 84** | 6 |
| vehicle→robot **boost + turn** | **41 / 84** | 7 |
| vehicle→robot **nitro** | **37 / 84** | 6 |
| vehicle→robot airborne | 9 / 84 | 16 |

- **Pattern.**
  - The pawn crosses the floor 6 frames after the press (frame 156 in 113/127 cases), which matches the form /
    collision swap.
  - It starts from a grounded vehicle in 119/127 cases.
  - The rate rises with speed: 1% below 5 m/s, 19% at 15–20, 40% at 20–25, 67% at 25–30 m/s.
  - It happens at 43 of 84 starts, on floors 0.09–25.6 m thick; only 15 cases are on a thin floor. So this is not a
    thin-floor tunnelling artifact.
- **Verdict rule.** FAIL only when, at the crossing frame, collision geometry exists at the press floor height
  (±0.6 m) at that xz, according to `collision_query.py` over the map collision GLB.
- **Owner:** Gameplay (vehicle→robot transform at speed).

Files: `p3_transform_stress.csv` (per run: press floor, thickness, penetration, crossing frame, tags) and
`p3_transform_stress_report.json`.

## Phase 4: mode presentation (`mode-presentation.ps1`)
9 PASS, 0 FAIL, 3 HUMAN, 9 INFO.
- Bases appear only in CTF and EXT; totems only in DOM.
- Exactly one KOTH ring is visible (12369), matching Gameplay's active zone.
- Pickups are identical in all modes; TDM equals DM.
- The flag and bomb effects could not be separated from the bases and nearby emitters by measurement: HUMAN.

Files: `p4_modes.csv`, `p4_sheet_*.jpg`.

## Phases 5–6: TDM match and frontend validators
- `match-validator.ps1` and `frontend-validator.ps1` both pass their self-tests.
- On a03d7f7: spawn class PASS in DM and TDM; the rest SKIP, because the product had no match or frontend events.
- Superseded for the frontend by the Milestone 05 gate (`m05-e2e-gate.ps1`), which reads the real `WFC_FLOWLOG`
  trace of agents/frontend.

## Phase 7: performance / leaks
- `soak-monitor.ps1` was **not run** in this pass: the machine was shared with another lane's game.
- Repeated-load memory is now measured by the M05 gate's loop run (`LIFETIME.memory_per_cycle`).
