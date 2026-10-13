# "RT Realistic" lighting + M6 Modern materials: plan (docs only; Systems, 2026-10-12)

**User, 2026-10-12** (relayed by Integration):
- "My goal is to make this game look brand new."
- On the RT sheets: "I want realistic lighting and shadows for everything."

That brings forward the approved optional "RT lighting" mode, here called **RT Realistic**. Faithful (baked lighting, original materials) stays the default. Every step below is optional, off by default, never auto-detected, and byte-identical when off.

## 1. Where we are (facts)

**Rendering's M7 stage 1** (agents/rendering-rt, held; docs/handoff/RENDERING_M7_STAGE1.md):
- A soft RT sun shadow (cone 0.27 deg) + 2 AO rays per pixel at the end of the opaque pass, plus the FidelityFX shadow denoiser.
- It shades characters / dynamic objects and adds character shadows + contact AO on the world. The world keeps its baked sun occlusion (faithful).
- Cost: +0.9-1.0 ms at 1440p render (7950X3D + RX 7900 XTX). Ray budget: about 20 rays per pixel inside 3.5 ms at 1440p with FSR 3.1 Quality.

**Stage 2 in progress**: reflection rays whose hits evaluate the original material graphs, compiled to HLSL by matc's new HLSL backend. That is the hit-shading machinery GI also needs.

**How the world is lit today**:
- UE3 directional lightmaps: three coefficient textures, uLM0-2, with uLMScale per page. Vertex lightmaps where authored.
- The sun, every static-channel light, the sky light and the bounce are all baked into them together; there is no separate sun shadow map.
- Dynamic objects use light environments: the 2 brightest lights direct, the rest folded into an ambient cube; plus the sky-light hemisphere.

**Light data available per map** (lighting.json + the decoded LightsVisibilitiesVolume table): 268 authored lights on Streets. Each has:
- type: point / spot / directional / sky hemisphere;
- colour x brightness, radius, falloff exponent and cone;
- Static / Dynamic channels, CastShadows / CastDynamicShadows / CastCompositeShadow, light-function flag;
- a GUID.

**Materials**: matc translates each original graph to `wfcMaterial()`, which outputs UE3's inputs: DiffuseColor, SpecularColor, SpecularPower, tangent Normal, Emissive, Opacity / OpacityMask. Lighting is UE3's Phong / Blinn-style specular with lightmap-directional specular.

## 2. RT Realistic: the order (each step is visible on its own and kept if the next one slips)

**R1: RT sun shadows on the world, replacing the baked sun occlusion (keeps the bake for everything else)**
- Problem: the sun's baked direct term cannot be read out of the lightmaps.
- Proposal: an offline per-map "baked sun visibility" texture V_bake at lightmap resolution, made by Systems' tools.
  - Rasterise each lightmapped triangle in lightmap-UV space to get each texel's world position and normal.
  - Trace the sun ray (same cone) through the static BLAS, which we already build. Assume the same lightmap UVs and padding as the bake.
- Fit per map: the sun's baked scale k is a robust regression on fully lit texels (V_bake = 1): LM = I + k * max(N.L, 0) * sunColor.
- At run time, per pixel:
  - indirect = LM - V_bake * k * D_sun;
  - final = indirect + V_rt * k * D_sun.
  V_rt is the RT visibility, soft and per pixel, so it is sharp where the lightmap was blurry and it includes characters and moving objects.
- Where RT and the bake agree, the image is exactly the original's. Where they differ, the shadows are the new RT ones.
- Cost: the stage-1 sun ray already exists for dynamic pixels; extending it to world pixels costs about +0.3 ms at 1440p. The textures add about 1/3 of the lightmap pages' memory (R8).
- Exit:
  - V_bake vs the visible baked shadows: SSIM over the lightmap pages, checked for UV mismatch.
  - Calibration error: mean |final - LM| on pixels where V_rt = V_bake, below 1 / 255 after the tonemap.
  - The user's sheet.

**R1 details agreed with Rendering (2026-10-12)**. These supersede the max(N.L, 0) fit above.

