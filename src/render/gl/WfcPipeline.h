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
#include <array>
#include <tuple>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <functional>
#include "render/gl/GLExt.h"
#include "assets/Json.h"
#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Renderer.h"
#include "render/LightVisibilityVolume.h"
#include "assets/SkinnedModel.h"

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
    std::map<std::string, std::pair<GLint, GLint>> rtLoc;      // every runtime parameter: (uRT_, uRTSet_)
    // per-draw uniform locations by literal name (looked up once per program; key = the literal's address)
    mutable std::vector<std::pair<const char*, GLint>> locCache;
    std::vector<Slot> slots;
    int blend = 0;                // 0 opaque, 1 masked, 2 translucent, 3 additive, 4 modulate
    bool twoSided = false, lit = true;
    bool original = false;        // compiled from the original material graph (not the glTF fallback)
    bool sceneDepth = false;      // reads scene depth (DepthBiasedAlpha / SceneDepth)
    bool sceneColor = false;      // reads the resolved scene colour (SceneTexture)
    std::string material;         // original material path, lower case (material-parameter routing)
    int distProg = -1;            // distortion-accumulate variant (material Distortion connected)
    int shadowProg = -1;          // shadow-depth variant (opaque/masked: depth, masked clip)
    int screenProg = -1;          // HUD post-process chain variant (EmissiveColor x ScreenAlpha, full screen)
    int instProg = -1;            // instanced character variant (per-instance uniforms from the instance texture)
    int mdiProg = -1;             // world MDI variant (per-draw constants from the row texture, row via aDrawRow)

    int instRtCount = 0;          // runtime params laid out in the instance row (sorted by name)
    float clip = 0.3333f;
};

// Cumulative shader compiles / texture creations (WFC_SLOWFRAME first-use evidence).
extern unsigned long gShaderCompiles, gTexCreates;

class Pipeline {
public:
    bool load(const std::string& mapName);
    // Shared read-only asset roots: WFC_ASSETS (default core::config::kAssetRootDefault, the VerticalSlice export)
    // and the content directory beside it (WFC_CONTENT overrides). Map render data stays in WFC_RENDER_DATA.
    static std::string assetRoot();
    static std::string renderDataRoot();              // WFC_RENDER_DATA, else the first work/render above the exe holding data
    static std::string contentRoot();
    void release();                                   // delete every GL object, reset to the unloaded state
    static void clearProgramCache();                  // M54: linked programs kept across loads (renderer teardown)
    void prewarmMaterials();                          // M54: effect / weapon materials, yielding (map loads)
    void requestMaterialPrewarm() { prewarmPending_ = true; }   // run at the end of the world mesh upload
    void skipMaterialPrewarm() { prewarmDone_ = true; prewarmPending_ = false; }   // frontend scenes
    void prewarmPlacedFx();                           // M58: the placed particle components (frontend scenes)
    int spriteProgram(const std::string& material);  // particle material program (cached; -1 = fallback)
    void setLoadYield(std::function<void()> y) { loadYield_ = std::move(y); }
    void yieldLoad() { if (loadYield_ && !inLoadYield_) { inLoadYield_ = true; loadYield_(); inLoadYield_ = false; glx::uniformCacheForgetCurrent(); } }
    // a loading-screen frame from anywhere in a load: the renderer's own yield (inside loadMapRenderData) or the
    // process-wide core::loadYield when the tree has it (the world mesh upload / warm-up run outside the former)
    void loadStep(const char* where);
    // Canvas material tile (UE3 FCanvas::DrawMaterialTile): queued, drawn after post onto the back buffer.
    void drawMaterialTile(const IRenderer::MaterialTile& t) {   // pooled: copy-assign reuses each slot's capacity
        if (uiTileCount_ < uiTiles_.size()) uiTiles_[uiTileCount_] = t; else uiTiles_.push_back(t);
        ++uiTileCount_;
    }
    bool hasMaterial(const std::string& m) const { return mats_.count(m) > 0; }
    bool active() const { return active_; }
    void setVisibility(IRenderer::VisibilityQuery q) { vis_ = std::move(q); visMemo_.clear(); }
    void setCharacterColors(const CharacterColors& c) { charColorsBy_[drawOwner_] = c; }
    // M75 loading-screen warm-up: one hidden, unculled draw of the world into the scene target (w x h) so the driver's
    // first-draw work (state-dependent shader finalisation, texture residency) is paid under the loading screen
    void warmupWorld(int meshId, int w, int h);
    // M74 energy death: the draw owner's dynamic materials swap to their package's Defrag instance at `defrag`
    void setDrawEnergyDeath(float defrag) {
        if (defrag < 0.0f) { ownerDefrag_.erase(drawOwner_); clearDrawMaterialParam("Defrag"); return; }
        ownerDefrag_[drawOwner_] = defrag;
        const float v[4] = {defrag, defrag, defrag, 1.0f};
        setDrawMaterialParam("Defrag", v);
    }
    const std::string* energyDeathFor(const std::string& material) const;
    bool isTranslucentMaterial(const std::string& material) const {   // compiled blend Translucent / Additive / Modulate
        auto it = mats_.find(material);
        return it != mats_.end() && it->second.blend >= 2;
    }
    // M73 runtime decals (spawnDecal): projected geometry built by the caller; expiry on the map clock, cap 50
    int addRuntimeDecal(MeshData&& mesh, float lifetime);
    size_t runtimeDecalCount() const { return rtDecals_.size(); }
    // M70 per-owner runtime material parameters for dynamic draws (held weapon SetMaterialParameter)
    void setDrawMaterialParam(const std::string& name, const float v[4]) {
        auto& L = ownerParams_[drawOwner_];
        for (auto& kv : L) if (kv.first == name) { std::copy(v, v + 4, kv.second.begin()); return; }
        L.push_back({name, {v[0], v[1], v[2], v[3]}});
    }
    void clearDrawMaterialParam(const std::string& name) {
        auto& L = ownerParams_[drawOwner_];
        L.erase(std::remove_if(L.begin(), L.end(), [&](const auto& kv) { return kv.first == name; }), L.end());
    }
    void setDrawOwner(int o) { drawOwner_ = o < 0 ? 0 : o; }
    void setDisplayGamma(float g) { displayGamma_ = g > 0.5f && g < 5.0f ? g : 2.2f; }
    // per-frame draw counters (always on, cheap) and resource counts for IRenderer::renderDiagnostics
    struct FrameCounts {
        int draws = 0, worldDraws = 0, bspDraws = 0, dynamicDraws = 0, fxDraws = 0;
        int opaque = 0, translucent = 0, lightmapped = 0, culled = 0, noProgram = 0;
        int opaqueNoDepthTest = 0;          // opaque draws issued with GL_DEPTH_TEST disabled (M11 regression)
        int materials = 0, programs = 0;
        std::vector<std::string> noProgramMats;
    };
    const FrameCounts& lastFrameCounts() const { return lastCounts_; }
    // WFC_SLOWFRAME: cumulative map FX time (ms, total / simulation share)
    double fxMsTotal() const { return statFxMsCum_; }
    double fxSimMsTotal() const { return statFxTickMsCum_; }
    int frameNumber() const { return frameNo_; }
    const std::string& dataDir() const { return dataDir_; }
    size_t materialCount() const { return mats_.size(); }
    size_t programCount() const { return progs_.size(); }
    size_t textureCount() const { return texCache_.size(); }
    size_t lightmapCount() const { return lmTextures_.size(); }
    size_t meshCount() const { return meshes_.size(); }
    const core::Mat4& viewProjMatrix() const { return viewProj_; }
    const core::Vec3& cameraPos() const { return camPos_; }
    static std::string& lastLoadError() { static std::string e; return e; }   // survives release()
    int scenePosesApplied() const { return posesApplied_; }
    const std::set<std::string>& scenePosesUnknown() const { return posesUnknown_; }
    void setActorPose(const std::string& actor, const core::Vec3& posUE, const core::Vec3& rotUEdeg);
    void setActorScale(const std::string& actor, float drawScale);
    // Downward trace against the level BSP (includes invisible collision brushes): the highest surface at or below
    // zFrom under (x, y), UE units. False if nothing is below.
    bool groundBelowUE(float x, float y, float zFrom, float& zHit) const;
    // frontend pose of a scene actor in UE space: world = M * (x - L0) + L1, M = DrawScale ratio * A1 A0^T (columns)
    bool frontendPoseUE(const std::string& actorLower, float M[9], float L0[3], float L1[3]) const;
    void loadSceneActors(const assets::Json& actorsByLevel);   // render_index actors_by_level (UI families)
    void loadSceneNonDrawnActors(const assets::Json& actorsByLevel, const assets::Json& cameras);   // cameras, lens flares

