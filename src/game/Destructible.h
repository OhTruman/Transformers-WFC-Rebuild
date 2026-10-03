// Clean-room reconstruction — WFC static destructible (TnStaticDestructibleActor + HmDestructibleComponent),
// the Streets wall panel DES_IAC_WallPanelSignDSYS_p.WallPanelSign. Provenance: AssetTools 7a69756
// manifests/streets_destructibles.json [CONF AUTHORED DATA unless marked]:
//   piece "Base" (Transform Z -160 UU), state graph:
//     handle 0 intact    InitialHealth 20; -> 1 on HmGenericDamage / HmTouch / HmKismet trigger
//     handle 1 destroyed AutomaticTransitionTime 10 s; -> 2 on HmTimerExpired
//     handle 2 settled   terminal
//   no intermediate damaged state; initial state = handle 0 [HIGH, engine convention].
//   Blueprint DamageAngle 180, AIDamageModifier 1, RadiusDamageModifier 1. No Kismet references.
// The single placed instance (TnStaticDestructibleActor_14465, UE (896, 89968, -352)) lies far outside the
// playable space (~1400 m from the player starts); it stays at its authored location.
// Gameplay owns health/state/transitions and raises one event per transition; meshes, debris, particles and
// cues (Base / Chunk02 / Chunk01 + debris, explosion cue, hum loop) belong to Rendering/Systems.
#pragma once
#include "game/Actor.h"
#include "core/Math.h"

#include <string>
#include <vector>

namespace game {

class CollisionWorld;

struct DestructibleEvent {
    int actor = -1;
    int fromState = 0, toState = 0;
    core::Vec3 pos{0, 0, 0};
};

class Destructible : public Actor {
public:
    Destructible(const std::string& name, const core::Vec3& loc, const core::Vec3& boxMin, const core::Vec3& boxMax, int index)
        : name_(name), boxMin_(boxMin), boxMax_(boxMax), index_(index) { pos_ = loc; }

    static constexpr float kInitialHealth = 20.0f;      // state 0 InitialHealth
    static constexpr float kDestroyedTime = 10.0f;      // state 1 AutomaticTransitionTime

    void tick(World& world, float dt) override;
    // HmGenericDamageDestructionTrigger: damage in state 0 depletes InitialHealth; 0 -> 1 when it reaches 0.
    void applyDamage(World& world, float amount);
    int state() const { return state_; }
    // Piece collision per state (Base mesh while intact; Chunk02 stump once destroyed/settled), registered by
    // the World as moving-collision sets in the pawn and weapon worlds [PROV: per-poly from the render meshes;
    // the pieces' simple hulls were not extracted].
    struct CollisionSet { CollisionWorld* world = nullptr; int intact = -1, broken = -1; };
    void addCollision(const CollisionSet& s) { collision_.push_back(s); applyCollision(); }
    float health() const { return health_; }
    const std::string& name() const { return name_; }
    // World-space damage box (Base mesh bounds at the piece transform) for weapon traces / touch.
    core::Vec3 boxMin() const { return pos_ + boxMin_; }
    core::Vec3 boxMax() const { return pos_ + boxMax_; }

private:
    std::string name_;
    core::Vec3 boxMin_, boxMax_;
    int index_;
    int state_ = 0;
    float health_ = kInitialHealth;
    float stateTime_ = 0.0f;
    void transition(World& world, int to);
    std::vector<CollisionSet> collision_;
    void applyCollision();
};

} // namespace game
