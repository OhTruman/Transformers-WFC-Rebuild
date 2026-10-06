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
            std::vector<uint32_t> srcv;                    // cooked vertex index (vertex-lightmap samples)
            if (prim["attributes"].has("_WFC_SRCVERT")) readIndices(doc, prim["attributes"]["_WFC_SRCVERT"].asInt(-1), srcv);
            if (pos.empty()) continue;

            std::vector<uint32_t> idx;
            if (prim.has("indices")) readIndices(doc, prim["indices"].asInt(-1), idx);
            if (idx.empty()) { idx.resize(pos.size()); for (uint32_t i = 0; i < pos.size(); ++i) idx[i] = i; }

            uint32_t base = (uint32_t)out.vertexCount();
            uint32_t indexStart = (uint32_t)out.indices.size();
            if (!srcv.empty() || !out.srcVert.empty()) {
                out.srcVert.resize(base, UINT32_MAX);
                for (size_t i = 0; i < pos.size(); ++i) out.srcVert.push_back(i < srcv.size() ? srcv[i] : UINT32_MAX);
            }
            // Normals transform by the inverse-transpose of the node's linear part (non-uniform and mirrored
            // instance scale, e.g. StaticMeshCollectionActor components): cofactor matrix * sign(det).
            const float* L = world.m;
            float a00 = L[0], a10 = L[1], a20 = L[2], a01 = L[4], a11 = L[5], a21 = L[6], a02 = L[8], a12 = L[9], a22 = L[10];
            float c00 = a11 * a22 - a12 * a21, c01 = -(a10 * a22 - a12 * a20), c02 = a10 * a21 - a11 * a20;
            float c10 = -(a01 * a22 - a02 * a21), c11 = a00 * a22 - a02 * a20, c12 = -(a00 * a21 - a01 * a20);
            float c20 = a01 * a12 - a02 * a11, c21 = -(a00 * a12 - a02 * a10), c22 = a00 * a11 - a01 * a10;
            float detL = a00 * c00 + a01 * c01 + a02 * c02;
            float sgn = detL < 0.0f ? -1.0f : 1.0f;
            auto normalXf = [&](const core::Vec3& n) {   // (A^-1)^T n = cof(A) n / det
                return core::normalize(core::Vec3{(c00 * n.x + c01 * n.y + c02 * n.z) * sgn,
                                                  (c10 * n.x + c11 * n.y + c12 * n.z) * sgn,
                                                  (c20 * n.x + c21 * n.y + c22 * n.z) * sgn});
            };
            for (size_t i = 0; i < pos.size(); ++i) {
                core::Vec3 wp = core::transformPoint(world, pos[i]);
                out.positions.push_back(wp.x);
                out.positions.push_back(wp.y);
                out.positions.push_back(wp.z);
                core::Vec3 wn = i < nrm.size() ? normalXf(nrm[i]) : core::Vec3{0, 1, 0};
                out.normals.push_back(wn.x);
                out.normals.push_back(wn.y);
                out.normals.push_back(wn.z);
                out.uv.push_back(i * 2 < uv.size() ? uv[i * 2] : 0.0f);
                out.uv.push_back(i * 2 + 1 < uv.size() ? uv[i * 2 + 1] : 0.0f);
                // No TEXCOORD_1: UE3's vertex factory binds the last available channel (UV0) for
                // missing texcoords, so material TexCoord[1] reads UV0 (e.g. Light_Cylinder_STAT).
                const std::vector<float>& u1 = uv1.empty() ? uv : uv1;
                out.uv1.push_back(i * 2 < u1.size() ? u1[i * 2] : 0.0f);
                out.uv1.push_back(i * 2 + 1 < u1.size() ? u1[i * 2 + 1] : 0.0f);
                if (!boundsInit) { out.boundsMin = out.boundsMax = wp; boundsInit = true; }
                else {
                    out.boundsMin = {std::min(out.boundsMin.x, wp.x), std::min(out.boundsMin.y, wp.y), std::min(out.boundsMin.z, wp.z)};
                    out.boundsMax = {std::max(out.boundsMax.x, wp.x), std::max(out.boundsMax.y, wp.y), std::max(out.boundsMax.z, wp.z)};
                }
            }
            // Mirrored instance (negative determinant): baking into world space reverses the
            // triangle winding, which back-face culling would then reject. UE3 compensates with
            // LocalToWorldRotDeterminantFlip; restore the winding here.
            const float* w = world.m;
            float det = w[0] * (w[5] * w[10] - w[9] * w[6]) - w[4] * (w[1] * w[10] - w[9] * w[2]) +
                        w[8] * (w[1] * w[6] - w[5] * w[2]);
            if (det < 0.0f && idx.size() % 3 == 0)
                for (size_t t = 0; t < idx.size(); t += 3) std::swap(idx[t + 1], idx[t + 2]);
            for (uint32_t i : idx) out.indices.push_back(base + i);
            render::SubMesh sm;
            sm.indexOffset = indexStart;
            sm.indexCount = (uint32_t)idx.size();
            sm.material = prim.has("material") ? prim["material"].asInt(-1) : -1;
            sm.nodeName = node["name"].asString();
            if (node.has("extras")) {
                sm.component = node["extras"]["component"].asString();
                sm.sourceMesh = node["extras"]["mesh"].asString();
                sm.hiddenGame = node["extras"]["hidden_game"].asBool(false);
                sm.sourceSection = (int)pi;
                if (node["extras"]["kind"].asString() == "bsp")      // level BSP (unlit in world.glb)
                    sm.component = "bsp:" + node["extras"]["source"].asString();
                // StaticMeshActor / (Static)InterpActor nodes name only their actor: the renderer
                // resolves the actor's single StaticMeshComponent (lightmap join key).
                else if (sm.component.empty() && !node["extras"]["actor"].asString().empty())
                    sm.component = "actor:" + node["extras"]["actor"].asString();
            }
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

