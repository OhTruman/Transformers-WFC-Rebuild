#include "game/VehicleFx.h"
#include "assets/Gltf.h"
#include "platform/Image.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {
namespace {

using core::Vec3;
using ED = VehicleFx::EmitterDef;
constexpr float UU = 0.01f;

// ---- sockets (VH_OptimusPrime_SKEL sockets; UE relative transforms incl. scale -> glTF) ------
const VehicleFx::SocketDef kSockets[VehicleFx::kSocketCount] = {
    {"BoostSocket_L", "L_Robo23_XT", {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, -0.35f, 1}},
    {"BoostSocket_R", "R_Robo23_XT", {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0.35f, 1}},
    {"HoverBooster_LBack", "L_Wheel02_XB", {0, 0, -2, 0, 0, -2.5f, 0, 0, -2.5f, 0, 0, 0, 0, 0, -0.13f, 1}},
    {"HoverBooster_RBack", "R_Wheel02_XB", {0, 0, 2, 0, 0, -2.5f, 0, 0, 2.5f, 0, 0, 0, 0, 0, 0.13f, 1}},
    {"HoverBooster_LFront", "L_Wheel01_XB", {0, 0, -3, 0, 0, -3, 0, 0, -3, 0, 0, 0, 0, 0, -0.13f, 1}},
    {"HoverBooster_RFront", "R_Wheel01_XB", {0, 0, 3, 0, 0, -3, 0, 0, 3, 0, 0, 0, 0, 0, 0.13f, 1}},
    {"HoverBooster_LBack2", "L_Wheel03_XB", {0, 0, -2, 0, 0, -2.5f, 0, 0, -2.5f, 0, 0, 0, 0, 0, -0.13f, 1}},
    {"HoverBooster_RBack2", "R_Wheel03_XB", {0, 0, 2, 0, 0, -2.5f, 0, 0, 2.5f, 0, 0, 0, 0, 0, 0.13f, 1}},
    {"JumpBoostSocket_C", "C_Body_XB", {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, -1.464059f, -1.0025f, 0, 1}},
    {"JumpBoostSocket_R", "C_Body_XB", {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0.969948f, -0.93f, 1.14068f, 1}},
    {"JumpBoostSocket_L", "C_Body_XB", {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0.97f, -0.93f, -1.14068f, 1}},
    {"RamSocket", "C_Body_XB", {1, 0, 0, 0, 0, 1.5f, 0, 0, 0, 0, 1.5f, 0, 3.8f, -0.4f, 0, 1}},   // (380,0,-40) UU, scale (1,1.5,1.5)
};

// ---- assets ----------------------------------------------------------------------------------
enum Tex { kSphereGlow, kRing, kLightningRing, kSmokeThin, kSpark, kTexCount };
const char* kTexPaths[kTexCount] = {
    "FX_Textures_p/Textures/SphereGlow_01_CLR.png",     // Basic_Particle_Add_MAT
    "FX_Textures_p/Textures/Ring_CLR.png",              // Ring_Distort_Add_MAT
    "FX_Textures_p/Textures/lightningring_01_CLR.png",  // ElectroRing01_Mat
    "FX_Textures_p/Textures/SmokeThin_CLR.png",         // booster_smoke1_MAT (additive)
    nullptr,                                            // Spark_MAT: procedural (generated below)
};
// `intensity`: material emissive scale where the GL1 path cannot evaluate the shader graph.
struct MeshAsset { const char* gltf; const char* texture; float intensity; float fresnelExp = 0, fresnelScale = 1, fresnelPower = 1; };
enum Mesh { kBooster02, kBullet, kBooster03, kCircuit, kLightCyl, kRamMesh, kMeshCount };
const MeshAsset kMeshes[kMeshCount] = {
    {"FX_Navigation_p/Boostermesh_02_STATMESH.gltf", "FX_Textures_p/Textures/LightBeam_Falloff_01_CLR.png", 1.0f}, // Boostermaterial_02_MAT
    {"FX_Navigation_p/bulletshape_fx_STAT.gltf", "FX_Textures_p/Textures/SphereGlow_01_CLR.png", 1.0f},            // bumble_boostcone_MAT
    {"FX_Navigation_p/Boostermesh_03.gltf", "FX_Textures_p/Textures/LightBeam_Falloff_01_CLR.png", 1.0f},          // Afterburn_02_MAT
    {"FX_Navigation_p/Boost_Circuit_STAT.gltf", "FX_Textures_p/Textures/LightBeam_Falloff_01_CLR.png", 1.0f},      // Afterburn_Circuitry_02_MAT
    // LightCylinder_Rays_MAT_INST -> LightVolume_Base_MAT: view-dependent volumetric (side-view/near
    // fade, depth bias, dust); only its DustPower 0.1 is applied as an intensity [PROV].
    {"FX_Mesh_p/Light_Cylinder_STAT.gltf", "FX_Textures_p/Textures/LightBeam_Falloff_01_CLR.png", 0.1f},
    // Ram_model_MAT: additive, two-sided, four panning Flame_Tile layers (only one drawn, static).
    // Its emissive = 2 x (vertex colour x c)^2 with c = saturate(pow(1 - N.V, FresnelExponent 2) x
    // FresnelScaleUp 1.5) x 2 x lerp(A x L1, L1, 0.4) [CONF graph]: a rim-lit shield. The fresnel is
    // applied per vertex (squared, as in the graph); the panning-layer term is one static layer [PROV].
    {"FX_Mesh_p/Mesh/Ram_STAT.gltf", "FX_Textures_p/Textures/Flame_Tile_CLR.png", 1.0f, 2.0f, 1.5f, 2.0f},
};

// ---- shared curves (21-entry baked tables over normalized life) -------------------------------
const std::vector<float> kRamp = {0, 0.25f, 0.5f, 0.75f, 1, 0.9375f, 0.875f, 0.8125f, 0.75f, 0.6875f, 0.625f,
                                  0.5625f, 0.5f, 0.4375f, 0.375f, 0.3125f, 0.25f, 0.1875f, 0.125f, 0.0625f, 0};
const std::vector<float> kHold = {0, 0.25f, 0.5f, 0.75f, 1, 1, 1, 1, 1, 1, 1, 1, 0.9933f, 0.9234f, 0.7981f,
                                  0.6374f, 0.4611f, 0.2893f, 0.1419f, 0.0388f, 0};
const std::vector<float> kRingAlpha = {0, 0.5f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.992f, 0.9222f, 0.7971f, 0.6366f,
                                       0.4605f, 0.2889f, 0.1417f, 0.0387f, 0};
const std::vector<float> kThrBase = {0, 0.3179f, 0.6358f, 0.9537f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.9651f,
                                     0.6363f, 0.2109f, 0, 0};
const std::vector<float> kSmokeAlpha = {0, 0.439f, 0.878f, 0.8904f, 0.8681f, 0.8328f, 0.7863f, 0.7304f, 0.667f,
                                        0.598f, 0.5252f, 0.4506f, 0.3759f, 0.303f, 0.2338f, 0.1701f, 0.1138f,
                                        0.0668f, 0.0309f, 0.008f, 0};
const std::vector<float> kFlicker = {0.8f, 1.0641f, 1.2293f, 1.3123f, 1.3299f, 1.2989f, 1.2359f, 1.1578f, 1.0813f,
                                     1.0231f, 1.0f, 1.0224f, 1.0788f, 1.153f, 1.2285f, 1.2892f, 1.3188f, 1.3009f,
                                     1.2194f, 1.0578f, 0.8f};

std::vector<Vec3> linear3(Vec3 a, Vec3 b) { return {a, b}; }
std::vector<Vec3> fromXYZ(const float* x, const float* y, const float* z, int n) {
    std::vector<Vec3> v; for (int i = 0; i < n; ++i) v.push_back({x[i], y[i], z[i]}); return v;
}

ED base(const char* name) {
    ED e{};
    e.name = name; e.mesh = -1; e.texture = kSphereGlow;
    e.sizeMin = e.sizeMax = {1, 1, 1}; e.size2Min = e.size2Max = {1, 1, 1};
    e.color = {1, 1, 1}; e.colorMul = 1.0f; e.killOnDeactivate = true;
    e.material = nullptr; e.loopMin = e.loopMax = 0.0f; e.hasColorRange = false; e.colorMax = {1, 1, 1};
    e.locMin = e.locMax = {0, 0, 0}; e.worldSpace = false; e.materialOnly = false;
    return e;
}

// ======================= BoostFx: bumble_boost_small1_FX =======================
const std::vector<const ED*>& boostEmitters() {
    static std::vector<const ED*> v;
    if (!v.empty()) return v;
    static const float thrX[21] = {0, 0.2079f, 0.4158f, 0.6237f, 0.8316f, 1.0395f, 1.2474f, 1.4553f, 1.6632f, 1.8711f,
                                   2.0648f, 1.9584f, 1.8519f, 1.7454f, 1.6389f, 1.5324f, 1.4259f, 1.3195f, 1.213f, 1.1065f, 1.0f};
    static const float thrY[21] = {0, 0.2009f, 0.4018f, 0.6027f, 0.8036f, 1.0045f, 1.2054f, 1.4063f, 1.6072f, 1.8081f,
                                   1.9955f, 1.896f, 1.7964f, 1.6969f, 1.5973f, 1.4978f, 1.3982f, 1.2987f, 1.1991f, 1.0996f, 1.0f};
    static ED thruster = [] { ED e = base("thruster"); e.mesh = kBooster02; e.burst = 3; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.5f; e.sizeMin = {4, 1, 2}; e.sizeMax = {5, 1, 1};
        e.grow = {fromXYZ(thrX, thrY, thrY, 21)}; e.alpha = {kRamp}; e.color = {1.0f, 0.6f, 0.05f}; e.colorMul = 0.98f;
        e.locX = 5; e.material = "FX_Navigation_p.Boostermaterial_02_MAT"; return e; }();
    static ED cone = [] { ED e = base("Cone_thrust_Dup"); e.mesh = kBullet; e.burst = 1; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.4f; e.sizeMin = e.sizeMax = {50, 37.5f, 15};
        e.grow = {{{1.134f, 0.8613f, 0.866f}, {1.1587f, 0.8675f, 0.894f}, {1.2256f, 0.8844f, 0.9696f}, {1.3239f, 0.909f, 1.0803f},
                   {1.443f, 0.9387f, 1.2134f}, {1.5721f, 0.9706f, 1.3564f}, {1.7004f, 1.0019f, 1.4968f}, {1.8172f, 1.0297f, 1.6218f},
                   {1.9117f, 1.0514f, 1.7191f}, {1.9732f, 1.0641f, 1.7759f}, {2.0182f, 1.0861f, 1.7754f}, {2.2426f, 1.2722f, 1.6818f},
                   {2.643f, 1.6152f, 1.5095f}, {3.1656f, 2.0672f, 1.2823f}, {3.7561f, 2.5807f, 1.0243f}, {4.3606f, 3.1078f, 0.7593f},
                   {4.9249f, 3.6009f, 0.5115f}, {5.3949f, 4.0122f, 0.3048f}, {5.7166f, 4.2939f, 0.1632f}, {5.8358f, 4.3985f, 0.1107f},
                   {5.8358f, 4.3985f, 0.1107f}}};
        e.alpha = {kRamp}; e.color = {5.0f, 1.5f, 0.05f}; e.colorMul = 3.0f; e.locX = 15;
        e.material = "FX_Navigation_p.bumble_boostcone_MAT"; return e; }();
    static ED glow = [] { ED e = base("Particle Emitter_Dup"); e.burst = 1; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.4f; e.sizeMin = e.sizeMax = {30, 30, 30}; e.rotMin = -0.5f; e.rotMax = 0.5f;
        e.grow = {{{2.5216f, 1.4231f, 1}, {2.525f, 1.4186f, 1}, {2.5339f, 1.4092f, 1}, {2.5466f, 1.4016f, 1}, {2.5613f, 1.4021f, 1},
                   {2.5764f, 1.4171f, 1}, {2.5899f, 1.4533f, 1}, {2.6002f, 1.5169f, 1}, {2.6056f, 1.6146f, 1}, {2.6042f, 1.7527f, 1},
                   {2.5944f, 1.9377f, 1}, {2.5743f, 2.1761f, 1}, {2.5422f, 2.4744f, 1}, {2.3981f, 2.8487f, 1}, {2.0921f, 3.2423f, 1},
                   {1.6806f, 3.6325f, 1}, {1.2194f, 3.9984f, 1}, {0.7638f, 4.3192f, 1}, {0.3696f, 4.5741f, 1}, {0.0923f, 4.7424f, 1},
                   {0.0f, 4.8031f, 1}}};
        e.alpha = {kRamp}; e.color = {5.0f, 1.5f, 0.05f}; e.colorMul = 3.0f; e.locX = 25;
        e.material = "FX_Materials_p.Materials.Basic_Particle_Add_MAT"; return e; }();
    static ED loopcone = [] { ED e = base("loopcone"); e.mesh = kBooster02; e.rateMin = 3; e.rateMax = 4; e.delay = 0.1f;
        e.lifeMin = e.lifeMax = 1.0f; e.sizeMin = {4, 1, 2}; e.sizeMax = {5, 1, 1}; e.size2Max = {1.1f, 1.1f, 1};
        e.alpha = {kRamp}; e.color = {1.0688f, 0.5922f, 0.1586f}; e.locX = 5;
        e.material = "FX_Navigation_p.Boostermaterial_02_MAT"; return e; }();
    static ED loopglow = [] { ED e = base("Particle Emitter_Dup_Dup"); e.rateMin = e.rateMax = 20; e.delay = 0.2f;
        e.lifeMin = 0.4f; e.lifeMax = 0.6f; e.sizeMin = e.sizeMax = {30, 30, 1}; e.grow = {linear3({1, 1, 1}, {3.0576f, 3.0791f, 3.0791f})};
        e.rotMin = -1; e.rotMax = 1; e.rotRateMin = -0.75f; e.rotRateMax = 0.75f;
        e.alpha = {kRamp}; e.color = {0.8f, 0.8f, 0.3f}; e.locX = 25;
        e.material = "FX_Materials_p.Materials.Basic_Particle_Add_MAT"; e.burst = 1; e.loopMin = e.loopMax = 0.5f; return e; }();
    v = {&thruster, &cone, &glow, &loopcone, &loopglow};
    return v;
}

// ======================= HoverFX: CarHover_A_01_FX =======================
// Looping: Rings_Dup (Ring_Distort_Add, 3/s), lightcone_Dup (Light_Cylinder mesh, 10/s, not killed
// on deactivate). One-shot on activation: Sparks_bolts (0.3 s), ElectroRing (0.2 s), Pulse (0.5 s).
// Material-only (no GL1 path): base_glow_Dup_Dup (Glow_Mod_MAT, modulate) and rays_Dup (Trail_Distort_MAT,
// distortion); their LOD-0 values are CONF, module roles MED (same order rules as the other emitters).
const std::vector<const ED*>& hoverEmitters() {
    static std::vector<const ED*> v;
    if (!v.empty()) return v;
    static ED sparks = [] { ED e = base("Sparks_bolts_Dup_Dup_Dup"); e.texture = kSpark; e.velocityAligned = true;
        e.burst = 10; e.rateMin = e.rateMax = 100; e.duration = 0.3f; e.lifeMin = 0.1f; e.lifeMax = 0.3f;
        e.sizeMin = {1.5f, 12, 1}; e.sizeMax = {0.5f, 8, 1}; e.alpha = {kRamp};
        e.colorLife = {linear3({1, 1, 1}, {0.8f, 0.8f, 1})}; e.color = {2.5f, 2.0f, 2.0f}; e.colorMul = 3.0f;
        e.velMin = {600, -50, 100}; e.velMax = {1200, 50, 400}; e.material = "FX_Materials_p.Materials.Spark_MAT"; return e; }();
    static ED rings = [] { ED e = base("Rings_Dup"); e.texture = kRing; e.burst = 1; e.rateMin = e.rateMax = 3;
        e.lifeMin = 0.35f; e.lifeMax = 0.5f; e.rotMin = -1; e.rotMax = 1; e.rotRateMin = -0.5f; e.rotRateMax = 0.5f;
        e.sizeMin = e.sizeMax = {120, 120, 1}; e.grow = {linear3({1, 1, 1}, {0.5f, 0.5f, 1})}; e.alpha = {kRingAlpha};
        // Trailing constant (150,0,0): role undecided (location vs velocity); used as a slow
        // velocity along the socket axis, since as a 1.5 m (x3 socket scale) offset it puts the
        // rings 4.5 m from the wheels [MED].
        e.velMin = e.velMax = {150, 0, 0}; e.color = {1.0f, 0.5f, 0.25f};
        e.material = "FX_Materials_p.Materials.Ring_Distort_Add_MAT"; e.loopMin = e.loopMax = 0.2f; return e; }();
    static ED electro = [] { ED e = base("Particle Emitter_Dup"); e.texture = kLightningRing; e.rateMin = e.rateMax = 20;
        e.duration = 0.2f; e.lifeMin = 0.1f; e.lifeMax = 0.2f; e.alpha = {kHold}; e.rotMin = 0; e.rotMax = 1;
        e.sizeMin = e.sizeMax = {50, 50, 1}; e.velMin = {10, 0, 0}; e.velMax = {250, 0, 0};
        e.color = {1.0f, 0.5f, 0.2f}; e.colorMul = 3.0f; e.material = "FX_Materials_p.Materials.ElectroRing01_Mat"; return e; }();
    static const float cx[21] = {1, 1.0043f, 1.0161f, 1.0339f, 1.0562f, 1.0815f, 1.1083f, 1.1351f, 1.1603f, 1.1824f, 1.2f,
                                 1.2064f, 1.1981f, 1.1785f, 1.1509f, 1.1185f, 1.0846f, 1.0525f, 1.0255f, 1.0069f, 1.0f};
    static const float cy[21] = {1, 1.0034f, 1.0129f, 1.0277f, 1.0467f, 1.0692f, 1.0941f, 1.1206f, 1.1477f, 1.1744f, 1.2f,
                                 1.2144f, 1.2107f, 1.193f, 1.1651f, 1.1308f, 1.0941f, 1.0587f, 1.0287f, 1.0078f, 1.0f};
    static const float cz[21] = {1, 1, 0.9999f, 0.9998f, 0.9997f, 0.9996f, 0.9995f, 0.9995f, 0.9996f, 0.9997f, 1.0f,
                                 1.0003f, 1.0004f, 1.0005f, 1.0005f, 1.0004f, 1.0003f, 1.0002f, 1.0001f, 1.0f, 1.0f};
    static ED lightcone = [] { ED e = base("lightcone_Dup"); e.mesh = kLightCyl; e.rateMin = e.rateMax = 10;
        e.killOnDeactivate = false; e.lifeMin = 1.5f; e.lifeMax = 2.0f;
        e.sizeMin = {0.3f, 0.3f, 0.075f}; e.sizeMax = {0.25f, 0.25f, 0.1f}; e.grow = {fromXYZ(cx, cy, cz, 21)};
        e.alpha = {{0.5f}}; e.bright = {kFlicker}; e.color = {1.0f, 0.1f, 0.1f}; e.colorMul = 2.0f;
        e.material = "FX_Materials_p.MatInst.LightCylinder_Rays_MAT_INST"; return e; }();
    static ED pulse = [] { ED e = base("Pulse"); e.mesh = kBooster03; e.rateMin = 3; e.rateMax = 4; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.4f; e.sizeMin = e.sizeMax = {0.8f, 4, 4}; e.alpha = {kRamp};
        e.color = {2.0f, 0.1f, 0.1f}; e.material = "FX_Navigation_p.Afterburn_02_MAT"; return e; }();
    // base_glow_Dup_Dup: burst 1 + 10/s per 0.5 s loop (EmitterLoops 0), life U[0.3,0.5], rotation U[-1,1]
    // turns, rotation rate U[-0.5,0.5], StartSize U[100,50] UU growing x1 -> 2, colour (1.5,1.1,1.1),
    // location U[10,20] UU along X; unassigned constants 1.0 / 0.0 / (0,0,0) [MED roles].
    static ED baseGlow = [] { ED e = base("base_glow_Dup_Dup"); e.texture = -1; e.materialOnly = true;
        e.material = "FX_Materials_p.Materials.Glow_Mod_MAT"; e.burst = 1; e.rateMin = e.rateMax = 10; e.loopMin = e.loopMax = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.5f; e.rotMin = -1; e.rotMax = 1; e.rotRateMin = -0.5f; e.rotRateMax = 0.5f;
        e.sizeMin = {100, 100, 1}; e.sizeMax = {50, 50, 1}; e.grow = {linear3({1, 1, 1}, {2, 2, 2})};
        e.color = {1.5f, 1.1f, 1.1f}; e.locMin = {10, 0, 0}; e.locMax = {20, 0, 0}; return e; }();
    // rays_Dup: velocity-aligned, 100/s per 2 s loop, life U[0.2,0.15], StartSize U[(0.5,1),(0.15,2)] x
    // SizeMultLife (4,20) -> (20,63.45) UU, velocity U[(200,-5,-5),(200,5,5)], colour
    // U[(0.9,0.5,0.4),(1,0.6,0.5)], alpha 1 -> 0 (21-entry), location -10 UU along X; unassigned
    // constants 0 / 0.5 / -1 / 35 / 10 [MED roles].
    static const float rayAlpha[21] = {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5625f, 0.525f, 0.4875f, 0.45f, 0.4125f, 0.375f,
                                       0.3375f, 0.3f, 0.2625f, 0.225f, 0.1875f, 0.15f, 0.1125f, 0.075f, 0.0375f, 0.0f};
    static ED rays = [] { ED e = base("rays_Dup"); e.texture = -1; e.materialOnly = true; e.velocityAligned = true;
        e.material = "FX_Materials_p.Materials.Trail_Distort_MAT"; e.rateMin = e.rateMax = 100; e.loopMin = e.loopMax = 2.0f;
        e.lifeMin = 0.2f; e.lifeMax = 0.15f; e.sizeMin = {0.5f, 1, 1}; e.sizeMax = {0.15f, 2, 1};
        e.grow = {linear3({4, 20, 2}, {20, 63.4487f, 2})}; e.velMin = {200, -5, -5}; e.velMax = {200, 5, 5};
        e.alpha = {std::vector<float>(rayAlpha, rayAlpha + 21)}; e.hasColorRange = true;
        e.color = {0.9f, 0.5f, 0.4f}; e.colorMax = {1.0f, 0.6f, 0.5f}; e.locX = -10; return e; }();
    v = {&sparks, &rings, &electro, &lightcone, &pulse, &baseGlow, &rays};
    return v;
}

// ======================= JumpFX: Jump_FX (one-shot, 0.5 s) =======================
const std::vector<const ED*>& jumpEmitters() {
    static std::vector<const ED*> v;
    if (!v.empty()) return v;
    static const float smx[21] = {1, 1.1358f, 1.4791f, 1.9334f, 2.4026f, 2.7902f, 3.0f, 2.9891f, 2.8897f, 2.7162f, 2.4829f,
                                  2.204f, 1.8938f, 1.5666f, 1.2367f, 0.9183f, 0.6258f, 0.3734f, 0.1755f, 0.0463f, 0};
    static const float smy[21] = {1, 1.1597f, 1.5556f, 2.0625f, 2.5556f, 2.9097f, 3.0f, 2.9255f, 2.7813f, 2.5796f, 2.3324f,
                                  2.0516f, 1.7493f, 1.4375f, 1.1283f, 0.8336f, 0.5656f, 0.3362f, 0.1574f, 0.0414f, 0};
    // thruster_streak / thruster_base growth (uniform curve, min == max): 0 -> peak at half life -> 1.
    static const float stx[21] = {0, 0.3f, 0.6f, 0.9f, 1.2f, 1.5f, 1.8f, 2.1f, 2.4f, 2.7f, 3.0f, 2.8f, 2.6f, 2.4f, 2.2f, 2.0f,
                                  1.8f, 1.6f, 1.4f, 1.2f, 1.0f};
    static const float sty[21] = {0, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.35f, 2.2f, 2.05f, 1.9f,
                                  1.75f, 1.6f, 1.45f, 1.3f, 1.15f, 1.0f};
    static const float spx[21] = {4, 3.9282f, 3.7296f, 3.4297f, 3.0537f, 2.627f, 2.175f, 1.723f, 1.2963f, 0.9203f, 0.6204f,
                                  0.4218f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f};
    static const float spy[21] = {5, 4.9156f, 4.6822f, 4.3296f, 3.8877f, 3.3861f, 2.8548f, 2.3234f, 1.8219f, 1.3799f, 1.0273f,
                                  0.7939f, 0.7095f, 0.7095f, 0.7095f, 0.7095f, 0.7095f, 0.7095f, 0.7095f, 0.7095f, 0.7095f};
    static const float one[21] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    static const float tsx[21] = {1, 0.9803f, 0.9259f, 0.8438f, 0.7407f, 0.6238f, 0.5f, 0.3762f, 0.2593f, 0.1562f, 0.0741f,
                                  0.0197f, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const float tsy[21] = {4, 3.9213f, 3.7037f, 3.375f, 2.963f, 2.4954f, 2.0f, 1.5046f, 1.037f, 0.625f, 0.2963f,
                                  0.0787f, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static ED glow = [] { ED e = base("Particle Emitter_Dup_Dup"); e.burst = 1; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.5f; e.rotMin = -1; e.rotMax = 1; e.rotRateMin = -0.5f; e.rotRateMax = 0.5f;
        e.alpha = {kHold}; e.sizeMin = {80, 80, 1}; e.sizeMax = {120, 120, 1}; e.grow = {linear3({1, 1, 1}, {5, 5, 5})};
        e.color = {4, 2, 1}; e.colorMul = 3.0f; e.locX = 50; e.material = "FX_Materials_p.Materials.Basic_Particle_Add_MAT"; return e; }();
    static ED smoke = [] { ED e = base("booster_smoke"); e.texture = kSmokeThin; e.rateMin = e.rateMax = 20; e.duration = 0.5f;
        e.lifeMin = 0.2f; e.lifeMax = 0.3f; e.rotMin = 0; e.rotMax = 1; e.alpha = {kSmokeAlpha};
        e.sizeMin = {30, 25, 25}; e.sizeMax = {50, 25, 25}; e.velMin = {900, -10, -10}; e.velMax = {1000, 10, 10};
        e.grow = {fromXYZ(smx, smy, smy, 21)}; e.color = {0.9301f, 0.3478f, 0.0664f}; e.locX = 50;
        e.material = "FX_Navigation_p.booster_smoke1_MAT"; return e; }();
    static ED thrBase2 = [] { ED e = base("thruster_base_Dup_Dup"); e.mesh = kBooster03; e.rateMin = e.rateMax = 3;
        e.duration = 0.5f; e.lifeMin = e.lifeMax = 0.2f; e.alpha = {kThrBase}; e.sizeMin = e.sizeMax = {3, 3, 3};
        e.color = {0.4f, 0.2f, 0.1f}; e.colorMul = 3.0f; e.locX = 50; e.material = "FX_Navigation_p.Afterburn_02_MAT"; return e; }();
    static ED glow2 = [] { ED e = base("Particle Emitter_Dup_Dup_Dup"); e.burst = 1; e.rateMin = e.rateMax = 10;
        e.duration = 0.5f; e.lifeMin = 0.6f; e.lifeMax = 0.8f; e.rotMin = -1; e.rotMax = 1; e.rotRateMin = -0.5f;
        e.rotRateMax = 0.5f; e.alpha = {kHold}; e.sizeMin = {60, 60, 1}; e.sizeMax = {100, 100, 1};
        e.grow = {linear3({1, 1, 1}, {2, 2, 2})}; e.color = {4, 2, 1}; e.colorMul = 3.0f; e.locX = 50;
        e.material = "FX_Materials_p.Materials.Basic_Particle_Add_MAT"; return e; }();
    static ED tailsparks = [] { ED e = base("tailsparks"); e.texture = kSpark; e.velocityAligned = true;
        e.rateMin = e.rateMax = 100; e.delay = 0.2f; e.duration = 0.5f; e.lifeMin = 0.2f; e.lifeMax = 0.25f;
        e.alpha = {kRamp}; e.sizeMin = {4, 12, 1}; e.sizeMax = {3, 10, 1}; e.grow = {fromXYZ(tsx, tsy, tsy, 21)};
        e.velMin = {600, -300, -300}; e.velMax = {700, 300, 300}; e.colorLife = {linear3({1, 1, 1}, {0.8f, 0, 0})};
        e.color = {0.9965f, 0.3048f, 0.1016f}; e.colorMul = 3.0f; e.locX = 50; e.material = "FX_Materials_p.Materials.Spark_MAT"; return e; }();
    static ED energon2 = [] { ED e = base("thruster_Energon_Dup_Dup"); e.mesh = kCircuit; e.rateMin = 1; e.rateMax = 2;
        e.duration = 0.5f; e.lifeMin = e.lifeMax = 0.2f; e.alpha = {kRamp}; e.sizeMin = e.sizeMax = {6, 5, 5};
        e.color = {5, 2, 1}; e.colorMul = 3.0f; e.locX = 50; e.material = "FX_Navigation_p.Afterburn_Circuitry_02_MAT"; return e; }();
    static ED sparks = [] { ED e = base("Sparks_burst"); e.texture = kSpark; e.velocityAligned = true; e.burst = 30;
        e.rateMin = e.rateMax = 100; e.duration = 0.3f; e.lifeMin = 0.1f; e.lifeMax = 0.5f; e.alpha = {kRamp};
        e.sizeMin = {2, 12, 1}; e.sizeMax = {1, 8, 1}; e.grow = {fromXYZ(spx, spy, one, 21)};
        e.velMin = {600, -50, 100}; e.velMax = {1200, 50, 400}; e.colorLife = {linear3({1, 1, 1}, {0.8f, 0.8f, 1})};
        e.color = {5, 4, 4}; e.colorMul = 3.0f; e.locX = 50; e.material = "FX_Materials_p.Materials.Spark_MAT"; return e; }();
    static ED streak = [] { ED e = base("thruster_streak_Dup"); e.mesh = kBooster03; e.burst = 3; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.5f; e.alpha = {kRamp}; e.sizeMin = e.sizeMax = {7, 5, 5};
        e.grow = {fromXYZ(stx, sty, sty, 21)}; e.color = {4, 2, 0.5f}; e.colorMul = 3.0f; e.locX = 50;
        e.material = "FX_Navigation_p.Afterburn_02_MAT"; return e; }();
    static ED thrBase = [] { ED e = base("thruster_base_Dup"); e.mesh = kBooster03; e.burst = 3; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.5f; e.alpha = {kRamp}; e.sizeMin = e.sizeMax = {3, 3, 3};
        e.grow = {fromXYZ(stx, sty, sty, 21)}; e.color = {4, 2, 1}; e.colorMul = 3.0f; e.locX = 50;
        e.material = "FX_Navigation_p.Afterburn_02_MAT"; return e; }();
    static ED electro = [] { ED e = base("Particle Emitter_Dup"); e.texture = kLightningRing; e.rateMin = e.rateMax = 20;
        e.duration = 0.2f; e.lifeMin = 0.1f; e.lifeMax = 0.2f; e.alpha = {kHold}; e.rotMin = 0; e.rotMax = 1;
        e.sizeMin = e.sizeMax = {100, 100, 1}; e.velMin = {10, 0, 0}; e.velMax = {800, 0, 0};
        e.grow = {linear3({1, 1, 1}, {4, 4, 4})}; e.color = {6, 4, 2}; e.colorMul = 3.0f; e.locX = 50;
        e.material = "FX_Materials_p.Materials.ElectroRing01_Mat"; return e; }();
    static ED energon = [] { ED e = base("thruster_Energon_Dup"); e.mesh = kCircuit; e.burst = 3; e.duration = 0.5f;
        e.lifeMin = 0.3f; e.lifeMax = 0.4f; e.alpha = {kRamp}; e.sizeMin = e.sizeMax = {6, 5, 5};
        e.grow = {{{0, 0, 0}, {3, 2.5f, 2.5f}, {3, 2.5f, 2.5f}}}; e.color = {5, 2, 1}; e.colorMul = 3.0f; e.locX = 50;
        e.material = "FX_Navigation_p.Afterburn_Circuitry_02_MAT"; return e; }();
    static ED thrBase3 = [] { ED e = base("thruster_base_Dup_Dup_Dup"); e.mesh = kBooster03; e.rateMin = 1; e.rateMax = 3;
        e.duration = 0.5f; e.lifeMin = e.lifeMax = 0.2f; e.alpha = {kThrBase}; e.sizeMin = e.sizeMax = {6, 4, 4};
        e.color = {0.4f, 0.2f, 0.1f}; e.colorMul = 3.0f; e.locX = 50; e.material = "FX_Navigation_p.Afterburn_02_MAT"; return e; }();
    v = {&glow, &smoke, &thrBase2, &glow2, &tailsparks, &energon2, &sparks, &streak, &thrBase, &electro, &energon, &thrBase3};
    return v;
}

// ======================= RamFX: Truck_ram_FX (looping while the nitro runs) =======================
// Emitter "None": Ram_STAT mesh (Ram_model_MAT), SpawnRate 20/s, looping; distributions in order:
// 1.0, 8.0, 0, 0, 0, 0.35, (2,1.8,1.3), (-250,0,-10), curve 1. Read as life 1.0 s, alpha 0.35,
// HDR colour (2,1.8,1.3), location -250 UU (the wedge envelops the truck nose) [MED roles].
// Material-only (no GL1 path): "dust" (Distortion_Cloud_01_MAT) and "rays_Dup" (Trail_Distort_MAT, world space).
const std::vector<const ED*>& ramEmitters() {
    static std::vector<const ED*> v;
    if (!v.empty()) return v;
    static ED shell = [] { ED e = base("Ram"); e.mesh = kRamMesh; e.rateMin = e.rateMax = 20;
        e.lifeMin = e.lifeMax = 1.0f; e.alpha = {{0.35f}}; e.color = {2.0f, 1.8f, 1.3f};
        e.locX = -250; e.material = "FX_Materials_p.Materials.Ram_model_MAT"; return e; }();
    // dust: burst 1 + 50/s per loop of U[0.1,0.2] s, rotation U[-1,1] turns, rate U[-0.5,0.5], life U[0.4,0.35],
    // alpha bell 0 -> 0.468 -> 0, location U[(300,-200,0),(300,200,200)], StartSize U[500,600] UU x 0.2 -> 1,
    // velocity U[(-2000,-400,0),(-3000,400,0)], colour 0.2; unassigned constant 0 [MED roles].
    static const float dustA[21] = {0, 0.0148f, 0.0546f, 0.1128f, 0.1824f, 0.2568f, 0.3291f, 0.3926f, 0.4405f, 0.466f,
                                    0.464f, 0.4395f, 0.3976f, 0.3429f, 0.2802f, 0.2142f, 0.1497f, 0.0913f, 0.0437f, 0.0117f, 0};
    static ED dust = [] { ED e = base("dust"); e.texture = -1; e.materialOnly = true;
        e.material = "FX_Materials_p.Materials.Distortion_Cloud_01_MAT"; e.burst = 1; e.rateMin = e.rateMax = 50;
        e.loopMin = 0.1f; e.loopMax = 0.2f; e.rotMin = -1; e.rotMax = 1; e.rotRateMin = -0.5f; e.rotRateMax = 0.5f;
        e.lifeMin = 0.4f; e.lifeMax = 0.35f; e.alpha = {std::vector<float>(dustA, dustA + 21)};
        e.locMin = {300, -200, 0}; e.locMax = {300, 200, 200}; e.sizeMin = {500, 500, 1}; e.sizeMax = {600, 600, 1};
        e.grow = {linear3({0.2f, 0.2f, 0.2f}, {1, 1, 1})}; e.velMin = {-2000, -400, 0}; e.velMax = {-3000, 400, 0};
        e.color = {0.2f, 0.2f, 0.2f}; return e; }();
    // rays_Dup (world space, bUseLocalSpace unset): velocity-aligned, 600/s per 2 s loop, life U[0.25,0.05],
    // alpha 0 -> 1 -> 0 (21-entry), StartSize U[(0.2,2),(0.2,8)] x (4,45) -> (20,63.45), velocity
    // U[(-7000,-100,-120),(-5000,100,100)], location (0,0,150), colour 0.5; constants 0/1/150/100 unassigned.
    static const float rayA[21] = {0.0061f, 0.253f, 0.4999f, 0.7468f, 0.9937f, 0.8155f, 0.6261f, 0.4368f, 0.2932f, 0.2688f,
                                   0.2444f, 0.2199f, 0.1955f, 0.171f, 0.1466f, 0.1222f, 0.0977f, 0.0733f, 0.0489f, 0.0244f, 0};
    static ED rays = [] { ED e = base("rays_Dup"); e.texture = -1; e.materialOnly = true; e.velocityAligned = true;
        e.worldSpace = true; e.material = "FX_Materials_p.Materials.Trail_Distort_MAT"; e.rateMin = e.rateMax = 600; e.loopMin = e.loopMax = 2.0f;
        e.lifeMin = 0.25f; e.lifeMax = 0.05f; e.alpha = {std::vector<float>(rayA, rayA + 21)};
        e.sizeMin = {0.2f, 2, 1}; e.sizeMax = {0.2f, 8, 1}; e.grow = {linear3({4, 45, 2}, {20, 63.4487f, 2})};
        e.velMin = {-7000, -100, -120}; e.velMax = {-5000, 100, 100}; e.locMin = e.locMax = {0, 0, 150};
        e.color = {0.5f, 0.5f, 0.5f}; return e; }();
    v = {&shell, &dust, &rays};
    return v;
}

float frand() { return (float)std::rand() / (float)RAND_MAX; }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
Vec3 lerpRand(const Vec3& a, const Vec3& b) { float t = frand(); return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t)}; }
Vec3 lerpRand3(const Vec3& a, const Vec3& b) { return {lerp(a.x, b.x, frand()), lerp(a.y, b.y, frand()), lerp(a.z, b.z, frand())}; }
Vec3 col(const core::Mat4& m, int c) { return {m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]}; }

