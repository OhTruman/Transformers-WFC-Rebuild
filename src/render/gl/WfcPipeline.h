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
#include <string>
#include <vector>
#include "render/gl/GLExt.h"
#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Renderer.h"

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
};

// Lighting inputs of a dynamic (or unbuilt static) primitive, UE3 light-environment style.
struct LightEnv {
    core::Vec3 cube[6];           // ambient cube irradiance: +X -X +Y -Y +Z -Z (glTF axes)
    int n = 0;
    float pos[3][4], dir[3][4], col[3][4], spot[3][4];
};

struct Program {
    GLuint id = 0;
    GLint uViewProj = -1, uModel = -1, uCamPos = -1, uTime = -1, uTwoSided = -1, uClip = -1, uMasked = -1,
          uLit = -1, uLMCoord = -1, uLMScale = -1, uAmb = -1, uNumLights = -1, uLPos = -1, uLDir = -1,
          uLCol = -1, uLSpot = -1, uFogOn = -1, uFogMaxH = -1, uFogScale = -1, uFogStart = -1, uFogExt = -1,
          uFogIn = -1;
    struct Slot { int unit; GLuint tex; bool cube; float umin[4]; float uscale[4]; };
    std::vector<Slot> slots;
    int blend = 0;                // 0 opaque, 1 masked, 2 translucent, 3 additive, 4 modulate
    bool twoSided = false, lit = true;
    float clip = 0.3333f;
};

class Pipeline {
public:
    bool load(const std::string& mapName);
    bool active() const { return active_; }
    void setVisibility(IRenderer::VisibilityQuery q) { vis_ = std::move(q); }

    void beginFrame(const Camera& cam, int w, int h);
    void endFrame();

    // Returns a GPU mesh id, or -1 (caller falls back to the legacy path).
    int upload(const MeshData& m);
    void draw(int id, const core::Mat4& model);
    void drawDynamic(const MeshData& m, const core::Mat4& model);

private:
    struct Sub {
        uint32_t first = 0, count = 0;
        int prog = -1;
        int lmTex[3] = {-1, -1, -1};
        float lmScale[3][3] = {};
        float lmCoord[4] = {1, 1, 0, 0};
        core::Vec3 bmin, bmax;
        bool envReady = false;
        LightEnv env;
    };
    struct GpuMesh {
        GLuint vao = 0, vbo = 0, ibo = 0;
        std::vector<Sub> subs;
        bool world = false;
        bool drawsBsp = false;    // replaced its level-BSP submeshes with the lit bspMesh_
    };

    GLuint texture(const std::string& file, bool srgb, bool clampU, bool clampV);
    GLuint cubeTexture(const std::vector<std::string>& faces, bool srgb);
    int programFor(const std::string& matName, const Material* gltfMat, bool lightmapped);
    int buildProgram(const std::string& key, const std::string& body, const std::vector<Program::Slot>& slots,
                     const std::vector<bool>& slotIsCube, int blend, bool twoSided, bool lit, float clip,
                     bool lightmapped);
    void computeEnv(const core::Vec3& p, bool dynamicObject, LightEnv& env) const;
    void bindCommon(const Program& P, const core::Mat4& model);
    void drawSubs(GpuMesh& g, const core::Mat4& model, bool dynamicObject);
    void ensureTargets(int w, int h);
    static void buildVertices(const MeshData& m, std::vector<float>& v);

    int bspMesh_ = -1;            // BSP rebuilt from the cooked vertex buffer with its lightmaps
    bool active_ = false;
    std::string dataDir_;
    IRenderer::VisibilityQuery vis_;

    // render data
    struct MatSrc { std::string glsl; std::vector<std::string> files; std::vector<std::vector<std::string>> faces; std::vector<bool> srgb, cube, clampU, clampV;
                    std::vector<std::vector<float>> umin, umax; int blend = 0; bool twoSided = false, lit = true;
                    float clip = 0.3333f; };
    std::map<std::string, MatSrc> mats_;
    struct LMRec { std::string coeff[3]; float scale[3][3]; float cs[2], cb[2]; };
    std::map<std::string, LMRec> lightmaps_;
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
    int frameNo_ = 0;

    // dynamic stream
    GLuint dynVao_ = 0, dynVbo_ = 0, dynIbo_ = 0;
    std::map<const Material*, int> dynProgCache_;

    // frame
    core::Mat4 viewProj_;
    core::Vec3 camPos_;
    float time_ = 0.0f;
    float frustum_[6][4] = {};
    int vpW_ = 0, vpH_ = 0;
    GLuint fbo_ = 0, colorTex_ = 0, depthRb_ = 0, postProg_ = 0, postVao_ = 0;
    GLuint bloomGatherProg_ = 0, blurProg_ = 0, bloomFbo_[2] = {0, 0}, bloomTex_[2] = {0, 0};
    int bloomW_ = 1, bloomH_ = 1;
    int fbW_ = 0, fbH_ = 0;
};

} // namespace wfc
} // namespace render
