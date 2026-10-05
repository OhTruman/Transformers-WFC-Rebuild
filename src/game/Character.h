// Clean-room reconstruction — Character/Pawn: a controllable embodied actor.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "game/Actor.h"
#include "game/TransformState.h"
#include "game/Health.h"
#include "game/Weapon.h"
#include "game/Ability.h"
#include "game/Recoil.h"
#include "game/ChassisDef.h"
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
    // GameInfo.RestartPlayer spawns a fresh TnPlayerPawnMultiplayer: robot form, no transformation, default vehicle
    // state, full HealthMax (no overshield), default inventory (Ion Blaster: full clip, InitialReserveAmmoCount), at rest.
    void respawnReset() {
        trans_ = Transition::None; transClip_ = -1; partnerVisible_ = false; partnerClip_ = -1;
        veh_ = VehicleState{}; restoreTimer_ = -1.0f; lastDriving_ = false; shiftRemain_ = 0.0f;
        velocity_ = {0, 0, 0}; onGround_ = true; rammedRemain_ = 0.0f;
        health_ = Health{}; overShield_ = false;
        if (!specHealth_.empty()) health_.initialize(specHealth_, specOvershield_);   // ApplySpecialty: Health_<Class>
        weapon_ = Weapon{}; speedMult_ = 1.0f; fineAiming_ = false;
        form_ = Form::Robot == form_ ? form_ : Form::Robot; setForm(Form::Robot); animTime_ = 0.0f; clip_ = -1;
    }
    Form form() const { return form_; }      // displayed form (mesh handoff happens mid-fold)
    // Movement form: TnPawn.Transforming.BeginTransformation sets _CurrentForm = TargetForm and the
    // target form's movement capabilities at the START of a transformation [CONF bytecode], so
    // physics follows the target form for the whole fold while the mesh still hands off mid-fold.
    Form moveForm() const { return trans_ != Transition::None ? transTarget_ : form_; }
    // Normalized progress through the whole fold (the incoming clip resumes at the outgoing
    // clip's normalized time, so animTime/duration of the active transform clip spans 0..1).
    float transformProgress() const {
        const assets::SkinnedModel* m = currentModel();
        if (trans_ == Transition::None || !m || transClip_ < 0) return 1.0f;
        float d = m->clips[(size_t)transClip_].duration;
        return d > 0.0f ? std::min(1.0f, animTime_ / d) : 1.0f;
    }
    float transformStartYaw() const { return transStartYaw_; }
    void toggleForm() { setForm(form_ == Form::Robot ? Form::Vehicle : Form::Robot); }

    // Transformation: plays paired transform clips with a mid-sequence skeleton handoff.
    void beginTransform();
    bool isTransforming() const { return trans_ != Transition::None; }

    // TnPawn speed multipliers (SetSpeedMultiplier / UpdateSpeeds): GroundSpeed/AirSpeed scale.
    void setSpeedMultiplier(float m) { speedMult_ = m; }
    // Factors multiply (TnPawn.UpdateSpeeds over _SpeedMultiplierFactors): the specialty factor (associated with the
    // pawn itself) stays for the pawn's life; the fine-aim factor comes and goes.
    float speedMultiplier() const { return speedMult_ * specialtySpeedMult_; }
    // TnSpecialty.Apply: SetSpeedMultiplier(SpeedMultiplier, P) + InitializeSegmentedHealth(HealthBlueprint) [CONF].
    void setSpecialty(const std::string& id, float speedMult, const std::vector<float>& segments, float overshield) {
        specialty_ = id; specialtySpeedMult_ = speedMult; specHealth_ = segments; specOvershield_ = overshield;
        health_.initialize(segments, overshield);
    }
    void clearSpecialty() { specialty_.clear(); specialtySpeedMult_ = 1.0f; specHealth_.clear(); specOvershield_ = 550.0f; health_ = Health{}; }
    const std::string& specialty() const { return specialty_; }
    // Fine aim state (TnFineAimManager.bFineAiming), owned by the controller.
    void setFineAiming(bool b) { fineAiming_ = b; }
    bool fineAiming() const { return fineAiming_; }
    // Hover offset the pawn currently carries above its supporting surface (vehicle suspension).
    float hoverApplied() const { return hoverApplied_; }
    void setHoverApplied(float h) { hoverApplied_ = h; }
    // Visual-only mesh offset absorbing a position shift during a transformation; decays to zero
    // over kTransformShiftBlend (TnPawn.Transforming.OnUpdate OffsetMeshes).
    void addTransformShift(const core::Vec3& s) { meshShift_ = meshOffset() + s; shiftRemain_ = core::config::kTransformShiftBlend; }
    core::Vec3 meshOffset() const {
        return shiftRemain_ > 0.0f ? meshShift_ * (shiftRemain_ / core::config::kTransformShiftBlend) : core::Vec3{0, 0, 0};
    }

    // Actor location (UE pawn Location: robot cylinder centre / vehicle RB actor) above the mesh origin of
    // form f. The robot mesh hangs CollisionHeight below the cylinder centre; TnVehicleForm.CalculateCylinderBounds
    // translates the vehicle mesh by -(bounds centre). position() is the mesh origin of moveForm().
    float meshToActor(Form f) const {
        if (f == Form::Robot) return chassis().robot.halfHeight;
        return vehicleModel_ ? 0.5f * (vehicleModel_->boundsMin.y + vehicleModel_->boundsMax.y) : 1.0f;
    }
    // Height of the vehicle mesh top above its origin (bind-pose bounds).
    float meshTopAboveOrigin() const { return vehicleModel_ ? vehicleModel_->boundsMax.y : 2.5f; }
    core::Vec3 actorLocation() const { return pos_ + meshOffset() + core::Vec3{0, meshToActor(moveForm()), 0}; }
    // Mesh origin of form f (both meshes hang off the shared actor location during a transformation).
    core::Vec3 meshOrigin(Form f) const { return actorLocation() - core::Vec3{0, meshToActor(f), 0}; }
    // Model matrix of form f's mesh: yaw, plus the vehicle rigid body's pitch/roll.
    core::Mat4 meshMatrix(Form f) const;
    bool partnerShown() const { return partnerVisible_; }   // second mesh drawn (transformation overlap)

    // Real skinned models per form (owned elsewhere). If unset, draws a fallback box.
    void setFormModels(const assets::SkinnedModel* robot, const assets::SkinnedModel* vehicle) {
        if (robot != robotModel_) { robotRig_ = RobotRig{}; lastModel_ = nullptr; clip_ = -1; }
        if (vehicle != vehicleModel_) { vehicleRig_ = VehicleRig{}; lastModel_ = nullptr; clip_ = -1; }
        robotModel_ = robot; vehicleModel_ = vehicle;
    }
    // The chassis this pawn was spawned as (TnPawn._Blueprint after ApplyTransformer). Never null: the default is
    // the pre-Pass-22 Optimus definition used by the harnesses.
    void setChassis(const ChassisDef* d) { chassis_ = d; }
    const ChassisDef& chassis() const { static const ChassisDef def; return chassis_ ? *chassis_ : def; }
    const RobotParams& robotParams() const { return chassis().robot; }
    const VehicleParams& vehicleParams() const { return chassis().vehicle; }
    // Collision cylinder of a form: robot = ROBODEF CollisionRadius / Height; vehicle = TnVehicleForm.CalculateCylinderBounds
    // of the vehicle mesh bounds (horizontal half-extent, vertical half-height) [HIGH].
    float cylinderRadius(Form f) const {
        if (f == Form::Robot) return chassis().robot.radius;
        if (!vehicleModel_) return 3.34f;
        const auto& a = vehicleModel_->boundsMin; const auto& b = vehicleModel_->boundsMax;
        return 0.5f * std::sqrt((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
    }
    float cylinderHalfHeight(Form f) const {
        if (f == Form::Robot) return chassis().robot.halfHeight;
        return vehicleModel_ ? 0.5f * (vehicleModel_->boundsMax.y - vehicleModel_->boundsMin.y) : 1.22f;
    }
    // Optimus_ROBODEF.ArmBlueprint: CP_OptimusArm_SKEL (+ OptimusArm_ROBO_ANIM ARM_Equip/ARM_Unequip),
    // attached at WeaponSocket_Secondary (R_Arm03_Elbow_XB, pitch 180) while no robot weapon is drawn.
    void setArmModel(const assets::SkinnedModel* arm) {
        armModel_ = arm;
        armEquipClip_ = arm ? arm->clipByName("ARM_Equip") : -1;
        armUnequipClip_ = arm ? arm->clipByName("ARM_Unequip") : -1;
    }
    bool armShown() const { return armVisible_; }

    // Ram victim reactions [CONF native M03 P8]. Robot: TnAcrobaticsManager.Rammed -> RammedReaction:
    // Falling, Velocity = dir * RammedSpeed (SharedAcrobatics 5000) + base velocity, for 0.5 s; EndState:
    // Velocity = (0, 0, base Z). dir = Normal(victim Location - rammer Location). Vehicle:
    // TnPawn.InVehicleForm.AddVelocity(v): rigid body += v * 0.5.
    void rammedAsRobot(const core::Vec3& dir);
    void addVelocityInVehicle(const core::Vec3& v) { velocity_ = velocity_ + v * 0.5f; }
    float rammedRemain() const { return rammedRemain_; }
    float rammedRemain_ = 0.0f;
    float rammedBaseY_ = 0.0f;
    bool handShrunk() const { return handShrunk_; }
    // TnWeaponSpreadModifier (robot form) [CONF RE d50e2a9]: AirborneMultiplier 2.0, ramps up over 0.25 s while
    // airborne and back down over 0.5 s after landing (hover ramp-down 0.1 s applies to the acrobatic hover,
    // not in the slice). Linear ramps [HIGH].
    void tickSpreadModifier(float dt) {
        bool air = moveForm() == Form::Robot && !onGround_;
        float target = air ? 2.0f : 1.0f, rate = air ? 1.0f / 0.25f : 1.0f / 0.5f;
        airSpreadMult_ += std::max(-rate * dt, std::min(rate * dt, target - airSpreadMult_));
    }
    float airborneSpreadMultiplier() const { return airSpreadMult_; }
    // HmWeapon.GetSpread for the robot weapon: Data.Spread (0 for the Ion Blaster) + CurrentSpread x
    // CurrentAirborneMultiplier x (fine aim ? FineAimSpreadModifier 0.5 : 1) [CONF RE d50e2a9].
    float effectiveSpread() const {
        return weapon_.spread * airSpreadMult_ * (fineAiming_ ? core::config::kFineAimSpreadMult : 1.0f);
    }
    float airSpreadMult_ = 1.0f;
    // TnOverShieldPickup granted (amount/duration are native and not recovered: state flag only [PARTIAL]).
    void grantOverShield() { overShield_ = true; ++overShieldGrants_; }
    bool overShield() const { return overShield_; }
    int overShieldGrants() const { return overShieldGrants_; }
    bool overShield_ = false;
    int overShieldGrants_ = 0;
    const assets::SkinnedModel* currentModel() const {
        return form_ == Form::Robot ? robotModel_ : vehicleModel_;
    }

    // Advance the animation state machine + CPU-skin into the pose buffer (call once per step).
    void updateAnimation(float dt);

    // World transform of a bone of the currently displayed model (this step's pose).
    bool boneWorld(const std::string& bone, core::Mat4& out) const {
        const assets::SkinnedModel* mdl = currentModel();
        if (!mdl) return false;
        int n = mdl->nodeByName(bone);
        if (n < 0 || (size_t)n >= animScratch_.size() || lastModel_ != mdl) return false;
        // Same model transform as draw()/updateWeaponSocket(), including the transform-shift offset.
        out = meshMatrix(form_) * animScratch_[(size_t)n];
        return true;
    }
    static float meshYawOffset();

    // Weapon socket (robot form): the bone node to follow + a local offset transform.
    // ArmBlueprint.AttachSocket (WeaponSocket_Secondary) of this chassis' robot mesh.
    void setArmSocket(int boneNode, const core::Mat4& offset) { armBone_ = boneNode; armOffset_ = offset; }
    void setWeaponSocket(int boneNode, const core::Mat4& offset) {
        weaponBone_ = boneNode; weaponOffset_ = offset;
    }
    bool hasWeapon() const { return weaponBone_ >= 0 && weaponValid_; }   // robot mesh shown (either slot)
    const core::Mat4& weaponWorld() const { return weaponWorld_; }

    core::Vec3& velocity() { return velocity_; }
    const core::Vec3& velocity() const { return velocity_; }
    bool onGround() const { return onGround_; }
    void setOnGround(bool g) { onGround_ = g; }
    float groundY = 0.0f;
    Form transTarget_ = Form::Robot;
    float restoreTimer_ = -1.0f;      // time since the weapon was restored during a vehicle->robot fold
    bool lastDriving_ = false;
    int vehTransClip_ = -1;
    float vehTransT_ = 0.0f;
    float transStartYaw_ = 0.0f;
    float speedMult_ = 1.0f;
    bool fineAiming_ = false;
    float hoverApplied_ = 0.0f;
    core::Vec3 meshShift_{0, 0, 0};
    float shiftRemain_ = 0.0f;

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

    // Vehicle mechanics state (TnCarForm Hovering/Driving, hover dash, truck nitro). Owned by the
    // movement code; read by animation and diagnostics.
    struct VehicleState {
        bool driving = false;         // TnCarForm state Driving (normal boost, wheels)
        bool tankBoost = false;       // TnHoverTankSimulation boosting (tank form)
        float rideHeight = 0.0f;      // diagnostics: COM height above the surface below it (m)
        float driftRemain = 0.0f;     // Hovering.BeginState Drift(): steering authority ramp
        float dashRemain = 0.0f;      // hover dash time remaining
        float dashCooldown = 0.0f;    // special-move cooldown (TimeBetweenDashes)
        core::Vec3 dashDir{0, 0, 0};  // local (x = forward, z = right)
        float nitroRemain = 0.0f;     // truck nitro (ram) time remaining
        float nitroCooldown = 0.0f;   // TimeBetweenNitros, from nitro start
        // Rigid-body attitude (UE rotator sense: pitch + = nose up, roll + = right side down) and
        // body-local angular velocity (UE axes: x = roll axis, y = pitch axis, z = yaw, + = turn right).
        float pitch = 0.0f, roll = 0.0f;
        core::Vec3 angVel{0, 0, 0};
        float spLen[4] = {-1.0f, -1.0f, -1.0f, -1.0f}; // TnSpring._Length per ray (m); Reset() sets -1
        int contacts = 0;             // suspension rays touching (TnHoverCarSimulation._NumContacts)
        core::Vec3 contactN{0, 1, 0}; // averaged contact normal
        bool onTheGround = false;     // hover: ContactNormal.Z > CosGroundAngle; driving: wheels down
        float jumpWait = 0.0f;        // TnCarForm._TimeBeforeNextJump
        float jumpBoost = 0.0f;       // TnCarSimulation._JumpTimeRemaining (Driving jump, FX colour)
        float steer = 0.0f;           // Driving steering after sign(s)*s^2 and SteeringScale
        float yawRate = 0.0f;         // rad/s, UE sense (+ = turning right)
        float tireForce = 0.0f;       // Driving: summed lateral tire force (N, body +Y) [diagnostics]
        float rollControl = 0.0f;     // Driving: RollControl = left-stick X (StrafeRightLeft); no barrel roll (RollDuration 0)
    };
    VehicleState veh_;
    VehicleState& vehicleState() { return veh_; }
    const VehicleState& vehicleState() const { return veh_; }
    // Weapon usable: robot control form and, during vehicle->robot, restored at 25% of the fold
    // plus the 0.2 s equip [CONF]. Robot->vehicle stores the weapon at fold start.
    bool weaponUsable() const;
    bool weaponRestored() const;

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

    const ChassisDef* chassis_ = nullptr;
    int armBone_ = -1;
    core::Mat4 armOffset_;
    std::string specialty_;
    float specialtySpeedMult_ = 1.0f;
    std::vector<float> specHealth_;
    float specOvershield_ = 550.0f;
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
    // Second mesh during a transformation: the form not in form_, posed from its partner clip at the
    // shared clip time and drawn inside its authored visibility window.
    int partnerClip_ = -1;
    bool partnerVisible_ = false;
    assets::LocalPose partnerPose_;
    std::vector<core::Mat4> partnerScratch_;
    render::MeshData partnerBuf_;
    Form partnerForm() const { return form_ == Form::Robot ? Form::Vehicle : Form::Robot; }
    const assets::SkinnedModel* modelOf(Form f) const { return f == Form::Robot ? robotModel_ : vehicleModel_; }
    bool meshVisible(Form f, float clipT) const;
    void updatePartner(float clipT);

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
        int walk[4] = {-1, -1, -1, -1}, jog[4] = {-1, -1, -1, -1};   // Nav_Strafe{Walk,Jog}_{F,B,R,L}
        std::vector<float> upperMask;            // C_Spine01_Lumbar01_XB subtree
        int reloadClip = -1, idleClip = -1, landClip = -1, land2Clip = -1, land3Clip = -1;   // Nav_Land / _02 / _03
    } robotRig_;
    struct VehicleRig {
        bool built = false;
        assets::LocalPose idle, f, b, l, r;      // Nav_Hover_{Pose,F,B,L,R}_VEH
        bool valid = false;
        int hoverAddClip = -1;                   // ADD_Nav_Hover_VEH
        int hoverToBoost = -1, boostToHover = -1, wheels = -1;   // Driving (normal boost) clips
    } vehicleRig_;
    void buildRobotRig(const assets::SkinnedModel& mdl);
    void buildVehicleRig(const assets::SkinnedModel& mdl);

    float aimPitch_ = 0.0f, aimPitchN_ = 0.0f, aimYawN_ = 0.0f, aimW_ = 0.0f;
    // Turn in place (TnAnimTurnInPlace): legs keep their world yaw while the pawn follows the aim.
    float legYaw_ = 0.0f, lastYaw_ = 0.0f;
    bool yawInit_ = false;
    int turnClip_ = -1;                       // active Nav_IdlePivot90 transition, -1 = none
    float turnT_ = 0.0f, turnStartOffset_ = 0.0f, turnProg_ = 0.0f;
    // Moving state (Robot_ANIMTREE): walk/jog TnStraferAnimBlends mixed by TnVelocityAnimBlend,
    // all strafe sequences phase-locked by the "Strafers" AnimNodeSynch group.
    float locoPhase_ = 0.0f;                  // shared normalized phase of the sync group
    float dirW_[4] = {1.0f, 0.0f, 0.0f, 0.0f};   // F, B, R, L
    float jogW_ = 0.0f;
    void robotLocomotion(const assets::SkinnedModel& mdl, float dt);
    void updateTurnInPlace(const assets::SkinnedModel& mdl, float dt, bool standing);
    float pivotProgress(const assets::SkinnedModel& mdl, int clip, float t) const;
    RecoilControl recoilSpine_{ionBlasterSpineRecoil()}, recoilHand_{ionBlasterRightHandRecoil()};
    float reloadW_ = 0.0f, reloadT_ = 0.0f;
    bool prevReloading_ = false;
    std::vector<float> reloadMask_;
    float airTime_ = 0.0f, landT_ = 0.0f, airApexY_ = 0.0f;
    int landClipSel_ = -1;                    // landing clip picked from SharedAcrobatics.LandingAnims
    float hoverW_ = 0.0f, hoverT_ = 0.0f;

    // TnArmAttachment states: Hidden (detached), Equipping (ARM_Equip, then held), Unequipping (ARM_Unequip).
    enum class ArmState { Hidden, Equipping, Unequipping };
    const assets::SkinnedModel* armModel_ = nullptr;
    int armEquipClip_ = -1, armUnequipClip_ = -1;
    ArmState armState_ = ArmState::Hidden;
    float armT_ = 0.0f;
    bool armVisible_ = false;
    assets::LocalPose armPose_;
    std::vector<core::Mat4> armScratch_;
    render::MeshData armBuf_;
    core::Mat4 armWorld_ = core::Mat4::identity();
    void updateArm(float dt);

    int handBone_ = -1;           // R_Arm04_Hand_XB (Robot_ANIMTREE HandSkelControl)
    bool handShrunk_ = false;
    void applyHandControl(const assets::SkinnedModel& mdl, assets::LocalPose& pose);

    int weaponBone_ = -1;
    core::Mat4 weaponOffset_ = core::Mat4::identity();
    core::Mat4 weaponWorld_ = core::Mat4::identity();
    bool weaponValid_ = false;
    void updateWeaponSocket();
};

} // namespace game
