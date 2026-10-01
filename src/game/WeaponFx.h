// Clean-room reconstruction — Ion Blaster weapon effects (muzzle flash, tracer, impact squib).
//
// Source of truth: the cooked UE3 ParticleSystems referenced by IonBlaster_WEPMESH [CONF]:
//   MuzzleFlashes[WP_Fire].PSTemplate = FX_AssaultRifle_p.FX.MuzzleFlash_AssaultRifle_FX (socket MuzzleFlash)
//   TracerTemplates[WP_Fire]          = FX_AssaultRifle_p.FX.Tracer_AssaultRifle_FX
//   DefaultSquib                      = FX_IonBlaster_p.FX.Impact_IonBlaster_FX
//   ImpactSquibPercentage 0.6, ImpactSquibMaxCount 5, MaxImpactEffectDistance 2500 UU (TnWeaponMesh CDO)
// Emitter values come from each LOD-0 ParticleLODLevel. WFC cooks the per-particle modules into a
// native stream of UE3 raw distributions (type/op/n/chunk + lookup table); RequiredModule data
// (material, screen alignment, burst, duration, local space) is tagged. See FIDELITY.md for the
// decode and which module-role assignments are inferred.
#pragma once
#include <string>
#include <vector>
#include "core/Math.h"
#include "render/Renderer.h"

namespace game {

class WeaponFx {
public:
    // Loads the original FX textures (ExtractedAssets/content/FX_Textures_p/...).
    void load(render::IRenderer& r, const std::string& contentRoot);

    // `socketWorld`: MuzzleFlash socket world matrix (columns: X = barrel forward).
    void spawnMuzzleFlash(const core::Mat4& socketWorld);
    void spawnTracer(const core::Vec3& muzzle, const core::Vec3& hit);
    // Returns false when the squib was culled (percentage / distance / max-count rules).
    bool spawnImpact(const core::Vec3& pos, const core::Vec3& normal, const core::Vec3& viewPos);

    // `muzzleNow`: current socket transform for local-space emitters (null if holstered).
    void tick(float dt, const core::Mat4* muzzleNow);
    void draw(render::IRenderer& r) const;

    size_t liveParticles() const { return parts_.size(); }
    int liveImpacts() const;

    // One authored emitter, LOD 0, values converted to metres / seconds.
    struct Curve {                  // uniformly sampled over normalized particle life [0,1]
        std::vector<float> v;
        float eval(float t) const;
    };
    struct EmitterDef {
        const char* name;
        int texture;                // index into the effect texture table
        render::ParticleBlend blend;
        bool velocityAligned;
        bool uAlongAxis;            // texture long axis = U
        bool localSpace;            // particles move with the spawning socket
        int burst;
        float lifeMin, lifeMax;
        core::Vec3 sizeMin, sizeMax;    // x = width, y = height/length (m)
        core::Vec3 velMin, velMax;      // effect frame: x = forward (m/s)
        core::Vec3 locMin, locMax;      // effect frame (m)
        core::Vec3 color;               // linear tint
        float colorMul;                 // per-particle brightness (ColorOverLife constant)
        float colorScale;               // SwitchableColorScaleOverLife (team A) -> overbright
        bool randomRot;
        int subH, subV;                 // SubUV sheet (random sub-image)
        Curve alpha, size, colorLife;   // over life (empty = 1)
        float speedStretch;             // >0: length grows with speed (m per m/s), sparks
        float gravity;                  // m/s^2 (down)
    };

private:
    struct Part {
        const EmitterDef* def;
        core::Vec3 pos, vel;            // world, or socket space when def->localSpace
        float age, life;
        float w, h, rot;
        float u0, v0, u1, v1;
        int effect;                     // impact instance id (-1 otherwise)
    };
    struct TracerSmoke { core::Vec3 a, b; float age; };

    void emit(const EmitterDef& d, const core::Mat4& frame, int effect, float lifeCap = 1e9f);

    std::vector<render::TextureHandle> tex_;
    std::vector<Part> parts_;
    std::vector<TracerSmoke> smoke_;
    core::Mat4 muzzleNow_ = core::Mat4::identity();
    bool haveMuzzle_ = false;
    int nextImpact_ = 0;
};

} // namespace game
