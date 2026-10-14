# M07 render-state after overlays

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin\wfc_rebuild.exe`

A world of only HUD / Optimus / effects FAILS on pixels even when draw counts look normal; GL state left by an overlay FAILS even when the renderer re-asserts it (both defences must hold).

| variant | transition | status | world | renderer | gl_entry |
|---|---|---|---|---|---|
| normal | t1_after_charselect | PASS | PASS (0.207) | ok (world 1114, bsp 228) | sane |
| normal | t3_after_resume | PASS | PASS (0.231) | ok (world 212, bsp 82) | sane |
| normal | t5_after_respawn | PASS | PASS (0.154) | ok (world 1177, bsp 240) | sane |
| normal | t7_second_match | PASS | PASS (0.269) | ok (world 2433, bsp 767) | sane |
| normal | t8_second_match_later | PARTIAL | PARTIAL (0.136) | ok (world 998, bsp 376) | sane |
| normal | t9_display1080 | PARTIAL | PARTIAL (0.104) | ok (world 46, bsp 27) | sane |
| normal | t9b_display720 | PASS | PASS (0.151) | ok (world 27, bsp 20) | sane |
