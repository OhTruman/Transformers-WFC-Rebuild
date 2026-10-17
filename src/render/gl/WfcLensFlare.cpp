// Lens flares (RE LENS_FLARE_RE.md s1-s4a; data <map>/lens_flares.json from tools/render/build_lens_flares.py): the map's
// LensFlareSources (one sun on six MP maps) drawn as the original's FLensFlareSceneProxy does -
//  - per enabled reflection element a camera-facing quad: S = the source's NDC, E = S (1 - 2 RayDistance), unprojected (the
//    quad's view depth k cancels: size x k -> an NDC half-extent of 0.5 Size Scaling OcclScaling AxisScaling P00 / P11) [C];
//  - r = max(|Ex|, |Ey|) with bNormalizeRadialDistance (else |E|; bUseSourceDistance: 0.5 |S - E|) [C];
//  - colour = Color(r) x DistMap_Color(D), alpha = Alpha(r) x DistMap_Alpha(D) (x SourceColor with bModulateColorBySource),
//    rotation = Rotation(r), material = LFMaterials[LFMaterialIndex(r)]; D = camera -> source distance in UU [C];
//  - the material's LensFlare* nodes read (r, 0.5 |S - E|, RayDistance, Occlusion) and the intensity [C];
//  - Occlusion = ScreenPercentageMap(coverage), coverage = the previous frame's visible samples of the FixedRelativeBoundingBox
//    over its projected pixel area (stock UE3 [HIGH]); intensity = 1 (proxy+0xE4: no MP flare authors a cone / radius, RE C);
//  - additive, after the translucency pass, no depth test (SDPG_Foreground) [C]; the VS applies the height fog as any draw.
#include "render/gl/WfcPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "assets/Json.h"
#include "core/Log.h"
#include "render/gl/GLExt.h"

