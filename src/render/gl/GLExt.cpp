#include "render/gl/GLExt.h"
#include "core/Log.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <cstdint>
#include <vector>

namespace glx {

#define WFC_GL_DEF(ret, name, args) PFN_##name name = nullptr;
WFC_GL_FUNCS(WFC_GL_DEF)
WFC_GL_OPT_FUNCS(WFC_GL_DEF)
#undef WFC_GL_DEF

static void* getProc(const char* n) {
#if defined(_WIN32)
    void* p = (void*)wglGetProcAddress(n);
    if (p == nullptr || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE gl = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(gl, n);
    }
    return p;
#else
    (void)n;
    return nullptr;
#endif
}

namespace {
DebugCounts gDebug;

void APIENTRY onDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* msg,
                             const void*) {
    const GLenum kError = 0x824C, kUndefined = 0x824E, kHigh = 0x9146, kMedium = 0x9147, kNotification = 0x826B;
    if (type == kError) ++gDebug.errors;
    else if (type == kUndefined) ++gDebug.undefined;
    else if (severity == kHigh) ++gDebug.high;
    else if (severity == kMedium) ++gDebug.medium;
    else ++gDebug.other;
    static const char* mode = std::getenv("WFC_GLDEBUG");
    const bool all = mode && std::strcmp(mode, "all") == 0;
    if (!all && severity == kNotification) return;
    if (!all && type != kError && type != kUndefined && severity != kHigh) return;
    // rate limit per message id: the first 5, then every 1000th
    static std::map<GLuint, unsigned> seen;
    unsigned n = ++seen[id];
    if (n > 5 && n % 1000 != 0) return;
    LOG_ERROR("GL debug [%s] type 0x%04X src 0x%04X id %u sev 0x%04X (x%u): %s",
              type == kError ? "ERROR" : type == kUndefined ? "UNDEFINED" : "HIGH", type, source, id, severity, n,
              msg ? msg : "");
}
} // namespace

void installDebugOutput() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!DebugMessageCallback) { LOG_INFO("GL debug output: not available"); return; }
    const GLenum kDebugOutput = 0x92E0, kDebugOutputSync = 0x8242;
    glEnable(kDebugOutput);
    static const char* mode = std::getenv("WFC_GLDEBUG");
    if (mode && std::strcmp(mode, "sync") == 0) glEnable(kDebugOutputSync);
    DebugMessageCallback(onDebugMessage, nullptr);
    while (glGetError() != GL_NO_ERROR) {}           // a driver without the enable token flags it: not ours
    if (mode && std::strcmp(mode, "selftest") == 0) {  // harmless GL_INVALID_ENUM: proves the callback path is live
        glEnable(0xFFFF);
        GLenum e = glGetError();
        LOG_INFO("GL debug self-test: glGetError 0x%04X, callback errors %u", e, gDebug.errors);
    }
    LOG_INFO("GL debug output: installed (reset status query %s)", GetGraphicsResetStatus ? "available" : "unavailable");
}

const DebugCounts& debugCounts() { return gDebug; }

GLenum pollResetStatus() {
    if (!GetGraphicsResetStatus) return GL_NO_ERROR;
    GLenum s = GetGraphicsResetStatus();
    static bool logged = false;
    if (s != GL_NO_ERROR && !logged) {
        logged = true;
        // GUILTY 0x8253 / INNOCENT 0x8254 / UNKNOWN 0x8255 (ARB_robustness): the context is lost; every later GL call
        // is undefined until it is recreated
        LOG_ERROR("GL context reset reported by the driver: status 0x%04X (%s)", s,
                  s == 0x8253 ? "guilty: this context caused it" : s == 0x8254 ? "innocent" : "unknown");
    }
    return s;
}

namespace {
GLuint gQ[3] = {0, 0, 0};
bool gQActive[3] = {false, false, false};
int gQi = 0;
bool gBegun = false;                 // a query was begun this frame (EndQuery only then: else GL_INVALID_OPERATION)
double gLastGpuMs = 0.0;
double gLastCpuMs = 0.0;
long gGpuReads = 0;            // readbacks so far (a new value of lastGpuFrameMs)
GLuint gTs[3][6] = {};
bool gTsSet[3][6] = {};
double gPassMs[6] = {-1, -1, -1, -1, -1, -1};      // the CPU span of the frame gLastGpuMs belongs to
// CPU time between the query's begin and end of the same frame: TIME_ELAPSED is GPU-timeline time between the two
// markers, so it includes the GPU waiting for commands the CPU had not submitted yet (driver work inside the frame).
// gpu ~ cpu span => a CPU-side stall inside the frame, not GPU load (no TDR risk); gpu >> cpu => real GPU work.
std::chrono::steady_clock::time_point gQBegin[3];
double gQCpuMs[3] = {0, 0, 0};
long gQFrame[3] = {-1, -1, -1};                     // frame index recorded in each slot
long gLastGpuIdx = -1;
std::chrono::steady_clock::time_point gCpuT0;       // this frame's gpuTimerBegin
double gCpuMark[6] = {-1, -1, -1, -1, -1, -1};
}

