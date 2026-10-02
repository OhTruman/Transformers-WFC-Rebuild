#include "game/Collision.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
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

    // WFC_TRACE_SELFTEST=N: compare segmentHit with a brute-force test over every triangle for N
    // random segments (lengths up to the 300 m weapon range).
    if (const char* st = std::getenv("WFC_TRACE_SELFTEST")) {
        int n = std::atoi(st), bad = 0, hits = 0;
        std::srand(12345);
        auto rf = [] { return (float)std::rand() / (float)RAND_MAX; };
        for (int i = 0; i < n; ++i) {
            core::Vec3 a{bmin_.x + rf() * (bmax_.x - bmin_.x), bmin_.y + rf() * (bmax_.y - bmin_.y), bmin_.z + rf() * (bmax_.z - bmin_.z)};
            core::Vec3 dir = core::normalize(core::Vec3{rf() * 2 - 1, (rf() * 2 - 1) * 0.5f, rf() * 2 - 1});
            float len = (i % 3 == 0) ? 300.0f : rf() * 60.0f;
            core::Vec3 b = a + dir * len;
            float tg = 0; bool hg = segmentHit(a, b, tg);
            float tb = 1e30f; bool hb = false;
            core::Vec3 d = b - a;
            for (const Tri& t : tris_) {
                core::Vec3 e1 = t.b - t.a, e2 = t.c - t.a, p = core::cross(d, e2);
                float det = core::dot(e1, p);
                if (std::fabs(det) < 1e-8f) continue;
                float inv = 1.0f / det; core::Vec3 tv = a - t.a;
                float u = core::dot(tv, p) * inv; if (u < 0 || u > 1) continue;
                core::Vec3 q = core::cross(tv, e1);
                float v = core::dot(d, q) * inv; if (v < 0 || u + v > 1) continue;
                float tt = core::dot(e2, q) * inv;
                if (tt >= 0 && tt <= 1 && tt < tb) { tb = tt; hb = true; }
            }
            hits += hb;
            if (hg != hb || (hb && std::fabs(tg - tb) * len > 1e-3f)) {
                if (++bad <= 5) LOG_INFO("trace selftest MISMATCH %d: grid %d t=%.6f brute %d t=%.6f len=%.1f", i, (int)hg, tg, (int)hb, tb, len);
            }
        }
        LOG_INFO("trace selftest: %d segments, %d hits, %d mismatches", n, hits, bad);
    }
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

CollisionWorld::Stats& CollisionWorld::stats() { static Stats s; return s; }

bool CollisionWorld::segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT) const {
    core::Vec3 n;
    return segmentHit(a, b, outT, n);
}

bool CollisionWorld::segmentHit(const core::Vec3& a, const core::Vec3& b, float& outT, core::Vec3& outN) const {
    static const bool perf = std::getenv("WFC_PERFLOG") != nullptr;
    auto t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    long tested = 0;
    // Möller–Trumbore over triangles in the cells the segment's endpoints touch (coarse).
    core::Vec3 d = b - a;
    float bestT = 1e30f;
    bool hit = false;
    int bestTri = -1;

    auto testCell = [&](int cx, int cz) {
        if (cx < 0 || cz < 0 || cx >= gx_ || cz >= gz_) return;
        for (int ti : grid_[(size_t)cz * gx_ + cx]) {
            const Tri& t = tris_[(size_t)ti];
            ++tested;
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
            if (tt >= 0 && tt <= 1 && tt < bestT) { bestT = tt; hit = true; bestTri = ti; }
        }
    };

    static const bool legacy = std::getenv("WFC_TRACE_AABB") != nullptr;   // A/B: old cell-rectangle scan
    if (legacy) {
        int cx0, cz0, cx1, cz1;
        cellRange(std::min(a.x, b.x), std::min(a.z, b.z), cx0, cz0);
        cellRange(std::max(a.x, b.x), std::max(a.z, b.z), cx1, cz1);
        for (int cz = cz0; cz <= cz1; ++cz)
            for (int cx = cx0; cx <= cx1; ++cx)
                testCell(cx, cz);
    } else {
        // Walk only the grid cells the segment crosses in XZ (Amanatides-Woo), nearest first. Any
        // triangle the segment hits at P is registered in P's cell (cells come from the triangle's XZ
        // bounds), so the hit set and nearest t are the same as the full rectangle scan; the walk
        // stops once the nearest hit lies inside the cells already visited.
        float t0 = 0.0f, t1 = 1.0f;
        const float lo[2] = {bmin_.x, bmin_.z}, hi[2] = {bmax_.x, bmax_.z};
        const float o[2] = {a.x, a.z}, dd[2] = {d.x, d.z};
        bool inside = true;
        for (int k = 0; k < 2 && inside; ++k) {        // clip to the grid's XZ bounds
            if (std::fabs(dd[k]) < 1e-12f) { if (o[k] < lo[k] - 1e-4f || o[k] > hi[k] + 1e-4f) inside = false; continue; }
            float ta = (lo[k] - o[k]) / dd[k], tb = (hi[k] - o[k]) / dd[k];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) inside = false;
        }
        if (inside) {
            float sx = a.x + d.x * t0, sz = a.z + d.z * t0;
            int cx, cz;
            cellRange(sx, sz, cx, cz);
            int stepX = d.x > 0 ? 1 : (d.x < 0 ? -1 : 0), stepZ = d.z > 0 ? 1 : (d.z < 0 ? -1 : 0);
            auto boundaryT = [&](float origin, float dir, int c, int step, float base) {
                if (step == 0) return 1e30f;
                float edge = base + (float)(c + (step > 0 ? 1 : 0)) * cell_;
                return (edge - origin) / dir;
            };
            float tMaxX = boundaryT(a.x, d.x, cx, stepX, bmin_.x);
            float tMaxZ = boundaryT(a.z, d.z, cz, stepZ, bmin_.z);
            float tDX = stepX ? cell_ / std::fabs(d.x) : 1e30f, tDZ = stepZ ? cell_ / std::fabs(d.z) : 1e30f;
            for (int guard = 0; guard < gx_ + gz_ + 4; ++guard) {
                testCell(cx, cz);
                float tExit = std::min(tMaxX, tMaxZ);
                if (hit && bestT <= tExit) break;           // nearest hit already inside visited cells
                if (tExit > t1) break;
                if (tMaxX < tMaxZ) { cx += stepX; tMaxX += tDX; } else { cz += stepZ; tMaxZ += tDZ; }
                if (cx < 0 || cz < 0 || cx >= gx_ || cz >= gz_) break;
            }
        }
    }

    if (perf) {
        Stats& s = stats();
        ++s.calls; s.tris += tested;
        s.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    if (hit) { outT = bestT; outN = tris_[(size_t)bestTri].n; }
    return hit;
}

} // namespace game