    void beginFrame(const Camera& cam, int w, int h);
    void endFrame();

    // Returns a GPU mesh id, or -1 (caller falls back to the legacy path).
    int upload(const MeshData& m);
    void draw(int id, const core::Mat4& model);
    // cacheKey / serial (drawDynamicMeshPosed): a persistent vertex buffer per key, rebuilt only when the serial changes
    // GPU skinning (IRenderer::drawSkinnedMesh): false when unsupported (too many joints) - the caller CPU-skins
    bool drawSkinned(const MeshData& bind, const std::vector<uint16_t>& joints, const std::vector<float>& weights,
                     const std::vector<core::Mat4>& palette, const std::vector<core::Mat4>* prevPalette, float alpha,
                     const core::Mat4& model, const void* key, uint64_t serial);
    // prevP / prevN (drawDynamicMeshBlended): the previous step's pose, blended in the vertex shader by alpha
    void drawDynamic(const MeshData& m, const core::Mat4& model, const void* cacheKey = nullptr, uint64_t serial = 0,
                     const std::vector<float>* prevP = nullptr, const std::vector<float>* prevN = nullptr, float alpha = 1.0f);
    void prewarmDynamic(const MeshData& m);   // resolve drawDynamic's programs / textures without drawing
    // Effects shaded by their original material graphs; `color` is the particle colour (vertex colour,
    // HDR). drawFx returns false when the mesh has no compiled original material (caller falls back).
    bool drawFx(int id, const core::Mat4& model, const float color[4]);
    struct Sprite {
        core::Vec3 c[4]; float uv[4][2]; float color[4];
        float uv2[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}}; float blend = 0.0f;   // M67 second SubUV cell + interp
    };
    bool drawSprites(const char* material, const Sprite* s, size_t n, const core::Vec3& facing);

