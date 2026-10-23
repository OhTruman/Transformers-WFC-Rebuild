// Clean-room reconstruction — the campaign post-process chain (RE CAMPAIGN_POSTPROCESS_RE.md).
//
// UE3 (RE): a view takes the highest-Priority enabled PostProcessVolume containing the view point, else the persistent
// WorldInfo.DefaultPostProcessSettings; each settings group (Bloom / DOF / MotionBlur / Scene) blends from the current
// values to the new ones over the new settings' *_InterpolationDuration (1 s default; D5's Omega-critical grade 10 s).
// Kismet toggles volumes by name (setPostVolumeEnabled). The data: <chapter dir>/postprocess_volumes.json and pp/<lut>.png
// (tools/render/build_campaign_pp.py). Over the chain, a matinee cutscene's view (setCinematicView): the CameraActor's
// CamOverridePostProcess x its blend alpha, the ColorScale, the fade and the letterbox.
//
// Original look: these are the original's own settings, chosen and blended the original's way - no mode gate. MP maps
// carry no postprocess_volumes.json: ppChapter_ stays false and the map path is untouched.
#include "render/gl/WfcPipeline.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "core/Log.h"
#include "platform/Image.h"
#include "render/gl/GLExt.h"

namespace render {
namespace wfc {

using namespace glx;

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// a 1024 x 32 LUT strip (u = x + 32 z, v = y) as a 3D texture, as the map CLUT is loaded
GLuint lutFromStrip(const std::string& file, int n) {
    ImageData strip;
    if (!platform::decodeImage(file, strip) || !strip.valid() || strip.w < n * n || strip.h < n) return 0;
    std::vector<uint8_t> vol((size_t)n * n * n * 4);
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                std::memcpy(&vol[(((size_t)z * n + y) * n + x) * 4], &strip.rgba[((size_t)y * strip.w + (size_t)z * n + x) * 4], 4);
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_3D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    TexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, n, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, vol.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);
    return t;
}

void interpOf(const assets::Json& S, float out[4]) {
    out[0] = std::max(S["Bloom_InterpolationDuration"].asFloat(1.0f), 0.0f);
    out[1] = std::max(S["DOF_InterpolationDuration"].asFloat(1.0f), 0.0f);
    out[2] = std::max(S["MotionBlur_InterpolationDuration"].asFloat(1.0f), 0.0f);
    out[3] = std::max(S["Scene_InterpolationDuration"].asFloat(1.0f), 0.0f);
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }
core::Vec3 lerpv(const core::Vec3& a, const core::Vec3& b, float t) { return a + (b - a) * t; }

}  // namespace

void Pipeline::loadChapterPost(const std::string& chapterDir) {
    for (auto& kv : ppLuts_) if (kv.second) glDeleteTextures(1, &kv.second);
    ppLuts_.clear(); ppVols_.clear(); ppChapter_ = false; ppTarget_ = -2;
    std::string dir = chapterDir;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const std::string txt = slurp(dir + "/postprocess_volumes.json");
    assets::Json J;
    if (txt.empty() || !assets::Json::parse(txt, J)) {
        LOG_INFO("campaign post: no postprocess_volumes.json in %s - the persistent level's settings alone", dir.c_str());
        return;
    }
    auto lutOf = [&](const assets::Json& v) -> GLuint {
        const std::string obj = v["settings"]["ColorCorrectionTexture"].asString();
        const std::string file = v["lut"].asString();
        if (obj.empty() || file.empty()) return 0;
        auto it = ppLuts_.find(obj);
        if (it != ppLuts_.end()) return it->second;
        const GLuint t = lutFromStrip(dir + "/" + file, J["luts"][obj]["size"][0].asInt(32));
        ppLuts_[obj] = t;
        return t;
    };
    parsePost(J["world_default"], ppWorld_.s);
    interpOf(J["world_default"], ppWorld_.interp);
    {
        assets::Json W; W.type = assets::Json::Type::Object; W.obj["settings"] = J["world_default"]; W.obj["lut"] = J["world_lut"];
        ppWorld_.lut = lutOf(W);
    }
    const assets::Json& V = J["volumes"];
    for (size_t i = 0; i < V.size(); ++i) {
        const assets::Json& v = V[i];
        PpVol pv;
        pv.name = v["name"].asString();
        pv.priority = v["priority"].asFloat(0.0f);
        pv.enabled = v["enabled"].asBool(true);
        parsePost(v["settings"], pv.s);
        interpOf(v["settings"], pv.interp);
        pv.lut = lutOf(v);
        // the brush's triangles as planes facing out (the vertex centroid inside: convex elements)
        const assets::Json& P = v["polygons_m"];
        core::Vec3 cen{0, 0, 0}; int nv = 0;
        for (size_t p = 0; p < P.size(); ++p)
            for (size_t q = 0; q < P[p].size(); ++q) { cen = cen + core::Vec3{P[p][q][0].asFloat(), P[p][q][1].asFloat(), P[p][q][2].asFloat()}; ++nv; }
        if (nv) cen = cen * (1.0f / (float)nv);
        for (size_t p = 0; p < P.size(); ++p) {
            if (P[p].size() < 3) continue;
            auto vert = [&](size_t q) { return core::Vec3{P[p][q][0].asFloat(), P[p][q][1].asFloat(), P[p][q][2].asFloat()}; };
            const core::Vec3 a = vert(0);
            core::Vec3 n = core::cross(vert(1) - a, vert(2) - a);
            const float len = core::length(n);
            if (len < 1e-8f) continue;
            n = n * (1.0f / len);
            float d = -core::dot(n, a);
            if (core::dot(n, cen) + d > 0.0f) { n = n * -1.0f; d = -d; }
            pv.planes.push_back({n.x, n.y, n.z, d});
        }
        ppVols_.push_back(std::move(pv));
    }
    ppChapter_ = true;
    size_t on = 0;
    for (const PpVol& v : ppVols_) on += v.enabled ? 1 : 0;
    LOG_INFO("campaign post: %zu volumes (%zu enabled), %zu LUTs, world default bloom %.2f / DOF %d (%s)", ppVols_.size(), on,
             ppLuts_.size(), ppWorld_.s.bloomScale, (int)ppWorld_.s.dof, J["chapter"].asString().c_str());
}

