// Clean-room reconstruction — GL object release for level travel (stopgap).
// IRenderer has no map-unload entry point and GLRenderer never deletes its GL objects, so every match load leaked the
// previous map's textures / buffers (~1.6 GB per frontend <-> match cycle, measured). The census records the GL name
// baseline before a match load and, after the match world is released, deletes the textures, buffers, framebuffers,
// renderbuffers, vertex arrays and programs created since. Shaders are kept (the renderer caches one shared vertex
// shader in a static). Objects owned by the frontend UI renderer are excluded.
// HANDOFF Rendering: a real IRenderer::unloadMap / resource release replaces this.
#pragma once
#include <set>
#include <string>

namespace ui {

class GlCensus {
public:
    struct Owned { std::set<unsigned> textures, buffers, framebuffers, renderbuffers, vertexArrays, programs; };
    void begin();
    // Returns a summary ("textures=N buffers=N ...").
    std::string release(const Owned& keep);
    // Measurement only: live GL names of each kind (glIs* from name 1 until a long gap), for the match-over-match
    // plateau check with a persistent renderer ("textures=N buffers=N ...").
    static std::string snapshot();

private:
    bool active_ = false;
    unsigned tex0_ = 0, buf0_ = 0, fbo0_ = 0, rbo0_ = 0, vao0_ = 0, prog0_ = 0;
};

} // namespace ui
