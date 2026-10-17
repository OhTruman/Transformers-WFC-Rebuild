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
// X1 white roll (the eye pass, Gorge step 1080): a pixel whose brightest channel is past whiteLo rolls, fully by whiteHi,
// to a warm white of its own hue (whiteMix of the way to white), then on to pure white by 8 x whiteHi - fire keeps its
// orange body, its core goes warm white and only the hottest centre white (no flat yellow, no flat white)
inline bool whiteRoll() { static const bool v = envOn("WFC_TONE_WHITEROLL"); return v; }
inline float whiteLo() { static const float v = std::max(1.0f, envNum("WFC_TONE_WHITELO", 1.5f)); return v; }
inline float whiteHi() { static const float v = std::max(whiteLo() + 0.5f, envNum("WFC_TONE_WHITEHI", 6.0f)); return v; }
// X3 froxel volumetrics (Realistic, RT): the medium = the map's authored height fog x x3Density + a haze floor (1 / m);
// in-scatter from the sun (x x3Sun, phase g), the in-view lamps (x x3Lamp, shadowed) and the transient lights (x x3Trans)
inline bool x3() { static const bool v = envOn("WFC_X3"); return v; }
inline bool envSet(const char* k) { return std::getenv(k) != nullptr; }
// the X1 contrast on frames with X3 (the in-scatter lowers the frame's contrast; this gives it back - the eye pass bar)
inline float x3ToneContrast() { static const float v = envNum("WFC_X3_TONECONTRAST", 1.2f); return v; }
inline float x3Density() { static const float v = std::max(0.0f, envNum("WFC_X3_DENSITY", 0.3f)); return v; }
inline float x3Haze() { static const float v = std::max(0.0f, envNum("WFC_X3_HAZE", 0.0015f)); return v; }
inline float x3Sun() { static const float v = std::max(0.0f, envNum("WFC_X3_SUN", 0.6f)); return v; }
inline float x3Lamp() { static const float v = std::max(0.0f, envNum("WFC_X3_LAMP", 2.0f)); return v; }
inline float x3Trans() { static const float v = std::max(0.0f, envNum("WFC_X3_TRANS", 0.2f)); return v; }
inline float x3G() { static const float v = std::min(0.95f, std::max(-0.95f, envNum("WFC_X3_G", 0.5f))); return v; }
inline float x3Dist() { static const float v = std::max(10.0f, envNum("WFC_X3_DIST", 120.0f)); return v; }
inline float x3LampCore() { static const float v = std::max(0.05f, envNum("WFC_X3_LAMPCORE", 2.0f)); return v; }
inline float x3Cap() { static const float v = std::max(0.0f, envNum("WFC_X3_CAP", 0.2f)); return v; }   // integrated in-scatter cap
inline float x3History() { static const float v = std::min(0.98f, std::max(0.0f, envNum("WFC_X3_HISTORY", 0.9f))); return v; }
// X6 (Realistic, RT reflections): more reflective surfaces + damp sheen. x6Refl = the reflection gate's floor on every lit
// world surface (the authored specular colour no longer zeroes it); x6Damp = how damp the up-facing ground is (patches in
// world space: smoother, darker, a sharper reflection), per map in kMapX6 (WfcPipeline.cpp) unless WFC_X6_DAMP is set
inline bool x6() { static const bool v = envOn("WFC_X6"); return v; }
inline float x6Refl() { static const float v = std::min(1.0f, std::max(0.0f, envNum("WFC_X6_REFL", 0.35f))); return v; }
inline float x6Damp() { static const float v = std::min(1.0f, std::max(0.0f, envNum("WFC_X6_DAMP", 0.6f))); return v; }
inline float x6Spec() { static const float v = std::max(0.0f, envNum("WFC_X6_SPEC", 0.6f)); return v; }   // damp highlight gain
inline float x6Scale() { static const float v = std::max(0.5f, envNum("WFC_X6_SCALE", 4.0f)); return v; }   // damp patch size, m
inline float whiteMix() { static const float v = std::min(1.0f, std::max(0.0f, envNum("WFC_TONE_WHITEMIX", 0.5f))); return v; }
}  // namespace rtdef
}  // namespace wfc
}  // namespace render
