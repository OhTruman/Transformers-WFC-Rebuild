# Chapter loader: several sublevels in one Pipeline (campaign)

Rendering, 2026-10-23. Integration asked for this with no separate design round: a short design, then the code.

## Data (AssetTools)

- One unit per streamed sublevel: `<render root>/<SublevelName>/` (the URL name, no `_m`), with `world.glb` inside.
- Per unit, the full render-data set: materials_glsl, lighting (lights, lightmaps, fogs, volume_fogs, postprocess), lightmaps/,
  map_fx_runtime, movers, fog_volumes, material_instance_actors, beast_probes, LVV, bsp.glb, decals.glb, render_index.
- A chapter manifest (from `campaign_streaming_order.json`) lists the units. The first is the persistent level.

## Runtime

`Pipeline::load(unit)` stays as it is: it loads the persistent unit, so MP is unchanged. `Pipeline::loadSublevel(unit)` merges one
more unit into the live Pipeline. A sublevel id (1..n; 0 = the persistent unit) tags whatever it adds.

| Data | Merge rule |
|---|---|
| materials | by name; the first definition wins (identical across units) |
| lights | appended; each unit's LVV binds its own unit's lights (stage 2: the first pass skips merged units' LVVs, so their lights use the light-environment traces) |
| lightmaps | the per-component records are added with the unit's directory (`LMRec::dir`), so pages load from that unit's lightmaps/ and never collide |
| vertex lightmaps, component flags, interpolated lightmaps | added by key |
| BSP / decals | uploaded as additional world meshes, lit through the same per-component lightmap records |
| world.glb | uploaded by GLRenderer per unit (`loadChapter`) |
| map FX, movers, fog volumes, material instance actors, beast probes, lens flares | appended |
| height fogs | the union of the units' layers, highest first, capped at 4 (FScene::AddHeightFog) |
| volume fogs | appended to the post-process volume list |
| post-process, R1, exposure | the persistent unit's only |

Ownership: every GPU resource a sublevel adds (meshes, lightmap textures, FX instances) is recorded under its id. That lets
`unloadSublevel(id)` (stage 3) free exactly those without a full `release()`.

## Stages

1. Static all-loaded mode (D5_OME): `GLRenderer::loadChapter(units)` = `load(units[0])` + `loadSublevel` for the rest + every
   unit's world.glb. Nothing streams.
2. Each merged unit's LVV bound to its own lights; several BSP meshes in the MDI path.
3. `unloadSublevel` / `loadSublevel` at runtime, from Gameplay's streaming notifies (load / unload events).

## Checks

- MP: `load()` behaviour unchanged. Identity (SIMHASH + captures) on one MP map.
- D5_OME: every unit loads, materials compile, lightmaps resolve per component, and a free-camera eye sheet across the
  sublevels shows no missing geometry and no wrong lightmaps.