**Owners**: the offline V_bake / fit tool is Systems'. The runtime split (lightmapped base pass, lightmap specular, HLSL hit shading) is Rendering's. This replaces Rendering's earlier idea of re-baking an indirect-only lightmap with a GPU path tracer.

**Lightmap UVs**: the original cooked ones, from the 14-float world vertex in the render data:
- layout: pos 0-2, normal 3-5, tangent 6-9 (w = handedness), UV0 10-11, UV1 12-13;
- page coordinates = uv1 * lmCoord.xy + lmCoord.zw per sub, where lmCoord is lighting.json's per-component CoordinateScale / CoordinateBias.
- Rasterise with conservative coverage and dilate 2 texels (texel-centre alignment against the cooked LightMapTexture2D is unverified).
- Vertex-lightmapped components (LMT_1D) get V_bake per vertex.

**Decode** (kFSMainLM): L = sum_i (n . B_i)^2 * tex_i(uv) * scale_i.
- The basis B_i is HL2-style: (0, 0.816, 0.577), (-0.707, -0.408, 0.577), (0.707, -0.408, 0.577), with n the tangent-space material normal.
- scale_i = Sub::lmScale[3][3], UE3's per-coefficient ScaleVectors.
- Pages are RGBA8, some sRGB (RtLightmap.srgb). All subtraction happens in linear space after decode x scale, never in the texel encoding.

**Sun fit**: per coefficient, in that basis.
- Coefficient i's sun share = k * max(B_i . L_t, 0)^p * sunColor, with L_t the sun direction in the texel's tangent frame.
- k and p are fitted per map on fully lit texels (UE3's projection weight is fitted, not guessed).
- The runtime split subtracts V_bake x share per coefficient and adds V_rt x share. Clamp each channel to >= 0.

**Lightmap specular** (lmSpec) uses the same three coefficients, so the split must feed it too. Otherwise the baked sun highlight stays in the new shadows.

**Halo risk**: Lightmass penumbrae are soft.
- Trace V_bake with the runtime sun cone (0.27 deg half-angle, or the area-light setting), supersampled 4x4 per texel.
- Rendering reviews the edges on full-frame pairs.

**R2: contact-hardening soft shadows + AO everywhere**
- Penumbra from the sun cone with distance-to-occluder (the stage-1 ray already returns the hit distance): the denoiser's blur radius scales with it.
- RT AO also multiplies the world's indirect term (from R1's split), not the direct term.
- Cost: AO rays are already traced for every pixel; +0.1-0.2 ms for the world composite.

