// Clean-room reconstruction — Win32 image decoder for PNG textures: GDI+ (the reference) and WIC (Windows Imaging Component).
// [Systems, load-time pass] WIC decodes only the PNG classes (colour type / bit depth / colour chunks) that the WFC_IMAGECMPTEST tool
// proved byte-identical to GDI+ over every shipped PNG (kWicClasses); every other file stays on GDI+, so the pixels never change.
// prefetchImages() decodes a batch on worker threads ahead of the main thread's decodeImage calls (uploads stay on the main thread).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wincodec.h>

#include "platform/Image.h"
#include "platform/LongPath.h"
#include "core/Log.h"

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

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

bool decodeGdiplus(const std::wstring& wpath, const std::string& path, render::ImageData& out) {
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

// CLSID_WICImagingFactory {cacaf262-9370-4615-a13b-9f5539da4c0a}; GUID_WICPixelFormat32bppRGBA {f5c7ad2d-6a8d-43dd-a7a8-a29935261ae9}
// (defined here: no import library for the GUIDs)
const GUID kClsidWicFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
const GUID kWicRgba = {0xf5c7ad2d, 0x6a8d, 0x43dd, {0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9}};

template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool decodeWic(const std::wstring& wpath, const std::string& path, render::ImageData& out) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // S_FALSE / RPC_E_CHANGED_MODE: this thread already has COM
    IWICImagingFactory* f = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICBitmapSource* rgba = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(kClsidWicFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), (void**)&f));
    ok = ok && SUCCEEDED(f->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec));
    ok = ok && SUCCEEDED(dec->GetFrame(0, &fr));
    ok = ok && SUCCEEDED(WICConvertBitmapSource(kWicRgba, fr, &rgba));
    UINT w = 0, h = 0;
    ok = ok && SUCCEEDED(rgba->GetSize(&w, &h)) && w > 0 && h > 0;
    if (ok) {
        out.w = (int)w; out.h = (int)h;
        out.rgba.resize((size_t)w * h * 4);
        ok = SUCCEEDED(rgba->CopyPixels(nullptr, w * 4, (UINT)out.rgba.size(), out.rgba.data()));
    }
    rel(rgba); rel(fr); rel(dec); rel(f);
    if (SUCCEEDED(co)) CoUninitialize();
    if (!ok) LOG_WARN("image: WIC decode failed %s", path.c_str());
    return ok;
}

// The WIC classes proven byte-identical to GDI+ (WFC_IMAGECMPTEST over the shipped base / HD / DLC PNGs; see the patch note).
const char* const kWicClasses[] = {"ct0-8", "ct2-8", "ct6-8", nullptr};   // 60,458 / 60,458 identical (base 53,668 + DLC 6,790; no gAMA / sRGB / iCCP in any)

bool wicAllowed(const std::string& cls) {
    for (const char* const* c = kWicClasses; *c; ++c) if (cls == *c) return true;
    return false;
}

// ---- the prefetch pool ----
struct Pending { bool started = false, done = false, ok = false, orphan = false; render::ImageData img; };   // orphan: dropped in flight
std::mutex g_poolMx;
std::condition_variable g_poolCv, g_doneCv;
std::deque<std::string> g_queue;
std::map<std::string, Pending> g_pending;
std::vector<std::thread> g_workers;
bool decodeNow(const std::string& path, render::ImageData& out);

void workerLoop() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lk(g_poolMx);
            g_poolCv.wait(lk, [] { return !g_queue.empty(); });
            path = g_queue.front(); g_queue.pop_front();
            auto it = g_pending.find(path);
            if (it == g_pending.end() || it->second.started) continue;   // taken by the main thread meanwhile
            it->second.started = true;
        }
        render::ImageData img;
        const bool ok = decodeNow(path, img);
        {
            std::lock_guard<std::mutex> lk(g_poolMx);
            auto it = g_pending.find(path);
            if (it != g_pending.end() && it->second.orphan) g_pending.erase(it);   // dropPrefetched while decoding: nobody collects it
            else if (it != g_pending.end()) { it->second.img = std::move(img); it->second.ok = ok; it->second.done = true; }
        }
        g_doneCv.notify_all();
    }
}

int poolThreads() {
    static const int n = [] {
        const char* e = std::getenv("WFC_IMAGETHREADS");   // 0 = no prefetch (the A/B arm)
        if (e) return std::max(0, std::atoi(e));
        const unsigned hw = std::thread::hardware_concurrency();
        return (int)std::max(1u, std::min(6u, hw > 2 ? hw - 2 : 1u));
    }();
    return n;
}

} // namespace

