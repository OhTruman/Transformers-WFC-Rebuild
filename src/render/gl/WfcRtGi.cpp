// Clean-room reconstruction — R4 (PC EXTENSION, RT Realistic only): the runtime ray-traced lightmap. The whole map lit
// by ray tracing in the lightmaps' own texel space: at load every lightmapped static triangle is rasterised into its
// lightmap page (texel centres) - per texel its world position and tangent frame - and the D3D12 texel tracer
// (rt_gi.hlsl) writes per texel the same three directional coefficients the bake stored (HL2 basis, decode
// tex.rgb x scale), so every lightmapped shader path applies unchanged: sun (soft, R2's cone) + sky + the map's
// static lights (Systems' fit_lights.json calibration) + bounce (the previous result read at the hit). Vertex-lit
// (LMT_1D) meshes: one texel per vertex. Realistic's default; WFC_RT_GI=0: the bake.
#include "render/gl/WfcPipeline.h"
#include "render/d3d12/D3D12RtBridge.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <chrono>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <unordered_set>

namespace render {
namespace wfc {
using namespace glx;

namespace {
// octahedral unit vector -> 2 x 16 bits (x low, y high)
uint32_t octEncode(float x, float y, float z) {
    const float l = std::fabs(x) + std::fabs(y) + std::fabs(z);
    if (l < 1e-20f) return 0x80008000u;
    x /= l; y /= l; z /= l;
    if (z < 0.0f) {
        const float ox = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f), oy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = ox; y = oy;
    }
    auto q = [](float a) { return (uint32_t)std::lround((std::min(std::max(a, -1.0f), 1.0f) * 0.5f + 0.5f) * 65535.0f); };
    return q(x) | (q(y) << 16);
}
void octDecode(uint32_t e, float n[3]) {
    float x = (float)(e & 0xFFFFu) / 65535.0f * 2.0f - 1.0f, y = (float)(e >> 16) / 65535.0f * 2.0f - 1.0f;
    float z = 1.0f - std::fabs(x) - std::fabs(y);
    if (z < 0.0f) { const float ox = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f), oy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f); x = ox; y = oy; }
    const float l = std::sqrt(x * x + y * y + z * z);
    n[0] = x / l; n[1] = y / l; n[2] = z / l;
}
}  // namespace

// (Realistic's default since 14 Oct - RT / bake 0.93-1.07 on the five user maps; WFC_RT_GI=0: the bake)
bool Pipeline::giOn() const {
    static const bool off = [] { const char* e = std::getenv("WFC_RT_GI"); return e && e[0] == '0'; }();
    return !off && rtInfra_ && !rtFailed_;
}

