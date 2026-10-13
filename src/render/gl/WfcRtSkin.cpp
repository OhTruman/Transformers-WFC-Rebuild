// Clean-room reconstruction — M7 RT characters, the GL side (PC ADAPTATION; OFF by default; Systems). WFC_RT_CHARS=1 (with
// the round trip on): every GPU-skinned instance drawSkinned prepares is noted (bone-texture row, palette serial, blend,
// model matrix, model-space bounds); just before the round trip's GL signal, rtSkinFeed selects the instances the RT
// passes need and skins them with a compute pass that mirrors the vertex shader's wfcSkin (same bone texture rows, the
// uSkin 2 blend with the previous palette) into their regions of a buffer shared with D3D12, in model space. D3D12's job
// then refits their BLAS and the TLAS gets them as instances with the model matrix as the transform
// (render/d3d12/D3D12RtDynamic.h).
// Selection (the plan's refit culling): a character is in the TLAS when its world box is on screen or within
// WFC_RT_CHARRANGE metres of the camera (default 30: the sun-shadow reach); beyond WFC_RT_CHARFAR (default 20) a changed
// pose is re-skinned / refit only every second frame (alternating by slot), and an unchanged pose (same palette serial,
// blend and alpha) is never redone. Instances not drawn for 300 frames give their slot back.
// WFC_RT_CHARCHECK=1: every 300 feeds one updated instance is read back and compared with a CPU evaluation of the same
// bone-texture palettes (and, unblended, with drawSkinned's exact skinPose bounds); D3D12 hashes the same region as it
// reads it (rtDynamicCheck).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "render/gl/WfcPipeline.h"
#include "render/d3d12/D3D12RtDynamic.h"
#include "render/d3d12/D3D12RtBridge.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace render {
namespace wfc {
using namespace glx;

namespace {
const char* kSkinCS = R"(#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer BindV { float bv[]; };   // the bind vertices: 14 floats, position first
layout(std430, binding = 1) readonly buffer JW { vec4 jw[]; };       // per vertex: 4 joints (as float), 4 weights
layout(std430, binding = 2) writeonly buffer Out { float outp[]; };  // the shared buffer: float3 per vertex
uniform sampler2D uBoneTex;           // RGBA32F: row per instance; cur palette at x = 4j, prev at x = 512 + 4j
uniform int uSkin, uSkinRow, uSkinBones, uVerts, uFirst;
uniform float uSkinAlpha;
mat4 wfcBone(int base, int j) {
    return mat4(texelFetch(uBoneTex, ivec2(base + j * 4, uSkinRow), 0), texelFetch(uBoneTex, ivec2(base + j * 4 + 1, uSkinRow), 0),
                texelFetch(uBoneTex, ivec2(base + j * 4 + 2, uSkinRow), 0), texelFetch(uBoneTex, ivec2(base + j * 4 + 3, uSkinRow), 0));
}
vec3 wfcSkinPos(int base, vec3 aPos, vec4 aJoints, vec4 aWeights) {   // the vertex shader's wfcSkin, positions only
    vec3 p = vec3(0.0);
    for (int k = 0; k < 4; ++k) {
        float w = aWeights[k];
        int j = int(aJoints[k] + 0.5);
        if (w <= 0.0 || j >= uSkinBones) continue;
        p += (wfcBone(base, j) * vec4(aPos, 1.0)).xyz * w;
    }
    return p;
}
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= uVerts) return;
    vec3 aPos = vec3(bv[i * 14], bv[i * 14 + 1], bv[i * 14 + 2]);
    vec4 aJoints = jw[i * 2], aWeights = jw[i * 2 + 1];
    vec3 p = wfcSkinPos(0, aPos, aJoints, aWeights);
    if (uSkin == 2) { vec3 pp = wfcSkinPos(512, aPos, aJoints, aWeights); p = pp + (p - pp) * uSkinAlpha; }
    int o = (uFirst + i) * 3;
    outp[o] = p.x; outp[o + 1] = p.y; outp[o + 2] = p.z;
}
)";

bool on() { static const bool v = std::getenv("WFC_RT_CHARS") != nullptr; return v; }
float envF(const char* n, float d) { const char* e = std::getenv(n); return e ? (float)std::atof(e) : d; }

