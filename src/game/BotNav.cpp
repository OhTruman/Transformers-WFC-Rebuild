// Clean-room reconstruction — bot navigation runtime (see BotNav.h). PC ADAPTATION.
#include "game/BotNav.h"
#include "assets/Json.h"
#include "core/Log.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <sstream>

namespace game {

namespace {
core::Vec3 v3(const assets::Json& j) { return {j[0].asFloat(), j[1].asFloat(), j[2].asFloat()}; }
float cross2(float ax, float az, float bx, float bz) { return ax * bz - az * bx; }
unsigned hashU(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
}

bool BotNav::load(const std::string& path) {
    cells_.clear(); links_.clear(); anchors_.clear(); grid_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    assets::Json j;
    if (!assets::Json::parse(ss.str(), j)) { LOG_WARN("botnav: %s is not valid JSON", path.c_str()); return false; }
    const assets::Json& cs = j["cells"];
    cells_.resize(cs.size());
    for (size_t i = 0; i < cs.size(); ++i) {
        const assets::Json& c = cs[i];
        Cell& o = cells_[i];
        const assets::Json& vs = c["vertices_gltf"];
        for (size_t k = 0; k < vs.size(); ++k) o.poly.push_back(v3(vs[k]));
        o.centroid = v3(c["centroid_gltf"]);
        o.clearance = c["clearance_m"].asFloat();
        o.headroom = c["headroom_m"].asFloat();
        o.vehicle = c["flags"]["vehicle_capable"].asBool();
        o.bmin = o.bmax = o.poly.empty() ? o.centroid : o.poly[0];
        for (const core::Vec3& p : o.poly) {
            o.bmin = {std::min(o.bmin.x, p.x), std::min(o.bmin.y, p.y), std::min(o.bmin.z, p.z)};
            o.bmax = {std::max(o.bmax.x, p.x), std::max(o.bmax.y, p.y), std::max(o.bmax.z, p.z)};
        }
    }
    // Portals: the part of each of a cell's edges shared with the neighbour (the neighbour's extent projected onto the edge).
    for (size_t i = 0; i < cs.size(); ++i) {
        const assets::Json& es = cs[i]["edges"];
        for (size_t e = 0; e < es.size(); ++e) {
            const assets::Json& ns = es[e]["neighbors"];
            if (ns.size() == 0) continue;
            const core::Vec3 s = v3(es[e]["start"]), t = v3(es[e]["end"]), en = v3(es[e]["normal"]);
            const core::Vec3 d = t - s;
            const float L2 = d.x * d.x + d.z * d.z;
            if (L2 < 1e-6f) continue;
            for (size_t n = 0; n < ns.size(); ++n) {
                const int to = ns[n]["cell"].asInt(-1);
                if (to < 0 || (size_t)to >= cells_.size()) continue;
                float lo = 1e9f, hi = -1e9f;
                for (const core::Vec3& p : cells_[(size_t)to].poly) {
                    const float u = ((p.x - s.x) * d.x + (p.z - s.z) * d.z) / L2;
                    lo = std::min(lo, u); hi = std::max(hi, u);
                }
                lo = std::max(lo, 0.0f); hi = std::min(hi, 1.0f);
                if (hi - lo < 1e-4f) continue;
                Portal P; P.to = to; P.a = s + d * lo; P.b = s + d * hi; P.width = std::sqrt(L2) * (hi - lo); P.n = en;
                cells_[i].portals.push_back(P);
            }
        }
    }
    const assets::Json& ls = j["links"];
    for (size_t i = 0; i < ls.size(); ++i) {
        const assets::Json& l = ls[i];
        Link L;
        L.from = l["from_cell"].asInt(-1); L.to = l["to_cell"].asInt(-1);
        if (L.from < 0 || L.to < 0 || (size_t)L.from >= cells_.size() || (size_t)L.to >= cells_.size()) continue;
        L.jump = l["kind"].asString() == "jump_up";
        L.robot = L.vehicle = false;
        const assets::Json& fm = l["forms"];
        for (size_t k = 0; k < fm.size(); ++k) { L.robot |= fm[k].asString() == "robot"; L.vehicle |= fm[k].asString() == "vehicle"; }
        if (fm.size() == 0) L.robot = true;
        L.fromPos = v3(l["from_gltf"]); L.toPos = v3(l["to_gltf"]);
        cells_[(size_t)L.from].links.push_back((int)links_.size());
        links_.push_back(L);
    }
    const assets::Json& as = j["anchors"];
    for (size_t i = 0; i < as.size(); ++i) {
        Anchor A; A.actor = as[i]["actor"].asString(); A.kind = as[i]["kind"].asString();
        A.pos = v3(as[i]["location_gltf"]); A.cell = as[i]["cell"].asInt(-1);
        A.approachCell = as[i]["approach_cell"].asInt(A.cell);
        if (A.approachCell >= (int)cells_.size()) A.approachCell = -1;
        anchors_.push_back(A);
    }
    // Bucket grid.
    if (!cells_.empty()) {
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (const Cell& c : cells_) { x0 = std::min(x0, c.bmin.x); z0 = std::min(z0, c.bmin.z); x1 = std::max(x1, c.bmax.x); z1 = std::max(z1, c.bmax.z); }
        gx0_ = x0; gz0_ = z0;
        gw_ = std::max(1, (int)std::ceil((x1 - x0) / gcell_) + 1); gh_ = std::max(1, (int)std::ceil((z1 - z0) / gcell_) + 1);
        grid_.assign((size_t)gw_ * (size_t)gh_, {});
        for (size_t i = 0; i < cells_.size(); ++i) {
            const Cell& c = cells_[i];
            const int ax = std::clamp((int)((c.bmin.x - gx0_) / gcell_), 0, gw_ - 1), bx = std::clamp((int)((c.bmax.x - gx0_) / gcell_), 0, gw_ - 1);
            const int az = std::clamp((int)((c.bmin.z - gz0_) / gcell_), 0, gh_ - 1), bz = std::clamp((int)((c.bmax.z - gz0_) / gcell_), 0, gh_ - 1);
            for (int z = az; z <= bz; ++z) for (int x = ax; x <= bx; ++x) grid_[(size_t)z * (size_t)gw_ + (size_t)x].push_back((int)i);
        }
    }
    // Connected pieces (portals + links, undirected).
    piece_.assign(cells_.size(), -1);
    std::vector<std::vector<int>> adj(cells_.size());
    for (size_t i = 0; i < cells_.size(); ++i) for (const Portal& p : cells_[i].portals) { adj[i].push_back(p.to); adj[(size_t)p.to].push_back((int)i); }
    for (const Link& l : links_) { adj[(size_t)l.from].push_back(l.to); adj[(size_t)l.to].push_back(l.from); }
    int pid = 0, best = 0;
    for (size_t i = 0; i < cells_.size(); ++i) {
        if (piece_[i] >= 0) continue;
        int n = 0; std::vector<int> st{(int)i}; piece_[i] = pid;
        while (!st.empty()) { int c = st.back(); st.pop_back(); ++n; for (int q : adj[(size_t)c]) if (piece_[(size_t)q] < 0) { piece_[(size_t)q] = pid; st.push_back(q); } }
        if (n > best) { best = n; mainPieceId_ = pid; }
        ++pid;
    }
    mainPiece_ = best;
    LOG_INFO("botnav: %s: %zu cells, %zu links, %zu anchors, %d pieces (main %d cells)", path.c_str(), cells_.size(), links_.size(), anchors_.size(), pid, best);
    return valid();
}

bool BotNav::inside(const Cell& c, float x, float z) const {
    if (x < c.bmin.x - 1e-3f || x > c.bmax.x + 1e-3f || z < c.bmin.z - 1e-3f || z > c.bmax.z + 1e-3f) return false;
    const size_t n = c.poly.size();
    if (n < 3) return false;
    int sign = 0;
    for (size_t i = 0; i < n; ++i) {
        const core::Vec3& a = c.poly[i]; const core::Vec3& b = c.poly[(i + 1) % n];
        const float cr = cross2(b.x - a.x, b.z - a.z, x - a.x, z - a.z);
        if (std::fabs(cr) < 1e-5f) continue;
        const int s = cr > 0 ? 1 : -1;
        if (sign == 0) sign = s; else if (s != sign) return false;
    }
    return true;
}

float BotNav::heightAt(const Cell& c, float x, float z) const {
    // Inverse-distance blend of the polygon's vertex heights (cells are near-planar: flat or one ramp slope).
    if (c.bmax.y - c.bmin.y < 0.01f) return c.centroid.y;
    float ws = 0.0f, hs = 0.0f;
    for (const core::Vec3& p : c.poly) { const float d = std::sqrt((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z)) + 1e-3f; ws += 1.0f / d; hs += p.y / d; }
    return ws > 0 ? hs / ws : c.centroid.y;
}

int BotNav::findCell(const core::Vec3& p, float maxDist, float maxDrop) const {
    if (grid_.empty()) return -1;
    const int gx = (int)std::floor((p.x - gx0_) / gcell_), gz = (int)std::floor((p.z - gz0_) / gcell_);
    int best = -1; float bestDy = 1e9f;
    if (gx >= 0 && gz >= 0 && gx < gw_ && gz < gh_)
        for (int ci : grid_[(size_t)gz * (size_t)gw_ + (size_t)gx]) {
            const Cell& c = cells_[(size_t)ci];
            if (!inside(c, p.x, p.z)) continue;
            const float h = heightAt(c, p.x, p.z);
            const float dy = p.y - h;
            if (dy < -1.5f || dy > 6.0f) continue;          // the floor under the pawn's feet (feet ~ on it; airborne up to 6 m)
            if (std::fabs(dy) < bestDy) { bestDy = std::fabs(dy); best = ci; }
        }
    if (best >= 0 || maxDist <= 0.0f) return best;
    // Off the mesh: the nearest cell edge point within maxDist and a plausible height.
    const int r = (int)std::ceil(maxDist / gcell_);
    float bestD = maxDist * maxDist + maxDrop * maxDrop * 0.25f;
    for (int z = gz - r; z <= gz + r; ++z) for (int x = gx - r; x <= gx + r; ++x) {
        if (x < 0 || z < 0 || x >= gw_ || z >= gh_) continue;
        for (int ci : grid_[(size_t)z * (size_t)gw_ + (size_t)x]) {
            const Cell& c = cells_[(size_t)ci];
            const float cx = std::clamp(p.x, c.bmin.x, c.bmax.x), cz = std::clamp(p.z, c.bmin.z, c.bmax.z);
            const float dy = p.y - heightAt(c, cx, cz);
            if (dy < -2.0f || dy > maxDrop) continue;
            const float d = (cx - p.x) * (cx - p.x) + (cz - p.z) * (cz - p.z) + dy * dy * 0.25f;
            if (d < bestD) { bestD = d; best = ci; }
        }
    }
    return best;
}

bool BotNav::usable(int cell, const Agent& a) const {
    if (cell < 0 || (size_t)cell >= cells_.size()) return false;
    const Cell& c = cells_[(size_t)cell];
    if (a.vehicle) return c.vehicle;
    // Robots: every cell (the generator keeps cells with clearance >= 1 m). clearance_m is the MINIMUM over a merged cell (most
    // big cells read 1.5 m because of one tight corner), so it is a cost, not a filter, for wider chassis (see cellCost).
    (void)a;
    return true;
}

bool BotNav::beginSearch(const core::Vec3& from, const core::Vec3& to, const Agent& a) const {
    Search& S = search_;
    S = Search{};
    S.from = from; S.to = to; S.a = a;
    S.s = findCell(from, 12.0f);
    if (S.s < 0) S.s = findCell(from, 25.0f, 34.0f);   // stranded on a prop / ledge: the floor it can drop to (MaxFallHeight 34 m)
    S.g = findCell(to, 12.0f);
    if (S.s < 0 || S.g < 0) return false;
    if (S.s == S.g || piece_[(size_t)S.s] != piece_[(size_t)S.g]) { S.active = true; S.done = true; S.trivial = S.s == S.g; return S.trivial; }
    const size_t N = cells_.size();
    if (gs_.size() != N) { gs_.assign(N, 0.0f); came_.assign(N, -1); viaLink_.assign(N, -1); stamp_.assign(N, 0); closedStamp_.assign(N, 0); gen_ = 0; }
    if (++gen_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); std::fill(closedStamp_.begin(), closedStamp_.end(), 0u); gen_ = 1; }
    S.G = gen_;
    S.goalC = cells_[(size_t)S.g].centroid;
    stamp_[(size_t)S.s] = S.G; gs_[(size_t)S.s] = 0.0f; came_[(size_t)S.s] = -1; viaLink_[(size_t)S.s] = -1;
    S.heap.push_back({kSearchWeight * core::length(cells_[(size_t)S.s].centroid - S.goalC), S.s});
    S.nearest = S.s; S.nearestD = core::length(cells_[(size_t)S.s].centroid - S.goalC);
    S.active = true;
    return true;
}

// Expand up to maxExpansions cells of the active search. 0 = still running, 1 = finished (finishSearch builds the result).
int BotNav::stepSearch(int maxExpansions) const {
    Search& S = search_;
    if (!S.active) return 1;
    if (S.done) return 1;
    const unsigned G = S.G;
    const Agent& a = S.a;
    const int g = S.g;
    auto gsAt = [&](int c) -> float { return stamp_[(size_t)c] == G ? gs_[(size_t)c] : 1e30f; };
    auto setNode = [&](int c, float gv, int from, int link) { stamp_[(size_t)c] = G; gs_[(size_t)c] = gv; came_[(size_t)c] = from; viaLink_[(size_t)c] = link; };
    auto isClosed = [&](int c) { return closedStamp_[(size_t)c] == G; };
    using QE = std::pair<float, int>;
    auto push = [&](float f, int c) { S.heap.push_back({f, c}); std::push_heap(S.heap.begin(), S.heap.end(), std::greater<QE>()); };
    int budget = maxExpansions;
    while (!S.heap.empty()) {
        if (budget-- <= 0) return 0;
        std::pop_heap(S.heap.begin(), S.heap.end(), std::greater<QE>());
        const int c = S.heap.back().second; S.heap.pop_back();
        if (isClosed(c)) continue;
        closedStamp_[(size_t)c] = G; ++S.n;
        if (c == g) break;
        { const float d = core::length(cells_[(size_t)c].centroid - S.goalC); if (d < S.nearestD) { S.nearestD = d; S.nearest = c; } }
        if (S.n > 8000) break;   // one-way (drop-only) unreachable goals would exhaust the mesh: give up, the bot picks another goal
        const Cell& cc = cells_[(size_t)c];
        for (const Portal& p : cc.portals) {
            if (isClosed(p.to) || (!usable(p.to, a) && p.to != g)) continue;
            const core::Vec3 mid = (p.a + p.b) * 0.5f;
            float tight = (!a.vehicle && cells_[(size_t)p.to].clearance < a.radius) ? 1.3f : 1.0f;   // prefer roomy cells [PROV]
            if (a.avoid && std::find(a.avoid->begin(), a.avoid->end(), p.to) != a.avoid->end()) tight *= 10.0f;
            const float ng = gsAt(c) + (core::length(mid - cc.centroid) + core::length(cells_[(size_t)p.to].centroid - mid)) * tight;
            if (ng < gsAt(p.to)) { setNode(p.to, ng, c, -1); push(ng + kSearchWeight * core::length(cells_[(size_t)p.to].centroid - S.goalC), p.to); }
        }
        for (int li : cc.links) {
            const Link& l = links_[(size_t)li];
            if (isClosed(l.to) || (a.vehicle ? !l.vehicle : !l.robot)) continue;
            if (!usable(l.to, a) && l.to != g) continue;
            const float ng = gsAt(c) + core::length(l.fromPos - cc.centroid) + core::length(l.toPos - l.fromPos) * 1.5f + 3.0f +
                             core::length(cells_[(size_t)l.to].centroid - l.toPos);
            if (ng < gsAt(l.to)) { setNode(l.to, ng, c, li); push(ng + kSearchWeight * core::length(cells_[(size_t)l.to].centroid - S.goalC), l.to); }
        }
    }
    S.done = true;
    return 1;
}

bool BotNav::finishSearch(std::vector<Waypoint>& out, int* expanded) const {
    Search& S = search_;
    out.clear();
    if (!S.active || !S.done) return false;
    S.active = false;
    if (expanded) *expanded = S.n;
    if (S.trivial) { out.push_back({S.to, 0, S.g}); return true; }
    if (S.G == 0) return false;   // different pieces
    int g = S.g;
    core::Vec3 endPos = S.to;
    if (stamp_[(size_t)g] != S.G || came_[(size_t)g] < 0) {
        // Unreachable goal (one-way pieces, a point off the walkable set): the reachable cell nearest to it, when close enough -
        // the bot gets as near as the mesh allows instead of standing still.
        if (S.nearest == S.s || S.nearestD > 30.0f) return false;
        g = S.nearest; endPos = cells_[(size_t)g].centroid;
    }
    return buildPath(S.s, g, S.from, endPos, S.a, out);
}

bool BotNav::findPath(const core::Vec3& from, const core::Vec3& to, const Agent& a, std::vector<Waypoint>& out, int* expanded) const {
    out.clear();
    if (!beginSearch(from, to, a)) { search_.active = false; return false; }
    while (stepSearch(1 << 30) == 0) {}
    return finishSearch(out, expanded);
}

// The corridor of cells s .. g (came_ chain of the finished search) as string-pulled waypoints ending at endPos.
bool BotNav::buildPath(int s, int g, const core::Vec3& from, const core::Vec3& endPos, const Agent& a, std::vector<Waypoint>& out) const {
    std::vector<int> chain;
    for (int c = g; c >= 0; c = came_[(size_t)c]) { chain.push_back(c); if (c == s) break; }
    std::reverse(chain.begin(), chain.end());
    // String pull (simple stupid funnel) per run of portal steps; links are hard corners with an action.
    const float r = a.vehicle ? 1.5f : std::min(a.radius, 1.2f);
    core::Vec3 apex = from;
    std::vector<std::pair<core::Vec3, core::Vec3>> portals;   // (left, right) relative to travel
    std::vector<int> portalCell;
    auto flush = [&](const core::Vec3& end, int endCell) {
        portals.push_back({end, end}); portalCell.push_back(endCell);
        core::Vec3 L = portals[0].first, R = portals[0].second; size_t li = 0, ri = 0;
        // Signed double area in the funnel's handedness (portals are (left, right) by cross2 of the edge normal; verified with
        // WFC_BOTNAVTEST: the opposite sign string-pulls through walls).
        auto tri = [](const core::Vec3& a0, const core::Vec3& b0, const core::Vec3& c0) { return -cross2(b0.x - a0.x, b0.z - a0.z, c0.x - a0.x, c0.z - a0.z); };
        for (size_t i = 1; i < portals.size(); ++i) {
            const core::Vec3& pl = portals[i].first; const core::Vec3& pr = portals[i].second;
            if (tri(apex, R, pr) <= 0.0f) {
                if ((apex.x == R.x && apex.z == R.z) || tri(apex, L, pr) > 0.0f) { R = pr; ri = i; }
                else { out.push_back({L, 0, portalCell[li]}); apex = L; size_t k = li; L = R = apex; li = ri = k; i = k; continue; }
            }
            if (tri(apex, L, pl) >= 0.0f) {
                if ((apex.x == L.x && apex.z == L.z) || tri(apex, R, pl) < 0.0f) { L = pl; li = i; }
                else { out.push_back({R, 0, portalCell[ri]}); apex = R; size_t k = ri; L = R = apex; li = ri = k; i = k; continue; }
            }
        }
        out.push_back({end, 0, endCell});
        portals.clear(); portalCell.clear();
    };
    portals.push_back({apex, apex}); portalCell.push_back(s);
    for (size_t i = 1; i < chain.size(); ++i) {
        const int c0 = chain[i - 1], c1 = chain[i];
        const int li = viaLink_[(size_t)c1];
        if (li >= 0) {
            const Link& l = links_[(size_t)li];
            flush(l.fromPos, c0);
            out.push_back({l.toPos, l.jump ? 1 : 2, c1});
            apex = l.toPos; portals.push_back({apex, apex}); portalCell.push_back(c1);
            continue;
        }
        const Portal* P = nullptr;
        for (const Portal& p : cells_[(size_t)c0].portals) if (p.to == c1) { P = &p; break; }
        if (!P) continue;
        // Shrink by the agent radius; orient (left, right) by the travel direction c0 -> c1.
        core::Vec3 a0 = P->a, b0 = P->b;
        const core::Vec3 ab = b0 - a0; const float w = std::sqrt(ab.x * ab.x + ab.z * ab.z);
        const float sh = std::min(r, std::max(0.0f, w * 0.5f - 0.05f));
        if (w > 1e-4f) { const core::Vec3 u = ab * (1.0f / w); a0 = a0 + u * sh; b0 = b0 - u * sh; }
        // Left / right seen travelling through the edge along its outward normal (c0 -> c1).
        const core::Vec3 mid = (a0 + b0) * 0.5f;
        if (cross2(P->n.x, P->n.z, a0.x - mid.x, a0.z - mid.z) > 0.0f) portals.push_back({a0, b0});
        else portals.push_back({b0, a0});
        portalCell.push_back(c1);
    }
    flush(endPos, g);
    // Multi-level corridors: a 2D string-pull can join two corners over open air (an upper walkway, the street 20 m below, another
    // walkway). Keep a corner-to-corner segment only when it stays on connected cells; otherwise walk that stretch through the
    // corridor's portal midpoints.
    {
        std::vector<Waypoint> fixed;
        core::Vec3 prev = from;
        size_t ci = 0;
        for (const Waypoint& w : out) {
            size_t cj = ci;
            while (cj < chain.size() && chain[cj] != w.cell) ++cj;
            if (w.action == 0 && cj < chain.size() && cj > ci + 1 && !directWalkable(prev, w.pos, a)) {
                for (size_t k = ci + 1; k < cj; ++k) {
                    for (const Portal& p : cells_[(size_t)chain[k - 1]].portals)
                        if (p.to == chain[k]) { fixed.push_back({(p.a + p.b) * 0.5f, 0, chain[k]}); break; }
                }
            }
            fixed.push_back(w);
            prev = w.pos;
            if (cj < chain.size()) ci = cj;
        }
        out.swap(fixed);
    }
    // Drop the duplicate start point and points closer than 0.5 m to the previous one.
    std::vector<Waypoint> pruned;
    core::Vec3 prev = from;
    for (const Waypoint& w : out) {
        if (w.action == 0 && core::length(w.pos - prev) < 0.5f && &w != &out.back()) continue;
        pruned.push_back(w); prev = w.pos;
    }
    out.swap(pruned);
    return !out.empty();
}

int BotNav::approachCellNear(const core::Vec3& p) const {
    int best = -1; float bestD = 3.0f * 3.0f;
    for (const Anchor& an : anchors_) {
        const float dx = an.pos.x - p.x, dz = an.pos.z - p.z, d = dx * dx + dz * dz;
        if (d < bestD && an.approachCell >= 0) { bestD = d; best = an.approachCell; }
    }
    return best;
}

core::Vec3 BotNav::randomPoint(unsigned seed, const Agent& a) const {
    if (cells_.empty()) return {0, 0, 0};
    for (int k = 0; k < 64; ++k) {
        const size_t i = hashU(seed + (unsigned)k * 7919U) % cells_.size();
        if (piece_[i] == mainPieceId_ && usable((int)i, a) && cells_[i].clearance >= 1.5f) return cells_[i].centroid;
    }
    return cells_[0].centroid;
}

bool BotNav::directWalkable(const core::Vec3& from, const core::Vec3& to, const Agent& a) const {
    const core::Vec3 d = to - from;
    const float L = std::sqrt(d.x * d.x + d.z * d.z);
    const int steps = std::max(1, (int)(L / 0.75f));
    int prev = findCell(from, 0.0f);
    if (prev < 0) return false;
    float y = from.y;
    for (int i = 1; i <= steps; ++i) {
        const float t = (float)i / (float)steps;
        const core::Vec3 p{from.x + d.x * t, y, from.z + d.z * t};
        const int c = findCell(p, 0.0f);
        if (c < 0 || !usable(c, a)) return false;
        if (c != prev) {
            // Adjacent, or touching at a corner (a segment through a grid vertex crosses two cells in one sample).
            bool adj = false;
            for (const Portal& q : cells_[(size_t)prev].portals) {
                if (q.to == c) { adj = true; break; }
                for (const Portal& q2 : cells_[(size_t)q.to].portals) if (q2.to == c) { adj = true; break; }
                if (adj) break;
            }
            if (!adj) return false;
            prev = c;
        }
        y = heightAt(cells_[(size_t)c], p.x, p.z);
    }
    return true;
}

} // namespace game
