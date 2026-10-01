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
#include "game/AimOffset.h"
#include "assets/SkinnedModel.h"
#include "render/Renderer.h"
#include "render/Mesh.h"

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

    // Weapon owner-animation layer (Robot_ANIMTREE "UpperBodyCustom" slot) + recoil controls.
    void setAimPitch(float p) { aimPitch_ = p; }
    float aimPitchValue() const { return aimPitch_; }
    const char* upperAnimName() const { return upperName_.c_str(); }
    float upperWeight() const { return upperW_; }
    bool recoilActive() const { return recoilSpine_.active() || recoilRHand_.active(); }
    const core::Vec3& aimProfile() const { return aimProfile_; }

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
    int clip_ = -1;
    float animTime_ = 0.0f;
    std::string animName_ = "-";
    render::MeshData poseBuf_;
    std::vector<core::Mat4> animScratch_;

    enum class Transition { None, Outgoing, Incoming };
    Transition trans_ = Transition::None;
    int transClip_ = -1;
    bool justExitedTransform_ = false;

    // Pose crossfade (snapshot blend). [CONF] transform blend-in/out = 0.115 / 0.25 s.
    render::MeshData curPose_;       // freshly evaluated current clip
    render::MeshData blendFrom_;     // snapshot of the pose at the last clip change
    float blendT_ = 1.0f;            // elapsed blend time
    float blendDur_ = 0.0f;          // 0 = hard cut
    const assets::SkinnedModel* lastModel_ = nullptr;
    void playClip(const assets::SkinnedModel& mdl, int clip, bool loop, float dt, float blendOnChange);

    // --- UpperBodyCustom slot (AnimNodeSlot under AnimNodeBlendMultiBone_4930) [CONF] ---
    // Per-bone weight 1 from C_Spine01_Lumbar01_XB down its subtree; legs/pelvis keep locomotion.
    int upperClip_ = -1;
    float upperTime_ = 0.0f;
    float upperW_ = 0.0f;            // current slot weight (linear blend in/out)
    bool upperWant_ = false;
    std::string upperName_ = "-";
    std::vector<float> upperMask_;
    const assets::SkinnedModel* upperMaskModel_ = nullptr;
    unsigned seenShot_ = 0, seenReload_ = 0;
    // --- skel controls (Robot_ANIMTREE SkelControlLists) ---
    RecoilControl recoilSpine_, recoilRHand_;
    int nodeSpineRecoil_ = -1, nodeRHandRecoil_ = -1;
    float aimPitch_ = 0.0f;
    AimOffset aim_;
    core::Vec3 aimProfile_{0, 0, 0};
    assets::LocalPose basePose_, overPose_;
    void updateUpperBody(float dt);
    void evalLayered(const assets::SkinnedModel& mdl, int clip, float t, bool loop, render::MeshData& out);

    int weaponBone_ = -1;
    core::Mat4 weaponOffset_ = core::Mat4::identity();
    core::Mat4 weaponWorld_ = core::Mat4::identity();
    bool weaponValid_ = false;
    void updateWeaponSocket();
};

} // namespace game
