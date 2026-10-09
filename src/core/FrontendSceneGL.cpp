#include "core/FrontendSceneGL.h"
#include "core/FrameProfile.h"

#include <chrono>
#include "assets/Gltf.h"
#include "core/Config.h"
#include "core/LoadYield.h"
#include "core/Log.h"
#include "platform/Image.h"
#include "render/Camera.h"

#include <cmath>
#include <cstdlib>
#include "core/HeapTrim.h"
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
template <class R> void nativeMaterialParams(R* r, const frontend::SceneView& v) {
    if constexpr (HasMaterialParam<R>::value) { for (const auto& m : v.materialParams) r->setFrontendMaterialParam(m.actor, m.param, (float)m.value); }
    else { (void)r; (void)v; }
}
template <class R> void nativeScales(R* r, const frontend::SceneView& v) {
    nativeMaterialParams(r, v);
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
template <class R> void nativeReleaseBody(R& r, int h) {
    if constexpr (HasReleaseBody<R>::value) { if (h >= 0) r.releasePreviewBody(h); }
    else { (void)r; (void)h; }
}
template <class R> bool nativePosedBody(R& r, const FrontendSceneGL::PreviewSlot& s, int slot, bool vehicle, double t,
                                         std::map<std::string, FrontendSceneGL::CachedBody>& bodies, uint64_t& clock,
                                         const std::vector<FrontendSceneGL::PreviewSlot>& shown, render::MeshData& out) {
    if constexpr (HasPreviewBody<R>::value) {
        if (vehicle) return false;
        std::string key = s.gltf + "#" + std::to_string(slot);
        auto it = bodies.find(key);
        if (it == bodies.end()) {
            // LRU cap: evict the least recently used body that no slot shows now (released when the renderer can).
            while (bodies.size() >= FrontendSceneGL::kMaxPreviewBodies) {
                auto victim = bodies.end();
                for (auto b = bodies.begin(); b != bodies.end(); ++b) {
                    bool inUse = false;
                    for (size_t i = 0; i < shown.size(); ++i) inUse = inUse || b->first == shown[i].gltf + "#" + std::to_string(i);
                    if (!inUse && (victim == bodies.end() || b->second.lastUse < victim->second.lastUse)) victim = b;
                }
                if (victim == bodies.end()) break;
                nativeReleaseBody(r, victim->second.handle);
                bodies.erase(victim);
            }
            FrontendSceneGL::CachedBody cb;
            core::prof::Scope prof("preview.body");
            cb.handle = r.loadPreviewBody(s.gltf, s.animSets, "Cust_Idle");
            it = bodies.emplace(key, cb).first;
        }
        it->second.lastUse = ++clock;
        return it->second.handle >= 0 && r.posePreviewBody(it->second.handle, (float)t, out) && !out.empty();
    } else { (void)r; (void)s; (void)slot; (void)vehicle; (void)t; (void)bodies; (void)clock; (void)shown; (void)out; return false; }
}
template <class R> void nativePreviewDraw(R& r, const FrontendSceneGL::PreviewSlot& s, int slot, bool vehicle, double t,
                                          std::map<std::string, render::MeshData>& cache,
                                          std::map<std::string, FrontendSceneGL::CachedBody>& bodies, uint64_t& clock,
                                          const std::vector<FrontendSceneGL::PreviewSlot>& shown, render::MeshData& posed) {
    if constexpr (HasPreviewDraw<R>::value) {
        const render::MeshData* mesh = nullptr;
        if (nativePosedBody(r, s, slot, vehicle, t, bodies, clock, shown, posed)) mesh = &posed;
        else {
            const std::string& g = vehicle && !s.vehicleGltf.empty() ? s.vehicleGltf : s.gltf;
            auto it = cache.find(g);
            if (it == cache.end()) {
                render::MeshData m;
                core::prof::Scope prof("preview.mesh");
            if (!r.loadContentMesh(g, m)) LOG_WARN("frontend preview: %s did not load", g.c_str());
                it = cache.emplace(g, std::move(m)).first;
            }
            mesh = &it->second;
        }
        if (mesh->empty()) return;
        render::CharacterColors cc;
        for (int i = 0; i < 3; ++i) { cc.primary[i] = s.primary[i]; cc.secondary[i] = s.secondary[i]; cc.energon[i] = 0; }
        r.setDrawOwner(1 + slot);
        r.setCharacterColors(cc);
        r.drawDynamicMesh(*mesh, r.actorMatrix(Vec3{s.pos[0], s.pos[1], s.pos[2]}, Vec3{0, s.yawDeg, 0}), Vec3{1, 1, 1});
        r.setDrawOwner(0);
    } else { (void)r; (void)s; (void)slot; (void)vehicle; (void)t; (void)cache; (void)bodies; (void)clock; (void)shown; (void)posed; }
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

template <class R> void nativeGround(R* r, FrontendSceneGL::PreviewSlot& s) {
    if constexpr (HasGroundHeight<R>::value) {
        float g = 0;
        if (r && r->sceneGroundHeight(s.pos[0], s.pos[1], s.pos[2], g)) s.pos[2] = g;
    } else { (void)r; (void)s; }
}

template <class R> int nativeBodyCount(const R* r) {
    if constexpr (HasBodyCount<R>::value) { return r ? (int)r->previewBodyCount() : -1; }
    else { (void)r; return -1; }
}

static double previewClock() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void FrontendSceneGL::setPreview(std::vector<PreviewSlot> slots) {
    for (size_t i = 0; i < slots.size() && i < 2; ++i) {
        nativeGround(r_, slots[i]);
        bool respawn = i >= preview_.size() || preview_[i].gltf != slots[i].gltf;   // UpdateSinglePreviewCharacter ChassisChanged
        if (respawn) { previewVehicle_[i] = false; previewSpawn_[i] = previewClock(); }
    }
    preview_ = std::move(slots);
}

void FrontendSceneGL::transformPreview(bool toRobotOnly) {
    for (size_t i = 0; i < preview_.size() && i < 2; ++i) {
        if (previewHidden_[i]) continue;
        bool v = toRobotOnly ? false : !previewVehicle_[i];
        if (v && preview_[i].vehicleGltf.empty()) return;   // no vehicle mesh exported for this chassis
        if (v != previewVehicle_[i]) LOG_INFO("FLOW preview.form slot=%zu form=%s", i, v ? "vehicle" : "robot");
        previewVehicle_[i] = v;
        return;
    }
}

FrontendSceneGL::PreviewStats FrontendSceneGL::previewStats() const {
    PreviewStats st;
    st.slots = (int)preview_.size();
    for (size_t i = 0; i < preview_.size() && i < 2; ++i) { st.visible += previewHidden_[i] ? 0 : 1; st.vehicles += previewVehicle_[i] ? 1 : 0; }
    st.meshes = (int)previewMeshes_.size();
    st.bodies = (int)previewBodies_.size();
    st.rendererBodies = nativeBodyCount(r_);
    return st;
}

void FrontendSceneGL::drawPreview(render::IRenderer& r) {
    // Belt and braces (setRenderer drops the cache on a new renderer): never pose handles the renderer no longer holds.
    int held = nativeBodyCount(&r);
    if (held >= 0 && (size_t)held < previewBodies_.size()) {
        LOG_INFO("frontend preview: renderer holds %d posed bodies, %zu cached; cache dropped", held, previewBodies_.size());
        previewBodies_.clear();
    }
    for (size_t i = 0; i < preview_.size(); ++i)
        if (i < 2 && !previewHidden_[i])
            nativePreviewDraw(r, preview_[i], (int)i, previewVehicle_[i], previewClock() - previewSpawn_[i], previewMeshes_, previewBodies_,
                              bodyClock_, preview_, posed_);
}

std::string FrontendSceneGL::familyFor(const std::string& uiLevel) {
    std::string f = uiLevel;
    if (f.size() > 2 && f.compare(f.size() - 2, 2, "_m") == 0) f.resize(f.size() - 2);
    return f;
}

bool FrontendSceneGL::load(const std::vector<std::string>& levels) {
    core::prof::Scope prof("scene.load");
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
        core::trimHeap("frontend scene loaded");   // [Systems] the scene load's temporaries back to the OS (under the loading screen)
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
    // Leaving the room: the preview pawns go with it (a new visit spawns them again: initStreamingLvl -> SPAWN_Char).
    // The posed-body handles stay cached (Rendering owns their lifetime; bounded by the roster).
    preview_.clear();
    previewHidden_[0] = previewHidden_[1] = false;
    previewVehicle_[0] = previewVehicle_[1] = false;
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
