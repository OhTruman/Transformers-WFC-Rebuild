// Clean-room reconstruction — interim presentation of the frontend's live levels through the public IRenderer API.
//
// The scene levels are AssetTools' exports of the UI levels (ExtractedAssets/VerticalSlice/Maps/<family>: one folder per
// scene family = the persistent UI level plus its streamed sublevels, e.g. UI_FrontEnd = UI_FrontEnd_m +
// UI_FrontEnd_capture_VIG_m, UI_PartyLobby = UI_PartyLobby_m + UI_CharacterCustomization_m; world.glb, lightmaps,
// render_index.json). This adapter loads a family like a match map (IRenderer::loadMapRenderData + uploadMesh, the
// way World does) and draws it with the camera the frontend's Kismet evaluates. Rendering owns the full presentation
// (original material shaders as far as its map path covers them, the 326 particle components, 17 matinee ships, lens
// flares, fog, the CybertronSky post chain): an IRenderer frontend-scene entry point replaces this class.
// Lifetime: the scene's GL objects are recorded (GlCensus) and released before a match loads, since the renderer is
// recreated after every match.
#pragma once
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstdlib>
#include <functional>
#include <map>

#include "frontend/FrontendScene.h"
#include "render/Renderer.h"
#include "ui/gl/GlCensus.h"

