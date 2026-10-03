#include "game/Collision.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace game {

bool CollisionWorld::build(const render::MeshData& mesh) {
    tris_.clear();
    grid_.clear();
    if (mesh.indices.size() < 3 || mesh.positions.size() < 9) return false;

    auto V = [&](uint32_t i) {
        return core::Vec3{mesh.positions[i * 3], mesh.positions[i * 3 + 1], mesh.positions[i * 3 + 2]};
    };
    tris_.reserve(mesh.indices.size() / 3);
    bmin_ = bmax_ = V(mesh.indices[0]);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        Tri t;
        t.a = V(mesh.indices[i]); t.b = V(mesh.indices[i + 1]); t.c = V(mesh.indices[i + 2]);
        t.n = core::normalize(core::cross(t.b - t.a, t.c - t.a));
        tris_.push_back(t);
        for (const core::Vec3* p : {&t.a, &t.b, &t.c}) {
            bmin_ = {std::min(bmin_.x, p->x), std::min(bmin_.y, p->y), std::min(bmin_.z, p->z)};
            bmax_ = {std::max(bmax_.x, p->x), std::max(bmax_.y, p->y), std::max(bmax_.z, p->z)};
        }
    }

    // Build a uniform XZ grid (~2 m cells, capped so memory stays sane).
    float spanX = std::max(1.0f, bmax_.x - bmin_.x);
    float spanZ = std::max(1.0f, bmax_.z - bmin_.z);
    cell_ = 2.0f;
    gx_ = std::min(512, std::max(1, (int)std::ceil(spanX / cell_)));
    gz_ = std::min(512, std::max(1, (int)std::ceil(spanZ / cell_)));
    // Recompute effective cell size to cover the span with the capped grid.
    cell_ = std::max(spanX / gx_, spanZ / gz_) + 0.001f;
    grid_.assign((size_t)gx_ * gz_, {});

    for (int ti = 0; ti < (int)tris_.size(); ++ti) {
        const Tri& t = tris_[(size_t)ti];
        float minx = std::min({t.a.x, t.b.x, t.c.x}), maxx = std::max({t.a.x, t.b.x, t.c.x});
        float minz = std::min({t.a.z, t.b.z, t.c.z}), maxz = std::max({t.a.z, t.b.z, t.c.z});
        int cx0, cz0, cx1, cz1;
        cellRange(minx, minz, cx0, cz0);
        cellRange(maxx, maxz, cx1, cz1);
        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cx = cx0; cx <= cx1; ++cx)
                grid_[(size_t)cz * gx_ + cx].push_back(ti);
    }

    LOG_INFO("collision: %zu tris, grid %dx%d (cell %.1fm), bounds Y [%.1f..%.1f]",
             tris_.size(), gx_, gz_, cell_, bmin_.y, bmax_.y);
    return true;
}

void CollisionWorld::cellRange(float x, float z, int& cx, int& cz) const {
    cx = (int)((x - bmin_.x) / cell_);
    cz = (int)((z - bmin_.z) / cell_);
    cx = std::min(gx_ - 1, std::max(0, cx));
    cz = std::min(gz_ - 1, std::max(0, cz));
}

bool CollisionWorld::groundHeight(float x, float z, float nearY, float stepUp,
                                  float& outY, core::Vec3& outNormal) const {
    if (grid_.empty()) return false;
    int cx, cz;
    cellRange(x, z, cx, cz);
    const std::vector<int>& cell = grid_[(size_t)cz * gx_ + cx];
    bool found = false;
    float best = -1e30f;
    const float ceil = nearY + stepUp;

    for (int ti : cell) {
        const Tri& t = tris_[(size_t)ti];
        // Barycentric of (x,z) in the triangle's XZ projection.
        float x1 = t.a.x, z1 = t.a.z, x2 = t.b.x, z2 = t.b.z, x3 = t.c.x, z3 = t.c.z;
        float det = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3);
        if (std::fabs(det) < 1e-9f) continue;
        float l1 = ((z2 - z3) * (x - x3) + (x3 - x2) * (z - z3)) / det;
        float l2 = ((z3 - z1) * (x - x3) + (x1 - x3) * (z - z3)) / det;
        float l3 = 1.0f - l1 - l2;
        const float eps = -0.001f;
        if (l1 < eps || l2 < eps || l3 < eps) continue;
        float y = l1 * t.a.y + l2 * t.b.y + l3 * t.c.y;
        if (y <= ceil && y > best) { best = y; outNormal = t.n; found = true; }
    }
    if (found) outY = best;
    return found;
}

