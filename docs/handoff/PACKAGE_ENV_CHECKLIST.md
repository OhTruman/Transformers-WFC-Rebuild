# Package environment checklist (Systems, 2026-10-24)

A distributable package must find every file through the folder it is unpacked into. The runtime reaches files three ways: an
environment variable `Play.bat` sets (relative to the package root `%R%`), a default derived from one of those, or a literal path
in the code. The first two are fine; a literal `F:/Transformers Rebuild/...` (or `core::config::kAssetRootDefault` read directly) only
works on the development PC. RE's DEV_PATH_AUDIT lists the code literals; Rendering is fixing the game-code ones.

## 1. Set by Play.bat (tools/package/slim_template/Play.bat, from 307413d6)

| Variable | Package path | Read by |
|---|---|---|
| WFC_ASSETS, WFC_ASSET_ROOT | %R%/ExtractedAssets/VerticalSlice | maps, chassis defs, almost every asset path |
| WFC_CONTENT | %R%/ExtractedAssets/content | content PNGs / waves |
| WFC_RENDER_DATA | %R%/Rebuild/work/render | per-map render data |
| WFC_MANIFESTS, WFC_FRONTEND_MANIFESTS, WFC_ASSETTOOLS_MANIFESTS | %R%/AssetTools/manifests | the frontend catalogue (incl. dlc/dlc_inventory.json), map manifests |
| WFC_DDS_ROOT | %R%/AssetTools/out/dds | original-block textures; the HD root defaults beside it |
| WFC_FRONTEND_DATA | %R%/Rebuild/data/frontend | the frontend data, previews; data/campaign beside it |
| WFC_CAMPAIGN_ROOT | %R%/AssetTools/out/campaign_render | the campaign chapters |
| WFC_DLC_TEXT | %R%/AssetTools/work/dlc | frontend::dlcTextFile (DLCMaps / DLCMaps2 __TransGame.<lang>) |
| WFC_DLC_CONTENT | %R%/ExtractedAssets/content | frontend::dlcContentRoot (the DLC thumbnails) and SoundCues' DLC wave fallback. build_slim merges the 587 dlc_only_content.tsv files into ExtractedAssets/content (the 9 UI_DLC*Thumbnails_p files among them) |
| WFC_BASE_CONTENT | %R%/ExtractedAssets/content | SoundCues' base wave fallback |

## 2. Not set, and need not be: the default derives from a set root

| Variable | Default when unset |
|---|---|
| WFC_EXTRACTED | the parent of WFC_ASSETS |
| WFC_HD_ROOT | beside WFC_DDS_ROOT (AssetTools/out/hd: where the HD pack merges in) |
| WFC_HD_PBR_ROOT | the HD root |
| WFC_LIGHTING_DIR, WFC_MAPFX_DIR, WFC_RT_VBAKE | the map's render data directory |
| WFC_CAMPAIGN_AUDIO | <frontend data>/../campaign |
| WFC_LOC_ROOT | the level's content root (_LOC) |
| WFC_PBR_DIR, WFC_RT_MATDIR, WFC_OVERLAY_ROOT | optional development overrides; unset = the packaged files |

Every other WFC_* variable the code reads (about 670) is a test, tuning or diagnostic switch, not a path.

## 3. Literals the package cannot satisfy (code fixes)

- GLRenderer::drawReticle: `config::kAssetRootDefault + "/../content/UI_GFxHud_p/Hud_GFX_I190.png"` (Rendering's one-liner: use
  WFC_ASSETS / WFC_CONTENT). The file itself ships since 307413d6.
- Every other F: literal is in RE's DEV_PATH_AUDIT; Rendering is fixing the game-code ones.

## 4. Catching misses before the laptop

- **verify_slim is already a clean-machine check for the code it runs.** It runs the package through a subst letter and logs every
  file open: an open outside the package root counts as a failure ("outside, package LACKS it"), even when the dev copy on F: made
  it succeed. That is how 48e42af6's two hidden dev-path reads (the DLC text, the reticle) showed up. Making the dev folders
  unreachable is not an option: ExtractedAssets and AssetTools are shared read-only resources (never renamed, never junctioned away).
- **The gap is coverage, not isolation.** verify_slim runs Streets, Gorge, Debris, the settings test and the RT check. Code paths
  those runs never reach (a campaign chapter, an Escalation wave, a DLC map, voice / online, the subtitles) are not checked.
  Proposed for the distributable package: verify_everything adds `-Maps` rows for D1_ORB, MP_ESC_Remnant (Escalation) and one DLC
  map (MP_ESC_District) on pass 1, so their opens are logged the same way.
- **Play.bat's drive letter:** it substs the package to F: only when F: is free, else W..G. On the development PC F: is taken, so
  a bare `F:/...` literal reaches the dev tree there - the verify's "outside" rule still flags it.
