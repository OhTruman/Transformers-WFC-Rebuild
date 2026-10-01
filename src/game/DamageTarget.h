// Clean-room reconstruction — a simple damageable practice dummy for weapon testing.
#pragma once
#include "game/Actor.h"
#include "render/Renderer.h"

namespace game {

class DamageTarget : public Actor {
public:
    explicit DamageTarget(const core::Vec3& p) { pos_ = p; }

    // Returns true if this hit destroyed the target.
    bool applyDamage(float dmg) {
        hp_ -= dmg;
        hitFlash_ = 0.12f;
        if (hp_ <= 0 && alive_) { alive_ = false; return true; }
        return false;
    }
    float hp() const { return hp_; }
    const core::Vec3& halfExtent() const { return half_; }

    void tick(World&, float dt) override { if (hitFlash_ > 0) hitFlash_ -= dt; }

    void draw(render::IRenderer& r) const override {
        core::Vec3 c = pos_ + core::Vec3{0, half_.y, 0};
        core::Vec3 col = hitFlash_ > 0 ? core::Vec3{1.0f, 0.95f, 0.3f}
                                       : core::Vec3{0.75f, 0.2f, 0.2f};
        r.drawBox(c, core::Vec3{half_.x * 2, half_.y * 2, half_.z * 2}, col);
    }

private:
    float hp_ = 100.0f;
    float hitFlash_ = 0.0f;
    core::Vec3 half_{0.6f, 1.6f, 0.6f};   // ~3.2 m tall dummy
};

} // namespace game