// HDR colour -> (clamped base colour, GL overbright 1/2/4).
void hdr(const Vec3& c, float mul, float& r, float& g, float& b, float& scale) {
    float m = std::max(c.x, std::max(c.y, c.z)) * mul;
    scale = m >= 4.0f ? 4.0f : (m >= 2.0f ? 2.0f : 1.0f);
    r = std::min(1.0f, c.x * mul / scale); g = std::min(1.0f, c.y * mul / scale); b = std::min(1.0f, c.z * mul / scale);
}

// Spark_MAT has no texture: its graph builds a procedural soft streak from texture coordinates.
// Approximated by a generated falloff (bright core along V, soft across U). [PROV]
render::ImageData sparkImage() {
    render::ImageData img; img.w = 16; img.h = 64; img.rgba.resize((size_t)img.w * img.h * 4);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            float u = std::fabs((x + 0.5f) / img.w * 2.0f - 1.0f), v = std::fabs((y + 0.5f) / img.h * 2.0f - 1.0f);
            float i = (1.0f - u) * (1.0f - u) * (1.0f - v * v);
            uint8_t c = (uint8_t)std::min(255.0f, i * 255.0f);
            uint8_t* p = &img.rgba[((size_t)y * img.w + x) * 4];
            p[0] = p[1] = p[2] = c; p[3] = 255;
        }
    return img;
}

} // namespace

