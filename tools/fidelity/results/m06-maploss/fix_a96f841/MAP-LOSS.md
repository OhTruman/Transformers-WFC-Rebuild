# Map-loss gate

exe: `F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fe_fix\build-release\bin\wfc_rebuild.exe` (run from its own location, no WFC_RENDER_DATA)

Verdict: **NO GROSS MAP LOSS (human look still required)**

| config | visit | map | start | world detail | VISUALCHECK world / bsp / materials | same start, reference runtime | verdict |
|---|---|---|---|---|---|---|---|
| normal | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.153/0.255/0.232 |  /  /  | 0.152 (known-good M05, ratio 1.68) | PASS |
| normal | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.175/0.212/0.239 |  /  /  |  | PASS |
| normal | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.16/0.248/0.243 |  /  /  | 0.546 (known-good M05, ratio 0.45) | PASS |
| w1920 | s1 | MP_IAC_Streets | TnTeamPlayerStart_12481 | 0.129/0.186/0.174 |  /  /  | 0.152 (known-good M05, ratio 1.22) | PASS |
| w1920 | b | MP_IAC_Berth | TnTeamPlayerStart_5269 | 0.145/0.181/0.206 |  /  /  |  | PASS |
| w1920 | s2 | MP_IAC_Streets | TnTeamPlayerStart_8835 | 0.128/0.192/0.175 |  /  /  | 0.546 (known-good M05, ratio 0.35) | PASS |

## All checks

- PASS **maploss.normal.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.normal.render_data_root**: render data resolved by the product: no root log line (build predates the M06b root search; it uses <exe>\..\..\work\render)
- PASS **maploss.normal.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0.006 / 0.038, black 0.948 / 0.936; centre detail 0.136 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.normal.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 1; centre detail 0.117 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.w1920.process**: process outcome: clean exit; display-driver / crash events in the run window: none
- INFO **maploss.w1920.render_data_root**: render data resolved by the product: no root log line (build predates the M06b root search; it uses <exe>\..\..\work\render)
- PASS **maploss.w1920.intro_edges.i1_intro_2s**: intro frame side columns (8 % each): detail 0 / 0.022, black 1 / 0.981; centre detail 0.1 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.w1920.intro_edges.i2_intro_6s**: intro frame side columns (8 % each): detail 0 / 0, black 1 / 0.982; centre detail 0.233 (FAIL: content beside the movie - the frontend shows through)
- PASS **maploss.normal.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.153/0.255/0.232, black 0.194/0.163/0.094; product VISUALCHECK median world draws , BSP draws , materials ; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 1.68). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.175/0.212/0.239, black 0.336/0.391/0.472; product VISUALCHECK median world draws , BSP draws , materials . FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.normal.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.16/0.248/0.243, black 0.211/0.171/0.099; product VISUALCHECK median world draws , BSP draws , materials ; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.45). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.w1920.s1.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_12481: world detail (HUD band and player excluded) 0.129/0.186/0.174, black 0.212/0.173/0.102; product VISUALCHECK median world draws , BSP draws , materials ; same start rendered by the known-good M05 runtime: detail 0.152 (median ratio 1.22). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.w1920.b.MP_IAC_Berth.environment**: MP_IAC_Berth via the lobby, start TnTeamPlayerStart_5269: world detail (HUD band and player excluded) 0.145/0.181/0.206, black 0.334/0.382/0.463; product VISUALCHECK median world draws , BSP draws , materials . FAIL = the environment is missing although the match runs (counts alone never pass this check)
- PASS **maploss.w1920.s2.MP_IAC_Streets.environment**: MP_IAC_Streets via the lobby, start TnTeamPlayerStart_8835: world detail (HUD band and player excluded) 0.128/0.192/0.175, black 0.213/0.172/0.101; product VISUALCHECK median world draws , BSP draws , materials ; same start rendered by the known-good M05 runtime: detail 0.546 (median ratio 0.35). FAIL = the environment is missing although the match runs (counts alone never pass this check)
- SKIP **maploss.hud_scale_720_vs_1080**: health widget not found in both spawn frames
