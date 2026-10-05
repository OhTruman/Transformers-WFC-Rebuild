#include "core/FrontendSceneGL.h"
#include "assets/Gltf.h"
#include "core/Config.h"
#include "core/LoadYield.h"
#include "core/Log.h"
#include "platform/Image.h"
#include "render/Camera.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>

namespace core {

namespace {
template <class R> bool nativeLoad(R* r, const std::vector<std::string>& levels) {
    if constexpr (HasFrontendScene<R>::value) return r->loadFrontendScene(levels);
    else { (void)r; (void)levels; return false; }
}
template <class R> void nativeActors(R* r, const frontend::SceneView& v) {
    if constexpr (HasActorTransform<R>::value) {
        for (const auto& a : v.actors)
            r->setFrontendActorTransform(a.actor, Vec3{(float)a.pos[0], (float)a.pos[1], (float)a.pos[2]},
                                         Vec3{(float)a.rot[0], (float)a.rot[1], (float)a.rot[2]});
    } else { (void)r; (void)v; }
}
template <class R> void nativeScales(R* r, const frontend::SceneView& v) {
    if constexpr (HasActorScale<R>::value) { for (const auto& s : v.scales) r->setFrontendActorScale(s.actor, (float)s.drawScale); }
    else { (void)r; (void)v; }
}
template <class R> void nativeDraw(R* r, const frontend::SceneView& v, int w, int h) {
    nativeActors(r, v);
    nativeScales(r, v);
    if constexpr (HasFrontendScene<R>::value)
        r->drawFrontendScene(Vec3{(float)v.pos[0], (float)v.pos[1], (float)v.pos[2]}, Vec3{(float)v.rot[0], (float)v.rot[1], (float)v.rot[2]},
                             (float)v.fov, w, h, v.time);
    else { (void)r; (void)v; (void)w; (void)h; }
}
template <class R> void nativePreviewHook(R* r, FrontendSceneGL* self, bool on) {
    if constexpr (HasPreviewDraw<R>::value) {
        if (on) r->setFrontendSceneDraw([self](R& rr) { self->drawPreview(rr); });
        else r->setFrontendSceneDraw({});
    } else { (void)r; (void)self; (void)on; }
}
template <class R> void nativePreviewDraw(R& r, const FrontendSceneGL::PreviewSlot& s, int slot, std::map<std::string, render::MeshData>& cache) {
    if constexpr (HasPreviewDraw<R>::value) {
        auto it = cache.find(s.gltf);
        if (it == cache.end()) {
            render::MeshData m;
            if (!r.loadContentMesh(s.gltf, m)) LOG_WARN("frontend preview: %s did not load", s.gltf.c_str());
            it = cache.emplace(s.gltf, std::move(m)).first;
        }
        if (it->second.empty()) return;
        render::CharacterColors cc;
        for (int i = 0; i < 3; ++i) { cc.primary[i] = s.primary[i]; cc.secondary[i] = s.secondary[i]; cc.energon[i] = 0; }
        r.setDrawOwner(1 + slot);
        r.setCharacterColors(cc);
        r.drawDynamicMesh(it->second, r.actorMatrix(Vec3{s.pos[0], s.pos[1], s.pos[2]}, Vec3{0, s.yawDeg, 0}), Vec3{1, 1, 1});
        r.setDrawOwner(0);
    } else { (void)r; (void)s; (void)slot; (void)cache; }
}
template <class R> void nativeUnload(R* r) {
    if constexpr (HasFrontendScene<R>::value) r->unloadFrontendScene();
    else (void)r;
}

std::string assetRoot() {
    if (const char* e = std::getenv("WFC_ASSETS")) return e;
    return config::kAssetRootDefault;
}
}

void FrontendSceneGL::drawPreview(render::IRenderer& r) {
    for (size_t i = 0; i < preview_.size(); ++i) nativePreviewDraw(r, preview_[i], (int)i, previewMeshes_);
}

std::string FrontendSceneGL::familyFor(const std::string& uiLevel) {
    std::string f = uiLevel;
    if (f.size() > 2 && f.compare(f.size() - 2, 2, "_m") == 0) f.resize(f.size() - 2);
    return f;
}

bool FrontendSceneGL::load(const std::vector<std::string>& levels) {
    if (!r_ || levels.empty()) return false;
    const std::string family = familyFor(levels.front());   // the persistent level names the family folder
    if (family == family_) return mesh_ != render::kInvalidMesh;
    if (!family_.empty()) {   // another family: the renderer holds one map's render data at a time
        ui::GlCensus::Owned none;
        release(none);
    }
    // Rendering's own frontend-scene presentation (render data, map FX, post) when available.
    if (nativeLoad(r_, levels)) {
        family_ = family;
        native_ = true;
        nativePreviewHook(r_, this, true);
        LOG_INFO("frontend scene: %s presented by the renderer (loadFrontendScene)%s", family.c_str(),
                 HasPreviewDraw<render::IRenderer>::value ? " + preview pawns" : "");
        return true;
    }
    native_ = false;
    if constexpr (HasFrontendScene<render::IRenderer>::value) {
        // The renderer owns frontend scenes and has no render data for this family. The raw world.glb without the
        // original materials (sky dome opaque white, additive rings opaque) is a wrong picture, not the scene:
        // nothing is drawn under the menus until the data is built (playtest 2026-10-04: "malformed background").
        family_ = family;
        mesh_ = render::kInvalidMesh;
        LOG_WARN("frontend scene: %s not drawn: no render data (build it: tools\\render\\build_render_data.ps1 -Map %s)",
                 family.c_str(), family.c_str());
        return false;
    }
    const std::string mapDir = assetRoot() + "/Maps/" + family + "/";
    if (!std::ifstream(mapDir + "world.glb").good()) { family_ = family; mesh_ = render::kInvalidMesh; return false; }
    if (!censusActive_) { census_.begin(); censusActive_ = true; }
    family_ = family;
    bool shaders = r_->loadMapRenderData(family);   // original-material path when Rendering's data covers the family
    render::MeshData mesh;
    if (!assets::loadGlb(mapDir + "world.glb", mesh) || mesh.empty()) return false;
    std::map<std::string, render::TextureHandle> cache;
    auto tex = [&](const std::string& uri) -> render::TextureHandle {
        if (uri.empty()) return render::kInvalidTexture;
        auto c = cache.find(uri);
        if (c != cache.end()) return c->second;
        render::ImageData img;
        render::TextureHandle th = platform::decodeImage(uri, img) ? r_->uploadTexture(img) : render::kInvalidTexture;
        cache[uri] = th;
        loadYield("FrontendScene: texture");
        return th;
    };
    for (render::Material& m : mesh.mats) { m.tex = tex(m.baseColorUri); m.emissiveTexHandle = tex(m.emissiveUri); }
    for (render::SubMesh& sm : mesh.subs)
        if (!sm.lightmapName.empty()) sm.lightmapTex = tex(mapDir + "lightmaps/" + sm.lightmapName + ".png");
    mesh_ = r_->uploadMesh(mesh);
    LOG_INFO("frontend scene: %s bounds (%.1f %.1f %.1f)..(%.1f %.1f %.1f) verts %zu", family.c_str(), mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z, mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z, mesh.vertexCount());
    LOG_INFO("frontend scene: %s loaded (%zu submeshes, %zu textures, render data %s)", family.c_str(), mesh.subs.size(),
             cache.size(), shaders ? "yes" : "no");
    return mesh_ != render::kInvalidMesh;
}

std::string FrontendSceneGL::release(const ui::GlCensus::Owned& keep) {
    if (!r_ || family_.empty()) return "";
    if (native_) { nativePreviewHook(r_, this, false); previewMeshes_.clear(); nativeUnload(r_); native_ = false; family_.clear(); return "renderer unloadFrontendScene"; }
    r_->unloadMapRenderData();
    std::string s = censusActive_ ? census_.release(keep) : std::string();
    censusActive_ = false;
    family_.clear();
    mesh_ = render::kInvalidMesh;
    return s;
}

void FrontendSceneGL::draw(const frontend::SceneView& v, int width, int height) {
    if (!r_ || width <= 0 || height <= 0) return;
    if (native_) { nativeDraw(r_, v, width, height); return; }
    if (mesh_ == render::kInvalidMesh) return;
    // UE (X fwd, Y right, Z up; units) -> the exports' glTF space: 0.01 * (x, z, y).
    const double kPi = 3.14159265358979323846;
    double P = v.rot[0] * kPi / 180, Y = v.rot[1] * kPi / 180;
    double fx = std::cos(P) * std::cos(Y), fy = std::cos(P) * std::sin(Y), fz = std::sin(P);
    Vec3 fwd{(float)fx, (float)fz, (float)fy};
    render::Camera cam;
    cam.pos = {(float)(v.pos[0] * 0.01), (float)(v.pos[2] * 0.01), (float)(v.pos[1] * 0.01)};
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwd.y)));
    cam.yaw = std::atan2(-fwd.x, -fwd.z);
    cam.fovXDeg = (float)v.fov;
    cam.aspect = (float)width / (float)height;
    cam.znear = 0.5f;
    cam.zfar = 50000.0f;
    r_->beginFrame(cam, width, height);
    r_->drawMesh(mesh_, Mat4::identity(), Vec3{1, 1, 1});
    r_->endFrame();
}

} // namespace core
