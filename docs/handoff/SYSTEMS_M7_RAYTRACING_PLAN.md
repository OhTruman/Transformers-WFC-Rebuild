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

### Stage-0 measurement: static-world BLAS (2026-10-12, tools/systems/blas_probe.cpp, RX 7900 XTX, DXR 1.1)

world.glb as the game loads it (every placed mesh baked into one world-space mesh, so no instancing: an upper bound),
PREFER_FAST_TRACE | ALLOW_COMPACTION, opaque, built once:

| Map | Triangles | BLAS compacted | Build (GPU, one BLAS / per slice) |
|---|---|---|---|
| Gorge | 1.22 M | 81 MB | 13.3 / 10.5 ms |
| Berth | 1.29 M | 82 MB | 16.8 / 13.0 ms |
| Complex | 1.64 M | 104 MB | 16.7 / 12.6 ms |
| Debris | 1.64 M | 112 MB | 17.3 / 12.8 ms |
| Molten | 1.70 M | 114 MB | 17.7 / 14.4 ms |
| Rust | 1.90 M | 129 MB | 21.0 / 15.4 ms |
| Streets | 1.98 M | 143 MB | 17.9 / 16.0 ms |
| BrokenHope | 2.16 M | 146 MB | 27.0 / 27.5 ms |
| Seed | 2.45 M | 161 MB | 25.1 / 20.7 ms |
| Remnant | 2.86 M | 194 MB | 30.9 / 33.2 ms |

- Compaction saves ~57 % (pre-compaction 190 - 445 MB); one BLAS per material slice (1,975 - 4,764 slices) costs < 1 % more
  memory than one BLAS and builds as fast or faster, so the opaque / alpha-tested split is free. Scratch: one whole-world
  BLAS needs 193 - 450 MB transient; per slice, built sequentially through one scratch buffer, <= 2.3 MB.
- Build cost is a one-off at map load (10 - 33 ms GPU): no per-frame cost for the static world.
- Plus positions + indices kept for hit shading: 31 - 73 MB per map (Gorge 31 MB).
- Next for stage 0: real instancing (repeated static meshes as TLAS instances of one BLAS: needs the per-node meshes,
  not the baked world mesh), the alpha-tested flag per slice, and the skinned-character refit cost.

### Stage-0 measurement: skinned characters per frame (2026-10-12, tools/systems/skin_probe.cpp, RX 7900 XTX)

64 characters = 8 robot bodies (Car2, Car4, Jet, Jet4, Tank2, Tank3, Truck, Truck3) x 8, real meshes / joints / weights:
534 k vertices, 623 k triangles, 6,464 joints. Per frame on the GPU (300 frames, D3D12 compute skinning, inputs in GPU-local
memory, joint matrices copied up each frame):

| Path | Skinning | 64 BLAS | TLAS | Total |
|---|---|---|---|---|
| refit (ALLOW_UPDATE, PREFER_FAST_BUILD, PERFORM_UPDATE) | 0.21 ms | 0.61 ms | 0.15 ms | **0.97 ms** |
| full rebuild (PREFER_FAST_BUILD) | 0.23 ms | 2.51 ms | 0.15 ms | 2.89 ms |

- Refit is the path; 98 MB of character BLAS (refittable, fast build), 6 MB of skinned positions.
- ~1 ms of the ~3.5 ms RT budget if every character is refit every frame: worth trimming before stage 1's budget check:
  only characters that can matter (on screen or inside the shadow-casting range of the lights traced: in a 64p match
  usually a fraction of the 64), a reduced-LOD proxy mesh for RT (shadows / AO tolerate it), distant characters refit every
  second frame, and a full rebuild only when a refit degrades (e.g. transforms vehicle <-> robot: a different mesh anyway).
- The compute skinning pass can also feed the raster path (today the vertex shader skins), so part of the 0.2 ms is a
  shift, not an addition, if Rendering takes the skinned buffer for drawing too.
- In production the skinning runs in GL compute into a buffer shared with D3D12 (one more shared resource in the existing
  hand-off); the costs above are the D3D12-side equivalent.

### Stage-0 measurement: blend modes and instancing (2026-10-12, tools/systems/m7_slices.py, world.glb structure)