namespace render {
namespace wfc {

using namespace glx;
GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);
namespace {
std::string lfReadText(const std::string& p) { std::ifstream in(p, std::ios::binary); std::stringstream s; s << in.rdbuf(); return s.str(); }
}

// a cooked distribution: constant values, or the lookup table [MinOut, MaxOut, InBias, InScale, samples x n] (RE / pstream)
void Pipeline::lfEval(const LfDist& d, float x, float* out) {
    const int n = std::max(d.n, 1);
    if (!d.c.empty()) { for (int k = 0; k < n; ++k) out[k] = d.c[(size_t)std::min<int>(k, (int)d.c.size() - 1)]; return; }
    if (d.t.size() < 4 + (size_t)n) { for (int k = 0; k < n; ++k) out[k] = 1.0f; return; }
    const size_t cnt = (d.t.size() - 4) / (size_t)n;
    const float bias = d.t[2], scale = d.t[3];
    float f = scale != 0.0f ? (x - bias) * scale : 0.0f;
    f = std::min(std::max(f, 0.0f), (float)(cnt - 1));
    const size_t i0 = (size_t)std::floor(f), i1 = std::min(i0 + 1, cnt - 1);
    const float a = f - (float)i0;
    for (int k = 0; k < n; ++k) out[k] = d.t[4 + i0 * (size_t)n + (size_t)k] * (1.0f - a) + d.t[4 + i1 * (size_t)n + (size_t)k] * a;
}

void Pipeline::loadLensFlares() {
    for (LensFlareRT& f : lensFlares_) if (f.query) DeleteQueries(1, &f.query);
    lensFlares_.clear();
    const std::string txt = lfReadText(dataDir_ + "/lens_flares.json");
    assets::Json J;
    if (txt.empty() || !assets::Json::parse(txt, J)) return;
    auto dist = [](const assets::Json& j, int n) {
        LfDist d; d.n = n;
        for (size_t i = 0; i < j["const"].size(); ++i) d.c.push_back(j["const"][i].asFloat(1.0f));
        for (size_t i = 0; i < j["table"].size(); ++i) d.t.push_back(j["table"][i].asFloat(0.0f));
        if (d.c.empty() && d.t.empty()) d.c.assign((size_t)n, 1.0f);
        return d;
    };
    for (size_t i = 0; i < J["flares"].size(); ++i) {
        const assets::Json& F = J["flares"][i];
        LensFlareRT f;
        f.pos = {F["position_m"][0].asFloat(), F["position_m"][1].asFloat(), F["position_m"][2].asFloat()};
        for (int k = 0; k < 4; ++k) f.srcColor[k] = F["source_color"][(size_t)k].asFloat(1.0f);
        const assets::Json& B = F["fixed_box"];   // [min xyz, max xyz, valid] relative to the source (UU)
        float h = 0.0f;
        for (int k = 0; k < 3; ++k) h = std::max(h, std::max(std::fabs(B[(size_t)k].asFloat(0.0f)), std::fabs(B[(size_t)k + 3].asFloat(0.0f))));
        f.boxHalf = (h > 0.0f ? h : 1000.0f) * 0.01f;
        f.spm = dist(F["screen_percentage_map"], 1);
        for (size_t e = 0; e < F["elements"].size(); ++e) {
            const assets::Json& E = F["elements"][e];
            LfElement el;
            el.name = E["name"].asString();
            el.ray = E["ray_distance"].asFloat(0.0f);
            el.size[0] = E["size"][0].asFloat(1.0f); el.size[1] = E["size"][1].asFloat(1.0f);
            el.normRadial = E["normalize_radial"].asBool(true);
            el.modSource = E["modulate_by_source"].asBool(false);
            el.srcDist = E["use_source_distance"].asBool(false);
            for (size_t m = 0; m < E["materials"].size(); ++m) el.mats.push_back(E["materials"][m].asString());
            el.scaling = dist(E["Scaling"], 1); el.occl = dist(E["OcclusionScaling"], 1); el.axis = dist(E["AxisScaling"], 3);
            el.rot = dist(E["Rotation"], 1); el.color = dist(E["Color"], 3); el.alpha = dist(E["Alpha"], 1);
            el.dmScale = dist(E["DistMap_Scale"], 3); el.dmColor = dist(E["DistMap_Color"], 3); el.dmAlpha = dist(E["DistMap_Alpha"], 1);
            el.matIdx = dist(E["LFMaterialIndex"], 1);
            if (el.rot.c.size() == 1 && el.rot.t.empty() && E["Rotation"]["const"].size() == 0) el.rot.c = {0.0f};
            if (el.matIdx.t.empty() && E["LFMaterialIndex"]["const"].size() == 0) el.matIdx.c = {0.0f};
            if (!el.mats.empty()) f.els.push_back(std::move(el));
        }
        if (!f.els.empty()) lensFlares_.push_back(std::move(f));
    }
    if (!lensFlares_.empty()) LOG_INFO("wfc: lens flares: %zu source(s)", lensFlares_.size());
}

void Pipeline::drawLensFlares() {
    static const bool off = std::getenv("WFC_NOLENSFLARES") != nullptr;   // A/B
    if (off || lensFlares_.empty()) return;
    const float* V = camView_.m;
    const core::Vec3 right{V[0], V[4], V[8]}, up{V[1], V[5], V[9]}, back{V[2], V[6], V[10]};
    const float p00 = camProj_.m[0], p11 = camProj_.m[5];
    const float* M = viewProj_.m;
    auto clip = [&](const core::Vec3& p, float* c) {
        for (int r = 0; r < 4; ++r) c[r] = M[r] * p.x + M[4 + r] * p.y + M[8 + r] * p.z + M[12 + r];
    };
    // occlusion box program (depth test only, no colour / depth writes)
    if (!lfBoxProg_) {
        const char* vs = "#version 330 core\nlayout(location=0) in vec3 aPos; uniform mat4 uVP;\nvoid main(){ gl_Position = uVP * vec4(aPos, 1.0); }\n";
        const char* fs = "#version 330 core\nout vec4 oColor;\nvoid main(){ oColor = vec4(0.0); }\n";
        GLuint v = compileShader(GL_VERTEX_SHADER, vs, "lensflare_box.vs"), f = compileShader(GL_FRAGMENT_SHADER, fs, "lensflare_box.fs");
        if (v && f) lfBoxProg_ = linkProgram(v, f, "lensflare_box");
        GenVertexArrays(1, &lfBoxVao_); GenBuffers(1, &lfBoxVbo_);
    }
    for (LensFlareRT& f : lensFlares_) {
        // the previous frame's query: visible samples / the box's projected pixel area -> coverage
        if (f.query && f.pending) {
            GLint avail = 0;
            GetQueryObjectiv(f.query, 0x8867 /*GL_QUERY_RESULT_AVAILABLE*/, &avail);
            if (avail) {
                GLint samples = 0;
                GetQueryObjectiv(f.query, 0x8866 /*GL_QUERY_RESULT*/, &samples);
                f.coverage = f.area > 0.0f ? std::min(1.0f, (float)samples / f.area) : 0.0f;
                f.pending = false;
            }
        }
        // this frame's query (issued now, read next frame)
        if (lfBoxProg_ && !f.pending) {
            if (!f.query) GenQueries(1, &f.query);
            const float h = f.boxHalf;
            core::Vec3 c8[8];
            for (int k = 0; k < 8; ++k) c8[k] = f.pos + core::Vec3{(k & 1) ? h : -h, (k & 2) ? h : -h, (k & 4) ? h : -h};
            float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f; bool behind = false;
            for (int k = 0; k < 8; ++k) {
                float c[4]; clip(c8[k], c);
                if (c[3] <= 1e-3f) { behind = true; break; }
                x0 = std::min(x0, c[0] / c[3]); x1 = std::max(x1, c[0] / c[3]); y0 = std::min(y0, c[1] / c[3]); y1 = std::max(y1, c[1] / c[3]);
            }
            if (behind) { x0 = -1; y0 = -1; x1 = 1; y1 = 1; }
            x0 = std::max(x0, -1.0f); y0 = std::max(y0, -1.0f); x1 = std::min(x1, 1.0f); y1 = std::min(y1, 1.0f);
            f.area = x1 > x0 && y1 > y0 ? (x1 - x0) * 0.5f * (float)vpW_ * (y1 - y0) * 0.5f * (float)vpH_ : 0.0f;
            static const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
            float tri[6 * 6 * 3];
            int n = 0;
            for (const auto& fc : faces)
                for (int t : {0, 1, 2, 0, 2, 3}) { const core::Vec3& p = c8[fc[t]]; tri[n++] = p.x; tri[n++] = p.y; tri[n++] = p.z; }
            BindVertexArray(lfBoxVao_);
            BindBuffer(GL_ARRAY_BUFFER, lfBoxVbo_);
            BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof tri, tri, GL_STREAM_DRAW);
            EnableVertexAttribArray(0); VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
            UseProgram(lfBoxProg_);
            UniformMatrix4fv(GetUniformLocation(lfBoxProg_, "uVP"), 1, GL_FALSE, viewProj_.m);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_FALSE);
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(behind ? GL_FRONT : GL_BACK);
            glDisable(GL_BLEND);
            BeginQuery(0x8914 /*GL_SAMPLES_PASSED*/, f.query);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            EndQuery(0x8914);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDisable(GL_CULL_FACE); glCullFace(GL_BACK);
            BindVertexArray(0);
            f.pending = true;
        }
        float s4[4]; clip(f.pos, s4);
        if (s4[3] <= 1e-3f) continue;                    // source behind the camera
        const float sx = s4[0] / s4[3], sy = s4[1] / s4[3];
        float occ[1];
        lfEval(f.spm, f.coverage, occ);
        const float vis = occ[0];
        const float D = core::length(f.pos - camPos_) * 100.0f;   // (UU: the DistMap_* input)
        const float kDepth = 1.0f;                       // the element plane's view depth (cancels in the NDC size)
        flushingTrans_ = true;                           // (drawn now, not queued)
        glDisable(GL_DEPTH_TEST);
        for (const LfElement& el : f.els) {
            const float ex = sx * (1.0f - 2.0f * el.ray), ey = sy * (1.0f - 2.0f * el.ray);
            float r = el.normRadial ? std::max(std::fabs(ex), std::fabs(ey)) : std::sqrt(ex * ex + ey * ey);
            const float srcD = 0.5f * std::sqrt((sx - ex) * (sx - ex) + (sy - ey) * (sy - ey));
            if (el.srcDist) r = srcD;
            float sc[1], oc[1], ax[3], rot[1], col[3], al[1], dms[3], dmc[3], dma[1], mi[1];
            lfEval(el.scaling, r, sc); lfEval(el.occl, vis, oc); lfEval(el.axis, r, ax); lfEval(el.rot, r, rot);
            lfEval(el.color, r, col); lfEval(el.alpha, r, al); lfEval(el.dmScale, D, dms); lfEval(el.dmColor, D, dmc);
            lfEval(el.dmAlpha, D, dma); lfEval(el.matIdx, r, mi);
            const size_t m = (size_t)std::min<int>(std::max<int>((int)std::floor(mi[0] + 0.5f), 0), (int)el.mats.size() - 1);
            // the element centre on the view plane at depth kDepth, its half-extents (world, along camera right / up)
            const core::Vec3 P = camPos_ - back * kDepth + right * (ex * kDepth / p00) + up * (ey * kDepth / p11);
            const float hx = 0.5f * el.size[0] * sc[0] * oc[0] * kDepth * ax[0] * dms[0];
            const float hy = 0.5f * el.size[1] * sc[0] * oc[0] * kDepth * ax[1] * dms[1];
            if (!(hx > 0.0f) && !(hy > 0.0f)) continue;
            const float cr = std::cos(rot[0]), sr = std::sin(rot[0]);
            const core::Vec3 R = right * cr + up * sr, U = up * cr - right * sr;
            Sprite q;
            const float cu[4] = {-1, 1, 1, -1}, cv[4] = {-1, -1, 1, 1};
            for (int k = 0; k < 4; ++k) {
                q.c[k] = P + R * (cu[k] * hx) + U * (cv[k] * hy);
                q.uv[k][0] = cu[k] * 0.5f + 0.5f; q.uv[k][1] = 0.5f - cv[k] * 0.5f;
            }
            for (int k = 0; k < 3; ++k) q.color[k] = col[k] * dmc[k] * (el.modSource ? f.srcColor[k] : 1.0f);
            q.color[3] = al[0] * dma[0] * (el.modSource ? f.srcColor[3] : 1.0f);
            lensFlareU_[0] = r; lensFlareU_[1] = srcD; lensFlareU_[2] = el.ray; lensFlareU_[3] = vis;
            lensFlareI_ = 1.0f;                          // (proxy+0xE4: 1 without a cone / radius - RE C, 0x82FB6C40)
            drawSprites(el.mats[m].c_str(), &q, 1, back);
        }
        glEnable(GL_DEPTH_TEST);
        flushingTrans_ = false;
        lensFlareU_[0] = 0; lensFlareU_[1] = 0; lensFlareU_[2] = 0; lensFlareU_[3] = 1; lensFlareI_ = 1.0f;
    }
}

}  // namespace wfc
}  // namespace render
