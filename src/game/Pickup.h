// Clean-room reconstruction — pickup (scaffold).
#pragma once
#include "game/Actor.h"
#include "render/Renderer.h"

namespace game {

class Pickup : public Actor {
public:
    enum class Kind { Health, Ammo, Ability };
    explicit Pickup(const core::Vec3& p, Kind k = Kind::Health) : kind_(k) { pos_ = p; }

    void tick(World&, float dt) override { spin_ += dt * 2.0f; yaw_ = spin_; }
    void draw(render::IRenderer& r) const override {
        core::Vec3 c = kind_ == Kind::Health ? core::Vec3{0.2f, 0.9f, 0.3f}
                     : kind_ == Kind::Ammo   ? core::Vec3{0.9f, 0.8f, 0.2f}
                                             : core::Vec3{0.6f, 0.3f, 0.9f};
        r.drawBox(pos_ + core::Vec3{0, 0.5f, 0}, core::Vec3{0.5f, 0.5f, 0.5f}, c, yaw_);
    }
    Kind kind() const { return kind_; }

private:
    Kind kind_;
    float spin_ = 0.0f;
};

} // namespace game
