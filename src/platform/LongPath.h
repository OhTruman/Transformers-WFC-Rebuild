// Clean-room reconstruction — long / non-ASCII data paths (PC ADAPTATION, Systems; docs/handoff/SYSTEMS_LONG_PATH_AUDIT.md).
#pragma once
#include <filesystem>
#include <string>

namespace platform {

// A data path ready for std::ifstream / std::ofstream / std::filesystem (file_size, exists) and _wfopen:
//   - UTF-8 -> UTF-16 (the bytes are taken as the ANSI code page instead when they are not valid UTF-8, as the narrow
//     opens did), so non-ASCII paths open;
//   - a path whose full form reaches MAX_PATH gets the \\?\ prefix (\\?\UNC\ for \\server\share) on its absolute,
//     normalised form (GetFullPathNameW): Windows 10 / 11 limit even the wide APIs to MAX_PATH without it (or a
//     longPathAware manifest plus the machine's LongPathsEnabled policy).
// Shorter paths, relative paths that stay short, already-prefixed paths and any conversion failure come back unprefixed:
// the same file as the narrow open of today. Other platforms: the path as given.
std::filesystem::path longPath(const std::string& utf8);

}  // namespace platform