**R3: reflections** (Rendering's stage 2)
- Roughness-gated, with a fallback to the existing env reflections.
- Their quality depends on M6's roughness (section 4); with original materials, SpecularPower drives the gate.

**R4: lightmap-free lighting = GI (the "brand new" look)**
- Direct light from every authored light, per pixel:
  - a clustered light list built from lighting.json;
  - RT shadows for the sun plus the most important local lights per tile (stochastic light selection; ReSTIR DI later if noisy).
- Sky: rays that miss the scene return the SkyLight's upper / lower hemisphere colours. This also serves as sky visibility.
- Indirect: RT GI (section 3). Emissive materials are light sources through GI hits (matc's EmissiveColor). Light functions come later (flag kept).
- The lightmaps are not sampled in this mode. Calibration: section 3.3.

**R5 (later)**: specular GI from the probe volume for rough surfaces; ReSTIR GI or a short screen-space trace for small-scale bounce; dynamic lights (muzzle flashes, explosions) as RT-shadowed lights.

## 3. GI: decision, light sources, calibration (to agree with Rendering)

### 3.1 Brixelizer GI vs RT GI (recommendation: RT GI with a DDGI-style probe volume; Brixelizer only as a fallback)

**RT GI, probe volume** (our own implementation of the published dynamic-diffuse-GI technique; no SDK licence needed):
- Probes on a grid per map (or a few cascades around the camera), each with irradiance + visibility maps (octahedral).
- Each frame, every probe casts N rays through the existing TLAS. Hits are shaded with the matc HLSL materials (stage 2's hit shading), the light list + shadow rays, and the previous frame's probes (multi-bounce). Misses return the sky.
- Pixels sample the 8 nearest probes with visibility weighting.
- Cost scales with probes x rays, not with resolution. For example 32 k probes x 128 rays = 4.2 M rays per frame, about 0.4-0.6 ms at the measured 7-15 G rays/s, plus hit shading.
- It reuses everything stages 1-2 built: TLAS, characters, materials, denoiser-free and temporally stable. It runs on any DXR 1.1 GPU (AMD and NVIDIA).
- Weaknesses: low-frequency, light leaking at thin walls (mitigated by the visibility term + probe relocation), probe placement per map.

**Brixelizer GI** (AMD FidelityFX Brixelizer + Brixelizer GI, MIT):
- A sparse distance-field cascade around the camera, built from the triangle meshes, with radiance caching and screen probes.
- Strengths: no ray-tracing hardware needed (compute), stable, also gives rough specular.
- Costs for us: a second scene representation (the SDF bricks: memory + build time per map + updates for movers), its own D3D12 integration and inputs, and characters only as coarse proxies.
- AMD quotes it in the low milliseconds at 1440p; to be measured if needed.

Agreed with Rendering 2026-10-12. Probe-ray hit shading reuses rt_material.hlsli / wfcEvalMaterial (agents/rendering-rt 753689bc, 7ec68a29); the probe-update shader is compiled per map against materials_rt.hlsl, like reflect_mat.cso.

**Choice**: build R4 on the probe volume first, because it shares the TLAS, hit materials and light list. Prototype Brixelizer only if the probes show leaking / cost problems that cannot be fixed, or for a non-RT fallback tier. Rendering decides with the R4 prototype numbers. This replaces M7 plan section 7's "RT GI vs Brixelizer at stage 3".

### 3.2 Light sources for a lightmap-free world

- All enabled authored lights, both Static and Dynamic channels. Static-channel-only lights were the bake's fill lights: dropping them would darken the maps against the original.
- CastShadows / CastStaticShadows decide whether a light gets RT shadow rays. Non-shadowing fills stay unshadowed, as authored.
- The sun = the brightest directional light, as stage 1. Additional directional lights are kept, but only the sun gets the cone.
- Sky: the SkyLight components (upper / lower colour x brightness) as the miss colour for GI / AO rays.
- Emissive surfaces light through GI hits. Strong emissive lamps usually have an authored point light already, so there is no double counting of direct light.
- Light functions, the original's projected light-function materials: phase 2 (kept as a flag).
- Units: the lights' colour x brightness as UE3 used them at run time. The bake's lightmap scale (uLMScale) is the reference for the calibration below.

### 3.3 Calibration (so "realistic" still looks like the same maps, just lit for real)

- Reference: Faithful renders of the same lockstep camera set per map (12 seeded positions, plus the RT sheets' frames).
- Solve per map, three scalars: exposure, sky intensity, light-intensity scale. Least squares on the log-average luminance of the lit regions, with indoor / outdoor masks from the sky visibility.
- Shadows are not matched: they are intentionally different.
- Check the GI energy against R1's split: R4's indirect vs R1's "LM - V_bake * k * D_sun" on the same pixels (mean ratio within 10 %). The bake is the best ground truth we have for bounce energy.
- The user reviews side-by-side sheets per map (Faithful / R1-R3 / R4) and can bias the result warmer / cooler / brighter (the M6 calibration knobs, section 4).

## 4. M6 Modern materials (PBR variants of the original graphs)

**Goal**: the same materials, shaded physically: energy-conserving GGX specular + Lambert / Burley diffuse, image-based lighting, correct Fresnel. It is optional as the "Materials: Original / Modern" row; Original stays the default.

**Mapping, generated per material by matc in a "pbr" mode**:
- The same graph evaluation, so textures, parameters and switches stay the original's. A mapping layer converts the outputs:
  - baseColor = DiffuseColor (plus SpecularColor where the material is classified metal);
  - roughness = sqrt(2 / (SpecularPower + 2)), the Blinn-Phong to GGX equivalence;
  - F0 = 0.04 for dielectrics, else from SpecularColor;
  - metallic from a per-material classification: an audit tool in the style of docs/rendering/vehicle_material_audit, with dark diffuse + strong coloured specular -> metal;
  - Normal and Emissive unchanged.
- Overrides: a reviewed JSON per material family (vehicles, robots, terrain, metal props) for the cases the heuristic gets wrong. The HD texture pack's normals / detail feed in where present.
- Lighting with baked lighting (M6 without RT):
  - the directional lightmap as irradiance;
  - specular from the existing env reflections, pre-filtered by roughness;
  - the dynamic lights with GGX.
  Calibrated so the per-map mean luminance stays within a tolerance of Original.

**How it combines with RT Realistic**:
- RT Realistic needs physically based materials. Direct light from hundreds of lights and GI hits must use one energy-conserving BRDF, and the reflections' roughness gate and denoiser need a real roughness.
- So **RT Realistic implies Modern materials**: the RT row turns Materials to Modern, greys it out, and says why. Modern materials also work alone, on the baked lighting.
- The RT hit shaders get the same PBR mapping through matc's HLSL backend. One mapping table serves both raster and RT, so reflections and GI see the same albedo / roughness as the screen.

**Order and owners (proposal)**:
1. Systems: the material classification audit (all maps: metal / dielectric / emissive / masked, SpecularPower histogram) plus the mapping table, as a docs + JSON deliverable.
2. Rendering: matc "pbr" mode (GLSL + HLSL) and the GGX / IBL shading variant.
3. Calibration sheets, then user review.
4. RT Realistic's R4 uses it.

R1-R3 do not need M6 and can ship first.

**Exit for M6**:
- Per-map mean luminance within 5 % of Original (Modern on baked lighting).
- No material brighter than physically possible: white-furnace check on the BRDF.
- The user's per-family sheets approved.
- The RT hit-shading albedo matches the raster albedo (the stage-2 reflection check).

## 4b. Defaults (user 2026-10-12) and what could become default later

User, verbatim: "Yes the brand new textures should be the default, and by default the Ray traced shadows and lighting should be off unless enabled."

| Piece | Default now | Could become default later (only if the user asks) |
|---|---|---|
| HD textures (M4) | **ON** (the package ships the pack; the profile default and auto-detect turn it on; the originals stay one menu switch away) | - |
| Modern materials (M6) on the baked lighting | off ("Materials: Original") | The best candidate: it needs no RT hardware and runs on every GPU the game supports. It completes the "brand new" look with the HD textures (HD normal / detail + physically based shading). It is only a candidate once calibrated (per-map luminance within 5 %) and approved on the per-family sheets. |
| RT Realistic R1-R2 (RT sun shadows on the world, soft contact-hardening shadows, AO everywhere) | **off** (user) | Possibly "on when the GPU supports DXR 1.1 and has headroom" through auto-detect, but only if the user asks. Never on GPUs without ray tracing. |
| RT reflections (R3) | **off** | Same rule as R1-R2, after a performance review. |
| Lightmap-free GI (R4) | **off** | Unlikely as a default: it is the largest departure from the original lighting and the most expensive. It stays an explicit choice. |
| FSR 3.1 / DLSS, frame generation | off (explicit choices, M5) | - |

A **graphics preset** row could bundle these later: "Original" (the faithful look), "Modern" (HD + Modern materials), "Realistic" (Modern + RT Realistic). The preset only sets the individual rows; each stays switchable. The proposal is Frontend's row; the user decides whether to add it.

## 5. Asks

- **Rendering**:
  - Agree the R1 to R5 order and the GI choice (probe volume first).
  - R1 needs the lightmap UV layout per component, already in the render data, and the lightmap scale per page.
  - Systems can write the offline V_bake / k tool (AssetTools-style, outputs beside the lightmaps) once R1 is agreed.
- **Integration**: R1 and M6's audit can start in parallel with HD, as docs and tools. Code stays on the held RT branch per the user's HD-first order.
