#include "game/RobotFoley.h"
#include "game/Character.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {
namespace {

// CHR_OPTIMUS event -> cue (SoundCues table names).
constexpr const char* kWalk = "BL_FS_LRG_BOT.FS_WALK_DEFAULT";
constexpr const char* kRun = "BL_FS_LRG_BOT.FS_RUN_DEFAULT";
constexpr const char* kScuff = "BL_FS_LRG_BOT.FS_SCUFF_DEFAULT";
constexpr const char* kJump = "BL_FS_LRG_BOT.FS_JUMP";
constexpr const char* kLand = "BL_FS_LRG_BOT.FS_LAND_DEFAULT";
constexpr const char* kHardLand = "BL_FS_LRG_BOT.FS_LAND_HARD";
constexpr const char* kHighFall = "BL_FS_LRG_BOT.FS_LAND_HIGH_FALL";
constexpr const char* kGroan = "BL_FS_LRG_BOT.FOLEY_FS_GROAN_SERVO_01";
constexpr const char* kIdleFoley = "BL_FOLY_IDLES.OPTIMUS_IDLE";

// [CONF] notify times in seconds of the AUTHORED SequenceLength (the glTF clips are one frame
// shorter, so times are compared as fractions of the authored length).
struct Notify { const char* clip; float len; float t; const char* cue; float minWeight; };
const Notify kNotifies[] = {
    {"Nav_StrafeJog_F", 0.766667f, 0.091f, kRun, 0.25f}, {"Nav_StrafeJog_F", 0.766667f, 0.513f, kRun, 0.25f},
    {"Nav_StrafeJog_B", 0.766667f, 0.194f, kRun, 0.0f},  {"Nav_StrafeJog_B", 0.766667f, 0.543f, kRun, 0.25f},
    {"Nav_StrafeJog_L", 0.766667f, 0.137f, kRun, 0.25f}, {"Nav_StrafeJog_L", 0.766667f, 0.523f, kRun, 0.25f},
    {"Nav_StrafeJog_R", 0.766667f, 0.132f, kRun, 0.25f}, {"Nav_StrafeJog_R", 0.766667f, 0.529f, kRun, 0.25f},
    {"Nav_StrafeWalk_F", 1.133333f, 0.238f, kWalk, 0.0f}, {"Nav_StrafeWalk_F", 1.133333f, 0.855f, kWalk, 0.25f},
    {"Nav_StrafeWalk_B", 1.133333f, 0.208f, kWalk, 0.0f}, {"Nav_StrafeWalk_B", 1.133333f, 0.777f, kWalk, 0.25f},
    {"Nav_StrafeWalk_L", 1.133333f, 0.231f, kWalk, 0.0f}, {"Nav_StrafeWalk_L", 1.133333f, 0.808f, kWalk, 0.25f},
    {"Nav_StrafeWalk_R", 1.133333f, 0.230f, kWalk, 0.0f}, {"Nav_StrafeWalk_R", 1.133333f, 0.811f, kWalk, 0.25f},
    {"Nav_IdlePivot90_L", 0.566667f, 0.002f, kScuff, 0.0f}, {"Nav_IdlePivot90_L", 0.566667f, 0.007f, kGroan, 0.25f},
    {"Nav_IdlePivot90_L", 0.566667f, 0.015f, kWalk, 0.0f},  {"Nav_IdlePivot90_L", 0.566667f, 0.222f, kWalk, 0.0f},
    {"Nav_IdlePivot90_R", 0.566667f, 0.002f, kScuff, 0.0f}, {"Nav_IdlePivot90_R", 0.566667f, 0.006f, kWalk, 0.0f},
    {"Nav_IdlePivot90_R", 0.566667f, 0.034f, kGroan, 0.25f}, {"Nav_IdlePivot90_R", 0.566667f, 0.129f, kWalk, 0.25f},
    {"Nav_IdlePivot90_R", 0.566667f, 0.389f, kWalk, 0.25f},
    {"NAV_Idle", 3.666667f, 0.0f, kIdleFoley, 0.25f},       // Optimus_ROBO_ANIM NAV_Idle
};

// [CONF] TR_Acrobatics_p.SharedAcrobatics.LandingAnims, in array order.
struct LandingAnim { float minHeight, minSpeed; const char* clip; };
const LandingAnim kLandingAnims[] = {
    {1200.0f, 1200.0f, "Nav_Land_03"}, {1000.0f, 1200.0f, "Nav_Land"}, {4500.0f, 0.0f, "Nav_Land_03"},
    {500.0f, 0.0f, "Nav_Land_02"},     {250.0f, 0.0f, "Nav_Land"},
};

constexpr float UU = 0.01f;

} // namespace

