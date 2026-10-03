// Clean-room reconstruction — WFC original-data shading pipeline (GL 3.3 shaders).
//
// Reproduces the original Xbox 360 UE3 render path as recovered from the game's own data:
//  * per-material GLSL translated offline from the cooked material graphs (tools/render/matc.py)
//  * base pass with FDirectionalTextureLightMapPolicy: L = sum_i dot(N_t,B_i)^2 * LM_i * Scale_i
//    (basis + squared weights decoded from the original Xenon shader microcode)
//  * WFC "UberLight" for dynamic objects: ambient cube + up to 3 lights with wrapped-squared
//    diffuse sat(N.L*0.6778+0.3333)^2 and Phong specular pow(sat(R.L), SpecularPower)
//  * UE3 per-vertex height fog, linear-light HDR target, DisplayGamma 2.2 resolve.
#pragma once
#include <map>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>
#include "render/gl/GLExt.h"
#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Renderer.h"
#include "render/LightVisibilityVolume.h"

namespace render {
namespace wfc {

struct Light {
    int type = 0;                 // 0 point, 1 spot, 2 directional, 3 sky (upper/lower hemisphere)
    core::Vec3 pos{0, 0, 0};
    core::Vec3 dir{0, -1, 0};     // light's forward (direction light travels)
    core::Vec3 color{1, 1, 1};    // linear colour * brightness (sky: upper hemisphere)
    core::Vec3 lowerColor{0, 0, 0}; // sky: lower hemisphere
    float radius = 10.0f;         // metres
    float falloff = 2.0f;
    float cosOuter = 0.0f, invConeRange = 1.0f;
    bool chStatic = true, chDynamic = true, castShadows = true, enabled = true;
    std::string name;             // source light component (frame report)
    bool castDynamicShadows = true, castCompositeShadow = false, hasLightFunction = false;
    float brightness = 1.0f;
    core::Vec3 colorByte{1, 1, 1};   // LightColor / 255 (DirectLightEnv ranking colour)
    uint32_t chMask = 0;             // LightingChannels (MSB-first bits)
    float radiusOfInfluence = -0.01f;   // metres (class default -1 UU)
    bool castStaticShadows = true;
    float spotCosI = 1.0f, spotCosO = 0.0f, spotOuterRad = 0.0f;   // IntensityAt clamped cones (0x82E2CFE0)
    LightVisibilityVolume::Guid guid;
    float modShadowColor[4] = {0, 0, 0, 1};   // FLinearColor (ModShadowColor)
    float shadowFalloffExponent = 2.0f;
    int minShadowResolution = 0, maxShadowResolution = 0;   // LightComponent +0x134 / +0x138 (0 = system)
};

// The light environment's synthetic shadow light (one slot per DirectLightEnv, ReverseEngineering 7033f18):
// rewritten from the composite shadow record on every proxy build (0x82DE6598 ->
// UpdateShadowLight_{Directional,Point,Spot} 0x82DDD128 / 0x82DDD240 / 0x82DDD368); flags cleared when
// there is no record. It is never the scene light itself.
struct ShadowProjector {
    bool on = false;              // LSI+0xF8 0x80000000|0x20000000 and Scene.Lights +0x20 0x60000000
    int type = 0;                 // 1 directional, 2 point, 3 spot
    int source = -1;              // index into lights_ of the composite light it copies
    core::Vec3 pos{0, 0, 0}, dir{0, -1, 0};   // LightToWorld (glTF metres; dir = light forward)
    float radius = 0.0f;          // metres (+0x174; directional 327680 UU)
    float falloff = 2.0f;         // ShadowFalloffExponent (+0x178; directional 2.0)
    float cosOuter = 0.0f, invConeRange = 1.0f;
    int minRes = 0, maxRes = 0;   // +0x100 / +0x104
    float modShadowColor[4] = {1, 1, 1, 1};   // +0x10C = shadowFactor
    int updates = 0, sourceChanges = 0;
};
// Build the record from the composite light and its shadowFactor and apply it to the slot (or clear it).
void updateShadowProjector(ShadowProjector& p, const Light* composite, int lightIndex, float shadowFactor);

// FPrimitiveViewRelevance bits used by shadow creation / projection (0x82DD8D58, 0x82C8BCF0).
struct ShadowSubject {
    bool castShadow = true, castDynamicShadow = true, castHiddenShadow = false;   // PrimitiveComponent +0xF0
    bool hidden = false, hasShadowParent = false;
    int dpg = 0;                  // DepthPriorityGroup (SDPG_World 0)
    bool useViewOwnerDPG = false; int viewOwnerDPG = 0; bool viewIsOwner = false;
    float maxDrawDistance = 0.0f; // 0 = unlimited
    float distSq = 0.0f;          // |bounds origin - view origin|^2 (LODDistanceFactor 1)
};
uint32_t shadowViewRelevance(const ShadowSubject& s);   // bit 0x4 = IsShadowCast(View), bit (5 + DPG)

// Native shadow constants (ReverseEngineering 13c0953): GetShadowDepthResolution 0x830101E0 and the
// BranchingPCF projection SetParameters 0x82CFB598. Shipped Xe-TransEngine.ini values.
constexpr int kMaxShadowResolution = 1024;
constexpr float kMaskedShadowsDepthBias = 0.2f;
constexpr float kShadowFilterRadius = 6.0f;
int shadowDepthResolution(int maxShadowResolution);                       // clamp(Max, 1, 2048)
float shadowDepthBiasParabolic(int res, float maskedShadowsDepthBias, float shadowFilterRadius);
bool shadowProjectionAllowed(uint32_t subjectViewRelevance, int dpg);
int shadowResolution(float screenRadius, int lightMin, int lightMax);   // CreateProjectedShadow 0x83056A20
extern const float kEdgeSampleOffsets[8];       // 4 x float2 (0x83711DFC)
extern const float kRefiningSampleOffsets[24];  // 12 x float2 (0x83711EC0)
// DirectLightAmbientContribution (FDirectLightEnv::BuildSceneProxyData 0x82CCE0A8):
// CubeSum(LightsSH) / (CubeSum(AmbientSH + LightsSH) + 0.001) per channel, w = 0.
void directLightAmbientContribution(const core::Vec3 total[6], const core::Vec3 lights[6], float out[3]);

// Lighting inputs of a dynamic (or unbuilt static) primitive, UE3 light-environment style.
struct LightEnv {
    core::Vec3 cube[6];           // ambient cube irradiance: +X -X +Y -Y +Z -Z (glTF axes)
    int n = 0;
    float pos[3][4], dir[3][4], col[3][4], spot[3][4];
    int light[3] = {-1, -1, -1};  // index into lights_ (frame report)
    float dlac[3] = {0, 0, 0};    // DirectLightAmbientContribution (DirectLightEnv only)
};

struct Program {
    GLuint id = 0;
    GLint uViewProj = -1, uModel = -1, uCamPos = -1, uTime = -1, uTwoSided = -1, uClip = -1, uMasked = -1,
          uLit = -1, uLMCoord = -1, uLMScale = -1, uAmb = -1, uNumLights = -1, uLPos = -1, uLDir = -1,
          uLCol = -1, uLSpot = -1, uFogOn = -1, uFogMaxH = -1, uFogScale = -1, uFogStart = -1, uFogExt = -1,
          uFogIn = -1;
    struct Slot { int unit; GLuint tex; bool cube; float umin[4]; float uscale[4]; };
    GLint uRT[3] = {-1, -1, -1}, uRTSet[3] = {-1, -1, -1};   // applier params (Cust_Color_A/B, EnergonColor)
    std::vector<Slot> slots;
    int blend = 0;                // 0 opaque, 1 masked, 2 translucent, 3 additive, 4 modulate
    bool twoSided = false, lit = true;
    bool original = false;        // compiled from the original material graph (not the glTF fallback)
    bool sceneDepth = false;      // reads scene depth (DepthBiasedAlpha / SceneDepth)
    int distProg = -1;            // distortion-accumulate variant (material Distortion connected)
    int shadowProg = -1;          // shadow-depth variant (opaque/masked: depth, masked clip)
    float clip = 0.3333f;
};

class Pipeline {
public:
    bool load(const std::string& mapName);
    bool active() const { return active_; }
    void setVisibility(IRenderer::VisibilityQuery q) { vis_ = std::move(q); visMemo_.clear(); }
    void setCharacterColors(const CharacterColors& c) { charColors_ = c; }

