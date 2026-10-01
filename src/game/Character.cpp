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
    velocity_ = {0, 0, 0};
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

    int cd = mdl.clipByName("Shooting_Aim_F_D"), cc = mdl.clipByName("Shooting_Aim_F_C"),
        cu = mdl.clipByName("Shooting_Aim_F_U");
    if (cd < 0 || cc < 0 || cu < 0) return;
    assets::samplePose(mdl, cd, 0.0f, false, R.aimD);
    assets::samplePose(mdl, cc, 0.0f, false, R.aimC);
    assets::samplePose(mdl, cu, 0.0f, false, R.aimU);
    R.aimValid = true;

    // Calibrate the grid from the authored poses: measure the Ion Blaster barrel pitch in the
    // D/C/U poses (socket bone * WeaponSocket_Primary * barrel +X), so camera pitch maps onto
    // the grid such that the barrel pitch tracks the aim pitch.
    if (weaponBone_ >= 0) {
        auto barrelPitch = [&](const assets::LocalPose& pose) {
            std::vector<core::Mat4> g;
            assets::poseGlobals(mdl, pose, g);
            core::Vec3 d = core::normalize(core::transformDir(g[(size_t)weaponBone_] * weaponOffset_,
                                                             core::Vec3{1, 0, 0}));
            return std::asin(core::clampf(d.y, -1.0f, 1.0f));
        };
        float pd = barrelPitch(R.aimD), pc = barrelPitch(R.aimC), pu = barrelPitch(R.aimU);
        {
            std::vector<core::Mat4> g;
            assets::poseGlobals(mdl, R.aimC, g);
            core::Vec3 d = core::normalize(core::transformDir(g[(size_t)weaponBone_] * weaponOffset_,
                                                             core::Vec3{1, 0, 0}));
            LOG_INFO("aim rig: Shooting_Aim_F_C barrel dir (model space) %.2f %.2f %.2f", d.x, d.y, d.z);
        }
        bool ok = pu > pc + 0.05f && pc > pd + 0.05f;
        if (ok) { R.pitchD = pd; R.pitchC = pc; R.pitchU = pu; }
        LOG_INFO("aim rig: barrel pitch D=%.1f C=%.1f U=%.1f deg (%s)", pd * 57.2958f, pc * 57.2958f,
                 pu * 57.2958f, ok ? "calibrated" : "fallback +-0.8 rad");
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

    // Reload slot: the authored Ion Blaster reload clip over the UPPER body (C_Spine01_Lumbar01
    // subtree) so the robot can reload on the move; standing still, the slot covers the whole
    // body (the full-body clip as authored). Blended in mesh space like UE3's per-bone blend, so
    // the reloading torso stays facing forward over the strafe clips' turned hips.
    // [PROV] mask root + slot blend time.
    bool reloading = weapon_.reloading();
    if (reloading && !prevReloading_) reloadT_ = 0.0f;
    prevReloading_ = reloading;
    reloadT_ += dt;
    bool wantReload = robotRig && steady && reloading && robotRig_.reloadClip >= 0;
    reloadW_ = approach(reloadW_, wantReload ? 1.0f : 0.0f, dt, core::config::kSlotBlend);
    if (robotRig && reloadW_ > 0.0f && robotRig_.reloadClip >= 0) {
        float standW = onGround_ ? 1.0f - core::clampf((speed - 0.4f) / 1.0f, 0.0f, 1.0f) : 0.0f;
        reloadMask_.resize(robotRig_.upperMask.size());
        for (size_t i = 0; i < reloadMask_.size(); ++i)
            reloadMask_[i] = robotRig_.upperMask[i] + (1.0f - robotRig_.upperMask[i]) * standW;
        assets::samplePose(mdl, robotRig_.reloadClip, reloadT_, false, layerPose_);
        assets::blendPoseMeshSpace(mdl, finalPose_, layerPose_, reloadW_, reloadMask_, finalPose_);
    }

    // Upper-body aim offset: pitch the spine/arms by the authored Shooting_Aim_F_{D,C,U} grid
    // (delta from the centre pose), driven by the controller pitch. Weapon-held robot only.
    bool wantAim = robotRig && steady && robotRig_.aimValid && weaponBone_ >= 0;
    aimW_ = approach(aimW_, wantAim ? 1.0f : 0.0f, dt, core::config::kSlotBlend);
    if (robotRig && robotRig_.aimValid && aimW_ > 0.0f) {
        const RobotRig& R = robotRig_;
        float p = aimPitch_;
        aimPitchN_ = (p >= R.pitchC) ? (p - R.pitchC) / (R.pitchU - R.pitchC)
                                     : (p - R.pitchC) / (R.pitchC - R.pitchD);
        aimPitchN_ = core::clampf(aimPitchN_, -1.0f, 1.0f);
        if (aimPitchN_ >= 0.0f) assets::blendPose(R.aimC, R.aimU, aimPitchN_, layerPose_);
        else assets::blendPose(R.aimC, R.aimD, -aimPitchN_, layerPose_);
        assets::deltaPose(R.aimC, layerPose_, deltaPose_);
        assets::addPose(finalPose_, deltaPose_, aimW_, &R.upperMask);
    }

    // Vehicle hover bob: the additive ADD_Nav_Hover_VEH loop on top of the hover poses.
    bool vehRig = (&mdl == vehicleModel_) && vehicleRig_.built && vehicleRig_.hoverAddClip >= 0;
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
            int ic = nm ? nm->firstClipOfCategory(inCat) : -1;
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
        if (vehicleRig_.valid) {
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

    // Robot. GroundSpeed (5.5 m/s) is the jog; the Nav_StrafeJog set is the standard locomotion.
    // A slow walk (Nav_StrafeWalk) is reserved for low analog input (<~3 m/s).
    if (onGround_) {
        if (airTime_ > core::config::kLandMinAirTime && robotRig_.landClip >= 0)
            landT_ = mdl->clips[(size_t)robotRig_.landClip].duration;
        airTime_ = 0.0f;
    } else {
        airTime_ += dt;
        landT_ = 0.0f;
    }
    if (landT_ > 0.0f) landT_ -= dt;

    std::string cat;
    if (!onGround_) cat = (velocity_.y > 0.5f) ? "jump" : "fall";
    else if (speed > 3.0f) cat = "run";
    else if (speed > 0.4f) cat = "walk";
    else cat = (landT_ > 0.0f) ? "land" : "idle";

    // Directional locomotion: the body faces the aim, so choose the F/B/L/R strafe clip by the
    // travel direction relative to facing (dot of velocity with the facing's forward/right axes).
    const char* suf = "_F";
    {
        core::Vec3 fwd = core::forwardFromYawPitch(yaw_, 0.0f);
        core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
        float along = velocity_.x * fwd.x + velocity_.z * fwd.z;
        float side = velocity_.x * right.x + velocity_.z * right.z;
        if (std::fabs(along) >= std::fabs(side)) suf = (along >= 0.0f) ? "_F" : "_B";
        else suf = (side >= 0.0f) ? "_R" : "_L";
    }
    int clip;
    bool loop = true;
    if (cat == "walk" || cat == "run") clip = mdl->clipOfCategoryNamed(cat, suf);
    else if (cat == "idle") clip = robotRig_.idleClip;
    else if (cat == "land") { clip = robotRig_.landClip; loop = false; }
    else { clip = mdl->firstClipOfCategory(cat); loop = (cat != "jump"); }  // take-off plays once
    if (clip < 0 && cat == "run") clip = mdl->clipOfCategoryNamed("walk", suf);
    if (clip < 0 && cat == "walk") clip = mdl->clipOfCategoryNamed("run", suf);
    if (clip < 0) clip = mdl->firstClipOfCategory("idle");
    if (clip < 0) clip = mdl->clips.empty() ? -1 : 0;

    if (clip != clip_) justExitedTransform_ = false;
    playClip(*mdl, clip, loop, dt, blend);
    finalizePose(*mdl, dt);
}

void Character::updateWeaponSocket() {
    weaponValid_ = false;
    // The Ion Blaster is holstered through the whole transform: no floating gun during the fold.
    if (trans_ != Transition::None) return;
    if (form_ != Form::Robot || weaponBone_ < 0) return;
    if ((size_t)weaponBone_ >= animScratch_.size()) return;
    core::Mat4 model = core::Mat4::translate(pos_) * core::Mat4::rotateY(yaw_ + core::config::kMeshYawOffset);
    weaponWorld_ = model * animScratch_[(size_t)weaponBone_] * weaponOffset_;
    weaponValid_ = true;
}

void Character::draw(render::IRenderer& r) const {
    const assets::SkinnedModel* mdl = currentModel();
    if (mdl && mdl->valid() && !poseBuf_.empty()) {
        core::Mat4 model = core::Mat4::translate(pos_) * core::Mat4::rotateY(yaw_ + core::config::kMeshYawOffset);
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
