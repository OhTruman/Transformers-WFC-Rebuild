#include "game/Destructible.h"
#include "game/Character.h"
#include "game/World.h"
#include "game/Collision.h"

#include <algorithm>

namespace game {

void Destructible::applyCollision() {
    for (const CollisionSet& s : collision_) {
        if (!s.world) continue;
        s.world->setDynamicEnabled(s.intact, state_ == 0);
        s.world->setDynamicEnabled(s.broken, state_ != 0);
    }
}

void Destructible::transition(World& world, int to) {
    DestructibleEvent e;
    e.actor = index_; e.fromState = state_; e.toState = to; e.pos = pos_;
    state_ = to;
    stateTime_ = 0.0f;
    applyCollision();
    world.raiseDestructibleEvent(e);
}

void Destructible::applyDamage(World& world, float amount) {
    if (state_ != 0 || amount <= 0.0f) return;
    health_ = std::max(0.0f, health_ - amount);
    if (health_ <= 0.0f) transition(world, 1);
}

void Destructible::tick(World& world, float dt) {
    stateTime_ += dt;
    if (state_ == 1 && stateTime_ >= kDestroyedTime) { transition(world, 2); return; }   // HmTimerExpired
    if (state_ != 0) return;
    // HmTouchDestructionTrigger: the player pawn's collision cylinder entering the piece box.
    const Character& c = world.player().pawn();
    core::Vec3 a = c.actorLocation();
    float r = c.moveForm() == Form::Robot ? core::config::kPawnRadius : 3.34f;
    float h = c.moveForm() == Form::Robot ? core::config::kPawnHalfHeight : 1.22f;
    core::Vec3 lo = boxMin(), hi = boxMax();
    if (a.x + r < lo.x || a.x - r > hi.x || a.z + r < lo.z || a.z - r > hi.z || a.y + h < lo.y || a.y - h > hi.y) return;
    transition(world, 1);
}

} // namespace game
