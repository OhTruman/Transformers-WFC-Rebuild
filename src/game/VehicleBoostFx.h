// Clean-room reconstruction — Optimus vehicle boost presentation.
//
// [CONF] TR_Optimus_VEHDEF_p.OptimusTruckForm (TnTruckFormBlueprint):
//   BoostFx = { BoostSocket_L, BoostSocket_R } x FX_Navigation_p.bumble_boost_small1_FX
//   (sockets on L/R_Robo23_XT, +-35 UU, yaw +-90 deg; WFC_VEH skeleton).
// The ParticleSystem (5 emitters, every one bUseLocalSpace + bKillOnDeactivate) splits into an
// ignition burst (EmitterLoops 1: 3 "thruster" cones, the "Cone_thrust" bullet cone, one glow) and
// emitters that loop while the boost is held ("loopcone" cones at U[3,4]/s after a 0.1 s first-loop
// delay, glow sprites at 20/s after 0.2 s). Deactivation kills every particle immediately.
#pragma once
#include <string>
#include <vector>
#include "core/Math.h"
#include "render/Renderer.h"

namespace game {

class VehicleBoostFx {
public:
    void load(render::IRenderer& r, const std::string& contentRoot);

    // `sockets[i]`: current world matrix of BoostSocket_L / BoostSocket_R (null = unavailable).
    // `active`: boost held in vehicle form. Rising edge = ignition burst; falling edge = kill.
    void tick(float dt, bool active, const core::Mat4* sockets[2]);
    void draw(render::IRenderer& r) const;

    size_t liveParticles() const { return parts_.size(); }
    bool active() const { return active_; }

    struct Curve3 { std::vector<core::Vec3> v; core::Vec3 eval(float t) const; };   // 21 entries over life
    struct Curve1 { std::vector<float> v; float eval(float t) const; };
    struct EmitterDef {
        const char* name;
        int mesh;                       // -1 = sprite
        int texture;                    // sprite texture (meshes carry theirs)
        int burst;                      // ignition burst (EmitterLoops 1)
        float rateMin, rateMax, delay;  // looping spawn rate (0 = none), first-loop delay
        float lifeMin, lifeMax;
        core::Vec3 sizeMin, sizeMax;    // mesh: scale; sprite: x/y size in UU
        core::Vec3 size2Min, size2Max;  // second uniform scale factor (loopcone)
        Curve3 grow;                    // size over life (empty = 1)
        Curve1 alpha;
        core::Vec3 color;               // linear, may be HDR (> 1)
        float colorMul;
        float rotMin, rotMax;           // sprite initial rotation (turns)
        float rotRateMin, rotRateMax;   // sprite rotation rate (turns/s)
        float locX;                     // location along socket X (UU)
    };

private:
    struct Part {
        const EmitterDef* def;
        int socket;
        float age, life, rot, rotRate;
        core::Vec3 size;
    };
    struct Loop { const EmitterDef* def; int socket; float t, acc, rate; };
    void spawn(const EmitterDef& d, int socket);

    std::vector<render::TextureHandle> tex_;
    std::vector<render::MeshHandle> meshes_;
    std::vector<Part> parts_;
    std::vector<Loop> loops_;
    core::Mat4 sockets_[2];
    bool haveSocket_[2] = {false, false};
    bool active_ = false;
};

} // namespace game
