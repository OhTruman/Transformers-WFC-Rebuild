// Clean-room reconstruction — base spatial entity.
// Loosely mirrors the Unreal Actor concept: a thing with a transform that ticks.
#pragma once
#include "core/Math.h"

namespace render { class IRenderer; }

namespace game {

class World;

class Actor {
public:
    virtual ~Actor() = default;

    virtual void tick(World& world, float dt) { (void)world; (void)dt; }
    virtual void draw(render::IRenderer& r) const { (void)r; }

    const core::Vec3& position() const { return pos_; }
    void setPosition(const core::Vec3& p) { pos_ = p; }
    float yaw() const { return yaw_; }
    void setYaw(float y) { yaw_ = y; }

    bool alive() const { return alive_; }
    void destroy() { alive_ = false; }

protected:
    core::Vec3 pos_{0, 0, 0};
    float yaw_ = 0.0f;
    bool alive_ = true;
};

} // namespace game
