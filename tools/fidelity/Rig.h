// WFC fidelity harness — deterministic, windowless simulation rig.
//
// Drives the PRODUCTION gameplay code (PlayerController -> CharacterMovement -> Weapon ->
// Character::updateAnimation) at a fixed step with scripted InputFrames, exactly in the order
// World::tick uses. No renderer, no wall clock: the same script always yields the same trace,
// which the in-game WFC_SMOKE_FRAMES path cannot guarantee (it integrates real frame time).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "assets/SkinnedModel.h"
#include "game/Character.h"
#include "game/CharacterMovement.h"
#include "game/Collision.h"
#include "game/Player.h"
#include "game/World.h"
#include "platform/Input.h"
#include "render/Camera.h"
#include "core/Config.h"

namespace fid {

// Optional Character accessors, detected at compile time so one harness builds against every
// branch (ab.ps1). Each returns -1 when the Character under test lacks the accessor.
namespace layer {
// Same pattern, kept defined for accessors declared further down (after the helper templates).
#define FID_OPTIONAL_ACCESSOR_LATE(name)                                                         \
    template <class C> auto name(const C& c, int) -> decltype((float)c.name()) { return (float)c.name(); } \
    template <class C> float name(const C&, long) { return -1.0f; }
#define FID_OPTIONAL_ACCESSOR(name)                                                              \
    template <class C> auto name(const C& c, int) -> decltype((float)c.name()) { return (float)c.name(); } \
    template <class C> float name(const C&, long) { return -1.0f; }
FID_OPTIONAL_ACCESSOR(reloadWeight)
FID_OPTIONAL_ACCESSOR(aimWeight)
FID_OPTIONAL_ACCESSOR(aimPitchNorm)
FID_OPTIONAL_ACCESSOR(upperWeight)       // agents/systems UpperBodyCustom slot
FID_OPTIONAL_ACCESSOR(moveForm)          // agents/gameplay Pass 11: movement/physics form (switches at fold start)
FID_OPTIONAL_ACCESSOR(fineAiming)        // PlayerController (Pass 11)
FID_OPTIONAL_ACCESSOR(fovXDeg)           // PlayerController smoothed FOV (Pass 11)
FID_OPTIONAL_ACCESSOR(transformProgress) // milestone-02 Character: normalized fold progress
FID_OPTIONAL_ACCESSOR(weaponUsable)      // milestone-02: weapon may fire (restored + equipped)
FID_OPTIONAL_ACCESSOR(weaponRestored)    // milestone-02: weapon restored during V->R
FID_OPTIONAL_ACCESSOR(hoverApplied)      // milestone-02: hover authority applied
FID_OPTIONAL_ACCESSOR(partnerShown)      // gameplay Pass 13+: second mesh drawn (transform overlap)
FID_OPTIONAL_ACCESSOR(armShown)          // gameplay Pass 13b+: separate Optimus arm mesh drawn
FID_OPTIONAL_ACCESSOR(handShrunk)        // gameplay Pass 14: HandSkelControl R_Arm04_Hand_XB scale 0.1
FID_OPTIONAL_ACCESSOR(rammedRemain)      // gameplay Pass 14: robot RammedReaction time remaining
#undef FID_OPTIONAL_ACCESSOR

// Vehicle rigid-body attitude / suspension (gameplay Pass 13+ VehicleState); valid=false otherwise.
struct VehAtt {
    bool valid = false, onGround = false;
    float pitch = 0, roll = 0;          // rad, UE sense (pitch + = nose up, roll + = right side down)
    core::Vec3 angVel;                  // body-local, UE axes (x roll, y pitch, z yaw)
    int contacts = 0;
    float springMean = -1;              // mean TnSpring length of the contacting probes (m)
};
template <class C> auto vehAttitude(const C& c, int) -> decltype(c.vehicleState().spLen[0], c.vehicleState().angVel, VehAtt()) {
    const auto& v = c.vehicleState();
    VehAtt a;
    a.valid = true; a.pitch = v.pitch; a.roll = v.roll; a.angVel = v.angVel; a.contacts = v.contacts;
    a.onGround = v.onTheGround;
    float s = 0; int n = 0;
    for (float l : v.spLen) if (l >= 0) { s += l; ++n; }
    a.springMean = n ? s / n : -1.0f;
    return a;
}
template <class C> VehAtt vehAttitude(const C&, long) { return VehAtt(); }
// Robot ram reaction / vehicle AddVelocity (Pass 14); return false when the build lacks them.
template <class C> auto ramRobot(C& c, const core::Vec3& d, int) -> decltype(c.rammedAsRobot(d), bool()) { c.rammedAsRobot(d); return true; }
template <class C> bool ramRobot(C&, const core::Vec3&, long) { return false; }
template <class C> auto addVehVelocity(C& c, const core::Vec3& v, int) -> decltype(c.addVelocityInVehicle(v), bool()) { c.addVelocityInVehicle(v); return true; }
template <class C> bool addVehVelocity(C&, const core::Vec3&, long) { return false; }

// Vehicle state machine snapshot (milestone-02 Character::vehicleState()); valid=false otherwise.
struct VehSnap {
    bool valid = false, driving = false;
    float ride = 0, drift = 0, dash = 0, dashCd = 0, nitro = 0, nitroCd = 0;
};
template <class C> auto vehicleSnap(const C& c, int) -> decltype(c.vehicleState().nitroCooldown, VehSnap()) {
    const auto& v = c.vehicleState();
    VehSnap s;
    s.valid = true; s.driving = v.driving; s.ride = v.rideHeight; s.drift = v.driftRemain;
    s.dash = v.dashRemain; s.dashCd = v.dashCooldown; s.nitro = v.nitroRemain; s.nitroCd = v.nitroCooldown;
    return s;
}
template <class C> VehSnap vehicleSnap(const C&, long) { return VehSnap(); }
template <class C> auto meshOffsetOf(const C& c, int) -> decltype(c.meshOffset(), core::Vec3()) { return c.meshOffset(); }
template <class C> core::Vec3 meshOffsetOf(const C&, long) { return core::Vec3{0, 0, 0}; }
// Drawn mesh root: gameplay Pass 13+ draws each form at meshOrigin(form) (actor location minus that
// form's mesh-to-actor offset); older builds draw at position() + meshOffset().
template <class C> auto drawRootOf(const C& c, int) -> decltype(c.meshOrigin(c.form()), core::Vec3()) { return c.meshOrigin(c.form()); }
template <class C> core::Vec3 drawRootOf(const C& c, long) { return c.position() + meshOffsetOf(c, 0); }
// Model matrix of form f's drawn mesh (Pass 13+: meshMatrix(f) incl. the vehicle body pitch/roll).
template <class C> auto drawMatrixOf(const C& c, game::Form f, int) -> decltype(c.meshMatrix(f), core::Mat4()) { return c.meshMatrix(f); }
template <class C> core::Mat4 drawMatrixOf(const C& c, game::Form, long) {
    return core::Mat4::translate(c.position() + meshOffsetOf(c, 0)) * core::Mat4::rotateY(c.yaw() + core::config::kMeshYawOffset);
}
// Camera anchor base: Pass 13+ orbits actorLocation() (shared by both forms during a transform).
template <class C> auto actorLocationOf(const C& c, int) -> decltype(c.actorLocation(), core::Vec3()) { return c.actorLocation(); }
template <class C> core::Vec3 actorLocationOf(const C& c, long) { return c.position() + meshOffsetOf(c, 0); }
template <class C> auto upperAnimName(const C& c, int) -> decltype(std::string(c.upperAnimName())) { return c.upperAnimName(); }
template <class C> std::string upperAnimName(const C&, long) { return ""; }
FID_OPTIONAL_ACCESSOR_LATE(effectiveSpread)          // gameplay Pass 16: bloom x airborne x fine aim
FID_OPTIONAL_ACCESSOR_LATE(airborneSpreadMultiplier) // gameplay Pass 16: TnWeaponSpreadModifier

// Reload-slot weight under either layering API (Gameplay reloadWeight / Systems UpperBodyCustom).
template <class C> float reloadSlotWeight(const C& c) {
    float w = reloadWeight(c, 0);
    if (w >= 0) return w;
    if (upperAnimName(c, 0).find("Reload") != std::string::npos) return upperWeight(c, 0);
    return -1.0f;
}
} // namespace layer

// Harness-only access to private product members (Access.cpp).
game::CollisionWorld& worldCollision(game::World& w);
const render::MeshData& drawnPose(const game::Character& c);
const render::MeshData* partnerPose(const game::Character& c);   // nullptr when the build has no partner mesh

// Hitscan calls captured by the World::fireHitscan stub (WorldStub.cpp).
struct ShotRecord { double t; core::Vec3 origin, dir; };
std::vector<ShotRecord>& shotLog();
void setShotClock(double t);

// One row of the per-step trace.
struct Frame {
    int step = 0;
    double t = 0;
    core::Vec3 pos, vel;
    float yaw = 0, camYaw = 0, camPitch = 0;
    bool grounded = false, transforming = false, reloading = false, weaponVisible = false;
    game::Form form = game::Form::Robot;
    std::string anim;
    float animT = 0;
    int ammo = 0, reserve = 0, shots = 0;
    float spread = 0;
    // Animation-layer weights when the Character exposes them (agents/gameplay Pass 7+); -1 otherwise.
    float reloadW = -1, aimW = -1, aimPitchN = -1;
    float moveForm = -1;     // 0 robot / 1 vehicle movement form, -1 if the build has no moveForm()
    float fineAim = -1;      // controller fine-aim active (1/0), -1 if unavailable
    float fov = -1;          // controller FOV (deg), -1 if unavailable
    float progress = -1;     // transform progress 0..1 (-1 unavailable)
    float wUsable = -1, wRestored = -1;   // weapon usable / restored (1/0, -1 unavailable)
    core::Vec3 meshOff;      // drawn-mesh offset from the pawn origin (transform height blend)
    layer::VehSnap veh;      // vehicle state machine (valid=false when not exposed)
    core::Vec3 muzzle;   // world-space barrel tip (valid when weaponVisible)
    // Camera (PlayerController::updateCamera) and the drawn skinned pose.
    core::Vec3 camPos, camFocus;   // camera position; anchor it orbits (pawn + mesh offset + Offset Z)
    float viewYaw = 0;             // rendered camera yaw (after camera smoothing; camYaw is the input yaw)
    float camFov = 0;              // horizontal FOV (deg)
    int drawnModel = -1;           // 0 robot mesh, 1 vehicle mesh, -1 none (fallback box)
    float poseDelta = -1;          // max model-space vertex move vs the previous step (-1: model changed / none)
    core::Vec3 bbMin, bbMax;       // world-space bounds of the drawn pose (1st..99th percentile per axis)
    int farVerts = 0;              // drawn vertices more than 10 m from the root
    float farMax = 0;              // farthest drawn vertex from the root (m)
    // Pass 13+/14 state (-1 / invalid when the build lacks it).
    float partner = -1, arm = -1, hand = -1, rammed = -1;
    core::Vec3 drawRoot;           // where the current form's mesh is drawn (see layer::drawRootOf)
    int partnerFarVerts = 0;       // partner-mesh vertices > 10 m from the root while the partner is drawn
    layer::VehAtt att;
    float comH = -1;               // vehicle centre-of-mass height above the floor under it (m; rig floor y = 0 / world collision)
    float comY = 0;                // vehicle centre-of-mass world height (m)
    bool robotVisible = false, vehicleVisible = false;   // meshes actually drawn (current + partner)
};

// Extracted Optimus models, loaded once and shared by every rig (read-only asset access).
struct Models {
    assets::SkinnedModel robot, vehicle, arm;   // arm: CP_OptimusArm_SKEL (gameplay Pass 13b+ arm display)
    bool armOk = false;
    bool ok = false;
    int weaponBone = -1;
    core::Mat4 socket;
    static const Models* get();          // null when assets are unavailable / disabled
    static void disable();
    static std::string assetRoot();
};

class Rig {
public:
    explicit Rig(double hz = 60.0, bool withModels = true);