void gpuTimerBegin() {
    gBegun = false;
    gCpuT0 = std::chrono::steady_clock::now();
    for (double& m : gCpuMark) m = -1.0;
    gCpuMark[0] = 0.0;
    if (!GenQueries || !BeginQuery || !GetQueryObjectui64v) return;
    if (!gQ[0]) GenQueries(3, gQ);
    const GLenum kTimeElapsed = 0x88BF, kResultAvailable = 0x8867, kResult = 0x8866;
    int slot = gQi % 3;
    if (gQActive[slot]) {                       // this slot's query is two frames old: read it if ready
        GLint ready = 0;
        GetQueryObjectiv(gQ[slot], kResultAvailable, &ready);
        if (!ready) return;                      // still in flight: skip timing this frame (no stall)
        unsigned long long ns = 0;
        GetQueryObjectui64v(gQ[slot], kResult, &ns);
        gLastGpuMs = (double)ns / 1.0e6;
        ++gGpuReads;
        // pass breakdown of the same frame
        unsigned long long t[6] = {}; bool have[6] = {};
        for (int k = 0; k < 6; ++k) {
            gPassMs[k] = -1.0;
            if (!gTsSet[slot][k]) continue;
            GLint rdy = 0; GetQueryObjectiv(gTs[slot][k], kResultAvailable, &rdy);
            if (rdy) { GetQueryObjectui64v(gTs[slot][k], kResult, &t[k]); have[k] = true; }
            gTsSet[slot][k] = false;
        }
        int prev = have[0] ? 0 : -1;
        for (int k = 1; k < 6; ++k) {
            if (!have[k]) continue;
            if (prev >= 0) gPassMs[k] = (double)(t[k] - t[prev]) / 1.0e6;
            prev = k;
        }
        gLastCpuMs = gQCpuMs[slot];
        gLastGpuIdx = gQFrame[slot];
        gQActive[slot] = false;
        static int logged = 0;
        if (gLastGpuMs > 250.0 && logged++ < 50)
            LOG_WARN("GPU frame time %.1f ms, CPU %.1f ms between the same markers (%s; Windows TDR resets the driver "
                     "at ~2000 ms of GPU work)", gLastGpuMs, gQCpuMs[slot],
                     gLastGpuMs < gQCpuMs[slot] * 1.25 + 5.0 ? "GPU waiting on CPU submission" : "GPU work");
    }
    BeginQuery(kTimeElapsed, gQ[slot]);
    gQBegin[slot] = std::chrono::steady_clock::now();
    gQFrame[slot] = gQi;
    if (QueryCounter) {
        if (!gTs[0][0]) for (auto& row : gTs) GenQueries(6, row);
        QueryCounter(gTs[slot][0], 0x8E28);   // GL_TIMESTAMP: frame start
        gTsSet[slot][0] = true;
    }
    gQActive[slot] = true;
    gBegun = true;
}

void gpuTimerEnd() {
    if (EndQuery && gBegun) {
        EndQuery(0x88BF);
        const int slot = gQi % 3;
        gQCpuMs[slot] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gQBegin[slot]).count();
    }
    gBegun = false;
    ++gQi;
}

double lastGpuFrameMs() { return gLastGpuMs; }
double lastGpuFrameCpuMs() { return gLastCpuMs; }
void gpuMark(int k) {
    if (k > 0 && k < 6 && gCpuMark[k] < 0.0)
        gCpuMark[k] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gCpuT0).count();
    if (!QueryCounter || !gBegun || k <= 0 || k >= 6 || !gTs[0][0]) return;
    const int slot = gQi % 3;
    if (gTsSet[slot][k]) return;               // first mark of this kind per frame
    QueryCounter(gTs[slot][k], 0x8E28);
    gTsSet[slot][k] = true;
}
double lastGpuPassMs(int k) { return k >= 0 && k < 6 ? gPassMs[k] : -1.0; }
long gpuFrameIndex() { return gQi; }
long lastGpuFrameIndex() { return gLastGpuIdx; }
double cpuPassMark(int k) { return k >= 0 && k < 6 ? gCpuMark[k] : -1.0; }
long gpuFrameReads() { return gGpuReads; }

