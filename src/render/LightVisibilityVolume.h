#pragma once
// WFC ALightsVisibilitiesVolume / FLightsVisibilityOctree: baked per-cell light visibility.
// Implemented from the native recovery (RE-Workspace notes/MILESTONE03_RENDERING_LIGHTVIS_SHADOW.md,
// ReverseEngineering b52dca9): FLightsVisibilityOctree::Serialize 0x82DF5748, FLVNode::Serialize
// 0x82DF5590, Octree::Query 0x82DDE200, QueryNode 0x82DDDA08, AccumulateSample 0x82DDD798,
// Renormalize 0x82E03238, FindLeaf 0x82DCB4A0 [CONFIRMED].
// Coordinates are UE units (the octree centre / extents are serialized in world space).
// UNKNOWN / PARTIAL: the u32 at octree +0x18 (Streets: 4096.0 as a float, not read by the query); the
// archive flag that halves count bytes on load (applied when the halved counts are the ones that match
// the pair pool: required for the Streets blob); the meaning of count-byte bit 7 (honoured as "invalid"
// on the face path only, as in the original).
#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace render {

class LightVisibilityVolume {
public:
    struct Facts {
        size_t bytes = 0, nodes = 0, lights = 0, corners = 0, samples = 0, pairs = 0;
        bool pairCountMatches = false, countsHalved = false, indicesAscending = false;
        int bit7Samples = 0;
        uint32_t unk18 = 0;
        float rootHalf = 0, finestHalf = 0;
    };
    struct Guid { uint32_t a = 0, b = 0, c = 0, d = 0; bool operator==(const Guid& o) const { return a == o.a && b == o.b && c == o.c && d == o.d; } };

    bool load(const std::string& file);
    bool valid() const { return decoded_; }
    const Facts& facts() const { return facts_; }
    bool selfCheck(std::string& why) const;

    // Light table (serialized order = pair light index). bind() maps table entries to renderer lights by
    // LightComponent.LightGuid; unbound entries are ignored by the query (component == null).
    const std::vector<Guid>& lightGuids() const { return guids_; }
    void bind(const std::vector<Guid>& rendererLightGuids);
    bool isBaked(int rendererLight) const;            // light present (bound) in this octree's table

    // Octree::Query at a UE-space position (the light-environment bounds origin). Returns false when
    // the point is outside the root cube (strict) or lands in an empty leaf; otherwise fills
    // (renderer light, visibility) for every bound light with a pair.
    bool contains(const core::Vec3& p) const;
    bool query(const core::Vec3& p, std::vector<int>& lights, std::vector<float>& vis) const;

private:
    struct Node { uint16_t corner[8]; bool hasData = false; int children = -1; };   // children: first of 8
    struct Bounds { core::Vec3 c; float h; };
    struct Acc { int16_t light; uint16_t vis; };
    bool decode(const std::vector<uint8_t>& b);
    bool queryNode(int node, const Bounds& B, const core::Vec3& p, std::vector<Acc>& acc) const;
    int findLeaf(const core::Vec3& q, Bounds& out) const;
    void accumulate(std::vector<Acc>& acc, int sample, float w) const;
    static core::Vec3 cornerPos(const Bounds& B, int k);

    std::vector<Node> nodes_;
    core::Vec3 center_{0, 0, 0};
    float rootHalf_ = 0, finestHalf_ = 0;
    std::vector<Guid> guids_;
    std::vector<uint16_t> remap_;
    std::vector<uint8_t> counts_;
    std::vector<uint32_t> starts_, pool_;
    std::vector<int> tableToRenderer_, rendererToTable_;
    Facts facts_;
    bool decoded_ = false;
};

} // namespace render