| Map | Placed triangles | Opaque | Masked (alpha-tested) | Translucent / additive | Unique-mesh triangles | Placements per mesh |
|---|---|---|---|---|---|---|
| Gorge | 1.22 M | 98.7 % | 0.4 % | 0.9 % | 0.13 M | 9.7x |
| Berth | 1.29 M | 99.4 % | 0.3 % | 0.3 % | 0.15 M | 8.5x |
| Complex | 1.64 M | 99.4 % | 0.1 % | 0.5 % | 0.15 M | 11.1x |
| Debris | 1.64 M | 98.4 % | 0.8 % | 0.8 % | 0.21 M | 7.9x |
| Molten | 1.70 M | 99.2 % | 0.2 % | 0.5 % | 0.18 M | 9.3x |
| Rust | 1.90 M | 99.7 % | 0.0 % | 0.3 % | 0.29 M | 6.6x |
| Streets | 1.98 M | 99.2 % | 0.5 % | 0.3 % | 0.23 M | 8.6x |
| BrokenHope | 2.16 M | 99.6 % | 0.0 % | 0.4 % | 0.30 M | 7.3x |
| Seed | 2.45 M | 99.7 % | 0.1 % | 0.3 % | 0.28 M | 8.7x |
| Remnant | 2.86 M | 98.2 % | 0.8 % | 1.1 % | 0.25 M | 11.4x |

- Flags: opaque BLAS geometry for >= 98 % of the world (D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE: no any-hit); masked
  <= 0.8 % (the RayQuery candidate loop with the opacity-mask texture, opacity_mask_clip from the material); translucent /
  additive <= 1.1 %: not in the BLAS (raster only). Materials carry extras.blend_mode in world.glb, so the split is free.
- Instancing: world.glb keeps its node -> mesh structure (the game's loader bakes it). One BLAS per unique mesh (150 - 320
  per map) + a TLAS over the placements (1,700 - 4,800 instances) would hold 6.6 - 11.4x fewer triangles: at the measured
  ~66 B per compacted triangle, ~9 - 20 MB of static BLAS per map instead of 81 - 194 MB (estimate from the ratio). The
  cost is TLAS traversal over thousands of instances vs one merged BLAS: measure trace time both ways in stage 1 before
  choosing (memory is not the constraint on a 24 GB card; trace time may be).

### Compute-skinning feed, built (2026-10-12, HELD with agents/rendering-rt: docs/handoff/SYSTEMS_M7_SKINFEED*.patch)

The interface was agreed with Rendering on 2026-10-12. Rendering owns the bridge, the TLAS cadence, the passes, denoising and the composite; Systems owns the feed.
- **GL side (render/gl/WfcRtSkin.cpp, WFC_RT_CHARS=1)**
  - drawSkinned notes each instance: bone-texture row, palette serial, blend mode / alpha, model matrix, model-space bounds.
  - Just before the round trip's GL signal, rtSkinFeed(afterCharacters) runs a compute pass that mirrors the vertex shader's wfcSkin (positions only; same rows; uSkin 2 blend). It writes model space into one buffer shared with D3D12 (float3, a region per instance slot, 2 M vertices / 24 MB by default, WFC_RT_CHARVERTS).
  - The buffer is listed in the round trip's semaphore signal / wait (rtBridgeSetSignalBuffers).
- **D3D12 side (render/d3d12/D3D12RtDynamic.{h,cpp})**
  - One index buffer per model.
  - One BLAS per instance slot (ALLOW_UPDATE): built once, then refit in place when GL rewrote its positions.
  - Characters go into the TLAS with mask 0x4 and the model matrix as the instance transform, so movement without a new pose costs no refit. Cull is disabled.
  - Hooks Rendering calls: rtDynamicAttach / rtDynamicRecord (before their TLAS build) / rtDynamicAppendInstances / rtDynamicClear.
- **Refit culling**
  - A character is in the TLAS only if its world box is on screen or within WFC_RT_CHARRANGE (30 m) of the camera.
  - A changed pose beyond WFC_RT_CHARFAR (20 m) is re-skinned / refit on alternate frames by slot.
  - An unchanged pose (same serial, blend, alpha and row) is never redone.
  - Slots are released after 300 frames unseen.
- **Timing**
  - With Rendering's end-of-opaque round trip (their 57a7654), the feed uses this frame's palettes, with no lag.
  - At the mid-map spot it would use last frame's (one frame of lag, consistent).
- **Proof** (09c 7fd831e + Rendering a4a03df + the feed; Streets, 5v5 TDM real play, 1080p window, RX 7900 XTX):
  - D3D12 hashes the region it reads; it matched GL's readback in 101 of 101 checks.
  - GL output vs a CPU skinPose on the same bone-texture palettes: max error 2.4e-7 (coordinates up to 3.9).
  - 0 GL errors.
  - Per frame: about 13 skinned instances noted, 6 - 9 in the TLAS, 3 - 4 refits (far-alternate and unchanged skips about 3.5), D3D12 refits 0.025 ms GPU, about 10 MB BLAS for 11 slots.
  - The GL timestamp pair around the dispatches reads about 0.001 ms. Not trusted: to recheck with the frame profiler.
  - 64-player numbers are still to come; the stage-0 probe estimated 0.97 ms for 64 at full refit.
