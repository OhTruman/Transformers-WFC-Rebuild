// Clean-room reconstruction — rendering abstraction.
// Gameplay/presentation code issues draw calls through this; no GL types leak out.
#pragma once
#include "core/Math.h"
#include "render/Camera.h"
#include "render/Mesh.h"

namespace render {

// Particle sprites (weapon/impact FX). Built into quads by the renderer from its view so
// gameplay code never needs the camera basis.
enum class ParticleBlend { Additive, Translucent };
struct Particle {
    core::Vec3 pos;                 // quad centre (world)
    core::Vec3 axis{0, 0, 0};       // non-zero: velocity-aligned (the quad's length runs along it)
    float w = 1.0f, h = 1.0f;       // width (across) / height-or-length (along axis), metres
    float rot = 0.0f;               // radians, camera-facing sprites only
    float r = 1, g = 1, b = 1, a = 1;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    bool uAlongAxis = false;        // texture long axis is U (rotate UVs 90 deg for aligned quads)
};
struct ParticleBatch {
    TextureHandle tex = kInvalidTexture;
    ParticleBlend blend = ParticleBlend::Additive;
    float colorScale = 1.0f;        // 1, 2 or 4: overbright (UE3 HDR emissive > 1)
    const Particle* p = nullptr;
    size_t n = 0;
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init() = 0;
    virtual void beginFrame(const Camera& cam, int viewportW, int viewportH) = 0;
    virtual void endFrame() = 0;

    // Oriented box centred at `center`, full extents `size`, rotated `yaw` about Y.
    virtual void drawBox(const core::Vec3& center, const core::Vec3& size,
                         const core::Vec3& color, float yaw = 0.0f) = 0;

    // Ground plane grid centred at origin.
    virtual void drawGroundGrid(float halfExtent, float spacing, const core::Vec3& color) = 0;

    // World-space line (debug).
    virtual void drawLine(const core::Vec3& a, const core::Vec3& b, const core::Vec3& color) = 0;

    // Upload a CPU mesh once; returns a handle for repeated drawing. kInvalidMesh on failure.
    virtual MeshHandle uploadMesh(const MeshData& mesh) = 0;

    // Upload a decoded RGBA image as a texture; returns a handle. kInvalidTexture on failure.
    virtual TextureHandle uploadTexture(const ImageData& image) = 0;

    // Draw an uploaded mesh with a model transform and flat base colour (lit).
    virtual void drawMesh(MeshHandle handle, const core::Mat4& model, const core::Vec3& color) = 0;

    // Draw a transient mesh (e.g. CPU-skinned each frame) without uploading/retaining it.
    virtual void drawDynamicMesh(const MeshData& mesh, const core::Mat4& model, const core::Vec3& color) = 0;

    // Textured particle quads (depth-tested, no depth write, unfogged for additive).
    virtual void drawParticles(const ParticleBatch& batch) = 0;

    // Mesh particle (UE3 ParticleModuleTypeDataMesh with an additive, unlit, two-sided material):
    // the uploaded mesh's base-colour texture x colour (x colorScale 1/2/4 overbright).
    virtual void drawMeshFx(MeshHandle mesh, const core::Mat4& model, float r, float g, float b, float a,
                            float colorScale) = 0;

    // Save the current framebuffer to a 24-bit BMP (debug/automated verification).
    virtual bool captureScreenshot(const char* path) = 0;
};

// Factory (fixed-function GL implementation for the first milestone).
IRenderer* createGLRenderer();

} // namespace render
