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

#include <functional>

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
template <class R, class = void> struct HasLoadYield : std::false_type {};
template <class R>
struct HasLoadYield<R, std::void_t<decltype(std::declval<R&>().setLoadYield(std::declval<std::function<void()>>()))>> : std::true_type {};

class FrontendSceneGL final : public frontend::IFrontendSceneRenderer {
public:
    explicit FrontendSceneGL(render::IRenderer* r) : r_(r) {}
    void setRenderer(render::IRenderer* r) { r_ = r; family_.clear(); mesh_ = render::kInvalidMesh; censusActive_ = false; }
    bool load(const std::vector<std::string>& levels) override;
    void draw(const frontend::SceneView& view, int width, int height) override;
    void unload() override {}   // the family stays loaded while UI levels travel within it; released by release()
    // Before a match loads: the scene's render data and GL objects go (keep = the UI renderer's own objects).
    std::string release(const ui::GlCensus::Owned& keep);
    static std::string familyFor(const std::string& uiLevel);

private:
    render::IRenderer* r_;
    std::string family_;                          // loaded scene family ("" none)
    render::MeshHandle mesh_ = render::kInvalidMesh;
    ui::GlCensus census_;
    bool censusActive_ = false;
    bool native_ = false;                         // Rendering's loadFrontendScene owns the family
};

} // namespace core
