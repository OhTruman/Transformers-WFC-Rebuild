#include "game/LevelFx.h"
#include "assets/Json.h"
#include "core/Log.h"
#include "platform/Image.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace game {
namespace {

constexpr float UU = 0.01f;
constexpr const char* kTemplate = "FX_Level_Generic_p.FX.Steam_Sm_FX";

float frand() { return (float)std::rand() / (float)RAND_MAX; }
float urand(float a, float b) { return a + (b - a) * frand(); }

// AlphaOverLife (float curve, 21 samples over life): 0 -> 0.3 at mid-life -> 0.
const float kAlpha[21] = {0.0f, 0.02222f, 0.07778f, 0.15f, 0.22222f, 0.27778f, 0.3f, 0.29563f, 0.28338f, 0.26458f, 0.24052f,
                          0.21254f, 0.18192f, 0.15f, 0.11808f, 0.08746f, 0.05948f, 0.03542f, 0.01662f, 0.00437f, 0.0f};
// SizeMultLife (vector curve X, 21 samples): 1 -> 3.
const float kSize[21] = {1.0f, 1.0145f, 1.056f, 1.1215f, 1.208f, 1.3125f, 1.432f, 1.5635f, 1.704f, 1.8505f, 2.0f,
                         2.1495f, 2.296f, 2.4365f, 2.568f, 2.6875f, 2.792f, 2.8785f, 2.944f, 2.9855f, 3.0f};

float sample21(const float (&c)[21], float t) {
    float x = core::clampf(t, 0.0f, 1.0f) * 20.0f;
    int i = (int)x;
    if (i >= 20) return c[20];
    return c[i] + (c[i + 1] - c[i]) * (x - (float)i);
}

} // namespace

bool LevelFx::load(render::IRenderer& r, const std::string& path, const std::string& contentRoot) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    assets::Json root;
    if (!assets::Json::parse(ss.str(), root)) return false;
    const assets::Json& comps = root["particle_components"];
    for (size_t i = 0; i < comps.size(); ++i) {
        const assets::Json& c = comps[i];
        if (c["props"]["Template"].asString() != kTemplate || c["owner_class"].asString() != "Emitter") continue;
        const assets::Json& op = c["owner_props"];
        const assets::Json& loc = op["Location"];
        // UE (X, Y, Z) cm -> glTF (X, Z, Y) m, as every other map record (audio.json location_gltf).
        Emitter e;
        e.pos = {loc["X"].asFloat() * UU, loc["Z"].asFloat() * UU, loc["Y"].asFloat() * UU};
        float yaw = op["Rotation"]["Yaw"].asFloat() * (6.2831853f / 65536.0f);
        float pitch = op["Rotation"]["Pitch"].asFloat() * (6.2831853f / 65536.0f);
        // UE actor axes (X forward, Y right, Z up) for yaw/pitch, mapped to glTF (x, z, y).
        core::Vec3 fx{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        core::Vec3 fy{-std::sin(yaw), std::cos(yaw), 0.0f};
        core::Vec3 fz = core::cross(fx, fy);
        e.fwd = {fx.x, fx.z, fx.y}; e.right = {fy.x, fy.z, fy.y}; e.up = {fz.x, fz.z, fz.y};
        emitters_.push_back(e);
    }
    render::ImageData img;
    if (platform::decodeImage(contentRoot + "FX_Textures_p/Textures/SmokeBall_CLR.png", img)) {
        // Translucent smoke: coverage from luminance (as WeaponFx's smoke textures); colour white,
        // the particle colour carries the Steam_Mat emissive.
        for (size_t p = 0; p + 3 < img.rgba.size(); p += 4) {
            uint8_t* px = &img.rgba[p];
            px[3] = (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
            px[0] = px[1] = px[2] = 255;
        }
        tex_ = r.uploadTexture(img);
    }
    LOG_INFO("level fx: %zu %s emitters, texture %s", emitters_.size(), kTemplate, tex_ >= 0 ? "ok" : "missing");
    return !emitters_.empty();
}

void LevelFx::tick(float dt) {
    for (size_t i = 0; i < parts_.size();) {
        Part& p = parts_[i];
        p.age += dt;
        if (p.age >= p.life) { parts_[i] = parts_.back(); parts_.pop_back(); continue; }
        p.pos = p.pos + p.vel * dt;
        ++i;
    }
    for (size_t ei = 0; ei < emitters_.size(); ++ei) {
        Emitter& e = emitters_[ei];
        e.spawnAcc += urand(2.0f, 3.0f) * dt;                          // SpawnRate U[2,3]/s
        while (e.spawnAcc >= 1.0f) {
            e.spawnAcc -= 1.0f;
            // MaxPeakCount 10 never binds: at most 3/s x 2 s = 6 live per emitter.
            Part p;
            p.age = 0.0f;
            p.life = urand(1.0f, 2.0f);                                 // Lifetime
            p.rot = urand(0.0f, 1.0f) * 6.2831853f;                     // StartRotation (turns)
            p.size = urand(600.0f, 1000.0f) * UU;                       // StartSize
            core::Vec3 lv{urand(-300.0f, 300.0f), urand(-100.0f, 100.0f), urand(-100.0f, 100.0f)};   // Velocity (UU/s)
            p.vel = (e.fwd * lv.x + e.right * lv.y + e.up * lv.z) * UU;
            p.pos = e.pos + e.fwd * (urand(-500.0f, 500.0f) * UU);       // Location
            parts_.push_back(p);
        }
    }
}

void LevelFx::draw(render::IRenderer& r) const {
    if (parts_.empty() || tex_ < 0) return;
    batch_.clear();
    for (const Part& p : parts_) {
        float t = p.age / p.life;
        render::Particle o;
        o.pos = p.pos;
        o.w = o.h = p.size * sample21(kSize, t);
        o.rot = p.rot;
        float c = 0.9f + 0.1f * t;                                       // ColorOverLife (0.9,0.9,1) -> 1
        o.r = c * 0.5f; o.g = c * 0.5f; o.b = 0.5f;                      // Steam_Mat emissive x 0.5 [MED]
        o.a = sample21(kAlpha, t);
        batch_.push_back(o);
    }
    r.drawParticles({tex_, render::ParticleBlend::Translucent, 1.0f, batch_.data(), batch_.size()});
}

} // namespace game
