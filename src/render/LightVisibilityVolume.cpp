#include "render/LightVisibilityVolume.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>

namespace render {

namespace {
struct Reader {
    const std::vector<uint8_t>& b;
    size_t o = 0;
    bool ok = true;
    bool need(size_t n) { if (o + n > b.size()) ok = false; return ok; }
    uint8_t u8() { if (!need(1)) return 0; return b[o++]; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3];
        o += 4; return v;
    }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = (uint16_t)(b[o] << 8 | b[o + 1]); o += 2; return v; }
    float f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
};
} // namespace

bool LightVisibilityVolume::load(const std::string& file) {
    decoded_ = false;
    std::vector<uint8_t> b;
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) { b.resize((size_t)n); if (std::fread(b.data(), 1, (size_t)n, f) != (size_t)n) b.clear(); }
    std::fclose(f);
    facts_ = Facts{};
    facts_.bytes = b.size();
    if (b.empty()) return false;
    decoded_ = decode(b);
    return true;
}

bool LightVisibilityVolume::decode(const std::vector<uint8_t>& b) {
    Reader r{b};
    if (!r.u8()) return false;                       // bHasOctree
    nodes_.clear();
    // FLVNode::Serialize: 8 x i32 corner (low 16 bits), i32 hasData (licensee >= 0x62), u8 hasChildren,
    // then 8 children (allocated contiguously).
    // FLVNode::Serialize: 8 x i32 corner (low 16 bits), i32 hasData (licensee >= 0x62), u8 hasChildren,
    // then 8 children, which the original allocates as one contiguous block.
    std::function<void(int)> parseInto = [&](int idx) {
        Node nd;
        for (int k = 0; k < 8; ++k) nd.corner[k] = (uint16_t)(r.u32() & 0xFFFF);
        nd.hasData = r.u32() != 0;
        bool hasChildren = r.u8() != 0;
        nodes_[(size_t)idx] = nd;
        if (!hasChildren || !r.ok) return;
        int first = (int)nodes_.size();
        nodes_.resize(nodes_.size() + 8);
        nodes_[(size_t)idx].children = first;
        for (int k = 0; k < 8 && r.ok; ++k) parseInto(first + k);
    };
    nodes_.resize(1);
    parseInto(0);
    if (!r.ok) return false;
    center_.x = r.f32(); center_.y = r.f32(); center_.z = r.f32();
    rootHalf_ = r.f32();
    facts_.unk18 = r.u32();                          // +0x18 UNKNOWN (not read by the query)
    finestHalf_ = r.f32();
    int nl = (int)r.u32();
    guids_.assign((size_t)std::max(nl, 0), Guid{});
    for (Guid& g : guids_) { g.a = r.u32(); g.b = r.u32(); g.c = r.u32(); g.d = r.u32(); }
    int nc = (int)r.u32();
    remap_.resize((size_t)std::max(nc, 0));
    for (uint16_t& v : remap_) v = r.u16();
    int ns = (int)r.u32();
    counts_.resize((size_t)std::max(ns, 0));
    for (uint8_t& v : counts_) v = r.u8();
    if (!r.ok) return false;
    size_t poolBytes = b.size() - r.o;
    auto total = [&](bool halve) {
        size_t t = 0;
        for (uint8_t c : counts_) t += (size_t)((halve ? (c >> 1) : c) & 0x7F);
        return t;
    };
    // Archive flag at +0x20 halves every count byte on load (PARTIAL: identity unknown); the pool size
    // decides which reading is in effect for this blob.
    if (total(false) * 4 != poolBytes && total(true) * 4 == poolBytes) {
        for (uint8_t& c : counts_) c = (uint8_t)(c >> 1);
        facts_.countsHalved = true;
    }
    size_t pairs = total(false);
    if (pairs * 4 != poolBytes) return false;
    pool_.resize(pairs);
    for (uint32_t& v : pool_) v = r.u32();
    starts_.resize(counts_.size());
    uint32_t acc = 0;
    for (size_t s = 0; s < counts_.size(); ++s) { starts_[s] = acc; acc += counts_[s] & 0x7F; }
    // facts
    facts_.nodes = nodes_.size(); facts_.lights = guids_.size(); facts_.corners = remap_.size();
    facts_.samples = counts_.size(); facts_.pairs = pairs; facts_.pairCountMatches = true;
    facts_.rootHalf = rootHalf_; facts_.finestHalf = finestHalf_;
    facts_.indicesAscending = true;
    for (size_t s = 0; s < counts_.size(); ++s) {
        if (counts_[s] & 0x80) ++facts_.bit7Samples;
        int cnt = counts_[s] & 0x7F;
        for (int k = 1; k < cnt; ++k)
            if ((int16_t)(pool_[starts_[s] + (uint32_t)k - 1] >> 16) >= (int16_t)(pool_[starts_[s] + (uint32_t)k] >> 16))
                facts_.indicesAscending = false;
    }
    tableToRenderer_.assign(guids_.size(), -1);
    return true;
}

