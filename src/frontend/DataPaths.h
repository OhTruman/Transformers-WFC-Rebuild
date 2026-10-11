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

} // namespace frontend
