// Clean-room reconstruction — spawn point (scaffold).
#pragma once
#include "core/Math.h"
#include "game/Team.h"

namespace game {

struct SpawnPoint {
    core::Vec3 pos{0, 0, 0};
    float yaw = 0.0f;
    Team team = Team::None;
};

} // namespace game
