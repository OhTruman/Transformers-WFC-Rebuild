// Clean-room reconstruction — the defining Transformers mechanic: robot <-> vehicle.
// Placeholder behaviour; authentic timings/handling to be filled from observation later.
#pragma once
#include "core/Config.h"
#include "core/Math.h"

namespace game {

enum class Form { Robot, Vehicle };

struct FormTuning {
    float moveSpeed;
    float accel;
    float jumpSpeed;
    core::Vec3 boxSize;   // full extents of the visual/collision box
    core::Vec3 color;
};

inline FormTuning tuningFor(Form f) {
    if (f == Form::Robot) {
        return FormTuning{
            core::config::kRobotMoveSpeed, core::config::kRobotAccel, core::config::kRobotJumpSpeed,
            // [CONF] capsule: radius 1.75 m -> width 3.5, half-height 2.0 -> full 4.0 m.
            core::Vec3{2.0f * core::config::kPawnRadius, 2.0f * core::config::kPawnHalfHeight,
                       2.0f * core::config::kPawnRadius},
            core::Vec3{0.85f, 0.30f, 0.25f}}; // red-ish
    }
    return FormTuning{
        core::config::kVehicleMoveSpeed, core::config::kVehicleAccel, core::config::kVehicleJumpSpeed,
        core::Vec3{1.4f, 0.9f, 2.6f}, core::Vec3{0.25f, 0.45f, 0.85f}};     // low, long, blue-ish
}

inline const char* formName(Form f) { return f == Form::Robot ? "ROBOT" : "VEHICLE"; }

} // namespace game
