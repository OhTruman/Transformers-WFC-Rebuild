// Clean-room reconstruction — fixed-function OpenGL renderer (graybox milestone).
// Deliberately GL 1.1 immediate mode: no extension loading needed to get pixels on screen.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include "assets/Gltf.h"
#include "assets/Json.h"
#include <sstream>
#include <map>
#include <fstream>
#include <array>
#include <windows.h>
#endif
#include <GL/gl.h>

#include "render/Renderer.h"
#include "render/gl/WfcPipeline.h"
#include "platform/Image.h"
#include "core/Config.h"
#include "core/Log.h"

// GL 1.2 enums the GL 1.1 header may omit (drivers still support them).
#ifndef GL_LIGHT_MODEL_COLOR_CONTROL
#define GL_LIGHT_MODEL_COLOR_CONTROL 0x81F8
#endif
#ifndef GL_SEPARATE_SPECULAR_COLOR
#define GL_SEPARATE_SPECULAR_COLOR 0x81FA
#endif
// GL 1.3 texture-env combine (for applying the lightmap HDR ScaleVector up to 4x).
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_RGB_SCALE 0x8573
#define GL_SOURCE0_RGB 0x8580
#define GL_SOURCE1_RGB 0x8581
#define GL_OPERAND0_RGB 0x8590
#define GL_OPERAND1_RGB 0x8591
#define GL_PRIMARY_COLOR 0x8577
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace render {
namespace {

class GLRenderer final : public IRenderer {
public:
    bool init() override {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glClearColor(0.16f, 0.07f, 0.08f, 1.0f);   // warm dark to blend with the recovered fog

        // Lighting approximation. NOTE: the real WFC Streets lighting is BAKED into lightmaps
        // (StaticLightCollectionActor; lightmaps not extracted), so this is a [PROV] stand-in.
        // The previous global+light ambient of 0.35+0.35 floored every surface at ~0.70
        // brightness -> the washed-out, contrast-free look. Lower ambient + a warm key light +
        // a separate specular term restore contrast and metallic highlights.
        GLfloat gAmb[4] = {0.14f, 0.15f, 0.17f, 1.0f};   // cool global ambient
        GLfloat lAmb[4] = {0.10f, 0.10f, 0.12f, 1.0f};
        GLfloat dif[4]  = {1.05f, 1.00f, 0.92f, 1.0f};   // warm directional key
        GLfloat spec[4] = {0.55f, 0.55f, 0.55f, 1.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT, lAmb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
        glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gAmb);
        // Add specular AFTER texture modulation so highlights are not darkened by the texture.
        glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        GLfloat matSpec[4] = {0.45f, 0.45f, 0.45f, 1.0f};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matSpec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 22.0f);

        // [CONF] HeightFog recovered from MP_IAC_Streets_ART_m HeightFogComponent_11010:
        // LightColor (234,91,116) warm red-pink, Density 2e-5/UU = 0.002/m (StartDistance 2048 UU
        // not modelled by GL_EXP). This replaces the earlier guessed blue-grey fog.
        GLfloat fogC[4] = {234.0f / 255.0f, 91.0f / 255.0f, 116.0f / 255.0f, 1.0f};
        glFogi(GL_FOG_MODE, GL_EXP);
        glFogfv(GL_FOG_COLOR, fogC);
        glFogf(GL_FOG_DENSITY, 0.0014f);   // [CONF-approx] softened from 0.002/m for the play-area scale
        glHint(GL_FOG_HINT, GL_NICEST);
        glEnable(GL_FOG);
        return true;
    }

    void beginFrame(const Camera& camIn, int vpW, int vpH) override {
        const Camera& cam0 = camIn;
        glViewport(0, 0, vpW, vpH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Diagnostic camera override for render inspection: WFC_RENDERCAM="x,y,z,yawRad,pitchRad".
        Camera camOv = cam0;
        if (const char* rc = std::getenv("WFC_RENDERCAM"))
            std::sscanf(rc, "%f,%f,%f,%f,%f", &camOv.pos.x, &camOv.pos.y, &camOv.pos.z, &camOv.yaw, &camOv.pitch);
        const Camera& cam = camOv;
        if (wfc_.active()) wfc_.beginFrame(cam, vpW, vpH);
        vpW_ = vpW; vpH_ = vpH;
        inFrame_ = true;
        glMatrixMode(GL_PROJECTION);
        core::Mat4 p = cam.proj();
        glLoadMatrixf(p.m);

        glMatrixMode(GL_MODELVIEW);
        view_ = cam.view();
        glLoadMatrixf(view_.m);

        // Directional light fixed in world space (set while modelview == view).
        GLfloat lightDir[4] = {0.4f, 1.0f, 0.6f, 0.0f};
        glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
    }

    void endFrame() override {
        if (wfc_.active()) { wfc_.drawMapPresentation(); wfc_.endFrame(); }
        drawReticle();
        for (const ScreenBatch& b : screenQueue_) drawScreenNow(b);   // 2D composition on top, in order
        screenQueue_.clear();
        inFrame_ = false;
        glFlush();
    }

    void drawScreenTriangles(const ScreenBatch& b) override {
        if (b.verts.empty()) return;
        if (inFrame_) screenQueue_.push_back(b); else drawScreenNow(b);
    }

    // ---- Canvas fonts (UE3 UFont from render data) ----
    struct CanvasFont {
        bool ok = false;
        std::vector<std::array<int, 6>> chars;        // StartU, StartV, USize, VSize, TextureIndex, VerticalOffset
        std::map<uint32_t, int> remap;
        std::vector<TextureHandle> pages; std::vector<std::pair<int, int>> pageSize;
        float lineHeight = 0;
    };
    std::map<std::string, CanvasFont> fonts_;
    CanvasFont& font(const std::string& name) {
        auto it = fonts_.find(name);
        if (it != fonts_.end()) return it->second;
        CanvasFont& f = fonts_[name];
        std::string dir = wfc::Pipeline::renderDataRoot() + "/_ui/fonts/";
        std::ifstream in(dir + name + ".json", std::ios::binary);
        if (!in) return f;
        std::stringstream ss; ss << in.rdbuf();
        assets::Json J;
        if (!assets::Json::parse(ss.str(), J)) return f;
        for (size_t i = 0; i < J["characters"].size(); ++i) {
            const assets::Json& c = J["characters"][i];
            std::array<int, 6> a{};
            for (int k = 0; k < 6; ++k) a[(size_t)k] = (int)c[(size_t)k].asFloat();
            f.chars.push_back(a);
            f.lineHeight = std::max(f.lineHeight, (float)a[3]);
        }
        for (const auto& kv : J["remap"].obj) f.remap[(uint32_t)std::stoul(kv.first)] = (int)kv.second.asFloat();
        for (size_t i = 0; i < J["pages"].size(); ++i) {
            ImageData img;
            if (!platform::decodeImage(dir + J["pages"][i].asString(), img)) continue;
            f.pageSize.push_back({img.w, img.h});
            f.pages.push_back(uploadTexture(img));
        }
        f.ok = !f.chars.empty() && !f.pages.empty();
        return f;
    }
    static std::vector<uint32_t> utf8Decode(const std::string& s) {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = (unsigned char)s[i];
            uint32_t cp = c; int n = 0;
            if (c >= 0xF0) { cp = c & 0x07; n = 3; } else if (c >= 0xE0) { cp = c & 0x0F; n = 2; } else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
            ++i;
            for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
            out.push_back(cp);
        }
        return out;
    }
    const std::array<int, 6>* glyph(const CanvasFont& f, uint32_t cp) const {
        auto it = f.remap.find(cp);
        int idx = it != f.remap.end() ? it->second : (f.remap.count('?') ? f.remap.at('?') : -1);
        return idx >= 0 && (size_t)idx < f.chars.size() ? &f.chars[(size_t)idx] : nullptr;
    }
    bool canvasTextSize(const std::string& name, const std::string& utf8, float& w, float& h, float scale) override {
        CanvasFont& f = font(name);
        w = h = 0;
        if (!f.ok) return false;
        for (uint32_t cp : utf8Decode(utf8)) if (const auto* g = glyph(f, cp)) w += (*g)[2] * scale;
        h = f.lineHeight * scale;
        return true;
    }
    bool drawCanvasText(const std::string& name, const std::string& utf8, float x, float y, const uint8_t rgba[4],
                        float scale) override {
        CanvasFont& f = font(name);
        if (!f.ok) return false;
        std::map<int, ScreenBatch> perPage;
        float cx = x;
        for (uint32_t cp : utf8Decode(utf8)) {
            const auto* g = glyph(f, cp);
            if (!g) continue;
            const auto& c = *g;
            int page = c[4] < (int)f.pages.size() ? c[4] : 0;
            float tw = (float)f.pageSize[(size_t)page].first, th = (float)f.pageSize[(size_t)page].second;
            float w = c[2] * scale, h = c[3] * scale, top = y + c[5] * scale;
            if (w > 0 && h > 0) {
                float u0 = c[0] / tw, v0 = c[1] / th, u1 = (c[0] + c[2]) / tw, v1 = (c[1] + c[3]) / th;
                ScreenBatch& b = perPage[page];
                b.texture = f.pages[(size_t)page]; b.blend = ScreenBlend::Alpha; b.clampUV = true;
                ScreenVertex q[4] = {{cx, top, u0, v0, rgba[0], rgba[1], rgba[2], rgba[3]},
                                     {cx + w, top, u1, v0, rgba[0], rgba[1], rgba[2], rgba[3]},
                                     {cx + w, top + h, u1, v1, rgba[0], rgba[1], rgba[2], rgba[3]},
                                     {cx, top + h, u0, v1, rgba[0], rgba[1], rgba[2], rgba[3]}};
                for (int k : {0, 1, 2, 0, 2, 3}) b.verts.push_back(q[k]);
            }
            cx += c[2] * scale;
        }
        for (auto& kv : perPage) drawScreenTriangles(kv.second);
        return true;
    }

    bool pickWorld(const core::Vec3& o, const core::Vec3& d, float maxDist, PickHit& out) override {
        float best = maxDist;
        bool hit = false;
        for (const MeshData& m : meshes_) {
            if (m.subs.empty() || m.subs[0].component.empty()) continue;      // authored world meshes only
            for (const SubMesh& sm : m.subs) {
                for (uint32_t k = sm.indexOffset; k + 2 < sm.indexOffset + sm.indexCount; k += 3) {
                    const float* a = &m.positions[(size_t)m.indices[k] * 3];
                    const float* b = &m.positions[(size_t)m.indices[k + 1] * 3];
                    const float* c = &m.positions[(size_t)m.indices[k + 2] * 3];
                    core::Vec3 A{a[0], a[1], a[2]}, B{b[0], b[1], b[2]}, C{c[0], c[1], c[2]};
                    core::Vec3 e1 = B - A, e2 = C - A, pv = core::cross(d, e2);
                    float det = core::dot(e1, pv);
                    if (std::fabs(det) < 1e-9f) continue;
                    float inv = 1.0f / det;
                    core::Vec3 tv = o - A;
                    float u = core::dot(tv, pv) * inv;
                    if (u < 0 || u > 1) continue;
                    core::Vec3 qv = core::cross(tv, e1);
                    float v = core::dot(d, qv) * inv;
                    if (v < 0 || u + v > 1) continue;
                    float t = core::dot(e2, qv) * inv;
                    if (t <= 1e-3f || t >= best) continue;
                    best = t; hit = true;
                    out.component = sm.component; out.mesh = sm.sourceMesh;
                    out.material = sm.material >= 0 && (size_t)sm.material < m.mats.size() ? m.mats[(size_t)sm.material].sourceName : "";
                    out.distance = t; out.point = o + d * t; out.normal = core::normalize(core::cross(e1, e2));
                }
            }
        }
        return hit;
    }

    bool updateTexture(TextureHandle h, const ImageData& img) override {
        if (h < 0 || (size_t)h >= textures_.size() || !img.valid()) return false;
        glBindTexture(GL_TEXTURE_2D, textures_[(size_t)h]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }
    int viewportWidth() const override { return vpW_; }
    int viewportHeight() const override { return vpH_; }

    void drawScreenNow(const ScreenBatch& b) {
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        const int W = vp[2] > 0 ? vp[2] : vpW_, H = vp[3] > 0 ? vp[3] : vpH_;
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING); glDisable(GL_FOG);
        if (b.scissor) { glEnable(GL_SCISSOR_TEST); glScissor(b.sx, H - (b.sy + b.sh), b.sw, b.sh); }
        switch (b.blend) {
            case ScreenBlend::Opaque: glDisable(GL_BLEND); break;
            case ScreenBlend::Alpha: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case ScreenBlend::Premultiplied: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            case ScreenBlend::Additive: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
            case ScreenBlend::Multiply: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
        }
        const bool tex = b.texture >= 0 && (size_t)b.texture < textures_.size();
        if (tex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textures_[(size_t)b.texture]);
            const GLint wrap = b.clampUV ? GL_CLAMP_TO_EDGE : GL_REPEAT, filt = b.linearFilter ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBegin(GL_TRIANGLES);
        for (const ScreenVertex& v : b.verts) {
            glColor4ub(v.r, v.g, v.b, v.a);
            glTexCoord2f(v.u, v.v);
            glVertex2f(v.x, v.y);
        }
        glEnd();
        glColor4ub(255, 255, 255, 255);
        if (tex) { glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); }
        if (b.scissor) glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    void setReticle(const ReticleState& s) override { reticle_ = s; }
    bool evaluatesFxMaterials() const override { return wfc_.active(); }

    // mc_crosshairIonBlaster (Hud_GFX.gfx char 404), at crosshairAnchor_mc = stage (560, 360) of the
    // 1120x720 movie: three prong_mc clips (char 402 -> shape 401) under rotations 0/120/240 deg, each
    // the 32x16 bitmap 400 (Hud_GFX_I190.png) stretched by its fill matrix to (-15..15, -6..6) px.
    // Its DoAction: SpreadMultiplier 300; on WeaponSpread change every prong_mc._y eases to
    // -300 * WeaponSpread over 0.2 s ("easeout"). Tint: NotifyTargetTypeChanged 0 -> 0x50B5D5,
    // 1 -> 0xFF3333, else white, eased over 0.2 s. [PROV] stage scale mode (ShowAll assumed) and the
    // HmActionScript easeout curve (quadratic ease-out assumed).
    void drawReticle() {
        if (!reticle_.visible || vpW_ <= 0 || vpH_ <= 0) return;
        if (reticleTex_ == 0 && !reticleTried_) {
            reticleTried_ = true;
            ImageData img;
            std::string path = std::string(core::config::kAssetRootDefault) + "/../content/UI_GFxHud_p/Hud_GFX_I190.png";
            if (platform::decodeImage(path, img)) {
                glGenTextures(1, &reticleTex_);
                glBindTexture(GL_TEXTURE_2D, reticleTex_);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
            } else {
                LOG_WARN("reticle: %s not found; crosshair not drawn", path.c_str());
            }
        }
        if (reticleTex_ == 0) return;
        // tweens (prong offset, tint) driven by wall time like the Flash timeline
        static auto t0 = std::chrono::steady_clock::now();
        static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
        static int frames = 0;
        float now = lockstep ? (float)(++frames) / 60.0f : std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        auto tween = [&](Tween& tw, float target, float dur) {
            if (target != tw.to) { tw.from = tw.value(now); tw.to = target; tw.start = now; tw.dur = dur; }
            return tw.value(now);
        };
        // the clip's first WeaponSpread is applied instantly, later changes ease (fineaim_hud.json, AssetTools 7a69756)
        if (!prongSet_) { prongTween_.to = 300.0f * reticle_.weaponSpread; prongTween_.start = -1.0f; prongSet_ = true; }
        float off = tween(prongTween_, 300.0f * reticle_.weaponSpread, 0.2f);
        uint32_t rgb = reticle_.targetType == 0 ? 0x50B5D5u : reticle_.targetType == 1 ? 0xFF3333u : 0xFFFFFFu;
        float tint[3];
        for (int c = 0; c < 3; ++c) tint[c] = tween(tintTween_[c], (float)((rgb >> (16 - 8 * c)) & 0xFF) / 255.0f, 0.2f);

        float scale = std::min((float)vpW_ / 1120.0f, (float)vpH_ / 720.0f);
        // midCenter_mc (560, 360) -> crosshair controller sprite 621 at (0.2, 0.2) -> crosshairAnchor_mc (0, 0)
        float cx = vpW_ * 0.5f + 0.2f * scale, cy = vpH_ * 0.5f + 0.2f * scale;
        glViewport(0, 0, vpW_, vpH_);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, vpW_, vpH_, 0, -1, 1);                       // Flash stage axes: y down
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_FOG);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, reticleTex_);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(tint[0], tint[1], tint[2], 1.0f);
        glBegin(GL_QUADS);
        for (int k = 0; k < 3; ++k) {
            float a = (float)k * 2.0943951f, ca = std::cos(a), sa = std::sin(a);
            const float px[4] = {-15, 15, 15, -15}, py[4] = {-6, -6, 6, 6}, u[4] = {0, 1, 1, 0}, v[4] = {0, 0, 1, 1};
            for (int i = 0; i < 4; ++i) {
                float x = px[i], y = py[i] - off;                  // prong_mc._y = -300 * spread
                glTexCoord2f(u[i], v[i]);
                glVertex2f(cx + scale * (ca * x - sa * y), cy + scale * (sa * x + ca * y));
            }
        }
        glEnd();
        glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    // Handles uploaded before the unload stay in range but become empty (no GPU mesh, CPU copy dropped): stale
    // handles draw nothing; owners re-upload for the next level.
    bool drawMaterialTile(const MaterialTile& t) override {
        if (!wfc_.active() || !wfc_.hasMaterial(t.material)) return false;
        wfc_.drawMaterialTile(t);
        return true;
    }
    bool hasMaterial(const std::string& m) const override { return wfc_.active() && wfc_.hasMaterial(m); }

    // ---- frontend scenes ----
    MeshHandle sceneMesh_ = kInvalidMesh;
    std::string sceneDir_;
    bool loadFrontendScene(const std::vector<std::string>& levels) override {
        unloadFrontendScene();
        const std::string data = wfc::Pipeline::renderDataRoot(), assets = wfc::Pipeline::assetRoot();
        // One render-data map at a time: the requested level with the most placed scenery (a lobby's persistent level is
        // nearly empty and streams UI_CharacterCustomization_m) [PARTIAL: no multi-level composition yet].
        std::string dir;
        size_t bestSize = 0;
        for (const std::string& l : levels) {
            std::string d = l.size() > 2 && l.compare(l.size() - 2, 2, "_m") == 0 ? l.substr(0, l.size() - 2) : l;
            std::ifstream probe(data + "/" + d + "/materials_glsl.json");
            std::ifstream glb(assets + "/Maps/" + d + "/world.glb", std::ios::binary | std::ios::ate);
            if (!probe || !glb) continue;
            size_t sz = (size_t)glb.tellg();
            if (dir.empty() || sz > bestSize) { dir = d; bestSize = sz; }
        }
        if (dir.empty()) { LOG_WARN("frontend scene: no render data for any of %zu levels", levels.size()); return false; }
        MeshData world;
        auto tw = std::chrono::steady_clock::now();
        bool okWorld = assets::loadGlb(assets + "/Maps/" + dir + "/world.glb", world);
        LOG_INFO("frontend scene %s: world.glb read %.0f ms", dir.c_str(),
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tw).count());
        wfc_.yieldLoad();
        if (!okWorld) {
            LOG_WARN("frontend scene %s: world.glb missing", dir.c_str());
            return false;
        }
        if (!loadMapRenderData(dir)) return false;
        sceneMesh_ = uploadMesh(world);
        sceneDir_ = dir;
        LOG_INFO("frontend scene %s loaded (%zu submeshes)", dir.c_str(), world.subs.size());
        return sceneMesh_ != kInvalidMesh;
    }
    void drawFrontendScene(const core::Vec3& p, const core::Vec3& r, float fovDeg, int w, int h, double t) override {
        if (sceneMesh_ == kInvalidMesh) return;
        Camera cam;
        cam.pos = core::Vec3{p.x * 0.01f, p.z * 0.01f, p.y * 0.01f};       // UE (x, y, z) UU -> glTF (x, z, y) m
        const float pr = r.x * 0.0174533f, yr = r.y * 0.0174533f;
        core::Vec3 fUE{std::cos(pr) * std::cos(yr), std::cos(pr) * std::sin(yr), std::sin(pr)};
        core::Vec3 f{fUE.x, fUE.z, fUE.y};
        cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
        cam.yaw = std::atan2(-f.x, -f.z);
        cam.fovXDeg = fovDeg;                                                 // UE FOVAngle is horizontal
        cam.aspect = (float)w / (float)std::max(h, 1);
        setMapClock((float)t);
        beginFrame(cam, w, h);
        drawMesh(sceneMesh_, core::Mat4::identity(), core::Vec3{1, 1, 1});
        endFrame();
    }
    void unloadFrontendScene() override {
        if (sceneMesh_ == kInvalidMesh && sceneDir_.empty()) return;
        unloadMapRenderData();
        sceneMesh_ = kInvalidMesh;
        sceneDir_.clear();
    }

    void setLoadYield(std::function<void()> y) override { wfc_.setLoadYield(std::move(y)); }

    void unloadMapRenderData() override {
        wfc_.release();
        for (size_t i = 0; i < meshes_.size(); ++i) { meshes_[i] = MeshData{}; gpu_[i] = -1; }
    }

    bool loadMapRenderData(const std::string& mapName) override {
        // one map's render data at a time: a new load releases the previous map (level travel, frontend scenes)
        if (wfc_.active() || sceneMesh_ != kInvalidMesh) {
            unloadMapRenderData();
            sceneMesh_ = kInvalidMesh; sceneDir_.clear();
        }
        bool ok = wfc_.load(mapName);
        if (ok) glDisable(GL_FOG);   // fog is evaluated per vertex in the shader path (UE3 height fog)
        return ok;
    }

    void setVisibilityQuery(VisibilityQuery q) override { wfc_.setVisibility(std::move(q)); }
    void setCharacterColors(const CharacterColors& c) override { wfc_.setCharacterColors(c); }
    void setDrawOwner(int o) override { wfc_.setDrawOwner(o); }
    void setActorHidden(const std::string& actor, bool hidden) override { wfc_.setActorHidden(actor, hidden); }
    void setMapEffectActive(const std::string& what, bool active) override { wfc_.setMapEffectActive(what, active); }
    void setMapEffectState(const std::string& k, bool a, bool h) override { wfc_.setMapEffectState(k, a, h); }
    void setActiveGameRules(const std::vector<std::string>& r) override { wfc_.setActiveGameRules(r); }
    bool drawsAuthoredMapFx() const override { return wfc_.active(); }
    void setMapClock(float t) override { wfc_.setMapClock(t); }
    void setDestructibleState(const std::string& a, int s) override { wfc_.setDestructibleState(a, s); }

    MeshHandle uploadMesh(const MeshData& mesh) override {
        if (mesh.empty()) return kInvalidMesh;
        meshes_.push_back(mesh);            // keep a CPU copy for GL 1.1 client arrays
        gpu_.push_back(wfc_.active() ? wfc_.upload(mesh) : -1);
        return (MeshHandle)(meshes_.size() - 1);
    }

    TextureHandle uploadTexture(const ImageData& img) override {
        if (!img.valid()) return kInvalidTexture;
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        textures_.push_back(id);
        return (TextureHandle)(textures_.size() - 1);
    }

    void drawMesh(MeshHandle h, const core::Mat4& model, const core::Vec3& color) override {
        if (h < 0 || (size_t)h >= meshes_.size()) return;
        if (wfc_.active() && gpu_[(size_t)h] >= 0) { wfc_.draw(gpu_[(size_t)h], model); glLoadMatrixf(view_.m); return; }
        drawMeshArrays(meshes_[(size_t)h], model, color);
    }

    void drawDynamicMesh(const MeshData& m, const core::Mat4& model, const core::Vec3& color) override {
        if (m.empty()) return;
        if (wfc_.active()) { wfc_.drawDynamic(m, model); glLoadMatrixf(view_.m); return; }
        drawMeshArrays(m, model, color);
    }

    void drawBox(const core::Vec3& center, const core::Vec3& size,
                 const core::Vec3& color, float yaw) override {
        core::Mat4 model = core::Mat4::translate(center) * core::Mat4::rotateY(yaw) *
                           core::Mat4::scale(size);
        core::Mat4 mv = view_ * model;
        glLoadMatrixf(mv.m);
        drawUnitCube(color);
        glLoadMatrixf(view_.m);
    }

    void drawGroundGrid(float half, float spacing, const core::Vec3& c) override {
        glLoadMatrixf(view_.m);
        // Solid dark quad first for depth, then grid lines on top.
        glBegin(GL_QUADS);
        glColor3f(c.x * 0.35f, c.y * 0.35f, c.z * 0.35f);
        glVertex3f(-half, 0, -half); glVertex3f(-half, 0, half);
        glVertex3f(half, 0, half);   glVertex3f(half, 0, -half);
        glEnd();

        glColor3f(c.x, c.y, c.z);
        glBegin(GL_LINES);
        for (float x = -half; x <= half + 0.001f; x += spacing) {
            glVertex3f(x, 0.01f, -half); glVertex3f(x, 0.01f, half);
        }
        for (float z = -half; z <= half + 0.001f; z += spacing) {
            glVertex3f(-half, 0.01f, z); glVertex3f(half, 0.01f, z);
        }
        glEnd();
    }

    bool captureScreenshot(const char* path) override {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        int w = vp[2], h = vp[3];
        if (w <= 0 || h <= 0) return false;
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

        std::FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        int rowPad = (4 - (w * 3) % 4) % 4;
        uint32_t imgSize = (uint32_t)((w * 3 + rowPad) * h);
        uint32_t fileSize = 54 + imgSize;
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        std::memcpy(hdr + 2, &fileSize, 4);
        uint32_t dataOff = 54; std::memcpy(hdr + 10, &dataOff, 4);
        uint32_t dibSize = 40; std::memcpy(hdr + 14, &dibSize, 4);
        std::memcpy(hdr + 18, &w, 4);
        std::memcpy(hdr + 22, &h, 4);            // positive = bottom-up (matches glReadPixels)
        uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
        uint16_t bpp = 24; std::memcpy(hdr + 28, &bpp, 2);
        std::memcpy(hdr + 34, &imgSize, 4);
        std::fwrite(hdr, 1, 54, f);
        std::vector<uint8_t> row((size_t)w * 3 + rowPad, 0);
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = rgb.data() + (size_t)y * w * 3;
            for (int x = 0; x < w; ++x) {       // RGB -> BGR
                row[(size_t)x * 3 + 0] = src[(size_t)x * 3 + 2];
                row[(size_t)x * 3 + 1] = src[(size_t)x * 3 + 1];
                row[(size_t)x * 3 + 2] = src[(size_t)x * 3 + 0];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        LOG_INFO("screenshot: %s (%dx%d)", path, w, h);
        return true;
    }

    void drawLine(const core::Vec3& a, const core::Vec3& b, const core::Vec3& c) override {
        glLoadMatrixf(view_.m);
        glColor3f(c.x, c.y, c.z);
        glBegin(GL_LINES);
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
        glEnd();
    }

    void drawParticles(const ParticleBatch& b) override {
        if (!b.p || b.n == 0) return;
        glLoadMatrixf(view_.m);
        // Camera basis from the view matrix (rows of the rotation part).
        core::Vec3 camR{view_.m[0], view_.m[4], view_.m[8]};
        core::Vec3 camU{view_.m[1], view_.m[5], view_.m[9]};
        core::Vec3 camF{-view_.m[2], -view_.m[6], -view_.m[10]};
        if (wfc_.active() && b.material) {
            // Original emitter material: same quad construction as below, shaded by the compiled graph.
            std::vector<wfc::Pipeline::Sprite> sp(b.n);
            for (size_t i = 0; i < b.n; ++i) {
                const Particle& p = b.p[i];
                core::Vec3 ax, ay;
                particleAxes(p, camR, camU, camF, ax, ay);
                core::Vec3 hx = ax * (p.w * 0.5f), hy = ay * (p.h * 0.5f);
                wfc::Pipeline::Sprite& s = sp[i];
                s.c[0] = p.pos - hx - hy; s.c[1] = p.pos + hx - hy; s.c[2] = p.pos + hx + hy; s.c[3] = p.pos - hx + hy;
                particleUVs(p, s.uv);
                float k = b.colorScale;
                s.color[0] = p.r * k; s.color[1] = p.g * k; s.color[2] = p.b * k; s.color[3] = p.a;
            }
            if (std::getenv("WFC_FXLOG")) {
                static int logged = 0;
                if (logged++ < 8) LOG_INFO("fx sprites %s: n=%zu color0=(%.2f,%.2f,%.2f,%.2f)", b.material, sp.size(),
                                           sp[0].color[0], sp[0].color[1], sp[0].color[2], sp[0].color[3]);
            }
            if (wfc_.drawSprites(b.material, sp.data(), sp.size(), camF * -1.0f)) { glLoadMatrixf(view_.m); return; }
        }
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        // Restore the caller's fog state afterwards: the WFC shader path keeps fixed-function fog off.
        const GLboolean fogWas = glIsEnabled(GL_FOG);
        bool add = b.blend == ParticleBlend::Additive;
        if (add) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDisable(GL_FOG); }
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        bool tex = b.tex >= 0 && (size_t)b.tex < textures_.size();
        if (tex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textures_[(size_t)b.tex]);
            if (b.colorScale > 1.0f) {
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, b.colorScale >= 4.0f ? 4.0f : (b.colorScale >= 2.0f ? 2.0f : 1.0f));
                // Alpha: texture alpha x vertex alpha (smoke opacity is baked into alpha at load).
                glTexEnvi(GL_TEXTURE_ENV, 0x8572 /*GL_COMBINE_ALPHA*/, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, 0x8588 /*GL_SOURCE0_ALPHA*/, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, 0x8598 /*GL_OPERAND0_ALPHA*/, GL_SRC_ALPHA);
                glTexEnvi(GL_TEXTURE_ENV, 0x8589 /*GL_SOURCE1_ALPHA*/, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, 0x8599 /*GL_OPERAND1_ALPHA*/, GL_SRC_ALPHA);
            }
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBegin(GL_QUADS);
        for (size_t i = 0; i < b.n; ++i) {
            const Particle& p = b.p[i];
            core::Vec3 ax, ay;   // ax: across (width), ay: up/along (height/length)
            particleAxes(p, camR, camU, camF, ax, ay);
            core::Vec3 hx = ax * (p.w * 0.5f), hy = ay * (p.h * 0.5f);
            glColor4f(p.r, p.g, p.b, p.a);
            core::Vec3 q[4] = {p.pos - hx - hy, p.pos + hx - hy, p.pos + hx + hy, p.pos - hx + hy};
            float uv[4][2];
            particleUVs(p, uv);
            for (int k = 0; k < 4; ++k) {
                glTexCoord2f(uv[k][0], uv[k][1]);
                glVertex3f(q[k].x, q[k].y, q[k].z);
            }
        }
        glEnd();
        if (tex && b.colorScale > 1.0f) {
            glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        if (fogWas) glEnable(GL_FOG); else glDisable(GL_FOG);
    }

    static void particleAxes(const Particle& p, const core::Vec3& camR, const core::Vec3& camU, const core::Vec3& camF,
                             core::Vec3& ax, core::Vec3& ay) {
        float al = core::length(p.axis);
        if (al > 1e-5f) {
            ay = p.axis * (1.0f / al);
            ax = core::normalize(core::cross(camF, ay));
            if (core::length(ax) < 1e-4f) ax = camR;
        } else {
            float c = std::cos(p.rot), s = std::sin(p.rot);
            ax = camR * c + camU * s;
            ay = camU * c - camR * s;
        }
    }
    // UVs: v0 (texture top) at the +axis end; uAlongAxis maps U along the axis instead.
    static void particleUVs(const Particle& p, float uv[4][2]) {
        if (p.uAlongAxis) {
            uv[0][0] = p.u0; uv[0][1] = p.v1; uv[1][0] = p.u0; uv[1][1] = p.v0;
            uv[2][0] = p.u1; uv[2][1] = p.v0; uv[3][0] = p.u1; uv[3][1] = p.v1;
        } else {
            uv[0][0] = p.u0; uv[0][1] = p.v1; uv[1][0] = p.u1; uv[1][1] = p.v1;
            uv[2][0] = p.u1; uv[2][1] = p.v0; uv[3][0] = p.u0; uv[3][1] = p.v0;
        }
    }

    void drawMeshFx(MeshHandle h, const core::Mat4& model, float r, float g, float b, float a,
                    float colorScale, float fresnelExp, float fresnelScale, float fresnelPower) override {
        if (h < 0 || (size_t)h >= meshes_.size()) return;
        if (wfc_.active() && (size_t)h < gpu_.size() && gpu_[(size_t)h] >= 0) {
            // Mesh emitter with its original material: the graph supplies fresnel/panning/depth fade;
            // the particle colour (HDR) is the mesh-emitter vertex colour.
            float sc = colorScale, col[4] = {r * sc, g * sc, b * sc, a};
            if (wfc_.drawFx(gpu_[(size_t)h], model, col)) { glLoadMatrixf(view_.m); return; }
        }
        const MeshData& m = meshes_[(size_t)h];
        core::Mat4 mv = view_ * model;
        glLoadMatrixf(mv.m);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FOG);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, m.positions.data());
        bool haveUV = m.hasUV();
        if (haveUV) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, m.uv.data()); }
        float sc = colorScale >= 4.0f ? 4.0f : (colorScale >= 2.0f ? 2.0f : 1.0f);
        glColor4f(r, g, b, a);
        // Fresnel rim: per-vertex colour from the view direction (camera position from the view).
        std::vector<float> fcol;
        bool fres = fresnelExp > 0.0f && m.normals.size() == m.positions.size();
        if (fres) {
            core::Mat4 inv = view_;   // eye = -R^T t
            core::Vec3 eye{-(inv.m[0] * inv.m[12] + inv.m[1] * inv.m[13] + inv.m[2] * inv.m[14]),
                           -(inv.m[4] * inv.m[12] + inv.m[5] * inv.m[13] + inv.m[6] * inv.m[14]),
                           -(inv.m[8] * inv.m[12] + inv.m[9] * inv.m[13] + inv.m[10] * inv.m[14])};
            size_t vc = m.vertexCount();
            fcol.resize(vc * 4);
            for (size_t i = 0; i < vc; ++i) {
                core::Vec3 p = core::transformPoint(model, {m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]});
                core::Vec3 n = core::normalize(core::transformDir(model, {m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]}));
                core::Vec3 v = core::normalize(eye - p);
                float f = std::pow(std::max(0.0f, 1.0f - std::fabs(core::dot(n, v))), fresnelExp) * fresnelScale;
                f = std::pow(core::clampf(f, 0.0f, 1.0f), fresnelPower);
                fcol[i * 4] = r * f; fcol[i * 4 + 1] = g * f; fcol[i * 4 + 2] = b * f; fcol[i * 4 + 3] = a;
            }
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_FLOAT, 0, fcol.data());
        }
        std::vector<SubMesh> all;
        if (m.subs.empty()) { SubMesh whole; whole.indexCount = (uint32_t)m.indices.size(); all.push_back(whole); }
        for (const SubMesh& s : m.subs.empty() ? all : m.subs) {
            const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
            bool tex = haveUV && mat && mat->tex >= 0 && (size_t)mat->tex < textures_.size();
            if (tex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, textures_[(size_t)mat->tex]);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, sc);
            } else {
                glDisable(GL_TEXTURE_2D);
            }
            glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT, m.indices.data() + s.indexOffset);
            if (tex) { glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); }
        }
        if (haveUV) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if (fres) glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glEnable(GL_FOG);
        glLoadMatrixf(view_.m);
    }

