# RT perf baseline - m9c_398b27e, 2560x1440 windowed, 31 v 32 bots + player bot, lockstep steps 2400-6000

Run conditions: WFC_NOINTERACT=1, WFC_MUTE=1, WFC_NOMOUSE=1 (pinned per launch by Add-WfcHarnessEnv; the header read the process env). (2026-10-16: rows under WFC_NOINTERACT are their own reference series - window pacing differs - never diff them against pre-NOINTERACT rows.)

GPU ms per RT pass = the product's RT summary averages over the run; frame p50 / p90 over the measured steps (ms).

| map | mode | frame p50 | frame p90 | shadow+AO | denoiser | reflections | GI tracer (per traced frame) | RT VRAM MB | frames |
|---|---|---|---|---|---|---|---|---|---|
| 504 | off | 1.89 | 2.31 |  |  |  |  |  | 3600 |
| 504 | faithful | 4.97 | 7.21 | 2.034 | 0.785 | 0 |  | 4201.29 | 3600 |
| 504 | realistic | 9.35 | 11.44 | 3.271 | 1.116 | 1.391 | 3.34 | 4754.633 | 3600 |
| 507 | off | 2.07 | 2.3 |  |  |  |  |  | 3600 |
| 507 | faithful | 4.19 | 5.41 | 1.919 | 0.727 | 0 |  | 3622.933 | 3600 |
| 507 | realistic | 7.44 | 8.49 | 2.871 | 0.941 | 1.291 | 6.697 | 3978.762 | 3600 |
| 508 | off | 1.6 | 1.96 |  |  |  |  |  | 3600 |
| 508 | faithful | 4.52 | 5.87 | 2.215 | 0.746 | 0 |  | 3650.319 | 3599 |
| 508 | realistic | 9.19 | 11.69 | 3.951 | 1.163 | 1.934 | 4.36 | 3981.924 | 3600 |
| 509 | off | 1.83 | 2.09 |  |  |  |  |  | 3600 |
| 509 | faithful | 5.03 | 6.35 | 2.237 | 0.755 | 0 |  | 3767.295 | 3600 |
| 509 | realistic | 9.71 | 11.94 | 3.973 | 1.094 | 1.758 | 4.441 | 4052.319 | 3599 |
| 510 | off | 1.92 | 2.26 |  |  |  |  |  | 3600 |
| 510 | faithful | 4.61 | 6.16 | 2.276 | 0.75 | 0 |  | 3489.562 | 3600 |
| 510 | realistic | 8.94 | 11.16 | 3.81 | 1.122 | 1.481 | 4.199 | 3923.181 | 3600 |

Notes (2026-10-16 03:09-03:24, package-5 head 398b27ef, alone on the card under PERF_HOLD, 0 contamination): the NEW reference
series (WFC_NOINTERACT). Every RT arm played the same match as Off on all 5 maps (SIMHASH). Transient muzzle / death lights ON
(package-5 decision; their RT-off cost is documented in the package-5 regress: +0.03 to +0.23 ms p90). Off rows match the pre-
NOINTERACT 30b2fa74 series within 0.07; Faithful / Realistic rows are higher partly from the run conditions and partly from items
1-4 + reflections + the lights - the series are not comparable (see the 30b2fa74 file's header).
