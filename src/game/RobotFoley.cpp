#include "game/RobotFoley.h"
#include "game/Character.h"
#include "game/CharacterAudio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {
namespace {

// The notifies and their sounds are the character profile's (CharacterAudio: the clips of its robot anim sets, the
// FS_DEFAULT_* events resolved through its CHR_* SoundEventSet). The default profile (Optimus) reproduces the previous
// hand-made table exactly (26 / 26 locomotion + pivot notifies, NAV_Idle, Nav_TakeOff_01, Nav_Land / _02 / _03).

// [CONF] TR_Acrobatics_p.SharedAcrobatics.LandingAnims, in array order.
struct LandingAnim { float minHeight, minSpeed; const char* clip; };
const LandingAnim kLandingAnims[] = {
    {1200.0f, 1200.0f, "Nav_Land_03"}, {1000.0f, 1200.0f, "Nav_Land"}, {4500.0f, 0.0f, "Nav_Land_03"},
    {500.0f, 0.0f, "Nav_Land_02"},     {250.0f, 0.0f, "Nav_Land"},
};

constexpr float UU = 0.01f;

} // namespace

const CharacterAudioProfile& RobotFoley::profile() const {
    return profile_ ? *profile_ : CharacterAudio::defaultProfile();
}

void RobotFoley::clipNotifies(const std::string& clip, float a, float b, bool includeStart, float weight,
                              std::vector<const char*>& out) const {
    // Window (a, b] in normalized time; wraps when b < a (looping clip / sync-group phase). Notify times are in
    // seconds of the AUTHORED SequenceLength (the glTF clips are one frame shorter, so fractions are compared).
    const CharacterAudioProfile& p = profile();
    const CharacterAudioProfile::Clip* c = p.clip(clip);
    if (!c || c->length <= 0.0f) return;
    for (const CharacterAudioProfile::Notify& n : c->notifies) {
        if (weight < n.minWeight) continue;
        const std::string& cue = p.notifyCue(n);
        if (cue.empty()) continue;
        float x = n.t / c->length;
        bool in = b >= a ? ((x > a || (includeStart && x >= a)) && x <= b)
                         : (x > a || x <= b || (includeStart && x >= a));
        if (in) out.push_back(cue.c_str());
    }
}

// Notifies of a (non-locomotion) clip in the clip-time window (t0, t1] seconds of the glTF clip
// (`dur` long); looping clips count every cycle the window covers.
void RobotFoley::clipNotifiesTimed(const std::string& clip, float dur, bool loop, float t0, float t1,
                                   bool includeStart, std::vector<const char*>& out) const {
    if (dur <= 0.0f || t1 < t0) return;
    const CharacterAudioProfile& p = profile();
    const CharacterAudioProfile::Clip* c = p.clip(clip);
    if (!c || c->length <= 0.0f) return;
    for (const CharacterAudioProfile::Notify& n : c->notifies) {
        const std::string& cue = p.notifyCue(n);
        if (cue.empty()) continue;
        // Authored fraction -> glTF clip seconds; MinWeight gate: the clip blends in linearly over its
        // transition time, so a notify cannot fire before the clip's weight reaches MinWeight
        // (Idle<->Moving 0.2 s, pivot TransitionBlendTime 0.1 s [CONF]; a one-step flip never fires).
        const float blendIn = clip == "NAV_Idle" ? 0.2f : 0.1f;
        float x = std::max(n.t / c->length * dur, n.minWeight * blendIn);
        if (!loop) {
            if ((x > t0 || (includeStart && x >= t0)) && x <= t1) out.push_back(cue.c_str());
            continue;
        }
        for (float k = std::floor(t0 / dur); k * dur <= t1; k += 1.0f) {
            float xt = k * dur + x;
            if ((xt > t0 || (includeStart && xt >= t0)) && xt <= t1) out.push_back(cue.c_str());
        }
    }
}

// A one-shot clip's notifies by authored time (landing / take-off): t = 0 now, later ones delayed.
void RobotFoley::actionLayer(const std::string& clip, float t, std::vector<const char*>& out) {
    if (clip.empty()) { actionClip_.clear(); actionT_ = 0.0f; return; }
    const CharacterAudioProfile::Clip* c = profile().clip(clip);
    const bool restart = clip != actionClip_ || t < actionT_;
    if (c && c->length > 0.0f) clipNotifiesTimed(clip, c->length, false, restart ? 0.0f : actionT_, t, restart, out);
    actionClip_ = clip; actionT_ = t;
}

