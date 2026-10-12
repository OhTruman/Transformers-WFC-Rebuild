# Lightmap specular: independent decode of the original (step 1 of the Molten check, 2026-10-12)

Decoded independently, without reading Rendering's notes. Sources are read only: RE-Workspace `work/q/eng_ShaderCache_*.bin` (the
extracted ShaderCache exports) and RE-Workspace `tools/xenosdis.py` / `xenostrace.py`. Output is in this folder (`ps_*.txt`,
`trace_60467.txt`, `lmps_scan.py`, `trace.py`).

## Which shaders

`lmps_scan.py` lists every pixel blob (magic 0x102A1100) whose constant table has `LightMapScale` + `LightMapTextures` (the
texture-lightmap policy). Two kinds:

- **No specular input** (e.g. 10303 @0x29057: LightMapScale c10-12, no WorldCameraPosition): `L = sum_i LM_i * c255.y * Scale_i`
  (flat normal), `oC0 = (Emissive + Diffuse * L) * SceneColorBiasFactor`. No specular term. This matches today's `kFSMainLM`.
- **Material with specular** (e.g. 10303 @0x60467: LightMapScale c22-24, WorldCameraPosition c3, normal map, TextureCube_0):
  **has a lightmap specular term.** Details below.

## Decoded term (10303 @0x60467)

Literals: c251 = (0.08, 0.0001, 0, 0), c252 = (1, 1.5, 2, -1), c253 = (0.01, 0.05, 0.5, 1), c254 = (0.57735, 0.70711, -0.40825, 0),
c255 = (0.8165, 0.57735, -0.70711, -0.40825). These are the UE3 directional lightmap basis B0 = (0, 0.8165, 0.57735),
B1 = (-0.70711, -0.40825, 0.57735), B2 = (0.70711, -0.40825, 0.57735), stored swizzled. It is the same basis as `kFSMainLM`'s LMB0-2.

**CORRECTED 2026-10-12 (retraction of the first version, 968995a):** the first version read r18 as the view vector and gave
`dot(N, normalize(V + B_i))`. That is wrong. r18 is the normal-map normal. The correct term is Rendering's form:

- N, ALU 16/19/27-28/36: normal map `(tex.xy * 2 - 1) * UniformScalar_2 (c11)`, `z = tex.z * 2 - 1`, normalized -> r18.
- Tangent-space -V, ALU 17/32/33/40/49: r6 = normalize(WorldCameraPosition - worldPos), dotted with the vertex normal, tangent and
  bitangent -> r3 = (-V).
- Reflection, ALU 50-53: r17.w = DOT4(r18, r3) with r18.w = dot(N,-V) and r3.w = 1, so it is `2 dot(N,-V)`;
  `R = -V - 2 N dot(N,-V) = reflect(-V, N)` -> r10. The same R drives the material's TextureCube_0 (CUBE, ALU 54).
- Per-basis directions, ALU 38-50: `r16 / r0 = N + B_i`; `|N + B_i|^2` is built per basis (B2 r4.x, B0 r4.y, B1 r4.z) -> RECIPSQ
  -> `normalize(N + B_i)`.
- Per basis, ALU 77-92: `s_i = pow(max(sat(dot(normalize(N + B_i), R)), 0.0001), P)`, P = UniformScalar_12 (c21) x a texture
  channel: the material's SpecularPower expression.
- Lightmap colour per basis: `LM_i * LightMapScale[i]` (c22-24 x the three LightMapTextures fetches). The same values feed the diffuse.
- Output, ALU 93-105:
  `oC0 = ( Emissive + Diffuse * sum_i dot(N,B_i)^2 * LM_i*Scale_i + SpecularColor * sum_i s_i * LM_i*Scale_i ) * SceneColorBiasFactor`.
  The specular weight is `s_i` alone; it does **not** carry the `dot(N,B_i)^2` diffuse weight.

So the original's lightmap specular is **Phong (reflection vector) against the bent basis directions normalize(N + B_i)**, each with
the colour of its lightmap coefficient. It is the same as Rendering's `dot(normalize(N+B_i), reflect(V,N))`, with V toward the eye
(reflect(-V,N) in this notation).

## Checks for Rendering's change (step 2, once it is committed)

1. The basis is the same as LMB0-2 (component order as above).
2. Direction `normalize(N + B_i)` (normal-map N, tangent space) against `R = reflect(-V, N)`.
3. `max(sat(dot(normalize(N+B_i), R)), 1e-4)` before pow; exponent = the material's SpecularPower.
4. Weight = `LM_i * Scale_i`; no `dot(N,B_i)^2`.
5. Multiplied by the material's SpecularColor; added before the SceneColorBiasFactor multiply.
6. Materials with no specular input compile with no term (blob 0x29057).
7. The vertex-lightmap variant (LMT_1D) should get the same term; not yet decoded here.

Step 3 (untimed Molten render A/B) waits for the commit.

## Step 2: Rendering's GLSL against the checklist (ac9d77c + frame fix d8a49fb, integration 2ae4c67): 7 / 7 match

Read from `git show d8a49fb:src/render/gl/WfcPipeline.cpp` (code read only; done during Rendering's timed window, no build or run).
The term is spliced into `kFSMainLM` right after `c += o.DiffuseColor * L;`:

    vec3 R = -m.cameraVector + n * (dot(n, m.cameraVector) * 2.0);
    Ls = sum_i pow(max(clamp(dot(normalize(n + LMB_i), R), 0, 1), 0.0001), o.SpecularPower) * l_i * uLMScale[i];
    c += o.SpecularColor * Ls;

1. Basis: the same LMB0-2 constants the diffuse uses. MATCH.
2. `n = normalize(o.Normal)` (material normal, tangent space); `m.cameraVector` = normalize(camPos - pos) in the T, B, N frame
   (toward the eye), so `R = -V + 2 n (n.V) = reflect(-V, n)`; directions `normalize(n + LMB_i)`. MATCH (d8a49fb removed ac9d77c's
   binormal flip; the frame is the one `wfcBuildInput` gives every material).
3. `max(sat(.), 1e-4)` before pow, exponent `o.SpecularPower`. MATCH.
4. Weight `l_i * uLMScale[i]`, no `w_i^2`. MATCH.
5. `* o.SpecularColor`, added to `c` with the diffuse before translucent-out / fog. SceneColorBiasFactor is a common linear scale on
   both, so the order is equivalent. MATCH.
6. Gated on lit, blend <= 1, translated original material, and the generated code not containing `o.SpecularColor = vec3(0.0);`.
   A material with no specular input gets the old diffuse-only text. MATCH (and a zero SpecularColor in any other form gives zero).
7. Vertex lightmaps: `l_i` reads `vVLM0-2` when `uVertexLM`, so the same term applies. MATCH by construction; I did not decode the
   LMT_1D microcode myself (Rendering's emulator check lists the Vertex policy as exact).

Notes, not defects:
- Lit translucent lightmapped materials (blend > 1) get no term. Whether the original translucent base pass has it is not checked
  here (my decode covers opaque @0x60467 only).
- WFC_NOLMSPEC=1 restores the previous text exactly (Rendering: 8/8 lockstep identical).

Step 3 (untimed Molten same-camera A/B, WFC_NOLMSPEC=1 vs default) runs after Rendering's current timed window.
