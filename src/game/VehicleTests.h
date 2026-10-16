// Clean-room reconstruction — deterministic vehicle handling measurements (WFC_VEHTEST=1).
#pragma once
#include <string>

namespace game {
void runVehicleTests();
int runChassisTests(const std::string& verticalSliceRoot);
// A boosting (Driving) vehicle on a flat floor at 25 m/s, steered every step by steer(yaw, targetYaw) toward a target errRad off its
// heading: seconds until the error is halved, or -1 if not within 2 s (BOTTEST guards the bot steer sign with it).
float driveSteerHalfTime(float errRad, float (*steer)(float yaw, float targetYaw));
} // namespace game
