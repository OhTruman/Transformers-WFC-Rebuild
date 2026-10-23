// M7 "RT Realistic" R1 - the offline baked-sun visibility (Systems; docs/handoff/SYSTEMS_RT_REALISTIC_M6_PLAN.md, R1).
// For every lightmap texel of a map: rasterise the lightmapped triangles in lightmap space (conservative coverage),
// trace the sun through the static world (CPU BVH, any-hit) with the runtime sun cone, 4 x 4 supersampled per texel,
// and write per atlas: coverage, V (fraction of rays that reach the sun), the sun direction in the texel's tangent frame
// (for the per-coefficient fit), N.L and the component index. vbake_fit.py then fits the sun's baked share (k, p) and
// writes the R8 V_bake pages + fit.json + preview sheets. Reads the render data / ExtractedAssets read-only; writes only
// into --out (the worktree's work/ folder).
//
// Occluders: world.glb nodes (BSP node excluded: bsp.glb has it, with lightmap UVs) + bsp.glb elements; opaque and masked
// materials (additive / translucent / modulate excluded); components flagged no_shadow in lighting.json excluded; sky
// meshes excluded (a sky dome blocks every sun ray). Receivers: every node / element with a lightmap in lighting.json.
// Vertex-lightmapped components (LMT_1D): per-vertex V into vertex_vbake.json (same rays at each vertex).
//
// --lights <stride> (Rendering's runtime RT lightmap, S1 / S2 / S4 calibration; 0 = off): on every stride-th covered texel
// (x and y) also trace (a) the sky: --sky-rays stratified uniform-hemisphere directions about the normal, a ray that
// leaves the world upward (+y) unoccluded counts, accumulated per lightmap coefficient as
// S_i = (2 / n) sum vis * max(B_i . w_t, 0) (= (1/pi) integral of the visible upper sky weighted like the sun's share, p 1;
// the tangent frame's y as the tracer guesses it - the fit applies fit.json's handedness), and (b) every enabled point /
// spot light within --light-range x its radius on the texel's front side: visibility V (1 ray to the centre + 3 jittered
// within 0.1 m; 1 without rays when the light does not cast static shadows), distance d (m), the light direction in the
// tangent frame and the cosine to a spot's axis. Writes lights_<page>.bin per page + lights_index.json (see writeLights).
// Build (repo root): clang++ -O2 -std=c++17 -Isrc tools/systems/vbake/vbake_trace.cpp -o work/vbake/vbake_trace.exe
// Usage: vbake_trace --world <world.glb> --bsp <bsp.glb> --render <render map dir> --out <dir> [--cone 0.27] [--ss 4]
//        [--threads 2] [--bias 0.02] [--lights 4] [--sky-rays 64] [--light-range 1.5]
// Campaign chapters (Rendering): one unit's receivers with the whole chapter around them - --occluder <another unit's
// world.glb / bsp.glb> (repeatable; its components are not in this unit's lightmap table, so they only block), --bsp
// optional (most units have none), --sun-from <lighting.json> (the chapter's sun when this unit's table has none),
// --lights-from <lighting.json> (repeatable: the other units' point / spot lights for --lights - one bake lit them all).
#include "assets/Json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using assets::Json;

