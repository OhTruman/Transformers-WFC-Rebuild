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

- V = normalize(tangent-space camera vector): r18, from WorldCameraPosition - (pos + PreViewTranslation), through the TBN.
- Half vectors, ALU 38-51: `H_i = normalize(V + B_i)`. The squared lengths `|V+B_i|^2` are built explicitly from the literals:
  r4.x for B2, r4.y for B0, r4.z for B1, then RECIPSQ.
- Per basis, ALU 79-92: `s_i = pow(max(sat(dot(N, H_i)), 0.0001), P)`, as LOG / MUL / EXP. N is the material's normal-map normal.
  P = UniformScalar_12 (c21) x a texture channel: the material's SpecularPower expression.
- Lightmap colour per basis: `LM_i * LightMapScale[i]` (c22-24 x the three LightMapTextures fetches). The same values feed the diffuse.
- Output, ALU 93-105:
  `oC0 = ( Emissive + Diffuse * sum_i dot(N,B_i)^2 * LM_i*Scale_i + SpecularColor * sum_i s_i * LM_i*Scale_i ) * SceneColorBiasFactor`.
  The specular weight is `s_i` alone; it does **not** carry the `dot(N,B_i)^2` diffuse weight. SpecularColor = UniformScalar_11
  (c20) x texture; it is material-specific.

So the original's lightmap specular is **Blinn-Phong per lightmap basis direction**: each basis vector acts as a light direction
with the colour of its coefficient, through a half vector with the view and the material's SpecularPower. It is not a
dominant-direction approximation and not a reflection-vector (Phong) form.

## Checks for Rendering's change (step 2, once it is committed)

1. The basis is the same as LMB0-2 (component order as above).
2. `H_i = normalize(V + B_i)` in tangent space, with V toward the camera.
3. `max(sat(dot(N,H_i)), 1e-4)` before pow; exponent = the material's SpecularPower.
4. Weight = `LM_i * Scale_i`; no `dot(N,B_i)^2`.
5. Multiplied by the material's SpecularColor; added before the SceneColorBiasFactor multiply.
6. Materials with no specular input compile with no term (blob 0x29057).
7. The vertex-lightmap variant (LMT_1D) should get the same term; not yet decoded here.

Step 3 (untimed Molten render A/B) waits for the commit.
