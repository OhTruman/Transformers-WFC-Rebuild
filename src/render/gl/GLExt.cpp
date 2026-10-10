#define WFC_NO_TEXCACHE_MACROS   // this file implements the cache over the real entry points
#include "render/gl/GLExt.h"
#include <algorithm>
#include <unordered_map>
#include <string>
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
std::vector<char> gProgBound;                         // program bound since its last link (first-bind counter)
unsigned long long gFirstBinds = 0, gBufAllocs = 0, gBufAllocBytes = 0;
void APIENTRY cUseProgram(GLuint p) {
    gUCur = p; gUCurKnown = true; ++gProgBinds;
    if (p) {
        if (p >= gProgBound.size()) gProgBound.resize((size_t)p + 256, 0);
        if (!gProgBound[p]) { gProgBound[p] = 1; ++gFirstBinds; }
    }
    realUseProgram(p);
}
PFN_BufferData realBufferData = nullptr;
PFN_BufferSubData realBufferSubData = nullptr;
// WFC_UPLOADPROF=1: buffer upload bytes by calling code address (logged as exe+0x..., tools/render/gltrace_sym.py)
const bool gUpProf = std::getenv("WFC_UPLOADPROF") != nullptr;
std::unordered_map<void*, unsigned long long> gUpBytes;
void APIENTRY cBufferData(GLenum t, GLsizeiptr n, const void* d, GLenum u) {
    if (d && n > 0) { gBufBytes += (unsigned long long)n; if (gUpProf) gUpBytes[__builtin_return_address(0)] += (unsigned long long)n; }
    if (n > 0) { ++gBufAllocs; gBufAllocBytes += (unsigned long long)n; }   // (new storage for the buffer)
    realBufferData(t, n, d, u);
}
void APIENTRY cBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void* d) {
    if (n > 0) { gBufBytes += (unsigned long long)n; if (gUpProf) gUpBytes[__builtin_return_address(0)] += (unsigned long long)n; }
    realBufferSubData(t, o, n, d);
}
// ---- texture-bind cache ----
constexpr int kTexUnits = 32;
GLuint gTexBound[kTexUnits][3] = {};
bool gTexKnown[kTexUnits][3] = {};
int gActiveUnit = -1;                                 // -1 = unknown
unsigned long long gTexIssued = 0, gTexSkipped = 0;
PFN_ActiveTexture realActiveTexture = nullptr;
int texSlot(GLenum t) { return t == GL_TEXTURE_2D ? 0 : t == 0x8513 /*CUBE_MAP*/ ? 1 : t == 0x8C1A /*2D_ARRAY*/ ? 2 : -1; }
bool texCacheOff() { static const bool off = std::getenv("WFC_NOTEXCACHE") != nullptr; return off; }
void APIENTRY cActiveTexture(GLenum unit) {
    const int u = (int)unit - (int)GL_TEXTURE0;
    if (!texCacheOff() && u == gActiveUnit && u >= 0) return;
    gActiveUnit = (u >= 0 && u < kTexUnits) ? u : -1;
    realActiveTexture(unit);
}

