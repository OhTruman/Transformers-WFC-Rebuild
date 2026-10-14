# M07 render-state after overlays

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m6b\build\bin\wfc_rebuild.exe`

A world of only HUD / Optimus / effects FAILS on pixels even when draw counts look normal; GL state left by an overlay FAILS even when the renderer re-asserts it (both defences must hold).

| variant | transition | status | world | renderer | gl_entry |
|---|---|---|---|---|---|
| normal | t1_after_charselect | FAIL | FAIL (0.066) | ok (world 1116, bsp 228) | depth test off |
| normal | t3_after_resume | FAIL | FAIL (0.037) | ok (world 183, bsp 72) | depth test off |
| normal | t5_after_respawn | FAIL | PASS (0.231) | ok (world 1183, bsp 240) | depth test off |
| normal | t7_second_match | FAIL | PASS (0.358) | ok (world 2431, bsp 768) | depth test off |
| normal | t8_second_match_later | FAIL | PASS (0.259) | ok (world 984, bsp 375) | depth test off |
