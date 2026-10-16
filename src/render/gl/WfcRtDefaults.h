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
// row 2: the most of a pixel's light a character's sun shadow removes (the eye pass: at 1 the shadows were black blobs)
inline float sunShareMax() { static const float v = std::min(1.0f, std::max(0.0f, envNum("WFC_RT_SUNSHAREMAX", 0.75f))); return v; }
inline bool probeFill() { static const bool v = envOn("WFC_RT_PROBEFILL"); return v; }             // row 5
inline bool origSpec() { static const bool v = envOn("WFC_PBR_ORIGSPEC"); return v; }              // Enhanced spec
inline bool reflTemporal() { static const bool v = envOn("WFC_RT_REFLTEMPORAL"); return v; }       // item 7
// X1 tone curve parameters (the key is the default; the per-map table in endFrame applies only without WFC_TONE_KEY)
inline bool toneKeySet() { static const bool v = std::getenv("WFC_TONE_KEY") != nullptr; return v; }
inline float toneKey() { static const float v = envNum("WFC_TONE_KEY", 0.02f); return v; }
inline float toneAdapt() { static const float v = envNum("WFC_TONE_ADAPT", 0.65f); return v; }
inline float toneGain() { static const float v = envNum("WFC_TONE_GAIN", 1.3f); return v; }
inline float toneSat() { static const float v = envNum("WFC_TONE_SAT", 1.1f); return v; }
inline float toneContrast() { static const float v = envNum("WFC_TONE_CONTRAST", 1.15f); return v; }
inline float toneBloom() { static const float v = envNum("WFC_TONE_BLOOM", 0.8f); return v; }
inline float bloomMax() { static const float v = std::max(1.0f, envNum("WFC_TONE_BLOOMMAX", 16.0f)); return v; }
// X1 white roll (the eye pass, Gorge step 1080): a pixel whose brightest channel is past whiteLo desaturates toward its
// luminance, fully by whiteHi - fire keeps its orange body and only the core goes white (no flat yellow at the shoulder)
inline bool whiteRoll() { static const bool v = envOn("WFC_TONE_WHITEROLL"); return v; }
inline float whiteLo() { static const float v = std::max(1.0f, envNum("WFC_TONE_WHITELO", 2.0f)); return v; }
inline float whiteHi() { static const float v = std::max(whiteLo() + 0.5f, envNum("WFC_TONE_WHITEHI", 16.0f)); return v; }
}  // namespace rtdef
}  // namespace wfc
}  // namespace render
