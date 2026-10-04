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
std::string assetRoot() {
    if (const char* e = std::getenv("WFC_ASSETS")) return e;
    return config::kAssetRootDefault;
}
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
    r_->unloadMapRenderData();
    std::string s = censusActive_ ? census_.release(keep) : std::string();
    censusActive_ = false;
    family_.clear();
    mesh_ = render::kInvalidMesh;
    return s;
}

void FrontendSceneGL::draw(const frontend::SceneView& v, int width, int height) {
    if (!r_ || mesh_ == render::kInvalidMesh || width <= 0 || height <= 0) return;
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