    void beginFrame(const Camera& cam, int w, int h);
    void endFrame();

    // Returns a GPU mesh id, or -1 (caller falls back to the legacy path).
    int upload(const MeshData& m);
    void draw(int id, const core::Mat4& model);
    void drawDynamic(const MeshData& m, const core::Mat4& model);
    // Effects shaded by their original material graphs; `color` is the particle colour (vertex colour,
    // HDR). drawFx returns false when the mesh has no compiled original material (caller falls back).
    bool drawFx(int id, const core::Mat4& model, const float color[4]);
    struct Sprite { core::Vec3 c[4]; float uv[4][2]; float color[4]; };
    bool drawSprites(const char* material, const Sprite* s, size_t n, const core::Vec3& facing);

private:
    struct Sub {
        uint32_t first = 0, count = 0;
        int prog = -1;
        std::string matName;      // original material path (diagnostics)
        std::string comp;         // source component (diagnostics: WFC_SKIPMAT "comp:<substring>")
        int lmTex[3] = {-1, -1, -1};
        float lmScale[3][3] = {};
        float lmCoord[4] = {1, 1, 0, 0};
        GLuint vlmTex = 0;        // vertex (LMT_1D) lightmap: RGB32F, width = vertices, rows = coefficients
        bool noLights = false;    // authored: receives no light (bAcceptsLights false / no lighting channels)
        int vlmBase = 0;          // first vertex of the component in the VBO (gl_VertexID - base)
        core::Vec3 bmin, bmax;
        bool envReady = false;
        LightEnv env;
    };
    struct GpuMesh {
        GLuint vao = 0, vbo = 0, ibo = 0;
        std::vector<Sub> subs;
        bool world = false;
        bool drawsBsp = false;
        bool decal = false;       // static decal geometry (clip to decal box)    // replaced its level-BSP submeshes with the lit bspMesh_
    };

