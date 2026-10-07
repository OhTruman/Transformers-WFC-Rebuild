// Clean-room reconstruction — a non-local match participant: a full pawn (Character) with its own Match player slot.
// It is bot-ready architecture, not AI: nothing decides its inputs except whoever calls setIntent() (harnesses today, a
// future controller - see docs/handoffs/GAMEPLAY_BOT_READINESS.md; any decision logic will be a RECONSTRUCTION EXTENSION).
// Owned by World; spawns / dies through the real Match flow. At spawn World applies the participant's resolved chassis
// (TnPawn.ApplyTransformer: body, collision, stats), specialty (health, speed) and loadout, exactly as for the local pawn,
// and the pawn then moves with the same CharacterMovement code against the same collision.
#pragma once
#include "game/Actor.h"
#include "game/Character.h"
#include "game/CharacterMovement.h"
#include "game/Health.h"
#include "render/Renderer.h"

namespace game {

class CollisionWorld;

class MatchOpponent : public Actor {
public:
    MatchOpponent(int matchPlayer, int team, bool drawn) : player_(matchPlayer), team_(team), drawn_(drawn) {}
    int matchPlayer() const { return player_; }
    int team() const { return team_; }
    Character& pawn() { return pawn_; }
    const Character& pawn() const { return pawn_; }
    Health& health() { return pawn_.health(); }
    bool spawned() const { return spawned_; }
    // RestartPlayer: a fresh pawn at the start (chassis / specialty / loadout already applied by World).
    void spawnAt(const core::Vec3& p) {
        pawn_.respawnReset();
        pawn_.setPosition(p); pawn_.groundY = p.y; pawn_.velocity() = {0, 0, 0};
        pos_ = p; spawned_ = true; intent_ = MoveIntent{};
    }
    void despawn() { spawned_ = false; }
    // DIAGNOSTIC participant input: holds the contextual pickup button (no AI decides this).
    bool pressesPickup = true;
    void setPosition(const core::Vec3& p) { pos_ = p; pawn_.setPosition(p); pawn_.velocity() = {0, 0, 0}; }
    // Inputs for the next simulation steps (MoveIntent, the same contract PlayerController produces).
    void setIntent(const MoveIntent& in) { intent_ = in; }
    const MoveIntent& intent() const { return intent_; }
    // One fixed step: movement (shared CharacterMovement) + animation. A participant with no intent stands still.
    void simulate(float dt, const CollisionWorld* col) { simulateMovement(dt, col); simulateAnimation(dt); }
    void simulateMovement(float dt, const CollisionWorld* col) {
        if (!spawned_) return;
        CharacterMovement::update(pawn_, intent_, dt, col);
        intent_.wantJump = false; intent_.wantDash = false; intent_.dodgeDir = 0;   // edge inputs are consumed
        pos_ = pawn_.position();
    }
    void simulateAnimation(float dt) { if (spawned_) pawn_.updateAnimation(dt); }
    // Animation level of detail (World, extended matches): sample the pose every N steps with the accumulated time.
    void simulateAnimationLod(float dt, int every) {
        if (!spawned_) return;
        animAccum_ += dt;
        if (++animStep_ % every != 0 && every > 1) return;
        pawn_.updateAnimation(animAccum_); animAccum_ = 0.0f;
    }
    // Collision cylinder of the current form (robot: ROBODEF radius / height; vehicle: CalculateCylinderBounds).
    bool rayHit(const core::Vec3& o, const core::Vec3& d, float range, float& t) const { return spawned_ && pawnRayHit(pawn_, o, d, range, t); }
    // Ray (unit d) against any pawn's collision cylinder of its current form; t = distance to the entry point.
    static bool pawnRayHit(const Character& p, const core::Vec3& o, const core::Vec3& d, float range, float& t) {
        const Form f = p.moveForm();
        const float r = p.cylinderRadius(f), hh = p.cylinderHalfHeight(f);
        const core::Vec3 c = p.actorLocation();
        float ox = o.x - c.x, oz = o.z - c.z;
        float a = d.x * d.x + d.z * d.z, b = 2.0f * (ox * d.x + oz * d.z), cc = ox * ox + oz * oz - r * r;
        float tt;
        if (a < 1e-8f) { if (cc > 0.0f) return false; tt = 0.0f; }
        else {
            float disc = b * b - 4.0f * a * cc;
            if (disc < 0.0f) return false;
            tt = (-b - std::sqrt(disc)) / (2.0f * a);
            if (tt < 0.0f) tt = 0.0f;
        }
        if (tt > range) return false;
        float y = o.y + d.y * tt;
        if (y < c.y - hh || y > c.y + hh) return false;
        t = tt;
        return true;
    }
    // Off-screen this frame (World::draw, extended matches only): no draw, so no skinning.
    void setCulled(bool c) const { culled_ = c; }
    bool culled() const { return culled_; }
    void draw(render::IRenderer& r) const override {
        if (!spawned_ || culled_) return;
        if (pawn_.currentModel()) { pawn_.draw(r); return; }   // its own chassis body
        if (!drawn_) return;                                    // no body loaded: diagnostic box only on request
        core::Vec3 col = team_ == 0 ? core::Vec3{0.3f, 0.45f, 1.0f} : core::Vec3{1.0f, 0.3f, 0.25f};
        r.drawBox(pos_ + core::Vec3{0, 2.0f, 0}, core::Vec3{2.8f, 4.0f, 2.8f}, col);
    }

private:
    int player_, team_;
    bool drawn_;
    bool spawned_ = false;
    mutable bool culled_ = false;
    float animAccum_ = 0.0f; unsigned animStep_ = 0;
    Character pawn_;
    MoveIntent intent_;
};

} // namespace game
