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
    return std::string(WFC_SOURCE_DIR) + "/data/frontend";
}

} // namespace

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
    return "F:/Transformers Rebuild/AssetTools/out/dlc_content/content";
}

std::string settingsPreviewPng(const std::string& row, int value) {
    const std::string p = frontendDataFile(("previews/" + row + "_" + std::to_string(value) + ".png").c_str());
    return std::ifstream(openPath(p)).good() ? p : std::string();
}

std::string dlcTextFile(int pack, const std::string& lang) {
    const std::string root = std::getenv("WFC_DLC_TEXT") ? std::getenv("WFC_DLC_TEXT") : "F:/Transformers Rebuild/AssetTools/work/dlc";
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
