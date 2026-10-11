# M5 plan: temporal upscaling + frame generation via the D3D12 present path (design only)

Systems, 2026-10-10, against 09c 5ce8701. For when M1 closes (docs/MILESTONES_09c.md: "M5 Upscaling + frame generation
(D3D12 interop -> FSR 3 / DLSS / FG), optional, off by default"). No code here; owners are proposals, Rendering leads.

## 1. Constraints (user decisions, 2026-10-09)

- Everything optional, OFF by default; the faithful GL path stays the reference and the default.
- AMD and NVIDIA: FSR 3.x and DLSS, upscaling and frame generation, staged. The user's desktop is a 7950X3D + AMD GPU at
  3840x2160 / 144 Hz; DLSS is verified on the user's RTX 5090 laptop.
- Interop target: D3D12 (the GL <-> D3D12 probe passes on this machine; Vulkan was the fallback).
- Settings come from auto-detect, adjustable: frame generation stays off by default even on strong PCs.

## 2. What exists (09c)

| Piece | Where | State |
|---|---|---|
| FSR 1 (EASU + RCAS), spatial | src/render/gl/WfcFsr.cpp, setUpscaling 0-3, WFC_UPSCALING | shipped option; runs AFTER post (display-referred LDR) |
| Motion vectors: static geometry, GPU-skinned characters, rigid movers / props / pickups | WfcMotion.cpp (ef1688c, b3ee0f0) | RG16F at render size, prevUV - curUV; WFC_MOTIONVECTORS=1 |
| Halton(2,3) projection jitter, jitter-free velocities, reactive mask (R8) | c173fc4, WFC_TEMPORAL=1 | built; "runtime check pending a slot" |
| GL <-> D3D12 interop probe | D3D12InteropProbe.cpp (426e87d), WFC_D3D12PROBE=1 | PASS on the user's machine |
| D3D12 presentation stage 1 | src/render/d3d12/D3D12Presenter.cpp (be2f105), WFC_D3D12PRESENT=1 | byte-identical image; cost p50 1.43 -> 1.99-2.19 ms, p99 2.3-2.5 -> 7.4-9.7 ms (one shared texture serialises GL behind D3D12; ring + latency waitable noted as next) |

Missing: transparency / composition mask (optional for FSR, for FG UI), exposure input, camera-cut reset signals, a HUD
layer separate from the 3D frame, any D3D12-side upscaler / FG integration, the settings rows.

## 3. Target architecture

```
GL (render size, jittered)                       D3D12 (display size)
 opaque + translucency (HDR) -----+               +--> upscaler (FSR 3.1 SR | DLSS SR) --> post*  --+
 depth (D32F) --------------------+--> shared --->|                                                  |
 motion vectors (RG16F) ----------+   images      |   HUD layer (RGBA8, premultiplied, display size) |
 reactive / T&C masks (R8) -------+   + fences    +--> frame generation (FSR 3 FG | DLSS FG) <---------+
 HUD (GFx, Canvas) -> UI layer ----+                   -> FG swapchain (interpolated + real frames) -> present
```
*post: where the upscale sits relative to bloom / tonemap / CLUT is decision D2 below.

Principles:
- Zero-copy where possible: allocate the GL render targets the upscaler reads (scene colour, depth, motion, masks) from
  D3D12-exported memory (GL_EXT_memory_object_win32), not copied each frame. At 4K the copies would cost GPU bandwidth
  that eats the gain. One D3D12 fence per frame (GL signals, D3D12 waits), as stage 1 already does.
- The HUD must not be interpolated: frame generation needs the 3D frame without the HUD plus a separate UI layer, which
  FG composites on real and generated frames (both FSR 3 FG and DLSS FG via Streamline accept a UI layer or callback).
- The faithful path is untouched: everything runs only when an option is on (as FSR 1, jitter and motion vectors do today).

## 4. Work items (sizes: S < 1 day, M 1-3 days, L > 3 days of one lane)

1. D3D12 present stage 2 (Rendering, M), the prerequisite: a ring of shared targets + frame-latency waitable;
   resize path tested. Exit: p50 overhead <= 0.3 ms, p99 <= +1 ms vs GL present at 1080 and 4K, byte-identical image.
2. Verify the temporal inputs (Rendering, S): WFC_TEMPORAL runtime check (jitter sequence, velocity sign / scale, reactive
   mask coverage) with debug views; camera-cut flags (map load, respawn, death / spectate cams, cinematics, teleports).
3. Shared-memory render targets (Rendering, M): scene colour (RGBA16F), depth, motion, reactive allocated from D3D12
   memory; GL renders into them directly.
4. Upscaler backend, FSR 3.1 SR (Rendering, M): AMD FidelityFX SDK (MIT, D3D12): context per render / display size, dispatch
   inputs (colour, depth, motion with scale (-w, -h) to pixels, reactive, jitter, frame time, camera near / far / FOV,
   reset), output display-size colour. Quality modes: Native AA / Quality / Balanced / Performance / Ultra Performance.
5. Post placement (D2) and the display-size post pass (Rendering, S-M).
6. HUD layer separation (Rendering + Systems, M): GfxRendererGL currently draws the HUD over a copy of the 3D frame
   (src/ui/gl/GfxRendererGL.cpp: glCopyTexSubImage2D into backdropTex_, drawn first, then the movie on top). For FG,
   render GFx + Canvas into a transparent premultiplied RGBA8 target instead, shared with D3D12. The merged
   history-exact verifier (WFC_GFXVERIFY_IMAGE) makes this provable: composite(UI layer, frame) must equal today's frame
   byte for byte. SWF blend modes that read the backdrop (non-normal blends) can't be expressed as a plain overlay:
   count them per movie first; any HUD movie that uses them stays composited into the frame (interpolated with it).
   HUD screen effects that read the scene (LowHealth / StaticDischarge chains) stay in the 3D frame (they're 3D-space
   post); only the 2D layer moves.
7. Frame generation, FSR 3 FG (Rendering, L): FidelityFX frame-interpolation swapchain replaces the DXGI swapchain;
   UI layer in; pacing; HUD-less colour in. Needs VRR or vsync on; with tearing allowed, FG pacing degrades.
8. DLSS SR + DLSS FG via NVIDIA Streamline (Rendering, M-L, on top of 3-7): same inputs; Streamline's swapchain hooks;
   Reflex for latency (DLSS FG requires it). Verified on the RTX 5090 laptop only.
9. Settings + auto-detect (Frontend + Systems, S): rows Upscaler (Off / FSR 1 (existing) / FSR 3.1 / DLSS), Quality
   mode, Sharpness, Frame generation (Off / On), Latency mode (Anti-Lag 2 / Reflex when available); auto-detect picks
   upscaling only when native misses the refresh; FG off by default.
10. Frame limiter interplay (Systems, S): Win32Window's PC frame cap and vsync must cap the BASE rate when FG is on
    (output = 2x base); with the user's 144 Hz panel, base 72 + FG = 144.

## 5. Options (pick one path; costs are for one lane at today's pace)

| Option | Gets the user | Cost | Notes |
|---|---|---|---|
| A. Driver frame generation (AMD Fluid Motion Frames 2 / NVIDIA driver smooth motion) over the D3D12 present | FG without integration | item 1 only (M) | HUD gets interpolated (artifacts at low base fps); to verify: the drivers' API / version support for this path and whether they engage on a D3D12 flip swapchain fed by GL. Zero game code beyond stage 2. |
| B. FSR 3.1 upscaling only | better image than FSR 1 at the same cost (temporal AA included) | 1-5 (M+M+M+S) | testable on AMD and NVIDIA; no UI split needed |
| C. B + FSR 3 frame generation | FG with a clean HUD | B + 6 + 7 (+L) | the user's AMD desktop path |
| D. C + DLSS SR / FG | NVIDIA quality + FG | C + 8 | needs the 5090 laptop for every check |

Recommendation: 1 -> 2 -> B (3, 4, 5) -> 6 -> C (7) -> D (8); A as a quick experiment right after item 1 (it may give
the user FG on the desktop for free while C is built). Items 9 and 10 travel with B and C.

Value check: wherever the base rate already reaches the panel's 144 Hz at 4K, FG adds nothing (measure per map at
2160p before deciding; the M1 numbers are what to check). It matters
for the later RT / modern-materials modes (M6 / M7) and for weaker PCs; temporal upscaling (B) is the enabler for RT at
4K. So B is worth doing first; C and D can wait for M6 / M7 to need them.

