#include "game/WeaponFx.h"
#include "platform/Image.h"
#include "game/Collision.h"
#include "assets/Gltf.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {
namespace {

using core::Vec3;
using render::ParticleBlend;

constexpr float UU = 0.01f;   // Unreal units -> metres

// Texture table (ExtractedAssets/content/...). `smoke` textures are greyscale RGB whose
// luminance is the opacity (translucent smoke); it is baked into alpha at load.
struct TexDef { const char* path; bool smoke; };
enum Tex { kBolt, kFlashSide, kSmokeball02, kSparksCurved, kMuzzleFlash2, kSparkTrail, kSmokeThin, kDiffClouds, kSmokeball01, kTexCount };
const TexDef kTex[kTexCount] = {
    {"FX_Textures_p/Textures/Bolt_CLR.png", false},               // Bolt_ADD_MAT
    {"FX_Textures_p/Textures/MuzzleFlash_Side_02_CLR.png", false}, // MuzzleFlash_Side_02_MAT_INST
    {"FX_Textures_p/Textures/smokeball_02_CLR.png", false},       // MuzzleFlashWing_MAT_INST
    {"FX_Textures_p/Textures/Sparks_Curved_CLR.png", false},      // SparksSheet1_MAT
    {"FX_Textures_p/Textures/MuzzleFlash2_CLR.png", false},       // MuzzleFlash_01_MAT (FX_Weapons_p)
    {"FX_Textures_p/FX_SparkTrail.png", false},                   // Spark_Tail_MAT
    {"FX_Textures_p/Textures/SmokeThin_CLR.png", true},           // SmokeCoolDepth_Mat
    {"FX_Textures_p/Textures/DiffClouds_CLR.png", true},          // Tracer_Smoke_MAT (trail)
    {"FX_Textures_p/Textures/smokeball_01_CLR.png", false},      // flareball01_MAT (reload flare)
};

// Default effect colour: the native colour constant in the muzzle/tracer LOD streams,
// bytes ff 33 19 ff = ARGB (255, 51, 25, 255): the blue-violet Ion Blaster energy
// (matches the editor thumbnails). TnParticleSystemParameterEnergonColor on the weapon is
// (255,255,255,A=0) = no override.
const Vec3 kIonBlue{51.0f / 255.0f, 25.0f / 255.0f, 1.0f};

// --- curves (21-entry baked lookup tables over life, UE3 TimeScale 20) ---------------------
const std::vector<float> kTrailAlpha = {1, 1, 1, 1, 1, 0.957f, 0.8438f, 0.6836f, 0.5f, 0.3164f, 0.1563f, 0.043f, 0,
                                        0, 0, 0, 0, 0, 0, 0, 0};
const std::vector<float> kImpactGlowAlpha = {0, 0.1576f, 0.5037f, 0.8479f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                             0.8443f, 0.5005f, 0.1564f, 0};
const std::vector<float> kImpactSmokeAlpha = {0, 0.0503f, 0.1545f, 0.2564f, 0.3f, 0.2914f, 0.2741f, 0.2509f, 0.2246f,
                                              0.1982f, 0.1746f, 0.1568f, 0.146f, 0.1307f, 0.11f, 0.0861f, 0.0613f,
                                              0.038f, 0.0184f, 0.005f, 0};
const std::vector<float> kImpactSmokeGrow = {1, 1.029f, 1.112f, 1.243f, 1.416f, 1.625f, 1.864f, 2.127f, 2.408f, 2.701f,
                                             3.0f, 3.299f, 3.592f, 3.873f, 4.136f, 4.375f, 4.584f, 4.757f, 4.888f,
                                             4.971f, 5.0f};
const std::vector<float> kFadeOut = {1, 0.3f};                      // ColorOverLife 1 -> 0.3
const std::vector<float> kSparkColor = {1, 0.9964f, 0.986f, 0.9696f, 0.948f, 0.9219f, 0.892f, 0.8591f, 0.824f,
                                        0.7874f, 0.75f, 0.7126f, 0.676f, 0.6409f, 0.608f, 0.5781f, 0.552f,
                                        0.5304f, 0.514f, 0.5036f, 0.5f};
// Spark length-by-life (Sparks_bolts size curve Y: 50 -> 2 over life, x0.01 width scale).
const std::vector<float> kSparkLen = {50, 46.44f, 37.56f, 26.0f, 14.44f, 5.56f, 2.0f};

using ED = WeaponFx::EmitterDef;
using CV = WeaponFx::Curve;

// ---- FX_AssaultRifle_p.FX.MuzzleFlash_AssaultRifle_FX (bUseLocalSpace emitters) ----------
// "Long_Dup_Dup_Dup_Dup": MuzzleFlash_Side_02, PSA_Velocity, burst 10, life U[0.15,0.2],
//   StartSize U[(190,350),(210,500)] UU, velocity U[890,900] UU/s fwd, location +190 UU fwd,
//   colour-over-life constant 0.1 (x10 burst = 1.0), team colour scale A = 4.
// "Top_Dup_Dup": MuzzleFlashWing (smokeball_02 star), burst 10, life 0.15, size
//   U[(190,240),(210,275)] UU, colour-over-life constant 0.2, scale 4.
// "Sparks"/"Sparks_Dup": SparksSheet1 (2x2 SubUV, random), burst 10, life U[0.05,0.1],
//   size U[(50,50),(150,75)] UU, velocity U[(20,-50,-50),(150,50,50)] UU/s, location
//   (50, +-35, +-35) UU. Omitted: GLOW_Dup_Dup (Glow_Mod_MAT, BLEND_Modulate), the
//   CamerafacingBurst distortion ring, and the BackSteam/BackJet smoke (alpha <= 0.05).
const ED kFlashLong = {"Long", kFlashSide, ParticleBlend::Additive, true, false, true, 10, 0.15f, 0.2f,
    {190 * UU, 350 * UU, 0}, {210 * UU, 500 * UU, 0}, {890 * UU, -10 * UU, -10 * UU}, {900 * UU, 10 * UU, 10 * UU},
    {190 * UU, 0, 0}, {190 * UU, 0, 0}, kIonBlue, 0.1f, 4.0f, false, 1, 1, {}, {}, {}, 0.0f, 0.0f};
const ED kFlashTop = {"Top", kSmokeball02, ParticleBlend::Additive, false, false, true, 10, 0.15f, 0.15f,
    {190 * UU, 240 * UU, 0}, {210 * UU, 275 * UU, 0}, {100 * UU, -100 * UU, -100 * UU}, {200 * UU, 100 * UU, 100 * UU},
    {0, 0, 0}, {0, 0, 0}, kIonBlue, 0.2f, 4.0f, true, 1, 1, {}, {}, {}, 0.0f, 0.0f};
const ED kFlashSparks = {"Sparks", kSparksCurved, ParticleBlend::Additive, false, false, true, 10, 0.05f, 0.1f,
    {50 * UU, 50 * UU, 0}, {150 * UU, 75 * UU, 0}, {20 * UU, -50 * UU, -50 * UU}, {150 * UU, 50 * UU, 50 * UU},
    {50 * UU, -35 * UU, -35 * UU}, {50 * UU, 35 * UU, 35 * UU}, kIonBlue, 1.0f, 4.0f, true, 2, 2, {}, {}, {}, 0.0f, 0.0f};

// ---- FX_AssaultRifle_p.FX.Tracer_AssaultRifle_FX ----------------------------------------
// "Bolt": Bolt_ADD_MAT, PSA_Velocity, burst 1, EmitterDuration 0.1, life 0.6,
//   StartSize U[(200,500),(200,700)] UU, StartVelocity (15000,0,0) UU/s = 150 m/s,
//   SwitchableColorScaleOverLife A = 2 (team 0). Bolts stop at the impact point.
const ED kTracerBolt = {"Bolt", kBolt, ParticleBlend::Additive, true, true, false, 1, 0.6f, 0.6f,
    {200 * UU, 500 * UU, 0}, {200 * UU, 700 * UU, 0}, {15000 * UU, 0, 0}, {15000 * UU, 0, 0},
    {0, 0, 0}, {0, 0, 0}, kIonBlue, 1.0f, 2.0f, false, 1, 1, {}, {}, {}, 0.0f, 0.0f};
// "Trail_Smoke_Dup_Dup" (Trail2 ribbon, Trail_Smoke_10 -> Tracer_Smoke_MAT): life 0.9, alpha
//   curve kTrailAlpha, width multiplier 0.1 -> 1.5 over life (base 100 UU). Drawn as a ribbon.
constexpr float kTrailLife = 0.9f;
constexpr float kTrailBaseW = 100 * UU;
// Tracer_Smoke_MAT [CONF material graph, Rendering matc 2026-10-05]: translucent unlit, u along the trail, v across;
//   opacity = clamp(vertexAlpha * L(u) * W(v) * (N + 0.2), 0, 1), N = DiffClouds.r(uv A) + DiffClouds.r(uv B) + 0.2,
//   W(v) = clamp(1 - 4(v - 0.5)^2, 0, 1)^2 (both long edges -> 0), L(u) = clamp(20u, 0, 1) * clamp(3(1 - u), 0, 1).
// Here: W and (N + 0.2) baked into the cloud texture's alpha (the quad's U runs across the width), the second, panned
// cloud sample approximated by the texture's mean [PROV]; L per ribbon segment (kTrailSegments). Clamping the texel
// before the vertex alpha under-reads only where vertexAlpha < 1 and the raw product > 1. The end the ribbon's u = 0
// lies at (the newest / hit end here) is [PROV].
constexpr int kTrailSegments = 24;
inline float trailWidthMask(float v) { float w = core::clampf(1.0f - 4.0f * (v - 0.5f) * (v - 0.5f), 0.0f, 1.0f); return w * w; }
inline float trailLengthMask(float u) { return core::clampf(20.0f * u, 0.0f, 1.0f) * core::clampf(3.0f * (1.0f - u), 0.0f, 1.0f); }

// ---- FX_IonBlaster_p.FX.Impact_IonBlaster_FX (world space, frame X = surface normal) ------
// "GLOW_Dup": MuzzleFlash_01_MAT (MuzzleFlash2 burst), burst 10, life U[0.2,0.25], random
//   rotation, size U[(50,75),(150,100)] UU, alpha kImpactGlowAlpha, colour 1 -> 0.3.
// "Sparks_bolts_Dup": Spark_Tail_MAT, PSA_Velocity, burst 20, life U[0.25,0.6], velocity
//   U[(150,-600,-600),(1000,600,600)] UU/s, colour 1 -> 0.5, length-by-life 50x -> 2x.
// "Smoke_Dup": SmokeCoolDepth_Mat, burst 4, life U[0.5,0.75], size U[100,150] UU growing 1->5x,
//   alpha kImpactSmokeAlpha (peak 0.3), velocity U[-100,100] UU/s, grey 0.83-0.90.
// Omitted: GLOW / GLOW_Flat (Glow_Mod_MAT modulate) and Shimmer (distortion).
const ED kImpactGlow = {"GLOW_Dup", kMuzzleFlash2, ParticleBlend::Additive, false, false, false, 10, 0.2f, 0.25f,
    {50 * UU, 75 * UU, 0}, {150 * UU, 100 * UU, 0}, {0, 0, 0}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, kIonBlue, 0.25f, 4.0f, true, 1, 1, {kImpactGlowAlpha}, {}, {kFadeOut}, 0.0f, 0.0f};
const ED kImpactSparks = {"Sparks_bolts", kSparkTrail, ParticleBlend::Additive, true, false, false, 20, 0.25f, 0.6f,
    {5 * UU, 3 * UU, 0}, {10 * UU, 5 * UU, 0}, {150 * UU, -600 * UU, -600 * UU}, {1000 * UU, 600 * UU, 600 * UU},
    {0, 0, 0}, {0, 0, 0}, kIonBlue, 1.0f, 2.0f, false, 1, 1, {}, {kSparkLen}, {kSparkColor}, 0.0f, 9.8f};
const ED kImpactSmoke = {"Smoke_Dup", kSmokeThin, ParticleBlend::Translucent, false, false, false, 4, 0.5f, 0.75f,
    {100 * UU, 100 * UU, 0}, {150 * UU, 150 * UU, 0}, {-100 * UU, -100 * UU, -100 * UU}, {100 * UU, 100 * UU, 100 * UU},
    {0, 0, 0}, {0, 0, 0}, {0.86f, 0.86f, 0.9f}, 1.0f, 1.0f, true, 1, 1, {kImpactSmokeAlpha}, {kImpactSmokeGrow}, {}, 0.0f, 0.0f};

// ---- Notify effects (weapon AnimNotifies) --------------------------------------------------
// Smoke growth tables (SizeMultLife X, 21 entries): the impact smoke grows 1 -> 5, the vent
// puffs below 1 -> 3 (decoded entries 1, 1.0145, 1.056, ... 2.1495 at t=0.55; tail completed
// with the same ease-in-out profile).
const std::vector<float> kVentSmokeGrow = {1, 1.0145f, 1.056f, 1.1215f, 1.208f, 1.3125f, 1.432f, 1.5635f, 1.704f,
                                           1.8505f, 2.0f, 2.1495f, 2.296f, 2.4365f, 2.568f, 2.6875f, 2.792f,
                                           2.8785f, 2.9435f, 2.9855f, 3.0f};
const std::vector<float> kLinearOut01 = {0.1f, 0.0f};                // AlphaOverLife 0.1 -> 0
const std::vector<float> kLinearOut1 = {1.0f, 0.0f};                 // AlphaOverLife 1 -> 0
const std::vector<float> kFadeTo01 = {1.0f, 0.1f};                   // ColorOverLife 1 -> 0.1
const std::vector<float> kReloadSmokeAlpha = {0, 0.0114f, 0.0345f, 0.0659f, 0.1026f, 0.1411f, 0.1784f, 0.2112f,
                                              0.2362f, 0.2502f, 0.25f, 0.2362f, 0.2133f, 0.1837f, 0.15f, 0.1145f,
                                              0.08f, 0.0487f, 0.0233f, 0.0062f, 0};
const std::vector<float> kReloadFlareAlpha = {0, 0.0208f, 0.0704f, 0.1296f, 0.1792f, 0.2f, 0.1975f, 0.1903f, 0.1792f,
                                              0.1649f, 0.1481f, 0.1296f, 0.11f, 0.09f, 0.0704f, 0.0519f, 0.0351f,
                                              0.0208f, 0.0097f, 0.0025f, 0};
// Reload flare brightness 20 -> 1 over the first half of life (x0.25 here; batch overbright x4).
const std::vector<float> kReloadFlareBright = {5.0f, 4.525f, 4.05f, 3.575f, 3.1f, 2.625f, 2.15f, 1.675f, 1.2f, 0.725f,
                                               0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
const std::vector<float> kShrink15 = {1.5f, 0.1f};                   // SizeMultLife 1.5 -> 0.1

// Shell_AssaultRifle_FX "SMOKE": SmokeCoolDepth, burst 4, life U[0.5,0.75], size U[50,100] UU,
//   alpha 0.1 -> 0, velocity U[(100,-300,-300),(300,300,300)] UU/s.
const ED kShellSmoke = {"Shell.SMOKE", kSmokeThin, ParticleBlend::Translucent, false, false, false, 4, 0.5f, 0.75f,
    {50 * UU, 50 * UU, 0}, {100 * UU, 100 * UU, 0}, {100 * UU, -300 * UU, -300 * UU}, {300 * UU, 300 * UU, 300 * UU},
    {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, 1.0f, 1.0f, true, 1, 1, {kLinearOut01}, {kVentSmokeGrow}, {}, 0.0f, 0.0f};
// Magazine_IonBlaster_FX "Smoke_Dup_Dup_Dup": as above, alpha 1 -> 0, colour 1 -> 0.1.
const ED kMagSmoke = {"Magazine.Smoke", kSmokeThin, ParticleBlend::Translucent, false, false, false, 4, 0.5f, 0.75f,
    {50 * UU, 50 * UU, 0}, {100 * UU, 100 * UU, 0}, {100 * UU, -300 * UU, -300 * UU}, {300 * UU, 300 * UU, 300 * UU},
    {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, 1.0f, 1.0f, true, 1, 1, {kLinearOut1}, {kVentSmokeGrow}, {kFadeTo01}, 0.0f, 0.0f};
// Reload_AssaultRifle_FX "GLOW_Dup_Dup": flareball01 (smokeball_01), local space, burst 10, life
//   U[0.2,0.5], random rotation, start size U[20,35] UU x (1.5 -> 0.1), alpha peak 0.2, brightness
//   20 -> 1; velocity U[(50,-20,-20),(200,20,20)] UU/s [MED: uniform-curve decode].
const ED kReloadFlare = {"Reload.GLOW", kSmokeball01, ParticleBlend::Additive, false, false, true, 10, 0.2f, 0.5f,
    {20 * UU, 20 * UU, 0}, {35 * UU, 35 * UU, 0}, {50 * UU, -20 * UU, -20 * UU}, {200 * UU, 20 * UU, 20 * UU},
    {0, 0, 0}, {0, 0, 0}, kIonBlue, 1.0f, 4.0f, true, 1, 1, {kReloadFlareAlpha}, {kShrink15}, {kReloadFlareBright}, 0.0f, 0.0f};
// Reload_AssaultRifle_FX "Smoke_Dup": SmokeCoolDepth, SpawnRate 20/s for EmitterDuration 0.75 s,
//   life U[0.5,0.75], size U[100,200] UU growing 1 -> 3, alpha peak 0.25, velocity
//   U[(50,-150,-150),(150,150,150)] UU/s, grey 0.83-0.90, location -50 UU (behind the muzzle).
const ED kReloadSmoke = {"Reload.Smoke", kSmokeThin, ParticleBlend::Translucent, false, false, false, 1, 0.5f, 0.75f,
    {100 * UU, 100 * UU, 0}, {200 * UU, 200 * UU, 0}, {50 * UU, -150 * UU, -150 * UU}, {150 * UU, 150 * UU, 150 * UU},
    {-50 * UU, 0, 0}, {-50 * UU, 0, 0}, {0.86f, 0.86f, 0.9f}, 1.0f, 1.0f, true, 1, 1, {kReloadSmokeAlpha}, {kVentSmokeGrow}, {}, 0.0f, 0.0f};
constexpr float kReloadSmokeRate = 20.0f, kReloadSmokeDuration = 0.75f;
// Omitted (no GL1 path / negligible): ShellGlow + GLOW (Glow_Mod_MAT, modulate), Shimmer
// (distortion), BackSteam/BackJet (alpha <= 0.05), Blaster_Trail ribbons on the shell/magazine.

// Mesh meshes (ParticleModuleTypeDataMesh) + their material diffuse textures.
struct MeshAsset { const char* gltf; const char* texture; };
const MeshAsset kMeshAssets[] = {
    {"FX_GrenadeLauncher_p/GrenadeAmmo_STAT.gltf", "WEP_GrenadeLauncher_p/WEP_GrenadeLauncher_CLR.png"},  // WEP_GrenadeLauncher_MATINST
    {"FX_IonBlaster_p/IonBlaster_Mag_STAT.gltf", "WEP_IonBlaster_p/WEP_IonBlaster_CLR.png"},              // IonBlaster_Mag_MATINST
};
// Shell_AssaultRifle_FX "Shell": GrenadeAmmo_STAT, burst 1, life 1.0, StartSize (0.75,0.3,0.3),
//   spin U[(-1,-1,-1),(5,5,1)] turns/s. Its ejection velocity is authored on the paired ShellGlow
//   emitter (same socket): U[(300,-300,100),(1000,-300,300)] UU/s [MED: shared velocity].
const WeaponFx::MeshDef kShellMesh = {0, 1.0f, {0.75f, 0.3f, 0.3f}, {-1, -1, -1}, {5, 5, 1},
    {300 * UU, -300 * UU, 100 * UU}, {1000 * UU, -300 * UU, 300 * UU}};
// Magazine_IonBlaster_FX "Shell": IonBlaster_Mag_STAT, burst 1, life 3.0, StartSize (1,1,1),
//   spin U[-1,1] turns/s, velocity (200,300,300) UU/s.
const WeaponFx::MeshDef kMagMesh = {1, 3.0f, {1, 1, 1}, {-1, -1, -1}, {1, 1, 1},
    {200 * UU, 300 * UU, 300 * UU}, {200 * UU, 300 * UU, 300 * UU}};
// [PROV] No acceleration module is authored on either mesh emitter; they fall under the world's
// pawn gravity (WorldInfo.DefaultGravityZ -2940 UU/s^2) and come to rest on the collision floor.
constexpr float kMeshGravity = 29.4f;

constexpr float kSquibPercentage = 0.6f;     // [CONF] ImpactSquibPercentage
constexpr int   kSquibMaxCount = 5;          // [CONF] ImpactSquibMaxCount
constexpr float kMaxImpactDistance = 25.0f;  // [CONF] MaxImpactEffectDistance 2500 UU

float frand() { return (float)std::rand() / (float)RAND_MAX; }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
Vec3 lerpRand(const Vec3& a, const Vec3& b) { return {lerp(a.x, b.x, frand()), lerp(a.y, b.y, frand()), lerp(a.z, b.z, frand())}; }

core::Vec3 col(const core::Mat4& m, int c) { return {m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]}; }
core::Mat4 rotZ(float a) {
    core::Mat4 r; float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

// Orthonormal frame with X along `fwd`, positioned at `pos`.
core::Mat4 frameFrom(const Vec3& pos, const Vec3& fwdIn) {
    Vec3 f = core::normalize(fwdIn);
    Vec3 up = std::fabs(f.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    Vec3 r = core::normalize(core::cross(up, f));
    Vec3 u = core::cross(f, r);
    core::Mat4 m;
    m.m[0] = f.x; m.m[1] = f.y; m.m[2] = f.z;
    m.m[4] = r.x; m.m[5] = r.y; m.m[6] = r.z;
    m.m[8] = u.x; m.m[9] = u.y; m.m[10] = u.z;
    m.m[12] = pos.x; m.m[13] = pos.y; m.m[14] = pos.z;
    return m;
}

} // namespace

float WeaponFx::Curve::eval(float t) const {
    if (v.empty()) return 1.0f;
    if (v.size() == 1) return v[0];
    float x = core::clampf(t, 0.0f, 1.0f) * 20.0f;      // baked tables: 21 entries over life
    if (v.size() != 21) x = core::clampf(t, 0.0f, 1.0f) * (float)(v.size() - 1);
    size_t i = (size_t)x;
    if (i + 1 >= v.size()) return v.back();
    float u = x - (float)i;
    return v[i] + (v[i + 1] - v[i]) * u;
}

void WeaponFx::load(render::IRenderer& r, const std::string& contentRoot) {
    tex_.assign(kTexCount, render::kInvalidTexture);
    int ok = 0;
    for (int i = 0; i < kTexCount; ++i) {
        render::ImageData img;
        if (!platform::decodeImage(contentRoot + kTex[i].path, img)) continue;
        if (i == kDiffClouds && img.w > 0) {          // Tracer_Smoke_MAT: rgb = alpha = W(across) * (N + 0.2)
            double sum = 0;
            for (size_t p = 0; p + 3 < img.rgba.size(); p += 4) sum += img.rgba[p] / 255.0;
            const float mean = (float)(sum / std::max<size_t>(1, img.rgba.size() / 4));
            for (int y = 0; y < img.h; ++y)
                for (int x = 0; x < img.w; ++x) {
                    uint8_t* px = &img.rgba[((size_t)y * img.w + x) * 4];
                    const float w = trailWidthMask(((float)x + 0.5f) / (float)img.w);
                    const float a = core::clampf((px[0] / 255.0f + mean + 0.4f) * w, 0.0f, 1.0f);
                    px[0] = px[1] = px[2] = px[3] = (uint8_t)(a * 255.0f + 0.5f);   // emissive = vertexColor x the same term
                }
        } else if (kTex[i].smoke) {
            for (size_t p = 0; p + 3 < img.rgba.size(); p += 4) {
                uint8_t* px = &img.rgba[p];
                int lum = (px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8;
                px[3] = (uint8_t)lum;
                px[0] = px[1] = px[2] = 255;
            }
        }
        tex_[(size_t)i] = r.uploadTexture(img);
        if (tex_[(size_t)i] >= 0) ++ok;
    }
    LOG_INFO("weapon fx: %d/%d original FX textures loaded", ok, (int)kTexCount);
}

void WeaponFx::emit(const EmitterDef& d, const core::Mat4& frame, int effect, float lifeCap, int count) {
    Vec3 fx = col(frame, 0), fy = col(frame, 1), fz = col(frame, 2), o = col(frame, 3);
    int n = count >= 0 ? count : d.burst;
    for (int i = 0; i < n; ++i) {
        Part p{};
        p.def = &d;
        p.effect = effect;
        p.age = 0.0f;
        p.life = std::min(lerp(d.lifeMin, d.lifeMax, frand()), lifeCap);
        Vec3 sz = lerpRand(d.sizeMin, d.sizeMax);
        p.w = sz.x; p.h = sz.y > 0 ? sz.y : sz.x;
        p.rot = d.randomRot ? frand() * 2.0f * core::PI : 0.0f;
        Vec3 lv = lerpRand(d.velMin, d.velMax), ll = lerpRand(d.locMin, d.locMax);
        if (d.localSpace) {                       // socket space (x = forward)
            p.pos = ll; p.vel = lv;
        } else {
            p.pos = o + fx * ll.x + fy * ll.y + fz * ll.z;
            p.vel = fx * lv.x + fy * lv.y + fz * lv.z;
        }
        p.u0 = 0; p.v0 = 0; p.u1 = 1; p.v1 = 1;
        if (d.subH > 1 || d.subV > 1) {           // random SubUV cell
            int cx = std::rand() % d.subH, cy = std::rand() % d.subV;
            p.u0 = (float)cx / d.subH; p.u1 = (float)(cx + 1) / d.subH;
            p.v0 = (float)cy / d.subV; p.v1 = (float)(cy + 1) / d.subV;
        }
        parts_.push_back(p);
    }
}

// Socket frame with unit axes (weapon/bone matrices may carry scale).
static core::Mat4 orthonormal(const core::Mat4& m) {
    core::Mat4 r = m;
    for (int c = 0; c < 3; ++c) {
        Vec3 v = core::normalize(col(m, c));
        r.m[c * 4] = v.x; r.m[c * 4 + 1] = v.y; r.m[c * 4 + 2] = v.z;
    }
    return r;
}

void WeaponFx::loadMeshes(render::IRenderer& r, const std::string& contentRoot) {
    meshes_.assign(sizeof(kMeshAssets) / sizeof(kMeshAssets[0]), render::kInvalidMesh);
    int ok = 0;
    for (size_t i = 0; i < meshes_.size(); ++i) {
        render::MeshData md;
        if (!assets::loadGlb(contentRoot + kMeshAssets[i].gltf, md)) continue;
        render::ImageData img;
        render::TextureHandle th = render::kInvalidTexture;
        if (platform::decodeImage(contentRoot + kMeshAssets[i].texture, img)) th = r.uploadTexture(img);
        for (render::Material& m : md.mats) { m.tex = th; m.color = {1, 1, 1}; }
        meshes_[i] = r.uploadMesh(md);
        if (meshes_[i] != render::kInvalidMesh) ++ok;
    }
    LOG_INFO("weapon fx: %d/%zu mesh-particle meshes loaded", ok, meshes_.size());
}

void WeaponFx::emitMesh(const MeshDef& d, const core::Mat4& frame) {
    if (d.mesh < 0 || (size_t)d.mesh >= meshes_.size() || meshes_[(size_t)d.mesh] == render::kInvalidMesh) return;
    core::Mat4 f = orthonormal(frame);
    MeshPart p{};
    p.def = &d;
    p.basis = f;
    p.basis.m[12] = p.basis.m[13] = p.basis.m[14] = 0.0f;
    p.pos = col(f, 3);
    Vec3 lv = lerpRand(d.velMin, d.velMax);
    p.vel = col(f, 0) * lv.x + col(f, 1) * lv.y + col(f, 2) * lv.z;
    p.spin = lerpRand(d.spinMin, d.spinMax) * (2.0f * core::PI);
    p.ang = {0, 0, 0};
    p.age = 0.0f;
    p.resting = false;
    meshParts_.push_back(p);
}

bool WeaponFx::spawnNotifyEffect(const std::string& ps, const core::Mat4& socketWorld) {
    core::Mat4 f = orthonormal(socketWorld);
    if (ps == "FX_AssaultRifle_p.FX.Shell_AssaultRifle_FX") {
        emitMesh(kShellMesh, f);
        emit(kShellSmoke, f, -1);
        return true;
    }
    if (ps == "FX_IonBlaster_p.FX.Magazine_IonBlaster_FX") {
        emitMesh(kMagMesh, f);
        emit(kMagSmoke, f, -1);
        return true;
    }
    if (ps == "FX_AssaultRifle_p.FX.Reload_AssaultRifle_FX") {
        muzzleNow_ = socketWorld; haveMuzzle_ = true;
        emit(kReloadFlare, socketWorld, -1);
        streams_.push_back({&kReloadSmoke, 0.0f, kReloadSmokeDuration, kReloadSmokeRate, 0.0f});
        return true;
    }
    return false;
}

void WeaponFx::spawnMuzzleFlash(const core::Mat4& socketWorld) {
    muzzleNow_ = socketWorld; haveMuzzle_ = true;
    emit(kFlashLong, socketWorld, -1);
    emit(kFlashTop, socketWorld, -1);
    emit(kFlashSparks, socketWorld, -1);
}

void WeaponFx::spawnTracer(const Vec3& muzzle, const Vec3& hit) {
    Vec3 d = hit - muzzle;
    float dist = core::length(d);
    if (dist < 0.5f) return;
    // The bolt flies at 150 m/s and is removed when it reaches the impact point.
    float speed = kTracerBolt.velMin.x;
    emit(kTracerBolt, frameFrom(muzzle, d), -1, std::min(kTracerBolt.lifeMax, dist / speed));
    smoke_.push_back({muzzle, hit, 0.0f});
}

int WeaponFx::liveImpacts() const {
    int ids[64]; int n = 0;
    for (const Part& p : parts_) {
        if (p.effect < 0) continue;
        bool seen = false;
        for (int i = 0; i < n; ++i) if (ids[i] == p.effect) { seen = true; break; }
        if (!seen && n < 64) ids[n++] = p.effect;
    }
    return n;
}

bool WeaponFx::spawnImpact(const Vec3& pos, const Vec3& normal, const Vec3& viewPos) {
    if (core::length(pos - viewPos) > kMaxImpactDistance) return false;
    if (frand() > kSquibPercentage) return false;
    if (liveImpacts() >= kSquibMaxCount) return false;
    core::Mat4 f = frameFrom(pos + normal * 0.05f, normal);
    int id = nextImpact_++;
    emit(kImpactGlow, f, id);
    emit(kImpactSparks, f, id);
    emit(kImpactSmoke, f, id);
    return true;
}

void WeaponFx::tick(float dt, const core::Mat4* muzzleNow, const CollisionWorld* colw) {
    if (muzzleNow) { muzzleNow_ = *muzzleNow; haveMuzzle_ = true; } else haveMuzzle_ = false;
    // Continuous emitters: SpawnRate particles/s at the current muzzle frame for EmitterDuration.
    for (size_t i = 0; i < streams_.size();) {
        Stream& st = streams_[i];
        st.age += dt;
        if (haveMuzzle_) {
            st.acc += st.rate * dt;
            int n = (int)st.acc;
            if (n > 0) { st.acc -= (float)n; emit(*st.def, orthonormal(muzzleNow_), -1, 1e9f, n); }
        }
        if (st.age >= st.duration || !haveMuzzle_) { streams_[i] = streams_.back(); streams_.pop_back(); } else ++i;
    }
    // Mesh particles: ballistic until they reach the collision floor, then rest.
    for (size_t i = 0; i < meshParts_.size();) {
        MeshPart& m = meshParts_[i];
        m.age += dt;
        if (m.age >= m.def->life) { meshParts_[i] = meshParts_.back(); meshParts_.pop_back(); continue; }
        if (!m.resting) {
            m.vel.y -= kMeshGravity * dt;
            Vec3 next = m.pos + m.vel * dt;
            float gy; Vec3 n;
            if (colw && m.vel.y < 0.0f && colw->groundHeight(next.x, next.z, m.pos.y + 0.05f, 0.05f, gy, n) && next.y <= gy + 0.03f) {
                next.y = gy + 0.03f;
                m.resting = true;
                m.vel = {0, 0, 0};
            }
            m.pos = next;
            m.ang += m.spin * dt;
        }
        ++i;
    }
    for (size_t i = 0; i < parts_.size();) {
        Part& p = parts_[i];
        p.age += dt;
        if (p.age >= p.life || (p.def->localSpace && !haveMuzzle_)) {
            parts_[i] = parts_.back(); parts_.pop_back(); continue;
        }
        if (p.def->gravity > 0.0f) p.vel.y -= p.def->gravity * dt;
        p.pos += p.vel * dt;
        ++i;
    }
    for (size_t i = 0; i < smoke_.size();) {
        smoke_[i].age += dt;
        if (smoke_[i].age >= kTrailLife) { smoke_[i] = smoke_.back(); smoke_.pop_back(); } else ++i;
    }
}

void WeaponFx::draw(render::IRenderer& r) const {
    // Group by (texture, blend, colour scale): one batch per emitter definition.
    const EmitterDef* defs[] = {&kReloadSmoke, &kShellSmoke, &kMagSmoke, &kImpactSmoke, &kImpactGlow,
                                &kImpactSparks, &kTracerBolt, &kFlashTop, &kFlashLong, &kFlashSparks,
                                &kReloadFlare};

    // Mesh particles (opaque, lit like the weapon).
    for (const MeshPart& m : meshParts_) {
        render::MeshHandle h = meshes_[(size_t)m.def->mesh];
        core::Mat4 model = core::Mat4::translate(m.pos) * m.basis * core::Mat4::rotateX(m.ang.x) *
                           core::Mat4::rotateY(m.ang.y) * rotZ(m.ang.z) * core::Mat4::scale(m.def->scale);
        r.drawMesh(h, model, core::Vec3{1, 1, 1});
    }
    std::vector<render::Particle> q;

    // Tracer smoke ribbons (translucent, drawn first).
    if (!smoke_.empty() && tex_.size() == kTexCount) {
        q.clear();
        for (const TracerSmoke& s : smoke_) {
            float t = s.age / kTrailLife;
            Curve a{kTrailAlpha};
            const Vec3 d = s.b - s.a;
            const float len = core::length(d);
            const float c = lerp(1.0f, 0.6f, t);
            const float alpha = a.eval(t);
            for (int k = 0; k < kTrailSegments; ++k) {     // u = 0 at the hit end, 1 at the muzzle
                const float u0 = (float)k / kTrailSegments, u1 = (float)(k + 1) / kTrailSegments;
                const float l = trailLengthMask(0.5f * (u0 + u1));
                if (l <= 0.0f) continue;
                render::Particle p;
                p.pos = s.b - d * (0.5f * (u0 + u1));
                p.axis = d;
                p.h = len / kTrailSegments;
                p.w = kTrailBaseW * lerp(0.1f, 1.5f, t);
                p.r = c; p.g = c; p.b = c;
                p.a = alpha * l;
                p.v0 = (1.0f - u1) * len / 4.0f; p.v1 = (1.0f - u0) * len / 4.0f;   // the cloud tiles every 4 m
                q.push_back(p);
            }
        }
        r.drawParticles({tex_[kDiffClouds], ParticleBlend::Translucent, 1.0f, q.data(), q.size()});
    }

    for (const EmitterDef* d : defs) {
        q.clear();
        for (const Part& p : parts_) {
            if (p.def != d) continue;
            float t = p.life > 0 ? p.age / p.life : 1.0f;
            render::Particle o;
            Vec3 pos = p.pos, vel = p.vel;
            if (d->localSpace) {
                if (!haveMuzzle_) continue;
                pos = core::transformPoint(muzzleNow_, p.pos);
                vel = core::transformDir(muzzleNow_, p.vel);
            }
            o.pos = pos;
            // Size-by-life: velocity-aligned streaks stretch in length only; sprites scale uniformly.
            float g = d->size.eval(t);
            o.w = d->velocityAligned ? p.w : p.w * g;
            o.h = p.h * g;
            if (d->velocityAligned) {
                o.axis = core::length(vel) > 1e-4f ? vel : core::Vec3{0, 1, 0};
                o.uAlongAxis = d->uAlongAxis;
            }
            o.rot = p.rot;
            float c = d->colorMul * d->colorLife.eval(t);
            o.r = d->color.x * c; o.g = d->color.y * c; o.b = d->color.z * c;
            o.a = d->alpha.eval(t);
            o.u0 = p.u0; o.v0 = p.v0; o.u1 = p.u1; o.v1 = p.v1;
            q.push_back(o);
        }
        if (!q.empty() && (size_t)d->texture < tex_.size())
            r.drawParticles({tex_[(size_t)d->texture], d->blend, d->colorScale, q.data(), q.size()});
    }
}

} // namespace game
