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

## 2b. Canvas text and HUD markers (M08)

| Call | Behaviour |
|---|---|
| `drawCanvasText(font, utf8, x, y, rgba, scale)` / `canvasTextSize(...)` | Original UE3 fonts from render data `_ui/fonts/<font>.json` (`tools/render/build_hud.py`: cooked FFontCharacter table + CharRemap + glyph pages). |
| `render::HudMarkers` (`src/render/HudMarkers.h`) | TnObjectiveMarkerTypeSprite.Draw from the authored marker-type setups (`_ui/hud_markers.json`, 40 types). |

**Text:**
- UE3 Canvas layout: USize advance, VerticalOffset, no kerning.
- Glyph alpha × colour, display-referred.
- Composited like `drawScreenTriangles`.
- Fonts available: MarkerFont. SubtitleFont is not cooked in the loaded packages.

**Markers:**
- projection;
- focus (threshold, auto-focus range, hysteresis);
- on- and off-screen tiles (`Over`, `OnScreen`, `ArrowAngle`);
- MarkerFont labels in the setup colour;
- health bar.

Gameplay passes the markers its rules show. Ownership of the in-match HUD layers: `docs/handoffs/FRONTEND_INMATCH_HUD.md`.

## 2c. Frontend 3D scenes, loading yields, characters (M09)

| Call | Behaviour |
|---|---|
| `loadFrontendScene(levels)` / `drawFrontendScene(camPosUE, camRotUEdeg, fovDeg, w, h, timeSec)` / `unloadFrontendScene()` | The live UI levels behind the menus (RE OVERNIGHT §D). |
| `setLoadYield(callback)` | Called between bounded load steps with no GL binding held, so the loading movie keeps presenting (RE PLAYTEST §6). |
| `setFrontendActorTransform(actor, posUE, rotUEdeg)` | Absolute matinee pose (UE units, degrees, attachment already applied) for a scene actor; applied as a delta against its authored pose. Actors not sent keep their authored pose and PHYS_Rotating. |
| `setMapEffectActive(key, on)` | Key = Emitter actor name (short or full path) or its ParticleSystemComponent name. Scene emitters start in their authored bAutoActivate state. |
| `setDisplayGamma(g)` | UE3 DisplayGamma for the scene resolve and Canvas material tiles (default 2.2). Profile Brightness → g is `HmProfileSettings.GetGammaSetting`: `2.2 + Lerp(-0.95, 0.95, Clamp(GammaSetting/100, 0, 1))` (CONFIRMED script), computed by the caller. GFx / video / Canvas text stay display-referred. |
| `setFrontendSceneDraw(callback)` | Called inside `drawFrontendScene` after the scene geometry, before translucency / post. The caller draws dynamic bodies there, e.g. the customization preview pawns: `setDrawOwner(slot)` + `setCharacterColors` + `drawDynamicMesh`. Gameplay owns the body (chassis, form, pose); Rendering draws it. |
| `render::ueActorMatrix(posUE, rotUEdeg)` | Model matrix for an exported content glTF (roster robot / vehicle) placed at a UE location / rotation, as authored actors are placed. |
| `setDrawOwner(id)` | Per-character light environment and applier colours for the following dynamic draws. |

**Frontend scenes:**
- `loadFrontendScene` loads the exported family with the most scenery; `drawFrontendScene` draws a complete frame;
  the GFx layer is composited after it.
- The camera comes from the Frontend's CameraActor / Matinee evaluation.
- Render data: `build_render_data.ps1 -Map UI_FrontEnd | UI_CharacterCustomization | UI_PartyLobby | UI_Lobby |
  UI_CampaignLobby`.

**Scene actors (render_index actors_by_level):**
- authored poses, authored bHidden (`setActorHidden` overrides) and PHYS_Rotating for the actors in world.glb;
- skeletal actors (the 17 vignette ships) are loaded from their glTF in bind pose, materials via
  `tools/render/scene_materials.py`. Their skeletal animation is not played [PARTIAL].

**Scene family:** the persistent level's family (`levels.front()`) is preferred: its render data composes its streamed
sublevels. The lobbies cook every MP chassis material in `UI_PartyLobby_m` / `UI_Lobby_m`, not in
`UI_CharacterCustomization_m`.

**Standard render data:** `build_render_data.ps1 -Map Standard` builds `MP_IAC_Streets` plus the five UI families. Every
worktree that runs the frontend needs it.

**Loading yields:**
- On UI_FrontEnd: 193 yields, longest step 70 ms. The remaining long steps are single large texture decodes.
- `loadMapRenderData` always releases the previous map first.

**Draw owner:**
- Robot / vehicle / weapon roles come from material packages, never from character names.
- Character materials come from the AssetTools roster (`tools/render/character_materials.py`: 98 materials over the
  MP chassis, all verified against their shipped permutations). They compile on the character's first draw, not in
  the prewarm.

## 2d. Render path diagnostics (M10)
- **Render data root:** `WFC_RENDER_DATA`, else the first `work/render` up to 4 levels above the executable that holds
  render data. A missing root is an ERROR.
- **`renderDiagnostics()`:**
  - render path (original / legacy, and why);
  - last frame's draw counts (world / BSP / dynamic / FX / opaque / translucent / lightmapped / culled / without program);
  - distinct materials and programs;
  - loaded resource counts;
  - camera and matrices, viewport, framebuffer;
  - with `WFC_VISUALCHECK`: pre-UI scene metrics and the inherited GL state.
- **Legacy fallback:** when a map or frontend scene asked for render data and fell back, a red 10 px frame is drawn unless
  `WFC_LEGACYRENDER` is set.
- **Validation:** `tools/render/visual_check.py`, `tools/render/visual_suite.sh`.

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
