#include "game/Character.h"
#include "assets/SkinnedModel.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {
constexpr int kVehicleHoverKey = -1000;    // base-layer key for the vehicle directional blend
constexpr int kRobotMoveKey = -1001;       // base-layer key for the robot Moving-state blend
float approach(float cur, float target, float dt, float blendTime) {
    float step = blendTime > 0.0f ? dt / blendTime : 1.0f;
    return cur < target ? std::min(target, cur + step) : std::max(target, cur - step);
}
} // namespace

// Kick off ROBOT<->VEHICLE transformation using the paired transform clips.
void Character::beginTransform() {
    if (trans_ != Transition::None) return;
    const assets::SkinnedModel* mdl = currentModel();
    std::string cat = (form_ == Form::Robot) ? "transform_to_vehicle" : "vehicle_transform_to_robot";
    int c = mdl ? mdl->firstClipOfCategory(cat) : -1;
    if (c < 0) { toggleForm(); return; }   // no transform clip: just swap instantly
    trans_ = Transition::Outgoing;
    transClip_ = c;
    animTime_ = 0.0f;
    clip_ = -1;                 // force a clip change so the blend-in snapshot fires
    legYaw_ = 0.0f; turnClip_ = -1; yawInit_ = false;   // the fold starts square to the aim
    // Momentum is kept: nothing in TnPawn.Transform / BeginTransformation / TnTransformation.Execute
    // touches velocity. The target form becomes the movement form now (see moveForm()).
    transTarget_ = (form_ == Form::Robot) ? Form::Vehicle : Form::Robot;
    transStartYaw_ = yaw_;
    if (transTarget_ == Form::Vehicle) {
        // Vehicle form activates hovering: Hovering.BeginState -> Drift() (authority ramp 0.5 s).
        veh_ = VehicleState{};
        veh_.rideHeight = core::config::kVehicleHoverH;
        veh_.driftRemain = core::config::kHoverDriftDuration;
    } else {
        // Vehicle->robot: the local player keeps full velocity and enters falling [CONF RE].
        onGround_ = false;
        restoreTimer_ = -1.0f;
        veh_.driving = false; veh_.nitroRemain = 0.0f; veh_.dashRemain = 0.0f;
    }
    if (transTarget_ == Form::Vehicle) {
        // TnVehicleForm.OnActivate: Velocity = ClampLength(pawn Velocity, kMaxTransformSpeed);
        // the rigid body takes that velocity and the pawn's rotation (heading preserved).
        float sp = core::length(velocity_);
        if (sp > core::config::kMaxTransformSpeed)
            velocity_ = velocity_ * (core::config::kMaxTransformSpeed / sp);
    }
}

