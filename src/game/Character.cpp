#include "game/Character.h"
#include "assets/SkinnedModel.h"
#include "core/Config.h"
#include "core/Math.h"

#include <cmath>

namespace game {

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

// Evaluate `clip` and crossfade from the snapshot taken at the last clip change.
void Character::playClip(const assets::SkinnedModel& mdl, int clip, bool loop, float dt, float blendOnChange) {
    if (clip < 0 || clip >= (int)mdl.clips.size()) return;
    bool changed = (clip != clip_) || (&mdl != lastModel_);
    if (changed) {
        if (!poseBuf_.empty()) blendFrom_ = poseBuf_;       // snapshot the last rendered pose
        // A model (skeleton/mesh) change cannot be vertex-blended -> hard cut at the handoff.
        blendDur_ = (&mdl != lastModel_ && lastModel_ != nullptr) ? 0.0f : blendOnChange;
        blendT_ = 0.0f;
        clip_ = clip;
        animTime_ = 0.0f;
        lastModel_ = &mdl;
    }
    animName_ = mdl.clips[(size_t)clip].name;
    assets::evaluatePose(mdl, clip, animTime_, animScratch_, curPose_, loop);

    bool blending = blendT_ < blendDur_ && !blendFrom_.positions.empty() &&
                    blendFrom_.positions.size() == curPose_.positions.size();
    if (blending) {
        float a = blendT_ / blendDur_;
        a = a * a * (3.0f - 2.0f * a);                      // smoothstep
        poseBuf_.indices = curPose_.indices;
        poseBuf_.uv = curPose_.uv; poseBuf_.subs = curPose_.subs; poseBuf_.mats = curPose_.mats;
        poseBuf_.positions.resize(curPose_.positions.size());
        poseBuf_.normals.resize(curPose_.normals.size());
        size_t n = curPose_.positions.size();
        for (size_t i = 0; i < n; ++i)
            poseBuf_.positions[i] = blendFrom_.positions[i] + (curPose_.positions[i] - blendFrom_.positions[i]) * a;
        for (size_t i = 0; i + 3 <= curPose_.normals.size() && i + 3 <= blendFrom_.normals.size(); i += 3) {
            core::Vec3 nb{blendFrom_.normals[i], blendFrom_.normals[i+1], blendFrom_.normals[i+2]};
            core::Vec3 nc{curPose_.normals[i], curPose_.normals[i+1], curPose_.normals[i+2]};
            core::Vec3 nn = core::normalize(nb + (nc - nb) * a);
            poseBuf_.normals[i] = nn.x; poseBuf_.normals[i+1] = nn.y; poseBuf_.normals[i+2] = nn.z;
        }
        blendT_ += dt;
    } else {
        poseBuf_ = curPose_;
    }
}

// Pick an animation category from the current movement state, then the matching clip.
void Character::updateAnimation(float dt) {
    const assets::SkinnedModel* mdl = currentModel();
    if (!mdl || !mdl->valid()) return;

    animTime_ += dt;   // advance the active clip's time (reset to 0 by playClip on a change)

    // --- transformation timeline (overrides locomotion; plays once, no loop) ---
    if (trans_ != Transition::None) {
        float dur = mdl->clips[(size_t)transClip_].duration;
        if (trans_ == Transition::Outgoing && animTime_ >= dur * core::config::kTransformHandoffFrac) {
            // Mid-fold handoff. The outgoing and incoming clips are the same physical fold authored
            // on each mesh, so we switch meshes and RESUME the partner clip at the same normalized
            // time rather than restarting it — one continuous transformation, not two.
            float frac = (dur > 0.0f) ? (animTime_ / dur) : 1.0f;
            setForm(form_ == Form::Robot ? Form::Vehicle : Form::Robot);
            const assets::SkinnedModel* nm = currentModel();
            std::string inCat = (form_ == Form::Vehicle) ? "vehicle_transform_to_vehicle"
                                                         : "transform_to_robot";
            int ic = nm ? nm->firstClipOfCategory(inCat) : -1;
            if (ic >= 0) {
                trans_ = Transition::Incoming; transClip_ = ic; mdl = nm;
                playClip(*mdl, ic, /*loop*/ false, dt, core::config::kTransformBlendIn); // hard cut
                animTime_ = nm->clips[(size_t)ic].duration * frac;      // resume at matching time
                assets::evaluatePose(*mdl, ic, animTime_, animScratch_, curPose_, false);
                poseBuf_ = curPose_;
                updateWeaponSocket();
                return;
            }
            trans_ = Transition::None; justExitedTransform_ = true;
        } else if (trans_ == Transition::Incoming && animTime_ >= dur) {
            trans_ = Transition::None; justExitedTransform_ = true;      // incoming finished
        }
        if (trans_ != Transition::None) {
            playClip(*mdl, transClip_, /*loop*/ false, dt, core::config::kTransformBlendIn);
            updateWeaponSocket();
            return;
        }
    }

    float speed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);

    std::string cat;
    if (form_ == Form::Robot) {
        // GroundSpeed (5.5 m/s) is the jog; the Nav_StrafeJog set is the standard locomotion.
        // A slow walk (Nav_StrafeWalk) is reserved for low analog input (<~3 m/s).
        if (!onGround_) cat = (velocity_.y > 0.5f) ? "jump" : "fall";
        else if (speed > 3.0f) cat = "run";
        else if (speed > 0.4f) cat = "walk";
        else cat = "idle";
    } else {
        cat = (speed > 0.5f) ? "vehicle_move" : "vehicle_idle";
    }

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
    if (cat == "walk" || cat == "run") clip = mdl->clipOfCategoryNamed(cat, suf);
    else clip = mdl->firstClipOfCategory(cat);
    if (clip < 0 && cat == "run") clip = mdl->clipOfCategoryNamed("walk", suf);
    if (clip < 0 && cat == "walk") clip = mdl->clipOfCategoryNamed("run", suf);
    if (clip < 0) clip = mdl->firstClipOfCategory("idle");
    if (clip < 0) clip = mdl->firstClipOfCategory("vehicle_idle");
    if (clip < 0) clip = mdl->clips.empty() ? -1 : 0;

    // Reload: the Ion Blaster reload is a one-shot full-body clip that overrides locomotion while
    // the weapon's reload timer runs (WeaponReloadAnimTime). [PARTIAL] WFC layers the additive
    // ADD_Shooting_Reload_* upper-body overlay so you can reload on the move; that needs additive
    // blending and is not yet implemented, so here it plays as a grounded full-body action.
    bool loop = true;
    if (form_ == Form::Robot && onGround_ && weapon_.reloading()) {
        int rc = mdl->clipOfCategoryNamed("reload", "IonBlaster");
        if (rc >= 0) { clip = rc; loop = false; }
    }

    // Leaving a transform blends out over 0.25 s; ordinary locomotion changes use a short blend.
    float blend = justExitedTransform_ ? core::config::kTransformBlendOut : core::config::kLocomotionBlend;
    if (clip != clip_) justExitedTransform_ = false;
    playClip(*mdl, clip, loop, dt, blend);
    updateWeaponSocket();
}

void Character::updateWeaponSocket() {
    weaponValid_ = false;
    // The Ion Blaster is holstered through the whole transform — no floating gun during the fold.
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