    GLuint texture(const std::string& file, bool srgb, bool clampU, bool clampV);
    GLuint cubeTexture(const std::vector<std::string>& faces, bool srgb);
    int programFor(const std::string& matName, const Material* gltfMat, bool lightmapped);
    std::string resolveBySourceName(const Material* m) const;
    int buildProgram(const std::string& key, const std::string& body, const std::vector<Program::Slot>& slots,
                     const std::vector<bool>& slotIsCube, int blend, bool twoSided, bool lit, float clip,
                     bool lightmapped, const std::vector<std::string>& rtParams = {});
    void computeEnv(const core::Vec3& p, bool dynamicObject, LightEnv& env) const;
    // Light-visibility samples of the drawing character's LightEnvironmentComponent
    // (NormalizedSampleOffsets, glTF axes) and its world bounds; null = single centre trace.
    const std::vector<core::Vec3>* envSamples_ = nullptr;
    // Frame report (WFC_FRAMEREPORT=<file>, written on the WFC_SMOKE_FRAMES frame): what was drawn.
    struct FrameDraw { int draws = 0; int blend = 0; bool lit = false, lightmapped = false, vertexLM = false,
                       distortion = false, dynamic = false, fx = false; };
    std::map<std::string, FrameDraw> frameDraws_;
    std::vector<std::string> frameEnvs_;
    bool frameFx_ = false;
    void writeFrameReport();
    core::Vec3 envBoundsCenter_, envBoundsExtent_;
    void bindCommon(const Program& P, const core::Mat4& model);
    void drawSubs(GpuMesh& g, const core::Mat4& model, bool dynamicObject);
    void ensureTargets(int w, int h);
    static void buildVertices(const MeshData& m, std::vector<float>& v);

    // Authored post-process (map TnWorldInfo.DefaultPostProcessSettings over Default__WorldInfo).
    struct Post {
        bool bloom = true, dof = false;
        float bloomScale = 1.0f, bloomThreshold = 1.0f;
        float dofPacked[4] = {0, 1.0f / 2000.0f, 4.0f, 1.0f / 2000.0f};
        float dofMaxBlur[2] = {1.0f, 1.0f};
        core::Vec3 shadows{0, 0, 0}, highlights{1, 1, 1}, midtones{1, 1, 1};
        float desat = 0.0f;
    } post_;
    GLuint clutTex_ = 0;
    int clutSize_ = 32;
    float znear_ = 0.1f, zfar_ = 20000.0f;
    CharacterColors charColors_;   // default all-zero -> every override skipped (authored values)
    int testMesh_ = -1;           // WFC_TESTMESH render verification hook
    core::Mat4 testModel_;
    int bspMesh_ = -1;            // BSP rebuilt from the cooked vertex buffer with its lightmaps
    int decalMesh_ = -1;          // static decals from their cooked receiver geometry
    bool active_ = false;
    std::string dataDir_;
    IRenderer::VisibilityQuery vis_;
    // light-visibility memo: (light, 0.25 m cell) -> occluded. Static lights + static world make the
    // trace a function of position; the cell matches the 0.3 m light-environment reuse tolerance.
    mutable std::unordered_map<uint64_t, bool> visMemo_;