// Robot rig: the 9-pose Shooting_Aim grid (we use the F column: the body always faces the aim
// yaw, so only pitch varies), the upper-body mask, and the clips the layers use.
void Character::buildRobotRig(const assets::SkinnedModel& mdl) {
    RobotRig& R = robotRig_;
    R.built = true;
    R.upperMask = assets::subtreeMask(mdl, mdl.nodeByName("C_Spine01_Lumbar01_XB"));
    R.reloadClip = mdl.clipByName("Shooting_Reload_IonBlaster_ROBO");
    if (R.reloadClip < 0) R.reloadClip = mdl.clipOfCategoryNamed("reload", "IonBlaster");
    // NAV_Idle is Optimus's own gameplay idle (Optimus_ROBO_ANIM); Cust_Idle is the
    // customization-screen idle and Nav_Idle_Base the shared-animset fallback.
    R.idleClip = mdl.clipByName("NAV_Idle");
    if (R.idleClip < 0) R.idleClip = mdl.clipByName("Nav_Idle_Base");
    if (R.idleClip < 0) R.idleClip = mdl.firstClipOfCategory("idle");
    R.landClip = mdl.clipByName("Nav_Land");

    R.rootRef = mdl.nodeByName("C_Root_Reference_XR");
    R.spine = mdl.nodeByName("C_Spine02_Lumbar02_XB");      // SpineRecoil bone (Robot_ANIMTREE)
    R.rightArm = mdl.nodeByName("R_Arm02_Shoulder_XB");     // RightHandRecoil bone (Robot_ANIMTREE)
    // Turn-in-place transitions [CONF Robot_ANIMTREE TnWeaponAnimChooser, WS_ONE_HANDED]:
    // Rt_90/Rt_180 -> Nav_IdlePivot90_R, Lt_90/Lt_180 -> Nav_IdlePivot90_L.
    R.pivotL = mdl.clipByName("Nav_IdlePivot90_L");
    R.pivotR = mdl.clipByName("Nav_IdlePivot90_R");
    static const char* kDirs[4] = {"F", "B", "R", "L"};
    for (int i = 0; i < 4; ++i) {
        R.walk[i] = mdl.clipByName(std::string("Nav_StrafeWalk_") + kDirs[i]);
        R.jog[i] = mdl.clipByName(std::string("Nav_StrafeJog_") + kDirs[i]);
    }

    // TnAnimNodeAimOffset "Default" profile [CONF Robot_ANIMTREE]: the bones it drives, baked from
    // the cells' poses (AnimName_* = Shooting_Aim_{L,F,R}_{D,C,U}). Bake rule (verified against the
    // shipped AimComponents to <0.07 deg / 0 mm, work/pass8/verify_aim.js): for each bone and cell,
    //   offsetRot = Gp * (L_cell * inv(L_centre)) * inv(Gp),   offsetPos = Gp * (t_cell - t_centre)
    // with Gp the parent's model-space rotation in that cell's pose.
    static const char* kAimBones[] = {
        "C_Spine01_Lumbar01_XB", "C_Spine02_Lumbar02_XB", "C_Spine03_Neck01_XB", "C_Spine04_Head_XB",
        "L_Arm01_Clav_XB", "L_Arm02_Shoulder_XB", "L_Arm03_Elbow_XB", "L_Arm04_Hand_XB",
        "R_Arm01_Clav_XB", "R_Arm02_Shoulder_XB", "R_Arm03_Elbow_XB"};
    static const char* kCols[3] = {"L", "F", "R"};
    static const char* kRows[3] = {"D", "C", "U"};
    assets::LocalPose cell[3][3];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) {
            int ci = mdl.clipByName(std::string("Shooting_Aim_") + kCols[c] + "_" + kRows[r]);
            if (ci < 0) return;
            assets::samplePose(mdl, ci, 0.0f, false, cell[c][r]);
        }
    const assets::LocalPose& centre = cell[1][1];
    for (const char* bn : kAimBones) {
        RobotRig::AimComp comp;
        comp.node = mdl.nodeByName(bn);
        if (comp.node < 0) continue;
        size_t b = (size_t)comp.node;
        int par = mdl.nodes[b].parent;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r) {
                const assets::LocalPose& P = cell[c][r];
                core::Quat gp = par >= 0 ? assets::meshRotation(mdl, P, par) : core::Quat{};
                core::Quat gpInv{-gp.x, -gp.y, -gp.z, gp.w};
                core::Quat cInv{-centre.r[b].x, -centre.r[b].y, -centre.r[b].z, centre.r[b].w};
                comp.q[c][r] = assets::quatMul(assets::quatMul(gp, assets::quatMul(P.r[b], cInv)), gpInv);
                comp.t[c][r] = assets::quatRotate(gp, P.t[b] - centre.t[b]);
            }
        R.aimComps.push_back(comp);
    }
    std::sort(R.aimComps.begin(), R.aimComps.end(),
              [](const RobotRig::AimComp& a, const RobotRig::AimComp& b) { return a.node < b.node; });
    R.aimValid = !R.aimComps.empty();
    LOG_INFO("aim rig: Default aim profile baked for %zu bones", R.aimComps.size());

    // Facing evidence (Pass 7): the straight-ahead pose points the barrel along model +X.
    if (weaponBone_ >= 0) {
        std::vector<core::Mat4> g;
        assets::poseGlobals(mdl, centre, g);
        core::Vec3 dc = core::normalize(core::transformDir(g[(size_t)weaponBone_] * weaponOffset_,
                                                           core::Vec3{1, 0, 0}));
        LOG_INFO("aim rig: Shooting_Aim_F_C barrel dir (model space) %.2f %.2f %.2f", dc.x, dc.y, dc.z);
    }
}

