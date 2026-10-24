// core::config::assetRootDefault (Config.h): the asset root when WFC_ASSETS is unset. A package keeps the dev tree's layout
// (tools/package/build_slim.py: <root>/Rebuild/build/release/bin/wfc_rebuild.exe beside <root>/ExtractedAssets/VerticalSlice) under
// any folder or drive letter, so the root is derived from the exe's folder when that layout is there; otherwise (a scratch build
// elsewhere, tests) this PC's tree - the value this constant always had.
#include "core/Config.h"

#include <string>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace core::config {

namespace {
std::string exeDir() {
#if defined(_WIN32)
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
    std::string p((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, &p[0], len, nullptr, nullptr);
    for (char& c : p) if (c == '\\') c = '/';
    const size_t s = p.rfind('/');
    return s == std::string::npos ? std::string() : p.substr(0, s);
#else
    return {};
#endif
}
bool isDir(const std::string& p) {
#if defined(_WIN32)
    const int wl = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)wl, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &w[0], wl);
    const DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}
} // namespace

const char* assetRootDefault() {
    static const std::string root = [] {
        std::string d = exeDir();   // <root>/Rebuild/build/release/bin
        const char* tail = "/Rebuild/build/release/bin";
        const size_t tl = std::char_traits<char>::length(tail);
        if (d.size() > tl && d.compare(d.size() - tl, tl, tail) == 0) {
            const std::string vs = d.substr(0, d.size() - tl) + "/ExtractedAssets/VerticalSlice";
            if (isDir(vs)) return vs;
        }
        return std::string("F:/Transformers Rebuild/ExtractedAssets/VerticalSlice");
    }();
    return root.c_str();
}

} // namespace core::config
