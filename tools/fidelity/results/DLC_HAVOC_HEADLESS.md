# DLC map 2: Havoc (MP_ORB_Havoc, MapId 511) - headless same-match + identity (2026-10-23 ~21:35)

Env on every DLC arm: WFC_ASSETS=F:/Transformers Rebuild/Rebuild/work/assets_dlc/VerticalSlice, WFC_RENDER_DATA=.../Rebuild/work/render,
WFC_NULLRENDER=1. lockstep-ab.ps1 (TDM, 31 v 32, PlayerBot), steps 600-9600, seed 1234, 1 rep. Head = 51f00e13 (the current head,
built now: split-screen local seat in World.cpp / WorldLocalSeat.cpp since 2ed12d3a). Runs work/dlc/havoc/.

| Row | Arms | Result |
|---|---|---|
| Havoc same match | base (af4d0731 + FINAL_BASE_ITEMS 01-12) vs head 51f00e13 | **True**, 9,000 steps |
| Havoc identity | gated d5bb6863 vs head 51f00e13 | **True** |
| Streets identity (versus unchanged by the split-screen seat code) | d5bb6863 vs 51f00e13 | **True**, 9,000 steps |
| Fortress re-check (re-emitted data) | base vs head 51f00e13 | **True**; texture decode failures **0** (were 19) |

Both DLC maps load for real ('map: MP_ORB_Havoc' / 'MP_KON_Fortress' from the DLC root), 0 ERROR lines, combat runs (Havoc 230 /
Fortress 303 respawns in 150 s), strand lines 19 / 13 with no strand suicides.
