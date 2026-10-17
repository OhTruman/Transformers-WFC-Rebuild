# Lens flares as the shipped game draws them (RE, 2026-10-18)

Labels: CONFIRMED (C) = cooked data / microcode / native code; HIGH (H) = stock UE3 lens-flare behaviour, not traced in WFC.
Data export: `notes/data/lens_flares.json` (`tools/lensflare_export.py`): all 10 LensFlare templates with CDO defaults merged per
element, plus all 32 placed LensFlareComponents with their LensFlareSource transforms.

## 1. Where they are (C)

- **32 LensFlareComponents, 6 of them in MP**, one per map. Every MP one is a LensFlareSource placed as a sun, far away and high up;
  rotation is unset (no cone).

| MP map (package) | Source actor | Template | Location (UE) | DrawScale |
|---|---|---|---|---|
| BrokenHope (ART) | LensFlareSource_6521 | ORB_LensFlare_FINAL03 | (41567, −17799, 31266) | 0.5 × 0.948 |
| Remnant (ART, group Sky) | LensFlareSource_13725 | ORB_LensFlare_FINAL01 | (−147152, −206608, 69968) | 1 |
| Rust (Art) | LensFlareSource_11899 | ORB_LensFlare_FINAL01 | (33722, −13862, 74641) | 1 |
| Seed (BASE) | LensFlareSource_11899 | ORB_LensFlare_FINAL01 | (54538, −6066, 98465) | 1 |
| Debris (MP_ORB_Art_m) | LensFlareSource_4136 | ORB_LensFlare_FINAL02_SmallBound | (−311096, −308768, 20576) | 1 |
| Gorge (BASE) | LensFlareSource_10149 | ORB_LensFlare_FINAL01 | (−18426, 16340, 57900) | 1 |

- Elsewhere: UI_FrontEnd (3: IAC_Zeta_LF ×2, "Test" in SDPG_PostProcess) and campaign (23).
- **Component (C):**
  - `DepthPriorityGroup = SDPG_Foreground` on every MP component;
  - `bAllowApproximateOcclusion = False` (CDO), `SourceColor` (1, 1, 1, 1), `bAutoActivate`;
  - tick group TG_PostAsyncWork;
  - LightingChannels {Dynamic}.

## 2. Templates (C, data): the ORB_LensFlare_FINAL family

All three MP templates (FINAL01 / 02_SmallBound / 03) share the structure. Values are in the json; the main points:

**Template-level:**
- `SourceDPG = SDPG_World`, `ReflectionsDPG = SDPG_Foreground`, `ConeFudgeFactor = 0.5` (CDO).
- No OuterCone / InnerCone / Radius authored: CDO 0, i.e. no cone or distance fade (H: 0 disables).
- `bUseFixedRelativeBoundingBox`, with `FixedRelativeBoundingBox` half-extent **60000** (FINAL01), 50000 (02_SmallBound), 5500 (FINAL03).
  This is the box the occlusion query and culling use around the source.
- `ScreenPercentageMap`: 0 → 1 over a coverage of 0 … 0.025 (FINAL03: 0 → 0.6), reading the table format of §2 (H).

**Source element** (`bIsEnabled = False` in all of them): **the source itself is not drawn**. The visible sun disc is the
"Base_SUN" reflection at RayDistance 0.

**Reflections:**

| Element | RayDistance | Size | Material | Notes |
|---|---|---|---|---|
| Base_SUN | 0 | 8 | ORB_LensFlare_REF01 | Color (2.45, 0.5, 0.5) × α 0.1, **bModulateColorBySource**, occlusion → scale linear 0..1, DistMap_Color curve |
| Reflection_01 | 0.01 | 1.3 (02: 0.8) | ORB_LensFlare_BASE | AxisScaling (1, 1, 0), DistMap_Alpha curve |
| 01 | 0.25 | 1.7 | ORB_LensFlare_REF02 | α 0.5 (02: 0.3); occlusion scaling linear (02: 0.3 → 1) |
| 02 | 0.55 | 1.4 | ORB_LensFlare_REF02 | α 0.5; occlusion curve 21 samples; DistMap_Scale curve; DistMap_Color (2, 0.5, 0.5) |
| Ring | 0.9 | 1.0 | ORB_LensFlare_RING | α 0.1; occlusion curve |
| Thin line (FINAL02 only) | 0 | 0.2 | ORB_LensFlare_REF01 | AxisScaling (20, 0.3): a streak ~67× wider than tall |