    game::Character& pawn() { return player_.pawn(); }
    game::PlayerController& controller() { return player_.controller(); }
    game::World& world() { return world_; }
    bool hasModels() const { return models_ != nullptr; }
    float dt() const { return dt_; }
    double time() const { return t_; }

    // Optional collision. When set, movement runs through CharacterMovement directly with
    // the controller-computed intent (World's collision member is private to World).
    void setCollision(const game::CollisionWorld* c) { col_ = c; }
    // Collision through World (World::collision()): the production PlayerController::applyToPawn
    // path (full intent mapping incl. Boost/Dash/FineAim, camera collision, aim trace) runs against it.
    void useWorldCollision(const render::MeshData& mesh) { worldCollision(world_).build(mesh); }

    // One fixed step with the given input (pressed[] edges are consumed this step).
    void step(const platform::InputFrame& in);
    // A render frame that runs ZERO fixed steps (Application::run: handleInput every frame,
    // applyToPawn per fixed step). Happens whenever the frame rate exceeds the 60 Hz sim rate.
    void frameWithoutStep(const platform::InputFrame& in) { controller().handleInput(in, dt_); }
    // A real key tap: one step with `b` pressed+held, then one step released (other keys in `base`
    // stay held). Works for press-triggered and release-triggered (WFC reload) handling alike.
    void tap(platform::Button b, platform::InputFrame base = {}) {
        platform::InputFrame f = base;
        f.down[(int)b] = true;
        f.pressed[(int)b] = true;
        step(f);
        step(base);
    }
    // Hold an input for `seconds` (edges only on the first step).
    void hold(const platform::InputFrame& in, double seconds);
    void idle(double seconds) { hold(platform::InputFrame{}, seconds); }

