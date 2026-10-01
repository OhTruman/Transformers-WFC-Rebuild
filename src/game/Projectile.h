// Clean-room reconstruction — projectile (scaffold).
#pragma once
#include "game/Actor.h"
#include "render/Renderer.h"

namespace game {

class Projectile : public Actor {
public:
    Projectile(const core::Vec3& pos, const core::Vec3& vel, float damage)
        : vel_(vel), damage_(damage) { pos_ = pos; }

    void tick(World&, float dt) override {
        pos_ += vel_ * dt;
        life_ -= dt;
        if (life_ <= 0.0f) destroy();
    }
    void draw(render::IRenderer& r) const override {
        r.drawBox(pos_, core::Vec3{0.2f, 0.2f, 0.2f}, core::Vec3{1.0f, 0.9f, 0.3f});
    }
    float damage() const { return damage_; }

private:
    core::Vec3 vel_;
    float damage_ = 0.0f;
    float life_ = 3.0f;
};

} // namespace game