- `bNormalizeRadialDistance` is on everywhere; `bUseSourceDistance` is off on every drawn element.
- **Curves (C format, H semantics):** each is a cooked lookup table `[MinOut, MaxOut, InBias, InScale, samples…]` (chunk 1 or 3), with
  sample i at input `InBias + i / InScale`. Decode it as for particle distributions.
  - `Scaling / Color / Alpha / AxisScaling / Rotation / Offset`: input = the element's radial distance.
  - `OcclusionScaling`: input = the occlusion visibility.
  - `DistMap_*`: input = the source-ratio / distance term.

## 3. Rendering path (C)

- **Vertex factory:** `FLensFlareVertexFactory`. The flare materials (`bUsedWithLensFlare`) cook it: 4 base-pass VS density
  variants + the base PS, plus FLocalVertexFactory. Its ShouldCache (0x82FB6F88) = Material vfunc 0x50 || vfunc 0x34 (H:
  IsUsedWithLensFlare || IsSpecialEngineMaterial).
- **Vertex declaration** (built once at 0x82FBF7B0, shared global 0x83861580). **One stream, stride 80 bytes**, CPU-filled each frame:

| Offset | Type | Usage | Meaning (VS use, C; names H) |
|---|---|---|---|
| 0 | float4 | POSITION | element centre, local space → `LocalToWorld` (c7–c10) |
| 16 | float4 | TANGENT | Size: half-extent = `.x·.z`, `.y·.w` (size × scale) |
| 32 | float1 | BLENDWEIGHT | Rotation (radians; wrapped to ±π, sin / cos rotate CameraRight / CameraUp) |
| 36 | float2 | TEXCOORD0 | corner UV (0..1): the billboard corner is `(uv − 0.5)`, and the UV goes to the PS |
| 44 | float4 | TEXCOORD1 | element colour RGBA → PS interpolator |
| 60 | float3 | TEXCOORD2 | (RadialDistance, SourceRatio, Intensity) → PS interpolator (w = 1) |
| 72 | float2 | TEXCOORD3 | (Occlusion, Intensity value) → PS interpolator, with the UV |

- **VS (C, flare_sun_Mat dabef980…):** a camera-facing quad.
  - `world = LocalToWorld·pos + rot(CameraRight, CameraUp) · (uv − 0.5) · size·scale` (signs per CameraRight / Up).
  - It **applies the 4-layer height fog** to the flare (FogInScattering etc., as for world translucency).
  - The material's flare nodes read the TEXCOORD1–3 values in the PS. Example, flare_sun_Mat:
    `tex.rgb · UniformScalar · (interp) · (1 − sat(interp))`, × vertex colour.
- **DPG (C data, H effect):** the reflections, including Base_SUN, go to `SDPG_Foreground`: drawn after the world, **no depth test
  against world geometry**. The (disabled) source would be in SDPG_World. Occlusion therefore comes only from the query, not the
  depth buffer.

## 4. Per-frame CPU logic (H: stock UE3 FLensFlareSceneProxy, not traced in WFC)

1. **Screen position:** project the source; `RadialDistance` = distance from the screen centre, normalised (0 at the centre, 1 at the
   corner) when bNormalizeRadialDistance. Each element sits at `source + RayDistance · (centre − source)` in screen space: 0 = on
   the source, 1 = the screen centre, >1 = beyond it.