// Moving state [CONF structure Robot_ANIMTREE]: TnVelocityAnimBlend (MinSpeed 450 / MaxSpeed 1200
// UU/s) mixes a walk and a jog TnStraferAnimBlend; each strafer weights its F/B/R/L sequences by
// the travel direction relative to the facing (eased over _BlendSpeed 0.2 s [CONF value; easing
// PROV]). Every strafe sequence is in the "Strafers" AnimNodeSynch group: one shared normalized
// phase, advanced at the rate of the highest-weight clip (UE3 synch master).
void Character::robotLocomotion(const assets::SkinnedModel& mdl, float dt) {
    const RobotRig& R = robotRig_;
    core::Vec3 fwd = core::forwardFromYawPitch(yaw_, 0.0f);
    core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
    float speed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    if (speed > 0.05f) {
        float f = (velocity_.x * fwd.x + velocity_.z * fwd.z) / speed;
        float r = (velocity_.x * right.x + velocity_.z * right.z) / speed;
        float tgt[4] = {std::max(0.0f, f), std::max(0.0f, -f), std::max(0.0f, r), std::max(0.0f, -r)};
        float sum = tgt[0] + tgt[1] + tgt[2] + tgt[3];
        float k = std::min(1.0f, dt / core::config::kStraferBlendTime);
        for (int i = 0; i < 4; ++i) dirW_[i] += (tgt[i] / sum - dirW_[i]) * k;
    }
    jogW_ = core::clampf((speed - core::config::kVelBlendMinSpeed) /
                         (core::config::kVelBlendMaxSpeed - core::config::kVelBlendMinSpeed), 0.0f, 1.0f);

    int clips[8];
    float w[8];
    for (int i = 0; i < 4; ++i) {
        clips[i] = R.walk[i]; w[i] = dirW_[i] * (1.0f - jogW_);
        clips[4 + i] = R.jog[i]; w[4 + i] = dirW_[i] * jogW_;
    }
    int master = 0;
    for (int i = 1; i < 8; ++i) if (w[i] > w[master] && clips[i] >= 0) master = i;
    float mDur = clips[master] >= 0 ? mdl.clips[(size_t)clips[master]].duration : 1.0f;
    locoPhase_ = std::fmod(locoPhase_ + dt / std::max(mDur, 1e-3f), 1.0f);

    float total = 0.0f;
    for (int i = 0; i < 8; ++i) {
        if (clips[i] < 0 || w[i] <= 1e-3f) continue;
        float t = locoPhase_ * mdl.clips[(size_t)clips[i]].duration;
        if (total <= 0.0f) {
            assets::samplePose(mdl, clips[i], t, true, basePose_);
            total = w[i];
        } else {
            assets::samplePose(mdl, clips[i], t, true, layerPose_);
            total += w[i];
            assets::blendPose(basePose_, layerPose_, w[i] / total, basePose_);
        }
    }
    animName_ = mdl.clips[(size_t)(clips[master] >= 0 ? clips[master] : 0)].name;
}

// Normalized progress (0..1) of a pivot transition at time t, from the clip's own root-bone yaw
// curve (the turn the clip authors on C_Root_Reference_XR before RRO_Discard removes it).
float Character::pivotProgress(const assets::SkinnedModel& mdl, int clip, float t) const {
    if (clip < 0 || robotRig_.rootRef < 0) return 1.0f;
    float dur = mdl.clips[(size_t)clip].duration;
    auto rootYaw = [&](float time) {
        assets::LocalPose p;
        assets::samplePose(mdl, clip, time, false, p);
        core::Quat q = p.r[(size_t)robotRig_.rootRef];
        float vx = 1 - 2 * (q.y * q.y + q.z * q.z), vz = 2 * (q.x * q.z - q.w * q.y);
        return std::atan2(-vz, vx);
    };
    float end = rootYaw(dur);
    if (std::fabs(end) < 1e-3f) return core::clampf(t / (dur > 0 ? dur : 1.0f), 0.0f, 1.0f);
    return core::clampf(rootYaw(std::min(t, dur)) / end, 0.0f, 1.0f);
}

// TnAnimTurnInPlace [values CONF Default__TnAnimTurnInPlace; trigger/unwind semantics PROV]: the
// pawn follows the aim yaw every frame, but while standing the lower body keeps its world yaw
// (legYaw_ accumulates the opposite of the pawn's rotation; "UnwindLowerBody") and the aim
// offset's L/R columns twist the torso back onto the aim. When the offset comes within
// TransitionThresholdAngle (4096 UU = 22.5 deg) of a RotTransition's RotationOffset (90/180 deg),
// that transition plays (pivot clip, root rotation discarded) and unwinds its RotationOffset
// along the clip's own root-yaw curve, so the stepping feet match the authored turn. A new
// transition may interrupt once PercentageToAllowAbort (0.5) of the current one has played.
void Character::updateTurnInPlace(const assets::SkinnedModel& mdl, float dt, bool standing) {
    const float kTwoPi = 6.2831853f;
    if (!yawInit_) { lastYaw_ = yaw_; yawInit_ = true; }
    float dYaw = std::remainder(yaw_ - lastYaw_, kTwoPi);
    lastYaw_ = yaw_;
    if (!standing) {
        // ResetYawOffsetWhenZeroWeight: locomotion owns the legs; ease the offset out.
        turnClip_ = -1;
        legYaw_ *= std::max(0.0f, 1.0f - dt / core::config::kTurnTransitionBlend);
        return;
    }
    legYaw_ = core::clampf(std::remainder(legYaw_ - dYaw, kTwoPi), -3.1416f, 3.1416f);
    if (turnClip_ >= 0) {
        turnT_ += dt;
        float p = pivotProgress(mdl, turnClip_, turnT_);
        legYaw_ -= turnStartOffset_ * (p - turnProg_);
        turnProg_ = p;
        if (turnT_ >= mdl.clips[(size_t)turnClip_].duration) turnClip_ = -1;
    }
    bool canStart = turnClip_ < 0 || turnProg_ >= core::config::kTurnAbortPct;
    float mag = std::fabs(legYaw_);
    const float k90 = 1.5707963f, k180 = 3.1415927f;   // RotTransitions 16384 / 32768 UU [CONF]
    if (canStart && mag >= k90 - core::config::kTurnThreshold) {
        // legYaw_ < 0: legs are right of the aim -> turn left (Lt_*), and vice versa.
        int c = legYaw_ < 0.0f ? robotRig_.pivotL : robotRig_.pivotR;
        if (c < 0) return;
        float rot = (mag >= k180 - core::config::kTurnThreshold) ? k180 : k90;   // *_180 vs *_90
        turnClip_ = c;
        turnT_ = 0.0f; turnProg_ = 0.0f;
        turnStartOffset_ = legYaw_ < 0.0f ? -rot : rot;   // signed RotationOffset to unwind
        clip_ = -2;   // force the base layer to (re)start the transition with its blend
    }
}

