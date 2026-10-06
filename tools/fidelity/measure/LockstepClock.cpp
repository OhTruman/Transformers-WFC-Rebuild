// WFC fidelity measurement build: lockstep clock + frame grabber.
//
// Linked into wfc_rebuild_observe INSTEAD of src/core/Time.cpp (every other product source is
// unmodified). core::nowSeconds() is the exe's only wall clock (Application's frame loop calls it
// once per frame), so returning n / WFC_FIXEDHZ (default 60) on the n-th call makes every frame
// exactly one fixed simulation step: the real game becomes deterministic and frame N is sim time
// (N-1)/hz regardless of machine speed. (A ~1e-9 s bias per frame keeps the FixedStepClock from
// dropping a step to rounding.) The renderer's own shader clock (uTime: animated signs, scrolling
// textures) still uses steady_clock - those pixels are not deterministic.
//
// Frame grabber: WFC_GRAB=<list> with WFC_GRAB_DIR=<dir> saves the presented frame as
// <dir>/f<NNNNN>.bmp for each listed frame. <list> is comma separated: single frames ("120") or
// ranges with a stride ("60:180:5"). The grab happens at the start of the next frame, from the
// front buffer of the current GL window (the frame the player saw). The final frame of a
// WFC_SMOKE_FRAMES run is not grabbed here (use WFC_SHOT for it).
//
// WFC_DEBUGSTATE=<file>: one line per presented frame "<frame> <DebugFlags.enabled> <WFC_DEBUGCAM set>"
// (exact record of whether the debug overlay / debug beacon was drawn).
#include "core/Time.h"
#include "core/Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif

namespace {

std::set<long> parseGrab() {
    std::set<long> out;
    const char* e = std::getenv("WFC_GRAB");
    if (!e) return out;
    std::string s(e);
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        std::string tok = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        long a = 0, b = 0, st = 1;
        int n = std::sscanf(tok.c_str(), "%ld:%ld:%ld", &a, &b, &st);
        if (n == 1) out.insert(a);
        else if (n >= 2) for (long f = a; f <= b; f += (st > 0 ? st : 1)) out.insert(f);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

void writeBmp(const char* path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    int rowPad = (4 - (w * 3) % 4) % 4;
    uint32_t imgSize = (uint32_t)((w * 3 + rowPad) * h), fileSize = 54 + imgSize, dataOff = 54, dib = 40;
    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4); std::memcpy(hdr + 10, &dataOff, 4); std::memcpy(hdr + 14, &dib, 4);
    std::memcpy(hdr + 18, &w, 4); std::memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2); std::memcpy(hdr + 28, &bpp, 2); std::memcpy(hdr + 34, &imgSize, 4);
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row((size_t)w * 3 + rowPad, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = rgb.data() + (size_t)y * w * 3;
        for (int x = 0; x < w; ++x) { row[(size_t)x * 3] = src[x * 3 + 2]; row[(size_t)x * 3 + 1] = src[x * 3 + 1]; row[(size_t)x * 3 + 2] = src[x * 3]; }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
}

void grabFront(long frame) {
    static const char* dir = std::getenv("WFC_GRAB_DIR");
    HDC dc = wglGetCurrentDC();
    if (!dc || !wglGetCurrentContext()) return;
    RECT rc;
    if (!GetClientRect(WindowFromDC(dc), &rc)) return;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    using BindFb = void(APIENTRY*)(GLenum, GLuint);
    using BindBuf = void(APIENTRY*)(GLenum, GLuint);
    static BindFb bindFb = (BindFb)wglGetProcAddress("glBindFramebuffer");
    static BindBuf bindBuf = (BindBuf)wglGetProcAddress("glBindBuffer");
    GLint prevRead = 0;
    glGetIntegerv(GL_READ_BUFFER, &prevRead);
    if (bindFb) bindFb(GL_READ_FRAMEBUFFER, 0);
    if (bindBuf) bindBuf(GL_PIXEL_PACK_BUFFER, 0);
    glReadBuffer(GL_FRONT);
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glReadBuffer((GLenum)prevRead);
    char path[1024];
    std::snprintf(path, sizeof path, "%s/f%05ld.bmp", dir && *dir ? dir : ".", frame);
    writeBmp(path, w, h, rgb);
}

} // namespace

// Optional per-frame hook (defined by measure/CallCounter.cpp in wfc_rebuild_count).
extern "C" void fidFrameBoundary(long presentedFrame) __attribute__((weak));

namespace core {
double nowSeconds() {
    static const double hz = [] { const char* e = std::getenv("WFC_FIXEDHZ"); double v = e ? std::atof(e) : 60.0; return v > 1 ? v : 60.0; }();
    static const std::set<long> grab = parseGrab();
    static long calls = 0;
    ++calls;
    // Call 1 happens before the loop; call c (c >= 2) starts frame c-1, so frame c-2 was just presented.
    if (!grab.empty() && calls >= 3 && grab.count(calls - 2)) grabFront(calls - 2);
    if (fidFrameBoundary && calls >= 3) fidFrameBoundary(calls - 2);
    // Debug-overlay state of the frame just presented (World::draw draws wire boxes/lines when on).
    static std::FILE* dbg = [] { const char* e = std::getenv("WFC_DEBUGSTATE"); return e && *e ? std::fopen(e, "wb") : (std::FILE*)nullptr; }();
    if (dbg && calls >= 3) {
        std::fprintf(dbg, "%ld %d %d\n", calls - 2, (int)core::DebugFlags::get().enabled, std::getenv("WFC_DEBUGCAM") ? 1 : 0);
        std::fflush(dbg);
    }
    return (double)calls / hz + (double)calls * 1e-9;
}
} // namespace core