// one static mesh's lightmapped subs into their pages (called from rtBuildWorld with the mesh's read-back buffers:
// 14 floats per vertex - position, normal, tangent + sign, UV0, UV1)
void Pipeline::giRasterMesh(int meshId, const std::vector<uint32_t>& idx, const std::vector<float>& v) {
    if (!giOn() || meshId < 0 || rtMode_ != 2) return;   // (Realistic at load; a live switch to it keeps the bake until a reload)
    const GpuMesh& g = meshes_[(size_t)meshId];
    const bool bsp = meshId == bspMesh_;
    if (giPageSize_.size() < lmTextures_.size()) giPageSize_.resize(lmTextures_.size(), {0, 0});
    auto pageSize = [&](int k) -> std::pair<int, int> {
        if (k < 0 || (size_t)k >= lmTextures_.size() || !lmTextures_[(size_t)k]) return {0, 0};
        auto& s = giPageSize_[(size_t)k];
        if (s.first == 0) {
            GLint w = 0, h = 0;
            glBindTexture(GL_TEXTURE_2D, lmTextures_[(size_t)k]);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            glBindTexture(GL_TEXTURE_2D, 0);
            s = {w, h};
        }
        return s;
    };
    const size_t nVert = v.size() / 14;
    for (size_t si = 0; si < g.subs.size(); ++si) {
        const Sub& s = g.subs[si];
        if (s.prog < 0 || s.count < 3 || (size_t)s.first + s.count > idx.size() || s.noLights) continue;
        if (s.vlmTex) {                                  // vertex-lit: one texel per vertex (the MDI sample layout)
            if (s.vlmMdiOff < 0 || meshId != giVlmMesh_ || s.vlmMdiCnt <= 0) { ++giVertexLitSubs_; continue; }
            const Program& PV = progs_[(size_t)s.prog];
            if (!PV.lit || PV.blend > 1) continue;
            GiSub vs;
            for (int k = 0; k < 3; ++k) for (int c = 0; c < 3; ++c) vs.scale[k][c] = s.lmScale[k][c];
            vs.bsp = bsp ? 1 : 0; vs.vcnt = s.vlmMdiCnt;
            const uint32_t vsub = (uint32_t)giSubs_.size();
            giSubs_.push_back(vs);
            for (int vi = 0; vi < s.vlmMdiCnt && vi < (1 << 23); ++vi) {
                const size_t vx = (size_t)s.vlmBase + (size_t)vi;
                if (vx >= nVert) break;
                const float* q = &v[vx * 14];
                GiTexel T;
                T.pageXY = (uint32_t)kGiVertexPage | ((uint32_t)vi << 9);
                for (int c = 0; c < 3; ++c) T.pos[c] = q[c];
                T.nrm = octEncode(q[3], q[4], q[5]);
                T.tan = octEncode(q[6], q[7], q[8]);
                T.sub = vsub | (q[9] < 0.0f ? 0x80000000u : 0u);
                T.pad = (uint32_t)s.vlmMdiOff;
                giTexels_.push_back(T);
                ++giVertexTexels_;
            }
            continue;
        }
        if (s.lmTex[0] < 0) continue;
        const Program& P = progs_[(size_t)s.prog];
        if (!P.lit || P.blend > 1) continue;
        const auto [pw, ph] = pageSize(s.lmTex[0]);
        if (pw <= 0 || ph <= 0 || (size_t)pw * (size_t)ph > (1u << 24)) continue;
        if (pw > 2048 || ph > 2048 || s.lmTex[0] >= 512) continue;     // (the texel's page / x / y packing)
        // the page's coverage map (first use)
        while (giCoveredBase_.size() <= (size_t)s.lmTex[0]) giCoveredBase_.push_back((size_t)-1);
        if (giCoveredBase_[(size_t)s.lmTex[0]] == (size_t)-1) {
            giCoveredBase_[(size_t)s.lmTex[0]] = giCovered_.size();
            giCovered_.resize(giCovered_.size() + (size_t)pw * (size_t)ph, 0);
        }
        uint32_t* cov = &giCovered_[giCoveredBase_[(size_t)s.lmTex[0]]];
        GiSub gs;
        for (int k = 0; k < 3; ++k) {
            gs.lm[k] = s.lmTex[k];
            for (int c = 0; c < 3; ++c) gs.scale[k][c] = s.lmScale[k][c];
        }
        gs.bsp = bsp ? 1 : 0;
        const uint32_t subId = (uint32_t)giSubs_.size();
        giSubs_.push_back(gs);
        const float su = s.lmCoord[0] * (float)pw, sv = s.lmCoord[1] * (float)ph, bu = s.lmCoord[2] * (float)pw, bv = s.lmCoord[3] * (float)ph;
        for (uint32_t t = 0; t + 2 < s.count; t += 3) {
            uint32_t vi[3];
            for (int k = 0; k < 3; ++k) vi[k] = idx[s.first + t + (uint32_t)k];
            if (vi[0] >= nVert || vi[1] >= nVert || vi[2] >= nVert) continue;
            const float* q[3] = {&v[(size_t)vi[0] * 14], &v[(size_t)vi[1] * 14], &v[(size_t)vi[2] * 14]};
            float x[3], y[3];                            // texel space (texel centres at integer + 0.5)
            for (int k = 0; k < 3; ++k) { x[k] = q[k][12] * su + bu; y[k] = q[k][13] * sv + bv; }
            const float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
            if (std::fabs(area) < 1e-12f) continue;
            const int x0 = std::max(0, (int)std::floor(std::min(x[0], std::min(x[1], x[2])))),
                      x1 = std::min(pw - 1, (int)std::ceil(std::max(x[0], std::max(x[1], x[2])))),
                      y0 = std::max(0, (int)std::floor(std::min(y[0], std::min(y[1], y[2])))),
                      y1 = std::min(ph - 1, (int)std::ceil(std::max(y[0], std::max(y[1], y[2]))));
            for (int ty = y0; ty <= y1; ++ty)
                for (int tx = x0; tx <= x1; ++tx) {
                    const float px = (float)tx + 0.5f, py = (float)ty + 0.5f;
                    float b[3];
                    b[0] = ((x[1] - px) * (y[2] - py) - (x[2] - px) * (y[1] - py)) / area;
                    b[1] = ((x[2] - px) * (y[0] - py) - (x[0] - px) * (y[2] - py)) / area;
                    b[2] = 1.0f - b[0] - b[1];
                    if (b[0] < -1e-4f || b[1] < -1e-4f || b[2] < -1e-4f) continue;
                    uint32_t& c = cov[(size_t)ty * (size_t)pw + (size_t)tx];
                    if (c) { ++giOverlap_; continue; }    // (charts do not overlap; shared edges: the first triangle)
                    c = subId + 1;
                    GiTexel T;
                    T.pageXY = (uint32_t)s.lmTex[0] | ((uint32_t)tx << 9) | ((uint32_t)ty << 20);
                    float n[3] = {0, 0, 0}, tg[3] = {0, 0, 0}, w = 0;
                    for (int k = 0; k < 3; ++k) {
                        for (int c3 = 0; c3 < 3; ++c3) {
                            T.pos[c3] += b[k] * q[k][c3];
                            n[c3] += b[k] * q[k][3 + c3];
                            tg[c3] += b[k] * q[k][6 + c3];
                        }
                        w += b[k] * q[k][9];
                    }
                    T.nrm = octEncode(n[0], n[1], n[2]);
                    T.tan = octEncode(tg[0], tg[1], tg[2]);
                    T.sub = subId | (w < 0.0f ? 0x80000000u : 0u);   // (bit 31: the tangent frame's sign)
                    giTexels_.push_back(T);
                }
        }
    }
}

