#include "render/gl/GLExt.h"
#include "core/Log.h"

#include <cstdlib>
#include <cstring>
#include <map>

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
}

void gpuTimerBegin() {
    gBegun = false;
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
        gQActive[slot] = false;
        static int logged = 0;
        if (gLastGpuMs > 250.0 && logged++ < 50)
            LOG_WARN("GPU frame time %.1f ms (Windows TDR resets the driver at ~2000 ms of GPU work)", gLastGpuMs);
    }
    BeginQuery(kTimeElapsed, gQ[slot]);
    gQActive[slot] = true;
    gBegun = true;
}

void gpuTimerEnd() {
    if (EndQuery && gBegun) EndQuery(0x88BF);
    gBegun = false;
    ++gQi;
}

double lastGpuFrameMs() { return gLastGpuMs; }

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
    return ok;
}

} // namespace glx
