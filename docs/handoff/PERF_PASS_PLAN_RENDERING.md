# Performance pass: Rendering rows (plan, 2026-10-24)

The user's step 3: everything working AND running as it should. Rendering measures, finds the cost, and fixes only what is
visually lossless (zero visible difference side by side; any change that shows is a user decision). Load times: Systems.

## Setup (every row)
- 2560x1440 fullscreen-equivalent window, the user's display class (memory: 1440p profile; no non-native fullscreen runs).
- Frontend boot -> the mode / map by script (lobby options with bots: WFC_LOBBY_OPTIONS so the match has its normal load),
  WFC_MUTE / WFC_NOINTERACT, WFC_AUTODETECT=0, profile copy per row (the profile overrides env for RT / HD).
- Two cameras per row: a fixed camera (WFC_FIXEDCAM, the same view every run: comparable numbers) and real play (the local
  player auto-walking: the frame-time spread of actual play). 60 s after InGame, frame log on.
- Numbers per row: GPU ms p50 / p95 / p99, CPU frame ms p50 / p95, frames over 16.7 / 33 ms, VRAM, the GPU pass split
  (world, characters, map FX, translucency, post, RT passes). Menu rows: menu fps (held at 60) and frame pacing.
- One exe at a time; two reps for any A/B before a claim (memory: two retractions on single runs).

## Rows (mode x map x setting)
| Mode / map | Off | Faithful | Realistic | HD Faithful | HD Enhanced | Upscaling FSR 1 Q | FG on |
|---|---|---|---|---|---|---|---|
| TDM Streets | x | x | x | x | x | x | x (Off + Realistic) |
| Escalation Remnant | x | | x | | | | |
| Campaign D1 (Crash) | x | | x | | | | |
| DLC map (Fortress, when its render data is final) | x | | x | | | | |
| Menus (frontend scene) | 60 held | | 60 held | | | | 60 held |
- Off is the reference row: the original look must run at the user's 144 Hz class on the 7950X3D / 7900 XTX (budget
  ~6.9 ms frame). Realistic: the user's floor ~60 fps at 1440p (memory) - report, then optimize losslessly.

## What I look for (lossless only)
- Passes costing more than the image needs: RT passes per frame (the split-screen descriptor idle wait does not apply to one
  view), the character light environments on no-volume DLC maps (Fortress: 688 lights on the ray path), map FX simulation
  throttling, the shadow projectors.
- CPU: draw submission, per-frame allocations, the light-environment ray budget, the skinning uploads.
- Every candidate fix: identity (Off byte-identical where it should be), a side-by-side eye check, then the A/B with 2 reps.

## Order
1. Menus at 60 (RENDERING_MENU_CAP2) re-measured.
2. TDM Streets all setting columns (the reference map).
3. Escalation, campaign D1, the DLC map: Off + Realistic.
4. Report: docs/handoff/PERF_PASS_RENDERING.md (table + the pass split + candidate fixes with their lossless proof).