// after the last static mesh: the gutter (uncovered texels next to a chart, which bilinear lookups and mips read)
// gets a copy list (dst <- its nearest covered neighbour, two rings; with the neighbour's sub for its other two
// coefficient pages), then the coverage maps are dropped
void Pipeline::giFinishTexels() {
    giFinished_ = true;
    size_t pages = 0;
    giDilate_.clear();
    for (size_t k = 0; k < giCoveredBase_.size(); ++k) {
        if (giCoveredBase_[k] == (size_t)-1) continue;
        ++pages;
        const int pw = giPageSize_[k].first, ph = giPageSize_[k].second;
        std::vector<uint32_t> cov(giCovered_.begin() + (std::ptrdiff_t)giCoveredBase_[k],
                                 giCovered_.begin() + (std::ptrdiff_t)(giCoveredBase_[k] + (size_t)pw * (size_t)ph));
        auto pack = [&](int x, int y) { return (uint32_t)k | ((uint32_t)x << 9) | ((uint32_t)y << 20); };
        for (int ring = 0; ring < 2; ++ring) {           // (ring 2 copies from ring 1: a 2-texel gutter)
            struct Added { int x, y; uint32_t sub; };
            std::vector<Added> added;
            for (int y = 0; y < ph; ++y)
                for (int x = 0; x < pw; ++x) {
                    if (cov[(size_t)y * pw + x]) continue;
                    int best = -1, bx = 0, by = 0; uint32_t bs = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if ((dx | dy) == 0 || nx < 0 || ny < 0 || nx >= pw || ny >= ph || !cov[(size_t)ny * pw + nx]) continue;
                            const int d = dx * dx + dy * dy;
                            if (best < 0 || d < best) { best = d; bx = nx; by = ny; bs = cov[(size_t)ny * pw + nx]; }
                        }
                    if (best < 0) continue;
                    giDilate_.push_back(pack(x, y)); giDilate_.push_back(pack(bx, by)); giDilate_.push_back(bs - 1);
                    added.push_back({x, y, bs});
                }
            for (const auto& a : added) cov[(size_t)a.y * pw + a.x] = a.sub;
        }
    }
    LOG_INFO("RT GI: %zu lightmap texels in %zu pages from %zu subs (%.1f MB; %zu gutter texels; %zu vertex-lit subs keep the bake;"
             " %zu overlapping texels skipped)", giTexels_.size(), pages, giSubs_.size(),
             (double)(giTexels_.size() * sizeof(GiTexel)) / 1048576.0, giDilate_.size() / 3, giVertexLitSubs_, giOverlap_);
    std::vector<uint32_t>().swap(giCovered_);
    std::vector<size_t>().swap(giCoveredBase_);
}

// item 2: per-light RT shadows for the characters (Realistic only - Faithful keeps the original's per-object light
// visibility; WFC_RT_LIGHTSHADOWS=0 off)
bool Pipeline::rtShadowLightsOn() const {
    static const bool off = [] { const char* e = std::getenv("WFC_RT_LIGHTSHADOWS"); return e && e[0] == '0'; }();
    return !off && rtMode_ == 2 && rtShadowAoOn() && !rtFailed_;
}

bool Pipeline::rtLampShadowsWorld() const { return rtShadowLightsOn() && giActive() && r1Map_ && !giLightCal_.empty(); }

// v3 row 1 (Realistic): the characters' light environments take the RT sun as a direct light at full visibility - the
// per-pixel RT sun ray (dynamic pixels: world + characters) shadows it, so a robot under a building darkens where the
// building covers it. The original's per-object light visibility (the baked volume) ranked the sun out on most of
// Streets (0-7 % of character draws had it), leaving robots lit by the ambient only, the same in sun and under cover.
// Faithful / Off keep the original's visibility. WFC_RT_CHARSUN=0 off.
bool Pipeline::rtCharSun() const {
    static const bool off = [] { const char* e = std::getenv("WFC_RT_CHARSUN"); return e && e[0] == '0'; }();
    return !off && rtMode_ == 2 && rtShadowAoOn() && !rtFailed_ && rtSunLight_ >= 0;
}

bool Pipeline::giActive() const { return giSetupDone_ && !giGlTex_.empty() && rtMode_ == 2 && giOn(); }