    // render data
    struct MatSrc { std::vector<std::string> rtParams; std::string glsl; std::vector<std::string> files; std::vector<std::vector<std::string>> faces; std::vector<bool> srgb, cube, clampU, clampV;
                    std::vector<std::vector<float>> umin, umax; int blend = 0; bool twoSided = false, lit = true;
                    float clip = 0.3333f; };
    std::map<std::string, MatSrc> mats_;
    std::map<std::string, std::string> slotMaterials_;   // "mesh|section" -> original material
    struct LMRec { std::string coeff[3]; float scale[3][3]; float cs[2], cb[2]; };
    std::map<std::string, LMRec> lightmaps_;
    std::map<std::string, std::string> actorComponent_;   // actor (lower) -> its only lightmapped component
    struct VertexLM { int count = 0; std::vector<float> rgb; float scale[3][3]; };
    std::map<std::string, VertexLM> vertexLMs_;          // component (lower) -> decoded samples
    std::set<std::string> hiddenComponents_, noLightComponents_;   // authored render flags (lower-case keys)
    LightVisibilityVolume lvv_;   // WFC LightsVisibilitiesVolume (native layout, b52dca9)
    // WFC DirectLightEnv per character form (0 robot, 1 vehicle): WfcDirectLightEnv.cpp
    struct DirectLightEnvState {
        bool initialized = false, wasMoving = false;
        float lastTickTime = -1.0f, pendingDt = 0.0f;
        core::Vec3 lastActorPos, velUE, curCenter, curExtent, lastUpdatePosUE, originGltf, extentGltf;
        int fullUpdates = 0, queuedFull = 0, lastFullFrame = -1;
        std::vector<int> known;
        struct PerLight { bool hasVis = false; float vis = 0.0f; int stagger = 0; };
        std::map<int, PerLight> lights;
        uint32_t rng = 12345u;
        LightEnv env;
        int directCount = 0, shadowLight = -1, shadowCandidates = 0, lastFrame = -1;
        float crossfade = 1.0f, shadowStrength = 0.0f;
        int shadowTop = -1;                       // top composite candidate (before the 0.05 drop)
        float shadowTopScore = 0, shadowTopVis = 0, shadowNextScore = -1;
        ShadowProjector projector;                // the environment's synthetic shadow light
    };
    std::map<int, DirectLightEnvState> dle_;
    int envForm_ = -1;                 // form of the dynamic mesh being drawn (-1 none)
    float intensityAt(const Light& l, const core::Vec3& pGltf) const;
    uint32_t envChannels(int form) const;
    bool lightAffectsEnv(const Light& l, uint32_t envCh, const core::Vec3& c, float radius) const;
    void tickDirectLightEnv(int form, const core::Vec3& boundsCenter, const core::Vec3& boundsExtent,
                            const core::Vec3& actorPos);
    void doDirectLightEnvUpdate(int form, bool full);
    void runDirectLightEnvSelfTest();
    GLuint shadowMaskTexFor(bool character);
    GLuint neutralMaskTex_ = 0, testMaskTex_ = 0;
    bool dynamicMaskDraw_ = false;
    struct DleQueueEntry { int form; int mode; int deadline; };   // global queue 0x83833084
    std::vector<DleQueueEntry> dleQueue_;
    std::vector<core::Vec3> dleRobotSamples_, dleVehicleSamples_;
    double statUpdateMs_ = 0.0;
    int statEnvCalls_ = 0, statVisCalls_ = 0, statLvvQueries_ = 0;
    double statLvvMs_ = 0.0;
    std::vector<Light> lights_;
    bool fogOn_ = false;
    float fogMaxH_ = 0, fogScale_ = 0, fogStart_ = 0, fogExt_ = 1e8f;
    core::Vec3 fogIn_{0, 0, 0};

    std::map<std::string, GLuint> texCache_;
    std::map<std::string, int> progIndex_;
    std::vector<Program> progs_;
    std::vector<GpuMesh> meshes_;
    std::vector<GLuint> lmTextures_;
    std::map<std::string, int> lmIndex_;
    GLuint whiteTex_ = 0, blackTex_ = 0, flatNormalTex_ = 0, blackCube_ = 0;

