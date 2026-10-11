#pragma once
#include <filesystem>
#include <string>

// File opens / existence checks of data paths (UTF-8 strings): Systems' platform::longPath (UTF-8 -> UTF-16, \\?\ only
// near MAX_PATH; short paths open exactly as before) where the tree has it (09c 60f99c6+); otherwise the plain narrow
// path, the old open - so this branch also builds alone. Use for reads under the data roots (a deep install), not for
// the files the exe writes next to itself.
#if __has_include("platform/LongPath.h")
#include "platform/LongPath.h"
namespace frontend {
inline std::filesystem::path openPath(const std::string& p) { return platform::longPath(p); }
} // namespace frontend
#else
namespace frontend {
inline std::filesystem::path openPath(const std::string& p) { return std::filesystem::path(p); }
} // namespace frontend
#endif
