# Campaign / Escalation culling, LOD and streaming levers (RE, 2026-10-24; for the 300 FPS pass)

**Labels:** C = config / authored data (read during the freeze with grep only); H = stock UE3 behaviour for this engine generation;
**M** = to measure after UNFREEZE (python over authored.db).

**Engine generation:** packages are version 511 / 144, i.e. UE3 of 2009. That fixes which systems exist.

## 1. Precomputed visibility: none (C)
- There are **0** `PrecomputedVisibilityVolume` objects in authored.db; that system came later in UE3. WFC has **no baked
  visibility cells**.
- **Runtime culling (H, stock UE3 of that era):**
  - per-view **frustum culling** of every primitive's bounds;
  - the **per-primitive cull distance** (§2);
  - **hardware occlusion queries**: a primitive occluded last frame is skipped and re-queried with its bounding box.
- **For us:** no data lever here. Frustum + distance (§2) + occlusion queries (or a Hi-Z equivalent) is the authored behaviour.

## 2. Cull distances: the main lossless lever (C)
- **CullDistanceVolumes exist in the maps** (`CullDistanceVolume_*` in 72 places; the class is referenced about 1,800 times).
- Primitive components carry:
  - **`CachedCullDistance`** (about 15,000 occurrences): the effective cull distance baked at cook from the volumes and the
    component's own value;
  - `CullDistance` (about 11,000 authored);
  - **`bAllowCullDistanceVolume`** (about 4,500 occurrences, mostly explicit false overrides).
- **Rule (H, stock):** a primitive is **not drawn when the distance from the view origin to its bounds is greater than
  CachedCullDistance**. 0 means never culled.
  - The volume's `CullDistances` table (size → distance) was applied at cook; the result is CachedCullDistance.
  - **Use CachedCullDistance as shipped; don't recompute it from the volumes.**
- This applies to static meshes (including the cooked StaticMeshCollectionActors), skeletal set dressing, decals and emitters
  (ParticleSystemComponent).
- **M:** the per-chapter count and histogram of components with CachedCullDistance > 0, so we can see how much each chapter
  culls.

## 3. Batching (C)
- `[Engine.StaticMeshCollectionActor] bCookOutStaticMeshActors=TRUE, MaxStaticMeshComponents=12`. The cooker merged
  StaticMeshActors into collection actors of up to 12 components.
- Each component **keeps its own bounds and CachedCullDistance**; culling is still per component.
- `StaticLightCollectionActor` likewise (up to 1,000 lights).

## 4. LOD (C config; data M)
- **SystemSettings (Xenon):**
  - **DetailMode=2** (High): every component with DetailMode ≤ High draws; none is dropped by detail mode at the original settings;
  - **SkeletalMeshLODBias=0**, **ParticleLODBias=0**, FoliageDrawRadiusMultiplier 1.0;
  - shadows: MinShadowResolution 128, MaxShadowResolution 1024, ShadowTexelsPerPixel 1.0, ModShadowFadeDistanceExponent 0.2;
  - texture LOD groups with LODBias 0 (lightmaps / shadowmaps capped at 512).
- **Skeletal meshes:**
  - LODInfo `DisplayFactor` is authored (about 160 occurrences).
  - H, stock: LOD *n* is used while the projected screen size < DisplayFactor(*n*), with LOD hysteresis. That covers characters,
    set-dressing skeletal props, gibs and bone-count reduced LODs.
  - **M:** the list of skeletal meshes with more than 1 LOD and their DisplayFactors, and which placed set dressing uses them.
- **Static meshes:** H, the 2009 StaticMesh LOD selection by screen size (LODDistanceRatio). **M:** the count of cooked static
  meshes with more than 1 LOD. Many 360 cooks ship LOD0 only.
- **Particles:** ParticleSystem `LODDistances` / LODMethod drive emitter LOD (PARTICLE docs). Distance-based LOD and
  `bUseFixedRelativeBoundingBox` culling are lossless.

## 5. Hidden and static actors at chapter start (C rule; lists M)
- `bHidden` actors don't render. About 96,000 serialized `"bHidden":true` (actors and components, all maps).
- **Kismet hides and shows things at PreGameplaySetup / GameplayStarted.** Examples: D3B hides the players for the intro, Omega and
  the BrokenHope walls. Those states hold until Kismet changes them; don't draw them before.
