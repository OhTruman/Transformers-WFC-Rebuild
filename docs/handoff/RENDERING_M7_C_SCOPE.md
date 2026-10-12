# M7 option C: scope of a D3D12 (or Vulkan) scene renderer (estimate, docs only)

Rendering, 2026-10-12, against 80c7dd5 (+ 0ebc73b). Input to the A vs C decision in Systems' M7 plan (fae1972). No code.

## Why this is on the table

Measured (0ebc73b, Gorge 64p, untimed ABBA): the mid-frame GL -> D3D12 -> GL hand-off architecture A needs costs
+0.75 ms of frame time at 1440p today and ~1.4 ms of GPU timeline (GL idles while D3D12 runs). FSR 3.1's end-of-frame
hand-off is ~1.5 ms more. With RT on (GPU-bound) that is ~3 ms of the plan's 3.5 ms budget at 1440p before any ray is
cast. A renderer that is already on D3D12 pays none of it, and FSR 3.1 / FG / DLSS run in the same device.

## What would be ported (sizes from 80c7dd5)

| GL subsystem | Today | What it holds | Port size |
|---|---|---|---|
| Device layer | GLExt.cpp 0.8 k lines + parts of GLRenderer | loader, uniform / texture caches, debug output | S-M: D3D12 device, descriptor heaps (bindless, SM 6.6 ResourceDescriptorHeap), upload rings, PSO cache, barriers |
| Frame orchestration | GLRenderer.cpp 2.1 k | frame order, GPU stamps, watchdog, settings, GFx hand-over | M |
| World pass | WfcPipeline.cpp 7.5 k + .h 1.2 k (most of it) | load / textures (DXT, sRGB S3TC, HD pack), MDI world + Hi-Z occlusion (compute), depth prepass, lightmap arrays, decals | L: MDI -> ExecuteIndirect, occlusion compute -> HLSL |
| Characters / dynamic | WfcPipeline (skinned, posed, weapons), WfcMovers 0.4 k | VS skinning, pawn occlusion queries, instancing | M |
| Shadows | WfcShadows.cpp 1.0 k, 6 shaders | character shadow projectors | M |
| Map FX / particles / translucency | WfcMapFx.cpp 2.2 k + sprites, fog volumes, distortion in WfcPipeline | particles, sprites, translucent sort, fog volumes, distortion | L (the most draw-state variety) |
| Post / screen effects | WfcPipeline post, HUD screen effects, WfcMotion 0.45 k, WfcFsr / WfcFsr3 / WfcFrameGen | post chain, motion vectors, FSR 1, FSR 3.1 / FG inputs | M (FSR 3.1 / FG get simpler: no interop) |
| Materials | matc.py 0.76 k + hlslexpr.py: GLSL per material (~521 on Gorge) | UE3 material graphs -> GLSL | M-L: an HLSL backend in matc (the expressions are HLSL-like already), uniforms -> constant buffers, every material re-verified |
| GFx UI (frontend + HUD) | src/ui/gl/GfxRendererGL.cpp 1.5 k, 8 shaders | Scaleform-compatible renderer | M; or keep it on GL for a first stage (below) |
| Inline shaders | ~25 GLSL programs (11 pipeline, 6 shadows, 8 UI) + small ones | | S-M each, part of the rows above |

GL-side C++ in scope: roughly 17-19 k lines, plus the material translator's backend.

Rough size: **one lane for 5-8 weeks** to a playable D3D12 path at parity on the eight TDM maps, and longer for full
image parity (FX edge cases, every map's materials). Most of the risk is in materials and map FX, not the device layer.
This is an estimate from line counts and pass inventory. It is not a measurement.

## Staging that keeps the game shippable throughout

1. C0: device layer, plus the world (static meshes, lightmaps, materials) into a shared texture, composited with GL's
   characters / FX. Debug only, to validate materials against GL early.
2. C1: the whole 3D scene on D3D12. The GFx UI stays on GL, drawn into its own shared overlay texture. The UI does not
   read the scene, so there is no mid-frame wait, only the end-of-frame composite the presenter already does.
3. C2: the GFx UI renderer ported. No GL in the frame at all.
4. RT (stage 1 shadows / AO) runs inside C1 onward with no hand-off. Stage 1 written now on A uses plain D3D12 compute /
   RayQuery passes that move across unchanged (they only need depth, normals and the TLAS).

## The faithful reference

The GL path stays the default and the byte-identity reference. Lockstep exact and the presentation gates keep running on
it, unchanged. The D3D12 path cannot be byte-identical to GL (different rasterisation rules, precision, sampler
behaviour), so it gets its own gate: per-map lockstep shot sets compared with GL under a tolerance (mean / p99
per-pixel difference, as hd_verify and the FSR checks already do), plus a frame-time gate. New modern features (RT,
DLSS, Modern materials) would live on the D3D12 path only. Gameplay-visible behaviour (HUD, FX timing, visibility
used by bots) must come from shared code, not per-renderer code. The cost is two renderers to maintain: every future
GL fidelity fix must be mirrored (matc fixes come for free once both backends share one front end).

## D3D12 or Vulkan

The user's 2026-10-09 plan allowed either ("a Vulkan renderer (or bridge)"). For the port itself:

| | D3D12 | Vulkan |
|---|---|---|
| Existing code | presenter, FSR 3.1, FG, Agility, RT bridge (all D3D12) | none |
| Shaders | HLSL: a matc backend and ~25 rewrites | GLSL reusable through glslang (layout / binding changes only): the biggest saving |
| FSR 3.1 / FG, DLSS / DLSS-G, RayQuery | all supported | all supported |
| Tooling | PIX, DRED, Agility SDK | RenderDoc, validation layers |

My lean is D3D12, because all the interop and upscaling work already done is D3D12 and DLSS (Streamline) is D3D12-first.
Vulkan would save the shader translation work, the largest single item. Worth the user's call together with A vs C.

## Recommendation

Keep stage 1 on A as a prototype (written to carry over). Decide C (and its API) with the user before stage 2. Starting
C would not block HD or the playtest builds, because the GL path is untouched.
