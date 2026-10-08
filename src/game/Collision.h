// Clean-room reconstruction — simple CPU collision against an extracted triangle mesh.
// Keeps the collision geometry CPU-side (NOT uploaded to the renderer) for queries.
// Uses a uniform XZ grid to accelerate the common downward-ground query.
#pragma once
#include <vector>
#include "core/Math.h"
#include "render/Mesh.h"

namespace game {

class CollisionWorld {
public:
    bool build(const render::MeshData& mesh);   // takes a copy of triangles; builds grid
    bool valid() const { return !tris_.empty(); }

    size_t triangleCount() const { return tris_.size(); }
    const core::Vec3& boundsMin() const { return bmin_; }
    const core::Vec3& boundsMax() const { return bmax_; }

    // Highest walkable surface at (x,z) with surface Y <= nearY + stepUp.
    // Returns false if nothing found under/near the point.
    bool groundHeight(float x, float z, float nearY, float stepUp, float& outY, core::Vec3& outNormal) const;

    // Does the segment a->b hit any triangle? (used as a crude wall block). Returns nearest t in [0,1].
    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT) const;
    // Any hit on a-b (no nearest t): the renderer's light-visibility queries. Same boolean as segmentHit, stops at the first hit.
    bool segmentAnyHit(const core::Vec3& a, const core::Vec3& b) const;
    // The previous segmentHit (no height rejection), kept for WFC_RAYBENCH's equality check.
    bool segmentHitReference(const core::Vec3& a, const core::Vec3& b, float& outT) const;
    // Same query, also returning the hit triangle's unit normal (orientation as authored). Also tests the
    // moving collision sets below (the 3-argument query is static geometry only).
    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN) const;

    // Moving collision (map movers, destructible pieces): triangle sets posed every tick by the World.
    // Triangles are given relative to a pivot; pose = world transform of that pivot frame.
    int addDynamicSet(const std::vector<core::Vec3>& triVerts, const core::Mat4& pose);
    void setDynamicPose(int id, const core::Mat4& pose);
    void setDynamicEnabled(int id, bool enabled);
    size_t dynamicSetCount() const { return dyn_.size(); }

private:
    struct Tri { core::Vec3 a, b, c, n; };
    struct DynamicSet {
        std::vector<core::Vec3> local;   // 3 vertices per triangle, pivot-relative
        std::vector<Tri> world;          // posed copy
        core::Vec3 bmin{0, 0, 0}, bmax{0, 0, 0};
        bool enabled = true;
        core::Mat4 lastPose{};           // setDynamicPose skips an unchanged pose (a mover holding still): same triangles
        bool posed = false;
    };
    std::vector<DynamicSet> dyn_;
    bool dynamicGround(float x, float z, float ceil, float& best, core::Vec3& outNormal) const;

    int cellOf(float x, float z) const;
    void cellRange(float x, float z, int& cx, int& cz) const;

    std::vector<Tri> tris_;
    std::vector<float> triYMin_, triYMax_;   // per triangle (static): the walk skips triangles above / below the segment in a cell
    template <bool Any> bool walk(const core::Vec3& a, const core::Vec3& b, float& outT) const;
    core::Vec3 bmin_{0, 0, 0}, bmax_{0, 0, 0};

    // Uniform grid over XZ for ground queries.
    int gx_ = 1, gz_ = 1;
    float cell_ = 1.0f;
    std::vector<std::vector<int>> grid_;   // gx_*gz_ cells -> triangle indices
};

} // namespace game
