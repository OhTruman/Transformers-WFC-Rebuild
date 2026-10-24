#include "frontend/DataPaths.h"
#include "core/Log.h"
#include "frontend/OpenPath.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace frontend {

namespace {

std::string exeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf, n);
    size_t k = s.find_last_of("\\/");
    return k == std::string::npos ? std::string(".") : s.substr(0, k);
#else
    return ".";
#endif
}

std::string parentOf(std::string p, int levels) {
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    for (int i = 0; i < levels; ++i) {
        const size_t k = p.find_last_of("/\\");
        if (k == std::string::npos) return std::string();
        p = p.substr(0, k);
    }
    return p;
}

std::string findPackageRoot() {
    auto env = [](const char* k) { const char* e = std::getenv(k); return e && *e ? std::string(e) : std::string(); };
    auto isRoot = [](const std::string& r) {   // a derived root counts only when it holds AssetTools (a dev scratch tree's data
        std::error_code ec;                     // dir, e.g. <worktree>/work/gpm/data/frontend, is not 3 levels below the root)
        return !r.empty() && std::filesystem::is_directory(openPath(r + "/AssetTools"), ec);
    };
    if (std::string a = env("WFC_ASSETS"); !a.empty()) if (std::string r = parentOf(a, 2); isRoot(r)) return r;
    if (std::string c = env("WFC_CONTENT"); !c.empty()) if (std::string r = parentOf(c, 2); isRoot(r)) return r;
    if (std::string d = env("WFC_FRONTEND_DATA"); !d.empty()) if (std::string r = parentOf(d, 3); isRoot(r)) return r;
    std::string dir = exeDir();
    for (int i = 0; i < 8 && !dir.empty(); ++i) {   // the package: <root>/Rebuild/build/release/bin; a dev tree: <root>/<worktree>/...
        std::error_code ec;
        if (std::filesystem::is_directory(openPath(dir + "/AssetTools"), ec)) return dir;
        dir = parentOf(dir, 1);
    }
    return exeDir();
}

std::string resolve() {
    if (const char* e = std::getenv("WFC_FRONTEND_DATA"); e && *e) return e;
    const std::string exe = exeDir();
    const std::vector<std::string> candidates = {exe + "/../../data/frontend", exe + "/../../../data/frontend", exe + "/data/frontend"};
    for (const std::string& c : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(openPath(c + "/profile_defaults.json"), ec)) {
            const std::filesystem::path p = std::filesystem::weakly_canonical(c, ec);
            return ec ? c : p.generic_string();
        }
    }
    // a lane / dev build whose exe sits deeper than the guesses above: the build's own source data (compile-time; it does not
    // exist on a player's machine, so a package never uses it) before another tree's data - de6d4302 read the shared Rebuild
    // tree's (possibly older) strings there and three frontend tests failed in a lane build
    {
        std::error_code ec;
        const std::string src = std::string(WFC_SOURCE_DIR) + "/data/frontend";
        if (std::filesystem::is_regular_file(openPath(src + "/profile_defaults.json"), ec)) return src;
    }
    return findPackageRoot() + "/Rebuild/data/frontend";   // the package's Rebuild tree
}

} // namespace

const std::string& packageRoot() {
    static const std::string root = [] {
        std::string r = findPackageRoot();
        LOG_INFO("frontend package root: %s", r.c_str());
        return r;
    }();
    return root;
}

const std::string& frontendDataDir() {
    static const std::string dir = [] {
        std::string d = resolve();
        LOG_INFO("frontend data: %s", d.c_str());
        return d;
    }();
    return dir;
}

std::string frontendDataFile(const char* name) { return frontendDataDir() + "/" + name; }

std::string dlcContentRoot() {
    if (const char* e = std::getenv("WFC_DLC_CONTENT")) return e;
    // the AssetTools export in a workspace, else the package's ExtractedAssets/content (where the package carries the DLC files)
    const std::string dev = packageRoot() + "/AssetTools/out/dlc_content/content";
    std::error_code ec;
    return std::filesystem::is_directory(openPath(dev), ec) ? dev : packageRoot() + "/ExtractedAssets/content";
}

std::string settingsPreviewPng(const std::string& row, int value) {
    const std::string p = frontendDataFile(("previews/" + row + "_" + std::to_string(value) + ".png").c_str());
    return std::ifstream(openPath(p)).good() ? p : std::string();
}

std::string dlcTextFile(int pack, const std::string& lang) {
    const std::string root = std::getenv("WFC_DLC_TEXT") ? std::getenv("WFC_DLC_TEXT") : packageRoot() + "/AssetTools/work/dlc";
    const std::string dir = pack == 1 ? "DLCMaps" : "DLCMaps2";
    return root + "/MapPack" + std::to_string(pack) + "/files/" + dir + "/" + dir + "__TransGame." + lang;
}

std::string contentImagePng(const std::string& extractedRoot, const std::string& object) {
    const size_t dot = object.find('.');
    if (dot == std::string::npos) return {};
    const std::string rel = object.substr(0, dot) + "/" + object.substr(dot + 1) + ".png";
    const std::string base = extractedRoot + "/content/" + rel;
    if (std::ifstream(openPath(base)).good()) return base;
    const std::string dlc = dlcContentRoot() + "/" + rel;
    return std::ifstream(openPath(dlc)).good() ? dlc : base;
}

} // namespace frontend
