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
- Cost: the stage-1 sun ray already exists for dynamic pixels; extending it to world pixels costs about +0.3 ms at 1440p. The R8 V_bake pages add about 1/12 of the three RGBA8 coefficient pages (one byte vs 3 x 4 per texel).
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

**Goal**: the same materials, shaded physically: energy-conserving GGX specular + Lambert / Burley diffuse, image-based lighting, correct Fresnel. It is part of the HD pack and follows its switch (user: "materials should be hd textures too"). HD off = the original materials and shading; that is the default.

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
- So **RT Realistic implies the HD pack's Modern materials**: switching RT Realistic on also requires HD Textures on (the RT row turns it on, greys it out while RT is on, and says why). Modern materials also work alone, as part of HD on the baked lighting.
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

User, verbatim, in order:
- "Yes the brand new textures should be the default, and by default the Ray traced shadows and lighting should be off unless enabled."
- Then, the same day: "HD texture's should be off my default too to be honest."
- And: "materials should be hd textures too". So the Modern-material maps ship **inside the HD pack** and follow the HD Textures switch: HD on = new textures + Modern materials. There is no separate Materials row; the M6 shading turns on with HD.

**Everything new is opt-in; the original look is the default.**

| Piece | Default now | Could become default later (only if the user asks) |
|---|---|---|
| HD pack: textures (M4) + Modern materials (M6: PBR mapping, roughness / ORM maps) | **off** (the package ships it; Settings > Graphics > HD Textures: On) | The best candidate if the user changes their mind: it needs no RT hardware and runs on every GPU the game supports. Only once calibrated (per-map luminance within 5 % of Original) and approved on the per-family sheets. |
| RT Realistic R1-R2 (RT sun shadows on the world, soft contact-hardening shadows, AO everywhere) | **off** (user) | Possibly "on when the GPU supports DXR 1.1 and has headroom" through auto-detect, but only if the user asks. Never on GPUs without ray tracing. |
| RT reflections (R3) | **off** | Same rule as R1-R2, after a performance review. |
| Lightmap-free GI (R4) | **off** | Unlikely as a default: it is the largest departure from the original lighting and the most expensive. It stays an explicit choice. |
| FSR 3.1 / DLSS, frame generation | off (explicit choices, M5) | - |

A **graphics preset** row could bundle these later: "Original" (the faithful look), "Modern" (HD + Modern materials), "Realistic" (Modern + RT Realistic). The preset only sets the individual rows; each stays switchable. The proposal is Frontend's row; the user decides whether to add it.

## 4c. Review gates (Experimental, 2026-10-12)

1. **Metal classifier**: materials that consume uRT_Cust_Color_* / EnergonColor (team / customisation tints) stay dielectric unless a reviewed per-family row says otherwise. Exit check: tinted regions keep their mean hue within 3 degrees of Original under Modern.
2. **Generated maps (HD pack)**:
   - each roughness map's mean is normalised to the derived sqrt(2 / (SpecularPower + 2)) of its material;
   - an ORM's metal channel must agree with the per-material class (else the map is rejected for that material);
   - per texture family: eye sheets and the luminance exit.
3. **VRAM**: the per-map table (work/m6/vram_table.py, 2026-10-12). It covers the map materials' 2D textures with full mip chains: the originals, the HD pack's rows (bytes as packed), and BC4 roughness for the work list's parents at the HD size.

   | map | original MB | HD (Faithful) MB | + roughness MB | Enhanced MB |
   |---|---|---|---|---|
   | BrokenHope | 623 | 914 | 222 | 1136 |
   | Remnant | 630 | 927 | 222 | 1149 |
   | Berth | 575 | 805 | 214 | 1019 |
   | Rust | 646 | 963 | 238 | 1201 |
   | Seed | 614 | 908 | 229 | 1137 |
   | Streets | 597 | 850 | 220 | 1070 |
   | Molten | 609 | 897 | 222 | 1119 |
   | Debris | 632 | 855 | 220 | 1075 |
   | Complex | 576 | 845 | 208 | 1053 |
   | Gorge | 576 | 832 | 214 | 1046 |

   - Worst case: Enhanced adds +554 MB over the originals (Rust).
   - Rendering measured the whole process's device-local use at 2.9-3.3 GB with RT on at 1440p, so Enhanced + RT is about 3.9 GB.
   - Half-resolution roughness would cut the roughness column by 3/4.
   - Floors given to Frontend: 6000 MB for Enhanced, 8000 MB for Enhanced + RT at 4K output. They only drive a row warning; nothing is blocked.
4. **Calibration inputs**:
   - every calibration file (M6 luminance, R1 split check, R4 GI fit) records the texture-pack version it was made with;
   - loading it with another pack version fails loudly (no silent stale calibration);
   - M6 / R4 calibration waits for the pack's E decision.
5. **R1 with HD normals**: k and p are fitted with the normal set the player sees (the HD pack boosts relief x1.6 where present). The < 1/255 exit is checked with HD on and off (a fit per pack state if they differ).
6. Memory note corrected above: V_bake R8 is about 1/12 of the coefficient pages.

## 5. Asks

