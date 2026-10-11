#pragma once
// Presentation randomness (Systems): std::rand() normally. Under WFC_LOCKSTEP (deterministic captures) each call site passes its own
// state and gets a deterministic LCG with std::rand()'s range (0..32767): the global std::rand() sequence is also consumed by code
// whose call count depends on timing (audio cues deferred until an async bank load completes, ...), which made lockstep frames
// differ once FX that draw from it (muzzle flashes, impacts, vehicle FX, idle-animation picks) were on screen.
#include <cstdlib>

namespace core {

inline int presRand(unsigned& state) {
    static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
    if (!lockstep) return std::rand();
    state = state * 1103515245u + 12345u;
    return (int)((state >> 16) & 0x7FFF);
}

}  // namespace core
