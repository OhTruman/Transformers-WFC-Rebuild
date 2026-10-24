# Dev-path audit for the distributable package (RE, 2026-10-24)

**Scope:** Rebuild commit **203449b7**, `src/`, `data/`, `tools/package/slim_template/`.
- Searched with `git grep -i -E 'F:[/\\]{1,2}Transformers[ _]Rebuild|C:[/\\]{1,2}Users[/\\]{1,2}coxes|Rebuild-Systems|AssetTools[/\\]{1,2}out'`.
- **47 hits:** 0 in `data/`; **0 for `C:/Users/coxes` and `Rebuild-Systems`**.

**How the package resolves paths:** `Play.bat` `subst`s the package folder to a free drive letter. It **tries F: first, else W, V, …
G**, then sets the `WFC_*` roots to `<letter>:/Transformers Rebuild/...`.
- Every literal that is **behind one of those env vars**, or **relative to WFC_ASSETS**, works on any PC.
- **A bare `F:/Transformers Rebuild/...` literal only works when F: was free and got the subst.** On a PC where F: is taken (a DVD,
  USB or second disk), the game silently reads the wrong place or nothing.

## A. Breaks off this PC when F: is taken (no env override)
These are all in `WorldCampaign*.cpp`, which **Rendering is already fixing (in progress)**:

| File:line | Literal | What breaks |
|---|---|---|
| src/game/WorldCampaignDestructibles.cpp:91 | `F:/…/AssetTools/out/campaign_characters/destructibles/` | campaign destructible blueprints / meshes: none spawn |
| src/game/WorldCampaignDestructibles.cpp:116 | `F:/…/ExtractedAssets/` | destructible content (meshes, materials) |
| src/game/WorldCampaignMatinee.cpp:440 | `F:/…/ExtractedAssets/` | matinee actor / mesh content |
| src/game/WorldCampaignMatinee.cpp:465 | `F:/…/ExtractedAssets/` | the same (second loader) |
| src/game/WorldCampaignMatinee.cpp:538 | `F:/…/ExtractedAssets/` + clip | matinee animation clip preload: cutscene characters don't animate |
| src/game/WorldCampaignMatinee.cpp:557 | `F:/…/ExtractedAssets/` + clip | the same, the on-demand clip load |
| src/game/WorldCampaignRuntime.cpp:37 | `F:/…/AssetTools/out/campaign_characters/` | campaign AI bodies: no enemies or squad |
| src/game/WorldCampaignRuntime.cpp:40 | `F:/…/AssetTools/out/escalation_characters/` | Escalation horde bodies (via the campaign runtime) |
| src/game/WorldCampaignRuntime.cpp:782 | `F:/…/AssetTools/out/campaign_characters/spawnables.json` | Kismet spawnables (pawn factories): spawns fail |
| src/game/WorldCampaignRuntime.cpp:955 | the same file | the same (second reader) |

**Fix pattern:** derive from an existing root. For example, `WFC_CONTENT` (already set: `…/ExtractedAssets/content`) + `/..` for
`ExtractedAssets/`. For `out/campaign_characters` / `out/escalation_characters`, add `WFC_CAMPAIGN_CHARS` / `WFC_ESC_CHARS`, or use
`assetRoot()/../../AssetTools/out/…`, as World.cpp:5435 and ChassisDef.cpp:153 already do.

## B. Env-overridden, and Play.bat sets the variable: OK off this PC

| File:line | Env var |
|---|---|
| src/core/Config.h:260 `kAssetRootDefault` | WFC_ASSETS; every one of its 17 use sites checks WFC_ASSETS first |
| src/frontend/Catalog.cpp:66; src/game/World.cpp:491, 6464; src/game/MapState.cpp:157 | WFC_FRONTEND_MANIFESTS (MapState: WFC_ASSETTOOLS_MANIFESTS first) |
| src/game/ChassisDef.cpp:81; src/game/WorldCampaign.cpp:50; src/game/WorldCampaignMatinee.cpp:166, 513; src/game/WorldCampaignRuntime.cpp:26; src/game/World.cpp:5626; src/game/WorldSurvival.cpp:149, 369 | WFC_MANIFESTS |
| src/frontend/Catalog.cpp:510; src/game/WorldCampaign.cpp:71 | WFC_CAMPAIGN_ROOT |
| src/frontend/DataPaths.cpp:60; src/game/SoundCues.cpp:323 | WFC_DLC_CONTENT |
| src/frontend/DataPaths.cpp:69 | WFC_DLC_TEXT |
| src/game/SoundCues.cpp:324 | WFC_BASE_CONTENT |
| src/render/gl/WfcPipeline.cpp:2125–2130 | WFC_DDS_ROOT, else `<ExtractedAssets>/../AssetTools/out/dds` (relative) |

**Note:** Play.bat sets `WFC_DLC_CONTENT` to `…/ExtractedAssets/content`, while the code default is `AssetTools/out/dlc_content/content`.
Check that the package's DLC content really sits under ExtractedAssets/content.

## C. Relative to WFC_ASSETS: OK
- src/game/ChassisDef.cpp:153–154 and src/game/World.cpp:5435–5436: `assetRoot()/../../AssetTools/out/{campaign,escalation}_characters/`.

## D. Dev or test paths only (not reached in normal play; env-guarded anyway)
- src/core/Application.cpp:76 (WFC_MODETEST, WFC_ASSET_ROOT);
- src/core/Application.cpp:7062, 7196 (weapon / tuning audits, WFC_MANIFESTS);
- src/core/Application_KismetTest.cpp:17 (WFC_MANIFESTS).

## E. Comments only (no runtime effect)
- src/frontend/Catalog.h:12;
- src/render/Renderer.h:219;
- src/render/gl/WfcPipeline.cpp:2100, 2119 (a log string), 2216, 2223;
- tools/package/slim_template/Play.bat:3 (comment); Play.bat:22 and 24 are the correct env assignments.

**Rendering's other items** (outside these literal patterns: game code and the reticle, 11 items) are **in progress** per
Integration and aren't repeated here. Run this grep again on the head once they land.