private:
    struct Sub {
        uint32_t first = 0, count = 0;
        int prog = -1;
        std::string matName;      // original material path (diagnostics)
        int matKey = -1;          // interned matName (per-frame distinct-material count without hashing the string)
        int mdiRow = -1;          // world MDI: this sub's row in the per-draw constant texture (-1 = drawn singly)
        std::string comp;         // source component (diagnostics: WFC_SKIPMAT "comp:<substring>")
        int lmTex[3] = {-1, -1, -1};
        float lmScale[3][3] = {};
        float lmCoord[4] = {1, 1, 0, 0};
        GLuint vlmTex = 0;        // vertex (LMT_1D) lightmap: RGB32F, width = vertices, rows = coefficients
        bool noLights = false;    // authored: receives no light (bAcceptsLights false / no lighting channels)
        bool dynChannel = false;  // authored LightingChannels = Dynamic only: Dynamic-channel lights, per frame
        int vlmBase = 0;          // first vertex of the component in the VBO (gl_VertexID - base)
        core::Vec3 bmin, bmax;
        bool envReady = false;
        LightEnv env;
        std::string actor;        // lower-case actor name of actor-placed world nodes (movers / hidden state)
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
    std::string resolveBySourceNameUncached(const Material* m) const;
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
    FrameCounts counts_, lastCounts_;
    std::unordered_set<std::string> frameMats_;   // (unused per draw since the interned keys; kept for reports)
    std::unordered_map<std::string, int> matKeys_;   // material name -> key
    std::vector<int> matSeenFrame_;                  // key -> last frame it was drawn
    int frameMatCount_ = 0;
    std::set<std::string> frameNoProg_;
    std::vector<char> progSeen_;
    std::vector<std::string> frameEnvs_;
    bool frameFx_ = false;
    void writeFrameReport();
    core::Vec3 envBoundsCenter_, envBoundsExtent_;
    void bindCommon(const Program& P, const core::Mat4& model);
    void drawSubs(GpuMesh& g, const core::Mat4& model, bool dynamicObject, int onlySub = -1);
    // UE3 translucency pass: every translucent primitive is drawn after all opaque geometry, sorted back to front
    // by the view-space depth of its bounds origin (FTranslucentPrimSet). Translucent subs of persistent meshes
    // and sprite batches are queued during the frame and drawn by flushTranslucency().
    // a deferred sprite batch keeps its data so adjacent same-state batches can merge at flush (exact: same order)
    struct SpriteBatch { std::string mat; core::Vec3 facing; float dyn[4]; std::vector<Sprite> sprites; };
    // The sorted translucency queue holds plain records (300+ fps lobbies: a std::function + shared_ptr per item were
    // heap allocations per translucent sub / sprite batch per frame). kind 0: a world / mesh sub (transSubs_[idx]);
    // kind 1: a sprite batch (spritePool_[idx], pooled - its vectors keep their capacity across frames).
    struct TransSub { long meshIdx; int sub; bool dynamicObject, fx; core::Mat4 mdl; float col[4], dyn[4]; };
    struct TransItem { float key; int kind; int idx; };
    std::vector<TransSub> transSubs_;
    std::vector<SpriteBatch> spritePool_;
    size_t spriteUsed_ = 0;
    // drawSprites' upload scratch and its one-sub mesh, reused per call
    std::vector<float> spriteV_, spriteCol_, spriteSub_;
    std::vector<uint32_t> spriteIdx_;
    GpuMesh spriteMesh_;
    // the frame sprite stream (flushTranslucency): every sprite group's vertices, uploaded once per frame
    // growable float buffer that is never zero-filled (every element is written before the upload)
    struct RawFloats {
        std::unique_ptr<float[]> p;
        size_t n = 0, cap = 0;
        void clear() { n = 0; }
        size_t size() const { return n; }
        const float* data() const { return p.get(); }
        float* grow(size_t add) {
            if (n + add > cap) {
                const size_t nc = std::max(cap * 2, n + add);
                std::unique_ptr<float[]> q(new float[nc]);
                if (n) std::memcpy(q.get(), p.get(), n * sizeof(float));
                p = std::move(q); cap = nc;
            }
            float* r = p.get() + n;
            n += add;
            return r;
        }
    };
    RawFloats spriteFrameV_, spriteFrameCol_, spriteFrameSub_;
    GLuint spriteFrameVao_ = 0, spriteFrameVbo_ = 0, spriteFrameCbo_ = 0, spriteFrameSbo_ = 0, spriteFrameIbo_ = 0;
    size_t spriteFrameIboQuads_ = 0;
    GpuMesh spriteFrameMesh_;
    void spriteCoverage(const char* material, const Sprite* sp, size_t n);
    void spriteAppend(const Sprite* sp, size_t n, const core::Vec3& facing, RawFloats& v, RawFloats& col, RawFloats& sub);
    int statSpriteBatches_ = 0, statSpriteMerged_ = 0;   // WFC_RENDERSTATS
    // GPU-spike evidence (a long GPU frame is reported 3 frames later): per-frame sprite count, total screen coverage
    // (in screens) and the materials that covered most - overdraw from effects at the camera is the usual suspect
    struct FrameRec { int frame = -1; int sprites = 0, draws = 0; double coverage = 0; std::map<std::string, double> matCov; };
    FrameRec frameRecs_[4];
public:
    std::string frameRecordText(int frame) const;
private:
    std::vector<TransItem> transQueue_;
    std::function<void()> loadYield_;
    bool inLoadYield_ = false;
    std::vector<IRenderer::MaterialTile> uiTiles_;
    size_t uiTileCount_ = 0;                                   // tiles queued this frame (uiTiles_ is a pool)
    void drawCanvasTiles();
    float displayGamma_ = 2.2f;                        // Xe-TransEngine.ini DisplayGamma / profile Brightness
    float canvasInvGamma_ = 0.0f;                      // > 0 while drawing Canvas tiles
    const std::vector<std::pair<std::string, std::array<float, 4>>>* drawParams_ = nullptr;   // per-draw runtime params
    std::map<int, std::vector<std::pair<std::string, std::array<float, 4>>>> ownerParams_;   // M70 by draw owner
    std::map<int, float> ownerDefrag_;                    // M74 energy death by draw owner
    struct BeastVolume { float loc[3], scale[3]; int n[3]; int priority; std::vector<float> sh; };
    std::vector<BeastVolume> beast_;                      // M09 Beast probe grids (beast_probes.json)
    // the probes' ambient cube at a UE point, glTF face order (+X, -X, +Y up, -Y, +Z, -Z); false outside every volume
    bool beastAmbient(const core::Vec3& ueP, core::Vec3 cube[6]) const;
    bool warmup_ = false;
    // first-use residency (09c first-InGame-frame / first-death GPU spikes): every texture created is referenced once by
    // a 1x1 off-screen draw at the next frame start (or at the end of the world warm-up), so the driver's first-use
    // work (residency / upload) happens where the texture was created (loading screen, prewarm), not where it is seen
    std::vector<std::pair<GLuint, bool>> touchQueue_;     // (texture, cube)
    std::vector<int> progTouchQueue_;                     // programs linked since the last touch (drawn once each)
    int touchedPrograms_ = 0;
    GLuint touchProg2D_ = 0, touchProgCube_ = 0, touchFbo_ = 0, touchTex_ = 0;
    int touchedTextures_ = 0;
    void touchNewTextures();
    // GPU skinning: static bind-pose buffers per model, palettes in rows of an RGBA32F texture per character instance
    static constexpr int kMaxBones = 128;                 // per palette (largest MP skeleton: 89 joints)
    struct SkinModel {
        GLuint vao = 0, vbo = 0, jwVbo = 0, ibo = 0;
        size_t verts = 0, idx = 0;
        int joints = 0;                                   // highest influencing joint + 1
        std::vector<core::Vec3> jc; std::vector<float> jr;   // per joint: bind-space centre / radius of its vertices
        // exact bounds, reduced: per joint the rigid (single influence, weight 1) vertices that can be extreme under a
        // rigid transform (hull candidates), plus every blended vertex (evaluated with the full skinPose sum)
        std::vector<std::vector<core::Vec3>> hullPts;
        std::vector<std::vector<float>> hullSoA;          // per joint: x[n4] y[n4] z[n4], n4 = count padded to 4 (SSE2)
        std::vector<uint32_t> blended;
        size_t boundsPts = 0;
        int lastFrame = 0;
    };
    struct SkinInst { int row = -1; uint64_t serial = ~0ull; bool prev = false; core::Vec3 mn, mx, pmn, pmx; int lastFrame = 0; };
    std::map<const void*, SkinModel> skinModels_;
    int statSkinRebuilds_ = 0;                            // skinned-model (re)builds since the last 600-frame log
    std::map<const void*, SkinInst> skinInsts_;
    std::vector<int> freeSkinRows_;
    int skinRowsUsed_ = 0;
    GLuint skinTex_ = 0;
    static constexpr int kSkinRows = 512;
    struct SkinDraw { GLuint vao; core::Vec3 mn, mx; };
    const SkinDraw* skinDraw_ = nullptr;                  // drawDynamic: a GPU-skinned draw (no vertex build / scan)
    int skinMode_ = 0, skinRow_ = 0, skinBones_ = 0;      // VS: 0 off, 1 skin, 2 skin + blend with the prev palette
    float skinAlpha_ = 1.0f;
    void evictSkin(bool all);
    void buildSkinBoundsSets(SkinModel& sm, const MeshData& bind, const std::vector<uint16_t>& joints, const std::vector<float>& weights);
    // Instanced character draws (300+ fps lobbies): an opaque GPU-skinned character sub is queued with the exact
    // per-draw uniform values its own draw would have used (read back from the GL uniform cache), and drawn with the
    // other instances of the same program / mesh range in one instanced draw. Any other draw / blit / scene copy
    // flushes the queue first (flushInstancesHook), so nothing can observe a pending character. WFC_NOINSTANCING=1.
    struct InstGroup { int instProg; GLuint vao; uint32_t first, count; float viewProj[16]; float camPos[3]; GLint depthFunc;
                       std::vector<float> rows; int n = 0; };
    std::vector<InstGroup> instGroups_;
    std::map<std::tuple<int, GLuint, uint32_t, uint32_t>, size_t> instGroupIndex_;
    GLuint instTex_ = 0;
    int instCursor_ = 0;
    bool inInstFlush_ = false, instWanted_ = false, instBuild_ = false;
    static constexpr int kInstW = 64, kInstRows = 4096;
    bool queueInstance(const Program& P, uint32_t first, uint32_t count, GLuint vao);
public:
    void flushInstances();
private:
    // bindCommon: per GL PROGRAM OBJECT (several Program entries can share one object - identical source, different
    // clip / blend uniforms), the inputs of the uniforms only bindCommon writes as last set on it; the block is skipped
    // while they are unchanged (GL keeps uniforms per program object: the skip cannot change a draw). Invalidated when
    // a program is linked (recycled names start from defaults).
    struct CommonKey { float v[34]; bool valid = false; };
    std::vector<CommonKey> commonKeyById_;
    // World MDI (WFC_MDI=1, opt-in until verified on every map): the world's opaque, static, non-vertex-lightmapped
    // subs are drawn per (program, lightmap page) bucket with glMultiDrawElementsIndirect. Each sub's per-draw
    // constants (lightmap coordinate transform / scale, static light environment) live in a row of an RGBA32F
    // texture; the row index reaches the shaders through a divisor-1 vertex attribute (location 11) and the command's
    // baseInstance. Per frame only the CPU frustum cull writes the indirect commands. Draw ORDER among those opaque
    // subs changes (exact except equal-depth ties: verified by image A/B per map).
    struct MdiBucket { int prog; int lm[3]; std::vector<uint32_t> subs; };
    std::vector<MdiBucket> mdiBuckets_;
    long mdiMesh_ = -1;
    GLuint mdiRowTex_ = 0, mdiRowVbo_ = 0, mdiCmdBuf_ = 0;
    // Lightmap pages of the common size share one GL_TEXTURE_2D_ARRAY (unit 21); each page's 2D texture becomes a
    // texture view of its layer (same storage, exact texels / mips), so MDI buckets key on the program alone for them.
    GLuint lmArray_ = 0;
    std::vector<int> lmLayer_;                 // lmTextures_ index -> array layer, -1 = separate texture
    void buildLmArray();
    std::vector<float> mdiRows_;                           // CPU copy (light environments filled at first sight)
    std::vector<char> mdiEnvFilled_;
    bool mdiWanted_ = false, mdiBuild_ = false;
    static constexpr int kMdiW = 24;
    void buildMdi(int meshId);
    void drawMdi(GpuMesh& g);
    int poseBlend_ = 0;                                   // vertex-shader pose blend for the current draw (attribs 7 / 8)
    float poseAlpha_ = 1.0f;
    int hudEffect_ = -1;
    std::unordered_map<int, float> ownerRenderedTime_;   // draw owner -> time_ when last rendered (light env LastRenderTime)
    std::map<int, std::chrono::steady_clock::time_point> ownerRendered_;   // draw owner -> last rendered (not culled)                                  // HUD post-process chain (-1 none, 0 static discharge, 1 low health)
    GLuint screenFxVao_ = 0, screenFxVbo_ = 0, screenFxIbo_ = 0;
    void drawHudScreenEffect();
public:
    void setHudScreenEffect(int chain) { hudEffect_ = chain < 0 ? -1 : (chain > 1 ? 1 : chain); }
    // seconds of renderer time (lockstep: frame-based, deterministic) since the owner was last actually drawn
    float drawOwnerRenderAge(int owner) const {
        auto it = ownerRenderedGame_.find(owner);
        if (it == ownerRenderedGame_.end()) return -1.0f;
        return std::max(time_ - it->second, 0.0f);
    }
    std::unordered_map<int, float> ownerRenderedGame_;   // owner -> time_ of the last render (markers / FX relevance)
    void setEmitterPoolCap(bool on) { poolCapOn_ = on; }
    bool poolCapOn_ = false;                              // EmitterPool MaxActiveEffects 50 (extended lobbies)
private:
    struct PosedBuf {
        GLuint vao = 0, vbo = 0, ibo = 0, prevVbo = 0;
        uint64_t serial = ~0ull;
        size_t verts = 0, idx = 0;
        int lastFrame = 0;
        bool blended = false;                       // the buffer holds raw cur normals + a prev-pose stream (attribs 7 / 8)
        core::Vec3 mn{0, 0, 0}, mx{0, 0, 0};        // cur pose bounds (cached per serial; no per-frame vertex scans)
        core::Vec3 pmn{0, 0, 0}, pmx{0, 0, 0};      // prev pose bounds
    };
    std::map<const void*, PosedBuf> posed_;              // drawDynamicMeshPosed buffers (Milestone E)
    // drawDynamic's per-mesh draw list (300+ fps lobbies: the sub list, programs, material-name strings and the
    // light-environment form were rebuilt per call - allocations per sub per body per frame). Keyed by the MeshData;
    // the signature (sub ranges / material indices, material-name buffers, defrag state) must match or it is rebuilt.
    struct DynSubs {
        GpuMesh g;
        std::vector<uint64_t> sig;
        int envKind = -1;          // -1 none, 0 robot, 1 vehicle
        bool weapon = false;
        int lastFrame = 0;
    };
    std::unordered_map<const MeshData*, DynSubs> dynSubs_;
    void evictPosed(bool all);
    double statFxTickMs_ = 0.0;                          // map FX simulation share of statFxMs_
    double statFxMsCum_ = 0.0, statFxTickMsCum_ = 0.0;   // never reset (WFC_SLOWFRAME deltas)
    std::map<std::string, int> statFxSpawns_;            // runtime spawns per template (WFC_RENDERSTATS)
    int hiddenGameSkipped_ = 0;
    std::map<std::string, std::string> matErrors_;        // materials_glsl.json entries without GLSL: their error
    std::set<std::string> warnedMatErrors_;                                 // M75: warm-up draw in progress (no frustum culling)
    std::map<std::string, std::string> energyDeath_;      // M74 lower-case mesh package -> Defrag instance
    bool inDynamicDraw_ = false;
    std::map<std::string, std::string> miaMaterial_;   // MaterialInstanceActor (lower) -> MIC path (lower)
    std::map<std::string, std::vector<std::pair<std::string, std::array<float, 4>>>> matParams_;   // MIC -> params
    bool deferTrans_ = false, flushingTrans_ = false;
    float viewDepth(const core::Vec3& p) const;
public:
    void flushTranslucency();
private:
    void ensureTargets(int w, int h);
    static void buildVertices(const MeshData& m, std::vector<float>& v, bool rawNormals = false);

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
    std::map<int, CharacterColors> charColorsBy_;   // per draw owner; default all-zero -> overrides skipped
    int drawOwner_ = 0;                              // character instance of the current dynamic draws
    // Pawn occlusion (WFC: stock UE3 hardware occlusion queries apply to pawns, RE 8ec2c46 HIGH). Per draw owner, the
    // union of its character / weapon parts' world boxes (+0.5 m) is tested against the scene depth after the opaque
    // pass (GL_ANY_SAMPLES_PASSED, colour / depth writes off); the result of the query issued two frames earlier
    // decides whether the owner's main draws are skipped (its shadow and light environment still update). An owner is
    // skipped only when the queries of frames -2 AND -3 both found no sample (hysteresis; revealing is never delayed
    // beyond one result); a result not yet available, a camera inside the box, or an owner not drawn in that frame =
    // visible. Default on (with the prep skip below); WFC_NOPAWNOCCLUSION=1 = reference.
    struct PawnOcc {
        GLuint q[4] = {0, 0, 0, 0};
        int qFrame[4] = {-1, -1, -1, -1};  // frame each slot's query was issued (-1 none)
        bool occluded = false;             // decision for this frame
        core::Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        int boxFrame = -1, lastSeen = -1;
    };
    std::unordered_map<int, PawnOcc> pawnOcc_;
    GLuint occProg_ = 0, occVao_ = 0, occVbo_ = 0, occIbo_ = 0;
    GLint occUVP_ = -1, occUMin_ = -1, occUMax_ = -1;
    int statOccCulled_ = 0, statOccTested_ = 0, statOccPrepSkipped_ = 0;
    // Prep skip (default on; WFC_NOPAWNOCCPREP=1 off): the query box also covers the owner's composite-shadow volume, so a
    // hidden result means neither body nor shadow can reach a visible pixel; such an owner skips its skinned prep
    // (exact bounds, palette upload), shadow and draw. Its light environment still ticks (last exact bounds, current
    // model transform) so lighting is current when it reappears.
    bool skinPrepSkipped_ = false;
    // The held weapon is the pawn's shadow child (RE 5122915: HmWeaponMesh.Attach SetShadowParent(pawn mesh)): its last
    // draw (persistent draw list, transform, skinning state, world box) is recorded per owner and drawn into the owner's
    // composite-shadow depth with it; the subject bounds of the fit include it. WFC_NOWEAPONSHADOW=1 = body only.
    struct WeaponShadowRec { GpuMesh* g = nullptr; core::Mat4 model; int skinMode = 0, skinRow = 0, skinBones = 0;
                             float skinAlpha = 1.0f; core::Vec3 mn, mx; int frame = -1; };
    std::unordered_map<int, WeaponShadowRec> weaponShadow_;
    static bool pawnOccPrepOn();
    static bool pawnOcclusionOn();
    void pawnOcclusionResults();       // beginFrame: decisions from the queries of frame - 2
    void pawnOcclusionQueries();       // after the opaque pass: this frame's queries
    int testMesh_ = -1;           // WFC_TESTMESH render verification hook
    core::Mat4 testModel_;
    int bspMesh_ = -1;            // BSP rebuilt from the cooked vertex buffer with its lightmaps
    std::vector<float> bspTris_;  // level BSP triangles in UE units (x, y, z per vertex, 3 vertices per triangle) for traces
    int decalMesh_ = -1;          // static decals from their cooked receiver geometry
    struct RuntimeDecal { float born = 0, life = 0; MeshData mesh; };
    std::vector<RuntimeDecal> rtDecals_;   // M73, oldest first
    bool rtDecalsDirty_ = false;
    int rtDecalMesh_ = -1;                 // merged GPU mesh of rtDecals_ (rebuilt on change)
    void updateRuntimeDecals();
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
    std::set<std::string> dynChannelComponents_;
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
    // per-draw string work removed (profile: resolveBySourceName 8 %, dynamicProgram / materialKey 2.6 % of a 32 v 32
    // frame): source-name resolution per name, and the dynamic program per Material object (validated field by field)
    mutable std::unordered_map<std::string, std::string> srcNameCache_;
    struct DynProgMemo { std::string wfcName, sourceName, baseColorUri, emissiveUri, normalUri, specularUri;
                         core::Vec3 color; TextureHandle tex, emissiveTex; int prog; };
    std::unordered_map<const Material*, DynProgMemo> dynProgMemo_;
    int dynamicProgram(const Material* mat);
    int dynamicProgramUncached(const Material* mat);   // cached per material key (drawDynamic / prewarmDynamic)

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
public:
    void setActorHidden(const std::string& actor, bool hidden);   // Gameplay-authoritative visibility
    void setMapEffectActive(const std::string& ownerOrComponent, bool active);
    // key = owner actor, component path, or "<owner>|custom" / "<owner>|highlight"; hidden = SetHidden (no draw)
    void setMapEffectState(const std::string& key, bool active, bool hidden);
    struct EffKey { std::string w, role, shortName; };
    std::unordered_map<std::string, EffKey> effKeyCache_;          // setMapEffectState key parses
    std::unordered_map<std::string, std::string> actorKeyCache_;   // setActorHidden key -> lower-case short name
    void setActiveGameRules(const std::vector<std::string>& rules);   // Gameplay's active TnGameRules classes
    void setMapClock(float t) { mapClock_ = t; hasMapClock_ = true; }  // Gameplay MapState clock
    float mapTime() const { return hasMapClock_ ? mapClock_ : time_; }
    void setDestructibleState(const std::string& actor, int state);
    float worldRadius() const { return worldRadius_; }   // max distance of world geometry from the origin (m)
    // runtime particle effects from the template library (IRenderer::spawnParticleEffect; UE units / axes)
    int spawnFx(const std::string& tpl, const float R[3][3], const float T[3], const float* color, const float* target);
    // Matinee material parameters on a MaterialInstanceActor's MIC (material_instance_actors.json); held until changed
    bool setMaterialParam(const std::string& actor, const std::string& param, const float v[4]);
    bool setFxTransform(int id, const float R[3][3], const float T[3]);
    bool setFxTarget(int id, const float target[3]);   // segment end (beam target), UE units
    void stopFx(int id);
    void markFxPooled(int id);                 // EmitterPool effect (see IRenderer::setParticleEffectPooled)
    long fxPoolSeq_ = 0;
    int statPoolPeak_ = 0, statPoolOver_ = 0, statPoolReclaimed_ = 0, statPoolFrames_ = 0;
    bool setFxParam(int id, const std::string& name, const float v[4]);
    int liveFx() const;
    void drawMapPresentation();                                   // map FX + totems + destructible
    // map FX data (WfcMapFx.cpp)
    struct FxDist {
        int kind = 0, comps = 1;      // 0 constant, 1 uniform, 2 constant curve, 3 uniform curve
        std::vector<float> v;
        void eval(float t, uint32_t& rng, float out[3]) const;
    };
    struct FxModule {
        std::string name; std::map<std::string, FxDist> dists; int flagA = 1, flagB = 1;
        // M63 PMI_LocationEmitter / PMI_LocationEmitterDirect: the source emitter (by name, in this system instance)
        std::string sourceEmitter;
        int selection = 0;                // 0 Random, 1 Sequential, 2 particle 0
        bool inheritVelocity = false, inheritRotation = false;
        float inheritVelocityScale = 1.0f, inheritRotationScale = 1.0f;   // CDO 1 / 1
        bool killOnHit = false;           // PMI_Collision: MaxCollisions 0 + EPCC_Kill (AssetTools authored.db)
    };
    struct FxBurst { int count, countLow; float time; };
    struct FxLod {
        std::string material, meshGltf;
        bool overrideMaterial = false, localSpace = false, rectangle = false;
        int typeData = 0;                 // 0 sprite, 1 mesh, 2 Trail2, 3 Beam2
        int maxBeams = 0;                 // Beam2 MaxBeamCount (0 = no cap)
        int taperMethod = 0, interpPoints = 0;   // Beam2: PEBTM_None / Full / Partial; InterpolationPoints
        FxDist taperFactor, taperScale;           // Beam2: evaluated along the beam (0 source .. 1 target)
        int tessFactor = 1; float tessStrength = 1.0f;   // Trail2: Hermite steps per segment, tangent scale
        struct BeamNoise {                // ParticleModuleBeamNoise (M56; CDO defaults from Engine.xxx)
            bool on = false, applyScale = false, oscillate = false, targetNoise = false, nrEmitterTime = false,
                 smooth = false;
            int freq = 0, freqLow = 0, tessellation = 1;
            float lockRadius = 1.0f, frequencyDistance = 0.0f, lockTime = 0.0f;
            FxDist range, rangeScale, speed, tangent, scale;
        } noise;
        FxDist sourceStrength, targetStrength;   // Beam2 tangent strengths (UU; CDO 25): noise curve tangents
        struct BeamEnd {                  // M60 ParticleModuleBeamSource / Target (RE pass 5 s11, native resolvers)
            int method = 0;               // 0 Default, 1 UserSet, 2 Emitter, 3 Particle, 4 Actor
            int tangentMethod = 0;        // 0 Direct, 1 UserSet, 2 Distribution, 3 Emitter
            bool named = false, absolute = false, lock = false, lockTangent = false;
            FxDist position, tangent;
        } beamSrc, beamTgt;
        bool beamDistance = false;        // BeamMethod Distance: target = source + X * Distance
        float textureTile = 1.0f, textureTileDistance = 0.0f;   // M61 Trail2 TextureTile (CDO 1); the distance is exported but neither fill reads it (RE s13)
        bool tilePerParticle = false;     // Trail2 bTilePerParticle
        bool particleTrail = false;       // M63 Trail2 placed by LocationEmitter: one chain through its own particles
        int trailCap = 0;                 // Trail2: MaxTrailCount x MaxParticleInTrailCount (spawn cap; RE s14)
        int billboardAxis = -1;           // M65 BillboardSettings.Direction: -1 camera facing, 0..2 local X/Y/Z, 3..5 world
        float sideScale[2] = {1, 1};      // Alignment: V0 / V1 side scales (Centered 1/1, Positive 0/2, Negative 2/0)
        FxDist distance;
        struct BeamSine { float amp = 0, period = 1, speed = 0, phase = 0, dir[3] = {0, 0, 0}; };
        std::vector<BeamSine> sines;      // ParticleModuleBeamSineWave (WFC addition; render fill CONFIRMED, RE 9i)
        std::string sizeParam;            // SizeMultiplyLife by instance parameter (HoverFX "Size"); "" = none
        float sizeParamConst[3] = {1, 1, 1};
        bool velocityAligned = false;     // PSA_Velocity
        int subH = 1, subV = 1, subMethod = 0;   // SubUV: 0 none, 1 Linear, 2 Linear_Blend, 3 Random, 4 Random_Blend
        float randomImageTime = 0.0f;     // Random re-pick interval (lifetime fraction; 0 = every tick)
        bool hasDefaultColor = false; float defaultColor[4] = {1, 1, 1, 1};   // ColorByParameter DefaultColor (linear)
        float duration = 1.0f; int loops = 0;
        FxDist spawnRate;
        std::vector<FxBurst> bursts;
        std::vector<FxModule> modules;
    };
    struct FxEmitter {
        std::string name; int maxPeak = 1; bool renderable = true; std::vector<FxLod> lods;
        // M66 ParticleEmitter.LockAxisFlags: 0 none, 1 X, 2 Y, 3 Z, 4 -X, 5 -Y, 6 -Z, 7..9 ROTATE_X/Y/Z,
        // 10..12 ROTATE_X/Y/Z_U (WFC)
        int lockAxis = 0;
        // M68 SpriteEmitterRenderMode (WFC): 0 Quad, 1 Octagon, 2 BestFit (polygons in cell-local 0..1 texture space)
        int renderMode = 0;
        struct Polygon { float time = 0; int count = 0; std::vector<std::array<float, 2>> v; };
        std::vector<Polygon> polygons;
    };
    struct FxSystem { std::string name; std::vector<float> lodDistances; bool directSet = false; std::vector<FxEmitter> emitters; };
private:
    struct FxParticle {
        float pos[3] = {0, 0, 0}, vel[3] = {0, 0, 0}, baseVel[3] = {0, 0, 0};
        float size[3] = {0, 0, 0}, baseSize[3] = {0, 0, 0};
        float color[4] = {1, 1, 1, 1}, baseColor[4] = {1, 1, 1, 1};
        float rot = 0, rotRate = 0, relTime = 0, oneOverLife = 0;
        float meshRot[3] = {0, 0, 0}, meshRotRate[3] = {0, 0, 0};
        float accel[3] = {0, 0, 0};       // ParticleModuleAcceleration (world / emitter space as spawned)
        int subImage = 0, subImage2 = 0;  // SubUV cells (row-major index) and the blend interp (RE s16)
        float subInterp = 0.0f, subLastChange = 0.0f; bool subInit = false;
        bool subDirect = false; float subPos[2] = {0, 0}, subSize[2] = {1, 1};   // SubUVDirect (cell units)
        int noiseCount = 0;               // Beam2 noise points (count + 1 offsets, UE units, beam space)
        float noiseTimer = 0.0f;          // seconds since the noise points were last re-drawn
        uint32_t seq = 0;                 // spawn order within its emitter (Trail2 chains link by spawn order)
        bool beamInit = false;            // Beam2 ends resolved (UE world units; tangents x strength)
        float beamSrc[3] = {0, 0, 0}, beamTgt[3] = {0, 0, 0}, beamSrcT[3] = {0, 0, 0}, beamTgtT[3] = {0, 0, 0};
        std::vector<float> noiseCur, noiseNext;
    };
    struct FxEmitterRT {
        float time = 0, spawnFrac = 0; int loop = 0, lod = 0; bool done = false;
        std::vector<bool> burstFired;
        std::vector<FxParticle> parts;
        float dynParam[4] = {1, 1, 1, 1}; bool hasDyn = false;
        std::vector<std::array<float, 4>> trail;   // Trail2: recent source positions (UE) + age (s), newest last
        int forceSpawn = 0;                        // Trail2: particles owed by source movement (spawn per unit)
        int locSequence = 0;                       // LocationEmitter Sequential selection counter
        uint32_t spawnSeq = 0;                     // next particle spawn order
    };
    struct FxInstance {
        std::string component, owner, ownerClass, system, role, requiredRule;
        std::map<std::string, std::array<float, 4>> colorParams;   // InstanceParameters, FLinearColor(FColor)
        bool active = true, hidden = false, attached = true;
        float R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, T[3] = {0, 0, 0};   // UE rows / translation
        float R0[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, T0[3] = {0, 0, 0}; // authored (before matinee poses)
        std::string ownerShort;                                               // owner actor name (lower case)
        uint32_t rng = 1;
        std::vector<FxEmitterRT> emitters;
        int id = 0; bool transient = false;   // runtime spawned (spawnFx), released when finished / at unload
        bool hasTarget = false; float target[3] = {0, 0, 0};
        float idleTime = 0.0f;                // runtime instance: seconds since it stopped spawning (guard release)
        long poolSeq = 0;                     // > 0: an EmitterPool effect, in spawn order
    };
    int nextFxId_ = 1;
    size_t statLiveParticles_ = 0;    // live map-FX particles at the start of the tick (global budget guard)
    int progCacheHits_ = 0;
    int vlmRemapped_ = 0;             // vertex-lightmap sections bound through _WFC_SRCVERT (M62)
    bool prewarmDone_ = false, prewarmPending_ = false;
    float worldRadius_ = 0.0f;
    std::map<std::string, FxSystem> fxSystems_;
    std::vector<FxInstance> fxInstances_;
    std::map<std::string, int> fxMeshes_;
    float dynParam_[4] = {1, 1, 1, 1};
    double statFxMs_ = 0.0; int statFxSprites_ = 0, statFxMeshes_ = 0;
    float lastFxTime_ = -1.0f;
    core::Mat4 camView_;
    bool loadMapFx(const std::string& path);
    void tickMapFx(float dt);
    int fxMeshFor(const FxLod& L);
    struct MapProp {
        std::string actor, actorLower; int kind = 0;   // 0 energon totem, 1 destructible
        core::Mat4 model; int state = 0; int stateMesh[2] = {-1, -1};
    };
    std::vector<MapProp> mapProps_;
    struct ActorPose0 { float L[3]; float rot[3]; float scale = 1.0f; };   // scale: authored DrawScale
    std::map<std::string, ActorPose0> actorPose0_;     // authored pose (lower-case actor) for absolute poses
    int kothMesh_ = -1;
    // ammo-crate PickupFactoryMesh (TnAmmoCratePickup.MeshComponentA): PROP_NEU_AmmoPickup_STAT, CullDistance 8000,
    // PickupRotationRate yaw 10000 while available
    // PickupFactoryMesh (render_index pickup_factory_visuals): the template mesh attached to the factory actor at zero
    // offset; while the pickup is available the whole factory actor is PHYS_Rotating at the inventory's
    // PickupRotationRate, frozen at its current yaw when taken and resumed from it on respawn (RE M05 GAMEPLAY §6).
    struct PickupMeshRT {
        std::string owner, gltf, mesh; float R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, T[3] = {0, 0, 0};
        float off[3] = {0, 0, 0};                     // component Translation (flag / bomb rest mesh +150 Z)
        float yawRate = 0.0f, cullDistance = 0.0f; int meshId = -1;
    };
    std::map<std::string, float> pickupSpin_;         // factory (lower) -> accumulated yaw (UU) while available
    std::vector<PickupMeshRT> pickupMeshes_;
    std::set<std::string> pickupMeshHidden_;
    float pickupYaw(const std::string& ownerLower) const;
    bool pickupRuleBlocked(const std::string& ownerLower) const;
    float mapClock_ = 0.0f; bool hasMapClock_ = false;
    std::unique_ptr<assets::SkinnedModel> totemModel_;
    int totemClip_ = -1;
    std::vector<core::Mat4> totemScratch_;
    MeshData totemPose_;
    void loadMapProps(const std::string& indexPath);
    // authored movers (WfcMovers.cpp)
    struct MoverRT {
        std::string actor;
        int kind = 0;                 // 0 PHYS_Rotating, 1 Matinee InterpTrackMove, 2 absolute pose (frontend)
        float L1[3] = {0, 0, 0}, rot1[3] = {0, 0, 0};   // kind 2: target location / rotator (UE units)
        float scale = 1.0f;                              // kind 2: matinee DrawScale / authored DrawScale
        float L[3] = {0, 0, 0}, rot0[3] = {0, 0, 0}, rate[3] = {0, 0, 0};
        float length = 0.0f; bool looping = true;
        struct RotKey { float t; float q[4]; };
        struct PosKey { float t; float v[3], arrive[3], leave[3]; };
        std::vector<RotKey> rotKeys;
        std::vector<PosKey> posKeys;
    };
    std::vector<MoverRT> movers_;
    std::map<std::string, MoverRT> actorPoses_;        // frontend-driven absolute poses (kind 2)
    int posesApplied_ = 0;                             // validation: poses received for known / unknown actors
    std::set<std::string> posesUnknown_;
    std::unordered_map<std::string, core::Mat4> moverDelta_;
    std::unordered_map<std::string, bool> actorHidden_;
    std::set<std::string> authoredHiddenActors_;
    bool loadMovers(const std::string& path);
    core::Mat4 moverDelta(const MoverRT& m, float t) const;
    void updateMovers();
    bool actorHidden(const std::string& actorLower) const;
    std::set<std::string> activeRules_;
    std::map<std::string, std::vector<std::string>> ruleGatedActors_;   // actor -> rules that show it
    bool ruleActive(const std::string& rule) const;
    bool depthDirty_ = true;
    bool sceneColorCopied_ = false;
    void ensureSceneDepth();
    void ensureSceneColor();
    float fxColor_[4] = {1, 1, 1, 1};
    GLuint spriteVao_ = 0, spriteVbo_ = 0, spriteCbo_ = 0, spriteIbo_ = 0;
    GLuint spriteSubBo_ = 0;          // M67 sprite second SubUV cell + blend
    std::map<std::string, int> spriteProg_;
    std::string resolveName(const std::string& name) const;
    GLuint bloomGatherProg_ = 0, blurProg_ = 0, bloomFbo_[2] = {0, 0}, bloomTex_[2] = {0, 0};
    int bloomW_ = 1, bloomH_ = 1;
    int fbW_ = 0, fbH_ = 0;
};

} // namespace wfc
} // namespace render
