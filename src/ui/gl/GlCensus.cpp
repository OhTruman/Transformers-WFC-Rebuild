#include "ui/gl/GlCensus.h"
#include "render/gl/GLExt.h"
#include "core/Log.h"

#include <cstdio>

namespace ui {

namespace {
typedef GLboolean(APIENTRY* PFN_IsName)(GLuint);
typedef void(APIENTRY* PFN_DeleteNames)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_GenNames)(GLsizei, GLuint*);
typedef GLuint(APIENTRY* PFN_CreateProgramX)(void);
typedef void(APIENTRY* PFN_DeleteProgramX)(GLuint);

struct Fns {
    PFN_IsName isBuffer = nullptr, isFramebuffer = nullptr, isRenderbuffer = nullptr, isVertexArray = nullptr, isProgram = nullptr;
    PFN_DeleteNames deleteBuffers = nullptr, deleteVertexArrays = nullptr;
    PFN_DeleteProgramX deleteProgram = nullptr;
    bool ok = false;
};

Fns& fns() {
    static Fns f;
    if (!f.ok) {
        f.isBuffer = (PFN_IsName)wglGetProcAddress("glIsBuffer");
        f.isFramebuffer = (PFN_IsName)wglGetProcAddress("glIsFramebuffer");
        f.isRenderbuffer = (PFN_IsName)wglGetProcAddress("glIsRenderbuffer");
        f.isVertexArray = (PFN_IsName)wglGetProcAddress("glIsVertexArray");
        f.isProgram = (PFN_IsName)wglGetProcAddress("glIsProgram");
        f.deleteBuffers = (PFN_DeleteNames)wglGetProcAddress("glDeleteBuffers");
        f.deleteVertexArrays = (PFN_DeleteNames)wglGetProcAddress("glDeleteVertexArrays");
        f.deleteProgram = (PFN_DeleteProgramX)wglGetProcAddress("glDeleteProgram");
        f.ok = f.isBuffer && f.isFramebuffer && f.isRenderbuffer && f.isVertexArray && f.isProgram && f.deleteBuffers &&
               f.deleteVertexArrays && f.deleteProgram;
    }
    return f;
}

// Next name the implementation hands out (generate + delete a probe).
template <class Gen, class Del> unsigned probe(Gen gen, Del del) {
    GLuint n = 0;
    gen(1, &n);
    del(1, &n);
    return n;
}
} // namespace

void GlCensus::begin() {
    Fns& f = fns();
    if (!f.ok || !glx::GenBuffers) { active_ = false; return; }
    tex0_ = probe([](GLsizei c, GLuint* n) { glGenTextures(c, n); }, [](GLsizei c, const GLuint* n) { glDeleteTextures(c, n); });
    buf0_ = probe(glx::GenBuffers, f.deleteBuffers);
    fbo0_ = probe(glx::GenFramebuffers, glx::DeleteFramebuffers);
    rbo0_ = probe(glx::GenRenderbuffers, glx::DeleteRenderbuffers);
    vao0_ = probe(glx::GenVertexArrays, f.deleteVertexArrays);
    GLuint p = glx::CreateProgram();
    f.deleteProgram(p);
    prog0_ = p;
    active_ = true;
}

std::string GlCensus::snapshot() {
    Fns& f = fns();
    auto live = [](auto isName) {
        int count = 0, gap = 0;
        for (unsigned n = 1; gap < 4096 && n < 4000000u; ++n) {
            if (!isName(n)) { ++gap; continue; }
            gap = 0;
            ++count;
        }
        return count;
    };
    int t = live([](GLuint n) { return glIsTexture(n) == GL_TRUE; });
    int b = live([&](GLuint n) { return f.isBuffer(n) == GL_TRUE; });
    int fb = live([&](GLuint n) { return f.isFramebuffer(n) == GL_TRUE; });
    int rb = live([&](GLuint n) { return f.isRenderbuffer(n) == GL_TRUE; });
    int va = live([&](GLuint n) { return f.isVertexArray(n) == GL_TRUE; });
    int pr = live([&](GLuint n) { return f.isProgram(n) == GL_TRUE; });
    char b2[256];
    std::snprintf(b2, sizeof b2, "textures=%d buffers=%d framebuffers=%d renderbuffers=%d vertexArrays=%d programs=%d", t, b, fb, rb, va, pr);
    return b2;
}

std::string GlCensus::release(const Owned& keep) {
    if (!active_) return "inactive";
    active_ = false;
    Fns& f = fns();
    auto sweep = [](unsigned from, auto isName, auto del, const std::set<unsigned>& own) {
        // Names are handed out increasingly; sweep until a long run of unused names.
        int count = 0, gap = 0;
        for (unsigned n = from; gap < 4096 && n < from + 4000000u; ++n) {
            if (!isName(n)) { ++gap; continue; }
            gap = 0;
            if (own.count(n)) continue;
            del(n);
            ++count;
        }
        return count;
    };
    int t = sweep(tex0_, [](GLuint n) { return glIsTexture(n) == GL_TRUE; }, [](GLuint n) { glDeleteTextures(1, &n); }, keep.textures);
    int b = sweep(buf0_, [&](GLuint n) { return f.isBuffer(n) == GL_TRUE; }, [&](GLuint n) { f.deleteBuffers(1, &n); }, keep.buffers);
    int fb = sweep(fbo0_, [&](GLuint n) { return f.isFramebuffer(n) == GL_TRUE; }, [&](GLuint n) { glx::DeleteFramebuffers(1, &n); }, keep.framebuffers);
    int rb = sweep(rbo0_, [&](GLuint n) { return f.isRenderbuffer(n) == GL_TRUE; }, [&](GLuint n) { glx::DeleteRenderbuffers(1, &n); }, keep.renderbuffers);
    int va = sweep(vao0_, [&](GLuint n) { return f.isVertexArray(n) == GL_TRUE; }, [&](GLuint n) { f.deleteVertexArrays(1, &n); }, keep.vertexArrays);
    int pr = sweep(prog0_, [&](GLuint n) { return f.isProgram(n) == GL_TRUE; }, [&](GLuint n) { f.deleteProgram(n); }, keep.programs);
    char b2[256];
    std::snprintf(b2, sizeof b2, "textures=%d buffers=%d framebuffers=%d renderbuffers=%d vertexArrays=%d programs=%d", t, b, fb, rb, va, pr);
    return b2;
}

} // namespace ui
