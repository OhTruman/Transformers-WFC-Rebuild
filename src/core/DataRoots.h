#pragma once
// The project tree for data read outside the asset / render / manifest roots (AssetTools' campaign and Escalation exports,
// ExtractedAssets files by their package-relative path). A package mirrors "F:/Transformers Rebuild" under a mapped drive letter
// (tools/package/slim_template/Play.bat: F: only when free), so a literal "F:/Transformers Rebuild/..." breaks on a PC with its own
// F: drive. The root: WFC_PROJECT_ROOT, else WFC_ASSETS when it is <root>/ExtractedAssets/VerticalSlice (the package), else this PC's
// tree (dev runs, whose WFC_ASSETS may point elsewhere).
#include "core/Config.h"

#include <cstdlib>
#include <string>

namespace core {

inline const std::string& projectRoot() {
    static const std::string r = [] {
        if (const char* e = std::getenv("WFC_PROJECT_ROOT")) if (*e) return std::string(e);
        const char* ae = std::getenv("WFC_ASSETS");
        std::string a = ae && *ae ? ae : config::kAssetRootDefault;
        for (char& c : a) if (c == '\\') c = '/';
        while (!a.empty() && a.back() == '/') a.pop_back();
        const std::string tail = "/ExtractedAssets/VerticalSlice";
        if (a.size() > tail.size() && a.compare(a.size() - tail.size(), tail.size(), tail) == 0) return a.substr(0, a.size() - tail.size());
        return std::string("F:/Transformers Rebuild");
    }();
    return r;
}
inline std::string extractedRoot() { return projectRoot() + "/ExtractedAssets/"; }   // + "content/..." (package-relative paths)
inline std::string assetToolsOut() { return projectRoot() + "/AssetTools/out/"; }

} // namespace core
