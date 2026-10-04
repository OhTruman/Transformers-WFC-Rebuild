#include "ui/gl/GfxRendererGL.h"
#include "render/gl/GLExt.h"
#include "platform/Image.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace ui {

namespace {
typedef void(APIENTRY* PFN_RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
PFN_RenderbufferStorageMultisample glRbMs = nullptr;

const char* kVS = R"(#version 120
attribute vec2 aPos;
uniform vec2 uView;
uniform vec3 uW0, uW1, uF0, uF1;
varying vec2 vFill;
void main() {
    vec3 p3 = vec3(aPos, 1.0);
    vec2 p = vec2(dot(uW0, p3), dot(uW1, p3));
    vFill = vec2(dot(uF0, p3), dot(uF1, p3));
    gl_Position = vec4(p.x / uView.x * 2.0 - 1.0, 1.0 - p.y / uView.y * 2.0, 0.0, 1.0);
}
)";

const char* kFS = R"(#version 120
uniform int uMode;
uniform vec4 uColor, uMul, uAdd;
uniform sampler2D uTex;
uniform vec2 uTexSize;
uniform float uFocal;
varying vec2 vFill;
void main() {
    vec4 c;
    if (uMode == 0) c = uColor;
    else if (uMode == 1) c = texture2D(uTex, vec2((vFill.x + 16384.0) / 32768.0, 0.5));
    else if (uMode == 2) c = texture2D(uTex, vec2(length(vFill) / 16384.0, 0.5));
    else if (uMode == 3) {
        // Focal radial gradient: ray from the focal point through the pixel to the unit circle.
        vec2 f = vec2(uFocal * 16384.0, 0.0);
        vec2 d = vFill - f;
        float a = dot(d, d), b = 2.0 * dot(f, d), cc = dot(f, f) - 16384.0 * 16384.0;
        float t = (-b + sqrt(max(b * b - 4.0 * a * cc, 0.0))) / (2.0 * max(a, 1e-6));
        c = texture2D(uTex, vec2(1.0 / max(t, 1e-6), 0.5));
    } else c = texture2D(uTex, vFill / uTexSize);
    c = clamp(c * uMul + uAdd, 0.0, 1.0);
    gl_FragColor = vec4(c.rgb * c.a, c.a);
}
)";

const char* kCompVS = R"(#version 120
attribute vec2 aPos;
varying vec2 vUv;
void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
const char* kCompFS = R"(#version 120
uniform sampler2D uTex;
varying vec2 vUv;
void main() { gl_FragColor = texture2D(uTex, vUv); }
)";

unsigned compile(GLenum type, const char* src) {
    GLuint s = glx::CreateShader(type);
    glx::ShaderSource(s, 1, &src, nullptr);
    glx::CompileShader(s);
    GLint ok = 0;
    glx::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glx::GetShaderInfoLog(s, sizeof log, nullptr, log); LOG_ERROR("GFX shader: %s", log); return 0; }
    return s;
}

unsigned link(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glx::CreateProgram();
    glx::AttachShader(p, v);
    glx::AttachShader(p, f);
    glx::BindAttribLocation(p, 0, "aPos");
    glx::LinkProgram(p);
    GLint ok = 0;
    glx::GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glx::GetProgramInfoLog(p, sizeof log, nullptr, log); LOG_ERROR("GFX link: %s", log); return 0; }
    return p;
}

struct Seg { gfx::Point a, b; };

} // namespace

GfxRendererGL::~GfxRendererGL() = default;

