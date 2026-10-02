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
    // Same query, also returning the hit triangle's unit normal (orientation as authored).
    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN) const;

    // Diagnostics (WFC_PERFLOG): segment queries since the last reset, triangles tested, time.
    struct Stats { long calls = 0; long tris = 0; double ms = 0.0; };
    static Stats& stats();

private:
    struct Tri { core::Vec3 a, b, c, n; };

    int cellOf(float x, float z) const;
    void cellRange(float x, float z, int& cx, int& cz) const;

    std::vector<Tri> tris_;
    core::Vec3 bmin_{0, 0, 0}, bmax_{0, 0, 0};

    // Uniform grid over XZ for ground queries.
    int gx_ = 1, gz_ = 1;
    float cell_ = 1.0f;
    std::vector<std::vector<int>> grid_;   // gx_*gz_ cells -> triangle indices
};

} // namespace game