private:
    void drawMeshArrays(const MeshData& m, const core::Mat4& model, const core::Vec3& color) {
        // client-side vertex arrays: no buffer object may be bound (another renderer / UI pass may leave one)
        glx::BindBuffer(GL_ARRAY_BUFFER, 0);
        glx::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glx::BindVertexArray(0);
        core::Mat4 mv = view_ * model;
        glLoadMatrixf(mv.m);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glColor3f(color.x, color.y, color.z);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, m.positions.data());
        bool haveN = m.normals.size() == m.positions.size();
        if (haveN) { glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, m.normals.data()); }
        bool haveUV = m.hasUV();
        if (haveUV) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, m.uv.data()); }

        static const bool lmOff = std::getenv("WFC_NOLIGHTMAP") != nullptr;   // A/B debug toggle
        bool haveUV1 = m.hasUV1() && !lmOff;
        if (m.subs.empty()) {
            glDrawElements(GL_TRIANGLES, (GLsizei)m.indices.size(), GL_UNSIGNED_INT, m.indices.data());
        } else {
            // --- base pass: lightmapped submeshes drawn UNLIT (baked lighting replaces the
            // stand-in directional); everything else lit as before. ---
            for (const SubMesh& s : m.subs) {
                const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size())
                                          ? &m.mats[(size_t)s.material] : nullptr;
                bool lm = haveUV1 && s.lightmapTex >= 0 && (size_t)s.lightmapTex < textures_.size();
                if (lm) glDisable(GL_LIGHTING); else glEnable(GL_LIGHTING);
                TextureHandle th = mat ? mat->tex : kInvalidTexture;
                if (haveUV && th >= 0 && (size_t)th < textures_.size()) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, textures_[(size_t)th]);
                    glColor3f(1, 1, 1);
                } else {
                    glDisable(GL_TEXTURE_2D);
                    core::Vec3 c = mat ? mat->color : color;
                    glColor3f(c.x, c.y, c.z);
                }
                glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT,
                               m.indices.data() + s.indexOffset);
            }
            glEnable(GL_LIGHTING);
            glDisable(GL_TEXTURE_2D);

            // --- baked-lightmap modulate pass: framebuffer *= atlas(uv1*scale + bias) ---
            if (haveUV1) {
                bool anyLm = false;
                for (const SubMesh& s : m.subs)
                    if (s.lightmapTex >= 0 && (size_t)s.lightmapTex < textures_.size()) { anyLm = true; break; }
                if (anyLm) {
                    glDisable(GL_LIGHTING);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_DST_COLOR, GL_ZERO);        // multiply
                    glDepthMask(GL_FALSE);
                    glEnable(GL_TEXTURE_2D);
                    glTexCoordPointer(2, GL_FLOAT, 0, m.uv1.data());   // lightmap UVs
                    // Reconstruct HDR lightmap brightness: fragment = atlas * (scaleVec/4) * 4.
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                    glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 4.0f);
                    for (const SubMesh& s : m.subs) {
                        if (s.lightmapTex < 0 || (size_t)s.lightmapTex >= textures_.size()) continue;
                        float r = s.lmScaleVec[0] * 0.25f, g = s.lmScaleVec[1] * 0.25f, b = s.lmScaleVec[2] * 0.25f;
                        glColor3f(r > 1 ? 1 : r, g > 1 ? 1 : g, b > 1 ? 1 : b);
                        glMatrixMode(GL_TEXTURE);
                        glLoadIdentity();
                        glTranslatef(s.lmBias[0], s.lmBias[1], 0.0f);
                        glScalef(s.lmScale[0], s.lmScale[1], 1.0f);     // atlasUV = uv1*scale + bias
                        glMatrixMode(GL_MODELVIEW);
                        glBindTexture(GL_TEXTURE_2D, textures_[(size_t)s.lightmapTex]);
                        glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT,
                                       m.indices.data() + s.indexOffset);
                    }
                    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
                    glTexCoordPointer(2, GL_FLOAT, 0, m.uv.data());     // restore UV0
                    glDisable(GL_TEXTURE_2D);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                    glEnable(GL_LIGHTING);
                }
            }

            // Emissive additive pass: self-illumination (Autobot optics, energon glow),
            // unlit and added on top so it reads as glow regardless of scene lighting.
            bool anyEm = false;
            if (haveUV)
                for (const SubMesh& s : m.subs) {
                    const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size())
                                              ? &m.mats[(size_t)s.material] : nullptr;
                    if (mat && mat->emissiveTexHandle >= 0 &&
                        (size_t)mat->emissiveTexHandle < textures_.size()) { anyEm = true; break; }
                }
            if (anyEm) {
                glDisable(GL_LIGHTING);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glDepthMask(GL_FALSE);
                glEnable(GL_TEXTURE_2D);
                glColor3f(1, 1, 1);
                for (const SubMesh& s : m.subs) {
                    const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size())
                                              ? &m.mats[(size_t)s.material] : nullptr;
                    TextureHandle eh = mat ? mat->emissiveTexHandle : kInvalidTexture;
                    if (eh < 0 || (size_t)eh >= textures_.size()) continue;
                    glBindTexture(GL_TEXTURE_2D, textures_[(size_t)eh]);
                    glDrawElements(GL_TRIANGLES, (GLsizei)s.indexCount, GL_UNSIGNED_INT,
                                   m.indices.data() + s.indexOffset);
                }
                glDisable(GL_TEXTURE_2D);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
                glEnable(GL_LIGHTING);
            }
        }

        if (haveUV) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        if (haveN) glDisableClientState(GL_NORMAL_ARRAY);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_LIGHTING);
        glLoadMatrixf(view_.m);
    }

    static void face(float r, float g, float b,
                     core::Vec3 a, core::Vec3 bb, core::Vec3 c, core::Vec3 d) {
        glColor3f(r, g, b);
        glVertex3f(a.x, a.y, a.z); glVertex3f(bb.x, bb.y, bb.z);
        glVertex3f(c.x, c.y, c.z); glVertex3f(d.x, d.y, d.z);
    }

    static void drawUnitCube(const core::Vec3& col) {
        // Unit cube of half-size 0.5, faces shaded for readability (fake directional light).
        const float h = 0.5f;
        core::Vec3 p000{-h, -h, -h}, p001{-h, -h, h}, p010{-h, h, -h}, p011{-h, h, h};
        core::Vec3 p100{h, -h, -h}, p101{h, -h, h}, p110{h, h, -h}, p111{h, h, h};
        auto shade = [&](float k) { return core::Vec3{col.x * k, col.y * k, col.z * k}; };
        glBegin(GL_QUADS);
        core::Vec3 s;
        s = shade(1.00f); face(s.x, s.y, s.z, p011, p111, p110, p010); // top (+Y)
        s = shade(0.45f); face(s.x, s.y, s.z, p000, p100, p101, p001); // bottom (-Y)
        s = shade(0.85f); face(s.x, s.y, s.z, p001, p101, p111, p011); // front (+Z)
        s = shade(0.60f); face(s.x, s.y, s.z, p100, p000, p010, p110); // back (-Z)
        s = shade(0.75f); face(s.x, s.y, s.z, p101, p100, p110, p111); // right (+X)
        s = shade(0.55f); face(s.x, s.y, s.z, p000, p001, p011, p010); // left (-X)
        glEnd();
    }

    core::Mat4 view_;
    int vpW_ = 0, vpH_ = 0;
    ReticleState reticle_;
    GLuint reticleTex_ = 0;
    bool reticleTried_ = false;
    struct Tween {
        float from = 0, to = 0, start = -1, dur = 0.2f;
        float value(float now) const {                          // quadratic ease-out
            if (start < 0 || dur <= 0) return to;
            float t = std::min(std::max((now - start) / dur, 0.0f), 1.0f);
            return from + (to - from) * (1.0f - (1.0f - t) * (1.0f - t));
        }
    };
    Tween prongTween_, tintTween_[3] = {{1, 1}, {1, 1}, {1, 1}};
    bool prongSet_ = false;
    wfc::Pipeline wfc_;
    std::vector<int> gpu_;
    std::vector<ScreenBatch> screenQueue_;   // 2D batches submitted inside a 3D frame
    bool inFrame_ = false;
    std::vector<MeshData> meshes_;
    std::vector<GLuint> textures_;
};

} // namespace

IRenderer* createGLRenderer() {
    auto* r = new GLRenderer();
    if (!r->init()) { delete r; return nullptr; }
    return r;
}

} // namespace render

namespace render {
std::string wfcRenderDataRoot() { return wfc::Pipeline::renderDataRoot(); }
}
