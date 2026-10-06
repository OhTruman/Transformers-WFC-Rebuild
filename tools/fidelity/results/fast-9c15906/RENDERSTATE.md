# M07 render-state after overlays

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin\wfc_rebuild.exe`

A world of only HUD / Optimus / effects FAILS on pixels even when draw counts look normal; GL state left by an overlay FAILS even when the renderer re-asserts it (both defences must hold).

| variant | transition | status | world | renderer | gl_entry |
|---|---|---|---|---|---|
| normal | t1_after_charselect | PASS | PASS (0.323) | ok (world 1767, bsp 450) | sane |
| normal | t3_after_resume | PASS | PASS (0.247) | ok (world 1058, bsp 313) | sane |
| normal | t5_after_respawn | PASS | PASS (0.186) | ok (world 852, bsp 251) | sane |
| normal | t7_second_match | PASS | PASS (0.344) | ok (world 2168, bsp 766) | sane |
| normal | t8_second_match_later | HUMAN | FAIL (0.187) | ok (world 736, bsp 338) | sane |
| normal | t9_display1080 | FAIL | FAIL (0.085) | ok (world 541, bsp 163) | sane |
| normal | t9b_display720 | FAIL | FAIL (0.061) | ok (world 255, bsp 81) | sane |