const VehicleFx::SocketDef& VehicleFx::socketDef(int s) { return kSockets[s]; }

Vec3 VehicleFx::Curve3::eval(float t) const {
    if (v.empty()) return {1, 1, 1};
    if (v.size() == 1) return v[0];
    float x = core::clampf(t, 0.0f, 1.0f) * (float)(v.size() - 1);
    size_t i = (size_t)x;
    if (i + 1 >= v.size()) return v.back();
    return v[i] + (v[i + 1] - v[i]) * (x - (float)i);
}
float VehicleFx::Curve1::eval(float t) const {
    if (v.empty()) return 1.0f;
    if (v.size() == 1) return v[0];
    float x = core::clampf(t, 0.0f, 1.0f) * (float)(v.size() - 1);
    size_t i = (size_t)x;
    if (i + 1 >= v.size()) return v.back();
    return v[i] + (v[i + 1] - v[i]) * (x - (float)i);
}

const std::vector<const ED*>& VehicleFx::emitters(System s) const {
    return s == Boost ? boostEmitters() : (s == Hover ? hoverEmitters() : (s == Jump ? jumpEmitters() : ramEmitters()));
}

void VehicleFx::load(render::IRenderer& r, const std::string& contentRoot) {
    tex_.assign(kTexCount, render::kInvalidTexture);
    for (int i = 0; i < kTexCount; ++i) {
        render::ImageData img;
        if (kTexPaths[i] ? platform::decodeImage(contentRoot + kTexPaths[i], img) : (img = sparkImage(), true))
            tex_[(size_t)i] = r.uploadTexture(img);
    }
    meshes_.assign(kMeshCount, render::kInvalidMesh);
    int ok = 0;
    for (int i = 0; i < kMeshCount; ++i) {
        render::MeshData md;
        if (!assets::loadGlb(contentRoot + kMeshes[i].gltf, md)) continue;
        render::ImageData img;
        render::TextureHandle th = render::kInvalidTexture;
        if (platform::decodeImage(contentRoot + kMeshes[i].texture, img)) th = r.uploadTexture(img);
        for (render::Material& m : md.mats) { m.tex = th; m.color = {1, 1, 1}; }
        meshes_[(size_t)i] = r.uploadMesh(md);
        if (meshes_[(size_t)i] != render::kInvalidMesh) ++ok;
    }
    int texOk = 0; for (render::TextureHandle t : tex_) if (t >= 0) ++texOk;
    LOG_INFO("vehicle fx: %d/%d meshes, %d/%d sprite textures (boost/hover/jump: %zu/%zu/%zu emitters)",
             ok, (int)kMeshCount, texOk, (int)kTexCount, boostEmitters().size(), hoverEmitters().size(),
             jumpEmitters().size());
}