2. **Occlusion:**
   - a hardware occlusion query draws the **FixedRelativeBoundingBox** around the source in the World DPG.
   - visible pixels / the box's projected pixel area = coverage; `ScreenPercentageMap(coverage)` = visibility (0..1). Stock uses
     the previous frame's query result (one frame of latency). There is no extra temporal smoothing beyond that and the curves.
   - `bAllowApproximateOcclusion` is False, so the precise query is used.
3. **Per element:**
   - size = Size × Scaling(radial) × OcclusionScaling(visibility) × DistMap_Scale × AxisScaling;
   - colour = Color(radial) × DistMap_Color × SourceColor (if bModulateColorBySource);
   - alpha = Alpha(radial) × DistMap_Alpha;
   - rotation = Rotation(radial);
   - these are written into the 80-byte vertices with Intensity / Occlusion / RadialDistance.
4. **Fades:** with Radius / OuterCone 0 there is no distance or cone fade, so the flare shows whenever the source is on screen and the
   query sees part of its box.
5. **Screen-edge:** elements beyond the screen are not culled individually (H).

## 5. For Rendering

- Rebuild with: one camera-facing quad per enabled reflection; positions on the source → centre line by RayDistance; sizes /
  colours / alpha from the curves; visibility from an occlusion query on the fixed box (or a depth-buffer sample count) mapped by
  ScreenPercentageMap.
- Draw additive (all flare materials are BLEND_Additive) after the world, **without depth test**, with height fog applied, using
  the material graphs (the LensFlare* nodes = the per-vertex values above).
- MP needs only the ORB_LensFlare_FINAL01 / 02_SmallBound / 03 templates (6 maps).
- **Open (UNKNOWN):** the WFC proxy's exact occlusion latency / smoothing and radial normalisation were not traced natively. The
  vertex layout, shaders, DPGs and data are confirmed.

## 4a. Native per-element fill (C, traced 2026-10-18; replaces the §4 H steps 1 and 3)

Functions:
- `0x82FC6620`: the proxy draw; per element, one 4-vertex quad of 80-byte vertices → `0x82F05628`.
- `0x82FC2AA0`: element evaluation.
- `0x82DCDBC8`: project with View+0x210 (ViewProjection; returns x/w, y/w, z/w, w).
- View+0x2D0: the inverse ViewProjection. View+0x310: the view origin.

Element struct offsets follow the declared order:
- +4 RayDistance;
- +8 bits: 0x80000000 bIsEnabled, 0x40000000 bUseSourceDistance, 0x20000000 bNormalizeRadialDistance, 0x10000000
  bModulateColorBySource;
- +0xC Size;
- +0x18 LFMaterials;
- distributions at 0x14-byte steps: +0x24 LFMaterialIndex, +0x38 Scaling, +0x4C OcclusionScaling, +0x60 AxisScaling, +0x74
  Rotation, +0x88 Color, +0x9C Alpha, +0xB0 Offset, +0xC4 DistMap_Scale, +0xD8 DistMap_Color, +0xEC DistMap_Alpha.

Per frame:
1. `S` = the source's NDC (x/w, y/w) from the component origin (LocalToWorld translation). `D_src` = |ViewOrigin − source| in
   world units (UU).
