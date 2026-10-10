// Clean-room reconstruction — OpenGL renderer for the GFx render list (shapes, gradients, bitmaps, glyph outlines,
// clip masks). Fills use stencil winding (nonzero) + cover; masks nest through the stencil's high nibble.
// The UI is drawn into a multisampled target and composited premultiplied, so it can sit over the 3D scene.
#pragma once
#include <cstdint>
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
    static gfx::Matrix movieMatrix(const gfx::Player& p, int width, int height);   // honours Stage.scaleMode noScale
    void draw(const std::vector<gfx::Player::RenderItem>& items, float alpha = 1.0f);
    // Composites the UI onto the default framebuffer.
    void end();
    // A full-screen movie frame (RGBA8, top row first), letterboxed into the window; serial skips re-uploads.
    void drawVideo(const uint8_t* rgba, int w, int h, uint64_t serial);
    bool ok() const { return ok_; }
    size_t cachedShapes() const { return shapes_.size(); }
    // The tessellation cache is keyed by ShapeDef address: when a movie (and its definitions) is destroyed, a later
    // allocation can reuse an address, so the cache is dropped (stale glyphs / an opaque vignette otherwise).
    void forgetShapes();
    size_t textures() const { return textures_.size(); }
    void ownedNames(GlCensus::Owned& o) const;   // GL objects of the UI renderer (kept across level travel)

private:
    // vbo: the fan followed by its 6-vertex cover quad, uploaded once (made on first draw; freed with the shape cache)
    // direct: the fan's triangles cover every filled pixel exactly once (one star-shaped loop seen from the pivot), so a
    // solid fill can be drawn straight, without the stencil winding pass and the cover quad (same pixels).
    struct Mesh { std::vector<float> fan; float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; int set = 0, style = 0; bool direct = false; mutable unsigned vbo = 0; };
    struct Stroke { std::vector<float> tris; int set = 0, style = 0; mutable unsigned vbo = 0; };
    struct Cached { std::vector<Mesh> fills; std::vector<Stroke> strokes; bool dynamic = false; unsigned lastUsed = 0; };
    const Cached& cache(const gfx::ShapeDef* s, bool glyph);
    unsigned texture(const std::string& path, int& w, int& h);
    unsigned gradientTexture(const gfx::FillStyle& fs);
    void setFill(const gfx::FillStyle& fs, const gfx::Matrix& world, const gfx::CXForm& cx, float alpha, int mode = -1);
    void drawTriangles(const std::vector<float>& v, const gfx::Matrix& m);
    void stencilWinding(const std::vector<float>& fan, const gfx::Matrix& m);
    void stencilState();                    // the winding pass state (stencilWinding without the draw)
    void coverState(bool mask);             // the cover pass state (cover without the draw)
    void drawMesh(const Mesh& mesh, bool mask, bool solid = false);    // stencilWinding + cover from the mesh's buffer
    void drawStroke(const Stroke& stroke);         // drawTriangles from the stroke's buffer
    void cover(float x0, float y0, float x1, float y1, const gfx::Matrix& m, bool mask);
    void fullscreen();

    bool ok_ = false;
    // GL state contract with the world / scene renderer: the UI pass (begin .. end) changes depth / cull / stencil /
    // blend / scissor / fixed-function enables, masks, the program, VAO, buffers, textures, framebuffers and the
    // viewport; begin() records them and end() restores them, so the next world frame starts from the state it left.
    // (From b1fce97 the in-match HUD ran this pass every match frame and left GL_DEPTH_TEST disabled: the Streets
    // architecture was overdrawn by later draws - the frontend-route world loss.)
    struct SavedGl {
        bool depthTest, cullFace, scissor, alphaTest, lighting, fog, stencil, blend;
        int depthMask;
        unsigned char colorMask[4];
        int stencilWriteMask;
        int blendSrcRgb, blendDstRgb, blendSrcA, blendDstA, blendEqRgb, blendEqA;
        int viewport[4];
        float clearColor[4];
        int clearStencil;
        int program, vao, arrayBuffer, activeTexture, texture2D, drawFbo, readFbo;
    } saved_{};
    void saveGlState();
    void restoreGlState();
    int w_ = 0, h_ = 0, fbw_ = 0, fbh_ = 0;
    unsigned prog_ = 0, compProg_ = 0, vbo_ = 0, vao_ = 0;
    unsigned msFbo_ = 0, msColor_ = 0, msDepth_ = 0, resFbo_ = 0, resTex_ = 0;
    // The default framebuffer's content (3D scene, match, video) copied in at begin(): movies blend against it
    // (SWF blend modes: multiply backdrops, additive glows), and end() writes the finished frame back opaque.
    unsigned backdropTex_ = 0;
    int backdropW_ = 0, backdropH_ = 0;
    int curBlend_ = -1;
    void applyBlend(int mode);
    static int effectiveBlend(const gfx::DisplayObject* d);
    int samples_ = 8;
    int level_ = 0;          // mask nesting level
    // Text drop shadows: glyph coverage rendered offscreen, box-blurred (two passes) and composited tinted.
    unsigned shBlurProg_ = 0, shCompProg_ = 0, shFbo_[2] = {}, shTex_[2] = {}, shStencil_ = 0;
    int shW_ = 0, shH_ = 0;
    void drawTextShadow(const std::vector<gfx::Player::RenderItem>& items, size_t at, float alpha);
    void drawGlyphCoverage(const gfx::Player::RenderItem& it, const gfx::Matrix& m);
    void ensureShadowTargets(int w, int h);
    bool inMask_ = false;
    std::map<std::pair<const gfx::ShapeDef*, bool>, Cached> shapes_;
    unsigned frame_ = 0;   // begin() count: display-object shapes (drawing API redrawn per frame) are dropped when unused
    struct Tex { unsigned id = 0; int w = 0, h = 0; };
    Tex video_;
    uint64_t videoSerial_ = 0;
    std::map<std::string, Tex> textures_;
    std::map<std::string, unsigned> gradients_;
    // Uniform locations.
    int uW1_ = -1, uF1_ = -1;   // the second matrix rows (looked up once, not per draw)
    unsigned boundBuf_ = ~0u;   // the GL_ARRAY_BUFFER this renderer last bound (~0 = unknown)
    void bindArray(unsigned buffer);
    int uView_ = -1, uWorld_ = -1, uFillInv_ = -1, uMode_ = -1, uColor_ = -1, uMul_ = -1, uAdd_ = -1, uTex_ = -1, uTexSize_ = -1, uFocal_ = -1;
};

} // namespace ui