// Vehicle rig: the single-frame directional hover poses + the additive hover bob.
void Character::buildVehicleRig(const assets::SkinnedModel& mdl) {
    VehicleRig& V = vehicleRig_;
    V.built = true;
    int ci = mdl.clipByName("Nav_Hover_Pose_VEH"), cf = mdl.clipByName("Nav_Hover_F_VEH"),
        cb = mdl.clipByName("Nav_Hover_B_VEH"), cl = mdl.clipByName("Nav_Hover_L_VEH"),
        cr = mdl.clipByName("Nav_Hover_R_VEH");
    V.hoverAddClip = mdl.clipByName("ADD_Nav_Hover_VEH");
    V.hoverToBoost = mdl.clipByName("Nav_HoverToBoost_VEH");
    V.boostToHover = mdl.clipByName("Nav_BoostToHover_VEH");
    V.wheels = mdl.clipByName("Nav_Idle_Wheels_VEH");
    if (ci < 0 || cf < 0 || cb < 0 || cl < 0 || cr < 0) return;
    assets::samplePose(mdl, ci, 0.0f, false, V.idle);
    assets::samplePose(mdl, cf, 0.0f, false, V.f);
    assets::samplePose(mdl, cb, 0.0f, false, V.b);
    assets::samplePose(mdl, cl, 0.0f, false, V.l);
    assets::samplePose(mdl, cr, 0.0f, false, V.r);
    V.valid = true;
}

// Switch the base-layer source; on a change, snapshot the last base pose for the crossfade.
void Character::beginBase(const assets::SkinnedModel& mdl, int key, float blendOnChange) {
    bool modelChanged = &mdl != lastModel_;
    if (key == clip_ && !modelChanged) return;
    // A model (skeleton/mesh) change cannot be bone-blended -> hard cut at the handoff.
    if (!modelChanged && basePose_.size() == mdl.nodes.size()) {
        snapPose_ = basePose_;
        blendDur_ = blendOnChange;
    } else {
        blendDur_ = 0.0f;
    }
    blendT_ = 0.0f;
    clip_ = key;
    animTime_ = 0.0f;
    lastModel_ = &mdl;
}

void Character::finishBaseBlend(float dt) {
    if (blendT_ < blendDur_ && snapPose_.size() == basePose_.size()) {
        float a = blendT_ / blendDur_;
        a = a * a * (3.0f - 2.0f * a);                      // smoothstep
        assets::blendPose(snapPose_, basePose_, a, basePose_);
        blendT_ += dt;
    }
}

// Evaluate `clip` as the base layer, crossfading from the snapshot taken at the last change.
void Character::playClip(const assets::SkinnedModel& mdl, int clip, bool loop, float dt, float blendOnChange) {
    if (clip < 0 || clip >= (int)mdl.clips.size()) return;
    beginBase(mdl, clip, blendOnChange);
    animName_ = mdl.clips[(size_t)clip].name;
    assets::samplePose(mdl, clip, animTime_, loop, basePose_);
    finishBaseBlend(dt);
}

// Vehicle base layer: blend the authored single-frame hover poses by the travel direction
// relative to the vehicle's facing (F/B/L/R), toward Nav_Hover_Pose_VEH at rest.
// [PROV] weight mapping: local velocity / MaxLinearSpeed per axis.
void Character::vehicleHoverBlend() {
    const VehicleRig& V = vehicleRig_;
    core::Vec3 fwd = core::forwardFromYawPitch(yaw_, 0.0f);
    core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
    float inv = 1.0f / core::config::kVehicleMoveSpeed;
    float f = core::clampf((velocity_.x * fwd.x + velocity_.z * fwd.z) * inv, -1.0f, 1.0f);
    float r = core::clampf((velocity_.x * right.x + velocity_.z * right.z) * inv, -1.0f, 1.0f);
    float wF = std::max(0.0f, f), wB = std::max(0.0f, -f), wR = std::max(0.0f, r), wL = std::max(0.0f, -r);
    float total = std::max(0.0f, 1.0f - (wF + wB + wR + wL));
    basePose_ = V.idle;
    auto add = [&](const assets::LocalPose& p, float w) {
        if (w <= 0.0f) return;
        total += w;
        assets::blendPose(basePose_, p, w / total, basePose_);
    };
    add(V.f, wF); add(V.b, wB); add(V.l, wL); add(V.r, wR);
}