2. `Occ = ScreenPercentageMap(proxy+0xE8)`, where +0xE8 is the visible-coverage input (see 6).
3. For each enabled element:
   - **Screen position:** `E = S·(1 − RD) − S·RD = S·(1 − 2·RD)`. So RD 0 = on the source, 0.5 = the screen centre, 1 = the point
     mirrored through the centre.
   - **Radial distance** (curve input `r`):
     - `|E|` (NDC);
     - if bNormalizeRadialDistance: `|E| / sqrt(1 + (min(|Ex|,|Ey|)/max)²)`, which equals **max(|Ex|, |Ey|)**: 0 at the centre, 1 on
       the NDC square's edge, including the corners. That's in NDC, so **no aspect correction**.
     - If bUseSourceDistance, `r = 0.5·|S − E|` instead.
     - `0.5·|S − E|` is always computed (vertex TEXCOORD2.y).
   - **Curves:**
     - `Scaling(r)`, `OcclusionScaling(Occ)`, `AxisScaling(r) × DistMap_Scale(D_src)`, `Rotation(r)`;
     - `Color(r) × DistMap_Color(D_src)`, `Alpha(r) × DistMap_Alpha(D_src)`;
     - `LFMaterialIndex(r)` picks the material (out of range → material 0; invalid → the default material);
     - `Offset(r)` is evaluated, but the fill doesn't use it.
   - **Placement:** **unproject (Ex, Ey, z = 0.1, 1)** with the inverse ViewProjection → `P = xyz / w_h`. The quad sits at NDC depth
     0.1 (near the camera, in front of everything; Foreground DPG), **not at the source's depth**.
   - **Size:** `k = 1 / w_h` = the clip w (view depth) of P. `TANGENT = (Size.X·Scaling·OcclScaling·k, Size.Y·Scaling·OcclScaling·k,
     AxisScale.x, AxisScale.y)`. The world extent is scaled by the view depth, so **the on-screen size is depth-independent**:
     NDC half-extent = 0.5 · Size · Scaling · OcclScaling · AxisScale · Proj[0][0] (x) or Proj[1][1] (y). The VS corner is
     (uv − 0.5) · tangent.xz / .yw along the camera right / up.
4. **Vertex** (all 4 corners equal except the UV):

| Offset | Value |
|---|---|
| 0 | (P, 1) |
| 16 | TANGENT above |
| 32 | Rotation |
| 36 | UV (0, 0) / (1, 0) / (1, 1) / (0, 1) |
| 44 | Color.rgb × DistMap_Color, Alpha × DistMap_Alpha (× SourceColor rgba when bModulateColorBySource; SourceColor at proxy+0xF0) |
| 60 | (r, 0.5·\|S − E\|, RayDistance) |
| 72 | (Occ, proxy+0xE4) |

5. The draw flags carry the DPG from the element setup. A disabled element (bIsEnabled clear) or a null material skips the
   quad.
6. **Still H:** what fills proxy+0xE4 / +0xE8.
   - Stock: +0xE8 = the occlusion query's visible fraction of the FixedRelativeBoundingBox (visible samples / the box's projected
     pixel area);
   - +0xE4 = the per-frame intensity / coverage.
   - The query writer wasn't traced.

## 4b. Intensity, curve tables, distance input (C, 2026-10-18)

- **proxy+0xE4 (TEXCOORD3.y "Intensity"):** set by `0x82FB6C40`.
  - **1.0** when proxy +0xD4 and +0xE0 are both 0 (H by name: OuterCone / Radius);
  - 0 if +0xE0 ≠ 0 and the camera is farther than +0xE0;
  - nonzero +0xD4 runs a cone test.
  - No MP template or component authors these, so **Intensity is always 1** in MP.
  - Coverage goes only through +0xE8 → ScreenPercentageMap.
- **No coverage gate in the element draw** (0x82FC6620, vtable slot 0x8232A704). Elements whose OcclusionScaling is constant
  (Reflection_01) stay visible while the flare primitive itself is drawn. H: the whole proxy is culled only by primitive
  occlusion / frustum on its bounds (the FixedRelativeBoundingBox).
- **Curve tables** (float `0x82B270B0`, vector `0x82B27210`):
  - header `[MinOut, MaxOut, InBias, InScale]`; points interleaved per point at stride ChunkSize;
  - `index = int(InScale·(x − InBias))`, clamped to the **table length − stride** (not LookupTableNumElements), lerp to the
    next point;
  - Type 1/7 = constant (first value).
- **DistMap_* input** = |ViewOrigin − flare origin| in **raw UU**, so every MP DistMap curve (authored over 0..1) takes its
  **last point**:
  - Base_SUN DistMap_Color = (0, 0, 2.45), so its colour is (0, 0, 1.225) × α 0.1, **blue**;
  - 02 DistMap_Scale = (5, 4, 4);
  - Reflection_01 DistMap_Alpha (InScale 0) = 1.
