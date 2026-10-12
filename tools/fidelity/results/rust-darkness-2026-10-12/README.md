# Rust interior darkness check (untimed, 2026-10-12; report only, no product change)

Question (Integration): are Rust interiors faithfully dark, or is there a lighting / exposure / lightmap / tonemap gap?
Inputs: the render data the build reads (`work/ab/m9c_9c3560e/work/render/<map>/lighting.json` + `lightmaps/*.png`), the
current renderer in Rebuild-Rendering `src/render/gl/WfcPipeline.cpp` (read only), the AssetTools pilot captures
(`AssetTools/out/hd/push_pilot/ingame/`, Rendering lane, 2026-10-11 19:26), and the original thumbnail `UI_LevelThumbnails_p/MP_Rust.png`.

## Finding: nothing Rust-specific is missing; Rust is not darker than the other maps in the data or the pipeline

1. **Baked light amount (`lmlum.py`).** For each lightmapped component: the mean of its lightmap region, PWL-like degamma
   (pow 2.2), times the coefficient scale, flat-normal weights. The median irradiance per map:

   | map | components | median | p25 | p75 | share < 0.02 |
   |---|---|---|---|---|---|
   | Rust | 1529 | 0.041 | 0.013 | 0.101 | 0.33 |
   | Gorge | 1363 | 0.039 | 0.017 | 0.086 | 0.30 |
   | Molten | 3315 | 0.041 | 0.019 | 0.105 | 0.26 |
   | Debris | 1582 | 0.039 | 0.009 | 0.118 | 0.38 |
   | Streets | 1975 | 0.037 | 0.007 | 0.123 | 0.39 |
   | Berth | 1430 | 0.034 | 0.012 | 0.083 | 0.35 |
   | Seed | 3049 | 0.029 | 0.013 | 0.070 | 0.38 |
   | Complex | 2485 | 0.058 | 0.033 | 0.127 | 0.04 |

   Rust sits in the middle. Its lightmap scales (`lmch.py`) are normal; it has the warmest ratio, red/blue ~1.9 against Gorge ~1.0
   and Streets ~0.7, which matches the rust colour grade.
2. **Lights (`rustlights.py`).** 347 of 348 lights are `built_into_lightmap` (326 point, 20 spot, 1 directional). The one that is
   not is a Dynamic/CompositeDynamic-channel directional at brightness 0.2: a character light. So the lightmapped-geometry shader
   (`kFSMainLM`, lightmap only) misses no static light pass on Rust.
3. **Lightmap decode.** Lightmap PNG/DDS go through the same `srgb=true` upload as colour maps. That is exact Xenos PWL
   (`uploadDds` / `pwlToLinear16`, no sRGB-curve blocks unless they are HD pack blocks), in the renderer since 0307607
   (2026-10-02), so the pilot captures include it.
4. **Post (`ppdump2.py`).** Rust uses one PostProcessVolume: HighLights 1.5 (x0.67 before gamma), MidTones 0.9, Shadows
   -0.001, no desaturation, CLUT mp40. Gorge has the same HighLights 1.5 (MidTones 0.86/0.97/0.97) and Berth 1.5 too; Molten
   1.2; Seed and Streets 1.0. Rust's grade is not darker than Gorge's.
5. **The pilot `interior_wall` capture** looks almost straight into an unlit alcove next to a lamp. That view is dark on any
   map with this light layout. The thumbnail is a curated still of the lit hall, not the same camera, so it cannot be
   compared directly.

## Remaining suspects (global, not Rust-specific; unverified)

- **No specular on lightmapped surfaces.** `kFSMainLM` adds `Diffuse * L` only. UE3 builds from about 2009 can add a lightmap
  specular term from the directional coefficients, which shows most on metal and wet surfaces; Rust is mostly rusted metal.
  The shader-cache strings (`scstr.py`) show only `LightMapScale` / `LightMapTextures` / `NoLightMapColor`, which neither
  confirms nor rules it out, because the term needs no extra uniform. Settling it needs the decoded texture-lightmap pixel
  shader microcode (Rendering / RE).
- **A fidelity verdict needs an original frame from the same camera** (a capture from the original on hardware or emulator,
  at a known Rust spot). Without one, "faithfully dark" can only be argued from data parity, as above.

No product change proposed.