// Overlays on top of the base layer, then CPU skinning + weapon socket.
void Character::finalizePose(const assets::SkinnedModel& mdl, float dt) {
    finalPose_ = basePose_;
    bool robotRig = (&mdl == robotModel_) && robotRig_.built;
    bool steady = trans_ == Transition::None;
    float speed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    const RobotRig& R = robotRig_;

    // 1) Aim offset (TnAnimNodeAimOffset, "Default" profile): bilinear blend of the 9 cells, each
    // bone rotated (and, for R_Arm01_Clav, moved) in model space about its pivot, parent first,
    // as UE3 applies AimComponents. Input = the pawn's aim as a fraction of 90 deg (pitch; yaw of
    // the aim relative to the legs = TurnInPlaceOffset), remapped from PawnAimOffsetRange onto
    // the profile range and interpolated at InterpSpeed 12 [CONF].
    bool wantAim = robotRig && steady && R.aimValid && weaponBone_ >= 0;
    aimW_ = approach(aimW_, wantAim ? 1.0f : 0.0f, dt, core::config::kSlotBlend);
    if (robotRig && R.aimValid && aimW_ > 0.0f) {
        namespace cfg = core::config;
        // Remap preserving the centre (each side scaled separately) [PROV interpretation].
        auto remap = [](float v, float pMin, float pMax, float oMin, float oMax) {
            v = core::clampf(v, pMin, pMax);
            return v >= 0.0f ? (pMax > 0.0f ? v / pMax * oMax : 0.0f) : (pMin < 0.0f ? v / pMin * oMin : 0.0f);
        };
        const float kQuarter = 1.5707963f;   // 16384 UU: AimOffsetPct unit
        float y = remap(aimPitch_ / kQuarter, cfg::kAimPawnVMin, cfg::kAimPawnVMax, cfg::kAimProfVMin, cfg::kAimProfVMax);
        float x = remap(legYaw_ / kQuarter, cfg::kAimPawnHMin, cfg::kAimPawnHMax, cfg::kAimProfHMin, cfg::kAimProfHMax);
        float k = std::min(1.0f, cfg::kAimInterpSpeed * dt);
        aimPitchN_ += (y - aimPitchN_) * k;
        aimYawN_ += (x - aimYawN_) * k;
        int col = aimYawN_ >= 0.0f ? 2 : 0, row = aimPitchN_ >= 0.0f ? 2 : 0;
        float xa = std::fabs(aimYawN_), ya = std::fabs(aimPitchN_);
        for (const RobotRig::AimComp& c : R.aimComps) {
            core::Quat qMid = assets::quatSlerp(c.q[1][1], c.q[col][1], xa);
            core::Quat qRow = assets::quatSlerp(c.q[1][row], c.q[col][row], xa);
            core::Quat q = assets::quatSlerp(core::Quat{}, assets::quatSlerp(qMid, qRow, ya), aimW_);
            core::Vec3 tMid = c.t[1][1] + (c.t[col][1] - c.t[1][1]) * xa;
            core::Vec3 tRow = c.t[1][row] + (c.t[col][row] - c.t[1][row]) * xa;
            core::Vec3 t = (tMid + (tRow - tMid) * ya) * aimW_;
            assets::applyMeshSpace(mdl, finalPose_, c.node, q, t);
        }
    }

    // 2) Unwind lower body (TnAnimTurnInPlaceRotator): rotate the skeleton root by legYaw_ so the
    // legs keep their world yaw; the aim columns above bring the torso back onto the aim.
    if (robotRig && R.rootRef >= 0 && std::fabs(legYaw_) > 1e-4f)
        assets::applyMeshSpace(mdl, finalPose_, R.rootRef, assets::quatAxisAngle({0, 1, 0}, legYaw_), {0, 0, 0});

    // 3) Reload slot: the authored Ion Blaster reload clip over the UPPER body, blended in mesh
    // space like UE3's per-bone blend (torso stays on the aim over strafe hips / planted legs).
    // Standing still the legs also take the clip, locally, keeping the root's yaw.
    // [PROV] slot blend time.
    bool reloading = weapon_.reloading();
    if (reloading && !prevReloading_) reloadT_ = 0.0f;
    prevReloading_ = reloading;
    reloadT_ += dt;
    bool wantReload = robotRig && steady && reloading && R.reloadClip >= 0;
    reloadW_ = approach(reloadW_, wantReload ? 1.0f : 0.0f, dt, core::config::kSlotBlend);
    if (robotRig && reloadW_ > 0.0f && R.reloadClip >= 0) {
        float standW = onGround_ ? 1.0f - core::clampf((speed - 0.4f) / 1.0f, 0.0f, 1.0f) : 0.0f;
        reloadMask_.resize(R.upperMask.size());
        for (size_t i = 0; i < reloadMask_.size(); ++i) {
            bool isRoot = (int)i == R.rootRef || mdl.nodes[i].parent < 0;
            reloadMask_[i] = isRoot ? 0.0f : (1.0f - R.upperMask[i]) * standW;
        }
        assets::samplePose(mdl, R.reloadClip, reloadT_, false, layerPose_);
        assets::blendPose(finalPose_, layerPose_, reloadW_, finalPose_, &reloadMask_);
        assets::blendPoseMeshSpace(mdl, finalPose_, layerPose_, reloadW_, R.upperMask, finalPose_);
    }

    // 4) Weapon recoil skel-controls (HmSkelControlRecoil via TnRecoiler), restarted per shot.
    // Applied in mesh space in the aim frame (bBoneSpaceRecoil=false): SpineRecoil on
    // C_Spine02_Lumbar02_XB, RightHandRecoil on R_Arm02_Shoulder_XB [CONF Robot_ANIMTREE].
    recoilSpine_.tick(dt);
    recoilHand_.tick(dt);
    if (robotRig && steady && weaponBone_ >= 0) {
        const float kU2R = 6.2831853f / 65536.0f;
        core::Quat aim = assets::quatAxisAngle({0, 0, 1}, aimPitch_);       // pitch about model right
        core::Quat aimInv = assets::quatAxisAngle({0, 0, 1}, -aimPitch_);
        auto apply = [&](const RecoilControl& rc, int node) {
            if (!rc.active || node < 0) return;
            // UE rotator -> model axes (fwd +X, up +Y, right +Z): pitch about +Z, yaw (rightward)
            // about -Y, roll about +X; FRotationMatrix order = Yaw * Pitch * Roll.
            core::Quat q = assets::quatMul(assets::quatMul(
                assets::quatAxisAngle({0, 1, 0}, -rc.rotOffset.y * kU2R),
                assets::quatAxisAngle({0, 0, 1}, rc.rotOffset.x * kU2R)),
                assets::quatAxisAngle({1, 0, 0}, rc.rotOffset.z * kU2R));
            q = assets::quatMul(assets::quatMul(aim, q), aimInv);
            core::Quat v{rc.locOffset.x * 0.01f, rc.locOffset.z * 0.01f, rc.locOffset.y * 0.01f, 0.0f};
            core::Quat w = assets::quatMul(assets::quatMul(aim, v), aimInv);
            assets::applyMeshSpace(mdl, finalPose_, node, q, {w.x, w.y, w.z});
        };
        apply(recoilSpine_, R.spine);      // parent first: the arm inherits the spine kick
        apply(recoilHand_, R.rightArm);
    }

    // Vehicle hover bob: the additive ADD_Nav_Hover_VEH loop on top of the hover poses.
    bool vehRig = (&mdl == vehicleModel_) && vehicleRig_.built && vehicleRig_.hoverAddClip >= 0 && !veh_.driving;
    hoverT_ += dt;
    hoverW_ = approach(hoverW_, (vehRig && steady) ? 1.0f : 0.0f, dt, core::config::kSlotBlend);
    if (vehRig && hoverW_ > 0.0f) {
        assets::samplePose(mdl, vehicleRig_.hoverAddClip, hoverT_, true, layerPose_, /*additive*/ true);
        assets::addPose(finalPose_, layerPose_, hoverW_);
    }

    assets::skinPose(mdl, finalPose_, animScratch_, poseBuf_);
    updateWeaponSocket();
}

