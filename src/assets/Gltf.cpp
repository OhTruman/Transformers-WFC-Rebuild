#include "assets/Gltf.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace assets {
namespace {

struct Glb {
    std::vector<uint8_t> file;
    const uint8_t* json = nullptr; size_t jsonLen = 0;
    const uint8_t* bin = nullptr;  size_t binLen = 0;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff n = f.tellg();
    if (n <= 0) return false;
    out.resize((size_t)n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return (bool)f;
}

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool openGlb(const std::string& path, Glb& g) {
    if (!readFile(path, g.file)) { LOG_ERROR("glb: cannot read %s", path.c_str()); return false; }
    if (g.file.size() < 12) { LOG_ERROR("glb: too small %s", path.c_str()); return false; }
    const uint8_t* d = g.file.data();
    if (rd32(d) != 0x46546C67u) { LOG_ERROR("glb: bad magic %s", path.c_str()); return false; }
    size_t total = rd32(d + 8);
    if (total > g.file.size()) total = g.file.size();
    size_t off = 12;
    while (off + 8 <= total) {
        uint32_t clen = rd32(d + off);
        uint32_t ctype = rd32(d + off + 4);
        const uint8_t* cdata = d + off + 8;
        if (off + 8 + clen > total) break;
        if (ctype == 0x4E4F534Au) { g.json = cdata; g.jsonLen = clen; }       // "JSON"
        else if (ctype == 0x004E4942u) { g.bin = cdata; g.binLen = clen; }    // "BIN\0"
        off += 8 + (size_t)clen;
        off = (off + 3) & ~size_t(3); // chunks are 4-byte aligned
    }
    if (!g.json) { LOG_ERROR("glb: no JSON chunk %s", path.c_str()); return false; }
    return true;
}

int componentBytes(int ct) {
    switch (ct) {
        case 5120: case 5121: return 1; // byte / ubyte
        case 5122: case 5123: return 2; // short / ushort
        case 5125: case 5126: return 4; // uint / float
        default: return 0;
    }
}
int typeComponents(const std::string& t) {
    if (t == "SCALAR") return 1;
    if (t == "VEC2") return 2;
    if (t == "VEC3") return 3;
    if (t == "VEC4") return 4;
    if (t == "MAT4") return 16;
    return 0;
}

struct GltfDoc {
    const Json* root = nullptr;
    const uint8_t* bin = nullptr;
    size_t binLen = 0;

    const Json& accessors() const { return (*root)["accessors"]; }
    const Json& bufferViews() const { return (*root)["bufferViews"]; }

    // Base pointer + stride for an accessor's data in the BIN chunk. Returns null on error.
    const uint8_t* accessorData(int accIdx, int& outCount, int& outComps, int& outCompType,
                                size_t& outStride) const {
        const Json& acc = accessors()[(size_t)accIdx];
        int bvIdx = acc["bufferView"].asInt(-1);
        if (bvIdx < 0) return nullptr;
        const Json& bv = bufferViews()[(size_t)bvIdx];
        if (bv["buffer"].asInt(0) != 0) return nullptr; // only embedded buffer 0 supported
        size_t bvOff = (size_t)bv["byteOffset"].asInt(0);
        size_t accOff = (size_t)acc["byteOffset"].asInt(0);
        outCount = acc["count"].asInt(0);
        outComps = typeComponents(acc["type"].asString());
        outCompType = acc["componentType"].asInt(0);
        int elemSize = outComps * componentBytes(outCompType);
        outStride = bv.has("byteStride") ? (size_t)bv["byteStride"].asInt(elemSize) : (size_t)elemSize;
        size_t start = bvOff + accOff;
        if (!bin || start > binLen) return nullptr;
        return bin + start;
    }
};

void readVec3(const GltfDoc& doc, int accIdx, std::vector<core::Vec3>& out) {
    int count = 0, comps = 0, ct = 0; size_t stride = 0;
    const uint8_t* base = doc.accessorData(accIdx, count, comps, ct, stride);
    if (!base || comps != 3 || ct != 5126) return; // expect float VEC3
    out.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        const uint8_t* e = base + (size_t)i * stride;
        float v[3];
        std::memcpy(v, e, 12);
        out[(size_t)i] = {v[0], v[1], v[2]};
    }
}

void readVec2(const GltfDoc& doc, int accIdx, std::vector<float>& out) {
    int count = 0, comps = 0, ct = 0; size_t stride = 0;
    const uint8_t* base = doc.accessorData(accIdx, count, comps, ct, stride);
    if (!base || comps != 2) return;
    out.resize((size_t)count * 2);
    for (int i = 0; i < count; ++i) {
        const uint8_t* e = base + (size_t)i * stride;
        for (int c = 0; c < 2; ++c) {
            float v = 0;
            if (ct == 5126) std::memcpy(&v, e + c * 4, 4);
            else if (ct == 5123) { uint16_t u; std::memcpy(&u, e + c * 2, 2); v = u / 65535.0f; }
            else if (ct == 5121) { v = e[c] / 255.0f; }
            out[(size_t)i * 2 + c] = v;
        }
    }
}

void readIndices(const GltfDoc& doc, int accIdx, std::vector<uint32_t>& out) {
    int count = 0, comps = 0, ct = 0; size_t stride = 0;
    const uint8_t* base = doc.accessorData(accIdx, count, comps, ct, stride);
    if (!base || comps != 1) return;
    out.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        const uint8_t* e = base + (size_t)i * stride;
        uint32_t idx = 0;
        if (ct == 5125) { std::memcpy(&idx, e, 4); }
        else if (ct == 5123) { uint16_t s; std::memcpy(&s, e, 2); idx = s; }
        else if (ct == 5121) { idx = *e; }
        out[(size_t)i] = idx;
    }
}