namespace {
std::string giReadText(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
RtGiJob& jobOf(std::vector<float>& v) {                  // (the job's constants, stored opaque in the pipeline)
    if (v.size() * sizeof(float) < sizeof(RtGiJob)) { v.assign((sizeof(RtGiJob) + sizeof(float) - 1) / sizeof(float), 0.0f); new (v.data()) RtGiJob(); }
    return *reinterpret_cast<RtGiJob*>(v.data());
}
}  // namespace

// at the end of the load warm-up (after the reflection hit data: its page map): the calibration (Systems' fit.json +
// fit_lights.json in the V_bake directory), the bridge's buffers + shared pages, the GI arrays, the load-time passes
void Pipeline::giSetup() {
    giSetupDone_ = true;
    if (!giOn() || rtMode_ != 2 || giTexels_.empty()) return;
    assets::Json FL;
    const std::string fl = giReadText(r1Dir_ + "/fit_lights.json");
    if (r1Fit_[0] <= 0.0f || fl.empty() || !assets::Json::parse(fl, FL)) {
        LOG_INFO("RT GI: no calibration for this map (%s/fit.json k %.3f, fit_lights.json %s) - Realistic keeps the bake",
                 r1Dir_.c_str(), r1Fit_[0], fl.empty() ? "missing" : "unparsable");
        return;
    }
    RtGiSetup S;
    // the sun's share k per coefficient-0 page (Systems' per-page fit.json pages[]: capped at the map's k; the map's k
    // where a page has none)
    std::vector<float> pageK(lmTextures_.size(), r1Fit_[0]);
    {
        assets::Json F;
        const std::string ft = giReadText(r1Dir_ + "/fit.json");
        size_t perPage = 0;
        if (!ft.empty() && assets::Json::parse(ft, F))
            for (const auto& pg : F["pages"].arr) {
                auto it = lmIndex_.find(pg["page"].asString());
                const float k = pg["k"].asFloat(-1.0f);
                if (it == lmIndex_.end() || it->second < 0 || (size_t)it->second >= pageK.size() || k < 0.0f) continue;
                pageK[(size_t)it->second] = k * (r1Fit_[0] / std::max(F["k"].asFloat(r1Fit_[0]), 1e-6f));   // (x WFC_RT_R1K's scale)
                ++perPage;
            }
        LOG_INFO("RT GI: sun share k per page: %zu pages from fit.json pages[], the rest the map's %.3f", perPage, r1Fit_[0]);
    }
    // subs: pages + BSP receiver; the three decode scales (row 1 .w: the sun's k of its coefficient-0 page)
    std::vector<float>& D = S.data;
    const uint32_t subOff = 0;
    std::set<int> pages;
    for (const GiSub& s : giSubs_) {
        D.insert(D.end(), {(float)s.lm[0], (float)s.lm[1], (float)s.lm[2], (float)s.bsp});
        const float kSun = s.lm[0] >= 0 && (size_t)s.lm[0] < pageK.size() ? pageK[(size_t)s.lm[0]] : r1Fit_[0];
        for (int k = 0; k < 3; ++k) D.insert(D.end(), {s.scale[k][0], s.scale[k][1], s.scale[k][2], k == 0 ? kSun : k == 1 ? (float)s.vcnt : 0.0f});
        for (int k = 0; k < 3; ++k) if (s.lm[k] >= 0) pages.insert(s.lm[k]);
    }
    // lights: the static point / spot lights the bake used, calibrated per class (and per light where Systems' fit has
    // a reliable override); dynamic-only fills, disabled and never-built lights excluded
    const uint32_t lightOff = (uint32_t)(D.size() / 4);
    std::set<std::string> excluded;
    for (const auto& e : FL["exclude"].arr) excluded.insert(e["name"].asString());
    uint32_t nLights = 0;
    for (const Light& l : lights_) {
        if ((l.type != 0 && l.type != 1) || !l.enabled || excluded.count(l.name)) continue;
        const assets::Json& cls = FL[l.type == 1 ? "spot" : "point"];
        float scale = cls["scale"].asFloat(1.0f);
        if (FL["overrides"].has(l.name)) scale = FL["overrides"][l.name]["scale"].asFloat(scale);
        const bool linear = cls["falloff"].asString() == "linear";
        const float chan = ((l.chMask & 0x40000000u) ? 1.0f : 0.0f) + ((l.chMask & 0x20000000u) ? 2.0f : 0.0f);
        const core::Vec3 fwd = core::normalize(l.dir);
        giLightCal_[(int)(&l - lights_.data())] = {l.color.x * scale, l.color.y * scale, l.color.z * scale, linear ? 1.0f : 0.0f};
        D.insert(D.end(), {l.pos.x, l.pos.y, l.pos.z, l.radius});
        D.insert(D.end(), {l.color.x * scale, l.color.y * scale, l.color.z * scale, l.falloff});
        D.insert(D.end(), {fwd.x, fwd.y, fwd.z, l.type == 1 ? l.spotCosO : -2.0f});
        D.insert(D.end(), {l.type == 1 ? l.spotCosI : 1.0f, l.type == 1 ? 1.0f : 0.0f, chan, linear ? 1.0f : 0.0f});
        ++nLights;
    }
    // the hit data's page numbering -> GL pages with a GI page (4 per float4; -1: the hit reads the bake)
    const uint32_t pageMapOff = (uint32_t)(D.size() / 4);
    for (size_t i = 0; i < giHitPageGl_.size(); i += 4)
        for (size_t c = 0; c < 4; ++c) {
            const int k = i + c < giHitPageGl_.size() ? giHitPageGl_[i + c] : -1;
            D.push_back(k >= 0 && pages.count(k) ? (float)k : -1.0f);
        }
    if (giHitPageGl_.empty()) D.insert(D.end(), {-1.0f, -1.0f, -1.0f, -1.0f});
    // the GI pages: every coefficient page a GI sub writes, at its lightmap page's size
    S.pageSize.assign(lmTextures_.size(), {0, 0});
    for (int k : pages) {
        if (k < 0 || (size_t)k >= lmTextures_.size() || !lmTextures_[(size_t)k]) continue;
        GLint w = 0, h = 0;
        glBindTexture(GL_TEXTURE_2D, lmTextures_[(size_t)k]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        S.pageSize[(size_t)k] = {w, h};
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glx::textureCacheInvalidate();
    const int vRows = giVertexTexels_ && giVlmEntries_ ? (int)((giVlmEntries_ + 4095) / 4096) : 0;
    if (vRows && lmTextures_.size() < (size_t)kGiVertexPage) {   // the vertex page (RGBA32F)
        S.pageSize.resize((size_t)kGiVertexPage + 1, {0, 0});
        S.pageSize[(size_t)kGiVertexPage] = {4096, vRows};
        S.floatPage = kGiVertexPage;
    }
    S.hitToGi = giHitPageGl_;                            // (item 3: reflection hits read the GI pages)
    S.texels.resize(giTexels_.size() * 8);
    std::memcpy(S.texels.data(), giTexels_.data(), giTexels_.size() * sizeof(GiTexel));
    for (size_t i = 0; i + 2 < giDilate_.size(); i += 3) S.dilate.insert(S.dilate.end(), {giDilate_[i], giDilate_[i + 1], giDilate_[i + 2], 0u});
    if (!rtBridgeGiSetup(S, giGlTex_)) { giGlTex_.clear(); LOG_WARN("RT GI: bridge setup failed - Realistic keeps the bake"); return; }
    if (vRows && (size_t)kGiVertexPage < giGlTex_.size() && giGlTex_[(size_t)kGiVertexPage]) {   // the MDI sample buffer's RT twin
        GenBuffers(1, &giVlmBo_);
        BindBuffer(0x8C2A /*GL_TEXTURE_BUFFER*/, giVlmBo_);
        BufferData(0x8C2A, (GLsizeiptr)((size_t)4096 * (size_t)vRows * 16), nullptr, 0x88EA /*GL_DYNAMIC_COPY*/);
        BindBuffer(0x8C2A, 0);
        glGenTextures(1, &giVlmTex_);
        glBindTexture(0x8C2A, giVlmTex_);
        TexBuffer(0x8C2A, 0x8814 /*GL_RGBA32F*/, giVlmBo_);
        glBindTexture(0x8C2A, 0);
        glx::textureCacheInvalidate();
        LOG_INFO("RT GI: %zu vertex texels of vertex-lit subs (%zu sample entries, %d rows)", giVertexTexels_, giVlmEntries_, vRows);
    }
    // the job: sun (the fitted share k x sun_linear, R2's cone), sky (Systems' fit), the hemisphere rays, offsets
    RtGiJob& J = jobOf(giJob_);
    const Light* sun = nullptr;
    for (const Light& L : lights_) if (L.type == 2 && L.enabled && (!sun || L.brightness > sun->brightness)) sun = &L;
    static const float sunArea = [] { const char* e = std::getenv("WFC_RT_SUNAREA"); return e ? (float)std::atof(e) : 1.5f; }();
    if (sun) {
        const core::Vec3 d = core::normalize(sun->dir);
        J.toSun[0] = -d.x; J.toSun[1] = -d.y; J.toSun[2] = -d.z; J.toSun[3] = 1.0f;
        for (int c = 0; c < 3; ++c) J.sunCol[c] = r1Sun_[c];   // (x the texel's page k in the shader)
        J.sunCol[3] = std::tan(sunArea * 0.017453292f);
    }
    const assets::Json& sky = FL["sky"]["color_linear"];
    static const int rays = [] { const char* e = std::getenv("WFC_RT_GIRAYS"); return e ? std::max(0, std::atoi(e)) : 32; }();
    for (int c = 0; c < 3; ++c) J.sky[c] = sky[(size_t)c].asFloat(0.0f);
    J.sky[3] = (float)rays;
    // (diagnostics: the terms one at a time) WFC_RT_GILIGHTS=0 no static lights, WFC_RT_GISUN=0 no sun, WFC_RT_GISKY=0 no sky
    J.lights = WFC_ENV("WFC_RT_GILIGHTS") && WFC_ENV("WFC_RT_GILIGHTS")[0] == '0' ? 0u : nLights;
    if (WFC_ENV("WFC_RT_GISUN") && WFC_ENV("WFC_RT_GISUN")[0] == '0') J.toSun[3] = 0.0f;
    if (WFC_ENV("WFC_RT_GISKY") && WFC_ENV("WFC_RT_GISKY")[0] == '0') J.sky[0] = J.sky[1] = J.sky[2] = 0.0f;
    J.mix[0] = 0.05f; J.mix[1] = r1Fit_[2] < 0.0f ? -1.0f : 1.0f; J.mix[2] = r1Fit_[1] > 0.0f ? r1Fit_[1] : 1.0f; J.mix[3] = 1.0f;
    J.subOff = subOff; J.lightOff = lightOff; J.pageMapOff = pageMapOff; J.mask = 0x0Bu;
    static const float lightSize = [] { const char* e = std::getenv("WFC_RT_GILIGHTSIZE"); return e ? std::max(0.0f, (float)std::atof(e)) : 0.15f; }();
    J.lightSize = lightSize;                              // (item 5: soft lamp shadows; 0 = hard)
    // the GI arrays (the MDI rows sample arrays): RGBA16F, one level, layer for layer as the lightmap arrays
    constexpr GLenum kArr = 0x8C1A;
    giArrays_.assign(lmArrays_.size(), 0);
    for (size_t a = 0; a < lmArrays_.size(); ++a) {
        if (!lmArrays_[a]) continue;
        glBindTexture(kArr, lmArrays_[a]);
        GLint w = 0, h = 0, d = 0;
        glGetTexLevelParameteriv(kArr, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(kArr, 0, GL_TEXTURE_HEIGHT, &h);
        glGetTexLevelParameteriv(kArr, 0, 0x8071 /*GL_TEXTURE_DEPTH*/, &d);
        if (w <= 0 || h <= 0 || d <= 0) continue;
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(kArr, t);
        TexStorage3D(kArr, 1, 0x881A /*GL_RGBA16F*/, w, h, d);
        glTexParameteri(kArr, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(kArr, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(kArr, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(kArr, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        giArrays_[a] = t;
    }
    glBindTexture(kArr, 0);
    glx::textureCacheInvalidate();
    // the load-time passes (under the loading screen): propagation passes first (each replaces the estimate: one more
    // bounce each), then accumulation passes (the running mean: the noise). The bounce gain: the bake's GI intensity was
    // not cooked (Systems' audit) - fitted here so the RT lightmap's decoded mean matches the bake's over the measured
    // pages (secant on gain -> ratio, 3 rounds; WFC_RT_GIBOUNCE=<gain> fixes it instead)
    static const int passes = [] { const char* e = std::getenv("WFC_RT_GIPASSES"); return e ? std::max(1, std::atoi(e)) : 6; }();
    static const int accum = [] { const char* e = std::getenv("WFC_RT_GIACCUM"); return e ? std::max(0, std::atoi(e)) : 4; }();
    static const float fixedGain = [] { const char* e = std::getenv("WFC_RT_GIBOUNCE"); return e ? (float)std::atof(e) : -1.0f; }();
    auto runPasses = [&](float gain) {
        RtGiJob L = J;
        L.mix[3] = gain; L.mix[0] = 1.0f;
        bool ok = rtBridgeGiRunNow(L, passes, 262144u);
        L.mix[0] = J.mix[0];
        if (ok && accum) ok = rtBridgeGiRunNow(L, accum, 262144u);
        return ok;
    };
    // the decoded mean (value x scale, coefficients / 3: a flat surface) of the RT lightmap against the bake's over the
    // covered texels of up to 8 coefficient-0 pages
    std::map<int, std::vector<size_t>> byPage;
    for (size_t i = 0; i < giTexels_.size(); ++i)
        if ((giTexels_[i].pageXY & 511u) != (uint32_t)kGiVertexPage) byPage[(int)(giTexels_[i].pageXY & 511u)].push_back(i);
    auto measure = [&](bool log) -> double {
        double sumGi = 0, sumBake = 0; size_t n = 0; int shown = 0;
        for (const auto& kv : byPage) {
            if (shown >= 8) break;
            const int k0 = kv.first;
            auto readPage = [&](GLuint tex, int w, int h, std::vector<float>& out) {
                out.assign((size_t)w * h * 4, 0.0f);
                glBindTexture(GL_TEXTURE_2D, tex);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, out.data());
            };
            const GiSub& s0 = giSubs_[giTexels_[kv.second[0]].sub & 0x7FFFFFFFu];
            std::vector<float> g[3], b[3];
            const int w = giPageSize_[(size_t)k0].first, h = giPageSize_[(size_t)k0].second;
            bool ok = w > 0 && h > 0;
            for (int c = 0; c < 3 && ok; ++c) {
                const int k = s0.lm[c];
                if (k < 0 || (size_t)k >= giGlTex_.size() || !giGlTex_[(size_t)k]) { ok = false; break; }
                readPage(giGlTex_[(size_t)k], w, h, g[c]);
                readPage(lmTextures_[(size_t)k], w, h, b[c]);   // (GL decodes sRGB lightmap pages on read)
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            if (!ok) continue;
            double pg = 0, pb = 0; size_t pn = 0;
            for (size_t ti : kv.second) {
                const GiTexel& T = giTexels_[ti];
                const GiSub& s = giSubs_[T.sub & 0x7FFFFFFFu];
                const size_t o = (((size_t)(T.pageXY >> 20)) * (size_t)w + ((T.pageXY >> 9) & 2047u)) * 4;
                double lg = 0, lb = 0;
                for (int c = 0; c < 3; ++c)
                    for (int ch = 0; ch < 3; ++ch) { lg += g[c][o + ch] * s.scale[c][ch]; lb += b[c][o + ch] * s.scale[c][ch]; }
                pg += lg / 9.0; pb += lb / 9.0; ++pn;
            }
            if (log) LOG_INFO("RT GI check: page %d (%zu texels): mean decoded RT %.4f vs bake %.4f (ratio %.2f)", k0, pn,
                              pg / std::max<size_t>(pn, 1), pb / std::max<size_t>(pn, 1), pb > 0 ? pg / pb : 0.0);
            sumGi += pg; sumBake += pb; n += pn; ++shown;
        }
        glx::textureCacheInvalidate();
        return sumBake > 0 ? sumGi / sumBake : 0.0;
    };
    float gain = fixedGain >= 0.0f ? fixedGain : 1.0f;
    bool ran = runPasses(gain);
    double ratio = ran ? measure(false) : 0.0;
    if (ran && fixedGain < 0.0f && ratio > 0.05) {
        double g0 = gain, r0 = ratio;
        double g1 = std::min(24.0, std::max(0.25, gain / ratio));
        for (int it = 0; it < 3 && ran; ++it) {
            ran = runPasses((float)g1);
            const double r1 = ran ? measure(false) : 0.0;
            LOG_INFO("RT GI calibration: bounce gain %.3f -> RT / bake %.3f", g1, r1);
            gain = (float)g1; ratio = r1;
            if (std::fabs(r1 - 1.0) < 0.02 || std::fabs(r1 - r0) < 1e-4) break;
            const double g2 = g1 + (1.0 - r1) * (g1 - g0) / (r1 - r0);
            g0 = g1; r0 = r1; g1 = std::min(24.0, std::max(0.25, g2));
        }
    }
    jobOf(giJob_).mix[3] = gain;                          // (the per-frame slices keep the fitted gain)
    J.mix[3] = gain;
    giSliceFirst_ = 0; giSliceCount_ = giTexels_.size();   // (every layer refreshed once)
    giAfterFrame();
    if (ran) giBuildProbes();                            // (S3: from the calibrated RT lightmap)
    if (ran) {
        const double r = measure(true);
        LOG_INFO("RT GI check: RT / bake mean ratio %.2f at bounce gain %.3f%s", r, gain, fixedGain >= 0.0f ? " (WFC_RT_GIBOUNCE)" : "");
    }
    LOG_INFO("RT GI: Realistic lights the map by ray tracing (%zu texels, %u lights, sky %.3f %.3f %.3f, sun k %.3f, %d rays per texel;"
             " load passes %s)", giTexels_.size(), nLights, J.sky[0], J.sky[1], J.sky[2], r1Fit_[0], rays, ran ? "done" : "FAILED");
    if (!ran) giGlTex_.clear();
}

// per frame (Realistic, before the round trip): a slice of the texels re-traced (rotating), the gutter every 8th frame;
// the map's lighting is static, so after WFC_RT_GISWEEPS full sweeps (default 32: the running mean has its noise down)
// the per-frame cost stops (0 = never stops)
void Pipeline::giFrame() {
    if (!giActive() || giJob_.empty() || giTexels_.empty()) return;
    static const size_t perFrame = [] { const char* e = std::getenv("WFC_RT_GITEXELS"); return e ? (size_t)std::max(0, std::atoi(e)) : (size_t)131072; }();
    static const int sweeps = [] { const char* e = std::getenv("WFC_RT_GISWEEPS"); return e ? std::max(0, std::atoi(e)) : 32; }();
    if (sweeps > 0 && giSweeps_ >= sweeps) return;
    RtGiJob J = jobOf(giJob_);
    if (giNext_ >= giTexels_.size()) {
        giNext_ = 0;
        if (++giSweeps_ == sweeps && sweeps > 0)
            LOG_INFO("RT GI: %d sweeps done at frame %d - the RT lightmap is converged; per-frame tracing stops", sweeps, frameNo_);
        if (sweeps > 0 && giSweeps_ >= sweeps) {         // (a last gutter pass; every array layer refreshed after it)
            J.count = 0; J.dilate = true; giSliceFirst_ = 0; giSliceCount_ = giTexels_.size(); rtBridgeGiQueue(J); return;
        }
    }
    J.first = (uint32_t)giNext_;
    J.count = (uint32_t)std::min(perFrame, giTexels_.size() - giNext_);
    J.frame = (uint32_t)frameNo_ * 2654435761u;
    J.dilate = (frameNo_ & 7) == 0;
    giSliceFirst_ = giNext_; giSliceCount_ = J.count;
    giNext_ += J.count;
    rtBridgeGiQueue(J);
}

// after the round trip (GL waited for D3D12): the GI arrays' layers whose pages the slice touched (+ the gutter frame's)
void Pipeline::giAfterFrame() {
    if (giGlTex_.empty() || !CopyImageSubData) return;
    std::set<int> touched;
    const size_t end = std::min(giTexels_.size(), giSliceFirst_ + giSliceCount_);
    int last = -1;
    for (size_t i = giSliceFirst_; i < end; ++i) {
        const uint32_t sub = giTexels_[i].sub & 0x7FFFFFFFu;
        if ((int)sub == last || sub >= giSubs_.size()) continue;
        last = (int)sub;
        for (int k = 0; k < 3; ++k) if (giSubs_[sub].lm[k] >= 0) touched.insert(giSubs_[sub].lm[k]);   // (vertex subs: none)
    }
    giSliceCount_ = 0;
    giCopyVertexPage();
    constexpr GLenum kArr = 0x8C1A;
    for (int k : touched) {
        if ((size_t)k >= giGlTex_.size() || !giGlTex_[(size_t)k] || (size_t)k >= lmArrOf_.size() || (size_t)k >= lmLayer_.size()) continue;
        const int a = lmArrOf_[(size_t)k], layer = lmLayer_[(size_t)k];
        if (a < 0 || layer < 0 || (size_t)a >= giArrays_.size() || !giArrays_[(size_t)a]) continue;
        GLint w = 0, h = 0;
        glBindTexture(GL_TEXTURE_2D, giGlTex_[(size_t)k]);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        glBindTexture(GL_TEXTURE_2D, 0);
        CopyImageSubData(giGlTex_[(size_t)k], GL_TEXTURE_2D, 0, 0, 0, 0, giArrays_[(size_t)a], kArr, 0, 0, 0, layer, w, h, 1);
    }
    glx::textureCacheInvalidate();
}

// S3: a sparse probe grid - the cells near the lightmapped surfaces (each texel, moved 0.6 cells to its open side, marks
// the 8 grid points a lookup there blends, plus up to 3 m above floors, where characters stand), WFC_RT_GIPROBESPACING apart (default
// 1.0 m; widened to keep <= 4 M probes), WFC_RT_GIPROBERAYS rays each (default 256), traced from the calibrated RT
// lightmap (the same bounce gain); a probe that sees mostly back faces (inside a wall / floor) is not used
void Pipeline::giBuildProbes() {
    giProbes_.clear(); giProbeOk_.clear(); giProbeCell_.clear(); giProbeDims_[0] = giProbeDims_[1] = giProbeDims_[2] = 0;
    static const bool on = [] { const char* e = std::getenv("WFC_RT_GIPROBES"); return !(e && e[0] == '0'); }();   // (Realistic default since 14 Oct; WFC_RT_GIPROBES=0 off)
    if (!on || giTexels_.empty() || giJob_.empty()) return;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const GiTexel& T : giTexels_)
        for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], T.pos[c]); hi[c] = std::max(hi[c], T.pos[c]); }
    static const float spacing0 = [] { const char* e = std::getenv("WFC_RT_GIPROBESPACING"); return e ? std::max(0.25f, (float)std::atof(e)) : 1.0f; }();
    static const int rays = [] { const char* e = std::getenv("WFC_RT_GIPROBERAYS"); return e ? std::max(8, std::atoi(e)) : 256; }();
    const auto t0 = std::chrono::steady_clock::now();
    float s = spacing0;
    float origin[3];
    int dims[3];
    std::vector<uint32_t> cells;
    for (;;) {
        for (int c = 0; c < 3; ++c) { origin[c] = lo[c] - 2.0f; dims[c] = std::max(2, (int)std::ceil((hi[c] - lo[c] + 4.0f) / s) + 2); }
        if ((double)dims[0] * dims[1] * dims[2] >= 4294967295.0) { s *= 1.25f; continue; }
        std::unordered_set<uint32_t> occ;
        const int up = std::max(1, (int)std::ceil(3.0f / s));
        for (const GiTexel& T : giTexels_) {
            // (the open side: the texel moved 0.6 cells along its normal - the cells behind the surface, inside walls /
            // floors, were 58 % of Molten's probes; the column above only over floors, normal up)
            float n[3]; octDecode(T.nrm, n);
            int ci[3];
            for (int c = 0; c < 3; ++c) ci[c] = (int)std::floor((T.pos[c] + n[c] * 0.6f * s - origin[c]) / s);
            // (the 8 grid points giProbeAt blends for that point - floor and floor + 1 per axis - plus the column above floors)
            const int top = n[1] > 0.5f ? up : 1;
            for (int dz = 0; dz <= 1; ++dz)
                for (int dy = 0; dy <= top; ++dy)
                    for (int dx = 0; dx <= 1; ++dx) {
                        const int x = ci[0] + dx, y = ci[1] + dy, z = ci[2] + dz;
                        if (x < 0 || y < 0 || z < 0 || x >= dims[0] || y >= dims[1] || z >= dims[2]) continue;
                        occ.insert(((uint32_t)z * (uint32_t)dims[1] + (uint32_t)y) * (uint32_t)dims[0] + (uint32_t)x);
                    }
            if (occ.size() > 4000000u) break;
        }
        if (occ.size() > 4000000u) { s *= 1.25f; continue; }
        cells.assign(occ.begin(), occ.end());
        std::sort(cells.begin(), cells.end());
        break;
    }
    std::vector<float> out;
    if (!rtBridgeGiProbes(jobOf(giJob_), origin, s, dims, rays, cells, out)) { LOG_WARN("RT GI: probe grid failed - characters keep the bake's ambient"); return; }
    const size_t n = cells.size();
    // the front-face convention (GL counter-clockwise geometry in D3D12): the majority of open probes see front faces
    double fs = 0; size_t fn = 0;
    for (size_t i = 0; i < n; ++i) if (out[i * 24 + 7] > 0.5f) { fs += out[i * 24 + 3]; ++fn; }
    const bool flip = fn && fs / (double)fn < 0.5;
    giProbeOk_.assign(n, 0);
    size_t ok = 0;
    for (size_t i = 0; i < n; ++i) {
        const float front = flip ? 1.0f - out[i * 24 + 3] : out[i * 24 + 3];
        if (out[i * 24 + 7] < 0.05f || front >= 0.75f) { giProbeOk_[i] = 1; ++ok; }   // (open sky, or mostly front faces)
    }
    giProbes_.swap(out);
    giProbeCell_.reserve(n);
    for (size_t i = 0; i < n; ++i) giProbeCell_.emplace(cells[i], (uint32_t)i);
    for (int c = 0; c < 3; ++c) { giProbeOrigin_[c] = origin[c]; giProbeDims_[c] = dims[c]; }
    giProbeSpacing_ = s;
    // the light environments' cached cubes are rebuilt with the probes
    for (GpuMesh& g : meshes_) for (Sub& sb : g.subs) sb.envReady = false;
    envCache_.clear();
    LOG_INFO("RT GI probes: %zu near the surfaces (%d x %d x %d grid at %.2f m), %zu usable = %.1f %%, %d rays each, %.0f ms (front faces %s)",
             n, dims[0], dims[1], dims[2], s, ok, n ? 100.0 * ok / n : 0.0, rays,
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), flip ? "clockwise" : "counter-clockwise");
}

// the ambient cube at p from the 8 surrounding usable probes (trilinear, renormalised); false: none usable / outside
bool Pipeline::giProbeAt(const core::Vec3& p, core::Vec3 cube[6]) const {
    if (giProbes_.empty() || !giActive()) return false;
    const float q[3] = {(p.x - giProbeOrigin_[0]) / giProbeSpacing_, (p.y - giProbeOrigin_[1]) / giProbeSpacing_,
                        (p.z - giProbeOrigin_[2]) / giProbeSpacing_};
    int i0[3]; float f[3];
    for (int c = 0; c < 3; ++c) {
        if (q[c] < 0.0f || q[c] > (float)(giProbeDims_[c] - 1)) return false;
        i0[c] = std::min((int)q[c], giProbeDims_[c] - 2); f[c] = q[c] - (float)i0[c];
    }
    for (int k = 0; k < 6; ++k) cube[k] = {0, 0, 0};
    float wsum = 0.0f;
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const uint32_t cell = ((uint32_t)(i0[2] + dz) * (uint32_t)giProbeDims_[1] + (uint32_t)(i0[1] + dy)) * (uint32_t)giProbeDims_[0] +
                                      (uint32_t)(i0[0] + dx);
                const auto it = giProbeCell_.find(cell);
                if (it == giProbeCell_.end() || !giProbeOk_[it->second]) continue;
                const float w = (dx ? f[0] : 1.0f - f[0]) * (dy ? f[1] : 1.0f - f[1]) * (dz ? f[2] : 1.0f - f[2]);
                if (w <= 0.0f) continue;
                const float* v = &giProbes_[(size_t)it->second * 24];
                for (int k = 0; k < 6; ++k) cube[k] = cube[k] + core::Vec3{v[k * 4], v[k * 4 + 1], v[k * 4 + 2]} * w;
                wsum += w;
            }
    if (wsum < 1e-4f) return false;
    for (int k = 0; k < 6; ++k) cube[k] = cube[k] * (1.0f / wsum);
    return true;
}

// the vertex page (D3D12 RGBA32F, 4096 entries per row) -> the RT twin of the MDI sample buffer (GPU-side pack)
void Pipeline::giCopyVertexPage() {
    if (!giVlmBo_ || (size_t)kGiVertexPage >= giGlTex_.size() || !giGlTex_[(size_t)kGiVertexPage]) return;
    BindBuffer(0x88EB /*GL_PIXEL_PACK_BUFFER*/, giVlmBo_);
    glBindTexture(GL_TEXTURE_2D, giGlTex_[(size_t)kGiVertexPage]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindBuffer(0x88EB, 0);
    glx::textureCacheInvalidate();
}

}  // namespace wfc
}  // namespace render
