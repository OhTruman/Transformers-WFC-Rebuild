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

// frontendDataDir() + "/" + name.
std::string frontendDataFile(const char* name);
// The DLC packs' extracted content (AssetTools out/dlc_content/content; WFC_DLC_CONTENT overrides): the map thumbnails
// UI_DLCThumbnails_p / UI_DLC2Thumbnails_p live there, not in ExtractedAssets.
std::string dlcContentRoot();
// The DLC packs' text (DLCMaps/DLCMaps__TransGame.<lang>, DLCMaps2/DLCMaps2__TransGame.<lang>; UTF-16): AssetTools' extraction
// (work/dlc/MapPack1|2/files); WFC_DLC_TEXT overrides (a packaged build ships them).
std::string dlcTextFile(int pack, const std::string& lang);
// "<Package>.<Name>" -> the extracted PNG: ExtractedAssets/content first, then the DLC content root; "" if neither has it.
std::string contentImagePng(const std::string& extractedRoot, const std::string& object);

} // namespace frontend
