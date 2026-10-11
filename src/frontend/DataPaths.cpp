#include "frontend/DataPaths.h"
#include "core/Log.h"
#include "frontend/OpenPath.h"

#include <cstdlib>
#include <filesystem>
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

} // namespace frontend
