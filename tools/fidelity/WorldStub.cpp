// Link-time stand-in for the one World method PlayerController::applyToPawn calls.
// The harness never links World.cpp (renderer/audio/asset loading); shots are recorded instead
// so fire cadence and aim direction can be measured.
#include "Rig.h"

namespace fid {
namespace { double gShotClock = 0; }
std::vector<ShotRecord>& shotLog() { static std::vector<ShotRecord> log; return log; }
void setShotClock(double t) { gShotClock = t; }
} // namespace fid

void game::World::fireHitscan(const core::Vec3& origin, const core::Vec3& dir) {
    fid::shotLog().push_back({fid::gShotClock, origin, dir});
}
