#include "game/VehicleBoostFx.h"
#include "assets/Gltf.h"
#include "platform/Image.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {
namespace {

using core::Vec3;
using ED = VehicleBoostFx::EmitterDef;

enum Tex { kSphereGlow, kTexCount };
const char* kTexPaths[kTexCount] = {
    "FX_Textures_p/Textures/SphereGlow_01_CLR.png",   // Basic_Particle_Add_MAT TextureParam
};
// Mesh particles: mesh + the diffuse texture its additive material samples.
struct MeshAsset { const char* gltf; const char* texture; };
enum Mesh { kBoosterCone, kBulletCone, kMeshCount };
const MeshAsset kMeshes[kMeshCount] = {
    {"FX_Navigation_p/Boostermesh_02_STATMESH.gltf", "FX_Textures_p/Textures/LightBeam_Falloff_01_CLR.png"}, // Boostermaterial_02_MAT
    {"FX_Navigation_p/bulletshape_fx_STAT.gltf", "FX_Textures_p/Textures/SphereGlow_01_CLR.png"},            // bumble_boostcone_MAT
};

// Alpha over life shared by every emitter: 0 -> 1 at 20 % of life, then linear to 0.
const std::vector<float> kAlpha = {0, 0.25f, 0.5f, 0.75f, 1, 0.9375f, 0.875f, 0.8125f, 0.75f, 0.6875f, 0.625f,
                                   0.5625f, 0.5f, 0.4375f, 0.375f, 0.3125f, 0.25f, 0.1875f, 0.125f, 0.0625f, 0};
// "thruster" size over life (uniform curve, min == max): 0 -> 2.06 at half life -> back down.
std::vector<Vec3> thrusterGrow() {
    const float x[21] = {0, 0.2079f, 0.4158f, 0.6237f, 0.8316f, 1.0395f, 1.2474f, 1.4553f, 1.6632f, 1.8711f, 2.0648f,
                         1.9584f, 1.8519f, 1.7454f, 1.6389f, 1.5324f, 1.4259f, 1.3195f, 1.2130f, 1.1065f, 1.0f};
    const float y[21] = {0, 0.2009f, 0.4018f, 0.6027f, 0.8036f, 1.0045f, 1.2054f, 1.4063f, 1.6072f, 1.8081f, 1.9955f,
                         1.8960f, 1.7964f, 1.6969f, 1.5973f, 1.4978f, 1.3982f, 1.2987f, 1.1991f, 1.0996f, 1.0f};
    std::vector<Vec3> v;
    for (int i = 0; i < 21; ++i) v.push_back({x[i], y[i], y[i]});
    return v;
}
// "Cone_thrust_Dup" size over life (full 21-entry table).
const std::vector<Vec3> kConeGrow = {
    {1.134f, 0.8613f, 0.866f}, {1.1587f, 0.8675f, 0.894f}, {1.2256f, 0.8844f, 0.9696f}, {1.3239f, 0.909f, 1.0803f},
    {1.443f, 0.9387f, 1.2134f}, {1.5721f, 0.9706f, 1.3564f}, {1.7004f, 1.0019f, 1.4968f}, {1.8172f, 1.0297f, 1.6218f},
    {1.9117f, 1.0514f, 1.7191f}, {1.9732f, 1.0641f, 1.7759f}, {2.0182f, 1.0861f, 1.7754f}, {2.2426f, 1.2722f, 1.6818f},
    {2.643f, 1.6152f, 1.5095f}, {3.1656f, 2.0672f, 1.2823f}, {3.7561f, 2.5807f, 1.0243f}, {4.3606f, 3.1078f, 0.7593f},
    {4.9249f, 3.6009f, 0.5115f}, {5.3949f, 4.0122f, 0.3048f}, {5.7166f, 4.2939f, 0.1632f}, {5.8358f, 4.3985f, 0.1107f},
    {5.8358f, 4.3985f, 0.1107f}};
// "Particle Emitter_Dup" (ignition glow) size over life, x/y.
const std::vector<Vec3> kGlowGrow = {
    {2.5216f, 1.4231f, 1}, {2.525f, 1.4186f, 1}, {2.5339f, 1.4092f, 1}, {2.5466f, 1.4016f, 1}, {2.5613f, 1.4021f, 1},
    {2.5764f, 1.4171f, 1}, {2.5899f, 1.4533f, 1}, {2.6002f, 1.5169f, 1}, {2.6056f, 1.6146f, 1}, {2.6042f, 1.7527f, 1},
    {2.5944f, 1.9377f, 1}, {2.5743f, 2.1761f, 1}, {2.5422f, 2.4744f, 1}, {2.3981f, 2.8487f, 1}, {2.0921f, 3.2423f, 1},
    {1.6806f, 3.6325f, 1}, {1.2194f, 3.9984f, 1}, {0.7638f, 4.3192f, 1}, {0.3696f, 4.5741f, 1}, {0.0923f, 4.7424f, 1},
    {0.0f, 4.8031f, 1}};
// "Particle Emitter_Dup_Dup" (looping glow) size over life 1 -> 3.08.
const std::vector<Vec3> kLoopGlowGrow = {{1, 1, 1}, {3.0576f, 3.0791f, 3.0791f}};

const Vec3 kOrangeHdr{5.0f, 1.5f, 0.05f};   // Cone_thrust / ignition glow colour, x ColorScale 3

const ED kThruster = {"thruster", kBoosterCone, -1, 3, 0, 0, 0, 0.3f, 0.5f,
    {4, 1, 2}, {5, 1, 1}, {1, 1, 1}, {1, 1, 1}, {thrusterGrow()}, {kAlpha}, {1.0f, 0.6f, 0.05f}, 0.98f,
    0, 0, 0, 0, 5};
const ED kConeThrust = {"Cone_thrust_Dup", kBulletCone, -1, 1, 0, 0, 0, 0.3f, 0.4f,
    {50, 37.5f, 15}, {50, 37.5f, 15}, {1, 1, 1}, {1, 1, 1}, {kConeGrow}, {kAlpha}, kOrangeHdr, 3.0f,
    0, 0, 0, 0, 15};
const ED kIgnitionGlow = {"Particle Emitter_Dup", -1, kSphereGlow, 1, 0, 0, 0, 0.3f, 0.4f,
    {30, 30, 30}, {30, 30, 30}, {1, 1, 1}, {1, 1, 1}, {kGlowGrow}, {kAlpha}, kOrangeHdr, 3.0f,
    -0.5f, 0.5f, 0, 0, 25};
const ED kLoopCone = {"loopcone", kBoosterCone, -1, 0, 3.0f, 4.0f, 0.1f, 1.0f, 1.0f,
    {4, 1, 2}, {5, 1, 1}, {1, 1, 1}, {1.1f, 1.1f, 1}, {}, {kAlpha}, {1.0688f, 0.5922f, 0.1586f}, 1.0f,
    0, 0, 0, 0, 5};
const ED kLoopGlow = {"Particle Emitter_Dup_Dup", -1, kSphereGlow, 0, 20.0f, 20.0f, 0.2f, 0.4f, 0.6f,
    {30, 30, 1}, {30, 30, 1}, {1, 1, 1}, {1, 1, 1}, {kLoopGlowGrow}, {kAlpha}, {0.8f, 0.8f, 0.3f}, 1.0f,
    -1.0f, 1.0f, -0.75f, 0.75f, 25};

const ED* kIgnition[] = {&kThruster, &kConeThrust, &kIgnitionGlow};
const ED* kLooping[] = {&kLoopCone, &kLoopGlow};

constexpr float UU = 0.01f;
float frand() { return (float)std::rand() / (float)RAND_MAX; }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
Vec3 lerpRand(const Vec3& a, const Vec3& b) { float t = frand(); return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)}; }
Vec3 col(const core::Mat4& m, int c) { return {m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]}; }

