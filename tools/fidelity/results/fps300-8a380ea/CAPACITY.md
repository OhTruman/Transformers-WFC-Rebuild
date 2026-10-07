# Capacity stress

build: `8a380eadc04d9859615c75ec9bea978d25b21808` (Release); TDM / DM private matches, TimeLimit 60 s, two matches per population in one process; difficulty 1; walk + strafe + periodic jump scripted player. Second-match numbers are the warm-cache comparison.

| res | map | pop | match | spawned | p50_ms | p90_ms | p95_ms | p99_ms | pct_under_3_33 | cpu_submit_ms | gpu_wait_ms | max_ms | over33 | over50 | sim_p50_ms | sim_p99_ms | ai_avg_ms | loaded_mb | unloaded_mb | voices_max | voices_dropped | kills | broken_bots | struggling_bots | end_reason |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1920x1080 | 508 | p32v32 | 1 | 64/64 | 2.99 | 7.68 | 13.4 | 16.39 | 59.1 | 14.77 | 1.09 | 27.12 | 0 | 0 | 0 | 3.94 | 0.855 | 5010 | 4269 | 96 | 18 | 69 | 0 | 0 |  |
| 1920x1080 | 508 | p32v32 | 2 | 64/64 | 20.84 | 24.04 | 25.12 | 26.58 | 0 | 14.77 | 1.09 | 46.3 | 2 | 0 | 3.74 | 7.22 | 0.868 | 5053 | 4316 | 97 | 378 | 83 | 0 | 0 |  |
| 2560x1440 | 508 | p32v32 | 1 | 64/64 | 21.75 | 27.07 | 28.6 | 30.83 | 0 | 13.67 | 1.26 | 52.38 | 4 | 1 | 4.25 | 8.91 | 0.908 | 4933 | 4218 | 98 | 610 | 76 | 0 | 0 |  |
| 2560x1440 | 508 | p32v32 | 2 | 64/64 | 21.6 | 25.36 | 26.57 | 28.44 | 0 | 13.67 | 1.26 | 33.13 | 0 | 0 | 3.92 | 7.65 | 0.841 | 4994 | 4271 | 98 | 849 | 88 | 0 | 0 |  |
