// Clean-room reconstruction — bot navigation runtime over the reconstructed per-map nav data.
// The versus maps shipped NO navigation for bots (only campaign-style PathNode / ReachSpec reach graphs; RE mp_navigation_audit).
// AssetTools scripts/wfc/bot_nav.py rebuilds a walkable cell mesh per map from the collision (Escalation NavigationCell schema,
// CONFIRMED movement constants: step 0.37 m, WalkableFloorZ 0.7, jump apex 5 m, max fall 34 m, robot / vehicle clearance) and
// writes Maps/<map>/bot_nav.json. This runtime loads it for any map (nothing map-specific here): cell lookup, A* over cells +
// jump / drop links, and a string-pulled corridor of corners for the steering layer. PC ADAPTATION (offline bots).
#pragma once
#include "core/Math.h"
#include <string>
#include <vector>

namespace game {

class BotNav {
public:
    struct Portal { int to; core::Vec3 a, b; float width; core::Vec3 n; };   // shared edge segment toward cell `to`; n = outward edge normal
    struct Link { int from, to; bool jump; bool robot, vehicle; core::Vec3 fromPos, toPos; };
    struct Cell {
        std::vector<core::Vec3> poly;                                   // convex polygon (glTF metres, Y up)
        core::Vec3 centroid, bmin, bmax;
        float clearance = 0.0f, headroom = 0.0f;
        bool vehicle = false;
        std::vector<Portal> portals;
        std::vector<int> links;                                          // indices into links_ starting here
    };
    struct Anchor { std::string actor, kind; core::Vec3 pos; int cell = -1; };
    // One step of a path: move to `pos`; `action` 1 = jump (up link) before / while going to pos, 2 = drop (walk off the edge).
    struct Waypoint { core::Vec3 pos; int action = 0; int cell = -1; };
    struct Agent { float radius = 1.75f; bool vehicle = false; };

    bool load(const std::string& path);
    bool valid() const { return !cells_.empty(); }
    const std::vector<Cell>& cells() const { return cells_; }
    const std::vector<Anchor>& anchors() const { return anchors_; }
    int mainPieceSize() const { return mainPiece_; }

    // The cell under / nearest to p (within `maxDist` horizontally when p is off the mesh), or -1.
    int findCell(const core::Vec3& p, float maxDist = 4.0f) const;
    bool usable(int cell, const Agent& a) const;
    // A* over cells + links; then a corridor string-pulled through the portals (shrunk by the agent radius). Empty on failure.
    bool findPath(const core::Vec3& from, const core::Vec3& to, const Agent& a, std::vector<Waypoint>& out, int* expanded = nullptr) const;
    // A random usable cell centroid in the main connected piece (roaming goals), deterministic from `seed`.
    core::Vec3 randomPoint(unsigned seed, const Agent& a) const;
    // Straight-line walkability on the mesh (cells along the 2D segment are connected and usable).
    bool directWalkable(const core::Vec3& from, const core::Vec3& to, const Agent& a) const;

private:
    std::vector<Cell> cells_;
    std::vector<Link> links_;
    std::vector<Anchor> anchors_;
    std::vector<int> piece_;     // connected piece id per cell (portals + links, undirected)
    int mainPieceId_ = -1, mainPiece_ = 0;
    // xz bucket grid over cell bounds
    float gx0_ = 0, gz0_ = 0, gcell_ = 8.0f; int gw_ = 0, gh_ = 0;
    std::vector<std::vector<int>> grid_;
    bool inside(const Cell& c, float x, float z) const;
    float heightAt(const Cell& c, float x, float z) const;
};

} // namespace game