namespace {

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { float l = len(a); return l > 1e-20f ? a * (1.0f / l) : V3{0, 0, 1}; }
inline V3 vmin(V3 a, V3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline V3 vmax(V3 a, V3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

bool readFile(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// ---------------------------------------------------------------- glb
struct Glb {
    Json j;
    std::string bin;
    bool load(const std::string& path) {
        std::string d;
        if (!readFile(path, d) || d.size() < 20) return false;
        uint32_t jl; std::memcpy(&jl, d.data() + 12, 4);
        if (!Json::parse(d.data() + 20, jl, j)) return false;
        size_t o = 20 + jl;
        if (o + 8 <= d.size()) { uint32_t bl; std::memcpy(&bl, d.data() + o, 4); bin = d.substr(o + 8, bl); }
        return true;
    }
    // accessor -> floats (n components per element) / indices
    bool floats(int acc, int n, std::vector<float>& out) const {
        const Json& a = j["accessors"][(size_t)acc];
        const Json& bv = j["bufferViews"][(size_t)a["bufferView"].asInt()];
        if (a["componentType"].asInt() != 5126) return false;
        const size_t count = (size_t)a["count"].asInt(), off = (size_t)bv["byteOffset"].asInt() + (size_t)a["byteOffset"].asInt();
        const size_t stride = bv.has("byteStride") ? (size_t)bv["byteStride"].asInt() : (size_t)n * 4;
        out.resize(count * (size_t)n);
        for (size_t i = 0; i < count; ++i) std::memcpy(&out[i * (size_t)n], bin.data() + off + i * stride, (size_t)n * 4);
        return true;
    }
    bool indices(int acc, std::vector<uint32_t>& out) const {
        const Json& a = j["accessors"][(size_t)acc];
        const Json& bv = j["bufferViews"][(size_t)a["bufferView"].asInt()];
        const size_t count = (size_t)a["count"].asInt(), off = (size_t)bv["byteOffset"].asInt() + (size_t)a["byteOffset"].asInt();
        out.resize(count);
        const int ct = a["componentType"].asInt();
        for (size_t i = 0; i < count; ++i) {
            if (ct == 5125) { uint32_t v; std::memcpy(&v, bin.data() + off + i * 4, 4); out[i] = v; }
            else if (ct == 5123) { uint16_t v; std::memcpy(&v, bin.data() + off + i * 2, 2); out[i] = v; }
            else if (ct == 5121) out[i] = (uint8_t)bin[off + i];
            else return false;
        }
        return true;
    }
};

// node world matrix (column-major), "matrix" or TRS
void nodeMatrix(const Json& n, float m[16]) {
    static const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(m, I, sizeof I);
    if (n.has("matrix")) { for (int i = 0; i < 16; ++i) m[i] = n["matrix"][(size_t)i].asFloat(); return; }
    float t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
    for (int i = 0; i < 3; ++i) { t[i] = n["translation"][(size_t)i].asFloat(0); s[i] = n["scale"][(size_t)i].asFloat(1); }
    for (int i = 0; i < 4; ++i) q[i] = n["rotation"][(size_t)i].asFloat(i == 3 ? 1.0f : 0.0f);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 2 * (x * y - z * w), 1 - 2 * (x * x + z * z),
                        2 * (y * z + x * w), 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
    for (int c = 0; c < 3; ++c) for (int rr = 0; rr < 3; ++rr) m[c * 4 + rr] = r[c * 3 + rr] * s[c];
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
}
V3 xformP(const float* m, V3 p) { return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]}; }
V3 xformN(const float* m, V3 n) {   // inverse-transpose of the 3x3 (via cofactors: proportional is enough, normalised after)
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g, D = -(b * i - c * h), E = a * i - c * g, F = -(a * h - b * g),
                G = b * f - c * e, H = -(a * f - c * d), K = a * e - b * d;
    return norm(V3{A * n.x + D * n.y + G * n.z, B * n.x + E * n.y + H * n.z, C * n.x + F * n.y + K * n.z});
}

// ---------------------------------------------------------------- BVH (binned SAH, any-hit)
struct Tri { V3 a, e1, e2; };
struct Node { V3 lo, hi; uint32_t left = 0, first = 0, count = 0; };   // count > 0: leaf
struct Bvh {
    std::vector<Tri> tris;
    std::vector<uint32_t> idx;
    std::vector<Node> nodes;
    std::vector<V3> cen, blo, bhi;
    void build() {
        const size_t n = tris.size();
        idx.resize(n); cen.resize(n); blo.resize(n); bhi.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const Tri& t = tris[i];
            const V3 b = t.a + t.e1, c = t.a + t.e2;
            blo[i] = vmin(t.a, vmin(b, c)); bhi[i] = vmax(t.a, vmax(b, c));
            cen[i] = (blo[i] + bhi[i]) * 0.5f; idx[i] = (uint32_t)i;
        }
        nodes.reserve(n * 2 / 3 + 16);
        nodes.push_back(Node{});
        split(0, 0, (uint32_t)n);
    }
    void bounds(Node& nd, uint32_t first, uint32_t count) {
        V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (uint32_t k = first; k < first + count; ++k) { lo = vmin(lo, blo[idx[k]]); hi = vmax(hi, bhi[idx[k]]); }
        nd.lo = lo; nd.hi = hi;
    }
    static float area(V3 lo, V3 hi) { V3 d = hi - lo; return d.x < 0 ? 0 : 2 * (d.x * d.y + d.y * d.z + d.z * d.x); }
    void split(uint32_t ni, uint32_t first, uint32_t count) {
        bounds(nodes[ni], first, count);
        if (count <= 4) { nodes[ni].first = first; nodes[ni].count = count; return; }
        V3 clo{1e30f, 1e30f, 1e30f}, chi{-1e30f, -1e30f, -1e30f};
        for (uint32_t k = first; k < first + count; ++k) { clo = vmin(clo, cen[idx[k]]); chi = vmax(chi, cen[idx[k]]); }
        constexpr int NB = 12;
        float bestCost = 1e30f; int bestAxis = -1, bestB = 0;
        for (int ax = 0; ax < 3; ++ax) {
            const float lo = ax == 0 ? clo.x : ax == 1 ? clo.y : clo.z, hi = ax == 0 ? chi.x : ax == 1 ? chi.y : chi.z;
            if (hi - lo < 1e-6f) continue;
            V3 bl[NB], bh[NB]; uint32_t bc[NB] = {};
            for (int b = 0; b < NB; ++b) { bl[b] = {1e30f, 1e30f, 1e30f}; bh[b] = {-1e30f, -1e30f, -1e30f}; }
            const float sc = NB / (hi - lo);
            for (uint32_t k = first; k < first + count; ++k) {
                const uint32_t t = idx[k];
                const float c = ax == 0 ? cen[t].x : ax == 1 ? cen[t].y : cen[t].z;
                int b = std::min(NB - 1, (int)((c - lo) * sc));
                ++bc[b]; bl[b] = vmin(bl[b], blo[t]); bh[b] = vmax(bh[b], bhi[t]);
            }
            for (int s = 1; s < NB; ++s) {
                V3 l0{1e30f, 1e30f, 1e30f}, h0{-1e30f, -1e30f, -1e30f}, l1 = l0, h1 = h0; uint32_t c0 = 0, c1 = 0;
                for (int b = 0; b < s; ++b) if (bc[b]) { l0 = vmin(l0, bl[b]); h0 = vmax(h0, bh[b]); c0 += bc[b]; }
                for (int b = s; b < NB; ++b) if (bc[b]) { l1 = vmin(l1, bl[b]); h1 = vmax(h1, bh[b]); c1 += bc[b]; }
                if (!c0 || !c1) continue;
                const float cost = area(l0, h0) * c0 + area(l1, h1) * c1;
                if (cost < bestCost) { bestCost = cost; bestAxis = ax; bestB = s; }
            }
        }
        uint32_t mid;
        if (bestAxis < 0) mid = first + count / 2;               // (all centroids equal: split in half)
        else {
            const float lo = bestAxis == 0 ? clo.x : bestAxis == 1 ? clo.y : clo.z, hi = bestAxis == 0 ? chi.x : bestAxis == 1 ? chi.y : chi.z;
            const float sc = NB / (hi - lo);
            auto it = std::partition(idx.begin() + first, idx.begin() + first + count, [&](uint32_t t) {
                const float c = bestAxis == 0 ? cen[t].x : bestAxis == 1 ? cen[t].y : cen[t].z;
                return std::min(NB - 1, (int)((c - lo) * sc)) < bestB;
            });
            mid = (uint32_t)(it - idx.begin());
            if (mid == first || mid == first + count) mid = first + count / 2;
        }
        const uint32_t l = (uint32_t)nodes.size();
        nodes.push_back(Node{}); nodes.push_back(Node{});
        nodes[ni].left = l; nodes[ni].count = 0;
        split(l, first, mid - first);
        split(l + 1, mid, first + count - mid);
    }
    static bool slab(const Node& nd, V3 o, V3 inv, float tmax) {
        float t0 = (nd.lo.x - o.x) * inv.x, t1 = (nd.hi.x - o.x) * inv.x; float tn = std::min(t0, t1), tf = std::max(t0, t1);
        t0 = (nd.lo.y - o.y) * inv.y; t1 = (nd.hi.y - o.y) * inv.y; tn = std::max(tn, std::min(t0, t1)); tf = std::min(tf, std::max(t0, t1));
        t0 = (nd.lo.z - o.z) * inv.z; t1 = (nd.hi.z - o.z) * inv.z; tn = std::max(tn, std::min(t0, t1)); tf = std::min(tf, std::max(t0, t1));
        return tf >= std::max(tn, 0.0f) && tn <= tmax;
    }
    bool occluded(V3 o, V3 d, float tmax) const {
        const V3 inv{1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f), 1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f)};
        uint32_t stack[96]; int sp = 0; stack[sp++] = 0;
        while (sp) {
            const Node& nd = nodes[stack[--sp]];
            if (!slab(nd, o, inv, tmax)) continue;
            if (nd.count) {
                for (uint32_t k = nd.first; k < nd.first + nd.count; ++k) {   // Moller-Trumbore, both faces
                    const Tri& t = tris[idx[k]];
                    const V3 p = cross(d, t.e2);
                    const float det = dot(t.e1, p);
                    if (std::fabs(det) < 1e-12f) continue;
                    const float id = 1.0f / det;
                    const V3 s = o - t.a;
                    const float u = dot(s, p) * id;
                    if (u < 0 || u > 1) continue;
                    const V3 q = cross(s, t.e1);
                    const float v = dot(d, q) * id;
                    if (v < 0 || u + v > 1) continue;
                    const float tt = dot(t.e2, q) * id;
                    if (tt > 1e-4f && tt < tmax) return true;
                }
            } else if (sp + 2 <= 96) { stack[sp++] = nd.left; stack[sp++] = nd.left + 1; }
        }
        return false;
    }
};

