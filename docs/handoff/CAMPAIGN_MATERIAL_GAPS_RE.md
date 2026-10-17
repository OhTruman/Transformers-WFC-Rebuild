# Campaign material gaps: shipped shader decodes for the translator (RE, 2026-10-18)

For Rendering's translator. These are the 38 campaign materials that don't compile today (Rebuild CAMPAIGN_ASSETS_SUMMARY: 23
new-expression-node, 12 DecalMaterial-rooted, 3 ShaderCode ddy / acos).

Method:
- `tools/shadermap_scan.py` finds each material's cooked shader map (per vertex factory);
- `tools/shader_by_guid.py` dumps the Xenos microcode and CTAB.

Labels: C = microcode / CTAB read; H = inference. Raw dumps: work/q/decal_ps.txt.

## Batch A: DecalMaterial roots (12)

### A.0 What a DecalMaterial is (C)
- Each DecalMaterial is cooked with **two VF sets**: FLocalVertexFactory / GPU-skin (the material on ordinary meshes) **and**
  FLocalDecalVertexFactory / FGPUSkinDecalVertexFactory / FGPUSkinMorphDecalVertexFactory (projected onto receivers).
- The pixel-shader body is the ordinary material graph. **Translate it exactly like a Material**; accept DecalMaterial as a
  chain root.
- The decal-specific part is all in the **decal vertex factory**:
  - **UV0** (decal VS 9c56c0b4… / 8930f347…):
    `uv = 0.5 − (DecalWorldToTexCoordMatrix · (WorldPos − DecalWorldLocation)).xy + DecalOffset`.
    - The matrix rows are c5–c8; WorldPos includes PreViewTranslation.
    - H: the 4th row enters through a term derived from the vertex w, which is 0 for normal geometry.
    - So the texture is projected from the decal frame, centred at 0.5.
  - **Tangent frame in the PS:** `DecalWorldTangent` (c20) and `DecalWorldBitangent` (c21), normalised, replace the mesh tangents.
    The normal comes from the receiver. This matters only for graphs that use tangent-space normals (DarkEnergon below).
  - The translucent decal VS also computes the 4-layer height fog (Fog* constants), like other translucency.
- Blend mode comes from the material (Translucent / Modulate / Masked below).

### A.1 PROP_FloorStains_p.DECAL_stains_02 / 03 / 04_MAT (Translucent, Unlit; PS f73099fc / 4fae11fe / f6095cb5, identical code)
- `t = Texture2D_0(uv).r`.
- `a = (1 − t) · UniformScalar_0`; **clip if a < 1/255**.
- `rgb = (VC.rgb + VC.a · UniformVector_0.rgb) · SceneColorBiasFactor`, where VC = the interpolated vertex-colour input of the decal VF.
- out = (rgb, a).

### A.2 PROP_FloorStains_p.DECAL_stains_01_MAT (Translucent, Unlit; PS 133e1daf)
- Like A.1, but the mask uses the green channel scaled: `t = tex.g · (1 + UniformVector_1.x)`.
- `a = (1 − t) · UniformScalar_0`; clip below 1/255.
- rgb as A.1.

### A.3 ENV_KON_arrowDecal_MAT, ENV_KON_HazardSignDecal_MAT, ENV_IAC_LightSources CustomShadowDecal_01_MAT (Modulate; one shared PS 9bfcffb4)
- All three compile to the **same** shader (only textures / params differ):
  `rgb = (tex.rgb + UniformVector_0.rgb) + (1 − UniformVector_0.rgb) · NoLightMapColor`; alpha unchanged.
- Modulate-blended (dest × src): white = no change.
- Lightmapped permutations exist (vertex / texture directional lightmap policies, 9 PS total). Use the NoLightMap form for decals
  projected at runtime.