void VehicleFx::setSocket(int s, const core::Mat4* world) {
    if (s < 0 || s >= kSocketCount) return;
    have_[s] = world != nullptr;
    if (world) sockets_[s] = *world;
}

int VehicleFx::start(System sys, int socket) {
    Inst in;
    in.id = nextId_++; in.sys = sys; in.socket = socket; in.age = 0.0f; in.active = true;
    const auto& em = emitters(sys);
    in.acc.assign(em.size(), 0.0f);
    in.rate.resize(em.size());
    in.loopT.assign(em.size(), 0.0f);
    in.loopLen.resize(em.size());
    for (size_t i = 0; i < em.size(); ++i) {
        in.rate[i] = lerp(em[i]->rateMin, em[i]->rateMax, frand());
        in.loopLen[i] = lerp(em[i]->loopMin, em[i]->loopMax, frand());
    }
    insts_.push_back(in);
    for (const ED* d : em)
        if (d->delay <= 0.0f) for (int k = 0; k < d->burst; ++k) spawn(*d, insts_.back());
    return in.id;
}

void VehicleFx::deactivate(int id) {
    for (Inst& in : insts_) if (in.id == id) in.active = false;
    for (size_t i = 0; i < parts_.size();) {
        if (parts_[i].inst == id && parts_[i].def->killOnDeactivate) { parts_[i] = parts_.back(); parts_.pop_back(); }
        else ++i;
    }
}