namespace core {

// Rendering's frontend-scene entry points (agents/rendering 004327c: IRenderer::loadFrontendScene / drawFrontendScene /
// unloadFrontendScene, setLoadYield) are used when the IRenderer in this tree has them; otherwise the interim path.
template <class R, class = void> struct HasFrontendScene : std::false_type {};
template <class R>
struct HasFrontendScene<R, std::void_t<decltype(std::declval<R&>().loadFrontendScene(std::declval<const std::vector<std::string>&>()))>>
    : std::true_type {};
template <class R, class = void> struct HasActorTransform : std::false_type {};
template <class R>
struct HasActorTransform<R, std::void_t<decltype(std::declval<R&>().setFrontendActorTransform(
    std::declval<const std::string&>(), std::declval<const Vec3&>(), std::declval<const Vec3&>()))>> : std::true_type {};
template <class R, class = void> struct HasActorScale : std::false_type {};
template <class R>
struct HasActorScale<R, std::void_t<decltype(std::declval<R&>().setFrontendActorScale(std::declval<const std::string&>(), 1.0f))>>
    : std::true_type {};
template <class R, class = void> struct HasDisplayGamma : std::false_type {};
template <class R>
struct HasDisplayGamma<R, std::void_t<decltype(std::declval<R&>().setDisplayGamma(1.0f))>> : std::true_type {};
// Ground under a point (agents/rendering: IRenderer::sceneGroundHeight, a downward trace against the scene's level BSP).
template <class R, class = void> struct HasGroundHeight : std::false_type {};
template <class R>
struct HasGroundHeight<R, std::void_t<decltype(std::declval<const R&>().sceneGroundHeight(0.0f, 0.0f, 0.0f, std::declval<float&>()))>>
    : std::true_type {};
// Posed preview body (agents/rendering c22e356: loadPreviewBody / posePreviewBody, the Cust_Idle preview animation).
template <class R, class = void> struct HasPreviewBody : std::false_type {};
template <class R>
struct HasPreviewBody<R, std::void_t<decltype(std::declval<R&>().loadPreviewBody(std::declval<const std::string&>(),
                                                                                 std::declval<const std::vector<std::string>&>(),
                                                                                 std::declval<const std::string&>())),
                                     decltype(std::declval<R&>().posePreviewBody(0, 0.0f, std::declval<render::MeshData&>()))>>
    : std::true_type {};
// Preview pawns (agents/rendering: setFrontendSceneDraw + actorMatrix + loadContentMesh).
template <class R, class = void> struct HasPreviewDraw : std::false_type {};
template <class R>
struct HasPreviewDraw<R, std::void_t<decltype(std::declval<R&>().setFrontendSceneDraw(std::declval<std::function<void(R&)>>())),
                                     decltype(std::declval<const R&>().actorMatrix(std::declval<const Vec3&>(), std::declval<const Vec3&>())),
                                     decltype(std::declval<R&>().loadContentMesh(std::declval<const std::string&>(), std::declval<render::MeshData&>()))>>
    : std::true_type {};
template <class R, class = void> struct HasLoadYield : std::false_type {};
template <class R>
struct HasLoadYield<R, std::void_t<decltype(std::declval<R&>().setLoadYield(std::declval<std::function<void()>>()))>> : std::true_type {};

class FrontendSceneGL final : public frontend::IFrontendSceneRenderer {
public:
    explicit FrontendSceneGL(render::IRenderer* r) : r_(r) {}
    void setRenderer(render::IRenderer* r) { r_ = r; family_.clear(); mesh_ = render::kInvalidMesh; censusActive_ = false; previewMeshes_.clear(); }
    bool load(const std::vector<std::string>& levels) override;
    void draw(const frontend::SceneView& view, int width, int height) override;
    void unload() override {}   // the family stays loaded while UI levels travel within it; released by release()
    void setEffectActive(const std::string& actor, bool on) override { if (r_ && native_) r_->setMapEffectActive(actor, on); }
    void setActorHidden(const std::string& actor, bool hidden) override {
        if (actor.rfind("PreviewGuy", 0) == 0) {   // spawned preview pawns (Preview_Characters ToggleHidden)
            int slot = std::atoi(actor.c_str() + 10);
            if (slot >= 0 && slot < 2) previewHidden_[slot] = hidden;
            return;
        }
        if (r_ && native_) r_->setActorHidden(actor, hidden);
    }
    // Before a match loads: the scene's render data and GL objects go (keep = the UI renderer's own objects).
    std::string release(const ui::GlCensus::Owned& keep);
    static std::string familyFor(const std::string& uiLevel);
    // Create a Character preview (TnCharacterScriptBinding.UpdatePreviewCharacter): one body per PreviewGuy slot, drawn
    // inside the scene by the renderer (setDrawOwner(1 + slot), linear colours, the content glTF in bind pose until
    // Gameplay supplies posed bodies). Empty = none.
    struct PreviewSlot { std::string gltf, vehicleGltf; std::vector<std::string> animSets; float pos[3] = {0, 0, 0}; float yawDeg = 0;
                         float primary[3] = {0, 0, 0}, secondary[3] = {0, 0, 0}; };
    // TnCharacterScriptBinding.TransformPreviewCharacter (toggle) / TransformPreviewCharacterToRobot: the first visible
    // pawn (FindPreviewCharacterToTransform) changes form. A chassis change respawns the pawn in robot form.
    void transformPreview(bool toRobotOnly);
    struct PreviewStats { int slots = 0, visible = 0, vehicles = 0, meshes = 0, bodies = 0; };
    PreviewStats previewStats() const;
    // Each pawn stands on the floor under its spawn point when the renderer can trace it (the original's
    // OnPreviewPawnTick FindGround; the roster meshes have their origin at the feet), else at the PathNode height.
    void setPreview(std::vector<PreviewSlot> slots);
    void drawPreview(render::IRenderer& r);

private:
    render::IRenderer* r_;
    std::string family_;                          // loaded scene family ("" none)
    render::MeshHandle mesh_ = render::kInvalidMesh;
    ui::GlCensus census_;
    bool censusActive_ = false;
    bool native_ = false;                         // Rendering's loadFrontendScene owns the family
    std::vector<PreviewSlot> preview_;
    bool previewHidden_[2] = {false, false};
    bool previewVehicle_[2] = {false, false};
    double previewSpawn_[2] = {0, 0};                           // idle clock start per slot (respawn on chassis change)
    std::map<std::string, int> previewBodies_;                  // posed body handles per (glTF, slot)
    render::MeshData posed_;
    std::map<std::string, render::MeshData> previewMeshes_;   // per content glTF, loaded once
};

} // namespace core
