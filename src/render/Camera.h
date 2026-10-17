// Clean-room reconstruction — camera (platform-independent).
#pragma once
#include <cmath>
#include "core/Math.h"

namespace render {

struct Camera {
    core::Vec3 pos{0, 3, 10};
    float yaw = 0.0f;     // radians, around Y
    float pitch = 0.0f;   // radians, around X
    // radians about the view axis, after yaw and pitch (UE Rotator.Roll sense: positive rolls the camera right, so the
    // horizon tilts counter-clockwise on screen); 0 = the yaw / pitch view exactly. Every view matrix comes from view()
    // (main, TAA / motion previous view, RT, shadow fitting, HUD markers); the audio listener keeps yaw / pitch only.
    float roll = 0.0f;
    // WFC authors FOV as HORIZONTAL degrees (UE3 convention). Vertical FOV is derived per aspect.
    float fovXDeg = 75.0f;
    float aspect = 16.0f / 9.0f;
    float znear = 0.1f;
    float zfar = 20000.0f;   // large to encompass full extracted maps (incl. skybox geometry)

    core::Mat4 proj() const {
        float fovX = core::radians(fovXDeg);
        float a = aspect > 0.01f ? aspect : 1.0f;
        float fovY = 2.0f * std::atan(std::tan(fovX * 0.5f) / a);
        return core::Mat4::perspective(fovY, a, znear, zfar);
    }
    core::Mat4 view() const {
        core::Vec3 fwd = core::forwardFromYawPitch(yaw, pitch);
        const core::Mat4 v = core::Mat4::lookAt(pos, pos + fwd, core::Vec3{0, 1, 0});
        return roll != 0.0f ? core::Mat4::rotateZ(roll) * v : v;   // (roll 0: the same matrix, no multiply)
    }
};

} // namespace render