void LightVisibilityVolume::bind(const std::vector<Guid>& rendererLightGuids) {
    tableToRenderer_.assign(guids_.size(), -1);
    rendererToTable_.assign(rendererLightGuids.size(), -1);
    for (size_t t = 0; t < guids_.size(); ++t)
        for (size_t i = 0; i < rendererLightGuids.size(); ++i)
            if (guids_[t] == rendererLightGuids[i]) { tableToRenderer_[t] = (int)i; rendererToTable_[i] = (int)t; break; }
}

bool LightVisibilityVolume::isBaked(int rendererLight) const {
    return decoded_ && rendererLight >= 0 && (size_t)rendererLight < rendererToTable_.size() &&
           rendererToTable_[(size_t)rendererLight] >= 0;
}

bool LightVisibilityVolume::contains(const core::Vec3& p) const {   // Volume::OctreeContains: strict
    return decoded_ && std::fabs(p.x - center_.x) < rootHalf_ && std::fabs(p.y - center_.y) < rootHalf_ &&
           std::fabs(p.z - center_.z) < rootHalf_;
}

core::Vec3 LightVisibilityVolume::cornerPos(const Bounds& B, int k) {   // corner bit0 = X, bit1 = Y, bit2 = Z
    return {B.c.x + ((k & 1) ? B.h : -B.h), B.c.y + ((k & 2) ? B.h : -B.h), B.c.z + ((k & 4) ? B.h : -B.h)};
}

int LightVisibilityVolume::findLeaf(const core::Vec3& q, Bounds& out) const {
    if (!contains(q)) return -1;
    int n = 0;
    Bounds B{center_, rootHalf_};
    while (nodes_[(size_t)n].children >= 0) {        // child index = (x>c.x)<<2 | (y>c.y)<<1 | (z>c.z)
        int i = (q.x > B.c.x ? 4 : 0) | (q.y > B.c.y ? 2 : 0) | (q.z > B.c.z ? 1 : 0);
        float hh = B.h * 0.5f;
        B.c = {B.c.x + ((i & 4) ? hh : -hh), B.c.y + ((i & 2) ? hh : -hh), B.c.z + ((i & 1) ? hh : -hh)};
        B.h = hh;
        n = nodes_[(size_t)n].children + i;
    }
    out = B;
    return n;
}

// AccumulateSample: both lists sorted by light index; 16-bit fixed point with truncation at every step.
void LightVisibilityVolume::accumulate(std::vector<Acc>& acc, int sample, float w) const {
    int cnt = counts_[(size_t)sample] & 0x7F;
    size_t a = 0;
    for (int k = 0; k < cnt; ++k) {
        uint32_t rec = pool_[starts_[(size_t)sample] + (uint32_t)k];
        int16_t li = (int16_t)(rec >> 16);
        float v = (float)(rec & 0xFFFF) / 65535.0f;
        while (a < acc.size() && acc[a].light < li) ++a;
        if (a < acc.size() && acc[a].light == li) {
            acc[a].vis = (uint16_t)(int)((v * w + (float)acc[a].vis / 65535.0f) * 65535.0f);
        } else {
            acc.insert(acc.begin() + (long)a, Acc{li, (uint16_t)(int)(v * w * 65535.0f)});
        }
    }
}