bool GfxRendererGL::init() {
    if (!glx::CreateShader && !glx::load()) { LOG_ERROR("GFX renderer: GL entry points unavailable"); return false; }
    if (!glx::CreateShader) glx::load();
    glRbMs = (PFN_RenderbufferStorageMultisample)wglGetProcAddress("glRenderbufferStorageMultisample");
    prog_ = link(kVS, kFS);
    compProg_ = link(kCompVS, kCompFS);
    if (!prog_ || !compProg_) return false;
    uView_ = glx::GetUniformLocation(prog_, "uView");
    uWorld_ = glx::GetUniformLocation(prog_, "uW0");
    uFillInv_ = glx::GetUniformLocation(prog_, "uF0");
    uMode_ = glx::GetUniformLocation(prog_, "uMode");
    uColor_ = glx::GetUniformLocation(prog_, "uColor");
    uMul_ = glx::GetUniformLocation(prog_, "uMul");
    uAdd_ = glx::GetUniformLocation(prog_, "uAdd");
    uTex_ = glx::GetUniformLocation(prog_, "uTex");
    uTexSize_ = glx::GetUniformLocation(prog_, "uTexSize");
    uFocal_ = glx::GetUniformLocation(prog_, "uFocal");
    glx::GenBuffers(1, &vbo_);
    glx::GenVertexArrays(1, &vao_);
    GLint maxS = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    samples_ = glRbMs ? std::min(8, (int)maxS) : 0;
    ok_ = true;
    LOG_INFO("GFX renderer ready (MSAA %d)", samples_);
    return true;
}

void GfxRendererGL::ownedNames(GlCensus::Owned& o) const {
    for (const auto& [k, t] : textures_) if (t.id) o.textures.insert(t.id);
    for (const auto& [k, id] : gradients_) o.textures.insert(id);
    if (resTex_) o.textures.insert(resTex_);
    if (video_.id) o.textures.insert(video_.id);
    if (vbo_) o.buffers.insert(vbo_);
    if (vao_) o.vertexArrays.insert(vao_);
    for (unsigned f : {msFbo_, resFbo_}) if (f) o.framebuffers.insert(f);
    for (unsigned r : {msColor_, msDepth_}) if (r) o.renderbuffers.insert(r);
    for (unsigned pr : {prog_, compProg_}) if (pr) o.programs.insert(pr);
}

gfx::Matrix GfxRendererGL::stageMatrix(float stageW, float stageH, int width, int height) {
    float s = std::min(width / stageW, height / stageH);
    gfx::Matrix m;
    m.a = m.d = s / 20.0f;
    m.tx = (width - stageW * s) * 0.5f;
    m.ty = (height - stageH * s) * 0.5f;
    return m;
}

