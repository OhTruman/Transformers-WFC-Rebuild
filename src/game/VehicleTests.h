// Clean-room reconstruction — deterministic vehicle handling measurements (WFC_VEHTEST=1).
#pragma once
#include <string>

namespace game {
void runVehicleTests();
int runChassisTests(const std::string& verticalSliceRoot);
} // namespace game
