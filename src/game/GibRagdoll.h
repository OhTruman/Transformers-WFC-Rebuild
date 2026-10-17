#pragma once
// [Gameplay] Robot skeletal gib ragdolls (TnSkeletalGibSpawner / TnSkeletalRigidBody; RE GAMEPLAY_SKELETAL_GIB_INIT_RE).
// One gib per robot death: GIB_Body or GIB_LowBody2 (50 / 50), spawned at PositionSocket with the pawn's Rotation in its mesh's
// REFERENCE pose, at rest (no impulse, no transferred velocity), its bodies from the PhysicsAsset (GibRagdollTable.inc), colliding with
// the world only (RBCC_EffectPhysics: not vehicles, not other effect physics).
// The solver is XPBD rigid bodies (positional joints + swing / twist limits, plane contacts generated once per step) at 8 substeps of
// the 60 Hz step [the solver and its constants PROV: PhysX 2 itself is not reproduced]. Presentation only: no sim state is read
// back; it sleeps (costs nothing) once settled.
#include <cstddef>
#include <vector>
#include "core/Math.h"

namespace assets { struct SkinnedModel; }

namespace game {

class CollisionWorld;

struct GibShapeDef { int kind; core::Vec3 c, axis; float r, halfLen; };          // kind 0 sphere, 1 capsule; bone frame, metres
struct GibBodyDef { const char* bone; int nShapes; GibShapeDef shapes[2]; };
struct GibJointDef { const char* child; const char* parent; float swing1, swing2, twist; int soft; core::Vec3 twistAxis, swingAxis; };

class GibRagdoll {
public:
    // world0: the gib actor's placement (PositionSocket, the pawn's Rotation) applied to the mesh's model space. False when the
    // model's skeleton lacks the table's bones.
    bool init(const assets::SkinnedModel& model, const core::Mat4& world0, const GibBodyDef* bodies, size_t nBodies,
              const GibJointDef* joints, size_t nJoints);
    // One 60 Hz step (dt clamped); a sleeping ragdoll returns at once.
    void step(float dt, const CollisionWorld* col);
    bool asleep() const { return asleep_; }
    bool valid() const { return !bodies_.empty(); }
    // World-space globals for every model node (pass world = identity to the skinned draw).
    void globals(std::vector<core::Mat4>& out) const;

private:
    struct Body {
        int node = -1;
        core::Vec3 x, xPrev, v, w;        // centre of mass (world), velocity, angular velocity (world)
        core::Quat q, qPrev;               // the bone frame's world orientation
        core::Vec3 com;                    // centre of mass in the bone frame
        float invM = 0.0f, invI = 0.0f, ext = 0.1f;
        struct Sphere { core::Vec3 c; float r; };   // relative to the COM, body frame
        std::vector<Sphere> spheres;
    };
    struct Joint {
        int child = -1, parent = -1;
        core::Vec3 aC, aP;                 // the anchor (the child bone origin) relative to each COM, body frames
        core::Quat rel0;                   // rest conj(qP) * qC
        core::Vec3 tA, sA, tB;             // twist / swing / third axes (child bone frame)
        float s1 = 0.0f, s2 = 0.0f, tw = 0.0f; bool soft = false;   // radians
    };
    struct Contact { int body; int sphere; core::Vec3 p, n; float lambda = 0.0f; core::Vec3 rWorld; float vnPre = 0.0f; };
    struct Follow { int body = -1; core::Mat4 rel; };   // non-body nodes: rigid in a body's bone frame

    std::vector<Body> bodies_;
    std::vector<Joint> joints_;
    std::vector<Follow> follow_;           // per model node (body nodes: body >= 0, rel identity)
    std::vector<Contact> contacts_;
    size_t nodeCount_ = 0;
    float quiet_ = 0.0f;
    bool asleep_ = false;

    void collide(const CollisionWorld& col, float dt);
    void solveJoints(float h);
    void solveContacts(float h);
    void velocityPass(float h);
};

// The robot gib templates (GibRagdollTable.inc): 0 GIB_Body, 1 GIB_LowBody2.
const GibBodyDef* gibBodies(int gib, size_t& n);
const GibJointDef* gibJoints(int gib, size_t& n);
const char* gibMeshPath(int gib);   // relative to the content root

} // namespace game
