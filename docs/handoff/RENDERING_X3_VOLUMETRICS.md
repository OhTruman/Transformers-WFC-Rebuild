# Rendering X3 (design, not built): ray-traced volumetric light for Realistic

Status: design only, written while the batch-4 sheet runs. Integration starts it if that sheet still does not read as a
different game (decision rule 2026-10-16). Realistic only; Off / Faithful never run any of it (byte-identity unchanged).

## What it adds
- Sun shafts: light through gaps, windows, between buildings, its shadowed parts dark (the RT sun visibility per froxel).
- Lamp halos and cones: the RT-shadowed static lamps (the frame's 4 shadow lights) and the transient lights (muzzle,
  explosions, impacts, headlights: the tiled list) scatter in the air.
- Lit fog: the map's height fog takes its colour from the light that reaches it, not the constant LightColor.

## Medium (per-map density source)
- The authored height fog in lighting.json `fog` (UE3 FHeightFogSceneInfo: Density, Height, StartDistance, LightColor x
  LightBrightness); the runtime already parses it (fogScale_, fogMaxH_, fogStart_). Measured: Streets 2e-5, Rust 1e-4,
  Molten 5e-5, Gorge 9e-5, Debris no height fog.
- fog_volumes.json (the original's FogVolume actors) where present.
- Realistic floor: a thin aerial haze (default sigma_s 0.002 / m, WFC_X3_HAZE) under the authored fog, so maps without fog
  (Debris) still show shafts; capped so the far field keeps the original's visibility. Per-map multiplier WFC_X3_DENSITY.

## Grid and passes (1440p numbers)
- Froxels: 160 x 90 screen tiles (16 px at 2560 x 1440) x 64 exponential depth slices to 150 m = 921,600 froxels,
  RGBA16F scattering + transmittance (2 x 7 MB). Render-size based (FSR / DLSS input size, not the window).
- Pass 1 (D3D12 compute, RayQuery, the AO / shadow pass's TLAS and constants): per froxel one sun ray (jittered in the
  froxel and over the sun cone, the soft-sun disc as rt_shadow_ao), the 4 shadow lights one ray each where the froxel is
  inside their radius (most froxels: 0-1); the transient lights unshadowed except the traced 4. Phase: Henyey-Greenstein,
  g 0.6 for the sun (forward shafts), 0.3 for lamps. Ambient in-scatter: the RT probe cube at the froxel (sky / bounce
  colour).
- Pass 2: temporal reprojection with the previous frame's grid (the reflection pass's prev VP), 8-frame exponential
  history, rejection on sun-direction / light changes; this is what keeps 1 ray per froxel noise-free.
- Pass 3: front-to-back integration along each tile's slices (scattering + transmittance), written to a 3D texture shared
  with GL (the pooled shared allocation of the R4 pages: no new interop leak, per the AMD charge finding).
- Apply: in the RT composite, per pixel trilinear at its depth: c = c x T + S (before the tone curve, after translucency
  for the opaque depth; translucent draws sample the grid at their own depth in the transient-light block).

## Cost estimate (7900-class, 1440p)
- Rays: ~0.92 M sun + ~0.3 M lamp rays per frame (the AO pass traces ~7 M); ~0.4-0.6 ms.
- Inject + reproject + integrate: ~0.3 ms; apply: ~0.05 ms. Total ~0.8-1.2 ms p50 (budget report, not a gate; Realistic
  p50 today 9-10 ms, the 14 ms flag stays far).
- VRAM: ~30 MB (two grids + history).

## Switches / checks
- WFC_X3=0 off; WFC_X3_HAZE, WFC_X3_DENSITY, WFC_X3_G (sun anisotropy); WFC_X3DIAG (froxel stats).
- Gates: Off / Faithful identity on the integration export; the eye-pass sheet (contrast ratio, % pixels changed); a
  shafts crop on Streets (between buildings), Gorge (open sky), Molten (lava glow through the haze).

## What the screen-space tries taught (v1-v3, 2026-10-16, uncommitted; out of package 6)
v1 to v3 ray-marched a quarter-size screen target instead of froxels:
- v1: sun and haze.
- v2: halos from the frame's 4 shadow lights.
- v3: halos from the 4 lamps in view, ranked by brightness x coverage.

All three were judged not showable. They added a grey veil (contrast 0.79-0.95 against no-X3 on the authored views)
and no visible shafts or halos. Rules for the froxel version:
- **No ambient in-scatter by default.** A probe-ambient term adds the same grey to every froxel, and that is the veil.
  In-scatter comes only from directional sources (the sun's visibility per froxel, the lamps, the transient lights),
  so it varies in space. Probe ambient stays behind a switch at a small share.
- **Shafts need a dark neighbourhood.** The in-scatter must differ strongly between lit and shadowed froxels along the
  view. Haze where the sun is blocked contributes only transmittance loss, about 0 in-scatter.
- **Halos need the lamp's own visibility.** A lamp contributes only where its traced ray to the froxel is clear, so its
  halo is a cone or sphere cut by geometry, not a smear. The v3 ranking (in-view lamps by brightness x coverage) stays
  as the pick for the 4 traced lamps.
- **Acceptance on the eye pass:**
  - Contrast on the authored views at or above 0.98 of no-X3.
  - A visible shafts crop on Streets / Gorge.
  - Molten's lava glow reading through the haze.
  - Measured with Experimental's frame_compare.py.

  If contrast drops below 0.98, it does not ship.
- **Reusable worktree code:** the ring slot of 28 float4 (shadowColors + volLights) and the v3 lamp ranking in
  WfcRt.cpp carry over. rt_volumetric.hlsl and the quarter-size vol_ target are replaced by the froxel passes.

## Effort
~2 days: day 1 the D3D12 inject (rays + phase + probe ambient) and the integration / shared 3D texture; day 2 the
temporal filter, the GL apply (composite + translucency), the haze floor and the per-map tuning by eye.
