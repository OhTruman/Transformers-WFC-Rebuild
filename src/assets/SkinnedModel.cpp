#include "assets/SkinnedModel.h"
#include "assets/Json.h"
#include "assets/Gltf.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace assets {
namespace {

// ---- GLB container ----
struct Glb {
    std::vector<uint8_t> file;
    std::vector<uint8_t> ext;     // .gltf: external buffers[0]
    const uint8_t* json = nullptr; size_t jsonLen = 0;
    const uint8_t* bin = nullptr;  size_t binLen = 0;
};
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool openGlb(const std::string& path, Glb& g) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff n = f.tellg();
    if (n <= 12) return false;
    g.file.resize((size_t)n); f.seekg(0); f.read((char*)g.file.data(), n);
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".gltf") == 0) {   // JSON text + external buffer
        g.json = g.file.data(); g.jsonLen = g.file.size();
        Json probe;
        if (!Json::parse((const char*)g.json, g.jsonLen, probe)) return false;
        std::string uri = probe["buffers"][0]["uri"].asString();
        size_t s = path.find_last_of("/\\");
        std::string dir = s == std::string::npos ? "." : path.substr(0, s);
        std::ifstream b(dir + "/" + uri, std::ios::binary | std::ios::ate);
        if (!b) return false;
        std::streamoff bn = b.tellg();
        g.ext.resize((size_t)bn); b.seekg(0); b.read((char*)g.ext.data(), bn);
        g.bin = g.ext.data(); g.binLen = g.ext.size();
        return true;
    }
    const uint8_t* d = g.file.data();
    if (rd32(d) != 0x46546C67u) return false;
    size_t total = std::min<size_t>(rd32(d + 8), g.file.size()), off = 12;
    while (off + 8 <= total) {
        uint32_t clen = rd32(d + off), ctype = rd32(d + off + 4);
        const uint8_t* cd = d + off + 8;
        if (off + 8 + clen > total) break;
        if (ctype == 0x4E4F534Au) { g.json = cd; g.jsonLen = clen; }
        else if (ctype == 0x004E4942u) { g.bin = cd; g.binLen = clen; }
        off += 8 + clen; off = (off + 3) & ~size_t(3);
    }
    return g.json != nullptr;
}

int compBytes(int ct) { return ct == 5120 || ct == 5121 ? 1 : (ct == 5122 || ct == 5123 ? 2 : 4); }
int typeComps(const std::string& t) {
    if (t == "SCALAR") return 1; if (t == "VEC2") return 2; if (t == "VEC3") return 3;
    if (t == "VEC4") return 4; if (t == "MAT4") return 16; return 0;
}

struct Doc {
    const Json* root = nullptr;
    const uint8_t* bin = nullptr; size_t binLen = 0;

    const uint8_t* accPtr(int acc, int& count, int& comps, int& ct, size_t& stride) const {
        const Json& a = (*root)["accessors"][(size_t)acc];
        int bv = a["bufferView"].asInt(-1);
        if (bv < 0) return nullptr;
        const Json& b = (*root)["bufferViews"][(size_t)bv];
        if (b["buffer"].asInt(0) != 0) return nullptr;
        size_t off = (size_t)b["byteOffset"].asInt(0) + (size_t)a["byteOffset"].asInt(0);
        count = a["count"].asInt(0);
        comps = typeComps(a["type"].asString());
        ct = a["componentType"].asInt(0);
        int elem = comps * compBytes(ct);
        stride = b.has("byteStride") ? (size_t)b["byteStride"].asInt(elem) : (size_t)elem;
        return (bin && off <= binLen) ? bin + off : nullptr;
    }

