# Map-loss gate

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin\wfc_rebuild.exe` (run from its own location, no WFC_RENDER_DATA)

Verdict: **NO GROSS MAP LOSS (human look still required)**

| config | visit | map | start | world detail | VISUALCHECK world / bsp / materials | same start, reference runtime | verdict |
|---|---|---|---|---|---|---|---|
| normal | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.207/0.247/0.237 | 43 / 24 / 37 | 0.152 (known-good M05, ratio 1.62) | PASS |
| normal | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.206/0.215/0.221 | 195 / 24 / 53 |  | PASS |
| normal | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.206/0.254/0.235 | 41 / 23 / 35 | 0.546 (known-good M05, ratio 0.47) | PASS |

## All checks

- PASS **maploss.normal.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.normal.render_data_root**: render data resolved by the product: wfc: render data root F:\Transformers Rebuild\Rebuild-Experimental\work\ab\m8b_175a634\build-release\bin/../../work/render
- PASS **maploss.normal.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0.006 / 0.038, black 0.948 / 0.936; centre detail 0.136 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.normal.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.117 (FAIL: content beside the movie - the frontend shows through)
- INFO **maploss.normal.s1.MP_IAC_Streets.draws_vs_direct**: draw counts do not distinguish a lost world from a healthy one (M06b control: same spawn draws); in-play world draws via the lobby at the spawn 1378 (walking median 43) vs direct boot of the same exe at the same start 1641 (ratio 0.84; FAIL < 0.5: the environment is not drawn on the frontend route)
- INFO **maploss.normal.b.MP_IAC_Berth.draws_vs_direct**: draw counts do not distinguish a lost world from a healthy one (M06b control: same spawn draws); in-play world draws via the lobby at the spawn 1271 (walking median 195) vs direct boot of the same exe at the same start 1087 (ratio 1.169; FAIL < 0.5: the environment is not drawn on the frontend route)
- INFO **maploss.normal.s2.MP_IAC_Streets.draws_vs_direct**: draw counts do not distinguish a lost world from a healthy one (M06b control: same spawn draws); in-play world draws via the lobby at the spawn 1275 (walking median 41) vs direct boot of the same exe at the same start 1739 (ratio 0.733; FAIL < 0.5: the environment is not drawn on the frontend route)
- PASS **maploss.normal.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.207/0.247/0.237, black 0.174/0.152/0.092; product VISUALCHECK median world draws 43, BSP draws 24, materials 37; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 1.62). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.206/0.215/0.221, black 0.355/0.438/0.546; product VISUALCHECK median world draws 195, BSP draws 24, materials 53. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.206/0.254/0.235, black 0.174/0.157/0.097; product VISUALCHECK median world draws 41, BSP draws 23, materials 35; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.47). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.streets_return_counts**: Streets world draws first visit 43 vs after Berth 41 (ratio 0.95); BSP 24 vs 23
- SKIP **maploss.hud_scale_720_vs_1080**: health widget not found in both spawn frames
