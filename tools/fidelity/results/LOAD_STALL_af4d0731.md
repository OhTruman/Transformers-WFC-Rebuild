# Frontend -> match load stall (2026-10-17 07:55, F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_af4d073\build-release\bin\wfc_rebuild.exe)

| mode | map | reached match | match load without frames, ms (sum) | largest in the match load | largest gap of the boot | watchdog stall | dumps | contaminated | settings line |
|---|---|---|---|---|---|---|---|---|---|
| off | Streets | True | 1294 | 692 ms at 'Render: load step' | 692 ms at 'Render: load step' | none | 0 | True | FLOW display.rayTracing mode=0 |
| faithful | Streets | True | 2784 | 679 ms at 'Render: load step' | 679 ms at 'Render: load step' | none | 0 | True | FLOW display.rayTracing mode=1 |
| real | Streets | True | 7510 | 4955 ms at 'World: map mesh upload' | 4955 ms at 'World: map mesh upload' | none | 0 | True | FLOW display.rayTracing mode=2 |
| realhd | Streets | True | 5987 | 4213 ms at 'World: map mesh upload' | 4213 ms at 'World: map mesh upload' | none | 0 | True | HD texture pack available: 3505; FLOW display.rayTracing mode=2; Modern materials on for MP_IAC_Streets |