    // Read accessor as floats (count*comps). Integer types optionally normalized.
    std::vector<float> floats(int acc, bool normalize = false) const {
        std::vector<float> out;
        int count, comps, ct; size_t stride;
        const uint8_t* p = accPtr(acc, count, comps, ct, stride);
        if (!p) return out;
        out.resize((size_t)count * comps);
        for (int i = 0; i < count; ++i) {
            const uint8_t* e = p + (size_t)i * stride;
            for (int c = 0; c < comps; ++c) {
                float v = 0;
                switch (ct) {
                    case 5126: std::memcpy(&v, e + c * 4, 4); break;
                    case 5125: { uint32_t u; std::memcpy(&u, e + c * 4, 4); v = (float)u; } break;
                    case 5123: { uint16_t u; std::memcpy(&u, e + c * 2, 2); v = normalize ? u / 65535.0f : (float)u; } break;
                    case 5121: { uint8_t u = e[c]; v = normalize ? u / 255.0f : (float)u; } break;
                    case 5122: { int16_t u; std::memcpy(&u, e + c * 2, 2); v = normalize ? std::max(u / 32767.0f, -1.0f) : (float)u; } break;
                    case 5120: { int8_t u = (int8_t)e[c]; v = normalize ? std::max(u / 127.0f, -1.0f) : (float)u; } break;
                }
                out[(size_t)i * comps + c] = v;
            }
        }
        return out;
    }
    std::vector<uint16_t> joints(int acc) const {
        std::vector<uint16_t> out;
        int count, comps, ct; size_t stride;
        const uint8_t* p = accPtr(acc, count, comps, ct, stride);
        if (!p || comps != 4) return out;
        out.resize((size_t)count * 4);
        for (int i = 0; i < count; ++i) {
            const uint8_t* e = p + (size_t)i * stride;
            for (int c = 0; c < 4; ++c) {
                if (ct == 5123) { uint16_t u; std::memcpy(&u, e + c * 2, 2); out[(size_t)i * 4 + c] = u; }
                else { out[(size_t)i * 4 + c] = e[c]; }
            }
        }
        return out;
    }
    std::vector<uint32_t> indices(int acc) const {
        std::vector<uint32_t> out;
        int count, comps, ct; size_t stride;
        const uint8_t* p = accPtr(acc, count, comps, ct, stride);
        if (!p) return out;
        out.resize((size_t)count);
        for (int i = 0; i < count; ++i) {
            const uint8_t* e = p + (size_t)i * stride; uint32_t idx = 0;
            if (ct == 5125) std::memcpy(&idx, e, 4);
            else if (ct == 5123) { uint16_t s; std::memcpy(&s, e, 2); idx = s; }
            else idx = *e;
            out[(size_t)i] = idx;
        }
        return out;
    }
};

} // namespace

