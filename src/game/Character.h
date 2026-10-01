// Clean-room reconstruction — Character/Pawn: a controllable embodied actor.
#pragma once
#include <string>
#include <vector>
#include "game/Actor.h"
#include "game/TransformState.h"
#include "game/Health.h"
#include "game/Weapon.h"
#include "game/Ability.h"
#include "game/Recoil.h"
#include "render/Renderer.h"
#include "render/Mesh.h"

#include "assets/SkinnedModel.h"

namespace game {

class Character : public Actor {
public:
    Character() { setForm(Form::Robot); }

    void setForm(Form f) {
        if (f != form_) { form_ = f; animTime_ = 0; clip_ = -1; }
        form_ = f;
        FormTuning t = tuningFor(f);
        boxSize_ = t.boxSize;
        color_ = t.color;
    }
    Form form() const { return form_; }
    void toggleForm() { setForm(form_ == Form::Robot ? Form::Vehicle : Form::Robot); }

    // Transformation: plays paired transform clips with a mid-sequence skeleton handoff.
    void beginTransform();
    bool isTransforming() const { return trans_ != Transition::None; }

    // Real skinned models per form (owned elsewhere). If unset, draws a fallback box.
    void setFormModels(const assets::SkinnedModel* robot, const assets::SkinnedModel* vehicle) {
        robotModel_ = robot; vehicleModel_ = vehicle;
    }
    const assets::SkinnedModel* currentModel() const {
        return form_ == Form::Robot ? robotModel_ : vehicleModel_;
    }

    // Advance the animation state machine + CPU-skin into the pose buffer (call once per step).
    void updateAnimation(float dt);

    // Weapon socket (robot form): the bone node to follow + a local offset transform.
    void setWeaponSocket(int boneNode, const core::Mat4& offset) {
        weaponBone_ = boneNode; weaponOffset_ = offset;
    }
    bool hasWeapon() const { return form_ == Form::Robot && weaponBone_ >= 0 && weaponValid_; }
    const core::Mat4& weaponWorld() const { return weaponWorld_; }

    core::Vec3& velocity() { return velocity_; }
    const core::Vec3& velocity() const { return velocity_; }
    bool onGround() const { return onGround_; }
    void setOnGround(bool g) { onGround_ = g; }
    float groundY = 0.0f;

    const core::Vec3& boxSize() const { return boxSize_; }
    const core::Vec3& color() const { return color_; }

    Health& health() { return health_; }
    const Health& health() const { return health_; }
    Weapon& weapon() { return weapon_; }
    const Weapon& weapon() const { return weapon_; }
    Ability& ability() { return ability_; }

    const char* animName() const { return animName_.c_str(); }
    float animTime() const { return animTime_; }
    // Layer weights for diagnostics: upper-body aim offset, reload slot, normalized aim pitch.
    float aimWeight() const { return aimW_; }
    float reloadWeight() const { return reloadW_; }
    float aimPitchNorm() const { return aimPitchN_; }
    float aimYawNorm() const { return aimYawN_; }
    // Turn-in-place: lower-body yaw offset from the aim (rad, + = legs left of aim) and state.
    float legYaw() const { return legYaw_; }
    bool turningInPlace() const { return turnClip_ >= 0; }
    bool recoiling() const { return recoilSpine_.active || recoilHand_.active; }

    // A shot was fired this step: restart the weapon recoil skel-controls (TnRecoiler.Recoil).
    void notifyFired() { recoilSpine_.start(); recoilHand_.start(); }

    // Controller aim pitch (radians, camera pitch) driving the upper-body aim offset.
    void setAimPitch(float p) { aimPitch_ = p; }

    void draw(render::IRenderer& r) const override;

private:
    Form form_ = Form::Robot;
    core::Vec3 velocity_{0, 0, 0};
    bool onGround_ = false;
    core::Vec3 boxSize_{1, 2, 1};
    core::Vec3 color_{1, 1, 1};
    Health health_;
    Weapon weapon_;
    Ability ability_;

