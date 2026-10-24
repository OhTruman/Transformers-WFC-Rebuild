# DLC Escalation maps 3 + 4: Tranquillity ("Forsaken") + District - headless same-match + identity (2026-10-23 ~22:25)

Env on every DLC arm: WFC_ASSETS=F:/Transformers Rebuild/Rebuild/work/assets_dlc/VerticalSlice, WFC_RENDER_DATA=.../Rebuild/work/render,
WFC_NULLRENDER=1. lockstep-ab.ps1 -Map MP_ESC_<name> (booted in ESC by name), steps 600-9600, seed 1234, PlayerBot, 1 rep.
Head 784dc096 (Gameplay's DLC Escalation). Runs work/dlc/escdlc/.

**Base rebuilt: af4d0731 + FINAL_BASE_ITEMS 01-14** (edba5bb3) -> work/ab/fast_af4d073_final8. **Gap in 14**: it changes
src/render/Renderer.h to `#include "render/SplitScreen.h"` but does not add that new header (src/render is outside the base-items
paths), so the base does not compile from 01-14 alone ('render/SplitScreen.h' file not found). Added the 74-line interface header from
the head (edba5bb3:src/render/SplitScreen.h) to the base tree; Gameplay to add it to 14 (or 01).

| Row | Arms | Result |
|---|---|---|
| Tranquillity same match | base 01-14 vs head 784dc096 | **True**, 1,784 steps (the run ends at the lone player's game over in wave 1, both arms) |
| District same match | base 01-14 vs head 784dc096 | **True**, 1,270 steps (game over in wave 1, both arms) |
| Tranquillity / District vs the gated d5bb6863 | gated vs base | False - **expected**: d5bb6863 predates the DLC Escalation wiring (it loads the map but builds no wave: 1 error line, 0 waves, runs the full 9,000 steps) |
| Streets identity | d5bb6863 vs 784dc096 | **True**, 9,000 steps - versus untouched by the DLC Escalation + split-screen seat code |

Both DLC ESC maps load for real ('map: MP_ESC_Tranquillity' / 'MP_ESC_District', launched in ESC), 0 errors on base and head, wave 1
begins and the horde kills the lone PlayerBot (as on Remnant / BrokenHope).