void RobotFoley::clipOneShot(const char* clip, std::vector<const char*>& out) {
    const CharacterAudioProfile& p = profile();
    const CharacterAudioProfile::Clip* c = p.clip(clip);
    if (!c) return;
    for (const CharacterAudioProfile::Notify& n : c->notifies) {
        const std::string& cue = p.notifyCue(n);
        if (cue.empty()) continue;
        if (n.t <= 0.0f) out.push_back(cue.c_str());
        else delayed_.push_back({n.t, cue.c_str()});
    }
}

void RobotFoley::tick(const Character& pc, float dt, std::vector<const char*>& out) {
    // Timed landing notifies (Nav_Land_03's servo groan @0.432 s).
    for (size_t i = 0; i < delayed_.size();) {
        delayed_[i].first -= dt;
        if (delayed_[i].first <= 0.0f) { out.push_back(delayed_[i].second); delayed_[i] = delayed_.back(); delayed_.pop_back(); }
        else ++i;
    }

    const bool active = pc.form() == Form::Robot && !pc.isTransforming() && pc.currentModel();
    const bool grounded = pc.onGround();
    const float y = pc.position().y;
    if (!active) { active_ = false; grounded_ = grounded; apexY_ = y; prevVy_ = 0.0f; clip_.clear(); delayed_.clear(); return; }

    // Take-off: Nav_TakeOff_01 (FS_DEFAULT_JUMP @0, MinWeight 0) when the jump launches.
    if (active_ && grounded_ && !grounded && pc.velocity().y > 0.5f) clipOneShot("Nav_TakeOff_01", out);
    // _FallBaseHeight [CONF TnAcrobaticsManager]: the ground height (OnTheGroundBase.BeginState), reset by
    // Falling.BeginState when a fall starts - walking off a ledge, or Jumping/DoubleJumping turning into
    // (FallingFromJump/)Falling as soon as the pawn descends.
    const float vy = pc.velocity().y;
    if (!grounded && (!active_ || (prevVy_ > 0.0f && vy <= 0.0f))) apexY_ = y;
    prevVy_ = vy;
    // Landing: OnTheGroundBase.PlayLandingAnimation -> FindLandingAnimationParams [CONF]:
    // FallDistance = _FallBaseHeight - Height; ForwardSpeed = |Velocity . Rotation|; first LandingAnims
    // entry with ForwardSpeed >= MinSpeed && FallDistance >= MinHeight; none while transforming.
    if (active_ && !grounded_ && grounded) {
        const core::Vec3& v = pc.velocity();
        float fallUU = (apexY_ - y) / UU;
        float speedUU = std::fabs(core::dot(v, core::forwardFromYawPitch(pc.yaw(), 0.0f))) / UU;
        const char* clip = nullptr;
        for (const LandingAnim& la : kLandingAnims)
            if (fallUU >= la.minHeight && speedUU >= la.minSpeed) { clip = la.clip; break; }
        lastFallUU_ = fallUU;
        lastLand_ = clip ? clip : "-";
        if (clip) clipOneShot(clip, out);                    // the landing clip's notifies (Nav_Land_03: + groan @0.432)
    }
    if (grounded) apexY_ = y;                           // OnTheGroundBase.BeginState / standing

    // Base-clip notifies (locomotion master, pivots, idle). The landing clip's own notifies are
    // handled above from the authored selection, so Nav_Land* clips are skipped here.
    const std::string clip = pc.animName();
    const bool loco = clip.rfind("Nav_Strafe", 0) == 0;
    const bool fire = grounded && clip.rfind("Nav_Land", 0) != 0;
    if (loco) {
        // Strafers sync group: one continuous normalized phase across master changes.
        float norm = pc.locoPhase();
        if (fire) clipNotifies(clip, loco_ ? norm_ : norm, norm, !loco_, pc.locoMasterWeight(), out);
        norm_ = norm;
    } else {
        const assets::SkinnedModel* m = pc.currentModel();
        int ci = m->clipByName(clip);
        float dur = ci >= 0 ? m->clips[(size_t)ci].duration : 0.0f;
        float t = pc.animTime();
        const bool loop = clip == "NAV_Idle";               // pivots play once
        // Clip (re)started: a new clip, or its time went backwards. Notifies at 0 fire then.
        bool restart = clip != clip_ || loco_ || t < time_;
        if (fire) clipNotifiesTimed(clip, dur, loop, restart ? 0.0f : time_, t, restart, out);
        time_ = t;
    }
    clip_ = clip; loco_ = loco;
    active_ = true; grounded_ = grounded;
}

} // namespace game