bool VehicleFx::alive(int id) const {
    for (const Inst& in : insts_) if (in.id == id) return true;
    return false;
}

void VehicleFx::spawn(const EmitterDef& d, const Inst& in) {
    Part p{};
    p.def = &d; p.inst = in.id; p.socket = in.socket;
    p.age = 0.0f;
    p.life = lerp(d.lifeMin, d.lifeMax, frand());
    Vec3 s1 = lerpRand(d.sizeMin, d.sizeMax), s2 = lerpRand(d.size2Min, d.size2Max);
    p.size = {s1.x * s2.x, s1.y * s2.y, s1.z * s2.z};
    p.rot = lerp(d.rotMin, d.rotMax, frand()) * 2.0f * core::PI;
    p.rotRate = lerp(d.rotRateMin, d.rotRateMax, frand()) * 2.0f * core::PI;
    Vec3 lo = lerpRand3(d.locMin, d.locMax);   // UE local (x, y, z) -> socket glTF-local (x, z, y)
    p.pos = Vec3{d.locX + lo.x, lo.z, lo.y} * UU;
    Vec3 v = lerpRand3(d.velMin, d.velMax);
    p.vel = Vec3{v.x, v.z, v.y} * UU;
    if (d.hasColorRange) {
        Vec3 c = lerpRand3(d.color, d.colorMax);
        p.tint = {d.color.x > 0 ? c.x / d.color.x : 1, d.color.y > 0 ? c.y / d.color.y : 1, d.color.z > 0 ? c.z / d.color.z : 1};
    }
    if (d.worldSpace && have_[in.socket]) {        // emitted in world space: fixed where spawned
        const core::Mat4& S = sockets_[in.socket];
        p.world = true;
        p.sockScale = core::length(col(S, 0));
        p.pos = core::transformPoint(S, p.pos);
        p.vel = core::transformDir(S, p.vel);
    }
    parts_.push_back(p);
}

