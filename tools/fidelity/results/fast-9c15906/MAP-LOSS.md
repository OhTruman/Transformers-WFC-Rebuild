# Map-loss gate

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin\wfc_rebuild.exe` (run from its own location, no WFC_RENDER_DATA)

Verdict: **NO GROSS MAP LOSS (human look still required)**

| config | visit | map | start | world detail | VISUALCHECK world / bsp / materials | same start, reference runtime | verdict |
|---|---|---|---|---|---|---|---|
| normal | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.213/0.248/0.24 | 42 / 23 / 35 |  | PASS |
| normal | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.21/0.22/0.223 | 179 / 21 / 53 |  | PASS |
| normal | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.32/0.313/0.23 | 452 / 142 / 95 |  | PASS |

## All checks

- PASS **maploss.normal.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.normal.render_data_root**: render data resolved by the product: wfc: render data root F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fast_9c15906\build-release\bin/../../work/render
- PASS **maploss.normal.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.177 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.normal.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.328 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.normal.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.213/0.248/0.24, black 0.176/0.147/0.09; product VISUALCHECK median world draws 42, BSP draws 23, materials 35. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.21/0.22/0.223, black 0.353/0.442/0.55; product VISUALCHECK median world draws 179, BSP draws 21, materials 53. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.32/0.313/0.23, black 0.124/0.05/0.152; product VISUALCHECK median world draws 452, BSP draws 142, materials 95. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- INFO **maploss.normal.streets_return_counts**: Streets world draws first visit 42 vs after Berth 452 (ratio 10.76); BSP 23 vs 142
- SKIP **maploss.hud_scale_720_vs_1080**: health widget not found in both spawn frames