// ---------------------------------------------------------------- receivers
struct Atlas {
    std::string name; int w = 0, h = 0;
    std::vector<uint8_t> cov;          // 0 none, 1 rasterised, 2 dilated
    std::vector<float> dist;           // rasterisation: texel-centre distance to the triangle (pixels)
    std::vector<float> V, ndl, lt;     // lt: 3 per texel (sun in the tangent frame: T, B, N)
    std::vector<int32_t> comp;
    std::vector<V3> pos, nrm, dpx, dpy; // world position / normal, world offset per texel pixel (x / y)
    std::vector<V3> tng, btg;          // the tangent frame (B with the UV0 handedness), as lt uses
};

struct LightRec { int32_t light; float V, d, lt[3], cosAxis; };
struct LightSrc { std::string name, cls; V3 pos, axis; float radius; bool shadows; };
struct LightSample { int32_t x, y; float sky[6]; uint32_t start, count; };   // sky[3..5]: the tangent y mirrored

bool pngSize(const std::string& p, int& w, int& h) {
    std::ifstream f(p, std::ios::binary); unsigned char b[24];
    if (!f.read((char*)b, 24) || b[1] != 'P' || b[2] != 'N' || b[3] != 'G') return false;
    w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19]; h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
    return true;
}

struct Args { std::string world, bsp, render, out, sunFrom; std::vector<std::string> occluders, lightsFrom; float coneDeg = 0.27f, bias = 0.02f, lightRange = 1.5f; int ss = 4, threads = 2, lights = 0, skyRays = 64, lightShadows = 1; };

inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float rnd01(uint32_t& s) { s = hash32(s + 0x9e3779b9U); return (s >> 8) * (1.0f / 16777216.0f); }

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

}  // namespace