typedef uint64_t GLuint64_;
typedef void(APIENTRY* PFN_CreateMemoryObjectsEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_ImportMemoryWin32HandleEXT)(GLuint, GLuint64_, GLenum, void*);
typedef void(APIENTRY* PFN_MemoryObjectParameterivEXT)(GLuint, GLenum, const GLint*);
typedef void(APIENTRY* PFN_BufferStorageMemEXT)(GLenum, GLsizeiptr, GLuint, GLuint64_);
constexpr GLenum kSSBO = 0x90D2, kHandleD3D12Resource = 0x958A, kDedicated = 0x9581;

struct GlModel { int id = -1; const float* posData = nullptr; size_t verts = 0, idx = 0; GLuint vbo = 0; };
struct GlInst { int slot = -1, model = -1, row = -1, mode = 0, lastSeen = 0; float alpha = -1.0f; uint64_t serial = 0; bool updated = false; };
struct State {
    bool failed = false;
    uint32_t epoch = 0;
    GLuint prog = 0, mem = 0, buf = 0;
    uint32_t capacity = 0;
    GLint uSkin = -1, uRow = -1, uBones = -1, uVerts = -1, uFirst = -1, uAlpha = -1, uTex = -1;
    std::map<const void*, GlModel> models;                // key: the bind mesh (skinModels_'s key)
    std::map<const void*, GlInst> insts;                  // key: drawSkinned's instance key
    GLuint q[3][2] = {}; bool qUsed[3] = {}; int qi = 0;  // GL timestamps around the dispatches (ring of 3)
    long feeds = 0, recs = 0, inTlas = 0, skinned = 0, farSkipped = 0, unchanged = 0, full = 0, vertsSkinned = 0;
    double glMs = 0; int glSamples = 0;
} S;

GLuint linkCS(const char* src) {
    GLuint sh = CreateShader(0x91B9 /*GL_COMPUTE_SHADER*/);
    ShaderSource(sh, 1, &src, nullptr);
    CompileShader(sh);
    GLint ok = 0; GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048] = {}; GetShaderInfoLog(sh, sizeof log, nullptr, log); LOG_WARN("RT characters: skin.cs compile failed: %s", log); DeleteShader(sh); return 0; }
    GLuint p = CreateProgram(); AttachShader(p, sh); LinkProgram(p); DeleteShader(sh);
    GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048] = {}; GetProgramInfoLog(p, sizeof log, nullptr, log); LOG_WARN("RT characters: skin.cs link failed: %s", log); DeleteProgram(p); return 0; }
    return p;
}
// the shared buffer, imported once (it lives until shutdown)
bool importBuffer() {
    if (S.buf) return true;
    RtDynamicBuffer b;
    if (!rtDynamicBuffer(&b)) return false;
    auto g = [](const char* n) { return (void*)wglGetProcAddress(n); };
    auto createMem = (PFN_CreateMemoryObjectsEXT)g("glCreateMemoryObjectsEXT");
    auto importMem = (PFN_ImportMemoryWin32HandleEXT)g("glImportMemoryWin32HandleEXT");
    auto memParam = (PFN_MemoryObjectParameterivEXT)g("glMemoryObjectParameterivEXT");
    auto storageMem = (PFN_BufferStorageMemEXT)g("glBufferStorageMemEXT");
    if (!createMem || !importMem || !storageMem) { LOG_WARN("RT characters: glBufferStorageMemEXT missing"); return false; }
    while (glGetError() != GL_NO_ERROR) {}
    createMem(1, &S.mem);
    const GLint ded = 1; if (memParam) memParam(S.mem, kDedicated, &ded);
    importMem(S.mem, b.bytes, kHandleD3D12Resource, b.handle);
    GenBuffers(1, &S.buf);
    BindBuffer(kSSBO, S.buf);
    storageMem(kSSBO, (GLsizeiptr)b.capacity * 12, S.mem, 0);
    BindBuffer(kSSBO, 0);
    if (glGetError() != GL_NO_ERROR) { LOG_WARN("RT characters: importing the shared positions buffer failed"); return false; }
    S.capacity = b.capacity;
    return true;
}
// a new clear (map unload) discarded D3D12's models / slots: forget ours
void syncEpoch() {
    const uint32_t e = rtDynamicEpoch();
    if (e != S.epoch) { S.models.clear(); S.insts.clear(); S.epoch = e; }
}
uint64_t fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t k = 0; k < n; ++k) { h ^= ((const unsigned char*)p)[k]; h *= 1099511628211ull; }
    return h;
}
}  // namespace

