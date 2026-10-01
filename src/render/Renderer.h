// Clean-room reconstruction — rendering abstraction.
// Gameplay/presentation code issues draw calls through this; no GL types leak out.
#pragma once
#include <functional>
#include <string>
#include "core/Math.h"
#include "render/Camera.h"
#include "render/Mesh.h"

namespace render {

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

    // Original-data rendering (WFC shader path): load the map's compiled materials, baked
    // directional lightmaps, static lights and height fog produced by tools/render/*.py.
    // Returns false when unavailable; the renderer then keeps its legacy fixed-function path.
    virtual bool loadMapRenderData(const std::string& mapName) { (void)mapName; return false; }

    // Segment occlusion query (true == blocked) used for dynamic-object light visibility,
    // like UE3's light-environment visibility traces. Optional.
    using VisibilityQuery = std::function<bool(const core::Vec3& from, const core::Vec3& to)>;
    virtual void setVisibilityQuery(VisibilityQuery q) { (void)q; }

    // Save the current framebuffer to a 24-bit BMP (debug/automated verification).
    virtual bool captureScreenshot(const char* path) = 0;
};

// Factory (fixed-function GL implementation for the first milestone).
IRenderer* createGLRenderer();

} // namespace render