int main(int argc, char** argv) {
    Args A;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--world") A.world = v; else if (k == "--bsp") A.bsp = v; else if (k == "--render") A.render = v; else if (k == "--out") A.out = v;
        else if (k == "--cone") A.coneDeg = (float)std::atof(v.c_str()); else if (k == "--ss") A.ss = std::max(1, std::atoi(v.c_str()));
        else if (k == "--threads") A.threads = std::max(1, std::atoi(v.c_str())); else if (k == "--bias") A.bias = (float)std::atof(v.c_str());
        else if (k == "--lights") A.lights = std::max(0, std::atoi(v.c_str())); else if (k == "--sky-rays") A.skyRays = std::max(4, std::atoi(v.c_str()));
        else if (k == "--light-range") A.lightRange = (float)std::atof(v.c_str()); else if (k == "--light-shadows") A.lightShadows = std::atoi(v.c_str());
        else if (k == "--occluder") A.occluders.push_back(v); else if (k == "--sun-from") A.sunFrom = v;
        else if (k == "--lights-from") A.lightsFrom.push_back(v);
    }
    if (A.world.empty() || A.render.empty() || A.out.empty()) {
        std::printf("usage: vbake_trace --world <world.glb> --bsp <bsp.glb> --render <render map dir> --out <dir> [--cone 0.27] [--ss 4] [--threads 2]\n");
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    // ---- lighting.json: the sun, lightmap table, shadow flags
    std::string lj;
    Json L;
    if (!readFile(A.render + "/lighting.json", lj) || !Json::parse(lj, L)) { std::printf("lighting.json unreadable\n"); return 1; }
    lj.clear(); lj.shrink_to_fit();
    const Json* sun = nullptr;
    Json SL;   // (--sun-from: another unit's lighting.json carries the chapter's sun)
    if (!A.sunFrom.empty()) {
        std::string sj;
        if (!readFile(A.sunFrom, sj) || !Json::parse(sj, SL)) { std::printf("--sun-from %s unreadable\n", A.sunFrom.c_str()); return 1; }
    }
    const Json& LS = A.sunFrom.empty() ? L : SL;
    for (size_t i = 0; i < LS["lights"].size(); ++i) {
        const Json& l = LS["lights"][i];
        if (l["class"].asString() != "DirectionalLightComponent" || !l["enabled"].asBool(true) || !l["built_into_lightmap"].asBool(false) ||
            !l["cast_static_shadows"].asBool(true)) continue;
        if (!sun || l["brightness"].asDouble() > (*sun)["brightness"].asDouble()) sun = &l;
    }
    if (!sun) { std::printf("no baked, shadow-casting directional light in lighting.json\n"); return 1; }
    const V3 sunDir = norm(V3{-(*sun)["direction"][0u].asFloat(), -(*sun)["direction"][1u].asFloat(), -(*sun)["direction"][2u].asFloat()});   // towards the sun
    std::set<std::string> noShadow;
    for (const auto& kv : L["component_flags"].obj) if (kv.second["no_shadow"].asBool(false) || kv.second["hidden"].asBool(false)) noShadow.insert(kv.first);
    const Json& props = L["lightmaps"]["props"];
    std::map<std::string, int> atlasIdx;
    std::vector<Atlas> atlases;
    std::vector<std::string> comps;
    std::map<std::string, int> compIdx;
    auto atlasOf = [&](const std::string& comp, float sc[2], float bi[2]) -> int {
        const Json& p = props[comp];
        if (!p.isObject()) return -1;
        const std::string a = p["coeffs"][0u].asString();
        sc[0] = p["coordScale"][0u].asFloat(); sc[1] = p["coordScale"][1u].asFloat();
        bi[0] = p["coordBias"][0u].asFloat(); bi[1] = p["coordBias"][1u].asFloat();
        auto it = atlasIdx.find(a);
        if (it != atlasIdx.end()) return it->second;
        Atlas at; at.name = a;
        if (!pngSize(A.render + "/lightmaps/" + a + ".png", at.w, at.h)) { std::printf("no lightmap page %s\n", a.c_str()); return -1; }
        const size_t n = (size_t)at.w * at.h;
        at.cov.assign(n, 0); at.dist.assign(n, 1e9f); at.V.assign(n, 0); at.ndl.assign(n, 0); at.lt.assign(n * 3, 0); at.comp.assign(n, -1);
        at.pos.assign(n, V3{}); at.nrm.assign(n, V3{}); at.dpx.assign(n, V3{}); at.dpy.assign(n, V3{});
        if (A.lights > 0) { at.tng.assign(n, V3{}); at.btg.assign(n, V3{}); }
        atlases.push_back(std::move(at));
        atlasIdx[a] = (int)atlases.size() - 1;
        return (int)atlases.size() - 1;
    };
    // ---- geometry
    Bvh bvh;
    size_t recvTris = 0, skippedNoUv = 0, occTris = 0, skippedBlend = 0, skippedShadow = 0, skippedSky = 0;
    auto addFile = [&](const std::string& path, bool isBspFile) -> bool {
        Glb g;
        if (!g.load(path)) { std::printf("cannot read %s\n", path.c_str()); return false; }
        const Json& J = g.j;
        for (size_t ni = 0; ni < J["nodes"].size(); ++ni) {
            const Json& nd = J["nodes"][ni];
            if (!nd.has("mesh")) continue;
            const Json& ex = nd["extras"];
            if (!isBspFile && ex["kind"].asString() == "bsp") continue;            // (bsp.glb carries the BSP with lightmap UVs)
            const std::string comp = ex["component"].asString(), meshName = lower(ex["mesh"].asString());
            // (the runtime TLAS rule, WfcRt.cpp rtBuildWorld: named sky / dome, or over 2 km - D5_OME's 9 km SpaceDome closed the
            // arena in the trace while the runtime leaves it out: a "sunless" fit for an arena open to the sky)
            const bool sky = meshName.find("sky") != std::string::npos || meshName.find("dome") != std::string::npos;
            const bool shadow = !noShadow.count(comp) && !(ex.has("actor") && noShadow.count("actor:" + ex["actor"].asString()));
            float M[16]; nodeMatrix(nd, M);
            float sc[2] = {0, 0}, bi[2] = {0, 0};
            const int ai = comp.empty() ? -1 : atlasOf(comp, sc, bi);
            int ci = -1;
            if (ai >= 0) { auto it = compIdx.find(comp); if (it == compIdx.end()) { ci = (int)comps.size(); compIdx[comp] = ci; comps.push_back(comp); } else ci = it->second; }
            const Json& mesh = J["meshes"][(size_t)nd["mesh"].asInt()];
            for (size_t pi = 0; pi < mesh["primitives"].size(); ++pi) {
                const Json& pr = mesh["primitives"][pi];
                if (pr["mode"].asInt(4) != 4 || !pr.has("indices")) continue;
                const std::string blend = pr.has("material") ? J["materials"][(size_t)pr["material"].asInt()]["extras"]["blend_mode"].asString() : "BLEND_Opaque";
                const bool opaque = blend.empty() || blend == "BLEND_Opaque" || blend == "BLEND_Masked";
                std::vector<float> P, N, U0, U1; std::vector<uint32_t> I;
                if (!g.floats(pr["attributes"]["POSITION"].asInt(), 3, P) || !g.indices(pr["indices"].asInt(), I)) continue;
                const bool hasN = pr["attributes"].has("NORMAL") && g.floats(pr["attributes"]["NORMAL"].asInt(), 3, N);
                const bool hasU0 = pr["attributes"].has("TEXCOORD_0") && g.floats(pr["attributes"]["TEXCOORD_0"].asInt(), 2, U0);
                const bool hasU1 = pr["attributes"].has("TEXCOORD_1") && g.floats(pr["attributes"]["TEXCOORD_1"].asInt(), 2, U1);
                std::vector<V3> WP(P.size() / 3), WN(P.size() / 3);
                for (size_t v = 0; v < WP.size(); ++v) {
                    WP[v] = xformP(M, V3{P[v * 3], P[v * 3 + 1], P[v * 3 + 2]});
                    WN[v] = hasN ? xformN(M, V3{N[v * 3], N[v * 3 + 1], N[v * 3 + 2]}) : V3{0, 1, 0};
                }
                bool skyPrim = sky;
                if (!skyPrim && !WP.empty()) {
                    V3 lo = WP[0], hi = WP[0];
                    for (const V3& q : WP) { lo = V3{std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)}; hi = V3{std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)}; }
                    skyPrim = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) > 2000.0f;
                }
                for (size_t t = 0; t + 2 < I.size(); t += 3) {
                    const uint32_t i0 = I[t], i1 = I[t + 1], i2 = I[t + 2];
                    if (i0 >= WP.size() || i1 >= WP.size() || i2 >= WP.size()) continue;
                    const V3 a = WP[i0], b = WP[i1], c = WP[i2];
                    if (!opaque) ++skippedBlend;
                    else if (!shadow) ++skippedShadow;
                    else if (skyPrim) ++skippedSky;
                    else { bvh.tris.push_back(Tri{a, b - a, c - a}); ++occTris; }
                    if (ai < 0 || !opaque) continue;                              // ---- a lightmapped receiver
                    if (!hasU1) { ++skippedNoUv; continue; }
                    Atlas& at = atlases[(size_t)ai];
                    float px[3], py[3];
                    const uint32_t ii[3] = {i0, i1, i2};
                    for (int k = 0; k < 3; ++k) {
                        px[k] = (U1[ii[k] * 2] * sc[0] + bi[0]) * at.w - 0.5f;    // (texel centres at integers)
                        py[k] = (U1[ii[k] * 2 + 1] * sc[1] + bi[1]) * at.h - 0.5f;
                    }
                    const float den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
                    if (std::fabs(den) < 1e-12f) continue;
                    ++recvTris;
                    // the world offset per texel pixel (dP/dx, dP/dy) and the tangent frame (from UV0)
                    const float ux1 = px[1] - px[0], uy1 = py[1] - py[0], ux2 = px[2] - px[0], uy2 = py[2] - py[0];
                    const float idet = 1.0f / (ux1 * uy2 - ux2 * uy1);
                    const V3 e1 = b - a, e2 = c - a;
                    const V3 dPdx = (e1 * uy2 - e2 * uy1) * idet, dPdy = (e2 * ux1 - e1 * ux2) * idet;
                    V3 T{1, 0, 0}; float hand = 1.0f;
                    if (hasU0) {
                        const float s1 = U0[i1 * 2] - U0[i0 * 2], t1 = U0[i1 * 2 + 1] - U0[i0 * 2 + 1], s2 = U0[i2 * 2] - U0[i0 * 2], t2 = U0[i2 * 2 + 1] - U0[i0 * 2 + 1];
                        const float r = s1 * t2 - s2 * t1;
                        if (std::fabs(r) > 1e-20f) {
                            const V3 tt = (e1 * t2 - e2 * t1) * (1.0f / r), bb = (e2 * s1 - e1 * s2) * (1.0f / r);
                            T = tt; hand = dot(cross(norm(cross(e1, e2)), tt), bb) < 0 ? -1.0f : 1.0f;
                        }
                    }
                    const int x0 = std::max(0, (int)std::floor(std::min({px[0], px[1], px[2]}) - 1)), x1 = std::min(at.w - 1, (int)std::ceil(std::max({px[0], px[1], px[2]}) + 1));
                    const int y0 = std::max(0, (int)std::floor(std::min({py[0], py[1], py[2]}) - 1)), y1 = std::min(at.h - 1, (int)std::ceil(std::max({py[0], py[1], py[2]}) + 1));
                    for (int y = y0; y <= y1; ++y)
                        for (int x = x0; x <= x1; ++x) {
                            float l0 = ((py[1] - py[2]) * (x - px[2]) + (px[2] - px[1]) * (y - py[2])) / den;
                            float l1 = ((py[2] - py[0]) * (x - px[2]) + (px[0] - px[2]) * (y - py[2])) / den;
                            float l2 = 1.0f - l0 - l1;
                            // distance (pixels) outside the triangle: 0 inside; conservative coverage = within 0.75 px
                            float d = 0.0f;
                            if (l0 < 0 || l1 < 0 || l2 < 0) {
                                float best = 1e9f;
                                for (int e = 0; e < 3; ++e) {
                                    const float ax_ = px[e], ay_ = py[e], bx_ = px[(e + 1) % 3], by_ = py[(e + 1) % 3];
                                    const float ex_ = bx_ - ax_, ey_ = by_ - ay_, ll = ex_ * ex_ + ey_ * ey_;
                                    float tt = ll > 0 ? ((x - ax_) * ex_ + (y - ay_) * ey_) / ll : 0; tt = std::clamp(tt, 0.0f, 1.0f);
                                    const float qx = ax_ + ex_ * tt - x, qy = ay_ + ey_ * tt - y;
                                    best = std::min(best, std::sqrt(qx * qx + qy * qy));
                                }
                                d = best;
                            }
                            if (d > 0.75f) continue;
                            const size_t o = (size_t)y * at.w + x;
                            if (d >= at.dist[o]) continue;
                            at.dist[o] = d;
                            l0 = std::max(l0, 0.0f); l1 = std::max(l1, 0.0f); l2 = std::max(l2, 0.0f);
                            const float s = l0 + l1 + l2; l0 /= s; l1 /= s; l2 /= s;
                            const V3 P_ = a * l0 + b * l1 + c * l2;
                            const V3 N_ = norm(WN[i0] * l0 + WN[i1] * l1 + WN[i2] * l2);
                            const V3 T_ = norm(T - N_ * dot(N_, T));
                            const V3 B_ = cross(N_, T_) * hand;
                            at.cov[o] = 1; at.comp[o] = ci; at.pos[o] = P_; at.nrm[o] = N_; at.dpx[o] = dPdx; at.dpy[o] = dPdy;
                            at.lt[o * 3] = dot(sunDir, T_); at.lt[o * 3 + 1] = dot(sunDir, B_); at.lt[o * 3 + 2] = dot(sunDir, N_);
                            at.ndl[o] = dot(sunDir, N_);
                            if (A.lights > 0) { at.tng[o] = T_; at.btg[o] = B_; }
                        }
                }
            }
        }
        return true;
    };
    if (!addFile(A.world, false) || (!A.bsp.empty() && !addFile(A.bsp, true))) return 1;
    for (const std::string& o : A.occluders)                                       // (chapter: the other units, blocking only)
        if (!addFile(o, o.size() >= 7 && lower(o.substr(o.size() - 7)) == "bsp.glb")) return 1;
    const auto t1 = std::chrono::steady_clock::now();
    bvh.build();
    const auto t2 = std::chrono::steady_clock::now();
    std::printf("sun %s, toward (%.3f %.3f %.3f); occluders %zu tris (skipped: %zu non-opaque, %zu no-shadow, %zu sky); receivers %zu tris "
                "in %zu components on %zu pages (%zu without lightmap UVs); BVH %zu nodes, %.1f s load + %.1f s build\n",
                (*sun)["name"].asString().c_str(), sunDir.x, sunDir.y, sunDir.z, occTris, skippedBlend, skippedShadow, skippedSky, recvTris,
                comps.size(), atlases.size(), skippedNoUv, bvh.nodes.size(),
                std::chrono::duration<double>(t1 - t0).count(), std::chrono::duration<double>(t2 - t1).count());
    // ---- trace: per covered texel ss x ss sub-texel positions, each with its own direction in the sun cone (stratified)
    const float tanCone = std::tan(A.coneDeg * 3.14159265f / 180.0f);
    V3 su = norm(cross(std::fabs(sunDir.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0}, sunDir)), sv = cross(sunDir, su);
    std::vector<std::pair<int, size_t>> work;
    for (size_t ai = 0; ai < atlases.size(); ++ai)
        for (size_t o = 0; o < atlases[ai].cov.size(); ++o) if (atlases[ai].cov[o] == 1) work.push_back({(int)ai, o});
    std::atomic<size_t> next{0};
    std::atomic<uint64_t> rays{0};
    auto worker = [&]() {
        uint64_t local = 0;
        for (;;) {
            const size_t w = next.fetch_add(256);
            if (w >= work.size()) break;
            for (size_t k = w; k < std::min(work.size(), w + 256); ++k) {
                Atlas& at = atlases[(size_t)work[k].first];
                const size_t o = work[k].second;
                if (at.ndl[o] <= 0.0f) { at.V[o] = 0.0f; continue; }             // (facing away: no sun in the bake either)
                int vis = 0;
                const int n = A.ss * A.ss;
                for (int s = 0; s < n; ++s) {
                    const float fx = ((s % A.ss) + 0.5f) / A.ss - 0.5f, fy = ((s / A.ss) + 0.5f) / A.ss - 0.5f;
                    // cone direction: a stratified point on the disk (the sub-texel index permuted so x/y and the disk decorrelate)
                    const int sp = (s * 7 + 3) % n;
                    const float r = std::sqrt((sp + 0.5f) / n) * tanCone, phi = 2.39996323f * sp;   // (golden-angle spiral)
                    const V3 d = norm(sunDir + su * (r * std::cos(phi)) + sv * (r * std::sin(phi)));
                    const V3 p = at.pos[o] + at.dpx[o] * fx + at.dpy[o] * fy;
                    const V3 org = p + at.nrm[o] * A.bias + d * A.bias;
                    if (!bvh.occluded(org, d, 4000.0f)) ++vis;
                    ++local;
                }
                at.V[o] = (float)vis / (float)n;
            }
        }
        rays += local;
    };
    std::vector<std::thread> th;
    for (int i = 0; i < A.threads; ++i) th.emplace_back(worker);
    for (auto& t : th) t.join();
    const auto t3 = std::chrono::steady_clock::now();
    // ---- --lights: the sky and the point / spot lights on every stride-th covered texel (see the header)
    if (A.lights > 0) {
        std::vector<LightSrc> srcs;
        std::vector<Json> LF(A.lightsFrom.size());                                 // (--lights-from: the chapter's other units)
        for (size_t f = 0; f < LF.size(); ++f) {
            std::string fj;
            if (!readFile(A.lightsFrom[f], fj) || !Json::parse(fj, LF[f])) { std::printf("--lights-from %s unreadable\n", A.lightsFrom[f].c_str()); return 1; }
        }
        std::vector<const Json*> lightList;
        for (size_t i = 0; i < L["lights"].size(); ++i) lightList.push_back(&L["lights"][i]);
        for (const Json& J2 : LF) for (size_t i = 0; i < J2["lights"].size(); ++i) lightList.push_back(&J2["lights"][i]);
        for (const Json* lp : lightList) {
            const Json& l = *lp;
            const std::string cls = l["class"].asString();
            if ((cls != "PointLightComponent" && cls != "SpotLightComponent") || !l["enabled"].asBool(true)) continue;
            LightSrc ls; ls.name = l["name"].asString(); ls.cls = cls;
            ls.pos = V3{l["position"][0u].asFloat(), l["position"][1u].asFloat(), l["position"][2u].asFloat()};
            ls.axis = norm(V3{l["direction"][0u].asFloat(), l["direction"][1u].asFloat(), l["direction"][2u].asFloat()});
            ls.radius = l["radius_m"].asFloat(10.24f); ls.shadows = A.lightShadows && l["cast_static_shadows"].asBool(true) && l["cast_shadows"].asBool(true);   // (--light-shadows 0: unshadowed, a diagnostic)
            srcs.push_back(ls);
        }
        const float Bs[3][3] = {{0.0f, 0.81649658f, 0.57735027f}, {-0.70710678f, -0.40824829f, 0.57735027f}, {0.70710678f, -0.40824829f, 0.57735027f}};
        size_t totSamples = 0, totRecs = 0;
        std::atomic<uint64_t> lrays{0};
        std::string idx = "{\n \"stride\": " + std::to_string(A.lights) + ",\n \"sky_rays\": " + std::to_string(A.skyRays) +
                          ",\n \"light_range\": " + std::to_string(A.lightRange) + ",\n \"lights\": [";
        for (size_t j = 0; j < srcs.size(); ++j) idx += std::string(j ? ",\n  " : "\n  ") + "{\"index\": " + std::to_string(j) + ", \"name\": \"" + srcs[j].name + "\", \"class\": \"" + srcs[j].cls + "\"}";
        idx += "\n ]\n}\n";
        std::ofstream(A.out + "/lights_index.json") << idx;
        for (Atlas& at : atlases) {
            std::vector<LightSample> samples;
            for (int y = 0; y < at.h; y += A.lights)
                for (int x = 0; x < at.w; x += A.lights) {
                    const size_t o = (size_t)y * at.w + x;
                    if (at.cov[o] == 1) samples.push_back(LightSample{x, y, {0, 0, 0, 0, 0, 0}, 0, 0});
                }
            std::vector<std::vector<LightRec>> recs(samples.size());
            std::atomic<size_t> nx{0};
            auto lw = [&]() {
                uint64_t local = 0;
                for (;;) {
                    const size_t w0 = nx.fetch_add(64);
                    if (w0 >= samples.size()) break;
                    for (size_t si = w0; si < std::min(samples.size(), w0 + 64); ++si) {
                        LightSample& sm = samples[si];
                        const size_t o = (size_t)sm.y * at.w + sm.x;
                        const V3 N = at.nrm[o], T = at.tng[o], Bt = at.btg[o], P = at.pos[o] + N * A.bias;
                        uint32_t seed = hash32((uint32_t)o * 2654435761U ^ (uint32_t)at.w);
                        // sky: stratified uniform hemisphere about N (cos theta uniform), rows x cols strata
                        const int rows = std::max(2, (int)std::sqrt((float)A.skyRays / 2.0f)), cols = std::max(2, A.skyRays / rows);
                        const int n = rows * cols;
                        for (int r = 0; r < rows; ++r)
                            for (int c = 0; c < cols; ++c) {
                                const float ct = (r + rnd01(seed)) / rows, st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
                                const float ph = 6.2831853f * (c + rnd01(seed)) / cols;
                                const float tx = st * std::cos(ph), ty = st * std::sin(ph);
                                const V3 d = norm(T * tx + Bt * ty + N * ct);
                                if (d.y <= 0.0f) continue;                                   // below the horizon: not sky
                                ++local;
                                if (bvh.occluded(P + d * A.bias, d, 4000.0f)) continue;
                                for (int i = 0; i < 3; ++i) { sm.sky[i] += std::max(0.0f, Bs[i][0] * tx + Bs[i][1] * ty + Bs[i][2] * ct); sm.sky[3 + i] += std::max(0.0f, Bs[i][0] * tx - Bs[i][1] * ty + Bs[i][2] * ct); }
                            }
                        for (int i = 0; i < 6; ++i) sm.sky[i] *= 2.0f / n;
                        // point / spot lights
                        for (size_t j = 0; j < srcs.size(); ++j) {
                            const LightSrc& ls = srcs[j];
                            const V3 toL = ls.pos - P;
                            const float d = len(toL);
                            if (d < 1e-3f || d > ls.radius * A.lightRange) continue;
                            const V3 Ld = toL * (1.0f / d);
                            if (dot(Ld, N) <= 0.0f) continue;
                            float V = 1.0f;
                            if (ls.shadows) {
                                int vis = 0;
                                for (int k = 0; k < 4; ++k) {
                                    V3 tgt = ls.pos;
                                    if (k) tgt = tgt + V3{rnd01(seed) - 0.5f, rnd01(seed) - 0.5f, rnd01(seed) - 0.5f} * 0.2f;
                                    const V3 dv = tgt - P; const float dl = len(dv);
                                    const V3 dn = dv * (1.0f / dl);
                                    if (!bvh.occluded(P + dn * A.bias, dn, std::max(0.0f, dl - 2.0f * A.bias))) ++vis;
                                    ++local;
                                }
                                V = vis / 4.0f;
                            }
                            if (V <= 0.0f) continue;
                            LightRec rc; rc.light = (int32_t)j; rc.V = V; rc.d = d;
                            rc.lt[0] = dot(Ld, T); rc.lt[1] = dot(Ld, Bt); rc.lt[2] = dot(Ld, N);
                            rc.cosAxis = dot(ls.axis, Ld * -1.0f);
                            recs[si].push_back(rc);
                        }
                    }
                }
                lrays += local;
            };
            std::vector<std::thread> lt_;
            for (int i = 0; i < A.threads; ++i) lt_.emplace_back(lw);
            for (auto& t : lt_) t.join();
            // writeLights: int32 w, h, stride, nsamples; per sample int32 x, y; f32 sky[6] (as traced, tangent y mirrored); u32 start, count;
            // int32 nrec; per record int32 light, f32 V, d, lt[3], cosAxis (lights_index.json: the light names)
            std::vector<LightRec> flat;
            for (size_t si = 0; si < samples.size(); ++si) {
                samples[si].start = (uint32_t)flat.size(); samples[si].count = (uint32_t)recs[si].size();
                flat.insert(flat.end(), recs[si].begin(), recs[si].end());
            }
            std::ofstream f(A.out + "/lights_" + at.name + ".bin", std::ios::binary);
            const int32_t hd[4] = {at.w, at.h, A.lights, (int32_t)samples.size()};
            f.write((const char*)hd, 16);
            f.write((const char*)samples.data(), (std::streamsize)(samples.size() * sizeof(LightSample)));
            const int32_t nr = (int32_t)flat.size();
            f.write((const char*)&nr, 4);
            f.write((const char*)flat.data(), (std::streamsize)(flat.size() * sizeof(LightRec)));
            totSamples += samples.size(); totRecs += flat.size();
        }
        std::printf("lights: %zu point / spot lights; %zu texel samples (stride %d), %zu light records, %llu rays, %.1f s\n", srcs.size(),
                    totSamples, A.lights, totRecs, (unsigned long long)lrays.load(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t3).count());
    }
    // ---- dilate 2 texels (bilinear sampling at chart borders reads them): neighbour average, same component kept
    for (Atlas& at : atlases)
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<uint8_t> cov = at.cov;
            for (int y = 0; y < at.h; ++y)
                for (int x = 0; x < at.w; ++x) {
                    const size_t o = (size_t)y * at.w + x;
                    if (cov[o]) continue;
                    float sum = 0; int n = 0, comp = -1, src = -1;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= at.w || yy >= at.h) continue;
                        const size_t q = (size_t)yy * at.w + xx;
                        if (!cov[q]) continue;
                        sum += at.V[q]; ++n; if (comp < 0) { comp = at.comp[q]; src = (int)q; }
                    }
                    if (!n) continue;
                    at.V[o] = sum / n; at.comp[o] = comp; at.cov[o] = 2;
                    at.ndl[o] = at.ndl[(size_t)src]; for (int c = 0; c < 3; ++c) at.lt[o * 3 + c] = at.lt[(size_t)src * 3 + c];
                }
        }
    // ---- write: per page a binary (w, h, then cov u8[n], V f32[n], ndl f32[n], lt f32[3n], comp i32[n]) + summary json
    size_t covered = 0, lit = 0;
    std::string summary = "{\n \"map_render_dir\": \"" + A.render + "\",\n \"sun\": \"" + (*sun)["name"].asString() + "\",\n";
    char buf[512];
    std::snprintf(buf, sizeof buf, " \"sun_dir\": [%.6f, %.6f, %.6f],\n \"cone_deg\": %.4f,\n \"supersample\": %d,\n \"bias_m\": %.4f,\n",
                  sunDir.x, sunDir.y, sunDir.z, A.coneDeg, A.ss, A.bias);
    summary += buf;
    summary += " \"components\": [";
    for (size_t i = 0; i < comps.size(); ++i) summary += (i ? ",\n  \"" : "\n  \"") + comps[i] + "\"";
    summary += "\n ],\n \"pages\": [";
    for (size_t ai = 0; ai < atlases.size(); ++ai) {
        Atlas& at = atlases[ai];
        const size_t n = (size_t)at.w * at.h;
        std::ofstream f(A.out + "/page_" + at.name + ".bin", std::ios::binary);
        const int32_t wh[2] = {at.w, at.h};
        f.write((const char*)wh, 8);
        f.write((const char*)at.cov.data(), (std::streamsize)n);
        f.write((const char*)at.V.data(), (std::streamsize)(n * 4));
        f.write((const char*)at.ndl.data(), (std::streamsize)(n * 4));
        f.write((const char*)at.lt.data(), (std::streamsize)(n * 12));
        f.write((const char*)at.comp.data(), (std::streamsize)(n * 4));
        size_t c = 0, l = 0;
        for (size_t o = 0; o < n; ++o) if (at.cov[o] == 1) { ++c; l += at.V[o] > 0.5f; }
        covered += c; lit += l;
        std::snprintf(buf, sizeof buf, "%s\n  {\"page\": \"%s\", \"w\": %d, \"h\": %d, \"covered\": %zu, \"lit\": %zu}", ai ? "," : "", at.name.c_str(), at.w, at.h, c, l);
        summary += buf;
    }
    const auto t4 = std::chrono::steady_clock::now();
    std::snprintf(buf, sizeof buf, "\n ],\n \"covered_texels\": %zu,\n \"lit_texels\": %zu,\n \"rays\": %llu,\n \"trace_s\": %.2f,\n \"total_s\": %.2f\n}\n",
                  covered, lit, (unsigned long long)rays.load(), std::chrono::duration<double>(t3 - t2).count(), std::chrono::duration<double>(t4 - t0).count());
    summary += buf;
    std::ofstream(A.out + "/vbake_trace.json") << summary;
    std::printf("traced %zu texels (%llu rays) in %.1f s on %d threads: %.1f %% lit; wrote %zu pages to %s\n", covered,
                (unsigned long long)rays.load(), std::chrono::duration<double>(t3 - t2).count(), A.threads,
                covered ? 100.0 * lit / covered : 0.0, atlases.size(), A.out.c_str());
    return 0;
}