bool loadSkinnedGlb(const std::string& path, SkinnedModel& m) {
    // A load replaces the model. Loading into a used model appended children / roots (and kept stale clips): every
    // reload duplicated each edge, so poseGlobals' DFS walked k^depth paths (M29: WFC_RELOADTEST "hang" at reload 4-5,
    // main thread in poseGlobals).
    m = SkinnedModel();
    Glb g;
    if (!openGlb(path, g)) { LOG_ERROR("skinned glb: open failed %s", path.c_str()); return false; }
    Json root;
    if (!Json::parse((const char*)g.json, g.jsonLen, root)) { LOG_ERROR("skinned glb: json %s", path.c_str()); return false; }
    Doc doc; doc.root = &root; doc.bin = g.bin; doc.binLen = g.binLen;

    // ---- nodes ----
    const Json& nodes = root["nodes"];
    m.nodes.resize(nodes.size());
    m.nodeNames.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Json& n = nodes[i];
        Node& nd = m.nodes[i];
        m.nodeNames[i] = n["name"].asString();
        if (n.has("matrix") && n["matrix"].size() == 16) {
            // Decompose only translation (rotation/scale from matrix is uncommon for skeletons here).
            nd.t = {n["matrix"][12].asFloat(), n["matrix"][13].asFloat(), n["matrix"][14].asFloat()};
        } else {
            if (n.has("translation")) nd.t = {n["translation"][0].asFloat(), n["translation"][1].asFloat(), n["translation"][2].asFloat()};
            if (n.has("rotation")) nd.r = {n["rotation"][0].asFloat(), n["rotation"][1].asFloat(), n["rotation"][2].asFloat(), n["rotation"][3].asFloat(1.0f)};
            if (n.has("scale")) nd.s = {n["scale"][0].asFloat(1.0f), n["scale"][1].asFloat(1.0f), n["scale"][2].asFloat(1.0f)};
        }
        const Json& ch = n["children"];
        for (size_t c = 0; c < ch.size(); ++c) nd.children.push_back(ch[c].asInt(0));
    }
    for (size_t i = 0; i < m.nodes.size(); ++i)
        for (int c : m.nodes[i].children) if (c >= 0 && c < (int)m.nodes.size()) m.nodes[(size_t)c].parent = (int)i;

    const Json& scenes = root["scenes"];
    int si = root["scene"].asInt(0);
    if (scenes.size() > 0) { const Json& sc = scenes[(size_t)si]["nodes"]; for (size_t i = 0; i < sc.size(); ++i) m.roots.push_back(sc[i].asInt(0)); }
    else for (size_t i = 0; i < m.nodes.size(); ++i) if (m.nodes[i].parent < 0) m.roots.push_back((int)i);

    // ---- skin ----
    const Json& skins = root["skins"];
    if (skins.size() == 0) { LOG_ERROR("skinned glb: no skin %s", path.c_str()); return false; }
    const Json& skin = skins[0];
    const Json& sj = skin["joints"];
    for (size_t i = 0; i < sj.size(); ++i) m.skinJoints.push_back(sj[i].asInt(0));
    if (skin.has("inverseBindMatrices")) {
        std::vector<float> ibm = doc.floats(skin["inverseBindMatrices"].asInt(-1));
        size_t nj = ibm.size() / 16;
        m.invBind.resize(nj);
        for (size_t j = 0; j < nj; ++j) m.invBind[j] = core::mat4FromArray(&ibm[j * 16]);
    }

    // ---- skinned mesh (combine primitives of the skinned mesh node) ----
    const Json& meshes = root["meshes"];
    // find the node that has both "mesh" and "skin"; fall back to mesh 0.
    int meshIdx = 0;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].has("mesh") && nodes[i].has("skin")) { meshIdx = nodes[i]["mesh"].asInt(0); break; }
    const Json& prims = meshes[(size_t)meshIdx]["primitives"];
    bool bInit = false;
    for (size_t pi = 0; pi < prims.size(); ++pi) {
        const Json& pr = prims[pi];
        const Json& at = pr["attributes"];
        int posA = at["POSITION"].asInt(-1);
        if (posA < 0) continue;
        std::vector<float> pos = doc.floats(posA);
        std::vector<float> nrm = at.has("NORMAL") ? doc.floats(at["NORMAL"].asInt(-1)) : std::vector<float>();
        std::vector<float> uvv = at.has("TEXCOORD_0") ? doc.floats(at["TEXCOORD_0"].asInt(-1)) : std::vector<float>();
        std::vector<uint16_t> jnt = at.has("JOINTS_0") ? doc.joints(at["JOINTS_0"].asInt(-1)) : std::vector<uint16_t>();
        std::vector<float> wgt = at.has("WEIGHTS_0") ? doc.floats(at["WEIGHTS_0"].asInt(-1), true) : std::vector<float>();
        std::vector<uint32_t> idx = pr.has("indices") ? doc.indices(pr["indices"].asInt(-1)) : std::vector<uint32_t>();
        size_t vc = pos.size() / 3;
        if (idx.empty()) { idx.resize(vc); for (uint32_t i = 0; i < vc; ++i) idx[i] = i; }
        uint32_t base = (uint32_t)m.vertexCount();
        uint32_t indexStart = (uint32_t)m.indices.size();
        for (size_t i = 0; i < vc; ++i) {
            m.positions.push_back(pos[i * 3]); m.positions.push_back(pos[i * 3 + 1]); m.positions.push_back(pos[i * 3 + 2]);
            for (int c = 0; c < 3; ++c) m.normals.push_back(i * 3 + c < nrm.size() ? nrm[i * 3 + c] : 0.0f);
            m.uv.push_back(i * 2 < uvv.size() ? uvv[i * 2] : 0.0f);
            m.uv.push_back(i * 2 + 1 < uvv.size() ? uvv[i * 2 + 1] : 0.0f);
            for (int c = 0; c < 4; ++c) m.joints.push_back(i * 4 + c < jnt.size() ? jnt[i * 4 + c] : 0);
            for (int c = 0; c < 4; ++c) m.weights.push_back(i * 4 + c < wgt.size() ? wgt[i * 4 + c] : (c == 0 ? 1.0f : 0.0f));
            core::Vec3 p{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
            if (!bInit) { m.boundsMin = m.boundsMax = p; bInit = true; }
            else { m.boundsMin = {std::min(m.boundsMin.x, p.x), std::min(m.boundsMin.y, p.y), std::min(m.boundsMin.z, p.z)};
                   m.boundsMax = {std::max(m.boundsMax.x, p.x), std::max(m.boundsMax.y, p.y), std::max(m.boundsMax.z, p.z)}; }
        }
        for (uint32_t i : idx) m.indices.push_back(base + i);
        render::SubMesh sm;
        sm.indexOffset = indexStart;
        sm.indexCount = (uint32_t)idx.size();
        sm.material = pr.has("material") ? pr["material"].asInt(-1) : -1;
        m.subs.push_back(sm);
    }

    // Materials: base-colour texture URI (resolved vs the glb dir) + tint factor.
    {
        std::string dir;
        size_t s = path.find_last_of("/\\"); dir = (s == std::string::npos) ? "." : path.substr(0, s);
        const Json& jmats = root["materials"];
        m.mats.resize(jmats.size());
        for (size_t i = 0; i < jmats.size(); ++i) parseGltfMaterial(root, i, dir, m.mats[i]);
    }

    // ---- animations ----
    const Json& anims = root["animations"];
    m.clips.reserve(anims.size());
    for (size_t ai = 0; ai < anims.size(); ++ai) {
        const Json& a = anims[ai];
        AnimClip clip;
        clip.name = a["name"].asString();
        if (a.has("extras")) { clip.category = a["extras"]["category"].asString(); clip.additive = a["extras"]["additive"].asBool(); }
        const Json& samp = a["samplers"];
        clip.samplers.resize(samp.size());
        for (size_t si2 = 0; si2 < samp.size(); ++si2) {
            AnimSampler& s = clip.samplers[si2];
            s.times = doc.floats(samp[si2]["input"].asInt(-1));
            int outAcc = samp[si2]["output"].asInt(-1);
            s.values = doc.floats(outAcc);
            int cc, comps, ct; size_t st; doc.accPtr(outAcc, cc, comps, ct, st);
            s.comps = comps;
            std::string in = samp[si2]["interpolation"].asString();
            s.interp = in == "STEP" ? Interp::Step : (in == "CUBICSPLINE" ? Interp::CubicSpline : Interp::Linear);
            if (!s.times.empty()) clip.duration = std::max(clip.duration, s.times.back());
        }
        const Json& chans = a["channels"];
        for (size_t ci = 0; ci < chans.size(); ++ci) {
            const Json& c = chans[ci];
            AnimChannel ch;
            ch.sampler = c["sampler"].asInt(-1);
            ch.node = c["target"]["node"].asInt(-1);
            const std::string& p = c["target"]["path"].asString();
            ch.path = p == "rotation" ? AnimPath::Rotation : (p == "scale" ? AnimPath::Scale : AnimPath::Translation);
            if (ch.node >= 0 && ch.sampler >= 0) clip.channels.push_back(ch);
        }
        m.clips.push_back(std::move(clip));
    }

    LOG_INFO("skinned glb: %s -> %zu verts, %zu joints, %zu clips", path.c_str(),
             m.vertexCount(), m.skinJoints.size(), m.clips.size());
    return m.valid();
}