// ---- redundant uniform elimination (300+ fps lobbies: ~2,500 draws x 40-60 uniform calls per frame, most of them
// re-sending the frame's constants). GL keeps uniform values per program object, so a call that sets the value the
// program already holds is a no-op: skipping it cannot change any image. The Uniform* / UseProgram / LinkProgram /
// DeleteProgram entry points are wrapped once after load: the current program is tracked (UseProgram is always
// forwarded), each program's last value per location is kept as raw bits, link / delete forget a program. Calls made
// while the current program is unknown (frame boundaries: other components may bind programs through their own
// loaders) are forwarded uncached. WFC_NOUNICACHE=1 disables the layer (A/B).
namespace {
struct UEntry { uint32_t n = 0; uint32_t bits[24]; };
std::vector<std::vector<UEntry>> gUCache;   // [program][location]
GLuint gUCur = 0;
bool gUCurKnown = false;
unsigned long long gUSkipped = 0, gUSent = 0;
PFN_UseProgram realUseProgram = nullptr;
PFN_LinkProgram realLinkProgram = nullptr;
PFN_DeleteProgram realDeleteProgram = nullptr;
PFN_Uniform1i realUniform1i = nullptr;
PFN_Uniform1f realUniform1f = nullptr;
PFN_Uniform2f realUniform2f = nullptr;
PFN_Uniform2fv realUniform2fv = nullptr;
PFN_Uniform3f realUniform3f = nullptr;
PFN_Uniform4f realUniform4f = nullptr;
PFN_Uniform1iv realUniform1iv = nullptr;
PFN_Uniform3fv realUniform3fv = nullptr;
PFN_Uniform4fv realUniform4fv = nullptr;
PFN_UniformMatrix4fv realUniformMatrix4fv = nullptr;

// true when the program already holds exactly these bits at `loc` (the call can be skipped); records them otherwise
bool uSame(GLint loc, const void* data, uint32_t words) {
    if (!gUCurKnown || gUCur == 0 || loc < 0) return false;
    if (words > 24) {                               // too large to cache: forward and forget the location
        if (gUCur < gUCache.size() && (size_t)loc < gUCache[gUCur].size()) gUCache[gUCur][(size_t)loc].n = 0;
        return false;
    }
    if (gUCur >= gUCache.size()) gUCache.resize((size_t)gUCur + 64);
    std::vector<UEntry>& pc = gUCache[gUCur];
    if ((size_t)loc >= pc.size()) pc.resize((size_t)loc + 8);
    UEntry& e = pc[(size_t)loc];
    if (e.n == words && std::memcmp(e.bits, data, words * 4) == 0) { ++gUSkipped; return true; }
    e.n = words;
    std::memcpy(e.bits, data, words * 4);
    ++gUSent;
    return false;
}
unsigned long long gProgBinds = 0, gBufBytes = 0;
void APIENTRY cUseProgram(GLuint p) { gUCur = p; gUCurKnown = true; ++gProgBinds; realUseProgram(p); }
PFN_BufferData realBufferData = nullptr;
PFN_BufferSubData realBufferSubData = nullptr;
void APIENTRY cBufferData(GLenum t, GLsizeiptr n, const void* d, GLenum u) { if (d && n > 0) gBufBytes += (unsigned long long)n; realBufferData(t, n, d, u); }
void APIENTRY cBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void* d) { if (n > 0) gBufBytes += (unsigned long long)n; realBufferSubData(t, o, n, d); }
void installUploadCounters() {                      // two adds per call; always on
    if (BufferData == cBufferData) return;
    realBufferData = BufferData; BufferData = cBufferData;
    realBufferSubData = BufferSubData; BufferSubData = cBufferSubData;
}
void APIENTRY cLinkProgram(GLuint p) { if (p < gUCache.size()) gUCache[p].clear(); realLinkProgram(p); }
void APIENTRY cDeleteProgram(GLuint p) { if (p < gUCache.size()) gUCache[p].clear(); if (p == gUCur) gUCurKnown = false; realDeleteProgram(p); }
void APIENTRY cUniform1i(GLint l, GLint v) { if (!uSame(l, &v, 1)) realUniform1i(l, v); }
void APIENTRY cUniform1f(GLint l, GLfloat v) { if (!uSame(l, &v, 1)) realUniform1f(l, v); }
void APIENTRY cUniform2f(GLint l, GLfloat a, GLfloat b) { const GLfloat v[2] = {a, b}; if (!uSame(l, v, 2)) realUniform2f(l, a, b); }
void APIENTRY cUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { const GLfloat v[3] = {a, b, c}; if (!uSame(l, v, 3)) realUniform3f(l, a, b, c); }
void APIENTRY cUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { const GLfloat v[4] = {a, b, c, d}; if (!uSame(l, v, 4)) realUniform4f(l, a, b, c, d); }
void APIENTRY cUniform2fv(GLint l, GLsizei n, const GLfloat* v) { if (n < 0 || !uSame(l, v, (uint32_t)n * 2)) realUniform2fv(l, n, v); }
void APIENTRY cUniform1iv(GLint l, GLsizei n, const GLint* v) { if (n < 0 || !uSame(l, v, (uint32_t)n)) realUniform1iv(l, n, v); }
void APIENTRY cUniform3fv(GLint l, GLsizei n, const GLfloat* v) { if (n < 0 || !uSame(l, v, (uint32_t)n * 3)) realUniform3fv(l, n, v); }
void APIENTRY cUniform4fv(GLint l, GLsizei n, const GLfloat* v) { if (n < 0 || !uSame(l, v, (uint32_t)n * 4)) realUniform4fv(l, n, v); }
void APIENTRY cUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat* v) {
    if (t != GL_FALSE || n < 0 || !uSame(l, v, (uint32_t)n * 16)) realUniformMatrix4fv(l, n, t, v);
}
void installUniformCache() {
    static const bool off = std::getenv("WFC_NOUNICACHE") != nullptr;
    if (off || UseProgram == cUseProgram) return;   // disabled, or already wrapped (load() runs per renderer init)
    realUseProgram = UseProgram; UseProgram = cUseProgram;
    realLinkProgram = LinkProgram; LinkProgram = cLinkProgram;
    realDeleteProgram = DeleteProgram; DeleteProgram = cDeleteProgram;
    realUniform1i = Uniform1i; Uniform1i = cUniform1i;
    realUniform1f = Uniform1f; Uniform1f = cUniform1f;
    realUniform2f = Uniform2f; Uniform2f = cUniform2f;
    realUniform2fv = Uniform2fv; Uniform2fv = cUniform2fv;
    realUniform3f = Uniform3f; Uniform3f = cUniform3f;
    realUniform4f = Uniform4f; Uniform4f = cUniform4f;
    realUniform1iv = Uniform1iv; Uniform1iv = cUniform1iv;
    realUniform3fv = Uniform3fv; Uniform3fv = cUniform3fv;
    realUniform4fv = Uniform4fv; Uniform4fv = cUniform4fv;
    realUniformMatrix4fv = UniformMatrix4fv; UniformMatrix4fv = cUniformMatrix4fv;
    LOG_INFO("GL uniform cache: on (redundant uniform calls skipped; WFC_NOUNICACHE=1 disables)");
}
}  // namespace