core::Mat4 nodeLocal(const Json& node) {
    if (node.has("matrix") && node["matrix"].size() == 16) {
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = node["matrix"][(size_t)i].asFloat();
        return core::mat4FromArray(m);
    }
    core::Vec3 t{0, 0, 0}, s{1, 1, 1};
    core::Quat q;
    if (node.has("translation")) {
        const Json& a = node["translation"];
        t = {a[0].asFloat(), a[1].asFloat(), a[2].asFloat()};
    }
    if (node.has("rotation")) {
        const Json& a = node["rotation"];
        q = {a[0].asFloat(), a[1].asFloat(), a[2].asFloat(), a[3].asFloat(1.0f)};
    }
    if (node.has("scale")) {
        const Json& a = node["scale"];
        s = {a[0].asFloat(1.0f), a[1].asFloat(1.0f), a[2].asFloat(1.0f)};
    }
    return core::mat4FromTRS(t, q, s);
}

void bakeNode(const GltfDoc& doc, const Json& nodes, const Json& meshes, int nodeIdx,
              const core::Mat4& parent, render::MeshData& out, bool& boundsInit) {
    const Json& node = nodes[(size_t)nodeIdx];
    core::Mat4 world = parent * nodeLocal(node);

    if (node.has("mesh")) {
        const Json& mesh = meshes[(size_t)node["mesh"].asInt(0)];
        const Json& prims = mesh["primitives"];
        for (size_t pi = 0; pi < prims.size(); ++pi) {
            const Json& prim = prims[pi];
            if (prim.has("mode") && prim["mode"].asInt(4) != 4) continue; // triangles only
            int posAcc = prim["attributes"]["POSITION"].asInt(-1);
            if (posAcc < 0) continue;
            std::vector<core::Vec3> pos, nrm;
            std::vector<float> uv, uv1;
            readVec3(doc, posAcc, pos);
            if (prim["attributes"].has("NORMAL"))
                readVec3(doc, prim["attributes"]["NORMAL"].asInt(-1), nrm);
            if (prim["attributes"].has("TEXCOORD_0"))
                readVec2(doc, prim["attributes"]["TEXCOORD_0"].asInt(-1), uv);
            if (prim["attributes"].has("TEXCOORD_1"))
                readVec2(doc, prim["attributes"]["TEXCOORD_1"].asInt(-1), uv1);   // lightmap UV
            if (pos.empty()) continue;

            std::vector<uint32_t> idx;
            if (prim.has("indices")) readIndices(doc, prim["indices"].asInt(-1), idx);
            if (idx.empty()) { idx.resize(pos.size()); for (uint32_t i = 0; i < pos.size(); ++i) idx[i] = i; }

            uint32_t base = (uint32_t)out.vertexCount();
            uint32_t indexStart = (uint32_t)out.indices.size();
            for (size_t i = 0; i < pos.size(); ++i) {
                core::Vec3 wp = core::transformPoint(world, pos[i]);
                out.positions.push_back(wp.x);
                out.positions.push_back(wp.y);
                out.positions.push_back(wp.z);
                core::Vec3 wn = i < nrm.size() ? core::normalize(core::transformDir(world, nrm[i]))
                                               : core::Vec3{0, 1, 0};
                out.normals.push_back(wn.x);
                out.normals.push_back(wn.y);
                out.normals.push_back(wn.z);
                out.uv.push_back(i * 2 < uv.size() ? uv[i * 2] : 0.0f);
                out.uv.push_back(i * 2 + 1 < uv.size() ? uv[i * 2 + 1] : 0.0f);
                out.uv1.push_back(i * 2 < uv1.size() ? uv1[i * 2] : 0.0f);
                out.uv1.push_back(i * 2 + 1 < uv1.size() ? uv1[i * 2 + 1] : 0.0f);
                if (!boundsInit) { out.boundsMin = out.boundsMax = wp; boundsInit = true; }
                else {
                    out.boundsMin = {std::min(out.boundsMin.x, wp.x), std::min(out.boundsMin.y, wp.y), std::min(out.boundsMin.z, wp.z)};
                    out.boundsMax = {std::max(out.boundsMax.x, wp.x), std::max(out.boundsMax.y, wp.y), std::max(out.boundsMax.z, wp.z)};
                }
            }
            for (uint32_t i : idx) out.indices.push_back(base + i);
            render::SubMesh sm;
            sm.indexOffset = indexStart;
            sm.indexCount = (uint32_t)idx.size();
            sm.material = prim.has("material") ? prim["material"].asInt(-1) : -1;
            // Baked-lightmap binding carried on the node extras (per prop instance).
            if (node.has("extras") && node["extras"].has("lm_atlas") && !uv1.empty()) {
                const Json& ex = node["extras"];
                sm.lightmapName = ex["lm_atlas"].asString();
                sm.lmScale[0] = ex["lm_scale"][0].asFloat(1.0f);
                sm.lmScale[1] = ex["lm_scale"][1].asFloat(1.0f);
                sm.lmBias[0] = ex["lm_bias"][0].asFloat(0.0f);
                sm.lmBias[1] = ex["lm_bias"][1].asFloat(0.0f);
                if (ex.has("lm_scalevec")) {
                    sm.lmScaleVec[0] = ex["lm_scalevec"][0].asFloat(1.0f);
                    sm.lmScaleVec[1] = ex["lm_scalevec"][1].asFloat(1.0f);
                    sm.lmScaleVec[2] = ex["lm_scalevec"][2].asFloat(1.0f);
                }
            }
            out.subs.push_back(sm);
        }
    }

    const Json& children = node["children"];
    for (size_t i = 0; i < children.size(); ++i)
        bakeNode(doc, nodes, meshes, children[i].asInt(0), world, out, boundsInit);
}

} // namespace