    // Dynamic-object light environments, rebuilt only when the owner moves more than
    // UpdateDistanceThreshold (TnRobotForm/TnVehicleForm: 30 UU = 0.3 m) [CONF].
    struct EnvCache { core::Vec3 pos; LightEnv env; int lastFrame = 0; };
    std::vector<EnvCache> envCache_;
    // Small short-lived dynamic meshes (shells, mesh particles): one shared environment per 1 m cell,
    // refreshed every kCellEnvFrames frames, as UE3 mesh emitters share their particle system's
    // light environment instead of each particle tracing its own.
    struct CellEnv { LightEnv env; int frame = -100000; };
    std::unordered_map<uint64_t, CellEnv> cellEnv_;
    int frameNo_ = 0;

    // dynamic stream
    GLuint dynVao_ = 0, dynVbo_ = 0, dynIbo_ = 0;
    // Keyed by material CONTENT, not address: dynamic meshes (the character pose buffer) reuse their
    // storage across robot/vehicle, so a pointer key handed the vehicle the robot's programs.
    std::map<std::string, int> dynProgCache_;

    // frame
    core::Mat4 viewProj_;
    core::Vec3 camPos_;
    float time_ = 0.0f;
    float frustum_[6][4] = {};
    int vpW_ = 0, vpH_ = 0;
    GLuint fbo_ = 0, colorTex_ = 0, depthTex_ = 0, postProg_ = 0, postVao_ = 0;
    GLuint depthCopyFbo_ = 0, depthCopyTex_ = 0;   // scene depth for translucent (soft) materials
    // UE3 distortion: accumulate (RGBA8, additive, scene depth tested) then apply before post
    GLuint distFbo_ = 0, distTex_ = 0, sceneCopyFbo_ = 0, sceneCopyTex_ = 0, distApplyProg_ = 0;
    bool distUsed_ = false;
    const char* mainOverride_ = nullptr;
    void applyDistortion();
    // Modulated projected shadows (WfcShadows.cpp): non-native stages from the cooked shaders.
    struct ShadowRequest { int light = -1; int res = 0; core::Vec3 origin{0, 0, 0}; core::Mat4 viewProj; float zRow[4] = {0, 0, 0, 0};
                           float invMaxSubjectDepth = 1, depthBias = 0; float modColor[4] = {1, 1, 1, 1}; };
    GLuint shadowFbo_ = 0, shadowDepthTex_ = 0, shadowProjProg_ = 0, randomAnglesTex_ = 0;
    GLuint maskDepthProg_ = 0, constProg_ = 0, volVao_ = 0, volVbo_ = 0;
    GLuint maskFbo_ = 0, maskTex_ = 0, maskDepthRb_ = 0;
    int maskW_ = 0, maskH_ = 0, maskForW_ = 0, maskForH_ = 0, maskClearedFrame_ = -1, maskDrawnFrame_ = -1;
    float maskTexelOffset_[2] = {0, 0};
    int randomAnglesSize_ = 0;
    int statShadowProj_ = 0, statShadowGated_ = 0;
    GLuint maskBlurProg_ = 0, maskTmpFbo_ = 0, maskTmpTex_ = 0, maskBlurFbo_ = 0, maskBlurTex_ = 0;
    core::Mat4 camProj_;
    struct ShadowFrameInfo { int form = -1; int source = -1; int type = 0; int res = 0; float factor = 1.0f; };
    std::vector<ShadowFrameInfo> shadowFrame_;
    void blurShadowMask();
    const ShadowProjector* projectorFor(int form, ShadowProjector& scratch) const;
    bool ensureShadowPrograms();
    void ensureShadowMask();
    void beginShadowMask();
    void fillMaskDepth();
    void drawShadowVolume(const core::Vec3 corners[8], const core::Mat4& vp, GLuint prog);
    void castCharacterShadow(GpuMesh& g, const core::Mat4& model, const ShadowProjector& p);
    bool renderShadowDepth(GpuMesh& g, const core::Mat4& model, const ShadowProjector& p, ShadowRequest& rq);
    void depthPrepass(GpuMesh& g, const core::Mat4& model);
    void runShadowMaskSelfTest();
    bool depthDirty_ = true;
    void ensureSceneDepth();
    float fxColor_[4] = {1, 1, 1, 1};
    GLuint spriteVao_ = 0, spriteVbo_ = 0, spriteCbo_ = 0, spriteIbo_ = 0;
    std::map<std::string, int> spriteProg_;
    std::string resolveName(const std::string& name) const;
    GLuint bloomGatherProg_ = 0, blurProg_ = 0, bloomFbo_[2] = {0, 0}, bloomTex_[2] = {0, 0};
    int bloomW_ = 1, bloomH_ = 1;
    int fbW_ = 0, fbH_ = 0;
};

} // namespace wfc
} // namespace render
