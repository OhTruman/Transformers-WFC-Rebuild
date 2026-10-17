# Frontend -> match load stall (2026-10-17 07:36, x)

| mode | map | reached match | match load without frames, ms (sum) | largest in the match load | largest gap of the boot | watchdog stall | dumps | contaminated | settings line |
|---|---|---|---|---|---|---|---|---|---|
| real | Seed Of Corruption | True | 7870 | 3697 ms at 'World: map mesh upload' | 3697 ms at 'World: map mesh upload' | none | 0 | True | FLOW display.rayTracing mode=2 |

Note (Integration 2026-10-17): the contamination flag on the Realistic boot was Systems' pid 20068 READING Rebuild/work/render (bsp / decals copies; output in Rebuild-Systems/work/matA/out; no file under Rebuild/work/render changed since 06:00) - shared disk reads, not changed data; the gap numbers stand. The boot also landed on Seed Of Corruption (autoplay map-selector race), so the 4-mode rerun on Streets replaces it.
