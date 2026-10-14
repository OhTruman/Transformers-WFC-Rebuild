# RT perf baseline - m9c_30b2fa7, 2560x1440 windowed, 31 v 32 bots + player bot, lockstep steps 2400-6000

GPU ms per RT pass = the product's RT summary averages over the run; frame p50 / p90 over the measured steps (ms).

| map | mode | frame p50 | frame p90 | shadow+AO | denoiser | reflections | GI tracer (per traced frame) | RT VRAM MB | frames |
|---|---|---|---|---|---|---|---|---|---|
| 504 | off | 1.84 | 2.29 |  |  |  |  |  | 3600 |
| 504 | faithful | 4.85 | 5.53 | 2.036 | 0.747 | 0 |  | 4122.324 | 3600 |
| 504 | realistic | 7.77 | 8.61 | 2.788 | 0.993 | 1.331 | 3.467 | 4629.338 | 3600 |
| 507 | off | 2.14 | 2.35 |  |  |  |  |  | 3600 |
| 507 | faithful | 4.45 | 5.23 | 1.916 | 0.746 | 0 |  | 3578.548 | 3600 |
| 507 | realistic | 6.99 | 7.83 | 2.631 | 0.923 | 1.275 | 6.643 | 3856.09 | 3600 |
| 508 | off | 1.6 | 1.96 |  |  |  |  |  | 3600 |
| 508 | faithful | 4.7 | 5.77 | 2.235 | 0.756 | 0 |  | 3603.857 | 3600 |
| 508 | realistic | 8.28 | 9.38 | 3.238 | 1.109 | 1.932 | 4.454 | 3863.633 | 3600 |
| 509 | off | 1.82 | 2.09 |  |  |  |  |  | 3600 |
| 509 | faithful | 5.25 | 6.21 | 2.269 | 0.77 | 0 |  | 3720.7 | 3600 |
| 509 | realistic | 8.68 | 9.88 | 3.286 | 1.036 | 1.768 | 4.567 | 3938.105 | 3600 |
| 510 | off | 1.93 | 2.31 |  |  |  |  |  | 3600 |
| 510 | faithful | 4.85 | 5.93 | 2.302 | 0.756 | 0 |  | 3441.238 | 3600 |
| 510 | realistic | 7.96 | 9.04 | 3.07 | 1.069 | 1.488 | 4.332 | 3795.067 | 3600 |

Notes (2026-10-14, 12:26-12:55, alone on the card under PERF_HOLD): every RT arm played the same match as Off over the measured
range (SIMHASH, all 5 maps). Rust was rerun: its first pass overlapped AssetTools python that predated the hold (Off p90 4.10 vs
2.29 clean; first pass kept in work/perf/rt_baseline_30b2fa7/contaminated_1226). Faithful reflections 0 = no RT reflection pass
in Faithful. GI tracer = GPU ms per frame that traced a slice (R4 sweeps stop after WFC_RT_GISWEEPS); RT VRAM = the RT device's.
Re-run for a diff: tools/fidelity/rt-perf-baseline.ps1 -Root work\ab\<build> -OutDir <dir>.