void Pipeline::rtSkinNote(const MeshData& bind, const SkinModel& sm, const void* key, uint64_t serial, const core::Mat4& model,
                          const core::Vec3& mn, const core::Vec3& mx) {
    if (!on()) return;
    if (rtSkinFrame_ != frameNo_) {                       // the first note of a frame: the previous frame's list moves back
        rtSkinPrevFrame_ = rtSkinFrame_; rtSkinPrev_.swap(rtSkinCur_); rtSkinCur_.clear(); rtSkinIdx_.clear();
        rtSkinFrame_ = frameNo_;
    }
    // the model's triangle list goes to D3D12 while the bind mesh is certainly alive (here)
    if (rtDynamicAvailable()) {
        syncEpoch();
        GlModel& m = S.models[&bind];
        if (m.id < 0 || m.posData != sm.posData || m.verts != sm.verts || m.idx != sm.idx || m.vbo != sm.vbo) {
            if (m.id >= 0) {
                for (auto& it : S.insts) if (it.second.model == m.id) { rtDynamicReleaseSlot(it.second.slot); it.second = GlInst{}; }
                rtDynamicReleaseModel(m.id);
            }
            m = GlModel{};
            if (bind.indices.size() == sm.idx && !bind.indices.empty())
                m.id = rtDynamicModel(bind.indices.data(), (uint32_t)bind.indices.size(), (uint32_t)sm.verts);
            m.posData = sm.posData; m.verts = sm.verts; m.idx = sm.idx; m.vbo = sm.vbo;
        }
    }
    RtSkinRec r;
    r.key = key; r.bindKey = &bind; r.row = skinRow_; r.mode = skinMode_; r.bones = skinBones_; r.alpha = skinAlpha_;
    r.serial = serial; r.model = model; r.mn = mn; r.mx = mx;
    auto it = rtSkinIdx_.find(key);                       // (an instance drawn again this frame: the last call wins)
    if (it != rtSkinIdx_.end()) rtSkinCur_[it->second] = r;
    else { rtSkinIdx_[key] = rtSkinCur_.size(); rtSkinCur_.push_back(r); }
}

