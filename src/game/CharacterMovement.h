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
    bool  wantBoost = false;   // Boost held (vehicle: Hovering -> Driving)
    bool  wantDash = false;    // Dash edge (VehicleSpecialMove), latched until a step consumes it
    float steer = 0.0f;        // Driving steering input -1..1 (TnPlayerInput.GetNormalizedTurn: look X)
    float viewPitch = 0.0f;    // controller (camera) pitch, radians (jet hover strafes in the full view frame)
    float turnIn = 0.0f, lookUpIn = 0.0f;   // GetNormalizedTurn / GetNormalizedLookUp (jet flight lean)
    bool ascend = false, descend = false;   // jet Hover Up / Down (held)
    int dodgeDir = 0;          // TnAcrobaticsManager dodge request: 1 left, 2 right, 3 forward, 4 back (0 = none)
};

namespace CharacterMovement {
// Integrate one fixed step: horizontal accel toward target velocity, gravity, and collision
// grounding against `col` (falls back to a flat plane at Character::groundY when col is null).
void update(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col);
} // namespace CharacterMovement

} // namespace game