void GfxRendererGL::begin(int width, int height) {
    w_ = width; h_ = height;
    if (!ok_) return;
    if (width != fbw_ || height != fbh_) {
        if (msFbo_) { glx::DeleteFramebuffers(1, &msFbo_); glx::DeleteRenderbuffers(1, &msColor_); glx::DeleteRenderbuffers(1, &msDepth_); }
        if (resFbo_) { glx::DeleteFramebuffers(1, &resFbo_); glDeleteTextures(1, &resTex_); }
        glx::GenFramebuffers(1, &msFbo_);
        glx::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
        glx::GenRenderbuffers(1, &msColor_);
        glx::BindRenderbuffer(GL_RENDERBUFFER, msColor_);
        if (samples_ > 0) glRbMs(GL_RENDERBUFFER, samples_, GL_RGBA8, width, height);
        else glx::RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
        glx::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
        glx::GenRenderbuffers(1, &msDepth_);
        glx::BindRenderbuffer(GL_RENDERBUFFER, msDepth_);
        if (samples_ > 0) glRbMs(GL_RENDERBUFFER, samples_, GL_DEPTH24_STENCIL8, width, height);
        else glx::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glx::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
        if (glx::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("GFX: MSAA framebuffer incomplete");
        glGenTextures(1, &resTex_);
        glBindTexture(GL_TEXTURE_2D, resTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glx::GenFramebuffers(1, &resFbo_);
        glx::BindFramebuffer(GL_FRAMEBUFFER, resFbo_);
        glx::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resTex_, 0);
        fbw_ = width; fbh_ = height;
    }
    glx::BindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClearStencil(0);
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glx::BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glx::UseProgram(prog_);
    glx::BindVertexArray(vao_);
    glx::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    glx::EnableVertexAttribArray(0);
    glx::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glx::Uniform2f(uView_, (float)width, (float)height);
    glx::Uniform1i(uTex_, 0);
    glx::ActiveTexture(GL_TEXTURE0);
    level_ = 0;
}

void GfxRendererGL::end() {
    if (!ok_) return;
    glDisable(GL_STENCIL_TEST);
    glx::BindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glx::BindFramebuffer(GL_DRAW_FRAMEBUFFER, resFbo_);
    glx::BlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glx::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w_, h_);
    glx::UseProgram(compProg_);
    glx::Uniform1i(glx::GetUniformLocation(compProg_, "uTex"), 0);
    glBindTexture(GL_TEXTURE_2D, resTex_);
    glEnable(GL_BLEND);
    glx::BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    float q[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    glx::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    glx::BufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    glx::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glx::UseProgram(0);
    glDisable(GL_BLEND);
    glx::BindVertexArray(0);
    // Leave neutral bindings for whatever draws next frame: the renderer's legacy path uses client-side vertex arrays,
    // which a bound GL_ARRAY_BUFFER would turn into offsets into this VBO (the frontend scene drew nothing).
    glx::DisableVertexAttribArray(0);
    glx::BindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

const GfxRendererGL::Cached& GfxRendererGL::cache(const gfx::ShapeDef* s, bool glyph) {
    auto key = std::make_pair(s, glyph);
    auto it = shapes_.find(key);
    if (it != shapes_.end()) return it->second;
    Cached c;
    // Fill edges per (style set, style): fill1 forward, fill0 reversed -> consistent winding.
    std::map<std::pair<int, int>, std::vector<Seg>> edges;
    for (const gfx::ShapePath& p : s->paths) {
        int f0 = p.fill0, f1 = p.fill1;
        if (glyph) { if (!f0 && !f1) continue; }
        for (size_t i = 1; i < p.pts.size(); ++i) {
            const gfx::Point& a = p.pts[i - 1];
            const gfx::Point& b = p.pts[i];
            if (glyph) { edges[{0, 1}].push_back(f1 ? Seg{a, b} : Seg{b, a}); continue; }
            if (f1) edges[{p.styleSet, f1}].push_back({a, b});
            if (f0) edges[{p.styleSet, f0}].push_back({b, a});
        }
        if (!glyph && p.line > 0) {
            Stroke st;
            st.set = p.styleSet; st.style = p.line;
            float hw = 10.0f;
            if ((size_t)p.styleSet < s->lineSets.size() && (size_t)(p.line - 1) < s->lineSets[(size_t)p.styleSet].size())
                hw = std::max(10.0f, s->lineSets[(size_t)p.styleSet][(size_t)(p.line - 1)].width * 0.5f);
            for (size_t i = 1; i < p.pts.size(); ++i) {
                gfx::Point a = p.pts[i - 1], b = p.pts[i];
                float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
                if (l < 1e-3f) continue;
                float nx = -dy / l * hw, ny = dx / l * hw;
                float ex = dx / l * hw, ey = dy / l * hw;   // square-ish caps / joins overlap
                gfx::Point q0{a.x - ex + nx, a.y - ey + ny}, q1{a.x - ex - nx, a.y - ey - ny}, q2{b.x + ex - nx, b.y + ey - ny}, q3{b.x + ex + nx, b.y + ey + ny};
                float t[] = {q0.x, q0.y, q1.x, q1.y, q2.x, q2.y, q0.x, q0.y, q2.x, q2.y, q3.x, q3.y};
                st.tris.insert(st.tris.end(), t, t + 12);
            }
            if (!st.tris.empty()) c.strokes.push_back(std::move(st));
        }
    }
    for (auto& [k, segs] : edges) {
        if (segs.empty()) continue;
        Mesh m;
        m.set = k.first; m.style = k.second;
        gfx::Point o = segs[0].a;
        m.bx0 = m.bx1 = o.x; m.by0 = m.by1 = o.y;
        for (const Seg& sg : segs) {
            float t[] = {o.x, o.y, sg.a.x, sg.a.y, sg.b.x, sg.b.y};
            m.fan.insert(m.fan.end(), t, t + 6);
            m.bx0 = std::min({m.bx0, sg.a.x, sg.b.x}); m.bx1 = std::max({m.bx1, sg.a.x, sg.b.x});
            m.by0 = std::min({m.by0, sg.a.y, sg.b.y}); m.by1 = std::max({m.by1, sg.a.y, sg.b.y});
        }
        c.fills.push_back(std::move(m));
    }
    return shapes_[key] = std::move(c);
}

unsigned GfxRendererGL::texture(const std::string& path, int& w, int& h) {
    auto it = textures_.find(path);
    if (it != textures_.end()) { w = it->second.w; h = it->second.h; return it->second.id; }
    Tex t;
    render::ImageData img;
    if (!path.empty() && platform::decodeImage(path, img) && img.w > 0) {
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        glx::GenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        t.w = img.w; t.h = img.h;
    } else {
        LOG_WARN("GFX texture missing: %s", path.c_str());
    }
    textures_[path] = t;
    w = t.w; h = t.h;
    return t.id;
}

unsigned GfxRendererGL::gradientTexture(const gfx::FillStyle& fs) {
    std::string key;
    key += (char)fs.spread;
    for (const gfx::GradStop& g : fs.grad) { char b[16]; std::snprintf(b, sizeof b, "%02x%02x%02x%02x%02x", g.ratio, g.color.r, g.color.g, g.color.b, g.color.a); key += b; }
    auto it = gradients_.find(key);
    if (it != gradients_.end()) return it->second;
    uint8_t px[256 * 4];
    for (int i = 0; i < 256; ++i) {
        const gfx::GradStop* a = &fs.grad.front();
        const gfx::GradStop* b = &fs.grad.back();
        for (size_t k = 0; k + 1 < fs.grad.size(); ++k)
            if (i >= fs.grad[k].ratio && i <= fs.grad[k + 1].ratio) { a = &fs.grad[k]; b = &fs.grad[k + 1]; break; }
        float t = b->ratio == a->ratio ? 0.0f : std::max(0.0f, std::min(1.0f, (float)(i - a->ratio) / (float)(b->ratio - a->ratio)));
        if (i < fs.grad.front().ratio) t = 0, b = a;
        px[i * 4 + 0] = (uint8_t)(a->color.r + (b->color.r - a->color.r) * t);
        px[i * 4 + 1] = (uint8_t)(a->color.g + (b->color.g - a->color.g) * t);
        px[i * 4 + 2] = (uint8_t)(a->color.b + (b->color.b - a->color.b) * t);
        px[i * 4 + 3] = (uint8_t)(a->color.a + (b->color.a - a->color.a) * t);
    }
    unsigned id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint wrap = fs.spread == 1 ? GL_MIRRORED_REPEAT : fs.spread == 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gradients_[key] = id;
    return id;
}

void GfxRendererGL::setFill(const gfx::FillStyle& fs, const gfx::Matrix& world, const gfx::CXForm& cx, float alpha, int forceMode) {
    glx::Uniform3f(uWorld_, world.a, world.c, world.tx);
    GLint w1 = glx::GetUniformLocation(prog_, "uW1");
    glx::Uniform3f(w1, world.b, world.d, world.ty);
    gfx::Matrix inv = fs.m.inverse();
    glx::Uniform3f(uFillInv_, inv.a, inv.c, inv.tx);
    glx::Uniform3f(glx::GetUniformLocation(prog_, "uF1"), inv.b, inv.d, inv.ty);
    glx::Uniform4f(uMul_, cx.mr, cx.mg, cx.mb, cx.ma * alpha);
    glx::Uniform4f(uAdd_, cx.ar / 255.0f, cx.ag / 255.0f, cx.ab / 255.0f, cx.aa / 255.0f * alpha);
    int mode = 0;
    if (forceMode >= 0) mode = forceMode;
    else if (fs.type == gfx::FillStyle::Linear) mode = 1;
    else if (fs.type == gfx::FillStyle::Radial) mode = 2;
    else if (fs.type == gfx::FillStyle::Focal) mode = 3;
    else if (fs.isBitmap()) mode = 4;
    glx::Uniform1i(uMode_, mode);
    glx::Uniform4f(uColor_, fs.color.r / 255.0f, fs.color.g / 255.0f, fs.color.b / 255.0f, fs.color.a / 255.0f);
    if (mode >= 1 && mode <= 3 && !fs.grad.empty()) {
        glBindTexture(GL_TEXTURE_2D, gradientTexture(fs));
        glx::Uniform1f(uFocal_, fs.focal);
    }
}

void GfxRendererGL::drawTriangles(const std::vector<float>& v, const gfx::Matrix& m) {
    (void)m;
    if (v.empty()) return;
    glx::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glx::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 2));
}

