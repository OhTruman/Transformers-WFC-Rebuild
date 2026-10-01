// Clean-room reconstruction — objective (scaffold).
#pragma once
#include "core/Math.h"

namespace game {

struct Objective {
    const char* name = "PlaceholderObjective";
    core::Vec3 location{0, 0, 0};
    bool completed = false;
    float progress = 0.0f;   // 0..1
};

} // namespace game