// Pick the base-layer source from the movement state, then apply the overlay layers.
void Character::updateAnimation(float dt) {
    const assets::SkinnedModel* mdl = currentModel();
    if (!mdl || !mdl->valid()) return;
    if (robotModel_ && !robotRig_.built && robotModel_->valid()) buildRobotRig(*robotModel_);
    if (vehicleModel_ && !vehicleRig_.built && vehicleModel_->valid()) buildVehicleRig(*vehicleModel_);

    animTime_ += dt;   // advance the active clip's time (reset to 0 by beginBase on a change)
    if (shiftRemain_ > 0.0f) shiftRemain_ = std::max(0.0f, shiftRemain_ - dt);
    // Weapon restore clock during vehicle->robot: starts when 25% of the fold has elapsed (the
    // restore point is measured on the fold even before the robot mesh is displayed).
    if (trans_ != Transition::None && transTarget_ == Form::Robot) {
        if (restoreTimer_ < 0.0f && transformProgress() >= core::config::kRestoreWeaponElapsed) restoreTimer_ = 0.0f;
        else if (restoreTimer_ >= 0.0f) restoreTimer_ += dt;
    }

    // --- transformation timeline (overrides locomotion; plays once, no loop) ---
    if (trans_ != Transition::None) {
        float dur = mdl->clips[(size_t)transClip_].duration;
        if (trans_ == Transition::Outgoing && animTime_ >= dur * core::config::kTransformHandoffFrac) {
            // Mid-fold handoff. The outgoing and incoming clips are the same physical fold authored
            // on each mesh, so we switch meshes and RESUME the partner clip at the same normalized
            // time rather than restarting it: one continuous transformation, not two.
            float frac = (dur > 0.0f) ? (animTime_ / dur) : 1.0f;
            setForm(form_ == Form::Robot ? Form::Vehicle : Form::Robot);
            const assets::SkinnedModel* nm = currentModel();
            std::string inCat = (form_ == Form::Vehicle) ? "vehicle_transform_to_vehicle"
                                                         : "transform_to_robot";
            // Partner clip by name (Transform_ToVehicle_ROBO <-> Transform_ToVehicle_VEH): first-of-
            // category would pick Transform_ToVehicle_SuperBoost_Veh (0.8 s), which does not pair.
            int ic = -1;
            if (nm) {
                std::string partner = mdl->clips[(size_t)transClip_].name;
                size_t us = partner.find_last_of('_');
                if (us != std::string::npos)
                    partner = partner.substr(0, us) + (form_ == Form::Vehicle ? "_VEH" : "_ROBO");
                ic = nm->clipByName(partner);
                if (ic < 0) ic = nm->firstClipOfCategory(inCat);
            }
            if (ic >= 0) {
                trans_ = Transition::Incoming; transClip_ = ic; mdl = nm;
                beginBase(*mdl, ic, core::config::kTransformBlendIn);  // model change: hard cut
                animTime_ = nm->clips[(size_t)ic].duration * frac;      // resume at matching time
                animName_ = nm->clips[(size_t)ic].name;
                assets::samplePose(*mdl, ic, animTime_, false, basePose_);
                finalizePose(*mdl, dt);
                return;
            }
            trans_ = Transition::None; justExitedTransform_ = true;
        } else if (trans_ == Transition::Incoming && animTime_ >= dur) {
            trans_ = Transition::None; justExitedTransform_ = true;      // incoming finished
        }
        if (trans_ != Transition::None) {
            playClip(*mdl, transClip_, /*loop*/ false, dt, core::config::kTransformBlendIn);
            finalizePose(*mdl, dt);
            return;
        }
    }

    float speed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    // Leaving a transform blends out over 0.25 s; ordinary locomotion changes use a short blend.
    float blend = justExitedTransform_ ? core::config::kTransformBlendOut : core::config::kLocomotionBlend;

    if (form_ == Form::Vehicle) {
        legYaw_ = 0.0f; turnClip_ = -1; yawInit_ = false;
        // Normal boost switches TnCarForm Hovering <-> Driving: Nav_HoverToBoost_VEH into the wheels
        // pose Nav_Idle_Wheels_VEH, Nav_BoostToHover_VEH back to the hover poses [clip names CONF,
        // sequencing PROV].
        const VehicleRig& V = vehicleRig_;
        if (veh_.driving != lastDriving_) {
            lastDriving_ = veh_.driving;
            vehTransClip_ = veh_.driving ? V.hoverToBoost : V.boostToHover;
            vehTransT_ = 0.0f;
        }
        vehTransT_ += dt;
        bool inVehTrans = vehTransClip_ >= 0 && vehTransT_ < mdl->clips[(size_t)vehTransClip_].duration;
        if (vehicleRig_.valid && inVehTrans) {
            justExitedTransform_ = false;
            playClip(*mdl, vehTransClip_, false, dt, core::config::kLocomotionBlend);
        } else if (vehicleRig_.valid && veh_.driving && V.wheels >= 0) {
            justExitedTransform_ = false;
            playClip(*mdl, V.wheels, true, dt, core::config::kLocomotionBlend);
        } else if (vehicleRig_.valid) {
            if (clip_ != kVehicleHoverKey) justExitedTransform_ = false;
            beginBase(*mdl, kVehicleHoverKey, blend);
            animName_ = "Nav_Hover_FBLR_VEH";
            vehicleHoverBlend();
            finishBaseBlend(dt);
        } else {
            int clip = mdl->firstClipOfCategory(speed > 0.5f ? "vehicle_move" : "vehicle_idle");
            if (clip < 0) clip = mdl->clips.empty() ? -1 : 0;
            if (clip != clip_) justExitedTransform_ = false;
            playClip(*mdl, clip, true, dt, blend);
        }
        finalizePose(*mdl, dt);
        return;
    }

    // Robot. Grounded and moving -> the tree's Moving state (walk/jog strafer blend); otherwise
    // Idle (with turn in place), jump/fall or land.
    if (onGround_) {
        if (airTime_ > core::config::kLandMinAirTime && robotRig_.landClip >= 0)
            landT_ = mdl->clips[(size_t)robotRig_.landClip].duration;
        airTime_ = 0.0f;
    } else {
        airTime_ += dt;
        landT_ = 0.0f;
    }
    if (landT_ > 0.0f) landT_ -= dt;

    // Turn in place while standing.
    updateTurnInPlace(*mdl, dt, onGround_ && speed <= 0.4f);

    std::string cat;
    if (turnClip_ >= 0) cat = "turn";
    else if (!onGround_) cat = (velocity_.y > 0.5f) ? "jump" : "fall";
    else if (speed > 0.4f) cat = "move";
    else cat = (landT_ > 0.0f) ? "land" : "idle";

    // Idle <-> Moving crossfades (IdleToMovingTransition / MovingToIdleTransition) [CONF 0.2 s].
    bool wasMoving = clip_ == kRobotMoveKey;
    if ((cat == "move") != wasMoving && !justExitedTransform_) blend = core::config::kIdleMoveBlend;

    bool haveLoco = robotRig_.walk[0] >= 0 && robotRig_.jog[0] >= 0;
    if (cat == "move" && haveLoco) {
        if (!wasMoving) justExitedTransform_ = false;
        beginBase(*mdl, kRobotMoveKey, blend);
        robotLocomotion(*mdl, dt);
        finishBaseBlend(dt);
        finalizePose(*mdl, dt);
        return;
    }

    int clip;
    bool loop = true;
    if (cat == "move") clip = mdl->clipOfCategoryNamed("run", "_F");      // no strafe set: fallback
    else if (cat == "idle") clip = robotRig_.idleClip;
    else if (cat == "land") { clip = robotRig_.landClip; loop = false; }
    else if (cat == "turn") { clip = turnClip_; loop = false; blend = core::config::kTurnTransitionBlend; }
    else { clip = mdl->firstClipOfCategory(cat); loop = (cat != "jump"); }  // take-off plays once
    if (clip < 0) clip = mdl->firstClipOfCategory("idle");
    if (clip < 0) clip = mdl->clips.empty() ? -1 : 0;

    // Leaving a pivot transition also uses TransitionBlendTime [CONF 0.1 s].
    if (cat != "turn" && clip_ >= 0 && (clip_ == robotRig_.pivotL || clip_ == robotRig_.pivotR))
        blend = core::config::kTurnTransitionBlend;
    if (clip != clip_) justExitedTransform_ = false;
    playClip(*mdl, clip, loop, dt, blend);
    // RRO_Discard [CONF Default__TnAnimTurnInPlacePlayer]: the pivot's authored root turn is not
    // applied; the unwinding legYaw_ supplies the rotation instead.
    if (cat == "turn" && robotRig_.rootRef >= 0)
        basePose_.r[(size_t)robotRig_.rootRef] = mdl->nodes[(size_t)robotRig_.rootRef].r;
    finalizePose(*mdl, dt);
}