core::Mat4 orthonormal(const core::Mat4& m) {
    core::Mat4 r = m;
    for (int c = 0; c < 3; ++c) {
        Vec3 v = core::normalize(col(m, c));
        r.m[c * 4] = v.x; r.m[c * 4 + 1] = v.y; r.m[c * 4 + 2] = v.z;
    }
    return r;
}

// HDR colour -> (clamped base colour, GL overbright 1/2/4).
void hdr(const Vec3& c, float mul, float& r, float& g, float& b, float& scale) {
    float m = std::max(c.x, std::max(c.y, c.z)) * mul;
    scale = m >= 4.0f ? 4.0f : (m >= 2.0f ? 2.0f : 1.0f);
    r = std::min(1.0f, c.x * mul / scale); g = std::min(1.0f, c.y * mul / scale); b = std::min(1.0f, c.z * mul / scale);
}

} // namespace

Vec3 VehicleBoostFx::Curve3::eval(float t) const {
    if (v.empty()) return {1, 1, 1};
    if (v.size() == 1) return v[0];
    float x = core::clampf(t, 0.0f, 1.0f) * (float)(v.size() - 1);
    size_t i = (size_t)x;
    if (i + 1 >= v.size()) return v.back();
    float u = x - (float)i;
    return v[i] + (v[i + 1] - v[i]) * u;
}
float VehicleBoostFx::Curve1::eval(float t) const {
    if (v.empty()) return 1.0f;
    float x = core::clampf(t, 0.0f, 1.0f) * (float)(v.size() - 1);
    size_t i = (size_t)x;
    if (i + 1 >= v.size()) return v.back();
    return v[i] + (v[i + 1] - v[i]) * (x - (float)i);
}

