// Clean-room reconstruction: MP transforming world actors (TnTransformingWorldActor: the interior doors, tube door and bridges of
// MP_ESC_BrokenHope / MP_ESC_Remnant / MP_IAC_Rust) [RE MP_TRANSFORMING_ACTORS_RE.md, CONFIRMED; data AssetTools
// manifests/maps/mp_transforming_actors.json + transforming_actors/<mesh>.glb].
// - The pose is one AnimNodeSequence: normalized position 0 = Untransformed, 1 = Transformed, played at rate +-1 in between.
// - Collision = the PhysicsAsset bodies at the animated bones: world box = actor gltf_matrix x bone_global(t) x box_local_gltf.
//   One dynamic collision set per body, in the pawn and weapon worlds.
// - Start states:
//   - BrokenHope / Remnant are Escalation-only maps in the original (CompatibleGameTypes SV, RE section 4): there the doors start
//     closed and open on the SurvivalGenerator's "New Power Level" for good; the bridges start retracted (PreGameplaySetup Untransform +
//     ChangeCollision NoCollision + Hide) and TransformBridgeN extends them (ChangeCollision + UnHide; their body bone does not animate).
//   - VERSUS on those two maps (no authored state; Integration decision 2026-10-17, option c): the doors start Transformed (open, no
//     animation: the late-Escalation layout); the bridges keep their Escalation start state (retracted, hidden, no collision) until
//     BrokenHope's bot_nav carries the extended decks (AssetTools, queued).
//   - Rust doors: Transform at level start in every mode (SequenceActivated -> TransformWorldProp).
// - Nav: the bot nav cells under a colliding actor's resting boxes are unusable (BotNav::setBlocked).
// Gameplay owns the state; Rendering draws each actor's skeletal mesh (glb) at its matrix with the clip at time t, unless hidden.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/Math.h"

namespace assets { struct SkinnedModel; }

namespace game {

class CollisionWorld;
class BotNav;

class TransformingActors {
public:
    struct Box { int body = 0; core::Mat4 local; core::Vec3 half{0, 0, 0}; };   // in the body bone's frame (glTF metres)
    struct Body { int node = -1; int pawnSet = -1, weaponSet = -1; };
    struct Actor {
        std::string name, mesh, glb, clip;   // glb: absolute path; clip: the transform sequence
        core::Mat4 matrix;                   // actor gltf_matrix
        float length = 1.0f;                 // clip_length_s
        float t = 0.0f;                      // clip time: 0 = Untransformed, length = Transformed
        int desired = 0;                     // DesiredTransformState: 0 Untransformed, 1 Transformed
        int startDesired = 0;                // at match start (Rust: Transform from SeqEvent_SequenceActivated)
        bool hidden = false, collide = true, bridge = false;
        std::vector<Body> bodies;
        std::vector<Box> boxes;
        std::shared_ptr<const assets::SkinnedModel> model;
        int clipIndex = -1;
        float posedT = -1.0f;                // the t the collision sets were last posed at
    };
    // Loads this map's actors (none: an empty list, nothing changes) and adds their collision sets to pawn / weapon.
    bool load(const std::string& mapName, const std::string& manifestRoot, CollisionWorld& pawn, CollisionWorld* weapon);
    void clear() { actors_.clear(); }
    // Kismet: TnSeqAct_TransformWorldProp Transform (1) / Untransform (0); SeqAct_ChangeCollision / ToggleHidden for the bridges.
    void setDesired(size_t i, int desired) { if (i < actors_.size()) actors_[i].desired = desired ? 1 : 0; }
    // Match start (a fresh level): the start states above, t at the start pose. escalation: the SV game type (not in the rebuild yet).
    void resetForNewMatch(CollisionWorld& pawn, CollisionWorld* weapon, bool escalation = false);
    // Advances t toward desired at rate 1 and poses the collision sets.
    void tick(float dt, CollisionWorld& pawn, CollisionWorld* weapon);
    // Blocks the nav cells under every colliding actor's boxes at its resting pose (desired end).
    int applyNavBlocks(BotNav& nav) const;
    const std::vector<Actor>& actors() const { return actors_; }
    // World AABB of one box of actor a at clip time t (tests / nav).
    void boxBounds(const Actor& a, const Box& b, float t, core::Vec3& lo, core::Vec3& hi) const;

private:
    std::vector<Actor> actors_;
    void pose(Actor& a, CollisionWorld& pawn, CollisionWorld* weapon);
    core::Mat4 boneWorld(const Actor& a, int node, float t) const;
};

} // namespace game