    const std::vector<Frame>& trace() const { return trace_; }
    const Frame& last() const { return trace_.back(); }
    render::Camera camera() const;
    bool writeCsv(const std::string& path) const;

    static platform::InputFrame press(platform::Button b);
    static platform::InputFrame down(std::initializer_list<platform::Button> bs);

private:
    void record();

    game::Player player_;
    game::World world_;              // never loaded; only used as the applyToPawn sink
    const game::CollisionWorld* col_ = nullptr;
    const Models* models_ = nullptr;
    float dt_;
    double t_ = 0;
    int step_ = 0;
    size_t shotBase_ = 0;
    std::vector<float> prevPose_;
    int prevModel_ = -2;
    std::vector<Frame> trace_;
};

// Synthetic collision geometry builder (axis-aligned boxes + floor) for collision probes.
struct BoxScene {
    render::MeshData mesh;
    void floor(float y, float half);
    void box(const core::Vec3& mn, const core::Vec3& mx);
    // Ramp rising toward -Z: starts at z0 (height 0) and climbs at `deg` over `len` metres.
    void ramp(float x0, float x1, float z0, float len, float deg);
};

} // namespace fid

namespace fid::layer {
// HUD spread notifications (gameplay Pass 16 PlayerController::hudNotifies()): the WeaponSpread values sent
// this step; empty/false on builds without it.
template <class P> auto hudSpreadNotifies(const P& pc, std::vector<float>& out, int) -> decltype(pc.hudNotifies(), bool()) {
    for (const auto& n : pc.hudNotifies())
        if ((int)n.type == 0) out.push_back(n.spread);   // HudNotify::Type::WeaponSpread
    return true;
}
template <class P> bool hudSpreadNotifies(const P&, std::vector<float>&, long) { return false; }
}