void RobotFoley::clipNotifies(const std::string& clip, float a, float b, bool includeStart, float weight,
                              std::vector<const char*>& out) const {
    // Window (a, b] in normalized time; wraps when b < a (looping clip / sync-group phase).
    for (const Notify& n : kNotifies) {
        if (clip != n.clip || weight < n.minWeight) continue;
        float x = n.t / n.len;
        bool in = b >= a ? ((x > a || (includeStart && x >= a)) && x <= b)
                         : (x > a || x <= b || (includeStart && x >= a));
        if (in) out.push_back(n.cue);
    }
}

// Notifies of a (non-locomotion) clip in the clip-time window (t0, t1] seconds of the glTF clip
// (`dur` long); looping clips count every cycle the window covers.
void RobotFoley::clipNotifiesTimed(const std::string& clip, float dur, bool loop, float t0, float t1,
                                   bool includeStart, std::vector<const char*>& out) const {
    if (dur <= 0.0f || t1 < t0) return;
    for (const Notify& n : kNotifies) {
        if (clip != n.clip) continue;
        // Authored fraction -> glTF clip seconds; MinWeight gate: the clip blends in linearly over its
        // transition time, so a notify cannot fire before the clip's weight reaches MinWeight
        // (Idle<->Moving 0.2 s, pivot TransitionBlendTime 0.1 s [CONF]; a one-step flip never fires).
        const float blendIn = clip == "NAV_Idle" ? 0.2f : 0.1f;
        float x = std::max(n.t / n.len * dur, n.minWeight * blendIn);
        if (!loop) {
            if ((x > t0 || (includeStart && x >= t0)) && x <= t1) out.push_back(n.cue);
            continue;
        }
        for (float k = std::floor(t0 / dur); k * dur <= t1; k += 1.0f) {
            float xt = k * dur + x;
            if ((xt > t0 || (includeStart && xt >= t0)) && xt <= t1) out.push_back(n.cue);
        }
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
    if (!active) { active_ = false; grounded_ = grounded; apexY_ = y; clip_.clear(); delayed_.clear(); return; }

    // Take-off: Nav_TakeOff_01 (FS_DEFAULT_JUMP @0, MinWeight 0) when the jump launches.
    if (active_ && grounded_ && !grounded && pc.velocity().y > 0.5f) out.push_back(kJump);
    if (!grounded) {
        if (grounded_ || !active_) apexY_ = y;
        apexY_ = std::max(apexY_, y);
    }
    // Landing: the clip LandingAnims would pick, and its notifies.
    if (active_ && !grounded_ && grounded) {
        const core::Vec3& v = pc.velocity();
        float fallUU = (apexY_ - y) / UU;
        float speedUU = std::sqrt(v.x * v.x + v.z * v.z) / UU;
        const char* clip = nullptr;
        for (const LandingAnim& la : kLandingAnims)
            if (fallUU >= la.minHeight && speedUU >= la.minSpeed) { clip = la.clip; break; }
        lastFallUU_ = fallUU;
        lastLand_ = clip ? clip : "-";
        if (clip && std::strcmp(clip, "Nav_Land") == 0) out.push_back(kLand);
        else if (clip && std::strcmp(clip, "Nav_Land_02") == 0) out.push_back(kHardLand);
        else if (clip) {                                   // Nav_Land_03
            out.push_back(kHighFall);
            out.push_back(kHardLand);
            delayed_.push_back({0.432f, kGroan});
        }
    }

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