void uniformCacheForgetCurrent() { gUCurKnown = false; }
unsigned long long programBinds() { return gProgBinds; }
unsigned long long bufferUploadBytes() { return gBufBytes; }
bool uniformCacheActive() { return UseProgram == cUseProgram; }
int uniformCacheGet(GLuint prog, GLint loc, void* out, unsigned words) {
    if (!uniformCacheActive() || loc < 0) return -1;
    if (prog >= gUCache.size() || (size_t)loc >= gUCache[prog].size()) return 0;
    const UEntry& e = gUCache[prog][(size_t)loc];
    if (e.n == 0) return 0;
    if (e.n != words) return -1;
    std::memcpy(out, e.bits, words * 4);
    return 1;
}
void uniformCacheStats(unsigned long long& sent, unsigned long long& skipped) { sent = gUSent; skipped = gUSkipped; gUSent = gUSkipped = 0; }

bool load() {
    bool ok = true;
#define WFC_GL_LOAD(ret, name, args) \
    name = (PFN_##name)getProc("gl" #name); \
    if (!name) { LOG_WARN("GL entry point missing: gl%s", #name); ok = false; }
    WFC_GL_FUNCS(WFC_GL_LOAD)
#undef WFC_GL_LOAD
#define WFC_GL_LOAD_OPT(ret, name, args) name = (PFN_##name)getProc("gl" #name);
    WFC_GL_OPT_FUNCS(WFC_GL_LOAD_OPT)
#undef WFC_GL_LOAD_OPT
    if (!GetGraphicsResetStatus) GetGraphicsResetStatus = (PFN_GetGraphicsResetStatus)getProc("glGetGraphicsResetStatusARB");
    if (!DebugMessageCallback) DebugMessageCallback = (PFN_DebugMessageCallback)getProc("glDebugMessageCallbackARB");
    if (ok) { installUniformCache(); installUploadCounters(); }
    return ok;
}

} // namespace glx
