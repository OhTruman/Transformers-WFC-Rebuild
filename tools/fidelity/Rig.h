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

namespace fid {

// Optional Character accessors, detected at compile time so one harness builds against every
// branch (ab.ps1). Each returns -1 when the Character under test lacks the accessor.
namespace layer {
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
#undef FID_OPTIONAL_ACCESSOR
template <class C> auto upperAnimName(const C& c, int) -> decltype(std::string(c.upperAnimName())) { return c.upperAnimName(); }
template <class C> std::string upperAnimName(const C&, long) { return ""; }

// Reload-slot weight under either layering API (Gameplay reloadWeight / Systems UpperBodyCustom).
template <class C> float reloadSlotWeight(const C& c) {
    float w = reloadWeight(c, 0);
    if (w >= 0) return w;
    if (upperAnimName(c, 0).find("Reload") != std::string::npos) return upperWeight(c, 0);
    return -1.0f;
}
} // namespace layer

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
    core::Vec3 muzzle;   // world-space barrel tip (valid when weaponVisible)
};

// Extracted Optimus models, loaded once and shared by every rig (read-only asset access).
struct Models {
    assets::SkinnedModel robot, vehicle;
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
    bool hasModels() const { return models_ != nullptr; }
    float dt() const { return dt_; }
    double time() const { return t_; }

    // Optional collision. When set, movement runs through CharacterMovement directly with
    // the controller-computed intent (World's collision member is private to World).
    void setCollision(const game::CollisionWorld* c) { col_ = c; }

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
