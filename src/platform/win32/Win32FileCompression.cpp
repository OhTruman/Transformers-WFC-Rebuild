// Clean-room reconstruction — Win32 lossless file compression (Compression API, XPRESS_HUFF), loaded at run time
// from cabinet.dll (Windows 8+) so nothing extra is linked.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/FileCompression.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace platform {
namespace {

// compressapi.h, declared here for the run-time binding
typedef void* CompressorHandle;
constexpr DWORD kXpressHuff = 4;   // COMPRESS_ALGORITHM_XPRESS_HUFF
typedef BOOL(WINAPI* PFN_CreateCompressor)(DWORD, void*, CompressorHandle*);
typedef BOOL(WINAPI* PFN_Compress)(CompressorHandle, const void*, SIZE_T, void*, SIZE_T, SIZE_T*);
typedef BOOL(WINAPI* PFN_CloseCompressor)(CompressorHandle);
typedef BOOL(WINAPI* PFN_CreateDecompressor)(DWORD, void*, CompressorHandle*);
typedef BOOL(WINAPI* PFN_Decompress)(CompressorHandle, const void*, SIZE_T, void*, SIZE_T, SIZE_T*);
typedef BOOL(WINAPI* PFN_CloseDecompressor)(CompressorHandle);

struct Api {
    PFN_CreateCompressor createC = nullptr; PFN_Compress compress = nullptr; PFN_CloseCompressor closeC = nullptr;
    PFN_CreateDecompressor createD = nullptr; PFN_Decompress decompress = nullptr; PFN_CloseDecompressor closeD = nullptr;
    bool ok = false;
};
const Api& api() {
    static const Api a = [] {
        Api r;
        HMODULE m = LoadLibraryW(L"cabinet.dll");
        if (!m) return r;
        r.createC = (PFN_CreateCompressor)(void*)GetProcAddress(m, "CreateCompressor");
        r.compress = (PFN_Compress)(void*)GetProcAddress(m, "Compress");
        r.closeC = (PFN_CloseCompressor)(void*)GetProcAddress(m, "CloseCompressor");
        r.createD = (PFN_CreateDecompressor)(void*)GetProcAddress(m, "CreateDecompressor");
        r.decompress = (PFN_Decompress)(void*)GetProcAddress(m, "Decompress");
        r.closeD = (PFN_CloseDecompressor)(void*)GetProcAddress(m, "CloseDecompressor");
        r.ok = r.createC && r.compress && r.closeC && r.createD && r.decompress && r.closeD;
        return r;
    }();
    return a;
}

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    if (n <= 0) return false;
    out.resize((size_t)n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return (bool)f;
}

}  // namespace

bool compressBuffer(const uint8_t* data, size_t n, std::vector<uint8_t>& out) {
    const Api& a = api();
    if (!a.ok) return false;
    CompressorHandle h = nullptr;
    if (!a.createC(kXpressHuff, nullptr, &h)) return false;
    SIZE_T need = 0;
    a.compress(h, data, n, nullptr, 0, &need);         // query the size (fails with ERROR_INSUFFICIENT_BUFFER)
    out.resize(need ? need : n + 1024);
    SIZE_T got = 0;
    const bool ok = a.compress(h, data, n, out.data(), out.size(), &got) != FALSE;
    a.closeC(h);
    if (!ok) return false;
    out.resize(got);
    return true;
}

bool decompressBuffer(const uint8_t* data, size_t n, std::vector<uint8_t>& out) {
    const Api& a = api();
    if (!a.ok) return false;
    CompressorHandle h = nullptr;
    if (!a.createD(kXpressHuff, nullptr, &h)) return false;
    SIZE_T need = 0;
    a.decompress(h, data, n, nullptr, 0, &need);       // the buffer format carries the original size
    out.resize(need);
    SIZE_T got = 0;
    const bool ok = need > 0 && a.decompress(h, data, n, out.data(), out.size(), &got) != FALSE && got == need;
    a.closeD(h);
    if (!ok) out.clear();
    return ok;
}

long long dataFileSize(const std::string& path) {
    for (const std::string& p : {path, path + kCompressedSuffix}) {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (f) return (long long)f.tellg();
    }
    return -1;
}

bool readFileMaybeCompressed(const std::string& path, std::vector<uint8_t>& out) {
    if (readAll(path, out)) return true;
    std::vector<uint8_t> packed;
    if (!readAll(path + kCompressedSuffix, packed)) return false;
    const bool ok = decompressBuffer(packed.data(), packed.size(), out);
    static const bool log = std::getenv("WFC_FILELOG") != nullptr;
    if (log) std::fprintf(stderr, "FILELOG %s%s: %zu -> %zu bytes%s\n", path.c_str(), kCompressedSuffix, packed.size(),
                          out.size(), ok ? "" : " (CORRUPT)");
    return ok;
}

} // namespace platform
#endif
