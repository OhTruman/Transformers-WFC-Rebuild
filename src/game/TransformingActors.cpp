// Clean-room reconstruction: MP transforming world actors (see TransformingActors.h).
#include "game/TransformingActors.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include "assets/Json.h"
#include "assets/SkinnedModel.h"
#include "core/Log.h"
#include "game/BotNav.h"
#include "game/Collision.h"

namespace game {

namespace {
core::Mat4 matFrom(const assets::Json& a) {   // 16 floats, column-major (glTF)
    core::Mat4 m;
    for (int i = 0; i < 16 && (size_t)i < a.size(); ++i) m.m[i] = a[(size_t)i].asFloat(m.m[i]);
    return m;
}
// 12 triangles of the box (local frame x half extents), 3 vertices each.
std::vector<core::Vec3> boxTris(const core::Mat4& local, const core::Vec3& h) {
    core::Vec3 c[8];
    for (int i = 0; i < 8; ++i) c[i] = core::transformPoint(local, core::Vec3{(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z});
    static const int f[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5}, {0, 4, 5}, {0, 5, 1}, {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
    std::vector<core::Vec3> out;
    for (const auto& t : f) for (int k = 0; k < 3; ++k) out.push_back(c[t[k]]);
    return out;
}
} // namespace

bool TransformingActors::load(const std::string& mapName, const std::string& manifestRoot, CollisionWorld& pawn, CollisionWorld* weapon) {
    campaign_ = false;
    if (loadFile(manifestRoot + "/maps/mp_transforming_actors.json", mapName, manifestRoot + "/..", pawn, weapon) && !actors_.empty()) return true;
    // the DLC maps (District): AssetTools writes their rows to a per-map file in the campaign format instead
    return loadFile(manifestRoot + "/maps/" + mapName + "/transforming_actors_gameplay.json", mapName, manifestRoot + "/..", pawn, weapon);
}

bool TransformingActors::loadChapter(const std::string& path, const std::string& toolsRoot, CollisionWorld& pawn, CollisionWorld* weapon) {
    campaign_ = true;
    return loadFile(path, std::string(), toolsRoot, pawn, weapon);
}

bool TransformingActors::loadFile(const std::string& path, const std::string& mapName, const std::string& toolsRoot, CollisionWorld& pawn, CollisionWorld* weapon) {
    actors_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    assets::Json root;
    if (!assets::Json::parse(ss.str(), root)) { LOG_WARN("transforming actors: cannot parse %s", path.c_str()); return false; }
    // glb paths are AssetTools-relative ("manifests/maps/transforming_actors/..."): toolsRoot
    std::map<std::string, std::shared_ptr<assets::SkinnedModel>> models;
    const assets::Json& acts = root["actors"];
    for (size_t i = 0; i < acts.size(); ++i) {
        const assets::Json& j = acts[i];
        if (!mapName.empty() && j["map"].asString() != mapName) continue;   // (a chapter file: every row)
        const std::string meshName = j["mesh"].asString();
        const assets::Json& m = root["meshes"][meshName];
        Actor a;
        a.name = j["actor"].asString(); a.mesh = meshName;
        a.glb = toolsRoot + "/" + m["glb"].asString(); a.clip = m["clip"].asString();
        a.animSet = j["animset"].asString(); a.sequence = j["anim_sequence"].asString(); a.skeletalMesh = j["skeletal_mesh"].asString();
        a.length = std::max(0.01f, m["clip_length_s"].asFloat(1.0f));
        a.matrix = matFrom(j["gltf_matrix"]);
        a.bridge = meshName.rfind("NEU_Bridge", 0) == 0;
        a.startTransformed = j["start_transformed"].asBool(false) || j["initial_state"].asString() == "Transformed";
        a.baseMatrix = a.matrix;
        // Level start: a Transform input fired directly by SeqEvent_SequenceActivated (Rust; the op's SeqCond_GameRuleActive inputs are
        // dead authoring, RE CONFIRMED) -> Transform at match start in every mode.
        const assets::Json& drivers = j["drivers"];
        for (size_t d = 0; d < drivers.size(); ++d)
            for (size_t in = 0; in < drivers[d]["inputs"].size(); ++in) {
                const assets::Json& inp = drivers[d]["inputs"][in];
                if (inp["input"].asString() != "Transform") continue;
                for (size_t k = 0; k < inp["fired_by_direct"].size(); ++k)
                    if (inp["fired_by_direct"][k]["class"].asString() == "SeqEvent_SequenceActivated") a.startDesired = 1;
            }
        auto& mdl = models[a.glb];
        if (!mdl) {
            mdl = std::make_shared<assets::SkinnedModel>();
            if (!assets::loadSkinnedGlb(a.glb, *mdl)) { LOG_WARN("transforming actors: cannot load %s", a.glb.c_str()); mdl.reset(); }
        }
        if (!mdl) continue;
        a.model = mdl;
        a.clipIndex = mdl->clipByName(a.clip);
        if (a.clipIndex < 0 && !mdl->clips.empty()) a.clipIndex = 0;   // one clip per GLB
        const assets::Json& bodies = m["bodies"];
        for (size_t b = 0; b < bodies.size(); ++b) {
            Body body; body.node = mdl->nodeByName(bodies[b]["bone"].asString());
            if (body.node < 0) { LOG_WARN("transforming actors: %s: bone %s missing", meshName.c_str(), bodies[b]["bone"].asString().c_str()); continue; }
            std::vector<core::Vec3> tris;
            const assets::Json& boxes = bodies[b]["boxes"];
            for (size_t k = 0; k < boxes.size(); ++k) {
                Box bx; bx.body = (int)a.bodies.size(); bx.local = matFrom(boxes[k]["box_local_gltf"]);
                const assets::Json& e = boxes[k]["box_extent_gltf"];
                bx.half = core::Vec3{e[0].asFloat(0) * 0.5f, e[1].asFloat(0) * 0.5f, e[2].asFloat(0) * 0.5f};
                const std::vector<core::Vec3> t = boxTris(bx.local, bx.half);
                tris.insert(tris.end(), t.begin(), t.end());
                a.boxes.push_back(bx);
            }
            body.pawnSet = pawn.addDynamicSet(tris, core::Mat4::identity());
            if (weapon) body.weaponSet = weapon->addDynamicSet(tris, core::Mat4::identity());
            a.bodies.push_back(body);
        }
        actors_.push_back(std::move(a));
    }
    resetForNewMatch(pawn, weapon);
    if (!actors_.empty()) LOG_INFO("transforming actors: %s: %zu actors", mapName.c_str(), actors_.size());
    return true;
}

void TransformingActors::resetForNewMatch(CollisionWorld& pawn, CollisionWorld* weapon, bool escalation) {
    for (Actor& a : actors_) {
        if (!escalation && !campaign_ && a.startDesired == 0 && !a.bridge) {
            // Versus on the Escalation-only maps (BrokenHope / Remnant: CompatibleGameTypes SV, RE MP_TRANSFORMING_ACTORS_RE.md section 4):
            // no authored versus state. Integration decision 2026-10-17 (option c): the doors Transformed from the start (t = length, no
            // animation; the late-Escalation layout). The BrokenHope bridges keep their Escalation start state (retracted, hidden, no
            // collision) until AssetTools bakes the extended decks into BrokenHope's bot_nav; then they extend in versus too.
            a.t = a.length; a.desired = 1; a.hidden = false; a.collide = true;
            a.posedT = -1.0f;
            pose(a, pawn, weapon);
            continue;
        }
        a.t = campaign_ && a.startTransformed ? a.length : 0.0f;   // Escalation (and Rust in every mode): Untransformed at the start (bridges: PreGameplaySetup's Untransform); campaign: authored
        a.desired = campaign_ && a.startTransformed ? 1 : a.startDesired;   // Rust doors: Transform at level start
        a.hidden = a.bridge;                 // bridges: Hide + ChangeCollision NoCollision at setup
        a.collide = !a.bridge;
        a.posedT = -1.0f;
        pose(a, pawn, weapon);
    }
}

core::Mat4 TransformingActors::boneWorld(const Actor& a, int node, float t) const {
    if (!a.model || node < 0) return a.matrix;
    assets::LocalPose lp;
    std::vector<core::Mat4> g;
    if (a.clipIndex >= 0) assets::samplePose(*a.model, a.clipIndex, std::clamp(t, 0.0f, a.length), false, lp);
    else assets::samplePose(*a.model, -1, 0.0f, false, lp);
    assets::poseGlobals(*a.model, lp, g);
    return (size_t)node < g.size() ? a.matrix * g[(size_t)node] : a.matrix;
}

core::Mat4 TransformingActors::boneWorldByName(const Actor& a, const std::string& bone) const {
    return boneWorld(a, a.model ? a.model->nodeByName(bone) : -1, a.t);
}

void TransformingActors::pose(Actor& a, CollisionWorld& pawn, CollisionWorld* weapon) {
    for (const Body& b : a.bodies) {
        const core::Mat4 w = boneWorld(a, b.node, a.t);
        if (b.pawnSet >= 0) { pawn.setDynamicPose(b.pawnSet, w); pawn.setDynamicEnabled(b.pawnSet, a.collide); }
        if (weapon && b.weaponSet >= 0) { weapon->setDynamicPose(b.weaponSet, w); weapon->setDynamicEnabled(b.weaponSet, a.collide); }
    }
    a.posedT = a.t;
}

void TransformingActors::tick(float dt, CollisionWorld& pawn, CollisionWorld* weapon) {
    for (Actor& a : actors_) {
        const float target = a.desired ? a.length : 0.0f;
        if (a.t < target) a.t = std::min(target, a.t + dt);   // rate +1 / -1 (one clip length per transform)
        else if (a.t > target) a.t = std::max(target, a.t - dt);
        if (a.t != a.posedT) pose(a, pawn, weapon);
    }
}

void TransformingActors::boxBounds(const Actor& a, const Box& b, float t, core::Vec3& lo, core::Vec3& hi) const {
    const core::Mat4 w = boneWorld(a, a.bodies[(size_t)b.body].node, t) * b.local;
    lo = core::Vec3{1e30f, 1e30f, 1e30f}; hi = core::Vec3{-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 8; ++i) {
        const core::Vec3 p = core::transformPoint(w, core::Vec3{(i & 1) ? b.half.x : -b.half.x, (i & 2) ? b.half.y : -b.half.y, (i & 4) ? b.half.z : -b.half.z});
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
}

int TransformingActors::applyNavBlocks(BotNav& nav) const {
    if (!nav.valid()) return 0;
    std::vector<uint8_t> blocked(nav.cells().size(), 0);
    for (const Actor& a : actors_) {
        if (!a.collide || a.bridge) continue;   // a bridge deck is a walkable surface, not a wall
        const float rest = a.desired ? a.length : 0.0f;
        for (const Box& b : a.boxes) {
            core::Vec3 lo, hi;
            boxBounds(a, b, rest, lo, hi);
            for (size_t c = 0; c < nav.cells().size(); ++c) {
                const BotNav::Cell& cell = nav.cells()[c];
                // A floor cell under the box: overlapping by > 0.5 m on both horizontal axes, its floor within the box's height span.
                const float ox = std::min(hi.x, cell.bmax.x) - std::max(lo.x, cell.bmin.x), oz = std::min(hi.z, cell.bmax.z) - std::max(lo.z, cell.bmin.z);
                if (ox > 0.5f && oz > 0.5f && cell.centroid.y > lo.y - 1.0f && cell.centroid.y < hi.y) blocked[c] = 1;
            }
        }
    }
    int n = 0;
    for (size_t c = 0; c < blocked.size(); ++c) { nav.setBlocked((int)c, blocked[c] != 0); n += blocked[c]; }
    if (n > 0) nav.recomputePieces();   // sealed-off areas leave the main piece
    return n;
}

} // namespace game