void VehicleFx::tick(float dt) {
    for (size_t i = 0; i < insts_.size();) {
        Inst& in = insts_[i];
        in.age += dt;
        bool anySpawning = false;
        if (in.active && have_[in.socket]) {
            const auto& em = emitters(in.sys);
            for (size_t e = 0; e < em.size(); ++e) {
                const ED& d = *em[e];
                float t = in.age - d.delay;
                if (t < 0.0f) { anySpawning = true; continue; }   // EmitterDelay not yet elapsed
                // A delayed emitter's burst fires on the step its delay elapses.
                if (d.delay > 0.0f && t - dt < 0.0f) for (int k = 0; k < d.burst; ++k) spawn(d, in);
                bool running = d.duration <= 0.0f || t < d.duration;
                if (!running) continue;
                anySpawning = true;
                // EmitterLoops 0: every new loop restarts the emitter time, so its BurstList fires again.
                if (in.loopLen[e] > 0.0f && d.duration <= 0.0f) {
                    in.loopT[e] += dt;
                    while (in.loopT[e] >= in.loopLen[e]) {
                        in.loopT[e] -= in.loopLen[e];
                        in.loopLen[e] = lerp(d.loopMin, d.loopMax, frand());   // EmitterDuration range per loop
                        for (int k = 0; k < d.burst; ++k) spawn(d, in);
                    }
                }
                in.acc[e] += in.rate[e] * dt;
                while (in.acc[e] >= 1.0f) { in.acc[e] -= 1.0f; spawn(d, in); }
            }
        }
        bool hasParts = false;
        for (const Part& p : parts_) if (p.inst == in.id) { hasParts = true; break; }
        if (!anySpawning && !hasParts && in.age > 0.0f) { insts_[i] = insts_.back(); insts_.pop_back(); continue; }
        if (!anySpawning) in.active = false;          // one-shot system finished spawning
        ++i;
    }
    for (size_t i = 0; i < parts_.size();) {
        Part& p = parts_[i];
        p.age += dt;
        if (p.age >= p.life) { parts_[i] = parts_.back(); parts_.pop_back(); continue; }
        p.pos += p.vel * dt;
        p.rot += p.rotRate * dt;
        ++i;
    }
}