bool LightVisibilityVolume::queryNode(int ni, const Bounds& B, const core::Vec3& p, std::vector<Acc>& acc) const {
    const Node& n = nodes_[(size_t)ni];
    if (n.children >= 0) {
        int i = (p.x > B.c.x ? 4 : 0) | (p.y > B.c.y ? 2 : 0) | (p.z > B.c.z ? 1 : 0);
        float hh = B.h * 0.5f;
        Bounds C{{B.c.x + ((i & 4) ? hh : -hh), B.c.y + ((i & 2) ? hh : -hh), B.c.z + ((i & 1) ? hh : -hh)}, hh};
        return queryNode(n.children + i, C, p, acc);
    }
    if (!n.hasData) return false;
    const float h = B.h;
    const float eps = finestHalf_ * 0.5f;
    // T-junction test: probe the 6 face neighbours only when this leaf is not of the finest size.
    int nb[6] = {-1, -1, -1, -1, -1, -1};
    Bounds nbB[6];
    bool tj = false;
    if (finestHalf_ < h) {
        for (int f = 0; f < 6; ++f) {
            int axis = f >> 1;
            core::Vec3 q = p;
            float* qa = axis == 0 ? &q.x : axis == 1 ? &q.y : &q.z;
            float ca = axis == 0 ? B.c.x : axis == 1 ? B.c.y : B.c.z;
            *qa = (f & 1) ? ca + (h + eps) : ca - (h + eps);
            nb[f] = findLeaf(q, nbB[f]);
            if (nb[f] >= 0 && nbB[f].h <= h - eps) tj = true;
        }
    }
    float emptyW = 0.0f;
    if (!tj) {   // plain trilinear over the 8 corners
        const float inv = 1.0f / (2.0f * h);
        for (int k = 0; k < 8; ++k) {
            core::Vec3 ck = cornerPos(B, k);
            float w = (1.0f - std::fabs(p.x - ck.x) * inv) * (1.0f - std::fabs(p.y - ck.y) * inv) *
                      (1.0f - std::fabs(p.z - ck.z) * inv);
            uint16_t cid = n.corner[k];
            int s = cid == 0xFFFF || cid >= remap_.size() ? -1 : remap_[cid];
            int cnt = s < 0 ? 0 : (counts_[(size_t)s] & 0x7F);
            if (cnt == 0) emptyW += w;               // 0-pair corner: renormalized away
            else accumulate(acc, s, w);
        }
    } else {     // face-blended: 6 faces x 4 corners, bilinear in each face plane, face weight 1/3
        static const int kFace[6][4] = {{0, 2, 4, 6}, {1, 3, 5, 7}, {0, 1, 4, 5}, {2, 3, 6, 7}, {0, 1, 2, 3}, {4, 5, 6, 7}};
        float t[3] = {(p.x - (B.c.x - h)) / (2.0f * h), (p.y - (B.c.y - h)) / (2.0f * h), (p.z - (B.c.z - h)) / (2.0f * h)};
        for (int f = 0; f < 6; ++f) {
            int axis = f >> 1;
            float fw = ((f & 1) ? t[axis] : 1.0f - t[axis]) * (1.0f / 3.0f);
            bool useSelf = nb[f] < 0 || nbB[f].h >= h - eps;
            const Node& src = useSelf ? n : nodes_[(size_t)nb[f]];
            const Bounds& sB = useSelf ? B : nbB[f];
            float invS = 1.0f / (2.0f * sB.h);
            for (int j = 0; j < 4; ++j) {
                int k = kFace[f][j];
                core::Vec3 ck = cornerPos(sB, k);
                float wx = axis == 0 ? 1.0f : 1.0f - std::fabs(p.x - ck.x) * invS;
                float wy = axis == 1 ? 1.0f : 1.0f - std::fabs(p.y - ck.y) * invS;
                float wz = axis == 2 ? 1.0f : 1.0f - std::fabs(p.z - ck.z) * invS;
                float w3 = wx * wy * wz;
                uint16_t cid = src.corner[k];
                bool invalid = cid == 0xFFFF || cid >= remap_.size();
                int s = invalid ? -1 : remap_[cid];
                bool flag = !invalid && (counts_[(size_t)s] >> 7) != 0;   // bit 7: meaning UNKNOWN
                if (invalid || flag) emptyW += w3 * fw;
                else accumulate(acc, s, w3 * fw);   // 0-pair samples contribute nothing, not renormalized
            }
        }
    }
    if (emptyW > 0.0f) {                             // Renormalize away empty / invalid corners
        float sc = 1.0f / (1.0f - emptyW);
        for (Acc& a : acc) a.vis = (uint16_t)(int)((float)a.vis * sc);
    }
    return true;
}

bool LightVisibilityVolume::query(const core::Vec3& p, std::vector<int>& lights, std::vector<float>& vis) const {
    lights.clear(); vis.clear();
    if (!decoded_ || pool_.empty() || !contains(p)) return false;
    static thread_local std::vector<Acc> acc;      // reused: one query per character light update (no allocation)
    acc.clear();
    acc.reserve(100);
    if (!queryNode(0, Bounds{center_, rootHalf_}, p, acc)) return false;
    for (const Acc& a : acc) {
        if (a.light < 0 || (size_t)a.light >= tableToRenderer_.size()) continue;
        int rl = tableToRenderer_[(size_t)a.light];
        if (rl < 0) continue;                        // table entry without a bound component
        lights.push_back(rl);
        vis.push_back((float)a.vis / 65535.0f);
    }
    return true;
}

bool LightVisibilityVolume::selfCheck(std::string& why) const {
    char buf[384];
    std::snprintf(buf, sizeof buf,
                  "%zu bytes: %zu nodes, %zu lights (GUID table), %zu corners, %zu samples, %zu pairs (pool %s, counts %s),"
                  " indices %s, bit7 samples %d, rootHalf %.1f, finestHalf %.1f, +0x18 = 0x%08x",
                  facts_.bytes, facts_.nodes, facts_.lights, facts_.corners, facts_.samples, facts_.pairs,
                  facts_.pairCountMatches ? "matches" : "MISMATCH", facts_.countsHalved ? "halved" : "as stored",
                  facts_.indicesAscending ? "ascending" : "NOT ASCENDING", facts_.bit7Samples, facts_.rootHalf,
                  facts_.finestHalf, facts_.unk18);
    why = buf;
    return decoded_ && facts_.pairCountMatches && facts_.indicesAscending;
}

} // namespace render
