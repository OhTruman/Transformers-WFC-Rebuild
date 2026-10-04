// Clean-room reconstruction — rendering abstraction.
// Gameplay/presentation code issues draw calls through this; no GL types leak out.
#pragma once
#include <cstdint>
#include <utility>
#include <array>
#include <functional>
#include <string>
#include <vector>
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
    // Level travel: release every GPU resource of the loaded map render data (meshes, textures, programs, targets).
    // The renderer stays usable; a later loadMapRenderData() rebuilds everything for the next map.
    virtual void unloadMapRenderData() {}
    // Character instance for the following dynamic-mesh draws (drawDynamicMesh: robot / vehicle / weapon of one pawn):
    // keys the per-character light environment (DirectLightEnv state, update queue) and the TnCharacterApplier colours
    // (setCharacterColors applies to the current owner). 0 = the local player (default; PlayerOnly light channel).
    // Any chassis works: the robot / vehicle / weapon role comes from the mesh's material packages (*_ROBO_p,
    // *_VEH_p, WEP_*), never from character names.
    virtual void setDrawOwner(int ownerId) { (void)ownerId; }
    // Loading presentation (RE MILESTONE05_PLAYTEST §6: the original loading Bink plays on the rendering thread while the
    // game thread blocks): during loadMapRenderData / loadFrontendScene the renderer calls this between bounded steps
    // (each mesh submesh with its material program and textures, each map prop, each load phase) with no GL objects
    // bound, so the caller can present a loading frame (movie + overlay). The callback throttles itself; it must not
    // load or unload map render data. Pass an empty function to clear.
    virtual void setLoadYield(std::function<void()> yield) { (void)yield; }

    // Frontend 3D scenes (RE OVERNIGHT 2026-10-04 §D: the title / main menu render over the live level UI_FrontEnd_m +
    // streamed UI_FrontEnd_capture_VIG_m, the lobbies over UI_CharacterCustomization_m). No match World exists.
    //   loadFrontendScene: the UE level package names (e.g. {"UI_FrontEnd_m", "UI_FrontEnd_capture_VIG_m"}); the export
    //     directory is the first level whose name without "_m" has render data. false = not exported / no render data.
    //   drawFrontendScene: one complete frame (scene + post) into the back buffer, for the GFx overlay composited
    //     after it. Camera in UE units: location (UU), rotation (pitch, yaw, roll in degrees), horizontal FOV (deg).
    //     timeSec drives the level's map FX / movers clock.
    //   unloadFrontendScene: releases it (as unloadMapRenderData).
    virtual bool loadFrontendScene(const std::vector<std::string>& levels) { (void)levels; return false; }
    virtual void drawFrontendScene(const core::Vec3& camPosUE, const core::Vec3& camRotUEdeg, float fovDeg, int w, int h,
                                   double timeSec) { (void)camPosUE; (void)camRotUEdeg; (void)fovDeg; (void)w; (void)h; (void)timeSec; }
    virtual void unloadFrontendScene() {}
    // Matinee-driven actor pose in the loaded scene (Frontend's matinee evaluator): absolute world location (UU) and
    // rotation (pitch, yaw, roll in degrees), including RelativeToInitial / attachment. Actors not sent keep their
    // authored pose (and PHYS_Rotating). Visibility stays with setActorHidden (authored bHidden applies until then).
    virtual void setFrontendActorTransform(const std::string& actor, const core::Vec3& posUE, const core::Vec3& rotUEdeg) {
        (void)actor; (void)posUE; (void)rotUEdeg;
    }

    // Canvas material tile (UE3 FCanvas::DrawMaterialTile / UCanvas.DrawMaterialTile): a screen quad shaded by a
    // compiled original material (e.g. UI_HudMarkers_p) with per-draw parameter values (MaterialInstanceDynamic
    // SetScalarParameterValue / SetVectorParameterValue). Pixels, top-left origin; drawn after the scene's post
    // processing in submission order. Returns false if the material is not available (caller may fall back).
    struct MaterialTile {
        std::string material;                                   // full object path
        float x = 0, y = 0, w = 0, h = 0;                       // pixels
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        float rotation = 0;                                     // radians, about the tile centre
        std::vector<std::pair<std::string, std::array<float, 4>>> params;   // scalar -> x
    };
    virtual bool drawMaterialTile(const MaterialTile& t) { (void)t; return false; }
    virtual bool hasMaterial(const std::string& material) const { (void)material; return false; }

    // ---- 2D composition (frontend / GFx movies / Bink frames / loading screens / fades) ----
    // Screen-space triangles in pixels (top-left origin) with per-vertex RGBA and UV, optionally textured.
    // Display-referred: colours and texels are written as given (no gamma conversion), as UI and video
    // sources are authored. Inside a 3D frame the batches are composited after the scene, its post pass, the
    // Canvas material tiles and the HUD reticle, in submission order; outside a frame they draw immediately.
    enum class ScreenBlend { Alpha, Premultiplied, Additive, Multiply, Opaque };
    struct ScreenVertex { float x, y, u, v; uint8_t r, g, b, a; };
    struct ScreenBatch {
        TextureHandle texture = kInvalidTexture;   // kInvalidTexture: untextured (vertex colour only)
        ScreenBlend blend = ScreenBlend::Alpha;
        bool clampUV = true, linearFilter = true;
        bool scissor = false; int sx = 0, sy = 0, sw = 0, sh = 0;   // pixels, top-left origin
        std::vector<ScreenVertex> verts;           // triangle list
    };
    virtual void drawScreenTriangles(const ScreenBatch& b) { (void)b; }
    // Replace a texture's contents (same or new size): streamed video frames, dynamic UI bitmaps.
    virtual bool updateTexture(TextureHandle h, const ImageData& image) { (void)h; (void)image; return false; }
    // Canvas text in an original UE3 font (render data _ui/fonts/<font>.json, tools/render/build_hud.py): UE3 Canvas
    // layout (glyph USize advance, VerticalOffset, no kerning), glyph coverage in alpha x colour, display-referred,
    // composited like drawScreenTriangles. font = short name, e.g. "MarkerFont". Returns false if unavailable.
    virtual bool drawCanvasText(const std::string& font, const std::string& utf8, float x, float y, const uint8_t rgba[4],
                                float scale = 1.0f) { (void)font; (void)utf8; (void)x; (void)y; (void)rgba; (void)scale; return false; }
    virtual bool canvasTextSize(const std::string& font, const std::string& utf8, float& w, float& h, float scale = 1.0f) {
        (void)font; (void)utf8; (void)scale; w = h = 0; return false;
    }
    // Diagnostics (collision / fidelity reports): the rendered static-mesh triangle hit first by a ray, with its
    // authored source (component object path, StaticMesh, material). BSP is not included. Not for gameplay use.
    struct PickHit { std::string component, mesh, material; float distance = 0; core::Vec3 point{0, 0, 0}, normal{0, 0, 0}; };
    virtual bool pickWorld(const core::Vec3& origin, const core::Vec3& dir, float maxDist, PickHit& out) {
        (void)origin; (void)dir; (void)maxDist; (void)out; return false;
    }
    virtual int viewportWidth() const { return 0; }
    virtual int viewportHeight() const { return 0; }

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

    // Gameplay-authoritative visibility of an authored map actor (world.glb actor name, e.g. "InterpActor_6165").
    // Authored bHidden actors start hidden; Kismet-driven state (SeqAct_ToggleHidden by game rule) is pushed here.
    virtual void setActorHidden(const std::string& actor, bool hidden) { (void)actor; (void)hidden; }
    // Activate / deactivate an authored map particle component (owner actor name or component path). Components
    // start in their authored bAutoActivate state (pickup factories toggle their effects at runtime).
    virtual void setMapEffectActive(const std::string& ownerOrComponent, bool active) { (void)ownerOrComponent; (void)active; }
    // Pickup presentation (TnPickupFactory.SetPickupHidden / SetPickupVisible): key "<factory actor>|custom" or
    // "|highlight"; hidden = SetHidden (nothing drawn). Gameplay / Systems own the factory state.
    virtual void setMapEffectState(const std::string& key, bool active, bool hidden) { (void)key; (void)active; (void)hidden; }
    // Gameplay's active game-rule classes (e.g. "TnGameRules_SingleFlagCTF"): presentation gates authored on rules
    // (Kismet UnHide of the objective bases, Conquest totems, objective-factory effects).
    virtual void setActiveGameRules(const std::vector<std::string>& rules) { (void)rules; }
    // True when the renderer draws the authored map particle components itself (Systems must not draw them too).
    virtual bool drawsAuthoredMapFx() const { return false; }
    // Gameplay's map clock: seconds since SeqEvent_GameplayStarted (MapState). Movers (PHYS_Rotating domes, the
    // looping SkyBeam Matinee), totem idle animation and KOTH state are evaluated at this time so that what is
    // drawn matches the moving collision Gameplay simulates. Optional.
    virtual void setMapClock(float secondsSinceGameplayStarted) { (void)secondsSinceGameplayStarted; }
    // Authored destructible presentation state (HmDestructionState handle: 0 intact, 1 destroyed, 2 settled).
    // Gameplay owns damage / triggers / timers.
    virtual void setDestructibleState(const std::string& actor, int state) { (void)actor; (void)state; }

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

// Render-data root (WFC_RENDER_DATA, default <exe>/../../work/render): map data in <root>/<map>, UI data in <root>/_ui.
std::string wfcRenderDataRoot();

} // namespace render
