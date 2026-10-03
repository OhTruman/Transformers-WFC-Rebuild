// Clean-room reconstruction — OpenGL renderer for the GFx render list (shapes, gradients, bitmaps, glyph outlines,
// clip masks). Fills use stencil winding (nonzero) + cover; masks nest through the stencil's high nibble.
// The UI is drawn into a multisampled target and composited premultiplied, so it can sit over the 3D scene.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/gfx/Display.h"
#include "ui/gl/GlCensus.h"

namespace ui {

class GfxRendererGL {
public:
    ~GfxRendererGL();
    bool init();
    // Starts a UI frame of the given window size. clear: frontend screens (no 3D scene below).
    void begin(int width, int height);
    // Base matrix that maps a movie stage (twips) into the window ("showAll": uniform scale, centred).
    static gfx::Matrix stageMatrix(float stageW, float stageH, int width, int height);
    void draw(const std::vector<gfx::Player::RenderItem>& items, float alpha = 1.0f);
    // Composites the UI onto the default framebuffer.
    void end();
    bool ok() const { return ok_; }
    size_t cachedShapes() const { return shapes_.size(); }
    size_t textures() const { return textures_.size(); }
    void ownedNames(GlCensus::Owned& o) const;   // GL objects of the UI renderer (kept across level travel)

private:
    struct Mesh { std::vector<float> fan; float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; int set = 0, style = 0; };
    struct Stroke { std::vector<float> tris; int set = 0, style = 0; };
    struct Cached { std::vector<Mesh> fills; std::vector<Stroke> strokes; };
    const Cached& cache(const gfx::ShapeDef* s, bool glyph);
    unsigned texture(const std::string& path, int& w, int& h);
    unsigned gradientTexture(const gfx::FillStyle& fs);
    void setFill(const gfx::FillStyle& fs, const gfx::Matrix& world, const gfx::CXForm& cx, float alpha, int mode = -1);
    void drawTriangles(const std::vector<float>& v, const gfx::Matrix& m);
    void stencilWinding(const std::vector<float>& fan, const gfx::Matrix& m);
    void cover(float x0, float y0, float x1, float y1, const gfx::Matrix& m, bool mask);
    void fullscreen();

    bool ok_ = false;
    int w_ = 0, h_ = 0, fbw_ = 0, fbh_ = 0;
    unsigned prog_ = 0, compProg_ = 0, vbo_ = 0, vao_ = 0;
    unsigned msFbo_ = 0, msColor_ = 0, msDepth_ = 0, resFbo_ = 0, resTex_ = 0;
    int samples_ = 8;
    int level_ = 0;          // mask nesting level
    bool inMask_ = false;
    std::map<std::pair<const gfx::ShapeDef*, bool>, Cached> shapes_;
    struct Tex { unsigned id = 0; int w = 0, h = 0; };
    std::map<std::string, Tex> textures_;
    std::map<std::string, unsigned> gradients_;
    // Uniform locations.
    int uView_ = -1, uWorld_ = -1, uFillInv_ = -1, uMode_ = -1, uColor_ = -1, uMul_ = -1, uAdd_ = -1, uTex_ = -1, uTexSize_ = -1, uFocal_ = -1;
};

} // namespace ui
