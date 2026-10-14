# Map-loss gate

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\m06l\mirror\Rebuild\build\release\bin\wfc_rebuild.exe` (run from its own location, no WFC_RENDER_DATA)

Verdict: **MAP PRESENTATION LOSS DETECTED (12 map visits)**

| config | visit | map | start | world detail | VISUALCHECK world / bsp / materials | same start, reference runtime | verdict |
|---|---|---|---|---|---|---|---|
| normal | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.07/0.064/0.034 | 35 / 20 / 32 | 0.152 (known-good M05, ratio 0.46) | FAIL |
| normal | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.143/0.152/0.131 | 152 / 19 / 45 |  | FAIL |
| normal | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.061/0.057/0.029 | 36 / 21 / 33 | 0.546 (known-good M05, ratio 0.11) | FAIL |
| w1280 | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.073/0.066/0.037 | 36 / 21 / 32 | 0.152 (known-good M05, ratio 0.48) | FAIL |
| w1280 | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.141/0.156/0.131 | 152 / 19 / 45 |  | FAIL |
| w1280 | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.063/0.055/0.029 | 36 / 21 / 33 | 0.546 (known-good M05, ratio 0.12) | FAIL |
| w1920 | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.053/0.046/0.028 | 35 / 20 / 32 | 0.152 (known-good M05, ratio 0.35) | FAIL |
| w1920 | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.129/0.134/0.108 | 192 / 26 / 54 |  | FAIL |
| w1920 | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.048/0.038/0.025 | 36 / 21 / 32 | 0.546 (known-good M05, ratio 0.09) | FAIL |
| lowtex | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.071/0.067/0.036 | 69 / 38 / 49 | 0.152 (known-good M05, ratio 0.47) | FAIL |
| lowtex | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.141/0.155/0.131 | 491 / 82 / 109 |  | FAIL |
| lowtex | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.064/0.055/0.029 | 64 / 32 / 44 | 0.546 (known-good M05, ratio 0.12) | FAIL |

## All checks

- PASS **maploss.normal.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.normal.render_data_root**: render data resolved by the product: wfc: render data root F:\Transformers Rebuild\Rebuild-Experimental\work\m06l\mirror\Rebuild\build\release\bin/../../../work/render
- PASS **maploss.normal.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0 / 0.024, black 1 / 0.984; centre detail 0.121 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.normal.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 0.986; centre detail 0.298 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.w1280.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.w1280.render_data_root**: render data resolved by the product: wfc: render data root F:\Transformers Rebuild\Rebuild-Experimental\work\m06l\mirror\Rebuild\build\release\bin/../../../work/render
- PASS **maploss.w1280.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0 / 0.024, black 1 / 0.984; centre detail 0.121 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.w1280.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 0.986; centre detail 0.298 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.w1920.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.w1920.render_data_root**: render data resolved by the product: wfc: render data root F:\Transformers Rebuild\Rebuild-Experimental\work\m06l\mirror\Rebuild\build\release\bin/../../../work/render
- PASS **maploss.w1920.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0 / 0.022, black 1 / 0.981; centre detail 0.1 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.w1920.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 0.982; centre detail 0.233 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.lowtex.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.lowtex.render_data_root**: render data resolved by the product: wfc: render data root F:\Transformers Rebuild\Rebuild-Experimental\work\m06l\mirror\Rebuild\build\release\bin/../../../work/render
- PASS **maploss.lowtex.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0 / 0.024, black 1 / 0.984; centre detail 0.121 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.lowtex.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 0.986; centre detail 0.298 (FAIL: content beside the movie - the frontend shows through)
- FAIL **maploss.normal.s1.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 35 vs direct boot of the same exe at the same start 1641 (ratio 0.021; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.normal.b.MP_IAC_Berth.draws_vs_direct**: in-play world draws via the lobby 152 vs direct boot of the same exe at the same start 1087 (ratio 0.14; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.normal.s2.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 36 vs direct boot of the same exe at the same start 1739 (ratio 0.021; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.w1280.s1.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 36 vs direct boot of the same exe at the same start 1641 (ratio 0.022; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.w1280.b.MP_IAC_Berth.draws_vs_direct**: in-play world draws via the lobby 152 vs direct boot of the same exe at the same start 1087 (ratio 0.14; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.w1280.s2.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 36 vs direct boot of the same exe at the same start 1739 (ratio 0.021; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.w1920.s1.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 35 vs direct boot of the same exe at the same start 1641 (ratio 0.021; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.w1920.b.MP_IAC_Berth.draws_vs_direct**: in-play world draws via the lobby 192 vs direct boot of the same exe at the same start 1087 (ratio 0.177; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.w1920.s2.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 36 vs direct boot of the same exe at the same start 1739 (ratio 0.021; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.lowtex.s1.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 69 vs direct boot of the same exe at the same start 1641 (ratio 0.042; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.lowtex.b.MP_IAC_Berth.draws_vs_direct**: in-play world draws via the lobby 491 vs direct boot of the same exe at the same start 1087 (ratio 0.452; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.lowtex.s2.MP_IAC_Streets.draws_vs_direct**: in-play world draws via the lobby 64 vs direct boot of the same exe at the same start 1739 (ratio 0.037; FAIL < 0.5: the environment is not drawn on the frontend route)
- FAIL **maploss.normal.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.07/0.064/0.034, black 0.037/0.079/0.158; product VISUALCHECK median world draws 35, BSP draws 20, materials 32; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 0.46). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.normal.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.143/0.152/0.131, black 0.001/0.006/0.004; product VISUALCHECK median world draws 152, BSP draws 19, materials 45. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.normal.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.061/0.057/0.029, black 0.045/0.083/0.167; product VISUALCHECK median world draws 36, BSP draws 21, materials 33; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.11). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.w1280.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.073/0.066/0.037, black 0.048/0.086/0.162; product VISUALCHECK median world draws 36, BSP draws 21, materials 32; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 0.48). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.w1280.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.141/0.156/0.131, black 0.001/0.006/0.004; product VISUALCHECK median world draws 152, BSP draws 19, materials 45. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.w1280.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.063/0.055/0.029, black 0.046/0.086/0.174; product VISUALCHECK median world draws 36, BSP draws 21, materials 33; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.12). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.w1920.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.053/0.046/0.028, black 0.058/0.095/0.168; product VISUALCHECK median world draws 35, BSP draws 20, materials 32; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 0.35). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.w1920.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.129/0.134/0.108, black 0.003/0.011/0.008; product VISUALCHECK median world draws 192, BSP draws 26, materials 54. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.w1920.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.048/0.038/0.025, black 0.055/0.096/0.182; product VISUALCHECK median world draws 36, BSP draws 21, materials 32; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.09). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.lowtex.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.071/0.067/0.036, black 0.046/0.085/0.162; product VISUALCHECK median world draws 69, BSP draws 38, materials 49; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 0.47). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.lowtex.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.141/0.155/0.131, black 0.001/0.006/0.004; product VISUALCHECK median world draws 491, BSP draws 82, materials 109. FAIL = the environment is missing although the match runs (counts alone never pass this check)
- FAIL **maploss.lowtex.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.064/0.055/0.029, black 0.046/0.085/0.174; product VISUALCHECK median world draws 64, BSP draws 32, materials 44; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.12). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.streets_return_counts**: Streets world draws first visit 35 vs after Berth 36 (ratio 1.03); BSP 20 vs 21
- PASS **maploss.w1280.streets_return_counts**: Streets world draws first visit 36 vs after Berth 36 (ratio 1); BSP 21 vs 21
- PASS **maploss.w1920.streets_return_counts**: Streets world draws first visit 35 vs after Berth 36 (ratio 1.03); BSP 20 vs 21
- PASS **maploss.lowtex.streets_return_counts**: Streets world draws first visit 69 vs after Berth 64 (ratio 0.93); BSP 38 vs 32
- PASS **maploss.hud_scale_720_vs_1080**: health widget bright box: 720p 130x78 of 1280x720 (0.102 of width), 1080p 196x118 of 1920x1080 (0.102 of width); ratio 1 (FAIL: the HUD does not scale with the viewport)