- **Experimental's review (SYSTEMS_M7_SKINFEED_REVIEW.patch, on Rendering's fd231d2)**
  - Fixed:
    - Pawns whose prep the occlusion test skipped are fed at their last prepared pose.
    - A BLAS is rebuilt after 120 refits (WFC_RT_CHARREBUILD) or when the pose's box outgrows its build box by a quarter.
    - Slots go back 60 frames after the last note (WFC_RT_CHARDROP) and at once when a model's buffers go.
    - The first slot failure is logged with the capacity.
    - Warm-up draws are not noted; a key drawn twice with another matrix is counted in the log.
    - sm.fp is part of the model identity.
    - The map-unload clear waits idle when dynamic work exists, else it abandons (leaks) instead of freeing.
  - Item 6 (the allocator wait result) was already in Rendering's fe343de.
  - CPU-skinned fallbacks (more bones than the GPU skin supports, bone rows full) are logged once and stay out of the TLAS: none occur on the shipped characters (max 106 joints of 128; 512 rows).
  - Rerun (Streets 5v5): 106/106 hash checks equal, 0 GL errors, about 1 rebuild per 100 refits, 17-22 skipped preps per 600 frames now in the TLAS, 0 double draws.
- **Open**
  - The world box ignores weapon attachments.
  - The full index list is used: hidden / translucent sub-meshes are not filtered yet.
  - A reduced-LOD RT proxy is still an option.

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
   **User decision 2026-10-12:** yes to a later, separate, optional **"RT lighting"** mode. In it, ray-traced GI *replaces* the baked
   lightmaps for a modern look. The faithful baked lighting stays the default; the mode is a settings choice, never auto-detected.
   - Replace vs calibrate:
     - The calibrated GI above stays an add-on to the baked look: it adds bounce for dynamic objects and is matched to the lightmaps' mean luminance.
     - "RT lighting" drops the lightmaps (and the vertex lightmaps / light-environment probes for characters) and lights the world from the
       level's own lights plus sky.
     - Exposure / tone is set per map against the baked look only as a starting point. The user reviews both side by side per map.
     - It needs every authored light (the LightsVisibilitiesVolume table: 268 on Streets) with its radius / falloff and a sky term.
     - Emissive surfaces count as light sources (or the result is darker than baked).
   - Brixelizer GI vs RT GI for it:
     - Brixelizer GI (FidelityFX, MIT): an SDF cascade around the camera plus a radiance cache. Cheaper and stable, with coarse
       detail (a few cm voxels near, coarser far). It needs no TLAS, but its own SDF build from the static meshes (and characters, as
       AABB / SDF instances).
     - RT GI: rays through the existing TLAS (static world + the skinned characters above) into a world-space radiance cache
       (e.g. a probe grid or ReSTIR GI). Sharper contact bounce and true character contribution. Higher cost, and it needs the denoiser.
     - Recommendation: prototype both on Gorge and Rust once stage 2 is decided. Keep whichever reaches the stage-1 budget class at 1440p
       FSR Quality with acceptable stability. Brixelizer is the likely default on mid GPUs, and RT GI may be offered as "High".
4. **Path-traced mode, assessed only**: a feasibility report (fps on the 7900 XTX at 1080p / 1440p with FSR 3.1
   Performance, denoise quality), no ship decision without the user.

## 7. Decisions for later

- B vs C at stage 2 (needs stage 1's round-trip cost).
- NRD licence read (with the DLSS / Streamline read), only if FidelityFX quality is short.
- RT GI vs Brixelizer GI at stage 3, for both the calibrated add-on and the user-approved "RT lighting" mode (§6 stage 3).
- 2026-10-12: superseded by SYSTEMS_RT_REALISTIC_M6_PLAN.md (the user's "brand new" goal): R1-R5 order, a probe-volume RT GI
  first, Brixelizer as a fallback, light sources / calibration, and how M6 Modern materials combine with it.

## 8. Sources

- AMD FidelityFX Denoiser (shadow / reflection denoisers, DX12 / Vulkan): https://gpuopen.com/fidelityfx-denoiser/ ;
  FidelityFX SDK (MIT): https://gpuopen.com/manuals/fidelityfx_sdk/
- AMD FSR SDK 2.2 / Ray Regeneration (RX 9000 series required, MIT): https://www.cgchannel.com/2026/03/amd-releases-fsr-sdk-2-2-with-new-ai-denoiser-ray-regeneration/
- NVIDIA NRD (licence terms in the repository): https://github.com/NVIDIA-RTX/NRD