void installUploadCounters() {                      // two adds per call; always on
    if (BufferData == cBufferData) return;
    realBufferData = BufferData; BufferData = cBufferData;
    realBufferSubData = BufferSubData; BufferSubData = cBufferSubData;
    if (ActiveTexture != cActiveTexture) { realActiveTexture = ActiveTexture; ActiveTexture = cActiveTexture; }
}
// uniform locations by program + literal name (cachedUniformLocation): forgotten with the program's uniform values
std::vector<std::vector<std::pair<const char*, GLint>>> gLocCache;
void forgetProgram(GLuint p) {
    if (p < gUCache.size()) gUCache[p].clear();
    if (p < gLocCache.size()) gLocCache[p].clear();
    if (p < gProgBound.size()) gProgBound[p] = 0;
}
void APIENTRY cLinkProgram(GLuint p) { forgetProgram(p); realLinkProgram(p); }
void APIENTRY cDeleteProgram(GLuint p) { forgetProgram(p); if (p == gUCur) gUCurKnown = false; realDeleteProgram(p); }
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
// ---- WFC_GLTRACE / WFC_TEXTRACE: every GL buffer / VAO / program / framebuffer / renderbuffer / query created through
// these entry points records its caller (return address, module-relative: symbolise with wfc_rebuild.map,
// tools/render/gltrace_sym.py); deletes drop it. textureTraceDump logs live counts per kind and the sites that grew.
namespace gltrace {
bool on() {
    static const bool v = std::getenv("WFC_GLTRACE") != nullptr || std::getenv("WFC_TEXTRACE") != nullptr;
    return v;
}
std::map<GLuint, uintptr_t> live[6];                  // buffers, vaos, programs, framebuffers, renderbuffers, queries
const char* kKind[6] = {"buffers", "vaos", "programs", "framebuffers", "renderbuffers", "queries"};
uintptr_t rel(void* ra) {
    static const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    return (uintptr_t)ra - base;
}
void add(int k, GLsizei n, const GLuint* ids, void* ra) { for (GLsizei i = 0; i < n; ++i) live[k][ids[i]] = rel(ra); }
void del(int k, GLsizei n, const GLuint* ids) { for (GLsizei i = 0; i < n; ++i) live[k].erase(ids[i]); }
PFN_GenBuffers rGenBuffers; PFN_DeleteBuffers rDeleteBuffers; PFN_GenVertexArrays rGenVertexArrays;
PFN_DeleteVertexArrays rDeleteVertexArrays; PFN_CreateProgram rCreateProgram; PFN_DeleteProgram rDeleteProgram;
PFN_GenFramebuffers rGenFramebuffers; PFN_DeleteFramebuffers rDeleteFramebuffers; PFN_GenRenderbuffers rGenRenderbuffers;
PFN_DeleteRenderbuffers rDeleteRenderbuffers; PFN_GenQueries rGenQueries; PFN_DeleteQueries rDeleteQueries;
void APIENTRY tGenBuffers(GLsizei n, GLuint* p) { rGenBuffers(n, p); add(0, n, p, __builtin_return_address(0)); }
void APIENTRY tDeleteBuffers(GLsizei n, const GLuint* p) { del(0, n, p); rDeleteBuffers(n, p); }
void APIENTRY tGenVertexArrays(GLsizei n, GLuint* p) { rGenVertexArrays(n, p); add(1, n, p, __builtin_return_address(0)); }
void APIENTRY tDeleteVertexArrays(GLsizei n, const GLuint* p) { del(1, n, p); rDeleteVertexArrays(n, p); }
GLuint APIENTRY tCreateProgram() { GLuint p = rCreateProgram(); add(2, 1, &p, __builtin_return_address(0)); return p; }
void APIENTRY tDeleteProgram(GLuint p) { del(2, 1, &p); rDeleteProgram(p); }
void APIENTRY tGenFramebuffers(GLsizei n, GLuint* p) { rGenFramebuffers(n, p); add(3, n, p, __builtin_return_address(0)); }
void APIENTRY tDeleteFramebuffers(GLsizei n, const GLuint* p) { del(3, n, p); rDeleteFramebuffers(n, p); }
void APIENTRY tGenRenderbuffers(GLsizei n, GLuint* p) { rGenRenderbuffers(n, p); add(4, n, p, __builtin_return_address(0)); }
void APIENTRY tDeleteRenderbuffers(GLsizei n, const GLuint* p) { del(4, n, p); rDeleteRenderbuffers(n, p); }
void APIENTRY tGenQueries(GLsizei n, GLuint* p) { rGenQueries(n, p); add(5, n, p, __builtin_return_address(0)); }
void APIENTRY tDeleteQueries(GLsizei n, const GLuint* p) { del(5, n, p); rDeleteQueries(n, p); }
void install() {
    if (!on() || GenBuffers == tGenBuffers) return;
    rGenBuffers = GenBuffers; GenBuffers = tGenBuffers; rDeleteBuffers = DeleteBuffers; DeleteBuffers = tDeleteBuffers;
    rGenVertexArrays = GenVertexArrays; GenVertexArrays = tGenVertexArrays;
    rDeleteVertexArrays = DeleteVertexArrays; DeleteVertexArrays = tDeleteVertexArrays;
    rCreateProgram = CreateProgram; CreateProgram = tCreateProgram; rDeleteProgram = DeleteProgram; DeleteProgram = tDeleteProgram;
    rGenFramebuffers = GenFramebuffers; GenFramebuffers = tGenFramebuffers;
    rDeleteFramebuffers = DeleteFramebuffers; DeleteFramebuffers = tDeleteFramebuffers;
    rGenRenderbuffers = GenRenderbuffers; GenRenderbuffers = tGenRenderbuffers;
    rDeleteRenderbuffers = DeleteRenderbuffers; DeleteRenderbuffers = tDeleteRenderbuffers;
    if (GenQueries && DeleteQueries) { rGenQueries = GenQueries; GenQueries = tGenQueries; rDeleteQueries = DeleteQueries; DeleteQueries = tDeleteQueries; }
    LOG_INFO("GL trace: on (buffers, VAOs, programs, framebuffers, renderbuffers, queries, textures)");
}
}  // namespace gltrace

