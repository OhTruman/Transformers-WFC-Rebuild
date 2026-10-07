#include "game/Character.h"
#include <memory>
#include <map>
#include "assets/SkinnedModel.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
    if (trans_ != Transition::None || chassis().flyerNoTransform) return;   // a non-transforming flyer ignores the request
    const assets::SkinnedModel* mdl = currentModel();
    std::string cat = (form_ == Form::Robot) ? "transform_to_vehicle" : "vehicle_transform_to_robot";
    int c = mdl ? mdl->firstClipOfCategory(cat) : -1;
    if (c < 0) { toggleForm(); return; }   // no transform clip: just swap instantly
    // Partner clip on the other mesh, paired by name (Transform_ToVehicle_ROBO <-> Transform_ToVehicle_VEH):
    // first-of-category would pick Transform_ToVehicle_SuperBoost_Veh (0.8 s), which does not pair.
    partnerClip_ = -1;
    if (const assets::SkinnedModel* pm = modelOf(partnerForm())) {
        std::string partner = mdl->clips[(size_t)c].name;
        size_t us = partner.find_last_of('_');
        if (us != std::string::npos) partner = partner.substr(0, us) + (form_ == Form::Robot ? "_VEH" : "_ROBO");
        partnerClip_ = pm->clipByName(partner);
        if (partnerClip_ < 0) partnerClip_ = pm->firstClipOfCategory(form_ == Form::Robot ? "vehicle_transform_to_vehicle" : "transform_to_robot");
    }
    partnerVisible_ = false;
    // The actor location is shared by both forms (TnVehicleForm.OnActivate: SetRBPosition(pawn Location
    // + mesh Translation)); position() is the mesh origin of the movement form, so re-express it.
    float actorAbove = meshToActor(form_);
    trans_ = Transition::Outgoing;
    transClip_ = c;
    animTime_ = 0.0f;
    clip_ = -1;                 // force a clip change so the blend-in snapshot fires
    legYaw_ = 0.0f; turnClip_ = -1; yawInit_ = false;   // the fold starts square to the aim
    // Momentum is kept: nothing in TnPawn.Transform / BeginTransformation / TnTransformation.Execute
    // touches velocity. The target form becomes the movement form now (see moveForm()).
    transTarget_ = (form_ == Form::Robot) ? Form::Vehicle : Form::Robot;
    transStartYaw_ = yaw_;
    pos_.y += actorAbove - meshToActor(transTarget_);
    if (transTarget_ == Form::Vehicle) {
        // Vehicle form activates hovering: rigid body at the pawn rotation (yaw only), springs at rest
        // length, TnCarForm.OnActivate _TimeBeforeNextJump = 0, Hovering.BeginState -> Drift() (0.5 s ramp).
        veh_ = VehicleState{};
        veh_.driftRemain = core::config::kHoverDriftDuration;
        onGround_ = false;
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
namespace { std::map<const assets::SkinnedModel*, std::shared_ptr<void>>& rigCache() { static std::map<const assets::SkinnedModel*, std::shared_ptr<void>> c; return c; } }
void Character::clearRigCache() { rigCache().clear(); }

void Character::buildRobotRig(const assets::SkinnedModel& mdl) {
    handBone_ = mdl.nodeByName("R_Arm04_Hand_XB");          // HandSkelControl bone (Robot_ANIMTREE)
    auto& cache = rigCache();
    if (auto it = cache.find(&mdl); it != cache.end()) { robotRig_ = *static_cast<const RobotRig*>(it->second.get()); return; }
    buildRobotRigUncached(mdl);
    cache[&mdl] = std::make_shared<RobotRig>(robotRig_);
}

void Character::buildRobotRigUncached(const assets::SkinnedModel& mdl) {
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
    R.land2Clip = mdl.clipByName("Nav_Land_02");
    R.land3Clip = mdl.clipByName("Nav_Land_03");

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
    V.cannon = mdl.nodeByName("C_Cannon_XB");
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

// Vehicle base layer: VEH_Car_ANIMTREE TnAccelerationAnimBlend [CONF native M03 P2] — velocity-driven
// despite the name: m = ClampLength(Velocity, 2000) in local space / 2000 x max(0, up.Z);
// child 0 (Nav_Hover_Pose_VEH) = 1 - |m|, directional children = max(0, +-sign * m^2 / |m|).
void Character::vehicleHoverBlend() {
    const VehicleRig& V = vehicleRig_;
    core::Vec3 fwd = core::forwardFromYawPitch(yaw_, 0.0f);
    core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
    const float maxV = 20.0f;                                   // MaxVelocity 2000 UU/s
    float upZ = std::max(0.0f, std::cos(veh_.pitch) * std::cos(veh_.roll));
    float f = (velocity_.x * fwd.x + velocity_.z * fwd.z), r = (velocity_.x * right.x + velocity_.z * right.z);
    float len = std::sqrt(f * f + r * r);
    float k = (len > maxV ? maxV / len : 1.0f) / maxV * upZ;
    f *= k; r *= k;
    float m = std::sqrt(f * f + r * r);
    float wF = 0.0f, wB = 0.0f, wR = 0.0f, wL = 0.0f;
    if (m > 1e-6f) {
        wF = std::max(0.0f, f * std::fabs(f) / m); wB = std::max(0.0f, -f * std::fabs(f) / m);
        wR = std::max(0.0f, r * std::fabs(r) / m); wL = std::max(0.0f, -r * std::fabs(r) / m);
    }
    float total = std::max(0.0f, 1.0f - m);
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
    bool reloading = weapon().reloading();
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

    // 3b) Melee action layer: the attack clip over locomotion (BlendIn / BlendOut 0.15 s, TnMeleeSet); whirlwind upper body.
    if (actionClip_ >= 0 && robotRig) {
        const float len = mdl.clips[(size_t)actionClip_].duration;
        actionT_ += dt;
        bool on = actionT_ < len;
        actionW_ = approach(actionW_, on ? 1.0f : 0.0f, dt, 0.15f);
        if (actionW_ > 0.0f) {
            assets::samplePose(mdl, actionClip_, std::min(actionT_, len), false, layerPose_);
            if (actionUpper_) assets::blendPoseMeshSpace(mdl, finalPose_, layerPose_, actionW_, R.upperMask, finalPose_);
            else {
                actionMask_.assign(R.upperMask.size(), 1.0f);
                for (size_t i = 0; i < actionMask_.size(); ++i) if ((int)i == R.rootRef || mdl.nodes[i].parent < 0) actionMask_[i] = 0.0f;
                assets::blendPose(finalPose_, layerPose_, actionW_, finalPose_, &actionMask_);
            }
        } else if (!on) actionClip_ = -1;
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

    // Tank cannon (VEH_Tank_ANIMTREE WeaponPrimary: HmSkelControl_TurretConstrained on C_Cannon_XB, actor space, no
    // constraints, LagDegreesPerSecond 360): the player sets DesiredBoneRotation = (view pitch, hull yaw, 0), so the cannon only
    // pitches [CONF script + authored; lag as a max turn rate HIGH]. Applied as a mesh-space pitch over the animated pose
    // (the cannon rests level in the vehicle clips) [PROV].
    if (&mdl == vehicleModel_ && vehicleRig_.cannon >= 0 && vehicleParams().form == VehicleFormType::Tank && steady) {
        const float maxStep = 6.2831853f * dt;   // 360 deg/s
        cannonPitch_ += core::clampf(aimPitch_ - cannonPitch_, -maxStep, maxStep);
        assets::applyMeshSpace(mdl, finalPose_, vehicleRig_.cannon, assets::quatAxisAngle({0, 0, 1}, cannonPitch_), {0, 0, 0});
    }
    if (&mdl == robotModel_) applyHandControl(mdl, finalPose_);
    // Bone matrices every step (sockets, hit volumes, weapon attach); the vertex skin waits for a draw (ensureSkinned): an off-screen
    // or culled pawn costs no skinning, and an on-screen one is skinned at most once per step.
    assets::poseGlobals(mdl, finalPose_, animScratch_);
    bodySkinModel_ = &mdl; bodySkinDirty_ = true;
    updateWeaponSocket();
}

// Pick the base-layer source from the movement state, then apply the overlay layers.
void Character::updateAnimation(float dt) {
    const assets::SkinnedModel* mdl = currentModel();
    if (!mdl || !mdl->valid()) return;
    if (robotModel_ && !robotRig_.built && robotModel_->valid()) buildRobotRig(*robotModel_);
    if (vehicleModel_ && !vehicleRig_.built && vehicleModel_->valid()) buildVehicleRig(*vehicleModel_);

    // Advance the active clip's time (reset to 0 by beginBase on a change). A transformation plays at its
    // Rate (1; 4 when downed), so the authored notify times are reached at t/Rate [CONF native M03 P10].
    animTime_ += dt * (trans_ != Transition::None ? core::config::kTransformRate : 1.0f);
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
        // The source mesh plays its clip until its authored Hide notify; the target mesh is drawn from
        // its Unhide notify on, posed from the paired clip at the SAME clip time (identical lengths).
        // At the source's hide time the target becomes the primary mesh and simply continues.
        if (trans_ == Transition::Outgoing && !meshVisible(form_, animTime_)) {
            float t = animTime_;
            const assets::SkinnedModel* nm = modelOf(partnerForm());
            int ic = partnerClip_;
            if (nm && ic >= 0) {
                struct ArmTick { Character* c; float dt; ~ArmTick() { c->updateArm(dt); } } armTick{this, dt};
                int oldClip = transClip_;
                setForm(partnerForm());
                trans_ = Transition::Incoming; transClip_ = ic; partnerClip_ = oldClip; mdl = nm;
                beginBase(*mdl, ic, 0.0f);                              // model change: no bone blend
                animTime_ = std::min(t, nm->clips[(size_t)ic].duration);
                animName_ = nm->clips[(size_t)ic].name;
                assets::samplePose(*mdl, ic, animTime_, false, basePose_);
                finalizePose(*mdl, dt);
                updatePartner(animTime_);
                return;
            }
            trans_ = Transition::None; justExitedTransform_ = true;
        } else if (trans_ == Transition::Incoming && animTime_ >= dur) {
            trans_ = Transition::None; justExitedTransform_ = true;      // incoming finished
            partnerVisible_ = false;
        }
        if (trans_ != Transition::None) {
            playClip(*mdl, transClip_, /*loop*/ false, dt, core::config::kTransformBlendIn);
            finalizePose(*mdl, dt);
            updatePartner(animTime_);
            updateArm(dt);
            return;
        }
    }
    partnerVisible_ = false;
    struct ArmTick { Character* c; float dt; ~ArmTick() { c->updateArm(dt); } } armTick{this, dt};   // after the pose

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
        // TR_Acrobatics_p.SharedAcrobatics.LandingAnims [CONF data; Systems handoff], tested in array
        // order on the apex->touchdown height and the horizontal speed (UU): {1200,1200} Nav_Land_03,
        // {1000,1200} Nav_Land, {4500,0} Nav_Land_03, {500,0} Nav_Land_02, {250,0} Nav_Land. Below 250 UU
        // no landing anim plays. [MED order/measure semantics; replaces the PROV 0.3 s air-time rule]
        if (airTime_ > 0.0f) {
            float h = (airApexY_ - pos_.y) * 100.0f;
            float sp = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z) * 100.0f;
            struct LandingAnim { float minH, minSpeed; int clip; };
            const LandingAnim table[] = {{1200, 1200, robotRig_.land3Clip}, {1000, 1200, robotRig_.landClip},
                                         {4500, 0, robotRig_.land3Clip}, {500, 0, robotRig_.land2Clip},
                                         {250, 0, robotRig_.landClip}};
            landClipSel_ = -1;
            for (const LandingAnim& la : table)
                if (h >= la.minH && sp >= la.minSpeed) { landClipSel_ = la.clip; break; }
            if (landClipSel_ >= 0) landT_ = mdl->clips[(size_t)landClipSel_].duration;
        }
        airTime_ = 0.0f;
    } else {
        if (airTime_ == 0.0f) airApexY_ = pos_.y;
        airApexY_ = std::max(airApexY_, pos_.y);
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
    else if (cat == "land") { clip = landClipSel_; loop = false; }
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
    // Restored once 25% of a vehicle->robot fold has elapsed (the robot mesh is displayed from 0.098 s,
    // before the restore point, so the weapon is attached to a visible robot).
    return transTarget_ == Form::Robot && meshVisible(Form::Robot, animTime_) &&
           transformProgress() >= core::config::kRestoreWeaponElapsed;
}

// Authored ToggleHidden windows of THIS chassis' transform clips (ChassisDef, from character.json), on the shared clip time.
bool Character::meshVisible(Form f, float t) const {
    namespace cfg = core::config;
    if (trans_ == Transition::None) return f == form_;
    if (transTarget_ == Form::Vehicle)
        return f == Form::Robot ? t < chassis().toVehRobotHide : t >= chassis().toVehVehicleShow;
    return f == Form::Robot ? t >= chassis().toRobotRobotShow : t < chassis().toRobotVehicleHide;   // per chassis [CONF authored notifies]
}

// Pose and skin the second mesh of a transformation at the shared clip time.
void Character::updatePartner(float t) {
    partnerVisible_ = false;
    const assets::SkinnedModel* pm = modelOf(partnerForm());
    if (trans_ == Transition::None || !pm || !pm->valid() || partnerClip_ < 0) return;
    if (!meshVisible(partnerForm(), t)) return;
    assets::samplePose(*pm, partnerClip_, t, false, partnerPose_);
    if (pm == robotModel_) applyHandControl(*pm, partnerPose_);
    assets::poseGlobals(*pm, partnerPose_, partnerScratch_);
    partnerSkinModel_ = pm; partnerSkinDirty_ = true;
    partnerVisible_ = true;
    updateWeaponSocket();     // the robot may be this partner mesh (vehicle->robot before the vehicle hides)
}

// TnArmAttachment (TARGETED_PASS2 §9) [CONF rules]: the arm is needed while no robot weapon is drawn
// (ShouldAlwaysEquip: no active weapon / not attached) — the whole robot->vehicle fold (weapon stored at
// t = 0) and vehicle->robot until the 25% restore. Becoming needed plays ARM_Equip (attached to
// WeaponSocket_Secondary); no longer needed plays ARM_Unequip, then detaches. Detached whenever the
// robot mesh is hidden. [PROV] HandSkelControl (hand bone scale 0.1 with a weapon) not applied: the
// rebuild's body mesh is RB_Optimus_A_SKELMESH.
void Character::updateArm(float dt) {
    armVisible_ = false;
    static const bool noArm = std::getenv("WFC_NOARM") != nullptr;   // A/B diagnostic
    if (noArm || !armModel_ || !armModel_->valid() || weaponBone_ < 0) return;
    const std::vector<core::Mat4>* robotScratch = nullptr;
    if (form_ == Form::Robot && lastModel_ == robotModel_) robotScratch = &animScratch_;
    else if (trans_ != Transition::None && partnerVisible_ && partnerForm() == Form::Robot) robotScratch = &partnerScratch_;
    if (!robotScratch || (size_t)weaponBone_ >= robotScratch->size()) { armState_ = ArmState::Hidden; return; }
    bool needed = !weaponValid_;
    if (needed && armState_ != ArmState::Equipping) { armState_ = ArmState::Equipping; armT_ = 0.0f; }
    else if (!needed && armState_ == ArmState::Equipping) { armState_ = ArmState::Unequipping; armT_ = 0.0f; }
    else armT_ += dt;
    int clip = armState_ == ArmState::Equipping ? armEquipClip_ : armUnequipClip_;
    if (armState_ == ArmState::Hidden) return;
    if (armState_ == ArmState::Unequipping && (clip < 0 || armT_ >= armModel_->clips[(size_t)clip].duration)) {
        armState_ = ArmState::Hidden;
        return;
    }
    if (clip >= 0) assets::samplePose(*armModel_, clip, armT_, false, armPose_);   // holds the last frame
    else assets::samplePose(*armModel_, 0, 0.0f, false, armPose_);
    assets::poseGlobals(*armModel_, armPose_, armScratch_);
    armSkinDirty_ = true;
    // WeaponSocket_Secondary: bone R_Arm03_Elbow_XB, relative rotation pitch 32768 (180 deg about UE Y =
    // glTF Z), no offset [CONF character.json].
    if (armBone_ >= 0 && (size_t)armBone_ < robotScratch->size()) armWorld_ = meshMatrix(Form::Robot) * (*robotScratch)[(size_t)armBone_] * armOffset_;
    else armWorld_ = meshMatrix(Form::Robot) * (*robotScratch)[(size_t)weaponBone_] * core::Mat4::rotateZ(3.1415927f);
    armVisible_ = true;
}

// TnArmAttachment.UpdateHand [CONF native M03 P7]: HandSkelControl (SkelControlSingleBone on R_Arm04_Hand_XB,
// BoneScale 0.1) strength = ShouldEquipHand ? 0 : 1, blend time 0 (binary, every tick).
// ShouldEquipHand = ShouldAlwaysEquip || weapon.IsHandRequired (Ion Blaster: false); ShouldAlwaysEquip = no
// weapon || not attached || (holstered && !equipping). The robot weapon is attached and drawn when not
// transforming, and from the 25% restore of vehicle->robot (it starts equipping then); stored on robot->vehicle.
void Character::applyHandControl(const assets::SkinnedModel& mdl, assets::LocalPose& pose) {
    handShrunk_ = weaponBone_ >= 0 && (trans_ == Transition::None ? form_ == Form::Robot
                                                                 : (transTarget_ == Form::Robot &&
                                                                    transformProgress() >= core::config::kRestoreWeaponElapsed));
    if (!handShrunk_ || handBone_ < 0 || (size_t)handBone_ >= pose.s.size() || &mdl != robotModel_) return;
    pose.s[(size_t)handBone_] = pose.s[(size_t)handBone_] * 0.1f;
}

void Character::rammedAsRobot(const core::Vec3& dir) {
    rammedBaseY_ = velocity_.y;
    velocity_ = dir * 50.0f + velocity_;      // RammedSpeed 5000 UU/s + base velocity
    rammedRemain_ = 0.5f;
    onGround_ = false;                        // Physics = PHYS_Falling
}

core::Mat4 Character::meshMatrix(Form f) const {
    core::Mat4 m = core::Mat4::translate(meshOrigin(f)) * core::Mat4::rotateY(yaw_ + core::config::kMeshYawOffset + drawYawOffset_);
    // Vehicle rigid-body attitude (both meshes hang off the body while the movement form is the vehicle).
    // Mesh space: +X forward, +Y up, +Z right; pitch + = nose up, roll + = right side down.
    if (moveForm() == Form::Vehicle && (veh_.pitch != 0.0f || veh_.roll != 0.0f))
        m = m * core::Mat4::rotateZ(veh_.pitch) * core::Mat4::rotateX(veh_.roll);
    return m;
}

bool Character::weaponUsable() const {
    if (moveForm() != Form::Robot) return false;
    if (switchTo_ >= 0) return false;              // putting down / equipping
    if (trans_ == Transition::None) return true;
    // Restored + EquipTime 0.2 s, and the gun is actually drawn on a displayed robot mesh this step:
    // no shot can originate from an invisible weapon.
    return restoreTimer_ >= core::config::kWeaponEquipTime && weaponValid_;
}

float Character::meshYawOffset() { return core::config::kMeshYawOffset; }

void Character::updateWeaponSocket() {
    weaponValid_ = false;
    // Robot->vehicle stores the weapon at fold start; vehicle->robot restores it at 25% of the fold,
    // attached to the robot mesh (drawn from 0.098 s, as the partner mesh until the vehicle hides).
    if (trans_ != Transition::None && !weaponRestored()) return;
    if (weaponBone_ < 0) return;
    const std::vector<core::Mat4>* scratch = nullptr;
    if (form_ == Form::Robot) scratch = &animScratch_;
    else if (trans_ != Transition::None && partnerVisible_ && partnerForm() == Form::Robot) scratch = &partnerScratch_;
    if (!scratch || (size_t)weaponBone_ >= scratch->size()) return;
    weaponWorld_ = meshMatrix(Form::Robot) * (*scratch)[(size_t)weaponBone_] * weaponOffset_;
    weaponValid_ = true;
}

void Character::beginStep() {
    prevPos_ = pos_; havePrev_ = true;
    // The previous step's vertices exist only if that step's pose was skinned (drawn); otherwise the next frames present the
    // current pose without the vertex blend.
    ++prevVersion_;
    if (skinnedStep_ == stepCounter_) {
        prevPoseP_ = poseBuf_.positions; prevPoseN_ = poseBuf_.normals;
        prevPartnerP_ = partnerBuf_.positions; prevPartnerN_ = partnerBuf_.normals;
    } else { prevPoseP_.clear(); prevPoseN_.clear(); prevPartnerP_.clear(); prevPartnerN_.clear(); }
    ++stepCounter_;
}

void Character::ensureSkinned() const {
    bool any = false;
    if (bodySkinDirty_ && bodySkinModel_) { assets::skinPose(*bodySkinModel_, finalPose_, skinGlobals_, poseBuf_); bodySkinDirty_ = false; ++bodySerial_; any = true; }
    if (partnerSkinDirty_ && partnerSkinModel_) { assets::skinPose(*partnerSkinModel_, partnerPose_, skinGlobals_, partnerBuf_); partnerSkinDirty_ = false; ++partnerSerial_; any = true; }
    if (armSkinDirty_ && armModel_) { assets::skinPose(*armModel_, armPose_, skinGlobals_, armBuf_); armSkinDirty_ = false; ++armSerial_; any = true; }
    if (any) skinnedStep_ = stepCounter_;
}

core::Vec3 Character::renderOffset() const {
    if (!havePrev_ || renderAlpha_ >= 1.0f) return {0, 0, 0};
    const core::Vec3 d = prevPos_ - pos_;
    if (core::dot(d, d) > 25.0f) return {0, 0, 0};   // a spawn / teleport this step: no interpolation across it
    return d * (1.0f - renderAlpha_);
}

// IRenderer::drawDynamicMeshPosed (agents/rendering), compile-time detected: the same MeshData across frames plus a serial that changes
// with its vertices lets the renderer keep the vertex buffer; without it, drawDynamicMesh.
template <class R> static auto drawPosed(R& r, const render::MeshData& m, const core::Mat4& model, const core::Vec3& c, uint64_t serial, int)
    -> decltype(r.drawDynamicMeshPosed(m, model, c, serial), void()) { r.drawDynamicMeshPosed(m, model, c, serial); }
template <class R> static void drawPosed(R& r, const render::MeshData& m, const core::Mat4& model, const core::Vec3& c, uint64_t, long) { r.drawDynamicMesh(m, model, c); }

// IRenderer::drawDynamicMeshBlended (agents/rendering), compile-time detected: the renderer keeps the current and the previous
// step's vertices per serial and blends prev + (cur - prev) * alpha in the vertex shader (the same formula as blendedPose), so an
// interpolated frame needs no CPU blend or upload. The serial changes with either buffer (skin serial, previous-snapshot version).
template <class R> static auto drawBlended(R& r, const render::MeshData& cur, const std::vector<float>& pp, const std::vector<float>& pn, float a,
                                           const core::Mat4& model, const core::Vec3& c, uint64_t serial, int)
    -> decltype(r.drawDynamicMeshBlended(cur, pp, pn, a, model, c, serial), bool()) { r.drawDynamicMeshBlended(cur, pp, pn, a, model, c, serial); return true; }
template <class R> static bool drawBlended(R&, const render::MeshData&, const std::vector<float>&, const std::vector<float>&, float,
                                           const core::Mat4&, const core::Vec3&, uint64_t, long) { return false; }

// render::MeshData::tangents (agents/rendering), when present: carried with the pose.
template <class M> static auto copyTangents(M& dst, const M& src, int) -> decltype(dst.tangents = src.tangents, void()) { dst.tangents = src.tangents; }
template <class M> static void copyTangents(M&, const M&, long) {}

// The skinned pose between the previous and the current step (mesh-local vertices; the same layout only - a model swap
// this step, e.g. the transformation's form swap, draws the current pose).
const render::MeshData& Character::blendedPose(const render::MeshData& cur, const std::vector<float>& prevP, const std::vector<float>& prevN,
                                               render::MeshData& scratch) const {
    if (renderAlpha_ >= 1.0f || prevP.size() != cur.positions.size() || cur.positions.empty()) return cur;
    if (scratch.indices.size() != cur.indices.size() || scratch.positions.size() != cur.positions.size() || scratch.subs.size() != cur.subs.size() ||
        scratch.mats.size() != cur.mats.size() || (!cur.mats.empty() && scratch.mats[0].tex != cur.mats[0].tex)) {
        scratch = cur;   // the topology / materials of this model (once per model change)
    } else {
        scratch.subs = cur.subs; scratch.mats = cur.mats; scratch.boundsMin = cur.boundsMin; scratch.boundsMax = cur.boundsMax;
    }
    const float a = renderAlpha_;
    for (size_t i = 0; i < cur.positions.size(); ++i) scratch.positions[i] = prevP[i] + (cur.positions[i] - prevP[i]) * a;
    if (prevN.size() == cur.normals.size()) {
        scratch.normals.resize(cur.normals.size());
        for (size_t i = 0; i < cur.normals.size(); ++i) scratch.normals[i] = prevN[i] + (cur.normals[i] - prevN[i]) * a;
    } else scratch.normals = cur.normals;
    copyTangents(scratch, cur, 0);
    return scratch;
}

void Character::draw(render::IRenderer& r) const {
    ensureSkinned();
    const assets::SkinnedModel* mdl = currentModel();
    if (mdl && mdl->valid() && !poseBuf_.empty()) {
        // Presentation interpolation: the whole pawn (body, transformation partner, arm) shifted by one rigid offset.
        const core::Mat4 off = core::Mat4::translate(renderOffset());
        // A GPU blend when the renderer offers it and the previous snapshot matches this model's layout; else the CPU blend scratch
        // (changes every interpolated frame) or the skinned buffer.
        auto canBlend = [&](const render::MeshData& cur, const std::vector<float>& pp) {
            return renderAlpha_ < 1.0f && !cur.positions.empty() && pp.size() == cur.positions.size();
        };
        if (!(canBlend(poseBuf_, prevPoseP_) &&
              drawBlended(r, poseBuf_, prevPoseP_, prevPoseN_, renderAlpha_, off * meshMatrix(form_), color_, (bodySerial_ << 24) ^ prevVersion_, 0))) {
            const render::MeshData& b = blendedPose(poseBuf_, prevPoseP_, prevPoseN_, lerpBody_);
            drawPosed(r, b, off * meshMatrix(form_), color_, &b == &poseBuf_ ? bodySerial_ : ++lerpSerial_, 0);
        }
        if (partnerVisible_ && !partnerBuf_.empty() &&
            !(canBlend(partnerBuf_, prevPartnerP_) &&
              drawBlended(r, partnerBuf_, prevPartnerP_, prevPartnerN_, renderAlpha_, off * meshMatrix(partnerForm()), color_, (partnerSerial_ << 24) ^ prevVersion_ ^ 0x5a5a000000000000ULL, 0))) {
            const render::MeshData& p = blendedPose(partnerBuf_, prevPartnerP_, prevPartnerN_, lerpPartner_);
            drawPosed(r, p, off * meshMatrix(partnerForm()), color_, &p == &partnerBuf_ ? partnerSerial_ : ++lerpSerial_, 0);
        }
        if (armVisible_ && !armBuf_.empty()) drawPosed(r, armBuf_, off * armWorld_, color_, armSerial_, 0);
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