bool loadAnimationsByName(const std::string& path, SkinnedModel& m) {
    Glb g;
    if (!openGlb(path, g)) { LOG_ERROR("anim gltf: open failed %s", path.c_str()); return false; }
    Json root;
    if (!Json::parse((const char*)g.json, g.jsonLen, root)) return false;
    Doc doc; doc.root = &root; doc.bin = g.bin; doc.binLen = g.binLen;
    const Json& nodes = root["nodes"];
    std::vector<int> remap(nodes.size(), -1);       // anim-file node -> model node (by bone name)
    for (size_t i = 0; i < nodes.size(); ++i) {
        const std::string nm = nodes[i]["name"].asString();
        for (size_t k = 0; k < m.nodeNames.size(); ++k)
            if (m.nodeNames[k] == nm) { remap[i] = (int)k; break; }
    }
    const Json& anims = root["animations"];
    size_t before = m.clips.size();
    for (size_t ai = 0; ai < anims.size(); ++ai) {
        const Json& a = anims[ai];
        AnimClip clip;
        clip.name = a["name"].asString();
        const Json& samp = a["samplers"];
        clip.samplers.resize(samp.size());
        for (size_t si2 = 0; si2 < samp.size(); ++si2) {
            AnimSampler& s = clip.samplers[si2];
            s.times = doc.floats(samp[si2]["input"].asInt(-1));
            int outAcc = samp[si2]["output"].asInt(-1);
            s.values = doc.floats(outAcc);
            int cc, comps, ct; size_t st; doc.accPtr(outAcc, cc, comps, ct, st);
            s.comps = comps;
            std::string in = samp[si2]["interpolation"].asString();
            s.interp = in == "STEP" ? Interp::Step : (in == "CUBICSPLINE" ? Interp::CubicSpline : Interp::Linear);
            if (!s.times.empty()) clip.duration = std::max(clip.duration, s.times.back());
        }
        const Json& chans = a["channels"];
        for (size_t ci = 0; ci < chans.size(); ++ci) {
            const Json& c = chans[ci];
            int n = c["target"]["node"].asInt(-1);
            if (n < 0 || (size_t)n >= remap.size() || remap[(size_t)n] < 0) continue;
            AnimChannel ch;
            ch.sampler = c["sampler"].asInt(-1);
            ch.node = remap[(size_t)n];
            const std::string& p = c["target"]["path"].asString();
            ch.path = p == "rotation" ? AnimPath::Rotation : (p == "scale" ? AnimPath::Scale : AnimPath::Translation);
            if (ch.sampler >= 0) clip.channels.push_back(ch);
        }
        m.clips.push_back(std::move(clip));
    }
    LOG_INFO("anim gltf: %s -> %zu clips", path.c_str(), m.clips.size() - before);
    return m.clips.size() > before;
}