ImageFallback g_fallback = nullptr;

void setImageFallback(ImageFallback f) { g_fallback = f; }

std::string pngClass(const std::string& path) {
    std::ifstream f(platform::longPath(path), std::ios::binary);
    unsigned char sig[8];
    if (!f.read((char*)sig, 8) || std::memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) return "notpng";
    std::string cls, flags;
    for (int guard = 0; guard < 64; ++guard) {
        unsigned char hdr[8];
        if (!f.read((char*)hdr, 8)) break;
        const uint32_t len = (uint32_t)hdr[0] << 24 | (uint32_t)hdr[1] << 16 | (uint32_t)hdr[2] << 8 | hdr[3];
        const std::string type((const char*)hdr + 4, 4);
        if (type == "IHDR" && len >= 13) {
            unsigned char ih[13];
            if (!f.read((char*)ih, 13)) break;
            cls = "ct" + std::to_string((int)ih[9]) + "-" + std::to_string((int)ih[8]) + (ih[12] ? "-i" : "");   // colour type, depth, interlace
            f.seekg((std::streamoff)(len - 13 + 4), std::ios::cur);
            continue;
        }
        if (type == "IDAT" || type == "IEND") break;
        if (type == "gAMA" || type == "sRGB" || type == "iCCP" || type == "cHRM" || type == "tRNS" || type == "sBIT") flags += "+" + type;
        f.seekg((std::streamoff)len + 4, std::ios::cur);
    }
    return cls.empty() ? "badpng" : cls + flags;
}

bool decodeImageWith(int decoder, const std::string& path, render::ImageData& out) {
    const std::wstring wpath = platform::longPath(path).wstring();
    return decoder == 1 ? decodeWic(wpath, path, out) : decodeGdiplus(wpath, path, out);
}

namespace {
bool decodeNow(const std::string& path, render::ImageData& out) {
    const std::wstring wpath = platform::longPath(path).wstring();   // [Systems] long / non-ASCII paths (GDI+ takes \\?\ paths)
    static const bool ddsFirst = std::getenv("WFC_DDSFIRST") != nullptr;
    static const bool pngLog = std::getenv("WFC_PNGLOG") != nullptr;
    static const int forced = [] { const char* e = std::getenv("WFC_IMAGEDECODER"); return !e ? -1 : std::strcmp(e, "wic") == 0 ? 1 : 0; }();
    if (g_fallback && (ddsFirst || GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) && g_fallback(path, out))
        return true;
    if (pngLog) LOG_INFO("PNGLOG opened %s", path.c_str());
    const bool wic = forced >= 0 ? forced == 1 : wicAllowed(pngClass(path));
    return wic ? decodeWic(wpath, path, out) : decodeGdiplus(wpath, path, out);
}
} // namespace

bool decodeImage(const std::string& path, render::ImageData& out) {
    if (poolThreads() > 0) {
        std::unique_lock<std::mutex> lk(g_poolMx);
        auto it = g_pending.find(path);
        if (it != g_pending.end()) {
            if (!it->second.started) { g_pending.erase(it); lk.unlock(); return decodeNow(path, out); }   // not begun: decode here
            g_doneCv.wait(lk, [&] { auto j = g_pending.find(path); return j == g_pending.end() || j->second.done; });
            auto j = g_pending.find(path);
            if (j != g_pending.end()) {
                const bool ok = j->second.ok;
                out = std::move(j->second.img);
                g_pending.erase(j);
                return ok;
            }
        }
    }
    return decodeNow(path, out);
}

void prefetchImages(const std::vector<std::string>& paths) {
    const int n = poolThreads();
    if (n <= 0) return;
    {
        std::lock_guard<std::mutex> lk(g_poolMx);
        while ((int)g_workers.size() < n) { g_workers.emplace_back(workerLoop); g_workers.back().detach(); }
        for (const std::string& p : paths) {
            if (p.empty() || g_pending.count(p)) continue;
            g_pending[p];
            g_queue.push_back(p);
        }
    }
    g_poolCv.notify_all();
}

void dropPrefetched() {
    std::unique_lock<std::mutex> lk(g_poolMx);
    g_queue.clear();
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->second.started && !it->second.done) { it->second.orphan = true; ++it; }   // the worker drops it when it finishes
        else it = g_pending.erase(it);
    }
}

} // namespace platform
#endif