### A.4 ENV_KON_GrungeDecals_p.DarkEnergon_Spread_MAT + 4 instances (Masked, Unlit; PS ed761550)
- A full graph:
  - 5 textures + a cube map;
  - **tangent-space normal** (two normal fetches blended, one at UV × const), built with the decal tangent / bitangent frame;
  - a view vector from WorldPos − PreViewTranslation → **reflection vector into TextureCube_0** with an explicit LOD (UniformScalar);
  - **Fresnel / specular** via pow (log / exp) with UniformScalar exponents;
  - a luminance desaturate (0.3, 0.59, 0.11) lerp.
  - **Opacity mask:** clip if `mask − OpacityMaskClipValue < 0`.
  - Output × SceneColorBiasFactor.
- The node graph already translates (it's the same nodes as other env materials). Only the root type and the decal tangent frame
  are new.
- Instances: DarkEnergon_Spreading / 01 / 02 / 03_MAT_INST (parameter overrides only).

### A.5 Two of the 12 aren't DecalMaterial in the cache (C)
- `ENV_IAC_Ground_p.Decal_Crater_Orange_MAT` and `ENV_KON_PrisonBarge_p.DomeDeco_01_Warm_Alt_Decal` are labelled **Material** in the
  shader cache. They have full lit permutations (spot / directional shadow-mask light shaders, depth, velocity) plus the decal VF
  set.
- So they are ordinary lit Materials with bUsedWithDecals, and they need no special root. If AssetTools reports them as "no
  master", that's a parent-chain lookup issue, not a decal root. Check their chain in authored.db.

## Batch B: new nodes If / Ddx / CameraWorldPosition and ShaderCode ddy / acos (10 materials)

Raw dumps: work/q/batchB_ps.txt, work/q/omega_skin.txt.

### B.1 MaterialExpressionIf: FX_Materials_p Splash_Big_Clip_MAT / _02_MAT (+ 2 instances; particle VFs, PS 26b4ae39 / e4686c56)
- Graph (authored.db): If(A = Texture.alpha (output 4), B = DynamicParameter, A>B → Constant 1, A==B → 0, A<B → 0).
- **Compiled form (C):** `ge = A >= B; gt = A > B; result = ge ? (gt ? AGreaterThanB : AEqualsB) : ALessThanB`. It's the plain
  comparisons (SETGT / SETGTE + CNDGTE), with **no equality epsilon**.
- Here that reduces to `step: A > B`. It feeds an alpha clip (KILL), giving a DynamicParameter-driven dissolve of the splash.
- **Translator:** implement If exactly as above. Equality is exact float equality.

### B.2 MaterialExpressionDdx: TR_OmegaSupreme_Mat (+ 3 instances)
- Graph: Ddx(ComponentMask(…)) → DotProduct(ddx, ddx), inside a StaticSwitchParameter branch.
- **Neither shipped PS contains a gradient instruction** (FLocalVF 0391bbfe, GPU-skin 934fd3e8; C). The cooked static-switch value
  takes the other branch, so the Ddx path is dead in the shipped game.
- **Translator:** support Ddx as dFdx / ddx for completeness. Honouring the static switch removes it, with no visual effect.

### B.3 ShaderCode `ddy(%A)`: ENV_AllShader_p.PostProcess Hologram_Post_MAT (+ its MATINST; PS 0ed7b3f5)
- Code nodes (authored.db): `ddy(%A)` on a texture sample's R → Sine; and `saturate(pow(sin(%A),1500)) + saturate(pow(sin(%A*sin(%A)),1000))`
  on Time (the flicker spikes).
- **Compiled (C):** the ddy becomes Xenos `getTextureGradients` (the hardware screen-space derivative) on that coordinate.
- **Translator:** map `ddy` → the screen-space y-derivative (GLSL dFdy). **Negate it if the target framebuffer's y axis is flipped
  relative to D3D** (D3D / Xenos y grows downward).
- The pow(…, 1500) spikes need full float precision (keep highp).

### B.4 ShaderCode with acos: ENV_AllShader_p.Special DarkEnergonCrystal_FloorCrack_Emiss_MAT (PS c62476d9; also cooked for the decal VF)
- Its literal pool holds **−0.0187293, 0.074261, −0.212114, 1.57073**. That's the standard fast acos:
  `acos(x) ≈ sqrt(1 − |x|)·(1.5707288 + |x|(−0.2121144 + |x|(0.0742610 − 0.0187293|x|)))`, mirrored for x < 0 (π − …). C: the
  constants and the 3.14159 present.
- Other Code nodes in this material: `1 − floor(saturate(%A.y + .01))` and `saturate(1 − pow(1 − saturate((A.xy + B.xy·0.3).y), 12))`.
- **Translator:** native acos is fine (error ≤ 7e-5 vs the approximation).

### B.5 MaterialExpressionCameraWorldPosition: ENV_ORB_CybertronPlanet_p.MATTE CybertronPenumber_MAT (PS e1d20335; orbit matte)
- 3 CameraWorldPosition nodes. The PS has **no camera-position constant in its CTAB**: only PreViewTranslation, and it's used once
  (WorldPos − PreViewTranslation).
- So the compiler expressed the camera in translated world space. There the camera is at the origin; in absolute world space
  **CameraWorldPosition = −PreViewTranslation**. The camera-relative terms fold into the interpolated translated position (H for
  the exact folding; C for the CTAB).
- **Translator:** CameraWorldPosition = the camera's world position (UU), as a uniform.

## Batch C: lens-flare node materials (14 + 1)

Nodes used (authored.db):

| Material | Nodes |
|---|---|
| ENV_ORB_Effects_p ORB_LensFlare_BASE | Intensity, Occlusion, RadialDistance |
| ORB_LensFlare_REF01 / REF02 / RING (MP; Rendering compiles these) | + RayDistance |
| ENV_NEU_LensflareTest_p flare_sun / flare_line / flare_blurring / flare_smalldot / _02 / flare_starburst2 | Intensity, Occlusion, RadialDistance |
| A1_LensFlare_RING_MAT | Intensity, Occlusion, RadialDistance, RayDistance |
| flare_radialBlur / _A1 / _Zeta | Occlusion only |
| ENV_NEU_CybertronLow_P LensFlare_MAT | Intensity, Occlusion, RadialDistance |

Shipped PS (FLensFlareVertexFactory; dumps work/q/lf_ps.txt), with interpolators as the PS reads them (C):

| Material | PS |
|---|---|
| flare_sun_Mat | `tex.rgb · UniformScalar_0 · Intensity · Occlusion · (1 − sat(RadialDistance)) · (0.4, 0.7, 1)`, then × vertex rgb, × vertex a + UniformVector_0, × SceneColorBias |
| A1_LensFlare_RING_MAT | luminance(tex) (0.3, 0.59, 0.11) · UniformScalar_0 · Intensity · Occlusion · (1 − sat(RadialDistance)), × vertex rgb, × vertex a + UniformVector_0. RayDistance is folded out (unused in the shipped code). |
| flare_radialBlur_Mat (Occlusion only, no texture) | radial falloff `pow(max(0, 1 − |uv − 0.5|² · k), 35)` · UniformScalar_0 · lerp(UniformVector_1, UniformVector_2, Occlusion), × vertex rgb, × Occlusion + UniformVector_0 |

**Node → per-vertex value** (native fill, LENS_FLARE_RE §4a–c):
- **Intensity** = proxy+0xE4: 1.0 for every flare without an authored cone / radius, which is all the campaign and MP templates.
- **Occlusion** = ScreenPercentageMap(coverage).
- **RadialDistance** = the element's normalised radial distance r (max(|x|, |y|) in NDC).
- **RayDistance** = the element's RayDistance.
- **SourceDistance** = 0.5·|S − E|.

In the PS these arrive as TEXCOORD3.x / .y (Occlusion / Intensity) and the TEXCOORD2 lanes. **The shader reads RadialDistance from
lane .y of the TEXCOORD2 interpolator.** So check your vertex packing against the node names, not my §4a byte table, which may be
one lane off for TEXCOORD1 / 2 (H).

**Translator:** add the 5 nodes as varyings fed from the flare vertex (the MP path already does this). Then all 15 compile, and
look correct given the §4c occlusion and culling rules.
