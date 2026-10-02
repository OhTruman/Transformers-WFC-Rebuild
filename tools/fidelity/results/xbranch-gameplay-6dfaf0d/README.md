# Validation: agents/gameplay 6dfaf0d (Pass 14, native RE M03 reconcile), product-neutral

**What was run:**
- Built in isolation (`ab.ps1 -Ref 6dfaf0d -Measure`), harness overlaid, nothing merged.
- Provenance: `RE-Workspace/notes/MILESTONE03_VEHICLE_NATIVE_FIDELITY.md` (P1–P10) and Gameplay FIDELITY.md PASS 14.
- All measurements are independent: the harness drives the production `PlayerController::applyToPawn`
  path on its own synthetic terrain.
- Gameplay's own `WFC_VEHTEST=1` was also run; its output is in `gameplay_vehtest.log` and agrees on every
  overlapping item.

| tree | PASS | FAIL | KNOWN | INFO | SKIP |
|---|---|---|---|---|---|
| **6dfaf0d** (`harness.json`) | **325** | **0** | 15 | 365 | 0 |
| milestone-02 baseline, same harness | 267 | 0 | 66 | 363 | 1 |

Same harness milestone-02 → 6dfaf0d: **51 FIXED, 0 regressed** (`diff_m02_vs_6dfaf0d.txt`).

## Measured (6dfaf0d)
| item | result | evidence class |
|---|---|---|
| rest COM height | 1.2872 m (L_eq 1.28725) | algorithm CONFIRMED, mass link HIGH |
| probe contacts / spring length at rest | 4 / 1.287 m | CONFIRMED |
| free fall (probes out of reach) | −19.404 m/s², 0 contacts | CONFIRMED (RB gravity, zero RB linear damping) |
| springs extended inside reach | −9.50 m/s² (push-only, never pulls) | CONFIRMED |
| release 15 m/s forward / sideways | 0.500 s / 0.500 s | CONFIRMED (one 3000 clamp) |
| 0.25 m / 0.5 m riser at 15 m/s | no one-frame snap; pitch 4.0° / 8.1°; COM excursion 0.28 / 0.52 m; rests at 1.287 on top | CONFIRMED mechanism; magnitude is a human check |
| 10 m drop | impact −18.3 m/s; ride height recovered in 0.98 s; min COM 0.60 m | recovery CONFIRMED; min clearance PROVISIONAL (hull) |
| airborne upright | pitch ×0.950 per 30 Hz tick | CONFIRMED (airborne only) |
| camera 90° step | heading = rendered camera yaw (max 0.06°); camera yaw t90 0.10 s; velocity re-aims in 0.70 s | CONFIRMED (heading); camera smoothing is a human check |
| hover jump standing / moving | Δvz +12.00, horizontal kept, pitch kick −1.00 rad/s; apex +3.78 m | CONFIRMED (apex INFO) |
| boost jump | Δv up +13.5 (14 − one step of g), forward +6.4; pitch rate 1.8 rad/s | CONFIRMED (pitch rate INFO) |
| Dash with RIGHT held | forward 30.0, lateral 0.008, vertical 0.000; exit forward 15.0 / lateral 0.008 | CONFIRMED |
| boost cap | 29.94 m/s over 6 s | CONFIRMED cap; rise PROVISIONAL (tire model) |
| nitro over 3 s from the cap | 43.5 m/s (cap 45) | INFO: the rise depends on the PROVISIONAL tire model |
| fine aim, level pitch | camera +2.00 m back, 0.00 lateral, 0.00 vertical, FOV 45 | CONFIRMED |
| hand shrink | equals "weapon attached + drawn" on 194/194 robot steps | CONFIRMED |
| transform notifies | R→V vehicle shows 0.417 s (notify 0.3958), robot hides 0.900 (0.8796); V→R robot shows 0.117 (0.0984), vehicle hides 0.683 (0.6634); both meshes inside the windows only; no usable weapon without a visible gun; drawn root continuous; arm mesh drawn while no weapon | CONFIRMED (±1.5 steps at 60 Hz) |
| ram reaction | robot 50.0 m/s for 0.5 s, then horizontal 0; vehicle AddVelocity ×0.5 | CONFIRMED. Victim **selection** (TnPawn only) is UNTESTED: the harness cannot create a pawn victim |

`traces/` holds the per-step CSVs of each probe: attitude, springs, COM height, visibility.
