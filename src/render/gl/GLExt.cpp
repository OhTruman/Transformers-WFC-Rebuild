#include "render/gl/GLExt.h"
#include "core/Log.h"

namespace glx {

#define WFC_GL_DEF(ret, name, args) PFN_##name name = nullptr;
WFC_GL_FUNCS(WFC_GL_DEF)
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

bool load() {
    bool ok = true;
#define WFC_GL_LOAD(ret, name, args) \
    name = (PFN_##name)getProc("gl" #name); \
    if (!name) { LOG_WARN("GL entry point missing: gl%s", #name); ok = false; }
    WFC_GL_FUNCS(WFC_GL_LOAD)
#undef WFC_GL_LOAD
    return ok;
}

} // namespace glx
