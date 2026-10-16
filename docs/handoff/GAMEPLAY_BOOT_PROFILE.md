# Direct-boot profile hook (`WFC_BOOT_PROFILE=1`), a harness feature

Gameplay, 2026-10-16. The patch is `docs/handoff/GAMEPLAY_BOOT_PROFILE.patch`, against integration 4a5c8711. It changes `Application.h`, `Application.cpp` and `Application_Frontend.cpp`.

## What it does

The direct map boot (`WFC_MAP`, `WFC_SHOTLIST`, the harnesses) used to ignore the profile's display and graphics rows, so authored eye-pass views could only run at 1280×720 without Enhanced.

With exactly **`WFC_BOOT_PROFILE=1`** (a windowed boot, not null render), the new `Application::applyBootProfile()` runs before the map loads:
1. It reads `wfc_profile.ini` from `core::userFilePath`, the same file the frontend reads.
2. It applies the same rows as the frontend boot block, through the same helpers:
   - the display row: `setDisplayMode` when the size or fullscreen differs, vsync, the frame limit (`WFC_FPS_LIMIT` honoured);
   - `applyGraphicsExtensions`: upscaling, the HD level (`hdTexturesEffective` over `gatherHardwareFacts`) and anisotropy, with the `WFC_UPSCALING` / `WFC_HDTEXTURES` / `WFC_ANISOTROPY` test overrides;
   - `applyModernMaterials(hd, "boot")`, `applyRayTracing` and gamma.
3. It emits the same FLOW trace lines, plus one `boot profile: display WxH ..., HD n, ray tracing n` log line.

**Default off:** `WFC_SHOTLIST` alone, and any boot without the variable, is unchanged.

**HD 2 note:** at this head the GL renderer reports HD level 2 (Enhanced) as unavailable ("no Enhanced root yet"). So a stored HD 2 is clamped to 1, exactly as on the frontend boot, and Modern materials stay off. `WFC_HDTEXTURES=2` overrides the level after the clamp, as on the frontend, and turns Modern materials on.

## Check

Streets, windowed, muted, NOINTERACT, RT off; the profile is 2560×1440 with HD 2; `WFC_HD_ROOT` = Systems' `hd_dev_full` (read only).

| Arm | Client | HD | Modern materials |
|---|---|---|---|
| Off (no variable) | `NOINTERACT: client 1280x720` | none | none |
| On | `NOINTERACT: client 2560x1440`, "HD texture pack available: 3505 textures" | effective 1 (the clamp above) | off |
| On + `WFC_HDTEXTURES=2` | 2560×1440 | `hdTextures=2` | on: "Modern materials on for MP_IAC_Streets (593 pbr bodies)", "HD modern material maps: 498 (hd_pbr_index.json)" |

- **SIMHASH:** off vs on identical over all 28 shared steps.
- **Patch:** applies on a clean 4a5c8711 archive; the files are byte-identical to the built tree; the exe contains `WFC_BOOT_PROFILE`.
