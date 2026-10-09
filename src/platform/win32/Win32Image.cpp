// Clean-room reconstruction — Win32 image decoder (GDI+) for PNG textures.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include "platform/Image.h"
#include "core/Log.h"

#include <cstdlib>
#include <mutex>

namespace platform {
namespace {

ULONG_PTR g_gdiplusToken = 0;
std::once_flag g_initFlag;

void ensureGdiplus() {
    std::call_once(g_initFlag, [] {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &in, nullptr);
    });
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

} // namespace

ImageFallback g_fallback = nullptr;

void setImageFallback(ImageFallback f) { g_fallback = f; }

bool decodeImage(const std::string& path, render::ImageData& out) {
    std::wstring wpath = widen(path);
    static const bool ddsFirst = std::getenv("WFC_DDSFIRST") != nullptr;
    static const bool pngLog = std::getenv("WFC_PNGLOG") != nullptr;
    if (g_fallback && (ddsFirst || GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) && g_fallback(path, out))
        return true;
    if (pngLog) LOG_INFO("PNGLOG opened %s", path.c_str());
    ensureGdiplus();
    Gdiplus::Bitmap bmp(wpath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        LOG_WARN("image: decode failed %s", path.c_str());
        return false;
    }
    int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
    if (w <= 0 || h <= 0) return false;

    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData bd;
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
        return false;

    out.w = w; out.h = h;
    out.rgba.resize((size_t)w * h * 4);
    const uint8_t* base = reinterpret_cast<const uint8_t*>(bd.Scan0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = base + (ptrdiff_t)y * bd.Stride;   // top row first
        uint8_t* dst = out.rgba.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            // GDI+ 32bppARGB is BGRA in memory -> RGBA.
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    bmp.UnlockBits(&bd);
    return true;
}

} // namespace platform
#endif
