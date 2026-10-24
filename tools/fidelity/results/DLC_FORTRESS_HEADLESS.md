# DLC map 1: Fortress (MP_KON_Fortress, MapId 512) - headless same-match + identity (2026-10-23 ~21:10)

Env on every arm: WFC_ASSETS=F:/Transformers Rebuild/Rebuild/work/assets_dlc/VerticalSlice, WFC_RENDER_DATA=.../Rebuild/work/render,
WFC_NULLRENDER=1. lockstep-ab.ps1 -Map 512 (TDM, 31 v 32, PlayerBot), steps 600-9600, seed 1234, 1 rep. Runs work/dlc/fortress/.

| Row | Arms | Result |
|---|---|---|
| Fortress same match | base (af4d0731 + FINAL_BASE_ITEMS 01-12) vs head 2ed12d3a | **True**, 9,000 steps each |
| Fortress identity | gated d5bb6863 vs head 2ed12d3a (and both vs base) | **True** |
| Streets identity (versus unchanged since the gate) | d5bb6863 vs 2ed12d3a | **True**, 9,000 steps (the one sim-side line since, WorldBots.cpp, is campaign-only: `if (cp_.active) cpTacticalConstrain`) |

All three arms load the real map: 'map: MP_KON_Fortress', world.glb from the DLC root (5,540,248 verts, 4,649,186 tris); 0 ERROR
lines; combat runs (303 respawns in 150 s of 63 bots).

Notes for AssetTools / Rendering (not sim):
- 19 'image: decode failed' warnings per run, all PNGs under AssetTools/out/dlc_content/content/... (e.g. ENV_AllShader_p, Designer_Materials_p, '...Rust_C_CLR.png'): textures that will show as missing in a windowed run.
- 13 'stranded off the mesh' lines in 150 s (bots walking back); no stranded suicides.

The timed rows wait for the DLC gate (final-gate.sh adds 512 automatically now that Rebuild/work/render/MP_KON_Fortress exists; the
arms need the same WFC_ASSETS - to be added to the gate's arm env when the DLC gate is called).
