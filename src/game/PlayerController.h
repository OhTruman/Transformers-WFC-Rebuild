// Clean-room reconstruction — bridges neutral input to gameplay intent + camera.
// This is the ONLY place that reads platform::InputFrame; the pawn stays input-agnostic.
#pragma once
#include "platform/Input.h"
#include "render/Camera.h"
#include "game/CharacterMovement.h"

namespace game {

class Character;
class World;

class PlayerController {
public:
    void possess(Character* c) { pawn_ = c; }
    Character* pawn() const { return pawn_; }

    // Consume input for this frame: updates camera orientation and buffers intent.
    void handleInput(const platform::InputFrame& in, float dt);

    // Apply the buffered intent to the possessed pawn for one fixed step.
    void applyToPawn(World& world, float dt);

    // Third-person follow camera positioned behind the pawn.
    void updateCamera(render::Camera& cam) const;

    float camYaw() const { return camYaw_; }
    float camPitch() const { return camPitch_; }
    void setCameraYaw(float y) { camYaw_ = y; }

private:
    Character* pawn_ = nullptr;
    float camYaw_ = 0.0f;
    float camPitch_ = -0.15f;
    MoveIntent intent_;
    bool wantJumpLatched_ = false;
    bool wantFire_ = false;
    bool wantReload_ = false;
};

} // namespace game