void GfxRendererGL::stencilWinding(const std::vector<float>& fan, const gfx::Matrix& m) {
    // Winding count in the low nibble, only where the mask level (high nibble) equals the current level.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_EQUAL, level_ << 4, 0xF0);
    glStencilMask(0x0F);
    glx::StencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    glx::StencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
    drawTriangles(fan, m);
}

void GfxRendererGL::cover(float x0, float y0, float x1, float y1, const gfx::Matrix& m, bool mask) {
    float q[] = {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    std::vector<float> v(q, q + 12);
    if (mask) {
        // Pixels with a nonzero winding become mask level + 1 (low nibble cleared).
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_NOTEQUAL, (level_ + 1) << 4, 0x0F);
        glStencilMask(0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    } else {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_NOTEQUAL, level_ << 4, 0x0F);
        glStencilMask(0x0F);
        glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
    }
    drawTriangles(v, m);
}

void GfxRendererGL::drawVideo(const uint8_t* rgba, int w, int h, uint64_t serial) {
    if (!ok_ || !rgba || w <= 0 || h <= 0) return;
    if (!video_.id || video_.w != w || video_.h != h) {
        if (!video_.id) glGenTextures(1, &video_.id);
        glBindTexture(GL_TEXTURE_2D, video_.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        video_.w = w; video_.h = h;
        videoSerial_ = serial;
    } else if (serial != videoSerial_) {
        glBindTexture(GL_TEXTURE_2D, video_.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        videoSerial_ = serial;
    }
    // Uniform scale, centred (the movies are 16:9 like the GFx stages).
    float s = std::min((float)w_ / w, (float)h_ / h);
    float dw = w * s, dh = h * s, ox = (w_ - dw) * 0.5f, oy = (h_ - dh) * 0.5f;
    gfx::FillStyle fs;
    fs.m.a = s; fs.m.d = s; fs.m.tx = ox; fs.m.ty = oy;
    gfx::Matrix id;
    id.a = id.d = 1;
    setFill(fs, id, gfx::CXForm{}, 1.0f, 4);
    glx::Uniform2f(uTexSize_, (float)w, (float)h);
    glBindTexture(GL_TEXTURE_2D, video_.id);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilMask(0x00);
    float q[] = {ox, oy, ox + dw, oy, ox + dw, oy + dh, ox, oy, ox + dw, oy + dh, ox, oy + dh};
    std::vector<float> v(q, q + 12);
    drawTriangles(v, id);
}

void GfxRendererGL::fullscreen() {
    gfx::FillStyle none;
    gfx::Matrix id;
    id.a = id.d = 1;
    setFill(none, id, gfx::CXForm{}, 1.0f, 0);
    float q[] = {0, 0, (float)w_, 0, (float)w_, (float)h_, 0, 0, (float)w_, (float)h_, 0, (float)h_};
    std::vector<float> v(q, q + 12);
    drawTriangles(v, id);
}

void GfxRendererGL::draw(const std::vector<gfx::Player::RenderItem>& items, float alpha) {
    if (!ok_) return;
    using RI = gfx::Player::RenderItem;
    for (const RI& it : items) {
        switch (it.type) {
        case RI::MaskBegin: inMask_ = true; continue;
        case RI::MaskEnd: inMask_ = false; if (level_ < 15) ++level_; continue;
        case RI::MaskPop: {
            if (level_ <= 0) continue;
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glStencilMask(0xFF);
            glStencilFunc(GL_EQUAL, level_ << 4, 0xF0);
            glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
            fullscreen();
            --level_;
            glStencilFunc(GL_NOTEQUAL, level_ << 4, 0x0F);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            fullscreen();
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            continue;
        }
        default: break;
        }
        if (it.type == RI::Bitmap || it.type == RI::Image) {
            const gfx::BitmapInstance* b = it.bitmap;
            int tw = 0, th = 0;
            unsigned tex = texture(b ? b->path : it.imagePath, tw, th);
            if (!tex) continue;
            float w = b ? (float)(b->width > 0 ? b->width : tw) : it.imgW, h = b ? (float)(b->height > 0 ? b->height : th) : it.imgH;
            gfx::FillStyle fs;
            fs.type = gfx::FillStyle::BitmapClip;
            fs.m = gfx::Matrix{20, 0, 0, 20, 0, 0};
            setFill(fs, it.m, it.cx, alpha);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glx::Uniform2f(uTexSize_, w, h);
            float x1 = w * 20, y1 = h * 20;
            float q[] = {0, 0, x1, 0, x1, y1, 0, 0, x1, y1, 0, y1};
            std::vector<float> fan(q, q + 12);
            if (inMask_) { stencilWinding(fan, it.m); cover(0, 0, x1, y1, it.m, true); }
            else {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilFunc(GL_EQUAL, level_ << 4, 0xF0);
                glStencilMask(0);
                drawTriangles(fan, it.m);
            }
            continue;
        }
        if (!it.shape) continue;
        bool glyph = it.type == RI::Glyph;
        const Cached& c = cache(it.shape, glyph);
        for (const Mesh& m : c.fills) {
            gfx::FillStyle fs;
            if (glyph) { fs.color = it.glyphColor; }
            else {
                if ((size_t)m.set >= it.shape->fillSets.size() || (size_t)(m.style - 1) >= it.shape->fillSets[(size_t)m.set].size()) continue;
                fs = it.shape->fillSets[(size_t)m.set][(size_t)(m.style - 1)];
            }
            setFill(fs, it.m, it.cx, alpha);
            if (fs.isBitmap()) {
                std::shared_ptr<const gfx::MovieDef> d;
                int tw = 0, th = 0;
                unsigned tex = 0;
                const gfx::DisplayObject* own = it.owner;
                if (own && own->def) {
                    const gfx::CharDef* cd = own->def->character(fs.bitmapId);
                    if (cd && cd->type == gfx::CharType::Bitmap) {
                        const gfx::BitmapDef& bd = own->def->bitmaps[(size_t)cd->index];
                        std::string path = own->player && own->player->externalTexture ? own->player->externalTexture(bd.exportName) : "";
                        tex = texture(path.empty() ? bd.resolvedPath : path, tw, th);
                        if (bd.targetWidth > 0) { tw = bd.targetWidth; th = bd.targetHeight; }
                    }
                }
                if (!tex) continue;
                glBindTexture(GL_TEXTURE_2D, tex);
                bool rep = fs.type == gfx::FillStyle::BitmapRepeat || fs.type == gfx::FillStyle::BitmapRepeatHard;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, rep ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, rep ? GL_REPEAT : GL_CLAMP_TO_EDGE);
                glx::Uniform2f(uTexSize_, (float)tw, (float)th);
            }
            stencilWinding(m.fan, it.m);
            cover(m.bx0, m.by0, m.bx1, m.by1, it.m, inMask_);
        }
        if (!inMask_) {
            for (const Stroke& s : c.strokes) {
                if ((size_t)s.set >= it.shape->lineSets.size() || (size_t)(s.style - 1) >= it.shape->lineSets[(size_t)s.set].size()) continue;
                gfx::FillStyle fs;
                fs.color = it.shape->lineSets[(size_t)s.set][(size_t)(s.style - 1)].color;
                setFill(fs, it.m, it.cx, alpha);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilFunc(GL_EQUAL, level_ << 4, 0xF0);
                glStencilMask(0);
                drawTriangles(s.tris, it.m);
            }
        }
    }
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

} // namespace ui
