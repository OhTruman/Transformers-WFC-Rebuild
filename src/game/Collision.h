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
    // minAbsNy > 0: only faces with |n.y| >= minAbsNy count (WalkableFloorZ; the collision winding is not reliable, hence |n.y|).
    bool groundHeight(float x, float z, float nearY, float stepUp, float& outY, core::Vec3& outNormal, float minAbsNy = 0.0f) const;

    // Does the segment a->b hit any triangle? (used as a crude wall block). Returns nearest t in [0,1].
    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT) const;
    // Any hit on a-b (no nearest t): the renderer's light-visibility queries. Same boolean as segmentHit, stops at the first hit.
    bool segmentAnyHit(const core::Vec3& a, const core::Vec3& b) const;
    // The previous segmentHit (no height rejection), kept for WFC_RAYBENCH's equality check.
    bool segmentHitReference(const core::Vec3& a, const core::Vec3& b, float& outT) const;
    // Same query, also returning the hit triangle's unit normal (orientation as authored). Also tests the
    // moving collision sets below (the 3-argument query is static geometry only).
    // outPhys (optional): the hit triangle's physical material index (0 = none / a moving set); physmatName() resolves it.
    bool segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN, uint16_t* outPhys = nullptr) const;
    // Per static triangle physical material indices (same order as the built triangles) and their names; index 0 / unknown =
    // engineDefault. Returns false (nothing set) when the count does not match the built triangles.
    bool setPhysmats(std::vector<uint16_t> perTri, std::vector<std::string> names, std::string engineDefault) {
        if (perTri.size() != tris_.size()) return false;
        triPhys_ = std::move(perTri); physNames_ = std::move(names); physDefault_ = std::move(engineDefault);
        return true;
    }
    const std::string& physmatName(uint16_t i) const {
        return i > 0 && i < physNames_.size() && !physNames_[i].empty() ? physNames_[i] : physDefault_;
    }
    // Diagnostic: a short ray into the centroid of `samples` static triangles with a material; returns how many hits report it
    // (outTested = rays that hit anything).
    int physmatSelfTest(int samples, int& outTested) const {
        outTested = 0; int ok = 0;
        if (triPhys_.size() != tris_.size() || tris_.empty()) return 0;
        const size_t stride = std::max<size_t>(1, tris_.size() / (size_t)std::max(1, samples));
        for (size_t i = 0; i < tris_.size() && outTested < samples; i += stride) {
            if (!triPhys_[i]) continue;
            const Tri& t = tris_[i];
            const core::Vec3 c = (t.a + t.b + t.c) * (1.0f / 3.0f);
            float ht; core::Vec3 hn; uint16_t hp = 0;
            if (!segmentHit(c + t.n * 0.02f, c - t.n * 0.02f, ht, hn, &hp)) continue;
            ++outTested; if (hp == triPhys_[i]) ++ok;
        }
        return ok;
    }

    // Moving collision (map movers, destructible pieces): triangle sets posed every tick by the World.
    // Triangles are given relative to a pivot; pose = world transform of that pivot frame.
    int addDynamicSet(const std::vector<core::Vec3>& triVerts, const core::Mat4& pose);
    void setDynamicPose(int id, const core::Mat4& pose);
    void setDynamicEnabled(int id, bool enabled);
    size_t dynamicSetCount() const { return dyn_.size(); }
    // Moving sets this thread's queries ignore while the scope lives (a pawn walking out of a barrier that spawned around it: its
    // movement runs on one thread, so the exemption is that pawn's only, also on the worker pool).
    struct IgnoreDynamicScope {
        const std::vector<int>* prev;
        explicit IgnoreDynamicScope(const std::vector<int>* sets) : prev(ignored()) { ignored() = sets; }
        ~IgnoreDynamicScope() { ignored() = prev; }
    };
    static const std::vector<int>*& ignored() { static thread_local const std::vector<int>* p = nullptr; return p; }
    static bool isIgnored(size_t set) { const std::vector<int>* l = ignored(); if (!l) return false; for (int v : *l) if ((size_t)v == set) return true; return false; }
    // Diagnostics: the nearest enabled moving set the segment hits (-1 none), with its t.
    int dynamicSetHit(const core::Vec3& a, const core::Vec3& b, float& outT) const;

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
    bool dynamicGround(float x, float z, float ceil, float& best, core::Vec3& outNormal, float minAbsNy = 0.0f) const;

    int cellOf(float x, float z) const;
    void cellRange(float x, float z, int& cx, int& cz) const;

    std::vector<Tri> tris_;
    std::vector<uint16_t> triPhys_;          // per static triangle physmat index (empty: none loaded)
    std::vector<std::string> physNames_;
    std::string physDefault_;
    std::vector<float> triYMin_, triYMax_;   // per triangle (static): the walk skips triangles above / below the segment in a cell
    template <bool Any> bool walk(const core::Vec3& a, const core::Vec3& b, float& outT) const;
    core::Vec3 bmin_{0, 0, 0}, bmax_{0, 0, 0};

    // Uniform grid over XZ for ground queries.
    int gx_ = 1, gz_ = 1;
    float cell_ = 1.0f;
    std::vector<std::vector<int>> grid_;   // gx_*gz_ cells -> triangle indices
};

} // namespace game