void Pipeline::setPostVolumeEnabled(const std::string& name, bool on) {
    for (PpVol& v : ppVols_)
        if (v.name == name) {
            if (v.enabled != on) LOG_INFO("campaign post: %s %s (Kismet)", name.c_str(), on ? "on" : "off");
            v.enabled = on;
            return;
        }
    LOG_WARN("campaign post: no volume %s to toggle", name.c_str());
}

// before the post pass: the volume at the camera, the per-group blend toward it, the cinematic layer; writes post_ and the
// shader's LUT pair / colour scale / fade / letterbox
void Pipeline::chapterPostFrame() {
    // the enable flags folded into the values, so every field blends (bloom off = scale 0, DOF off = no blur)
    auto effective = [](Post p) {
        if (!p.bloom) { p.bloomScale = 0.0f; p.bloom = true; }
        if (!p.dof) { p.dofMaxBlur[0] = p.dofMaxBlur[1] = 0.0f; p.dof = true; }
        return p;
    };
    postColorScale_[0] = postColorScale_[1] = postColorScale_[2] = 1.0f;
    postFade_[0] = postFade_[1] = postFade_[2] = postFade_[3] = 0.0f;
    postLetterbox_[0] = postLetterbox_[1] = 0.5f;
    postLutA_ = postLutB_ = 0; postLutLerp_ = 0.0f;
    if (!ppChapter_) return;
    // the target: the highest-Priority enabled volume containing the camera, else the world default
    int target = -1;
    for (size_t i = 0; i < ppVols_.size(); ++i) {
        const PpVol& v = ppVols_[i];
        if (!v.enabled || v.planes.empty() || (target >= 0 && v.priority <= ppVols_[(size_t)target].priority)) continue;
        bool in = true;
        for (const auto& p : v.planes)
            if (p[0] * camPos_.x + p[1] * camPos_.y + p[2] * camPos_.z + p[3] > 0.01f) { in = false; break; }
        if (in) target = (int)i;
    }
    const PpVol& T = target >= 0 ? ppVols_[(size_t)target] : ppWorld_;
    if (target != ppTarget_) {
        // a new target: blend from the chain's values shown last frame (mid-blend included); the first frame of a chapter
        // starts at its target, no blend. The LUT pair restarts from the side that dominated.
        if (ppTarget_ == -2) { ppFrom_ = effective(T.s); ppFromLut_ = T.lut; }
        else { ppFrom_ = ppCur_; ppFromLut_ = ppCurLutLerp_ >= 0.5f ? ppToLut_ : ppFromLut_; }
        ppTo_ = effective(T.s); ppToLut_ = T.lut;
        std::copy(T.interp, T.interp + 4, ppToInterp_);
        ppT0_ = time_;
        LOG_INFO("campaign post: view now in %s (blend %.1f s)", target >= 0 ? T.name.c_str() : "the world default", ppToInterp_[3]);
        ppTarget_ = target;
    }
    auto alpha = [&](int g) { const float d = ppToInterp_[g]; return d <= 1e-3f ? 1.0f : std::min(std::max((time_ - ppT0_) / d, 0.0f), 1.0f); };
    const float aB = alpha(0), aD = alpha(1), aS = alpha(3);
    Post p = ppTo_;
    p.bloomScale = lerpf(ppFrom_.bloomScale, ppTo_.bloomScale, aB);
    p.bloomThreshold = lerpf(ppFrom_.bloomThreshold, ppTo_.bloomThreshold, aB);
    for (int k = 0; k < 4; ++k) p.dofPacked[k] = lerpf(ppFrom_.dofPacked[k], ppTo_.dofPacked[k], aD);
    for (int k = 0; k < 2; ++k) p.dofMaxBlur[k] = lerpf(ppFrom_.dofMaxBlur[k], ppTo_.dofMaxBlur[k], aD);
    p.shadows = lerpv(ppFrom_.shadows, ppTo_.shadows, aS);
    p.highlights = lerpv(ppFrom_.highlights, ppTo_.highlights, aS);
    p.midtones = lerpv(ppFrom_.midtones, ppTo_.midtones, aS);
    p.desat = lerpf(ppFrom_.desat, ppTo_.desat, aS);
    postLutA_ = ppFromLut_; postLutB_ = ppToLut_; postLutLerp_ = ppFromLut_ == ppToLut_ ? 1.0f : aS;
    ppCur_ = p; ppCurLutLerp_ = postLutLerp_;
    // the cinematic layer: CamOverridePostProcess x its alpha, ColorScale, fade, letterbox
    if (cin_.active) {
        const IRenderer::PostOverride& o = cin_.pp;
        const float a = std::min(std::max(cin_.ppAlpha, 0.0f), 1.0f);
        if (a > 0.0f) {
            Post c;
            c.bloom = o.bloom; c.dof = o.dof;
            c.bloomScale = o.bloomScale; c.bloomThreshold = o.bloomThreshold;
            c.dofPacked[0] = o.dofFocusDistance; c.dofPacked[1] = 1.0f / std::max(o.dofNearFalloff, 1.0f);
            c.dofPacked[2] = o.dofFalloffExponent; c.dofPacked[3] = 1.0f / std::max(o.dofFarFalloff, 1.0f);
            c.dofMaxBlur[0] = o.dofMaxNearBlur; c.dofMaxBlur[1] = o.dofMaxFarBlur;
            c.shadows = {o.shadows[0], o.shadows[1], o.shadows[2]};
            c.highlights = {o.highlights[0], o.highlights[1], o.highlights[2]};
            c.midtones = {o.midtones[0], o.midtones[1], o.midtones[2]};
            c.desat = o.desaturation;
            if (!o.sceneEffect) { c.shadows = {0, 0, 0}; c.highlights = {1, 1, 1}; c.midtones = {1, 1, 1}; c.desat = 0.0f; }
            c = effective(c);
            p.bloomScale = lerpf(p.bloomScale, c.bloomScale, a); p.bloomThreshold = lerpf(p.bloomThreshold, c.bloomThreshold, a);
            // (DOF: the camera's focus replaces the volume's once it weighs in at all - a focus distance does not lerp sensibly)
            if (o.dof) for (int k = 0; k < 4; ++k) p.dofPacked[k] = c.dofPacked[k];
            for (int k = 0; k < 2; ++k) p.dofMaxBlur[k] = lerpf(p.dofMaxBlur[k], c.dofMaxBlur[k], a);
            p.shadows = lerpv(p.shadows, c.shadows, a); p.highlights = lerpv(p.highlights, c.highlights, a);
            p.midtones = lerpv(p.midtones, c.midtones, a); p.desat = lerpf(p.desat, c.desat, a);
            if (!o.lut.empty()) {
                auto it = ppLuts_.find(o.lut);
                if (it != ppLuts_.end() && it->second) { postLutA_ = postLutLerp_ >= 0.5f ? postLutB_ : postLutA_; postLutB_ = it->second; postLutLerp_ = a; }
            }
        }
        for (int k = 0; k < 3; ++k) postColorScale_[k] = cin_.colorScale[k];
        for (int k = 0; k < 4; ++k) postFade_[k] = cin_.fade[k];
        if (cin_.aspect > 0.0f && vpW_ > 0 && vpH_ > 0) {
            const float view = (float)vpW_ / (float)vpH_;
            if (cin_.aspect > view) postLetterbox_[1] = 0.5f * view / cin_.aspect;        // bars top and bottom
            else postLetterbox_[0] = 0.5f * cin_.aspect / view;                            // (a narrower shot: bars at the sides)
        }
    }
    p.bloom = p.bloomScale > 0.0f;
    p.dof = p.dofMaxBlur[0] > 0.0f || p.dofMaxBlur[1] > 0.0f;
    post_ = p;
}

}  // namespace wfc
}  // namespace render
