#pragma once
#include <string>

namespace frontend {

// The folder holding the authored frontend data (data/frontend/*.json: game settings, character presets, chassis XP
// unlocks, scenes, profile defaults, graphics presets), resolved once at runtime so a packaged copy finds its own:
//   1. WFC_FRONTEND_DATA (explicit override, e.g. the package verifier);
//   2. next to the exe's tree: <exe dir>/../../data/frontend (build/bin), <exe dir>/../../../data/frontend
//      (build/release/bin), <exe dir>/data/frontend;
//   3. the compile-time source tree (WFC_SOURCE_DIR/data/frontend), the old behaviour.
// A candidate counts when it holds profile_defaults.json. No trailing slash.
const std::string& frontendDataDir();
// The package / workspace root that holds AssetTools, ExtractedAssets and Rebuild ("Transformers Rebuild"): from the package roots
// WFC_ASSETS (<root>/ExtractedAssets/VerticalSlice), WFC_CONTENT (<root>/ExtractedAssets/content) or WFC_FRONTEND_DATA
// (<root>/Rebuild/data/frontend), else the nearest parent of the exe's folder that contains AssetTools. Every frontend default path
// derives from it (no absolute dev path); the WFC_* overrides keep priority.
const std::string& packageRoot();

// frontendDataDir() + "/" + name.
std::string frontendDataFile(const char* name);
// PC ADAPTATION (the user, playtest 2026-10-23b #5): a Graphics row's preview image, data/frontend/previews/<row>_<value>.png
// (row: Upscaling / HDTextures / RayTracing / FrameGeneration); "" when that value has none (the panel then hides).
std::string settingsPreviewPng(const std::string& row, int value);
// The DLC packs' extracted content (AssetTools out/dlc_content/content; WFC_DLC_CONTENT overrides): the map thumbnails
// UI_DLCThumbnails_p / UI_DLC2Thumbnails_p live there, not in ExtractedAssets.
std::string dlcContentRoot();
// The DLC packs' text (DLCMaps/DLCMaps__TransGame.<lang>, DLCMaps2/DLCMaps2__TransGame.<lang>; UTF-16): AssetTools' extraction
// (work/dlc/MapPack1|2/files); WFC_DLC_TEXT overrides (a packaged build ships them).
std::string dlcTextFile(int pack, const std::string& lang);
// "<Package>.<Name>" -> the extracted PNG: ExtractedAssets/content first, then the DLC content root; "" if neither has it.
std::string contentImagePng(const std::string& extractedRoot, const std::string& object);

} // namespace frontend