void installUniformCache() {
    static const bool off = std::getenv("WFC_NOUNICACHE") != nullptr;
    if (LinkProgram != cLinkProgram) {                // always: the location cache must forget relinked / deleted programs
        realLinkProgram = LinkProgram; LinkProgram = cLinkProgram;
        realDeleteProgram = DeleteProgram; DeleteProgram = cDeleteProgram;
    }
    if (off || UseProgram == cUseProgram) return;   // disabled, or already wrapped (load() runs per renderer init)
    realUseProgram = UseProgram; UseProgram = cUseProgram;
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

// A uniform location by program and a NAME WITH STATIC STORAGE (string literal: the pointer is the key), cached until the
// program is relinked or deleted: per-frame / per-robot passes looked names up in the driver every time
// (~35 per projected shadow). WFC_NOLOCCACHE=1 = the driver every time (A/B).
void uploadProfDump(long frames) {
    if (!gUpProf || frames <= 0) return;
    static const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    std::vector<std::pair<unsigned long long, void*>> v;
    unsigned long long total = 0;
    for (const auto& kv : gUpBytes) { v.push_back({kv.second, kv.first}); total += kv.second; }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    LOG_INFO("UPLOADPROF %ld frames: %.1f KB / frame buffer uploads, top callers:", frames, (double)total / frames / 1024.0);
    for (size_t i = 0; i < v.size() && i < 12; ++i)
        LOG_INFO("UPLOADPROF   %8.1f KB / frame  exe+0x%llx", (double)v[i].first / frames / 1024.0,
                 (unsigned long long)((uintptr_t)v[i].second - base));
    gUpBytes.clear();
}

bool locCheckOn() { static const bool on = std::getenv("WFC_LOCCHECK") != nullptr; return on; }
long gLocChecks = 0, gLocMismatch = 0;
void locCheckCount(bool same) { ++gLocChecks; if (!same) ++gLocMismatch;
    if ((gLocChecks & 0xFFFFF) == 0) LOG_INFO("LOCCHECK %ld cached uniform locations checked against the driver: %ld different", gLocChecks, gLocMismatch); }

GLint cachedUniformLocation(GLuint p, const char* name) {
    static const bool off = std::getenv("WFC_NOLOCCACHE") != nullptr;
    if (off || LinkProgram != cLinkProgram) return GetUniformLocation(p, name);
    if (p >= gLocCache.size()) gLocCache.resize((size_t)p + 64);
    std::vector<std::pair<const char*, GLint>>& v = gLocCache[p];
    for (const auto& e : v) if (e.first == name) {
        if (locCheckOn()) locCheckCount(GetUniformLocation(p, name) == e.second);
        return e.second;
    }
    const GLint l = GetUniformLocation(p, name);
    v.emplace_back(name, l);
    return l;
}

void uniformCacheForgetCurrent() { gUCurKnown = false; }
unsigned long long programBinds() { return gProgBinds; }
void cachedBindTexture(GLenum target, GLuint texture) {
    const int s = texSlot(target);
    if (s >= 0 && gActiveUnit >= 0 && !texCacheOff() && gTexKnown[gActiveUnit][s] && gTexBound[gActiveUnit][s] == texture) {
        ++gTexSkipped;
        return;
    }
    ::glBindTexture(target, texture);
    ++gTexIssued;
    if (s >= 0 && gActiveUnit >= 0) { gTexBound[gActiveUnit][s] = texture; gTexKnown[gActiveUnit][s] = true; }
}
namespace {
struct TexSite { const char* file; int line; };
std::map<GLuint, TexSite>& texTrace() { static std::map<GLuint, TexSite> m; return m; }
bool texTraceOn() { static const bool on = std::getenv("WFC_TEXTRACE") != nullptr || std::getenv("WFC_GLTRACE") != nullptr; return on; }
}  // namespace
void tracedGenTextures(GLsizei n, GLuint* textures, const char* file, int line) {
    ::glGenTextures(n, textures);
    if (texTraceOn()) for (GLsizei i = 0; i < n; ++i) texTrace()[textures[i]] = TexSite{file, line};
}
void textureTraceDump(const char* tag) {
    if (!texTraceOn()) return;
    std::map<std::string, int> now;
    for (const auto& kv : texTrace()) {
        const char* f = kv.second.file;
        const char* s = std::strrchr(f, '/'); const char* b = std::strrchr(f, '\\');
        if (b > s) s = b;
        now[std::string(s ? s + 1 : f) + ":" + std::to_string(kv.second.line)]++;
    }
    static std::map<std::string, int> prev;
    std::string grew;
    for (const auto& kv : now) {
        const int d = kv.second - (prev.count(kv.first) ? prev[kv.first] : 0);
        if (d > 0) grew += " " + kv.first + " +" + std::to_string(d) + " (" + std::to_string(kv.second) + ")";
    }
    LOG_INFO("TEXTRACE %s: %zu live traced textures; sites that grew since the last dump:%s", tag, texTrace().size(),
             grew.empty() ? " none" : grew.c_str());
    prev = now;
    static std::map<uintptr_t, int> prevSites[6];
    for (int k = 0; k < 6; ++k) {
        std::map<uintptr_t, int> sites;
        for (const auto& kv : gltrace::live[k]) sites[kv.second]++;
        std::string g;
        for (const auto& kv : sites) {
            const int d = kv.second - (prevSites[k].count(kv.first) ? prevSites[k][kv.first] : 0);
            if (d > 0) { char b[64]; std::snprintf(b, sizeof b, " exe+0x%llx +%d (%d)", (unsigned long long)kv.first, d, kv.second); g += b; }
        }
        LOG_INFO("GLTRACE %s: %zu live %s; sites that grew:%s", tag, gltrace::live[k].size(), gltrace::kKind[k],
                 g.empty() ? " none" : g.c_str());
        prevSites[k] = sites;
    }
}
void cachedDeleteTextures(GLsizei n, const GLuint* textures) {
    ::glDeleteTextures(n, textures);
    if (texTraceOn()) for (GLsizei i = 0; i < n; ++i) texTrace().erase(textures[i]);
    for (GLsizei i = 0; i < n; ++i)
        for (int u = 0; u < kTexUnits; ++u)
            for (int s = 0; s < 3; ++s)
                if (gTexKnown[u][s] && gTexBound[u][s] == textures[i]) gTexBound[u][s] = 0;   // reverts to 0
}
void textureCacheInvalidate() {
    for (auto& row : gTexKnown) for (bool& k : row) k = false;
    gActiveUnit = -1;
}
void textureCacheStats(unsigned long long& issued, unsigned long long& skipped) { issued = gTexIssued; skipped = gTexSkipped; }
unsigned long long bufferUploadBytes() { return gBufBytes; }
unsigned long long firstProgramBinds() { return gFirstBinds; }
unsigned long long bufferAllocs() { return gBufAllocs; }
unsigned long long bufferAllocBytes() { return gBufAllocBytes; }
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

// ---- WFC_DRAWCOMBO=1 (DEV TOOL, Systems): every draw is keyed by program x the fixed-function state the driver may compile into
// it (blend enable / funcs / equation, depth test / mask / func, cull / face, stencil, polygon offset, alpha-to-coverage, the
// bound draw framebuffer, primitive, index type, entry point); a combination never seen before is logged once with the present
// count (SwapBuffers calls since start; SLOWFRAME counts match frames only), the time and the call site (module-relative return address: symbolise with llvm-symbolizer or wfc_rebuild.map).
// Drivers compile shader variants for new state combinations lazily at the first draw, and SwapBuffers waits on them: a stall
// frame's "draw combo" lines name what to prewarm. The GL 1.1 draws (glDrawElements / glDrawArrays, OPENGL32 imports) are
// hooked in the exe's import table, the extension draws through the function pointers; SwapBuffers is hooked to count frames.
// Off: nothing installed. On: ~15 glGet* per draw (a diagnostics build, not for timing).
namespace drawcombo {
bool on() { static const bool v = std::getenv("WFC_DRAWCOMBO") != nullptr; return v; }
struct Key {
    GLint prog, blend, bsrc, bdst, bsrcA, bdstA, beq, depth, dmask, dfunc, cull, cface, stencil, poffset, a2c, fbo;
    GLenum mode, type; int entry;
    bool operator==(const Key& o) const { return std::memcmp(this, &o, sizeof(Key)) == 0; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        const unsigned char* p = (const unsigned char*)&k; size_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(Key); ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
};
std::unordered_map<Key, int, KeyHash>* seen = nullptr;
long frame = 0, combos = 0;
std::chrono::steady_clock::time_point t0;
const char* kEntry[] = {"DrawElements", "DrawArrays", "DrawElementsInstanced", "MultiDrawElementsIndirect"};
GLint geti(GLenum e) { GLint v = 0; glGetIntegerv(e, &v); return v; }
void note(int entry, GLenum mode, GLenum type, void* ra) {
    Key k;
    std::memset(&k, 0, sizeof k);   // padding too: the key is hashed / compared bytewise
    k.prog = geti(0x8B8D);                                                      // GL_CURRENT_PROGRAM
    k.blend = glIsEnabled(GL_BLEND);
    if (k.blend) { k.bsrc = geti(0x80C9); k.bdst = geti(0x80C8); k.bsrcA = geti(0x80CB); k.bdstA = geti(0x80CA); k.beq = geti(0x8009); }
    k.depth = glIsEnabled(GL_DEPTH_TEST);
    k.dmask = geti(0x0B72); k.dfunc = k.depth ? geti(0x0B74) : 0;               // GL_DEPTH_WRITEMASK, GL_DEPTH_FUNC
    k.cull = glIsEnabled(GL_CULL_FACE); k.cface = k.cull ? geti(0x0B45) : 0;    // GL_CULL_FACE_MODE
    k.stencil = glIsEnabled(0x0B90); k.poffset = glIsEnabled(0x8037); k.a2c = glIsEnabled(0x809E);
    k.fbo = geti(0x8CA6);                                                        // GL_DRAW_FRAMEBUFFER_BINDING
    k.mode = mode; k.type = type; k.entry = entry;
    if (!seen) seen = new std::unordered_map<Key, int, KeyHash>();
    if (!seen->emplace(k, 1).second) return;
    ++combos;
    static const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    LOG_INFO("draw combo #%ld present %ld t %.3f: %s prog %d blend %d (%x %x %x %x eq %x) depth %d mask %d func %x cull %d (%x) "
             "stencil %d poffset %d a2c %d fbo %d mode %x type %x at +0x%llx", combos, frame, t, kEntry[entry], k.prog, k.blend,
             k.bsrc, k.bdst, k.bsrcA, k.bdstA, k.beq, k.depth, k.dmask, k.dfunc, k.cull, k.cface, k.stencil, k.poffset, k.a2c, k.fbo,
             mode, type, (unsigned long long)((uintptr_t)ra - base));
}
typedef void(APIENTRY* PFN_DE)(GLenum, GLsizei, GLenum, const void*);
typedef void(APIENTRY* PFN_DA)(GLenum, GLint, GLsizei);
typedef BOOL(WINAPI* PFN_Swap)(HDC);
PFN_DE rDE = nullptr; PFN_DA rDA = nullptr; PFN_Swap rSwap = nullptr;
PFN_DrawElementsInstanced rDEI = nullptr; PFN_MultiDrawElementsIndirect rMDEI = nullptr;
void APIENTRY tDE(GLenum m, GLsizei n, GLenum t, const void* i) { note(0, m, t, __builtin_return_address(0)); rDE(m, n, t, i); }
void APIENTRY tDA(GLenum m, GLint f, GLsizei n) { note(1, m, 0, __builtin_return_address(0)); rDA(m, f, n); }
void APIENTRY tDEI(GLenum m, GLsizei n, GLenum t, const void* i, GLsizei c) { note(2, m, t, __builtin_return_address(0)); rDEI(m, n, t, i, c); }
void APIENTRY tMDEI(GLenum m, GLenum t, const void* i, GLsizei c, GLsizei s) { note(3, m, t, __builtin_return_address(0)); rMDEI(m, t, i, c, s); }
BOOL WINAPI tSwap(HDC dc) { ++frame; return rSwap(dc); }
template <class F> int patchImport(const char* name, F hook, F& original) {   // the exe's import slots for `name`
    auto* b = (unsigned char*)GetModuleHandleW(nullptr);
    auto* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int n = 0;
    for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(b + dir.VirtualAddress); imp->Name; ++imp) {
        if (!imp->OriginalFirstThunk) continue;
        auto* names = (IMAGE_THUNK_DATA*)(b + imp->OriginalFirstThunk);
        auto* slots = (IMAGE_THUNK_DATA*)(b + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (std::strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(b + names->u1.AddressOfData))->Name, name) != 0) continue;
            if ((void*)slots->u1.Function == (void*)hook) continue;   // already ours (load() runs per renderer init)
            DWORD old = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            original = (F)(void*)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)(void*)hook;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            ++n;
        }
    }
    return n;
}
void install() {
    if (!on()) return;
    static bool imports = false;
    if (!imports) {
        imports = true;
        t0 = std::chrono::steady_clock::now();
        const int a = patchImport("glDrawElements", (PFN_DE)tDE, rDE), b = patchImport("glDrawArrays", (PFN_DA)tDA, rDA);
        patchImport("SwapBuffers", (PFN_Swap)tSwap, rSwap);
        LOG_INFO("WFC_DRAWCOMBO: on (import hooks glDrawElements %d, glDrawArrays %d)", a, b);
    }
    if (DrawElementsInstanced && DrawElementsInstanced != tDEI) { rDEI = DrawElementsInstanced; DrawElementsInstanced = tDEI; }
    if (MultiDrawElementsIndirect && MultiDrawElementsIndirect != tMDEI) { rMDEI = MultiDrawElementsIndirect; MultiDrawElementsIndirect = tMDEI; }
}
}  // namespace drawcombo

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
    if (ok) { installUniformCache(); installUploadCounters(); gltrace::install(); drawcombo::install(); }
    {   // GL <-> Vulkan interop capability (optional Vulkan present / upscalers / ray tracing): logged once
        static bool logged = false;
        typedef const GLubyte*(APIENTRY* PFN_GetStringi)(GLenum, GLuint);
        PFN_GetStringi getStringi = (PFN_GetStringi)wglGetProcAddress("glGetStringi");
        if (!logged && getStringi) {
            logged = true;
            GLint n = 0;
            glGetIntegerv(0x821D /*GL_NUM_EXTENSIONS*/, &n);
            const char* want[] = {"GL_EXT_memory_object", "GL_EXT_memory_object_win32", "GL_EXT_semaphore",
                                  "GL_EXT_semaphore_win32", "GL_NV_draw_vulkan_image"};
            std::string have;
            for (const char* w : want) {
                bool found = false;
                for (GLint i = 0; i < n && !found; ++i) {
                    const char* e = (const char*)getStringi(GL_EXTENSIONS, (GLuint)i);
                    found = e && std::strcmp(e, w) == 0;
                }
                have += std::string(" ") + w + (found ? "=yes" : "=no");
            }
            LOG_INFO("GL interop:%s (vendor %s, renderer %s)", have.c_str(), (const char*)glGetString(GL_VENDOR),
                     (const char*)glGetString(GL_RENDERER));
        }
    }
    textureCacheInvalidate();
    return ok;
}

} // namespace glx