bool CollisionWorld::segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT) const {
    // Moller-Trumbore against the triangles of the grid cells the segment's XZ projection passes
    // through, walked front to back with a 2D DDA (Amanatides-Woo) and stopped as soon as the
    // nearest hit lies before the current cell's exit. Exact: a triangle is binned into every cell
    // its XZ bounds overlap, so the cell containing any intersection point is always visited.
    // (Previously every cell of the segment's XZ bounding box was tested: a long diagonal ray —
    // e.g. the 300 m weapon trace — scanned thousands of cells per call.)
    if (grid_.empty()) return false;
    core::Vec3 d = b - a;
    float bestT = 1e30f;
    bool hit = false;

    auto testCell = [&](int cx, int cz) {
        for (int ti : grid_[(size_t)cz * gx_ + cx]) {
            const Tri& t = tris_[(size_t)ti];
            core::Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
            core::Vec3 p = core::cross(d, e2);
            float det = core::dot(e1, p);
            if (std::fabs(det) < 1e-8f) continue;
            float inv = 1.0f / det;
            core::Vec3 tv = a - t.a;
            float u = core::dot(tv, p) * inv;
            if (u < 0 || u > 1) continue;
            core::Vec3 q = core::cross(tv, e1);
            float v = core::dot(d, q) * inv;
            if (v < 0 || u + v > 1) continue;
            float tt = core::dot(e2, q) * inv;
            if (tt >= 0 && tt <= 1 && tt < bestT) { bestT = tt; hit = true; }
        }
    };

    // Clip the segment's XZ projection to the grid rectangle (slab test).
    const float gxMax = bmin_.x + gx_ * cell_, gzMax = bmin_.z + gz_ * cell_;
    float t0 = 0.0f, t1 = 1.0f;
    const float o[2] = {a.x, a.z}, dd[2] = {d.x, d.z}, lo[2] = {bmin_.x, bmin_.z}, hi[2] = {gxMax, gzMax};
    for (int k = 0; k < 2; ++k) {
        if (std::fabs(dd[k]) < 1e-12f) {
            if (o[k] < lo[k] || o[k] > hi[k]) return false;
        } else {
            float ta = (lo[k] - o[k]) / dd[k], tb = (hi[k] - o[k]) / dd[k];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
    }

    // DDA over cells from t0 to t1.
    float sx = a.x + d.x * t0, sz = a.z + d.z * t0;
    int cx, cz;
    cellRange(sx, sz, cx, cz);
    const int stepX = d.x > 0 ? 1 : (d.x < 0 ? -1 : 0), stepZ = d.z > 0 ? 1 : (d.z < 0 ? -1 : 0);
    const float inf = 1e30f;
    float tDeltaX = stepX ? cell_ / std::fabs(d.x) : inf, tDeltaZ = stepZ ? cell_ / std::fabs(d.z) : inf;
    float nextX = stepX > 0 ? bmin_.x + (cx + 1) * cell_ : bmin_.x + cx * cell_;
    float nextZ = stepZ > 0 ? bmin_.z + (cz + 1) * cell_ : bmin_.z + cz * cell_;
    float tMaxX = stepX ? (nextX - a.x) / d.x : inf, tMaxZ = stepZ ? (nextZ - a.z) / d.z : inf;
    for (int guard = 0; guard < gx_ + gz_ + 4; ++guard) {
        testCell(cx, cz);
        float tExit = std::min(std::min(tMaxX, tMaxZ), t1);
        if (hit && bestT <= tExit) break;          // nothing in later cells can be nearer
        if (tExit >= t1) break;                    // segment ends inside this cell
        if (tMaxX < tMaxZ) { cx += stepX; tMaxX += tDeltaX; } else { cz += stepZ; tMaxZ += tDeltaZ; }
        if (cx < 0 || cz < 0 || cx >= gx_ || cz >= gz_) break;
    }

    if (hit) outT = bestT;
    return hit;
}

} // namespace game

namespace game {

// Hit triangle normal for a segment query (hover suspension contact normals). Uses segmentHit() for
// the nearest t, then picks the triangle intersected at that t among the triangles binned in the hit
// point's cell (a triangle is binned into every cell its XZ bounds overlap, so it is there).
bool CollisionWorld::segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN) const {
    if (!segmentHit(a, b, outT)) return false;
    core::Vec3 d = b - a;
    core::Vec3 p = a + d * outT;
    int cx, cz;
    cellRange(p.x, p.z, cx, cz);
    float best = 1e30f;
    outN = core::Vec3{0, 1, 0};
    for (int ti : grid_[(size_t)cz * gx_ + cx]) {
        const Tri& t = tris_[(size_t)ti];
        core::Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
        core::Vec3 pv = core::cross(d, e2);
        float det = core::dot(e1, pv);
        if (std::fabs(det) < 1e-8f) continue;
        float inv = 1.0f / det;
        core::Vec3 tv = a - t.a;
        float u = core::dot(tv, pv) * inv;
        if (u < 0 || u > 1) continue;
        core::Vec3 q = core::cross(tv, e1);
        float v = core::dot(d, q) * inv;
        if (v < 0 || u + v > 1) continue;
        float tt = core::dot(e2, q) * inv;
        if (std::fabs(tt - outT) < best) { best = std::fabs(tt - outT); outN = t.n; }
    }
    return true;
}

} // namespace game
