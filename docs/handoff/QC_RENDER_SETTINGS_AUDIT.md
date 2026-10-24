# QC: render settings audit (Rendering, 2026-10-24)

User playtest 2026-10-23b, item 1: every graphics setting does exactly what its row says, in Off and Realistic, and survives a
map change. Code trace (every row: profile key -> apply -> renderer effect -> release keep list) + 4 measured runs on the
fd58fde1 head (work/cand_smoke), Streets TDM via the frontend, 1280x720 windowed, profile with non-default values
(FrameLimit 90, TextureQuality Low, Anisotropy 8x, Lens Flares Subtle, Smoke Enhanced, VSync off, Show FPS), WFC_SETTINGSTEST=1.

## Fixed (merged unless noted)
| Row | Was | Now |
|---|---|---|
| Texture Quality | no-op (stored only) | mip LOD bias 2 / 1 / 0 on material textures (Low / Medium / High = original); live; kept across map change (QC_SETTINGS 41a3bcac + Frontend 7f0efe32) |
| VSync | ignored on the D3D12 present path (FG on) | the presenter takes the window's swap interval (41a3bcac) |
| Menus 72 FPS | uncapped / player limit in menus | 60 in menus, the player's limit in a match and during loads (41a3bcac; the frontend's own loop path: RENDERING_MENU_CAP2, measured below) |
| Brightness preview | logos static | UI layer through the display gamma (GFX_GAMMA + Frontend 05767448; shot 09's white = the phone's exposure, the image draws as authored) |
| Recreate-renderer mode | only Brightness re-applied | every row re-applied (Frontend) |
| Water (dev row) | stored value applied with the row hidden | neutral unless the dev row shows (Frontend) |
| Local pawn flicker while stuck | own pawn occlusion-culled | never culled (LOCAL_PAWN_OCC da9b3e48) |

## Measured (4 runs: Off, Realistic, FG + walking Off, FG + walking Realistic) - work/qcrun/<ARM>/out.txt + shots
- settings self-test (map change keep list): **12 / 12 PASS in every arm** (incl. texture quality, RT mode, lens flares, smoke).
- every row's apply line present at boot: frame limit 90 (menus 60), anisotropic 8x, texture quality Low (LOD bias 2), smoke
  Enhanced, RT Realistic (taking effect at the match load: RT world 539 BSP subs + GI probes built in the match).
- no errors in any log; shots normal (Texture Quality Low renders, the walking pawn with FG shows no doubling in captured
  frames - captures are the real frames; an artefact only in generated frames needs an eye check or an FG-output capture).
- **menus held at 60: NOT effective in this run** - menus measured 90 FPS (FG: 45 = 90 / 2). Cause: the frontend runs its
  own loop, so menu frames come through endFrame's fallback wait, which the first fix left at the player's limit. Fixed in
  RENDERING_MENU_CAP2 (both wait paths ask one rule: a match or a running load at the player's limit, else the 60 cap);
  re-measure on the next slot.
- FG: presents through FSR 3 frame generation, 0 resets; the menu / match rate is the limit / 2 as designed (the limit counts
  generated frames).

## Rows that do what they say, with labelled conditions (no change)
- Ray Tracing On and Volumetric Lighting: take effect at the next map load when the current map loaded without the RT scene
  (labelled "(next)"); Volumetric = Realistic only (labelled).
- HD Textures: Faithful / Enhanced load the same HD pack by design (Enhanced = + modern materials); next map load (hint label
  for Enhanced: Frontend).
- Frame Generation: after a restart (labelled).
- Resolution / Fullscreen / Anisotropy / Upscaling / Lens Flares / Smoke / Show FPS / Brightness: live.

## Harness notes
WFC_FRAMELIMIT blocks every menu frame-limit change; WFC_UPSCALING / WFC_ANISOTROPY / WFC_HDTEXTURES / WFC_SMOKE / WFC_WATER /
WFC_LENSFLARE / WFC_X3=0 / WFC_FSR / WFC_FSR3 / WFC_HDFORCE / WFC_MODERN override the menu in test runs - never set them in a
settings check.