- **Rendering**:
  - Agree the R1 to R5 order and the GI choice (probe volume first).
  - R1 needs the lightmap UV layout per component, already in the render data, and the lightmap scale per page.
  - Systems can write the offline V_bake / k tool (AssetTools-style, outputs beside the lightmaps) once R1 is agreed.
- **Integration**: R1 and M6's audit can start in parallel with HD, as docs and tools. Code stays on the held RT branch per the user's HD-first order.

**R1 V_bake, first bake (2026-10-13, tools/systems/vbake/run_all.sh, 8 threads; output work/vbake/<map>/).** All 10 MP maps took about 2 minutes in total, 2-8 s of tracing and 4-10 s of fitting per map.

| map | texels | lit | k | p | residual | trace lit / bake dark | trace shadowed / bake lit | clamped indirect |
|---|---|---|---|---|---|---|---|---|
| UND_Gorge | 524,908 | 1.8 % | 1.023 | 1.0 | 0.16 | 5.2 % | 0.3 % | 19.1 % |
| ESC_BrokenHope | 1,366,179 | 2.5 % | 0.819 | 1.0 | 0.44 | 7.4 % | 0.8 % | 24.7 % |
| ESC_Remnant | 2,303,235 | 1.9 % | 0.792 | 1.0 | 0.65 | 7.9 % | 0.2 % | 30.5 % |
| IAC_Berth | 811,234 | 4.1 % | 0.894 | 1.0 | 0.26 | 6.5 % | 0.4 % | 28.2 % |
| IAC_Rust | 1,367,380 | 4.4 % | 0.874 | 1.0 | 0.25 | 6.7 % | 1.1 % | 23.3 % |
| IAC_Seed | 1,307,470 | 5.0 % | 0.853 | 1.0 | 0.31 | 11.0 % | 0.5 % | 21.2 % |
| IAC_Streets | 1,159,744 | 3.7 % | 0.734 | 1.0 | 0.63 | 3.5 % | 5.6 % | 11.2 % |
| KON_Molten | 823,523 | 3.2 % | 0.895 | 1.0 | 0.16 | 3.2 % | 0.1 % | 12.4 % |
| ORB_Debris | 1,263,732 | 3.2 % | 0.842 | 1.5 | 0.69 | 4.3 % | 0.5 % | 21.1 % |
| UND_Complex | 994,279 | 1.2 % | 0.829 | 1.0 | 0.49 | 4.5 % | 0.1 % | 26.1 % |

Reading:
- The traced sun patches line up with the lightmaps' sunlit areas (previews checked on Gorge and Streets).
- Most lightmap energy on these maps is local lights, so few texels are sunlit.
- The fit is good on Gorge, Molten, Rust and Berth (residual 0.16-0.26), and weak on Remnant, Streets, Debris and Complex (0.49-0.69).
- The clamp removes 11-30 % of the sun energy on every map. That is above the 10 % sanity line: k is likely high where the lightmap's penumbra is softer than the traced cone, or where local lights sit on the lit side of a pair.
- Vertex-lightmapped (LMT_1D) components have no V_bake yet; they keep the original lightmap.
- Next: Rendering's full-frame pairs decide whether k needs a per-map scale-down or a softer cone.

**R1 V_bake refit (2026-10-13, Rendering: Realistic read "a touch strong" on Rust).**

`vbake_fit.py --k-quantile 0.3`: k is the 30 % quantile of the per-pair estimates (lit minus shadowed over the unit-k model share). p and the handedness stay from the least-squares fit; fit.json keeps it as k_fit, and k is what the runtime reads.

| map | k (q30) | k_fit | share of the fit | clamp after |
|---|---|---|---|---|
| UND_Gorge | 0.844 | 1.023 | 83 % | 16.2 % |
| ESC_BrokenHope | 0.468 | 0.819 | 57 % | 19.8 % |
| ESC_Remnant | 0.208 | 0.792 | 26 % | 16.9 % |
| IAC_Berth | 0.687 | 0.894 | 77 % | 23.8 % |
| IAC_Rust | 0.662 | 0.874 | 76 % | 19.9 % |
| IAC_Seed | 0.541 | 0.853 | 63 % | 17.0 % |
| IAC_Streets | 0.354 | 0.734 | 48 % | 7.6 % |
| KON_Molten | 0.746 | 0.895 | 83 % | 11.0 % |
| ORB_Debris | 0.466 | 0.842 | 55 % | 15.1 % |
| UND_Complex | 0.427 | 0.829 | 52 % | 18.1 % |

Reading:
- **The clamp is not a k problem.** `--max-clamp 0.10` (bisect k until the >= 0 clamp removes at most 10 % of the sun energy) needed k at 1-27 % of the fit on 8 of 10 maps, which effectively disables the swap. The clamp comes from the 4-11 % of texels the trace calls lit but the bake has dark (UV / occluder mismatch), not from k.
- **The cone is not the lever either.** A 0.5° retrace of Rust (work/vbake/MP_IAC_Rust_cone05) gives 4.4 % lit (the same as 0.27°) and k(q30) 0.665 vs 0.662.