void Pipeline::rtSkinFeed(bool afterCharacters) {
    if (!on() || S.failed || !rtDynamicAvailable()) return;
    syncEpoch();
    if (!S.prog) {
        S.prog = linkCS(kSkinCS);
        if (!S.prog || !importBuffer()) { S.failed = true; LOG_WARN("RT characters: off (no compute skinning / shared buffer)"); return; }
        S.uSkin = GetUniformLocation(S.prog, "uSkin"); S.uRow = GetUniformLocation(S.prog, "uSkinRow");
        S.uBones = GetUniformLocation(S.prog, "uSkinBones"); S.uVerts = GetUniformLocation(S.prog, "uVerts");
        S.uFirst = GetUniformLocation(S.prog, "uFirst"); S.uAlpha = GetUniformLocation(S.prog, "uSkinAlpha");
        S.uTex = GetUniformLocation(S.prog, "uBoneTex");
        static const float range = envF("WFC_RT_CHARRANGE", 30.0f), farD = envF("WFC_RT_CHARFAR", 20.0f);
        LOG_INFO("RT characters: on (%s; in the TLAS on screen or within %.0f m, refit every second frame beyond %.0f m; %u shared vertices)",
                 afterCharacters ? "this frame's poses" : "last frame's poses", range, farD, S.capacity);
    }
    static const float range = envF("WFC_RT_CHARRANGE", 30.0f), farD = envF("WFC_RT_CHARFAR", 20.0f);
    static const bool check = std::getenv("WFC_RT_CHARCHECK") != nullptr;
    // the instances whose palettes the bone texture holds now
    const std::vector<RtSkinRec>* recs = nullptr;
    if (afterCharacters) { if (rtSkinFrame_ == frameNo_) recs = &rtSkinCur_; flushSkinUploads(); }
    else if (rtSkinFrame_ == frameNo_ - 1) recs = &rtSkinCur_;          // (no skinned draw yet this frame)
    else if (rtSkinFrame_ == frameNo_ && rtSkinPrevFrame_ == frameNo_ - 1) recs = &rtSkinPrev_;
    std::vector<RtDynamicInstance> out;
    struct Job { const RtSkinRec* r; const SkinModel* sm; uint32_t first; };
    std::vector<Job> jobs;
    if (recs) {
        out.reserve(recs->size());
        for (const RtSkinRec& r : *recs) {
            ++S.recs;
            auto smIt = skinModels_.find(r.bindKey);
            auto gmIt = S.models.find(r.bindKey);
            if (smIt == skinModels_.end() || gmIt == S.models.end() || gmIt->second.id < 0 || gmIt->second.vbo != smIt->second.vbo) continue;
            const SkinModel& sm = smIt->second;
            // the world box: on screen (all corners outside one clip plane: off), the distance from the camera to it
            core::Vec3 wmn{1e30f, 1e30f, 1e30f}, wmx{-1e30f, -1e30f, -1e30f};
            unsigned outAll = 0x3F;
            for (int c = 0; c < 8; ++c) {
                const core::Vec3 p{(c & 1) ? r.mx.x : r.mn.x, (c & 2) ? r.mx.y : r.mn.y, (c & 4) ? r.mx.z : r.mn.z};
                const core::Vec3 w = core::transformPoint(r.model, p);
                wmn = {std::min(wmn.x, w.x), std::min(wmn.y, w.y), std::min(wmn.z, w.z)};
                wmx = {std::max(wmx.x, w.x), std::max(wmx.y, w.y), std::max(wmx.z, w.z)};
                const float* m = viewProj_.m;
                const float cx = m[0] * w.x + m[4] * w.y + m[8] * w.z + m[12], cy = m[1] * w.x + m[5] * w.y + m[9] * w.z + m[13];
                const float cz = m[2] * w.x + m[6] * w.y + m[10] * w.z + m[14], cw = m[3] * w.x + m[7] * w.y + m[11] * w.z + m[15];
                outAll &= (cx < -cw ? 1u : 0u) | (cx > cw ? 2u : 0u) | (cy < -cw ? 4u : 0u) | (cy > cw ? 8u : 0u) |
                          (cz < -cw ? 16u : 0u) | (cz > cw ? 32u : 0u);
            }
            const core::Vec3 q{std::clamp(camPos_.x, wmn.x, wmx.x), std::clamp(camPos_.y, wmn.y, wmx.y), std::clamp(camPos_.z, wmn.z, wmx.z)};
            const float dist = core::length(q - camPos_);
            if (outAll && dist > range) continue;                     // neither visible nor within shadow reach
            GlInst& gi = S.insts[r.key];
            gi.lastSeen = frameNo_;
            if (gi.slot >= 0 && gi.model != gmIt->second.id) { rtDynamicReleaseSlot(gi.slot); gi = GlInst{}; gi.lastSeen = frameNo_; }
            if (gi.slot < 0) {
                gi.slot = rtDynamicSlot(gmIt->second.id); gi.model = gmIt->second.id;
                if (gi.slot < 0) { ++S.full; gi = GlInst{}; continue; }
            }
            const bool changed = !gi.updated || gi.serial != r.serial || r.serial == 0 || gi.mode != r.mode || gi.alpha != r.alpha || gi.row != r.row;
            bool update = changed;
            if (changed && gi.updated && dist > farD && ((frameNo_ + gi.slot) & 1)) { update = false; ++S.farSkipped; }
            if (!changed) ++S.unchanged;
            RtDynamicInstance di;
            di.slot = gi.slot; di.update = update;
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c) di.transform[row * 4 + c] = r.model.m[c * 4 + row];
            out.push_back(di);
            if (update) {
                jobs.push_back({&r, &sm, rtDynamicFirstVertex(gi.slot)});
                gi.updated = true; gi.serial = r.serial; gi.mode = r.mode; gi.alpha = r.alpha; gi.row = r.row;
            }
        }
    }
    // the compute pass (state saved / restored around it; the bone texture is on its own unit 18)
    if (!jobs.empty()) {
        const int qs = S.qi;
        if (S.qUsed[qs]) {                                    // (this ring slot's last pair: three feeds ago)
            GLint rdy = 0; GetQueryObjectiv(S.q[qs][1], 0x8867 /*GL_QUERY_RESULT_AVAILABLE*/, &rdy);
            if (rdy) {
                unsigned long long t0 = 0, t1 = 0;
                GetQueryObjectui64v(S.q[qs][0], 0x8866, &t0); GetQueryObjectui64v(S.q[qs][1], 0x8866, &t1);
                if (t1 > t0) { S.glMs += (double)(t1 - t0) * 1e-6; ++S.glSamples; }
            }
            S.qUsed[qs] = false;
        }
        if (!S.q[0][0]) for (auto& p : S.q) GenQueries(2, p);
        QueryCounter(S.q[qs][0], 0x8E28 /*GL_TIMESTAMP*/);
        GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        GLint unit = GL_TEXTURE0; glGetIntegerv(0x84E0 /*GL_ACTIVE_TEXTURE*/, &unit);
        ActiveTexture(GL_TEXTURE0 + 18); glBindTexture(GL_TEXTURE_2D, skinTex_); ActiveTexture((GLenum)unit);
        UseProgram(S.prog);
        Uniform1i(S.uTex, 18);
        BindBufferBase(kSSBO, 2, S.buf);
        for (const Job& j : jobs) {
            Uniform1i(S.uSkin, j.r->mode); Uniform1i(S.uRow, j.r->row); Uniform1i(S.uBones, j.r->bones);
            Uniform1f(S.uAlpha, j.r->alpha); Uniform1i(S.uVerts, (GLint)j.sm->verts); Uniform1i(S.uFirst, (GLint)j.first);
            BindBufferBase(kSSBO, 0, j.sm->vbo); BindBufferBase(kSSBO, 1, j.sm->jwVbo);
            DispatchCompute((GLuint)((j.sm->verts + 63) / 64), 1, 1);
            S.vertsSkinned += (long)j.sm->verts;
        }
        for (GLuint k = 0; k < 3; ++k) BindBufferBase(kSSBO, k, 0);
        MemoryBarrier(0x2000 /*GL_SHADER_STORAGE_BARRIER_BIT*/ | 0x200 /*GL_BUFFER_UPDATE_BARRIER_BIT*/);
        UseProgram((GLuint)prevProg);
        QueryCounter(S.q[qs][1], 0x8E28);
        S.qUsed[qs] = true; S.qi = (S.qi + 1) % 3;
        S.skinned += (long)jobs.size();
        // (WFC_RT_CHARCHECK) one instance per 300 feeds: GL's result vs the CPU on the same palettes; D3D12 hashes it too
        if (check && S.feeds % 300 == 0) {
            const Job& j = jobs.front();
            const size_t n = j.sm->verts;
            std::vector<float> gpu(n * 3), bv(n * 14), jw(n * 8);
            BindBuffer(kSSBO, S.buf); GetBufferSubData(kSSBO, (GLintptr)j.first * 12, (GLsizeiptr)(n * 12), gpu.data());
            BindBuffer(kSSBO, j.sm->vbo); GetBufferSubData(kSSBO, 0, (GLsizeiptr)(n * 14 * 4), bv.data());
            BindBuffer(kSSBO, j.sm->jwVbo); GetBufferSubData(kSSBO, 0, (GLsizeiptr)(n * 8 * 4), jw.data());
            BindBuffer(kSSBO, 0);
            const int slot = S.insts[j.r->key].slot;
            rtDynamicCheck(slot, (uint32_t)n, fnv(gpu.data(), n * 12));
            double maxErr = -1.0, scale = 0.0;
            if (!skinMirror_.empty()) {                         // (the batched upload path: the mirror holds the rows)
                maxErr = 0.0;
                const float* row = &skinMirror_[(size_t)j.r->row * 1024 * 4];
                auto skin = [&](int base, size_t i) {
                    core::Vec3 s{0, 0, 0};
                    const core::Vec3 p{bv[i * 14], bv[i * 14 + 1], bv[i * 14 + 2]};
                    for (int k = 0; k < 4; ++k) {
                        const float w = jw[i * 8 + 4 + k];
                        const int jj = (int)(jw[i * 8 + k] + 0.5f);
                        if (w <= 0.0f || jj >= j.r->bones) continue;
                        core::Mat4 M; std::memcpy(M.m, row + (size_t)(base + jj * 4) * 4, sizeof M.m);
                        s += core::transformPoint(M, p) * w;
                    }
                    return s;
                };
                for (size_t i = 0; i < n; ++i) {
                    core::Vec3 c = skin(0, i);
                    if (j.r->mode == 2) { const core::Vec3 pp = skin(512, i); c = pp + (c - pp) * j.r->alpha; }
                    maxErr = std::max({maxErr, (double)std::fabs(c.x - gpu[i * 3]), (double)std::fabs(c.y - gpu[i * 3 + 1]), (double)std::fabs(c.z - gpu[i * 3 + 2])});
                    scale = std::max({scale, (double)std::fabs(c.x), (double)std::fabs(c.y), (double)std::fabs(c.z)});
                }
            }
            core::Vec3 gmn{1e30f, 1e30f, 1e30f}, gmx{-1e30f, -1e30f, -1e30f};
            for (size_t i = 0; i < n; ++i) {
                gmn = {std::min(gmn.x, gpu[i * 3]), std::min(gmn.y, gpu[i * 3 + 1]), std::min(gmn.z, gpu[i * 3 + 2])};
                gmx = {std::max(gmx.x, gpu[i * 3]), std::max(gmx.y, gpu[i * 3 + 1]), std::max(gmx.z, gpu[i * 3 + 2])};
            }
            const double bErr = std::max({std::fabs(gmn.x - j.r->mn.x), std::fabs(gmn.y - j.r->mn.y), std::fabs(gmn.z - j.r->mn.z),
                                          std::fabs(gmx.x - j.r->mx.x), std::fabs(gmx.y - j.r->mx.y), std::fabs(gmx.z - j.r->mx.z)});
            LOG_INFO("RT characters check: slot %d, %zu vertices, mode %d (alpha %.3f): max |GPU - CPU| %.3g (coordinates up to %.3g), "
                     "box vs drawSkinned's skinPose bounds %.3g%s", slot, n, j.r->mode, j.r->alpha, maxErr, scale, bErr,
                     j.r->mode == 2 ? " (blended: drawSkinned's box is interpolated, not exact)" : "");
        }
    }
    rtDynamicFrame(out.data(), (int)out.size());
    if (S.buf) rtBridgeSetSignalBuffers(&S.buf, 1);
    S.inTlas += (long)out.size();
    // slots of instances not drawn for 300 frames go back
    for (auto it = S.insts.begin(); it != S.insts.end();)
        if (frameNo_ - it->second.lastSeen > 300) { rtDynamicReleaseSlot(it->second.slot); it = S.insts.erase(it); }
        else ++it;
    if (++S.feeds % 300 == 0) {
        const RtDynamicStats d = rtDynamicTakeStats();
        const double f = 300.0;
        LOG_INFO("RT characters (per frame over 300): noted %.1f, in TLAS %.1f, skinned %.1f (%.0f vertices; far-alternate skipped %.1f, "
                 "unchanged %.1f), slot failures %ld | GL skin %.3f ms GPU | D3D12: in TLAS %.1f, refits %.1f, builds %ld, %.3f ms GPU | "
                 "%u slots, %u / %u shared vertices, BLAS %.1f MB",
                 S.recs / f, S.inTlas / f, S.skinned / f, S.vertsSkinned / f, S.farSkipped / f, S.unchanged / f, S.full,
                 S.glSamples ? S.glMs / S.glSamples : 0.0, d.records ? (double)d.instances / d.records : 0.0,
                 d.records ? (double)d.refits / d.records : 0.0, d.builds, d.gpuSamples ? d.gpuMs / d.gpuSamples : 0.0,
                 d.slots, d.verticesUsed, d.capacity, d.blasMB);
        S.recs = S.inTlas = S.skinned = S.farSkipped = S.unchanged = S.full = S.vertsSkinned = 0; S.glMs = 0; S.glSamples = 0;
    }
}

}  // namespace wfc
}  // namespace render
