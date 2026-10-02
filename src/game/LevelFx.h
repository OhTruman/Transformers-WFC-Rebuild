// Clean-room reconstruction — authored level particle emitters of MP_IAC_Streets (map_fx.json).
//
// [CONF] 8 Emitter actors (MP_IAC_Streets_ART_m, incl. the prefab vent Emitter_5707), auto-activating
// ParticleSystemComponents with Template FX_Level_Generic_p.FX.Steam_Sm_FX: one sprite emitter
// "Smoke_Dup" (SERM_Octagon, MaxPeakCount 10), material FX_Materials_p.Materials.Steam_Mat (translucent,
// unlit; emissive = desaturate(SmokeBall_CLR x vertex colour) x 0.5, opacity = vertex alpha x SmokeBall,
// depth-biased alpha 200 UU), LODMethod DirectSet (LOD 0 unless code selects another).
// LOD 0 values are CONF from the compiled module stream; the module ROLES follow the order used for
// every other decoded WFC effect [MED]: SpawnRate U[2,3]/s, Lifetime U[1,2] s, StartRotation U[0,1]
// turn, AlphaOverLife 0 -> 0.3 -> 0, StartSize U[600,1000] UU, SizeMultLife 1 -> 3, Velocity
// U[(-300,-100,-100),(300,100,100)] UU/s, Location U[(-500,0,0),(500,0,0)] UU, ColorOverLife
// (0.9,0.9,1) -> 1 (emitter-local axes). Unassigned stream entries: a constant 100, constants
// 0 / 300 / 0 / 0, and a Z-only uniform U[5,10] (documented, not applied).
// Not reproduced (Rendering): the panned SmokeTile UV distortion and the depth-biased (soft) alpha.
#pragma once
#include <string>
#include <vector>
#include "core/Math.h"
#include "render/Renderer.h"

namespace game {

class LevelFx {
public:
    bool load(render::IRenderer& r, const std::string& mapFxJson, const std::string& contentRoot);
    void tick(float dt);
    void draw(render::IRenderer& r) const;
    size_t liveParticles() const { return parts_.size(); }
    int emitterCount() const { return (int)emitters_.size(); }

private:
    struct Emitter { core::Vec3 pos, fwd, right, up; float spawnAcc = 0.0f; };
    struct Part {
        core::Vec3 pos, vel;
        float age, life, size, rot;
    };
    std::vector<Emitter> emitters_;
    std::vector<Part> parts_;
    render::TextureHandle tex_ = render::kInvalidTexture;
    mutable std::vector<render::Particle> batch_;
};

} // namespace game
