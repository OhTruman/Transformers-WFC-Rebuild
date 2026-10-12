# M7 plan: optional ray tracing (design only)

Systems, 2026-10-12, against 09c 42f7f5b. User order (2026-10-12): frame generation, then the HD texture pass, then ray
tracing. User scope (2026-10-09): RT shadows + AO -> reflections -> GI -> a path-traced mode *assessed*; every effect a
setting, OFF by default; the faithful GL path stays the reference. No code here; Rendering leads, owners are proposals.

## 1. Where we start (facts, 09c)

| | |
|---|---|
| Renderer | OpenGL (WfcPipeline), forward shading with a depth prepass; the faithful path, byte-identity-gated |
| D3D12 | exists, opt-in: presenter (flip swapchain; worker-thread present with glClientWaitSync, Rendering, M5 item 1), FSR 3.1 SR (D3D12Upscaler; GL scene colour / depth / velocity / reactive shared with D3D12), Agility SDK selection (D3D12Core beside the exe; SM 6.6+) |
| Hand-off cost | GL -> D3D12 -> GL round trip: FSR 3.1 has a fixed 1.4 - 2.9 ms (hand-off + upscale; Gorge 1440p fullscreen native 547 fps vs FSR 3.1 Quality 294); the AMD driver implements the cross-API semaphore signal as a CPU-side poll until the GPU reaches it (Systems' CpuProf, 2026-10-12) |
| User PC | 7950X3D + RX 7900 XTX (DXR tier 1.1: inline RayQuery + pipelines), desktop 2560x1440 @ 144 Hz; native 1440p is CPU / submit-bound at ~550 fps (1.8 ms) |
| Lighting (original) | baked lightmaps (Gorge: 1,363 lightmapped components, vertex lightmaps), 411 lights (most baked; a few dynamic), character shadow projectors, fog, cube / env reflections; no RT anywhere in the original |
| Geometry | Gorge world.glb 1.22 M triangles / 1.51 M vertices (+ BSP 1.8 k, decals); characters ~7 - 8 k vertices each, GPU-skinned in the vertex shader (no skinned vertex buffer exists), 64 players in the big modes; movers / rigid props posed per frame |

Budget that matters for the user: with RT on, the floor is the panel's 144 fps = 6.94 ms per frame at 1440p. Today native
is 1.8 ms and FSR 3.1 Quality ~3.4 ms, so RT has roughly **3.5 ms** at 1440p with FSR 3.1 Quality (hand-offs included).

## 2. Architecture options

| | A. Inline DXR passes on D3D12, GL interop | B. Hybrid: D3D12 ray passes (pipelines + inline) beside GL raster | C. D3D12 renderer port |
|---|---|---|---|
| What | compute passes with RayQuery (DXR 1.1) read GL's depth (+ normals) from shared images, write RT masks back; GL shading samples them | as A, plus ray-generation pipelines (reflections / GI need hit shading: material + texture fetch at hit points) | the scene renderer itself on D3D12 (GLSL -> HLSL, WfcPipeline's passes); GL stays the faithful reference |
| Hand-offs | +1 round trip mid-frame (after the depth prepass, before shading) on top of FSR's | same as A | none (also removes FSR / FG / DLSS hand-offs) |
| Effort | M per effect (S for the first: shadows) | M-L (hit shading needs the material system on D3D12: bindless textures, SM 6.6 ResourceDescriptorHeap) | XL (weeks: every pass, the materials (521 on Gorge), MDI, lightmaps, FX, HUD composite), two renderers to maintain |
| Risk to the faithful path | none (opt-in passes) | none | none if GL stays default; but RT-on frames come from a different renderer |
| Fits | shadows + AO (need position / normal only) | reflections, GI (need what a ray hits) | everything, at the best frame times |