- `bNoDelete` is a spawn / destroy rule (level-placed and not destroyable), **not a render rule**. There's no culling lever in it.
- **Destroyed actors** (SeqAct_Destroy, e.g. establishing-shot props after A1's intro, Pulse spikes) must also leave the render
  list.

## 6. Streaming: the biggest lever (C, CAMPAIGN_STREAMING_RE §5 + campaign_streaming_order.json)
- Each chapter runs a sequence of **SetStreamingState** calls. Per level the target is **Visible** (in the world), **Loaded** (in
  memory but **not in the world: no rendering, collision or ticking**) or **Unloaded**.
- **The rebuild must render only the levels currently Visible in the active streaming state.** A merged-chapter renderer that draws
  every sublevel is drawing several times the original's scene.
- **The unload points per chapter (A1 … D5) are the SetStreamingState actions** in `work/handoff/campaign_streaming_order.json`
  (state names, their level lists, and the Kismet that fires them).
  - Checkpoints restore `Checkpoint.StreamingState`.
  - Escalation maps have no streaming states (all levels always visible).
- `LevelStreamingDistance` appears only about 6 times (no distance streaming in campaign). LevelStreamingVolume: 0.

## 7. To measure after UNFREEZE (python, read-only; one pass)
1. Per map package: primitive components with CachedCullDistance > 0, as a histogram and as a share of all primitives.
2. CullDistanceVolume `CullDistances` tables per map.
3. Skeletal meshes with LODInfo > 1 and their DisplayFactors; static meshes with more than 1 LOD.
4. Per chapter: the actors hidden by PreGameplaySetup / GameplayStarted Kismet, and actors with bHidden true that are never shown.
5. Per chapter: the Visible-level set per streaming state, with the triangle and primitive counts per level (with AssetTools'
   render data), to rank the lossless savings.

## 7a. Results (C, measured 2026-10-24 after UNFREEZE)
**Tool:** `tools/culling_lod_tables.py`. **Data:** `work/handoff/culling_lod_tables.json`, with:
- per-package primitive counts and cull histograms;
- the volumes;
- the skeletal LOD list;
- hidden-at-load actors by class;
- Kismet hides fired at start;
- per streaming state, the visible-level primitive totals.

**"Primitives"** = the primitive components serialized in authored.db (static / skeletal mesh, particle, decal, model, sprite). It
is a count, not triangles.

### Cull distances
- **222,454 primitives** across the 10 chapters and 2 ESC maps; **14,494 (6.5 %) carry a CachedCullDistance > 0**.

| CachedCullDistance | Count |
|---|---|
| > 32,000 UU | 11,726 |
| 16,000–32,000 | 2,745 |
| < 16,000 | 23 |
| 0 (never culled) | 207,960 |

- **The authored distances are long.** As a lever this is small: worth applying, but it isn't where the frame time is.

**Share culled per chapter:**

| Chapter | Share |
|---|---|
| D3A | **29 %** |
| A4 | 11 % |
| A5 | 7 % |
| D3B | 7 % |
| A3 | 6 % |
| A1 | 4 % |
| D5 | 4 % |
| A2 | 2 % |
| D1 | 2 % |
| D2 | 2 % |
| BrokenHope / Remnant | 1 % |

**CullDistanceVolumes:** only **2 in the whole campaign**.
- A3_COR_CoreExt_Art_m `CullDistanceVolume_12757`: size ≤ 1000 → 40,000.
- D3A_IAC_CityTrans_ART_m `_12145`: size ≤ 1800 → 19,000, ≤ 3500 → 0 (never).
- The CDO is a {0 → 0, 10000 → 0} placeholder.

### Skeletal LOD
- **All 401 cooked skeletal meshes ship exactly one LOD** (LODInfo length 1, DisplayFactor 0).
- **There are no skeletal or bone-count LODs in the 360 data.** The original draws LOD0 at every distance, so there's **no
  authored lever** here. Any skeletal LOD would be a rebuild-side (lossy) choice.

### Hidden at start
- **Actors with `bHidden` at load:** listed per package in the json. They're never drawn until Kismet unhides them.
- **Kismet `ToggleHidden (Hide)` within 3 hops of a start event** (PreGameplaySetup / GameplayStarted / LevelLoaded), counting
  target vars:

| Map | Count |
|---|---|
| A1 | 10 |
| A3 | 1 |
| A4 | 9 |
| D1 | 12 |
| D3A | 14 |
| D3B | 1 (the intro's players) |
| BrokenHope | 29 |
| A2, A5, D2, D5, Remnant | 0 |

### Streaming: the dominant lever

| Chapter | All levels (prims) | States | Median visible state | Largest state |
|---|---|---|---|---|
| A1 | 30,049 | 22 | 3,233 | Highway2 5,555 |
| A2 | 27,993 | 25 | 2,603 | Wall_Crawler 7,938 |
| A3 | 32,486 | 21 | 3,546 | Omega_Gate_Design 7,201 |
| A4 | 18,202 | 20 | 2,802 | Relay1Interior 4,681 |
| A5 | 4,381 | 4 | 2,704 | CraterLower 2,761 |
| D1 | 35,877 | 30 | 3,828 | Load_Assembly2 6,708 |
| D2 | 30,494 | 18 | 3,235 | Cave2B 5,507 |
| D3A | 20,228 | 28 | 3,924 | City5a 4,895 |
| D3B | 12,098 | 25 | 3,078 | ("Default" = all levels, a debug / editor state) |
| D5 | 1,784 | 1 | 1,784 | Arena |

- **Rendering only the active state's Visible levels cuts the primitive count to about 10–25 % of the whole chapter.** That's the
  original's behaviour and the largest lossless saving by far.
- A merged-chapter renderer draws 5–10× the original's scene.
- **Ignore "Everything" / "Default" states:** they're editor states, and no gameplay Kismet sets them (CAMPAIGN_STREAMING).
