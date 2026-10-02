// Clean-room reconstruction — fixed-function OpenGL renderer (graybox milestone).
// Deliberately GL 1.1 immediate mode: no extension loading needed to get pixels on screen.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "render/Renderer.h"
#include "render/gl/WfcPipeline.h"
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

#include <cmath>
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
        if (wfc_.active()) wfc_.endFrame();
        glFlush();
    }

    bool loadMapRenderData(const std::string& mapName) override {
        bool ok = wfc_.load(mapName);
        if (ok) glDisable(GL_FOG);   // fog is evaluated per vertex in the shader path (UE3 height fog)
        return ok;
    }

    void setVisibilityQuery(VisibilityQuery q) override { wfc_.setVisibility(std::move(q)); }
    void setCharacterColors(const CharacterColors& c) override { wfc_.setCharacterColors(c); }

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
            core::Vec3 hx = ax * (p.w * 0.5f), hy = ay * (p.h * 0.5f);
            glColor4f(p.r, p.g, p.b, p.a);
            core::Vec3 q[4] = {p.pos - hx - hy, p.pos + hx - hy, p.pos + hx + hy, p.pos - hx + hy};
            // UVs: v0 (texture top) at the +axis end; uAlongAxis maps U along the axis instead.
            float uv[4][2];
            if (p.uAlongAxis) {
                uv[0][0] = p.u0; uv[0][1] = p.v1; uv[1][0] = p.u0; uv[1][1] = p.v0;
                uv[2][0] = p.u1; uv[2][1] = p.v0; uv[3][0] = p.u1; uv[3][1] = p.v1;
            } else {
                uv[0][0] = p.u0; uv[0][1] = p.v1; uv[1][0] = p.u1; uv[1][1] = p.v1;
                uv[2][0] = p.u1; uv[2][1] = p.v0; uv[3][0] = p.u0; uv[3][1] = p.v0;
            }
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

private:
    void drawMeshArrays(const MeshData& m, const core::Mat4& model, const core::Vec3& color) {
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
    wfc::Pipeline wfc_;
    std::vector<int> gpu_;
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
