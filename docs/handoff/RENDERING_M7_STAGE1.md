# M7 stage 1: ray-traced shadows + AO (architecture A), report

Rendering, 2026-10-12, agents/rendering-rt (held until the HD pass is done, per the user's order). All numbers are
untimed, windowed, one exe at a time, on the user's PC (7950X3D + RX 7900 XTX), Gorge 64p lockstep unless named.

## What it does (WFC_RT_SHADOWAO=1, off by default)

- Static world (world + level BSP MDI sets, opaque prepassed subs; sky domes left out) as BLAS / TLAS at map load;
  characters added per frame through Systems' skinning feed (WFC_RT_CHARS=1; compute skinning shared with D3D12, BLAS
  refits, a per-frame TLAS).
- Once per frame at the end of the opaque pass (flushDynQueue): the world depth is copied, the dynamic objects are
  drawn depth-only, then one GL -> D3D12 -> GL round trip runs the RT passes (inline RayQuery): a soft sun-shadow ray
  (sun cone 0.27 deg) + 2 AO rays per pixel, the FidelityFX shadow denoiser (SDK v1.1.4, MIT, our own driver) and a
  depth-aware AO filter. Then a full-screen multiply on world pixels and the dynamic objects shaded with the results.
- Faithful by default: the world's occlusion of the sun stays the original's (baked lightmaps on the world, the
  per-object light visibility on dynamic objects). RT adds what the original approximated or lacked: character shadows
  on everything (self-shadowing included; the original character shadow projectors are not run), contact AO.
  WFC_RT_WORLDSHADOWS=1 also shadows dynamic objects by the world (for the later "modern" mode the user approved).
- RT-off is untouched: the RT shader variants are substituted only when WFC_RT_SHADOWAO is set at start-up.

## The four numbers the user asked for

| | |
|---|---|
| Performance | Gorge 1440p render, characters: frame interval 2.20 -> 3.11 ms (+0.90). FSR 3.1 Quality (1080p window, 1281x720 render): 2.63 -> 3.60 ms (+0.97, 380 -> 278 fps). Estimate at 1440p output with FSR 3.1 Quality: about 4.5 ms (about 220 fps), well above the 144 fps floor; RT uses about 1.1 ms of the plan's 3.5 ms budget |
| VRAM | static BLAS 112 MB (Gorge); characters about 10 MB (11 slots); targets 6 x render size (1440p: about 90 MB with the denoiser's); the whole process's device-local usage 2.9-3.3 GB |
| Synchronisation | one round trip per frame: about 0.4 ms main thread (mostly AMD's semaphore spin) + the GPU bubble; GL waits for D3D12 before translucency: "RT: round trip" 1.12 ms GPU at 1281x720, 1.68 ms at 1440p (the passes inside it) |
| Ray budget | 7-15 G rays / s on these scenes (any-hit shadow / AO rays). Inside the 3.5 ms budget at 1440p FSR 3.1 Quality: about 20 rays per pixel; stage 1 uses 3-4 (sun, a world re-test where a character blocks, 2 AO) |

GPU split at 1440p render with characters: shadow + AO rays 1.4-1.6 ms, AO filter 0.35 ms, denoiser 0.19 ms, world
composite 0.10 ms, dynamic depth pass 0.02 ms, character skinning 0.005 ms, refits about 0.03 ms.

## Checks

- Stage 0 exit: primary-ray depth matches the raster depth within 1e-3 on 99.97-99.98 % of pixels (Gorge).
- Characters: D3D12 reads of the skinned positions equal GL's in 101 / 101 hash checks (Systems), GPU vs CPU skinning
  max error 2.4e-7.
- Denoiser: 256 output levels, mean |denoised - raw| 0.004 (hard sun); soft edges with the sun cone.
- Look, RT on vs off (lockstep match shots, 1080p): Gorge 0.0-0.3 % of pixels changed, Rust 0-2.3 % (the player's
  vehicle self-shadowing and its shadow on the ground outdoors, contact AO); no false darkening indoors.

- Temporal stability (lockstep RT off / on, 61 consecutive frames, 2560x1421 windowed): on the pixels that are static
  with RT off, RT adds a mean frame-to-frame change of 0.006 (Gorge) / 0.041 (Rust) levels; flicker above 4 levels on
  0.007 % / 0 % of them; their 60-frame per-pixel std with RT on 0.12 / 0.21 (max 0.5 / 0.6). Stable.
- Before / after sheets for the user: work/fsr3/sheet/rt_{rust_00300,rust_01500,gorge_01200,gorge_00600}.png in the
  Rendering worktree (full frames + 2x crops of the region with the most change).

## Found and fixed on the way

- The DXR front-face convention is the reverse of GL's with our positions (depth check 10 % -> 99.98 %).
- The denoiser's passes need FFX_HALF + 16-bit types (the f32 path is stubs: filtered tiles were 0).
- Rust's 4.5 km Spacedome blocked every sun ray (sky subs now left out).
- One shadow mask for all lights darkened characters indoors: the RT visibility now shadows only the traced sun.

## Open

- One D3D12 device for the presenter, FSR 3.1 / DLSS and the RT bridge (three today): before stage 2.
- Faithful world shadows on dynamic objects need the components' CastShadow / bCastDynamicShadow flags in the render
  data (AssetTools); without them WFC_RT_WORLDSHADOWS shadows characters by occluders the original did not count.
- Masked (alpha-tested) geometry is not in the TLAS yet (foliage, grates cast no RT shadows).