static std::string imageUri(const Json& root, int texIdx, const std::string& dir) {
    const Json& jtex = root["textures"];
    const Json& jimg = root["images"];
    if (texIdx < 0 || (size_t)texIdx >= jtex.size()) return std::string();
    int imgIdx = jtex[(size_t)texIdx]["source"].asInt(-1);
    if (imgIdx < 0 || (size_t)imgIdx >= jimg.size()) return std::string();
    std::string uri = jimg[(size_t)imgIdx]["uri"].asString();
    if (uri.empty() || uri.rfind("data:", 0) == 0) return std::string();
    return dir + "/" + uri;
}

void parseGltfMaterial(const Json& root, size_t i, const std::string& dir, render::Material& M) {
    const Json& jm = root["materials"][i];
    const Json& pbr = jm["pbrMetallicRoughness"];
    if (pbr.has("baseColorFactor") && pbr["baseColorFactor"].size() >= 3)
        M.color = {pbr["baseColorFactor"][0].asFloat(1.0f), pbr["baseColorFactor"][1].asFloat(1.0f),
                   pbr["baseColorFactor"][2].asFloat(1.0f)};
    M.baseColorUri = imageUri(root, pbr["baseColorTexture"]["index"].asInt(-1), dir);
    M.normalUri = imageUri(root, jm["normalTexture"]["index"].asInt(-1), dir);
    M.wfcName = jm["extras"]["wfc_material"].asString();
    M.sourceName = jm["name"].asString();
    // Derive the parallel emissive/specular textures (AssetTools names them *_basecolor / *_emissive).
    size_t bc = M.baseColorUri.find("basecolor");
    if (bc != std::string::npos) {
        M.emissiveUri = M.baseColorUri.substr(0, bc) + "emissive" + M.baseColorUri.substr(bc + 9);
        M.specularUri = M.baseColorUri.substr(0, bc) + "specular" + M.baseColorUri.substr(bc + 9);
    }
}

bool loadGlb(const std::string& path, render::MeshData& out) {
    Glb g;
    std::vector<uint8_t> extBin;   // .gltf: JSON text + external buffers[0].uri (umodel exports)
    bool textGltf = path.size() > 5 && path.compare(path.size() - 5, 5, ".gltf") == 0;
    if (textGltf) {
        if (!readFile(path, g.file)) { LOG_ERROR("gltf: cannot read %s", path.c_str()); return false; }
        g.json = g.file.data(); g.jsonLen = g.file.size();
        Json probe;
        if (Json::parse(reinterpret_cast<const char*>(g.json), g.jsonLen, probe) && probe["buffers"].size() > 0) {
            std::string uri = probe["buffers"][0]["uri"].asString();
            size_t s = path.find_last_of("/\\");
            std::string dir = (s == std::string::npos) ? "." : path.substr(0, s);
            if (!uri.empty() && readFile(dir + "/" + uri, extBin)) { g.bin = extBin.data(); g.binLen = extBin.size(); }
        }
    } else if (!openGlb(path, g)) {
        return false;
    }

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
    out.mats.resize(jmats.size());
    for (size_t i = 0; i < jmats.size(); ++i) parseGltfMaterial(root, i, dir, out.mats[i]);

    if (out.empty()) { LOG_WARN("glb: no triangles baked from %s", path.c_str()); return false; }
    LOG_INFO("glb: %s -> %zu verts, %zu tris, bounds [%.1f %.1f %.1f]..[%.1f %.1f %.1f]",
             path.c_str(), out.vertexCount(), out.triangleCount(),
             out.boundsMin.x, out.boundsMin.y, out.boundsMin.z,
             out.boundsMax.x, out.boundsMax.y, out.boundsMax.z);
    return true;
}

} // namespace assets
