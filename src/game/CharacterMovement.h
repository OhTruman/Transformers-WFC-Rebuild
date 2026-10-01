// Clean-room reconstruction — character movement component (platform-independent).
// Reads a neutral MoveIntent; never touches input/rendering/OS APIs.
#pragma once
#include "core/Math.h"

namespace game {

class Character;
class CollisionWorld;

// Per-tick movement request in world space semantics:
//  moveForward/moveRight are -1..1 desired directions relative to faceYaw.
struct MoveIntent {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float faceYaw = 0.0f;   // desired facing (radians), usually camera yaw
    bool  wantJump = false;
    bool  wantBoost = false;   // vehicle dash/boost (Sprint)
};

namespace CharacterMovement {
// Integrate one fixed step: horizontal accel toward target velocity, gravity, and collision
// grounding against `col` (falls back to a flat plane at Character::groundY when col is null).
void update(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col);
} // namespace CharacterMovement

} // namespace game