bool loadGlb(const std::string& path, render::MeshData& out) {
    Glb g;
    if (!openGlb(path, g)) return false;

    Json root;
    if (!Json::parse(reinterpret_cast<const char*>(g.json), g.jsonLen, root) || !root.isObject()) {
        LOG_ERROR("glb: JSON parse failed %s", path.c_str());
        return false;
    }
    if (root.has("extensionsRequired") && root["extensionsRequired"].size() > 0) {
        const Json& ext = root["extensionsRequired"];
        for (size_t i = 0; i < ext.size(); ++i)
            LOG_WARN("glb: requires extension '%s' (unsupported) in %s", ext[i].asString().c_str(), path.c_str());
    }

    GltfDoc doc;
    doc.root = &root;
    doc.bin = g.bin;
    doc.binLen = g.binLen;

    const Json& nodes = root["nodes"];
    const Json& meshes = root["meshes"];
    const Json& scenes = root["scenes"];
    int sceneIdx = root["scene"].asInt(0);
    bool boundsInit = false;

    if (scenes.size() > 0) {
        const Json& scene = scenes[(size_t)sceneIdx];
        const Json& roots = scene["nodes"];
        for (size_t i = 0; i < roots.size(); ++i)
            bakeNode(doc, nodes, meshes, roots[i].asInt(0), core::Mat4::identity(), out, boundsInit);
    } else {
        // No scene graph: bake every node at identity.
        for (size_t i = 0; i < nodes.size(); ++i)
            bakeNode(doc, nodes, meshes, (int)i, core::Mat4::identity(), out, boundsInit);
    }

    // Materials: base-colour texture URI (resolved vs the glb dir) + tint factor.
    std::string dir;
    { size_t s = path.find_last_of("/\\"); dir = (s == std::string::npos) ? "." : path.substr(0, s); }
    const Json& jmats = root["materials"];
    const Json& jtex = root["textures"];
    const Json& jimg = root["images"];
    out.mats.resize(jmats.size());
    for (size_t i = 0; i < jmats.size(); ++i) {
        render::Material& M = out.mats[i];
        const Json& pbr = jmats[i]["pbrMetallicRoughness"];
        if (pbr.has("baseColorFactor") && pbr["baseColorFactor"].size() >= 3)
            M.color = {pbr["baseColorFactor"][0].asFloat(1.0f), pbr["baseColorFactor"][1].asFloat(1.0f),
                       pbr["baseColorFactor"][2].asFloat(1.0f)};
        int tIdx = pbr["baseColorTexture"]["index"].asInt(-1);
        if (tIdx >= 0 && (size_t)tIdx < jtex.size()) {
            int imgIdx = jtex[(size_t)tIdx]["source"].asInt(-1);
            if (imgIdx >= 0 && (size_t)imgIdx < jimg.size()) {
                std::string uri = jimg[(size_t)imgIdx]["uri"].asString();
                if (!uri.empty() && uri.rfind("data:", 0) != 0) M.baseColorUri = dir + "/" + uri;
            }
        }
        // Derive the parallel emissive texture (AssetTools names them *_basecolor / *_emissive).
        size_t bc = M.baseColorUri.find("basecolor");
        if (bc != std::string::npos)
            M.emissiveUri = M.baseColorUri.substr(0, bc) + "emissive" + M.baseColorUri.substr(bc + 9);
    }

    if (out.empty()) { LOG_WARN("glb: no triangles baked from %s", path.c_str()); return false; }
    LOG_INFO("glb: %s -> %zu verts, %zu tris, bounds [%.1f %.1f %.1f]..[%.1f %.1f %.1f]",
             path.c_str(), out.vertexCount(), out.triangleCount(),
             out.boundsMin.x, out.boundsMin.y, out.boundsMin.z,
             out.boundsMax.x, out.boundsMax.y, out.boundsMax.z);
    return true;
}

} // namespace assets