// ---------------- evaluation ----------------
namespace {
core::Quat slerp(core::Quat a, core::Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        core::Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        float n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (n > 1e-8f) { r.x /= n; r.y /= n; r.z /= n; r.w /= n; }
        return r;
    }
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

void segment(const AnimSampler& s, float t, int& k0, int& k1, float& u) {
    size_t n = s.times.size();
    if (n == 0) { k0 = k1 = 0; u = 0; return; }
    if (t <= s.times.front()) { k0 = k1 = 0; u = 0; return; }
    if (t >= s.times.back()) { k0 = k1 = (int)n - 1; u = 0; return; }
    int k = 0;
    while (k + 1 < (int)n && s.times[k + 1] < t) ++k;
    k0 = k; k1 = k + 1;
    float dt = s.times[k1] - s.times[k0];
    u = dt > 1e-8f ? (t - s.times[k0]) / dt : 0.0f;
}
const float* keyVal(const AnimSampler& s, int k) {
    int stride = s.interp == Interp::CubicSpline ? s.comps * 3 : s.comps;
    int off = s.interp == Interp::CubicSpline ? s.comps : 0;   // middle (value) element
    return &s.values[(size_t)k * stride + off];
}
} // namespace

namespace {
core::Quat qmul(const core::Quat& a, const core::Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
core::Quat qconj(const core::Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
core::Vec3 vlerp(const core::Vec3& a, const core::Vec3& b, float u) {
    return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u};
}
} // namespace

void samplePose(const SkinnedModel& model, int clip, float timeSec, bool loop, LocalPose& out,
                bool additive) {
    size_t nn = model.nodes.size();
    out.t.resize(nn); out.r.resize(nn); out.s.resize(nn);
    for (size_t i = 0; i < nn; ++i) {
        if (additive) { out.t[i] = {0, 0, 0}; out.r[i] = core::Quat{}; out.s[i] = {1, 1, 1}; }
        else { out.t[i] = model.nodes[i].t; out.r[i] = model.nodes[i].r; out.s[i] = model.nodes[i].s; }
    }
    if (clip < 0 || clip >= (int)model.clips.size()) return;
    const AnimClip& c = model.clips[(size_t)clip];
    float t = c.duration > 0 ? (loop ? std::fmod(timeSec, c.duration)
                                      : std::min(timeSec, c.duration)) : 0.0f;
    for (const AnimChannel& ch : c.channels) {
        const AnimSampler& s = c.samplers[(size_t)ch.sampler];
        if (s.times.empty() || ch.node >= (int)nn) continue;
        int k0, k1; float u; segment(s, t, k0, k1, u);
        const float* a = keyVal(s, k0); const float* b = keyVal(s, k1);
        if (ch.path == AnimPath::Rotation) {
            core::Quat qa{a[0], a[1], a[2], a[3]}, qb{b[0], b[1], b[2], b[3]};
            out.r[(size_t)ch.node] = s.interp == Interp::Step ? qa : slerp(qa, qb, u);
        } else {
            core::Vec3 va{a[0], a[1], a[2]}, vb{b[0], b[1], b[2]};
            core::Vec3 r = s.interp == Interp::Step ? va : vlerp(va, vb, u);
            if (ch.path == AnimPath::Translation) out.t[(size_t)ch.node] = r; else out.s[(size_t)ch.node] = r;
        }
    }
}

void blendPose(const LocalPose& a, const LocalPose& b, float alpha, LocalPose& out,
               const std::vector<float>* mask) {
    size_t n = std::min(a.size(), b.size());
    if (&out != &a && &out != &b) out = a;
    for (size_t i = 0; i < n; ++i) {
        float u = alpha * (mask ? (i < mask->size() ? (*mask)[i] : 0.0f) : 1.0f);
        u = std::min(std::max(u, 0.0f), 1.0f);
        core::Vec3 t = vlerp(a.t[i], b.t[i], u), s = vlerp(a.s[i], b.s[i], u);
        core::Quat r = u <= 0.0f ? a.r[i] : (u >= 1.0f ? b.r[i] : slerp(a.r[i], b.r[i], u));
        out.t[i] = t; out.r[i] = r; out.s[i] = s;
    }
}

void blendPoseMeshSpace(const SkinnedModel& model, const LocalPose& a, const LocalPose& b, float alpha,
                        const std::vector<float>& mask, LocalPose& out) {
    size_t nn = model.nodes.size();
    if (a.size() < nn || b.size() < nn) { blendPose(a, b, alpha, out, &mask); return; }
    // Parent-before-child order.
    std::vector<int> order, st;
    order.reserve(nn);
    for (int r : model.roots) {
        st.assign(1, r);
        while (!st.empty()) {
            int n = st.back(); st.pop_back();
            order.push_back(n);
            for (int ch : model.nodes[(size_t)n].children) st.push_back(ch);
        }
    }
    auto meshRot = [&](const LocalPose& p, std::vector<core::Quat>& g) {
        g.assign(nn, core::Quat{});
        for (int n : order) {
            int par = model.nodes[(size_t)n].parent;
            g[(size_t)n] = par >= 0 ? qmul(g[(size_t)par], p.r[(size_t)n]) : p.r[(size_t)n];
        }
    };
    std::vector<core::Quat> ga, gb, go(nn);
    meshRot(a, ga);
    meshRot(b, gb);
    if (&out != &a) out = a;
    for (int n : order) {
        size_t i = (size_t)n;
        int par = model.nodes[i].parent;
        core::Quat pg = par >= 0 ? go[(size_t)par] : core::Quat{};
        float u = std::min(std::max(alpha * (i < mask.size() ? mask[i] : 0.0f), 0.0f), 1.0f);
        if (u <= 0.0f) { go[i] = qmul(pg, out.r[i]); continue; }   // keep a's local rotation
        core::Quat target = u >= 1.0f ? gb[i] : slerp(ga[i], gb[i], u);
        out.r[i] = qmul(qconj(pg), target);
        go[i] = target;
        out.t[i] = vlerp(a.t[i], b.t[i], u);
        out.s[i] = vlerp(a.s[i], b.s[i], u);
    }
}

void addPose(LocalPose& base, const LocalPose& delta, float weight, const std::vector<float>* mask) {
    size_t n = std::min(base.size(), delta.size());
    for (size_t i = 0; i < n; ++i) {
        float w = weight * (mask ? (i < mask->size() ? (*mask)[i] : 0.0f) : 1.0f);
        if (w <= 0.0f) continue;
        base.t[i] += delta.t[i] * w;
        core::Quat d = (w >= 1.0f) ? delta.r[i] : slerp(core::Quat{}, delta.r[i], w);
        base.r[i] = qmul(base.r[i], d);
        core::Vec3 ds = vlerp({1, 1, 1}, delta.s[i], w);
        base.s[i] = {base.s[i].x * ds.x, base.s[i].y * ds.y, base.s[i].z * ds.z};
    }
}

void deltaPose(const LocalPose& ref, const LocalPose& p, LocalPose& out) {
    size_t n = std::min(ref.size(), p.size());
    out.t.resize(n); out.r.resize(n); out.s.resize(n);
    for (size_t i = 0; i < n; ++i) {
        out.t[i] = p.t[i] - ref.t[i];
        out.r[i] = qmul(qconj(ref.r[i]), p.r[i]);
        auto div = [](float x, float y) { return std::fabs(y) > 1e-6f ? x / y : 1.0f; };
        out.s[i] = {div(p.s[i].x, ref.s[i].x), div(p.s[i].y, ref.s[i].y), div(p.s[i].z, ref.s[i].z)};
    }
}

core::Quat quatMul(const core::Quat& a, const core::Quat& b) { return qmul(a, b); }

core::Quat quatAxisAngle(const core::Vec3& axis, float angle) {
    float s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}

core::Quat quatSlerp(const core::Quat& a, const core::Quat& b, float t) { return slerp(a, b, t); }

core::Vec3 quatRotate(const core::Quat& q, const core::Vec3& v) {
    core::Quat r = qmul(qmul(q, core::Quat{v.x, v.y, v.z, 0.0f}), qconj(q));
    return {r.x, r.y, r.z};
}

core::Quat meshRotation(const SkinnedModel& model, const LocalPose& pose, int node) {
    core::Quat q;
    for (int n = node; n >= 0 && (size_t)n < pose.size(); n = model.nodes[(size_t)n].parent)
        q = qmul(pose.r[(size_t)n], q);
    return q;
}

void applyMeshSpace(const SkinnedModel& model, LocalPose& pose, int node, const core::Quat& meshRot,
                    const core::Vec3& meshOffset) {
    if (node < 0 || (size_t)node >= pose.size()) return;
    int par = model.nodes[(size_t)node].parent;
    core::Quat pg = par >= 0 ? meshRotation(model, pose, par) : core::Quat{};
    core::Quat inv = qconj(pg);
    // global' = meshRot * global  ->  local' = inv(Pg) * meshRot * Pg * local
    pose.r[(size_t)node] = qmul(qmul(qmul(inv, meshRot), pg), pose.r[(size_t)node]);
    // Offset: rotate the model-space vector into the parent's frame.
    core::Quat v{meshOffset.x, meshOffset.y, meshOffset.z, 0.0f};
    core::Quat lv = qmul(qmul(inv, v), pg);
    pose.t[(size_t)node] += core::Vec3{lv.x, lv.y, lv.z};
}

std::vector<float> subtreeMask(const SkinnedModel& model, int rootNode) {
    std::vector<float> m(model.nodes.size(), 0.0f);
    if (rootNode < 0 || rootNode >= (int)model.nodes.size()) return m;
    std::vector<int> st{rootNode};
    while (!st.empty()) {
        int n = st.back(); st.pop_back();
        m[(size_t)n] = 1.0f;
        for (int ch : model.nodes[(size_t)n].children) st.push_back(ch);
    }
    return m;
}

void poseGlobals(const SkinnedModel& model, const LocalPose& pose, std::vector<core::Mat4>& global) {
    size_t nn = model.nodes.size();
    global.assign(nn, core::Mat4::identity());
    if (pose.size() < nn) return;
    // Resolve the hierarchy parent-before-child via a DFS pre-order from the roots.
    std::vector<int> st;
    for (int r : model.roots) {
        st.assign(1, r);
        while (!st.empty()) {
            int n = st.back(); st.pop_back();
            core::Mat4 local = core::mat4FromTRS(pose.t[(size_t)n], pose.r[(size_t)n], pose.s[(size_t)n]);
            int p = model.nodes[(size_t)n].parent;
            global[(size_t)n] = (p >= 0) ? global[(size_t)p] * local : local;
            for (int ch : model.nodes[(size_t)n].children) st.push_back(ch);
        }
    }
}

void skinPose(const SkinnedModel& model, const LocalPose& pose,
              std::vector<core::Mat4>& global, render::MeshData& out) {
    poseGlobals(model, pose, global);

    // Joint matrices = global(joint) * invBind(joint).
    std::vector<core::Mat4> jm(model.skinJoints.size());
    for (size_t j = 0; j < model.skinJoints.size(); ++j) {
        core::Mat4 gm = global[(size_t)model.skinJoints[j]];
        jm[j] = (j < model.invBind.size()) ? gm * model.invBind[j] : gm;
    }

    // CPU skin.
    size_t vc = model.vertexCount();
    out.positions.resize(vc * 3);
    out.normals.resize(vc * 3);
    out.indices = model.indices;
    // M45: always from THIS model. Keeping them when only the count matched left another model's sub-mesh ranges /
    // UVs on a reused output (robot <-> vehicle, chassis change: equal section counts): every draw then read past the
    // index buffer (out-of-bounds GPU vertex fetch; caught by the M43 guard on the player route: Optimus spawn).
    out.uv = model.uv;
    out.subs = model.subs;
    out.mats = model.mats;                                      // picks up resolved texture handles
    for (size_t i = 0; i < vc; ++i) {
        core::Vec3 p{model.positions[i * 3], model.positions[i * 3 + 1], model.positions[i * 3 + 2]};
        core::Vec3 n{model.normals[i * 3], model.normals[i * 3 + 1], model.normals[i * 3 + 2]};
        core::Vec3 sp{0, 0, 0}, sn{0, 0, 0};
        for (int w = 0; w < 4; ++w) {
            float wt = model.weights[i * 4 + w];
            if (wt <= 0) continue;
            uint16_t ji = model.joints[i * 4 + w];
            if (ji >= jm.size()) continue;
            const core::Mat4& M = jm[ji];
            sp += core::transformPoint(M, p) * wt;
            sn += core::transformDir(M, n) * wt;
        }
        out.positions[i * 3] = sp.x; out.positions[i * 3 + 1] = sp.y; out.positions[i * 3 + 2] = sp.z;
        core::Vec3 nn2 = core::normalize(sn);
        out.normals[i * 3] = nn2.x; out.normals[i * 3 + 1] = nn2.y; out.normals[i * 3 + 2] = nn2.z;
    }
}

void evaluatePose(const SkinnedModel& model, int clip, float timeSec,
                  std::vector<core::Mat4>& global, render::MeshData& out, bool loop) {
    LocalPose pose;
    samplePose(model, clip, timeSec, loop, pose);
    skinPose(model, pose, global, out);
}

} // namespace assets
