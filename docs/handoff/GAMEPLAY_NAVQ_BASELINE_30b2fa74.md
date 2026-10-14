# NAVQ baseline: integration 30b2fa74 (2026-10-14)

Shipping numbers for later bot-movement rounds: compare against this file instead of rerunning shipping. The raw per-run NAVQ lines are in `GAMEPLAY_NAVQ_BASELINE_30b2fa74.txt`.

**Build:** integration head 30b2fa74. It contains squad tactics (15b89a75, Smart default `squadTactics = 2`) and the lockstep input drop (c7f72c50).

## How it was run

- **Env per run:** `WFC_NULLRENDER=1 WFC_MUTE=1 WFC_AUTODETECT=0 WFC_SIMTHREADS=2 WFC_NAVQTEST=1 WFC_BOTAI=<smart|classic> WFC_MAP=<map> WFC_SEED=<1..6>`. Run from the direct-boot run dir, CPU only.
- **Maps:** MP_IAC_Streets, MP_KON_Molten, MP_ORB_Debris, MP_UND_Gorge.
- **Table:** `node work/pass29/navq_table.js <dir>`. Values are means ± SE over the 6 seeds.

## Regression safety

The runs are deterministic, so a later run with the same env must reproduce the "NAVQ SUMMARY" lines in the .txt exactly. A difference means behaviour changed.

| Arm | Reproduces | Matches |
|---|---|---|
| Smart | Gameplay's dev-tree default-A runs (pass29 b88) | 24/24 |
| Classic | The pre-squad-tactics Classic logs (pass29 b62; the dither-escape field excluded, it was added later) | 24/24 |

Classic is unchanged by squad tactics.

## Per-map table

slow % / reversals per travel-min / off-mesh % / idle5 / stucks / repaths / failed:

```

MP_IAC_Streets   (n = seeds)
arm                  slow%    rev/min   offmesh%      idle5     stucks    repaths     failed   n
classic           6.90±1.6   3.30±0.8   5.86±1.3   0.02±0.0   1.48±0.2  52.98±1.9   0.59±0.0   6
smart             5.88±0.7   5.33±1.3   3.97±0.9   0.18±0.0   1.56±0.2  51.21±2.0   0.82±0.3   6

MP_KON_Molten   (n = seeds)
arm                  slow%    rev/min   offmesh%      idle5     stucks    repaths     failed   n
classic           1.77±0.4   2.00±0.5   1.46±0.3   0.01±0.0   0.84±0.3  48.93±2.1   2.62±1.9   6
smart             1.83±0.6   3.44±0.6   2.02±0.4   0.27±0.1   0.69±0.1  50.83±1.9   2.20±0.8   6

MP_ORB_Debris   (n = seeds)
arm                  slow%    rev/min   offmesh%      idle5     stucks    repaths     failed   n
classic           3.83±0.8   1.23±0.3   2.08±0.3   0.05±0.0   1.09±0.1  51.41±2.0   4.68±3.1   6
smart             4.73±0.7   2.98±0.6   1.91±0.4   0.10±0.0   1.08±0.2  48.86±1.9   1.34±0.6   6

MP_UND_Gorge   (n = seeds)
arm                  slow%    rev/min   offmesh%      idle5     stucks    repaths     failed   n
classic           5.85±2.1   5.01±2.3   7.04±2.7   0.10±0.1   2.31±1.1  57.04±1.6   1.90±0.7   6
smart             4.62±0.5   4.59±0.5   5.00±0.9   0.17±0.0   1.23±0.2  53.65±1.7   1.68±0.6   6
```

## Smart reversals split

Per travel-min: pathing / follow (a member within 30 m of its leader) / fidget (within 6 m of a leader slower than 1 m/s).

| Map | Pathing | Follow | Fidget |
|---|---|---|---|
| Streets | 2.50 | 2.23 | 0.60 |
| Molten | 1.27 | 1.51 | 0.66 |
| Debris | 1.28 | 1.10 | 0.60 |
| Gorge | 2.10 | 1.95 | 0.55 |

Classic has no squads, so all of its reversals count as pathing.

**The next round targets the follow part** (after the playtest). The pre-tactics Smart reversal totals were Streets 3.16 / Molten 1.99 / Debris 1.29 / Gorge 3.94 (pass29 b62).

## Smart squad cohesion

| Metric | Value |
|---|---|
| Settled member-to-leader mean | 43.6 m |
| Settled, within 15 m | 41.2 % |
| In fight, mean | 61.5 m |
| In fight, within 20 m | 25.2 % |
