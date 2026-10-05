# M06 human playtest: black Streets, malformed menu scene, missing lobby background (Rendering M10)

**For:** Integration, Experimental and Frontend.

**Root cause (CONFIRMED, reproduced):** the integrated M06 executable ran without the original render path.

## The failure
1. `Pipeline::renderDataRoot()` (agents/rendering) resolved the render data as `<exe>/../../work/render`.
   - That is correct only for `build/bin`.
   - The M06 playtest executable is the Release build, `Rebuild\build\release\bin\wfc_rebuild.exe` (named in Integration's
     M06 STATUS). For it, the path resolves to `Rebuild\build\work\render`, which does not exist.
2. Every `loadMapRenderData` / `loadFrontendScene` therefore failed. The failure was logged at warning level, and the
   renderer silently used the legacy fixed-function path.
3. Each visible symptom follows from that:
   - **Title:** the frontend adapter's interim path drew `UI_FrontEnd/world.glb` fixed-function. The result is a giant grey
     sphere (the SpaceDome), rainbow tori (the energon rings without their materials) and flat grey panels.
   - **Streets:** fixed-function world geometry without lightmaps or materials. It is mostly black; the skinned Optimus,
     the weapon, the HUD and some effects still draw, because they do not depend on map render data.
4. Every automated capture used the direct-to-match boot (`WFC_SMOKE_FRAMES`, `WFC_SHOTLIST`, … force it; see
   `Application::wantsFrontendBoot`). Those runs used the Debug executable from `build/bin` or set `WFC_RENDER_DATA`, so
   no automated run took the failing path.

## Evidence
All runs below are in `agents/rendering/work/m10`.

| run | result |
|---|---|
| integration commit 95edd7b, built locally, human path (frontend script), render data available | title, lobbies and Streets correct |
| same build, direct boot, spawn 18 | Streets correct |
| same build, human path, render data unavailable (`WFC_RENDER_DATA` → empty folder) | reproduces the recording exactly: grey sphere and rainbow tori behind the menu, grey lobby panels, black Streets with only Optimus / HUD / weapon |

- **Render data:** integration's Streets render data equals Rendering's. `bsp.glb`, `lvv_0.bin`, `decals.glb`, `movers.json` and
  `map_fx_runtime.json` are byte-identical. `lighting.json` and `materials_glsl.json` differ only in absolute versus relative
  file paths.
- **Integration code changes:** Integration's own changes to render code (render_index fallback, Mesh nodeName, the legacy
  BindBuffer guard, build_lighting BSP fixes) are not involved.

## Fixed in agents/rendering
- **`renderDataRoot()`:** the first `work/render` that actually holds render data, searched up to 4 levels above the
  executable (`build/bin`, `build/release/bin`, a copied `bin/`). The chosen root is logged; if none is found, an ERROR
  names the fix.
  - Integration's uncommitted M06b patch does the same. Take Rendering's version on merge, because the two conflict.
  - `WFC_RENDER_DATA` still wins.
- **Legacy fallback is never silent:**
  - a failed load logs at ERROR level ("LEGACY RENDERER, not the original presentation");
  - `IRenderer::renderDiagnostics()` reports `originalPath = false` together with the reason;
  - a 10 px red frame is drawn around the screen whenever a map or frontend scene asked for render data and did not get
    it. `WFC_LEGACYRENDER`, the intended fallback, draws no frame.
- The legacy-path crash guard (Integration M06 `drawMeshArrays`) is applied identically, so the merge stays clean.

## For Experimental / Integration: how to reject a broken run
- **`WFC_VISUALCHECK=1`:** every screenshot also writes `<shot>.bmp.json`, and every 120 frames a line is logged:
  `VISUALCHECK frame N path=original|legacy black= flat= ... -> PASS|FAIL: reasons`. That covers live human sessions too.
  The JSON holds:
  - render path, data root, last load error;
  - draws: world / BSP / dynamic / FX / opaque / translucent / lightmapped;
  - culled submeshes; drawn submeshes without a program, with their material names;
  - distinct materials and programs;
  - loaded material / program / texture / lightmap / mesh counts;
  - camera position, yaw, pitch and FOV; view-projection matrix;
  - viewport and framebuffer;
  - the inherited GL state;
  - metrics for the pre-UI scene and the final image (black fraction, flat 16×16 tiles, luminance p50 / p95, colour count).
- **`tools/render/visual_check.py <captures> [--ref <dir>] [--expect-map]`:** exits 1 on:
  - a renderer FAIL;
  - a map capture that is not on the original path or has no world / BSP draws;
  - more than 25 % of pixels differing from a known-good reference;
  - world / BSP / material draw counts below half of the reference.
- **`tools/render/visual_suite.sh <exe> <out> [--ref <dir>] [--flow]`:**
  - five fixed Streets cameras (`WFC_RENDERCAM`, independent of Gameplay's camera code);
  - the title scene from its authored camera;
  - with `--flow`, the human path: title → party lobby → Streets → match end → lobby → Streets.
  - The M06 executable's failure mode fails every capture. A good build passes every capture.

## Not caused by the renderer, but seen in the recording
- **Character preview** in customization: Rendering now provides the draw path.
  - **Draw:** `IRenderer::setFrontendSceneDraw`. Inside it, call `setDrawOwner(slot)`, `setCharacterColors`, then
    `drawDynamicMesh(body, render::ueActorMatrix(posUE, rotUEdeg))`. Verified with Bumblebee: original materials, lobby light,
    colour overrides.
  - **Render data:** run `tools/render/build_render_data.ps1 -Map Standard` in every tree that runs the frontend.
  - Before this pass, nothing instantiated one:
  - All 66 robot / vehicle body meshes of the 33 roster chassis are exported (AssetTools `roster_package.json`, every glTF
    present).
  - No code path hands the selection to the renderer: Frontend's `setFrontendPreviewCharacter` is still a proposal in
    `docs/FRONTEND.md` §12, and the renderer has no such entry point.
  - The lobby 3D layer is the authored `UI_CharacterCustomization` SpaceDome (180 vertices). It draws correctly and is dark
    by design.
- **Second match on Seed** in a flow test: the lobby rotates the map after a match. That is authored behaviour, not a
  rendering issue. A map without render data now fails visibly.