    const assets::SkinnedModel* robotModel_ = nullptr;
    const assets::SkinnedModel* vehicleModel_ = nullptr;
    int clip_ = -1;                  // active base-layer source (clip index or kVehicleHoverKey)
    float animTime_ = 0.0f;
    std::string animName_ = "-";
    render::MeshData poseBuf_;
    std::vector<core::Mat4> animScratch_;   // per-node model-space matrices of the final pose

    enum class Transition { None, Outgoing, Incoming };
    Transition trans_ = Transition::None;
    int transClip_ = -1;
    bool justExitedTransform_ = false;

    // Base layer (bone space) with a snapshot crossfade. [CONF] transform blend-in/out
    // = 0.115 / 0.25 s. Overlays (reload slot, aim offset, hover additive) go on finalPose_.
    assets::LocalPose basePose_, snapPose_, finalPose_, layerPose_, deltaPose_;
    float blendT_ = 1.0f;            // elapsed blend time
    float blendDur_ = 0.0f;          // 0 = hard cut
    const assets::SkinnedModel* lastModel_ = nullptr;
    void beginBase(const assets::SkinnedModel& mdl, int key, float blendOnChange);
    void finishBaseBlend(float dt);
    void playClip(const assets::SkinnedModel& mdl, int clip, bool loop, float dt, float blendOnChange);
    void finalizePose(const assets::SkinnedModel& mdl, float dt);
    void vehicleHoverBlend();

    // Authored pose rigs, built once per model from the GLB clip set.
    struct RobotRig {
        bool built = false;
        // TnAnimNodeAimOffset "Default" profile: per-bone mesh-space offsets for the 9 cells
        // [col L,C,R][row D,C,U], baked at load from Shooting_Aim_* by the UE3 bake rule.
        struct AimComp {
            int node = -1;
            core::Quat q[3][3];
            core::Vec3 t[3][3];
        };
        std::vector<AimComp> aimComps;
        bool aimValid = false;
        int rootRef = -1, spine = -1, rightArm = -1;           // C_Root_Reference / recoil bones
        int pivotL = -1, pivotR = -1;                          // Nav_IdlePivot90_{L,R}
        std::vector<float> upperMask;            // C_Spine01_Lumbar01_XB subtree
        int reloadClip = -1, idleClip = -1, landClip = -1;
    } robotRig_;
    struct VehicleRig {
        bool built = false;
        assets::LocalPose idle, f, b, l, r;      // Nav_Hover_{Pose,F,B,L,R}_VEH
        bool valid = false;
        int hoverAddClip = -1;                   // ADD_Nav_Hover_VEH
    } vehicleRig_;
    void buildRobotRig(const assets::SkinnedModel& mdl);
    void buildVehicleRig(const assets::SkinnedModel& mdl);

    float aimPitch_ = 0.0f, aimPitchN_ = 0.0f, aimYawN_ = 0.0f, aimW_ = 0.0f;
    // Turn in place (TnAnimTurnInPlace): legs keep their world yaw while the pawn follows the aim.
    float legYaw_ = 0.0f, lastYaw_ = 0.0f;
    bool yawInit_ = false;
    int turnClip_ = -1;                       // active Nav_IdlePivot90 transition, -1 = none
    float turnT_ = 0.0f, turnStartOffset_ = 0.0f, turnProg_ = 0.0f;
    void updateTurnInPlace(const assets::SkinnedModel& mdl, float dt, bool standing);
    float pivotProgress(const assets::SkinnedModel& mdl, int clip, float t) const;
    RecoilControl recoilSpine_{ionBlasterSpineRecoil()}, recoilHand_{ionBlasterRightHandRecoil()};
    float reloadW_ = 0.0f, reloadT_ = 0.0f;
    bool prevReloading_ = false;
    std::vector<float> reloadMask_;
    float airTime_ = 0.0f, landT_ = 0.0f;
    float hoverW_ = 0.0f, hoverT_ = 0.0f;

    int weaponBone_ = -1;
    core::Mat4 weaponOffset_ = core::Mat4::identity();
    core::Mat4 weaponWorld_ = core::Mat4::identity();
    bool weaponValid_ = false;
    void updateWeaponSocket();
};

} // namespace game
