# Renderer contract for Frontend, HUD and additional maps (agents/rendering, M07)

This document defines what `render::IRenderer` (`src/render/Renderer.h`) provides to the other lanes.

Rendering owns:
- drawing;
- render data;
- GPU resources.

Frontend, Gameplay and Systems own:
- state;
- movies;
- ActionScript execution;
- decoding.

## 1. Level travel

| Call | Behaviour |
|---|---|
| `loadMapRenderData(dir)` | Loads `WFC_RENDER_DATA/<dir>` (materials, lighting, BSP, decals, movers, map FX, props from `WFC_ASSETS/Maps/<dir>/render_index.json`). |
| `unloadMapRenderData()` | Releases every GPU object of the map and of the shader path: meshes, programs, textures, render targets. |

After `unloadMapRenderData()`:
- **Mesh handles:** handles returned by `uploadMesh` before the unload stay in range but are empty (they draw nothing).
  The owners re-upload after the next load.
- **Texture handles:** `uploadTexture` handles are not released. They are UI-owned, and survive travel.
- **Verification:** `WFC_RELOADTEST=<frame>` runs a full in-process unload / reload / world reload. The captured frame
  matches a run without the cycle, apart from character pose and time.

## 2. Canvas material tiles (HUD markers: RE M05 GAMEPLAY §5, Canvas, not Scaleform)

`drawMaterialTile(MaterialTile)` is UE3 `Canvas.DrawMaterialTile`:
- a screen quad in pixels, top-left origin;
- optional rotation about its centre and a UV rectangle;
- shaded by the compiled original material;
- per-draw parameter values, standing in for a `MaterialInstanceDynamic`: scalar → `x`, vector → `xyzw`. A parameter
  that is not passed keeps its authored value.

| Point | Detail |
|---|---|
| Materials | `tools/render/ui_materials.txt`: the 15 `UI_HudMarkers_p` materials. They are compiled with every parameter as a runtime uniform and verified against their shipped permutations (231/231). Add more UI materials to that list. |
| Order | Drawn after the scene's post pass, in submission order, without depth. |
| Display gamma | 1/2.2 is applied, as the scene post does. **PARTIAL:** WFC's canvas tile shader is not decoded. |
| Availability | `hasMaterial(path)` reports whether a material is available. `drawMaterialTile` returns false when it is not. |
| Parameter names | As authored: `OnScreen`, `ArrowAngle`, `Neutral`, `Flashing`, `PingOpacity`, `TeamID`, `NodeID`, `Health`, `Alpha`, `FriendlyColor`, `EnemyColor`, `NeutralColor`. The UI_HudMarkers_p materials also read `AutobotColor` and `DecepticonColor` (vectors). |
| Sizes | Fractions of the canvas, per RE: focused 0.05, unfocused per type, off-screen 0.0625. The caller multiplies by the canvas size. |
| `MarkerAlly_MAT` | Authored empty (no expressions), so it draws nothing. Ally tags are the label plus `TargetHealthBar_MAT` (CONFIRMED data). |
| Verification | `WFC_TILETEST=1` draws sample markers. |

## 3. 2D composition (GFx movies, Bink frames, loading screens, fades)

| Call | Behaviour |
|---|---|
| `drawScreenTriangles(ScreenBatch)` | Triangle lists in pixels (top-left origin) with per-vertex RGBA and UV, optionally textured (`uploadTexture` handle). |
| `updateTexture(h, image)` | Replaces a texture's contents (video frames, dynamic bitmaps). |
| `viewportWidth()` / `viewportHeight()` | The current back-buffer size. |

Batch options:
- blend `Alpha`, `Premultiplied`, `Additive`, `Multiply` or `Opaque`;
- optional scissor (pixels);
- clamp or wrap addressing;
- linear or nearest filtering.

| Point | Detail |
|---|---|
| Display-referred | Values are written as given, with no gamma conversion. UI and video sources are authored in display space. |
| Inside a 3D frame | Queued, then composited after the scene, post, Canvas tiles and the reticle, in submission order. |
| Outside a frame | Drawn immediately (frontend-only screens). |
| Movie scaling | The caller maps the movie stage (e.g. Hud_GFX 1120×720) to the viewport. **PROVISIONAL:** the stage scale mode is assumed to be ShowAll (RE). |
| Verification | `WFC_SCREENTEST=1` draws a fade and an additive panel. |

The frontend scene (`UI_FrontEnd_m`: orbit cameras, `CybertronSky_PostProcess_PP`, energon materials) is a map. It uses
the same `loadMapRenderData` path once AssetTools exports it.

## 4. Additional maps

The render data tools take the map name:

```
powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1 -Map <MapName>
```

- **Cooked packages:** `<Map>_ART_m.xxx` and `<Map>_BASE_m.xxx`.
- **AssetTools per-map manifests:** `<prefix>_movers.json`, `<prefix>_kismet.json`, `<prefix>_pickup_fx.json`.
  - The prefix is the last token of the map name in lower case (`streets`), or `WFC_MANIFEST_PREFIX`.
  - A missing manifest gives empty movers or pickup systems.
- **Inline lightmaps:** listed from the ART package (`build_lighting.py --list-inline`).
- **Runtime asset roots:** `WFC_ASSETS` (default `ExtractedAssets/VerticalSlice`) and `WFC_CONTENT` (default
  `<assets>/../content`). No absolute paths remain in `src/render`.
- **Map props:** from `render_index.json`.
  - Pickup factory meshes: `pickup_factory_visuals`, gated by the factory's game rule.
  - Totems and KOTH rings.
  - Destructible state meshes: `states[]`, joined to their cooked `StaticMeshComponent`s by `build_map_fx.py`.
- **Remaining Streets-specific code: none in `src/render`.** `World.cpp` still loads `Maps/MP_IAC_Streets`. That is
  Gameplay's; the agents/frontend `setMapName()` replaces it.
