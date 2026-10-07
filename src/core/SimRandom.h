#pragma once
#include <cstdint>

// Simulation random numbers (weapon spread, grenade fuses, match coin flips), separate from presentation (particles, sound
// variations keep std::rand). Presentation runs per rendered frame, so sharing one stream would let the frame rate change the
// simulation; this stream advances only from fixed-step simulation code. World reseeds it at every match launch (WFC_SEED when
// set, else a fixed value), so the same inputs give the same match.
namespace core {

inline uint32_t& simRandState() { static uint32_t s = 0x9e3779b9u; return s; }
inline void simRandSeed(uint32_t v) { simRandState() = v ? v : 0x9e3779b9u; }
inline uint32_t simRandU32() {   // xorshift32
    uint32_t& x = simRandState();
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}
inline float simRand01() { return (float)(simRandU32() >> 8) * (1.0f / 16777215.0f); }   // [0, 1]
inline float simRandSigned() { return simRand01() * 2.0f - 1.0f; }                      // [-1, 1]

}  // namespace core