void VehicleBoostFx::load(render::IRenderer& r, const std::string& contentRoot) {
    tex_.assign(kTexCount, render::kInvalidTexture);
    for (int i = 0; i < kTexCount; ++i) {
        render::ImageData img;
        if (platform::decodeImage(contentRoot + kTexPaths[i], img)) tex_[(size_t)i] = r.uploadTexture(img);
    }
    meshes_.assign(kMeshCount, render::kInvalidMesh);
    int ok = 0;
    for (int i = 0; i < kMeshCount; ++i) {
        render::MeshData md;
        if (!assets::loadGlb(contentRoot + kMeshes[i].gltf, md)) continue;
        render::ImageData img;
        render::TextureHandle th = render::kInvalidTexture;
        if (platform::decodeImage(contentRoot + kMeshes[i].texture, img)) th = r.uploadTexture(img);
        for (render::Material& m : md.mats) { m.tex = th; m.color = {1, 1, 1}; }
        meshes_[(size_t)i] = r.uploadMesh(md);
        if (meshes_[(size_t)i] != render::kInvalidMesh) ++ok;
    }
    LOG_INFO("vehicle boost fx: %d/%d meshes, sprite texture %s", ok, (int)kMeshCount,
             tex_[0] >= 0 ? "ok" : "missing");
}

void VehicleBoostFx::spawn(const EmitterDef& d, int socket) {
    Part p{};
    p.def = &d;
    p.socket = socket;
    p.age = 0.0f;
    p.life = lerp(d.lifeMin, d.lifeMax, frand());
    Vec3 s1 = lerpRand(d.sizeMin, d.sizeMax), s2 = lerpRand(d.size2Min, d.size2Max);
    p.size = {s1.x * s2.x, s1.y * s2.y, s1.z * s2.z};
    p.rot = lerp(d.rotMin, d.rotMax, frand()) * 2.0f * core::PI;
    p.rotRate = lerp(d.rotRateMin, d.rotRateMax, frand()) * 2.0f * core::PI;
    parts_.push_back(p);
}

void VehicleBoostFx::tick(float dt, bool active, const core::Mat4* sockets[2]) {
    for (int i = 0; i < 2; ++i) {
        haveSocket_[i] = sockets && sockets[i];
        if (haveSocket_[i]) sockets_[i] = orthonormal(*sockets[i]);
    }
    bool can = active && haveSocket_[0] && haveSocket_[1];
    if (can && !active_) {                           // activation: ignition burst + looping emitters
        for (int s = 0; s < 2; ++s) {
            for (const ED* d : kIgnition) for (int k = 0; k < d->burst; ++k) spawn(*d, s);
            for (const ED* d : kLooping) loops_.push_back({d, s, -d->delay, 0.0f, lerp(d->rateMin, d->rateMax, frand())});
        }
    } else if (!can && active_) {                    // deactivation: bKillOnDeactivate
        parts_.clear();
        loops_.clear();
    }
    active_ = can;
    if (!active_) return;

    for (Loop& l : loops_) {
        l.t += dt;
        if (l.t < 0.0f) continue;                    // EmitterDelay (first loop only)
        l.acc += l.rate * dt;
        while (l.acc >= 1.0f) { l.acc -= 1.0f; spawn(*l.def, l.socket); }
    }
    for (size_t i = 0; i < parts_.size();) {
        Part& p = parts_[i];
        p.age += dt;
        if (p.age >= p.life) { parts_[i] = parts_.back(); parts_.pop_back(); continue; }
        p.rot += p.rotRate * dt;
        ++i;
    }
}

void VehicleBoostFx::draw(render::IRenderer& r) const {
    if (!active_) return;
    std::vector<render::Particle> q;
    for (const ED* d : {&kLoopGlow, &kIgnitionGlow}) {
        q.clear();
        float br = 1, bg = 1, bb = 1, scale = 1;
        hdr(d->color, d->colorMul, br, bg, bb, scale);
        for (const Part& p : parts_) {
            if (p.def != d || !haveSocket_[p.socket]) continue;
            float t = p.age / p.life;
            const core::Mat4& S = sockets_[p.socket];
            Vec3 g = d->grow.eval(t);
            render::Particle o;
            o.pos = core::transformPoint(S, Vec3{d->locX * UU, 0, 0});
            o.w = p.size.x * g.x * UU;
            o.h = p.size.y * g.y * UU;
            o.rot = p.rot;
            o.r = br; o.g = bg; o.b = bb;
            o.a = d->alpha.eval(t);
            q.push_back(o);
        }
        if (!q.empty()) r.drawParticles({tex_[(size_t)d->texture], render::ParticleBlend::Additive, scale, q.data(), q.size()});
    }
    for (const Part& p : parts_) {
        const ED* d = p.def;
        if (d->mesh < 0 || !haveSocket_[p.socket]) continue;
        render::MeshHandle h = meshes_[(size_t)d->mesh];
        if (h == render::kInvalidMesh) continue;
        float t = p.age / p.life;
        Vec3 g = d->grow.eval(t);
        float br, bg, bb, scale;
        hdr(d->color, d->colorMul, br, bg, bb, scale);
        // Mesh frame = socket frame (mesh +X = socket +X = emission axis), offset along X.
        core::Mat4 model = sockets_[p.socket] * core::Mat4::translate(Vec3{d->locX * UU, 0, 0}) *
                           core::Mat4::scale(Vec3{p.size.x * g.x, p.size.y * g.y, p.size.z * g.z});
        r.drawMeshFx(h, model, br, bg, bb, d->alpha.eval(t), scale);
    }
}

} // namespace game
