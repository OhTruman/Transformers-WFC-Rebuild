// Clean-room reconstruction — the Realistic defaults (PC ADAPTATION): one accessor per switch, the only copy of each
// default, read once from the environment. The features use these and the "RT Realistic defaults" log line prints them,
// so Systems' verify_slim gate reads exactly the values in effect (no second copy that could drift).
#pragma once
#include <algorithm>
#include <cstdlib>

namespace render {
namespace wfc {
namespace rtdef {
inline float envNum(const char* k, float d) { const char* e = std::getenv(k); return e ? (float)std::atof(e) : d; }
inline bool envOn(const char* k) { const char* e = std::getenv(k); return !(e && e[0] == '0'); }

inline bool tone() { static const bool v = envOn("WFC_RT_TONE"); return v; }                       // X1 tone curve
inline bool lmSpec() { static const bool v = envOn("WFC_RT_LMSPEC"); return v; }                   // X2 on
inline float lmSpecGain() { static const float v = envNum("WFC_RT_LMSPECGAIN", 2.5f); return v; }  // X2 gain
inline float tlGain() { static const float v = std::max(0.0f, envNum("WFC_RT_TLGAIN", 2.0f)); return v; }   // row 4
inline float giSat() { static const float v = std::max(0.0f, envNum("WFC_RT_GISAT", 2.5f)); return v; }     // X4
inline float giEmissive() { static const float v = std::max(0.0f, envNum("WFC_RT_GIEMISSIVE", 4.0f)); return v; }   // Molten a
inline float charAoRadius() { static const float v = envNum("WFC_RT_CHARAORADIUS", 3.0f); return v; }   // X5-1
inline bool softParticles() { static const bool v = envOn("WFC_SOFTPARTICLES"); return v; }
inline float softParticlesM() { static const float v = std::max(0.01f, envNum("WFC_SOFTPARTICLES_M", 0.5f)); return v; }
inline bool tiled() { static const bool v = envOn("WFC_TLTILED"); return v; }
inline bool charSun() { static const bool v = envOn("WFC_RT_CHARSUN"); return v; }                  // row 1
inline bool charLampCal() { static const bool v = envOn("WFC_RT_CHARLAMPCAL"); return v; }          // row 8
inline bool reflTl() { static const bool v = envOn("WFC_RT_REFLTL"); return v; }                    // row 6
inline bool sunShare() { static const bool v = envOn("WFC_RT_SUNSHARE"); return v; }               // row 2
inline bool probeFill() { static const bool v = envOn("WFC_RT_PROBEFILL"); return v; }             // row 5
inline bool origSpec() { static const bool v = envOn("WFC_PBR_ORIGSPEC"); return v; }              // Enhanced spec
inline bool reflTemporal() { static const bool v = envOn("WFC_RT_REFLTEMPORAL"); return v; }       // item 7
}  // namespace rtdef
}  // namespace wfc
}  // namespace render
