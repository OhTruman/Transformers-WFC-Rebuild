// Clean-room reconstruction — a renderer that draws nothing (WFC_NULLRENDER=1, sim-only harness boots).
// Direct-boot harnesses that never present a frame (NAVQ, BOTTEST, DETERMINISM, ...) still created the window, a GL context
// and uploaded the whole map to the GPU. With this stub the process holds no window and no GPU resources, so simulation
// measurements can run on the CPU outside GPU slots. Every upload returns a fresh handle (callers treat -1 as a failure and
// would take fallback paths); every draw / effect call is accepted and dropped; optional IRenderer hooks keep their defaults.
// The simulation must be identical to the GL boot: DETERMINISM prints the event-log digest for that comparison.
#include "render/Renderer.h"

namespace render {

namespace {
class NullRenderer final : public IRenderer {
public:
    bool init() override { return true; }
    void beginFrame(const Camera&, int, int) override {}
    void endFrame() override {}
    void drawBox(const core::Vec3&, const core::Vec3&, const core::Vec3&, float) override {}
    void drawGroundGrid(float, float, const core::Vec3&) override {}
    void drawLine(const core::Vec3&, const core::Vec3&, const core::Vec3&) override {}
    MeshHandle uploadMesh(const MeshData&) override { return nextMesh_++; }
    TextureHandle uploadTexture(const ImageData&) override { return nextTexture_++; }
    void drawMesh(MeshHandle, const core::Mat4&, const core::Vec3&) override {}
    void drawDynamicMesh(const MeshData&, const core::Mat4&, const core::Vec3&) override {}
    void drawParticles(const ParticleBatch&) override {}
    void drawMeshFx(MeshHandle, const core::Mat4&, float, float, float, float, float, float, float, float) override {}
    bool captureScreenshot(const char*) override { return false; }

private:
    MeshHandle nextMesh_ = 0;
    TextureHandle nextTexture_ = 0;
};
} // namespace

IRenderer* createNullRenderer() { return new NullRenderer(); }

} // namespace render
