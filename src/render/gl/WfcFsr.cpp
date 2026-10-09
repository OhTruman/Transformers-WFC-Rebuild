// Clean-room reconstruction — optional spatial upscaling: AMD FidelityFX Super Resolution 1 (EASU upscale + RCAS
// sharpening; third_party/ffx_fsr1, MIT, unmodified). PC ADAPTATION, OFF by default: the original presentation
// renders at the window size with no upscaler, and stays the reference.
//
// With a render scale below 1 the 3D frame (world, characters, FX, post) renders at scale x window; the post pass
// writes its display-referred result (after tonemap / CLUT / gamma: FSR's expected perceptual input) to an RGBA8
// target, EASU upscales it to the window size and RCAS sharpens it into the window. HUD effects, Canvas tiles and
// GFx draw afterwards at the window resolution, as before. Scale 1 with sharpening only runs RCAS (no EASU).
#include "render/gl/WfcPipeline.h"
#include "core/Log.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

// CPU side of the FFX headers: the constant setup (FsrEasuCon / FsrRcasCon)
#define A_CPU 1
#include "../../../third_party/ffx_fsr1/ffx_a.h"
#include "../../../third_party/ffx_fsr1/ffx_fsr1.h"

namespace render {
namespace wfc {
using namespace glx;

namespace {
#include "render/gl/FfxFsr1Source.inc"

std::string joined(const char* const* parts, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s += parts[i];
    return s;
}

const char* kFsrVS = R"(#version 430
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";

std::string easuFS() {
    return std::string("#version 430\n#define A_GPU 1\n#define A_GLSL 1\n") +
           joined(kFfxA, sizeof(kFfxA) / sizeof(kFfxA[0])) +
           "\n#define FSR_EASU_F 1\nuniform sampler2D uIn;\nuniform uvec4 uCon0, uCon1, uCon2, uCon3;\n"
           "AF4 FsrEasuRF(AF2 p) { return textureGather(uIn, p, 0); }\n"
           "AF4 FsrEasuGF(AF2 p) { return textureGather(uIn, p, 1); }\n"
           "AF4 FsrEasuBF(AF2 p) { return textureGather(uIn, p, 2); }\n" +
           joined(kFfxFsr1, sizeof(kFfxFsr1) / sizeof(kFfxFsr1[0])) +
           "\nout vec4 oColor;\n"
           "void main() { AF3 c; FsrEasuF(c, AU2(gl_FragCoord.xy), uCon0, uCon1, uCon2, uCon3); oColor = vec4(c, 1.0); }\n";
}

std::string rcasFS() {
    return std::string("#version 430\n#define A_GPU 1\n#define A_GLSL 1\n") +
           joined(kFfxA, sizeof(kFfxA) / sizeof(kFfxA[0])) +
           "\n#define FSR_RCAS_F 1\nuniform sampler2D uIn;\nuniform uvec4 uCon;\n"
           "AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(uIn, p, 0); }\n"
           "void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}\n" +
           joined(kFfxFsr1, sizeof(kFfxFsr1) / sizeof(kFfxFsr1[0])) +
           "\nout vec4 oColor;\n"
           "void main() { AF1 r, g, b; FsrRcasF(r, g, b, AU2(gl_FragCoord.xy), uCon); oColor = vec4(r, g, b, 1.0); }\n";
}
}  // namespace

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);   // WfcPipeline.cpp
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);

void Pipeline::setUpscaling(float scale, float sharpness) {
    fsrScale_ = scale <= 0.0f ? 1.0f : std::min(std::max(scale, 0.25f), 1.0f);
    fsrSharpness_ = sharpness;
}

bool Pipeline::fsrActive() const { return fsrScale_ < 0.999f || fsrSharpness_ >= 0.0f; }

bool Pipeline::ensureFsr(int inW, int inH, int outW, int outH) {
    if (!fsrEasuProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kFsrVS, "fsr.vs");
        GLuint fe = vs ? compileShader(GL_FRAGMENT_SHADER, easuFS(), "fsr_easu.fs") : 0;
        GLuint fr = vs ? compileShader(GL_FRAGMENT_SHADER, rcasFS(), "fsr_rcas.fs") : 0;
        fsrEasuProg_ = fe ? linkProgram(vs, fe, "fsr_easu") : 0;
        fsrRcasProg_ = fr ? linkProgram(vs, fr, "fsr_rcas") : 0;
        if (!fsrEasuProg_ || !fsrRcasProg_) {
            LOG_WARN("wfc: FSR 1 shaders unavailable: upscaling off (the frame is scaled as without it)");
            fsrFailed_ = true;
            return false;
        }
        LOG_INFO("UPSCALER: FSR 1 (EASU + RCAS) active: render scale %.2f, sharpness %.2f stops", fsrScale_,
                 fsrSharpness_ < 0.0f ? 0.2f : fsrSharpness_);
    }
    auto target = [](GLuint& fbo, GLuint& tex, int w, int h, int& cw, int& ch) {
        if (fbo && cw == w && ch == h) return;
        if (!fbo) { GenFramebuffers(1, &fbo); glGenTextures(1, &tex); }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // EASU gathers with bilinear setup
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        BindFramebuffer(GL_FRAMEBUFFER, fbo);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        cw = w; ch = h;
    };
    target(fsrInFbo_, fsrInTex_, inW, inH, fsrInW_, fsrInH_);
    if (inW != outW || inH != outH) target(fsrMidFbo_, fsrMidTex_, outW, outH, fsrMidW_, fsrMidH_);
    return true;
}

// The post pass has drawn into fsrInFbo_ (inW x inH); upscale + sharpen into the window framebuffer.
void Pipeline::runFsr(int inW, int inH, int outW, int outH) {
    const bool upscale = inW != outW || inH != outH;
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    BindVertexArray(postVao_);
    GLuint rcasIn = fsrInTex_;
    if (upscale) {
        AU1 c0[4], c1[4], c2[4], c3[4];
        FsrEasuCon(c0, c1, c2, c3, (AF1)inW, (AF1)inH, (AF1)inW, (AF1)inH, (AF1)outW, (AF1)outH);
        BindFramebuffer(GL_FRAMEBUFFER, fsrMidFbo_);
        glViewport(0, 0, outW, outH);
        UseProgram(fsrEasuProg_);
        ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fsrInTex_);
        Uniform1i(GetUniformLocation(fsrEasuProg_, "uIn"), 0);
        Uniform4uiv(GetUniformLocation(fsrEasuProg_, "uCon0"), 1, c0);
        Uniform4uiv(GetUniformLocation(fsrEasuProg_, "uCon1"), 1, c1);
        Uniform4uiv(GetUniformLocation(fsrEasuProg_, "uCon2"), 1, c2);
        Uniform4uiv(GetUniformLocation(fsrEasuProg_, "uCon3"), 1, c3);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        rcasIn = fsrMidTex_;
    }
    AU1 rc[4];
    FsrRcasCon(rc, fsrSharpness_ < 0.0f ? 0.2f : fsrSharpness_);   // stops: 0 = maximum sharpening
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outW, outH);
    UseProgram(fsrRcasProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rcasIn);
    Uniform1i(GetUniformLocation(fsrRcasProg_, "uIn"), 0);
    Uniform4uiv(GetUniformLocation(fsrRcasProg_, "uCon"), 1, rc);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace wfc
}  // namespace render