void VehicleFx::draw(render::IRenderer& r) const {
    // Original-material path (Rendering evaluates the compiled emitter material graphs): the particle
    // colour is the authored HDR value, unclamped and without GL1 stand-ins (overbright quantisation,
    // intensity scales, fresnel approximations). Otherwise the GL1 fallback below.
    const bool graphs = r.evaluatesFxMaterials();
    std::vector<render::Particle> q;
    for (System sys : {Boost, Hover, Jump, Ram}) {
        for (const ED* d : emitters(sys)) {
            const bool viaMaterial = graphs && d->material;
            if (d->materialOnly && !viaMaterial) continue;     // distortion / modulate: no substitute visual
            float br, bg, bb, scale;
            if (viaMaterial) { br = d->color.x * d->colorMul; bg = d->color.y * d->colorMul; bb = d->color.z * d->colorMul; scale = 1.0f; }
            else hdr(d->color, d->colorMul, br, bg, bb, scale);
            if (d->mesh >= 0) {
                render::MeshHandle h = meshes_[(size_t)d->mesh];
                if (h == render::kInvalidMesh) continue;
                const MeshAsset& ma = kMeshes[d->mesh];
                for (const Part& p : parts_) {
                    if (p.def != d || !have_[p.socket]) continue;
                    float t = p.age / p.life;
                    Vec3 g = d->grow.eval(t), cl = d->colorLife.eval(t);
                    float k = d->bright.eval(t);
                    // Mesh scale is authored in UE axes; umodel meshes are (UE x, UE z, UE y).
                    Vec3 s{p.size.x * g.x, p.size.z * g.z, p.size.y * g.y};
                    core::Mat4 model = sockets_[p.socket] * core::Mat4::translate(p.pos) * core::Mat4::scale(s);
                    if (viaMaterial) {
                        r.drawMeshFx(h, model, br * cl.x * k * p.tint.x, bg * cl.y * k * p.tint.y,
                                     bb * cl.z * k * p.tint.z, d->alpha.eval(t), 1.0f);
                    } else {
                        float mi = ma.intensity;
                        r.drawMeshFx(h, model, std::min(1.0f, br * cl.x * k * mi), std::min(1.0f, bg * cl.y * k * mi),
                                     std::min(1.0f, bb * cl.z * k * mi), d->alpha.eval(t), scale,
                                     ma.fresnelExp, ma.fresnelScale, ma.fresnelPower);
                    }
                }
                continue;
            }
            q.clear();
            for (const Part& p : parts_) {
                if (p.def != d || (!p.world && !have_[p.socket])) continue;
                float t = p.age / p.life;
                Vec3 g = d->grow.eval(t), cl = d->colorLife.eval(t);
                float k = d->bright.eval(t);
                render::Particle o;
                float sockScale;
                Vec3 wv;
                if (p.world) {
                    o.pos = p.pos; wv = p.vel; sockScale = p.sockScale;
                } else {
                    const core::Mat4& S = sockets_[p.socket];
                    sockScale = core::length(col(S, 0));
                    o.pos = core::transformPoint(S, p.pos);
                    wv = core::transformDir(S, p.vel);
                }
                o.w = p.size.x * g.x * UU * sockScale;
                o.h = p.size.y * g.y * UU * sockScale;
                if (d->velocityAligned) {
                    o.axis = core::length(wv) > 1e-5f ? wv
                           : (p.world ? Vec3{0, 1, 0} : col(sockets_[p.socket], 0));
                } else {
                    o.rot = p.rot;
                }
                if (viaMaterial) {
                    o.r = br * cl.x * k * p.tint.x; o.g = bg * cl.y * k * p.tint.y; o.b = bb * cl.z * k * p.tint.z;
                } else {
                    o.r = std::min(1.0f, br * cl.x * k * p.tint.x); o.g = std::min(1.0f, bg * cl.y * k * p.tint.y);
                    o.b = std::min(1.0f, bb * cl.z * k * p.tint.z);
                }
                o.a = d->alpha.eval(t);
                q.push_back(o);
            }
            if (q.empty()) continue;
            render::TextureHandle tex = d->texture >= 0 && (size_t)d->texture < tex_.size() ? tex_[(size_t)d->texture]
                                                                                          : render::kInvalidTexture;
            if (!viaMaterial && tex == render::kInvalidTexture) continue;
            render::ParticleBatch b{tex, render::ParticleBlend::Additive, scale, q.data(), q.size()};
            if (viaMaterial) b.material = d->material;
            r.drawParticles(b);
        }
    }
}

} // namespace game