bool Character::weaponRestored() const {
    if (trans_ == Transition::None) return form_ == Form::Robot;
    // Restored once 25% of a vehicle->robot fold has elapsed; needs the robot skeleton displayed.
    return transTarget_ == Form::Robot && form_ == Form::Robot &&
           transformProgress() >= core::config::kRestoreWeaponElapsed;
}

bool Character::weaponUsable() const {
    if (moveForm() != Form::Robot) return false;
    if (trans_ == Transition::None) return true;
    return restoreTimer_ >= core::config::kWeaponEquipTime;   // restored + EquipTime 0.2 s
}

void Character::updateWeaponSocket() {
    weaponValid_ = false;
    // The Ion Blaster is holstered through the whole transform: no floating gun during the fold.
    if (trans_ != Transition::None && !weaponRestored()) return;
    if (form_ != Form::Robot || weaponBone_ < 0) return;
    if ((size_t)weaponBone_ >= animScratch_.size()) return;
    core::Mat4 model = core::Mat4::translate(pos_ + meshOffset()) * core::Mat4::rotateY(yaw_ + core::config::kMeshYawOffset);
    weaponWorld_ = model * animScratch_[(size_t)weaponBone_] * weaponOffset_;
    weaponValid_ = true;
}

void Character::draw(render::IRenderer& r) const {
    const assets::SkinnedModel* mdl = currentModel();
    if (mdl && mdl->valid() && !poseBuf_.empty()) {
        core::Mat4 model = core::Mat4::translate(pos_ + meshOffset()) * core::Mat4::rotateY(yaw_ + core::config::kMeshYawOffset);
        r.drawDynamicMesh(poseBuf_, model, color_);
        return;
    }
    // Fallback graybox.
    core::Vec3 c = pos_ + core::Vec3{0, boxSize_.y * 0.5f, 0};
    r.drawBox(c, boxSize_, color_, yaw_);
    core::Vec3 fwd = core::forwardFromYawPitch(yaw_, 0.0f);
    r.drawBox(c + fwd * (boxSize_.z * 0.5f) + core::Vec3{0, 0.2f, 0},
              core::Vec3{0.25f, 0.25f, 0.25f}, core::Vec3{1, 1, 1}, yaw_);
}

} // namespace game
