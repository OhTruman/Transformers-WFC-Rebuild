// Clean-room reconstruction — Optimus vehicle-form effects (boost afterburners, hover thrusters,
// jump boosters), all from TR_Optimus_VEHDEF_p.OptimusTruckForm [CONF]:
//   BoostFx: BoostSocket_L/R                     -> FX_Navigation_p.bumble_boost_small1_FX
//   HoverFX: HoverBooster_{L,R}{Front,Back,Back2} -> FX_Navigation_p.CarHover_A_01_FX
//   JumpFX:  JumpBoostSocket_{C,R,L}             -> FX_Navigation_p.Jump_FX
// Every emitter of these systems is bUseLocalSpace (particles ride their socket). Values are the
// LOD-0 data decoded from the cooked ParticleSystems (see FIDELITY.md PASS 8/9).
#pragma once
#include <string>
#include <vector>
#include "core/Math.h"
#include "render/Renderer.h"

namespace game {

class VehicleFx {
public:
    enum Socket {
        BoostL, BoostR,
        HoverLBack, HoverRBack, HoverLFront, HoverRFront, HoverLBack2, HoverRBack2,
        JumpC, JumpR, JumpL,
        kSocketCount
    };
    struct SocketDef { const char* name; const char* bone; float rel[16]; };   // glTF, incl. socket scale
    static const SocketDef& socketDef(int s);

    enum System { Boost, Hover, Jump };

    void load(render::IRenderer& r, const std::string& contentRoot);
    // World matrix of each socket for this step (null = unavailable, e.g. robot form).
    void setSocket(int s, const core::Mat4* world);
    // Start a system instance on a socket; returns its id.
    int start(System sys, int socket);
    // Stop spawning; emitters flagged bKillOnDeactivate drop their particles immediately.
    void deactivate(int instance);
    bool alive(int instance) const;
    void tick(float dt);
    void draw(render::IRenderer& r) const;
    size_t liveParticles() const { return parts_.size(); }

    // ---- data model ----
    struct Curve3 { std::vector<core::Vec3> v; core::Vec3 eval(float t) const; };   // uniform over life
    struct Curve1 { std::vector<float> v; float eval(float t) const; };
    struct EmitterDef {
        const char* name;
        int mesh;                       // mesh index, -1 = sprite
        int texture;                    // sprite texture index
        bool velocityAligned;           // sprite: length along velocity
        int burst;                      // particles at emitter start
        float rateMin, rateMax;         // SpawnRate (particles/s), 0 = none
        float delay;                    // EmitterDelay (first loop)
        float duration;                 // EmitterDuration for EmitterLoops 1; 0 = loop forever
        bool killOnDeactivate;
        float lifeMin, lifeMax;
        core::Vec3 sizeMin, sizeMax;    // sprite: (w, h) UU; mesh: scale in UE axes
        core::Vec3 size2Min, size2Max;  // second uniform scale factor (1 = none)
        Curve3 grow;                    // size multiplier over life
        Curve1 alpha;                   // alpha over life
        Curve1 bright;                  // colour brightness multiplier over life
        Curve3 colorLife;               // colour over life (per channel)
        core::Vec3 color;               // linear, may be HDR
        float colorMul;                 // colour scale (e.g. x3)
        float rotMin, rotMax;           // initial rotation (turns)
        float rotRateMin, rotRateMax;   // rotation rate (turns/s)
        core::Vec3 velMin, velMax;      // local velocity (UU/s)
        float locX;                     // local offset along socket X (UU)
    };

private:
    struct Inst { int id; System sys; int socket; float age; bool active; std::vector<float> acc, rate; };
    struct Part {
        const EmitterDef* def; int inst; int socket;
        core::Vec3 pos, vel;            // socket-local, metres (pre socket scale)
        float age, life, rot, rotRate;
        core::Vec3 size;
    };
    void spawn(const EmitterDef& d, const Inst& in);
    const std::vector<const EmitterDef*>& emitters(System s) const;

    std::vector<render::TextureHandle> tex_;
    std::vector<render::MeshHandle> meshes_;
    core::Mat4 sockets_[kSocketCount];
    bool have_[kSocketCount] = {};
    std::vector<Inst> insts_;
    std::vector<Part> parts_;
    int nextId_ = 0;
};

} // namespace game