## 6. Decisions needed

- D1 (user): the order above, and whether option A is wanted as a stopgap.
- D2 (Rendering): upscale before bloom / tonemap / CLUT (HDR input; post at display size: best quality, + post cost) or
  after (LDR, like FSR 1 today; cheaper, lower quality). Proposal: before, with the post pass at display size, measured.
- D3 (user / Integration): licensing. FidelityFX SDK is MIT. DLSS / Streamline ship NVIDIA's runtime DLLs under the NVIDIA
  RTX SDK licence (attribution + redistribution terms): needs a read before shipping D.
- D4: minimum OS / driver for the D3D12 path (Windows 10 21H2+? D3D12 feature level 12_0), and the fallback (GL present,
  FSR 1) when D3D12 init fails at runtime.

## 7. Validation

- Faithful path: image byte-identical to the reference with every option off (existing presentation gates).
- D3D12 present: byte-identical (WFC_D3D12SHOT), overhead rows at 1080 / 4K, 2 verdict reps (MILESTONES measurement rule).
- Temporal upscalers: no byte identity possible; gates = per-map screenshot sheets vs native (user review), ghosting
  checks under particles / glass (reactive mask), motion-vector debug view, camera-cut resets (no smear after respawn).
- FG: frame pacing (PresentMon-style intervals from WFC_PRESENTTRACE), HUD clean on generated frames (capture both),
  latency with Anti-Lag 2 / Reflex on vs off.
- Stability: 64p real-play soak per backend (no device-removed, no interop errors), window resize / fullscreen toggles,
  alt-tab, map travel.
