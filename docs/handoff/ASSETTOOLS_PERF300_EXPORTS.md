# AssetTools exports for the 300 FPS pass (Rendering, 2026-10-24)

Integration runs these if the AssetTools session stays blocked. Run them after the a2e9fbd9 package verifies.
Background: PERF_300_PLAN.md phase 1, items 0a and 0c, and RE CAMPAIGN_CULLING_LOD_RE.md sections 2 and 4.

## 1. Per-component cull distance: ASSETTOOLS_CULL_DISTANCE.patch (scripts/wfc/vs_map.py)
**What the patch changes:**
- **Where:** `lm_extras` builds the extras of every world.glb mesh node: StaticMeshCollectionActor components, StaticMeshActor,
  InterpActor, StaticInterpActor and TnStaticDestructibleActor.
- **What it adds:** `extras.cached_cull_distance`, the component's PrimitiveComponent.CachedCullDistance (UE units), from the same
  effective authored values the HiddenGame flag already reads (`authored.values(component)`). It is written only when > 0.
- **Effect on existing data:** none beyond the new key. The node set, transforms, materials and lightmap keys stay unchanged.

**Applying it:**
- Base: AssetTools d6332c7 (`vs_map: world.glb nodes carry extras.hidden_game`). The patch is CRLF like the file:
  `patch -p1 --binary < ASSETTOOLS_CULL_DISTANCE.patch` in AssetTools.

**Re-export:**
- **MP and DLC maps:** `run_map_pipeline.py <MAP>` runs vs_map twice, the second time after vs_vertex_remap. Re-running only the
  world stage is enough: `vs_map.py <MAP>`, then copy world.glb into the render data as the chain does.
- **Campaign chapters:** world.glb comes from the split input (work/campaign_render_dryrun/split_sublevels.py) built on vs_map's
  output. Re-run vs_map for each chapter level set, then the split, then campaign_render.py's copy step (it copies world.glb only
  when missing, so remove the old copy first, or copy it over).

**Check:**
- `grep -c cached_cull_distance` on a chapter's world.glb JSON chunk. RE §7.1 gives the expected component counts.
- Values must equal authored.db's CachedCullDistance for 3 sample components per map.

**Renderer side (Rendering, after UNFREEZE):** reads the key, keeps a node only while
`distance(view origin, node bounds) <= cached_cull_distance / 100` (metres). It covers the MDI build plus the dynamic, map FX and
set-dressing paths. Shadow depth uses the same rule with the view origin, as UE3.

## 2. Skeletal LOD1 and above (spec; no patch yet)
- **Today:** the content glTFs in ExtractedAssets carry LOD0 only (the shared export, read-only).
- **Needed for 0c:** the LOD1+ meshes of every skeletal mesh whose LODInfo count is > 1 (RE §7.3 lists them, after UNFREEZE),
  plus each LOD's `DisplayFactor` and `LODHysteresis`.
- **Proposal:** a separate export, never into ExtractedAssets.
  - Output: `AssetTools/out/skel_lods/<package>/<mesh>_LOD<n>.gltf`.
  - Tool: umodel's all-LOD export (`-lods`) for the listed meshes only.
  - Index: `skel_lods.json` with mesh, lod, gltf, display_factor and hysteresis.
- **The renderer side** picks the LOD by projected screen size as UE3 does. Characters keep the same skeleton, so the GPU skin
  palette is shared across LODs.
- **Scope decision:** after RE's §7.3 list. If only a handful of meshes have LODs, this lever is small and goes last.