Recommendation: **A for stage 1 (shadows + AO)**, measured; **decide B vs C at stage 2 (reflections)** with the stage 1
numbers in hand. Reflections and GI need hit shading, i.e. the materials on D3D12 either way: B builds a second material
path for hits only; C moves everything. If stage 1 shows the extra round trip costs more than ~1 ms at 1440p, C is the
better long-term base (it also takes FSR 3.1's fixed cost away). Don't start C before that number exists.

Stage-1 detail (A): after GL's depth prepass, one shared submission: GL signals (worker-thread path, as the presenter),
D3D12 runs RT shadows + AO (+ denoise) at render size, signals back, GL's forward shading samples the masks. With FSR 3.1
on, FSR runs in a second D3D12 submission at the end of the frame. Normals: a normal prepass (octahedral RG16) beside the
depth prepass, or reconstructed from depth (cheaper, worse at edges); start with reconstruction, measure.

## 3. BLAS / TLAS sources

- Static world: world.glb + bsp.glb at map load: one BLAS per mesh section group (opaque vs alpha-tested split: foliage /
  grates take the RayQuery candidate loop with an alpha-mask texture fetch), PREFER_FAST_TRACE + compaction, built once;
  memory to measure (Gorge 1.2 M triangles: estimate 100 - 200 MB; a budget line in the settings row's tooltip).
  Mode-hidden objective bases / destructibles: instance masks, not rebuilds.
- Movers / rigid props: their own BLAS, per-frame instance transforms (WfcMotion already has this frame's and last frame's
  model matrices).
- Characters (and vehicle forms): skinning happens in the vertex shader today, so RT needs a compute skinning pass writing
  skinned positions into a buffer shared with D3D12, then a BLAS refit per character per frame (ALLOW_UPDATE, rebuild on
  a pose-topology change). 64 x ~8 k vertices = ~0.5 M vertices / frame; refit + skinning cost is an estimate to measure
  (order of tenths of a ms on the 7900 XTX). Far characters: a lower LOD or excluded beyond a distance (they are
  sub-pixel in shadows / AO anyway).
- Excluded from the TLAS: particles / sprites / translucent FX (raster only; the reactive mask handles them for FSR),
  the HUD, decals (they shade, they don't occlude).
- TLAS: rebuilt per frame (a few thousand instances).
- Systems could own the data plumbing (compute skinning into shared buffers, the BLAS / TLAS feed, LOD selection) while
  Rendering owns the ray passes, denoise and composite.

## 4. Denoising (licences checked 2026-10-12)

| Option | Effects | Licence | On the 7900 XTX |
|---|---|---|---|
| AMD FidelityFX Denoiser (Shadow, Reflection) | shadows, reflections | MIT (FidelityFX SDK) | yes (DX12 / Vulkan) |
| AO | (no dedicated FFX RT-AO denoiser) | - | a small spatio-temporal filter of our own, or the shadow denoiser's approach |
| NVIDIA NRD (ReBLUR / ReLAX / SIGMA) | shadows, AO, reflections, GI | NVIDIA's own licence (object code inside an app, not stand-alone; the user / Integration must read it, as with DLSS / Streamline) | yes (vendor-agnostic) |
| AMD FSR Ray Regeneration (FSR SDK 2.2, AI) | RT denoise | MIT | **no**: needs a Radeon RX 9000 series GPU |
| AMD Brixelizer GI (FidelityFX) | GI without RT (SDF) | MIT | yes; an alternative for stage 3 if RT GI is too slow |

Recommendation: FidelityFX Denoiser for shadows (stage 1) and reflections (stage 2), our own AO filter; NRD only if the
licence is accepted and FFX quality isn't enough; GI: decide between RT GI (NRD ReLAX) and Brixelizer GI at stage 3.

## 5. Pairing with FSR 3.1 (and frame generation)

- RT runs at the render size (FSR's input size), with the same projection jitter; the denoisers reproject with
  WfcMotion's jitter-free motion vectors. Denoise before upscale: FSR sees a clean, jittered image.
- Reflections: their pixels go into FSR's reactive mask (history must not drag them). Shadows / AO: no reactive.
- Camera cuts (respawn, death cam, map load): reset the denoisers' history together with FSR's (Rendering's camera-cut
  hooks).
- Frame generation interpolates the final frame; RT adds nothing special there. RT-on without FSR is allowed (native RT)
  but will rarely meet 144 at 1440p; the settings tooltip says so, auto-detect never turns RT on (as FSR 3.1).

## 6. Stages and exit criteria

Every stage: OFF by default; with RT off the frame is byte-identical to the current reference (lockstep exact + the
presentation gates); a settings row per effect (Frontend + Systems; feature check: D3D12_RAYTRACING_TIER_1_1 via
CheckFeatureSupport OPTIONS5, else the row is greyed); auto-detect never enables RT.

0. **Infrastructure** (BLAS / TLAS, compute skinning, shared depth / normals): a debug view whose primary-ray depth matches
   the raster depth within 1e-3 (view depth) on >= 99.5 % of pixels on 3 maps, characters included; build / refit / TLAS
   times and BLAS memory logged per map; no frame-time change with RT off.
1. **RT shadows (sun + dynamic lights) + RT AO**: shadow placement agrees with the original's intent (projected character
   shadows; per-map screenshot sheets for the user); temporally stable after denoise (no crawl on a static camera:
   per-pixel variance over 60 frames below a threshold); 1440p with FSR 3.1 Quality >= 144 fps p50 on Gorge / Rust 64p
   real play (the 3.5 ms budget); the round-trip cost measured and reported (B / C decision input).
2. **RT reflections** (roughness-gated, fallback to the existing env reflections above the threshold): the B / C
   decision taken first; budget as stage 1; reflections in the reactive mask; user review sheets.
3. **RT GI** (or Brixelizer GI): calibrated against the baked lightmaps (per-map mean luminance within a tolerance; the
   M6 modern-materials calibration method), user review; budget restated then (GI may need FSR Balanced / Performance).
4. **Path-traced mode, assessed only**: a feasibility report (fps on the 7900 XTX at 1080p / 1440p with FSR 3.1
   Performance, denoise quality), no ship decision without the user.

## 7. Decisions for later

- B vs C at stage 2 (needs stage 1's round-trip cost).
- NRD licence read (with the DLSS / Streamline read), only if FidelityFX quality is short.
- RT GI vs Brixelizer GI at stage 3.

## 8. Sources

- AMD FidelityFX Denoiser (shadow / reflection denoisers, DX12 / Vulkan): https://gpuopen.com/fidelityfx-denoiser/ ;
  FidelityFX SDK (MIT): https://gpuopen.com/manuals/fidelityfx_sdk/
- AMD FSR SDK 2.2 / Ray Regeneration (RX 9000 series required, MIT): https://www.cgchannel.com/2026/03/amd-releases-fsr-sdk-2-2-with-new-ai-denoiser-ray-regeneration/
- NVIDIA NRD (licence terms in the repository): https://github.com/NVIDIA-RTX/NRD
