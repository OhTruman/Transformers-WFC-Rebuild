// Clean-room reconstruction — see platform/LongPath.h.
#include "platform/LongPath.h"

#include <windows.h>

namespace platform {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    for (UINT cp : {(UINT)CP_UTF8, (UINT)CP_ACP}) {
        const DWORD flags = cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
        const int n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) continue;
        std::wstring w((size_t)n, L'\0');
        if (MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), &w[0], n) == n) return w;
    }
    return std::wstring(s.begin(), s.end());
}

// The full-path length at which the prefix goes on: MAX_PATH (260) counts the terminating NUL; directories (CreateDirectory)
// are limited to MAX_PATH - 12. Below this the plain path is returned, so short paths open exactly as before.
constexpr size_t kPrefixAt = MAX_PATH - 12;

}  // namespace

std::filesystem::path longPath(const std::string& utf8) {
    std::wstring w = widen(utf8);
    if (w.size() >= 4 && (w.compare(0, 4, L"\\\\?\\") == 0 || w.compare(0, 4, L"\\\\.\\") == 0)) return std::filesystem::path(w);
    DWORD n = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
    if (n == 0) return std::filesystem::path(w);
    std::wstring full((size_t)n, L'\0');
    n = GetFullPathNameW(w.c_str(), n, &full[0], nullptr);
    if (n == 0 || n >= full.size()) return std::filesystem::path(w);
    full.resize(n);
    if (full.size() < kPrefixAt) return std::filesystem::path(w);   // short: unchanged (relative stays relative)
    if (full.size() >= 2 && full[0] == L'\\' && full[1] == L'\\') return std::filesystem::path(L"\\\\?\\UNC\\" + full.substr(2));
    return std::filesystem::path(L"\\\\?\\" + full);
}

}  // namespace platform
