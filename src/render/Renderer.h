// Clean-room reconstruction — rendering abstraction.
// Gameplay/presentation code issues draw calls through this; no GL types leak out.
#pragma once
#include <functional>
#include <string>
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
    // Original emitter material (object name or path, e.g. "Ring_Distort_Add_MAT"). When the renderer
    // has compiled it, the sprites are shaded by that graph (particle colour = vertex colour, depth-biased
    // alpha, panners, blend mode) and `tex` is ignored; otherwise the textured fallback is drawn.
    const char* material = nullptr;
};

// Runtime character customization as pushed by WFC's TnCharacterApplier onto every character mesh
// (robot, vehicle, separate arm, weapon): material vector parameters Cust_Color_A, Cust_COLOR_B and
// EnergonColor, linear RGBA. A value whose RGB is all zero SKIPS the override (the material keeps
// its authored value). The faction selects which colour set the caller passes.
struct CharacterColors {
    float primary[4] = {0, 0, 0, 1};     // Cust_Color_A
    float secondary[4] = {0, 0, 0, 1};   // Cust_COLOR_B
    float energon[4] = {0, 0, 0, 1};     // EnergonColor
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

    // Original-data rendering (WFC shader path): load the map's compiled materials, baked
    // directional lightmaps, static lights and height fog produced by tools/render/*.py.
    // Returns false when unavailable; the renderer then keeps its legacy fixed-function path.
    virtual bool loadMapRenderData(const std::string& mapName) { (void)mapName; return false; }

    // Character customization for subsequent dynamic draws (see CharacterColors). Optional.
    virtual void setCharacterColors(const CharacterColors& c) { (void)c; }

    // HUD weapon crosshair (UI_GFxHud_p Hud_GFX.gfx, mc_crosshairIonBlaster), drawn by the renderer
    // from the movie's own geometry; gameplay supplies the state each frame. weaponSpread is the
    // value TnHUD passes to NotifyWeaponSpreadChanged; targetType as NotifyTargetTypeChanged
    // (0, 1, other = no tint). Fine aim keeps this crosshair for the Ion Blaster (the HUD's
    // NotifyFineAimChanged shows a scope only for HeavyPistol/BurstRifle/SniperRifle). Optional.
    // True when effect meshes/sprites are shaded by their compiled original material graphs
    // (drawMeshFx / ParticleBatch::material): callers must then pass the authored particle colour
    // only, without GL1 stand-ins for graph terms (intensity scales, fresnel approximations).
    virtual bool evaluatesFxMaterials() const { return false; }

    struct ReticleState { bool visible = false; float weaponSpread = 0.0f; int targetType = -1; };
    virtual void setReticle(const ReticleState& s) { (void)s; }

    // Segment occlusion query (true == blocked) used for dynamic-object light visibility,
    // like UE3's light-environment visibility traces. Optional.
    using VisibilityQuery = std::function<bool(const core::Vec3& from, const core::Vec3& to)>;
    virtual void setVisibilityQuery(VisibilityQuery q) { (void)q; }

    // Textured particle quads (depth-tested, no depth write, unfogged for additive).
    virtual void drawParticles(const ParticleBatch& batch) = 0;

    // Mesh particle (UE3 ParticleModuleTypeDataMesh with an additive, unlit, two-sided material):
    // the uploaded mesh's base-colour texture x colour (x colorScale 1/2/4 overbright).
    // fresnelExp > 0: per-vertex rim term f = saturate(pow(1 - |N.V|, exp) * scale), raised to
    // `fresnelPower` (materials whose emissive is built from a camera-vector fresnel).
    virtual void drawMeshFx(MeshHandle mesh, const core::Mat4& model, float r, float g, float b, float a,
                            float colorScale, float fresnelExp = 0.0f, float fresnelScale = 1.0f,
                            float fresnelPower = 1.0f) = 0;

    // Save the current framebuffer to a 24-bit BMP (debug/automated verification).
    virtual bool captureScreenshot(const char* path) = 0;
};

// Factory (fixed-function GL implementation for the first milestone).
IRenderer* createGLRenderer();

} // namespace render
