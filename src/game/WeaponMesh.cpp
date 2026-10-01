#include "game/WeaponMesh.h"
#include "core/Log.h"

#include <cmath>

namespace game {
namespace {

constexpr float kBlendOutTime = 0.2f;   // [CONF] HmAnimatedMesh_9126.BlendOutTime

// [CONF] AnimNotifies authored on WEP_IonBlaster_ANIM sequences (times in seconds).
struct NotifyDef { const char* clip; WeaponNotify n; };
const NotifyDef kNotifies[] = {
    {"IonBlaster_Idle", {WeaponNotify::Kind::Sound, 0.022f, "BL_WPN_GUN_ION_BLASTER.IDLE_01", ""}},
    {"IonBlaster_Idle", {WeaponNotify::Kind::Sound, 2.751f, "BL_WPN_GUN_ION_BLASTER.IDLE_02", ""}},
    {"IonBlaster_Fire", {WeaponNotify::Kind::Effect, 0.005f, "FX_AssaultRifle_p.FX.Shell_AssaultRifle_FX", "ShellSocket"}},
    {"Shooting_Reload_IonBlaster_AP", {WeaponNotify::Kind::Sound, 0.000f, "BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_01", ""}},
    {"Shooting_Reload_IonBlaster_AP", {WeaponNotify::Kind::Effect, 0.034f, "FX_AssaultRifle_p.FX.Reload_AssaultRifle_FX", "MuzzleFlash"}},
    {"Shooting_Reload_IonBlaster_AP", {WeaponNotify::Kind::Sound, 0.137f, "BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_02", ""}},
    {"Shooting_Reload_IonBlaster_AP", {WeaponNotify::Kind::Effect, 0.174f, "FX_IonBlaster_p.FX.Magazine_IonBlaster_FX", "MagSocket"}},
};

core::Mat4 mat(const float (&a)[16]) { return core::mat4FromArray(a); }

} // namespace

void WeaponMesh::setModel(const assets::SkinnedModel* m) {
    model_ = m;
    sockets_.clear();
    if (!valid()) return;
    clipIdle_ = m->clipByName("IonBlaster_Idle");
    clipFire_ = m->clipByName("IonBlaster_Fire");
    clipReload_ = m->clipByName("Shooting_Reload_IonBlaster_AP");
    // [CONF] WEP_IonBlaster_SKEL SkeletalMeshSockets; relative transforms converted UE -> glTF
    // with the extractor's axis change (vs_common.ue_rot/ue_trans/ue_to_gltf_matrix).
    const float shell[16] = {-0.541059f, -0.840985f, 0.0f, 0.0f, 0.840985f, -0.541059f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, -0.126288f, -0.055891f, -0.033542f, 1.0f};
    const float magz[16] = {0.207816f, 0.900016f, 0.383124f, 0.0f, -0.974045f, 0.226329f, -0.003334f, 0.0f,
                            -0.089713f, -0.372487f, 0.923691f, 0.0f, 0.247790f, 0.364682f, -0.036683f, 1.0f};
    sockets_.push_back({"MuzzleFlash", m->nodeByName("C_Robo04_XT"), core::Mat4::identity()});
    sockets_.push_back({"ShellSocket", m->nodeByName("C_Robo15_XT"), mat(shell)});
    sockets_.push_back({"MagSocket", m->nodeByName("C_Robo01_XT"), mat(magz)});
    LOG_INFO("weapon mesh: %zu clips idle=%d fire=%d reload=%d muzzleNode=%d", m->clips.size(),
             clipIdle_, clipFire_, clipReload_, sockets_[0].node);
    play(Event::Idle);
    std::vector<WeaponNotify> none;
    tick(0.0f, none);
}

void WeaponMesh::play(Event e) {
    int c = e == Event::Fire ? clipFire_ : (e == Event::Reload ? clipReload_ : clipIdle_);
    if (c < 0) c = clipIdle_;
    clip_ = c;
    time_ = 0.0f;
    loop_ = (c == clipIdle_);
    blendOut_ = 0.0f;
}

const char* WeaponMesh::clipName() const {
    return (model_ && clip_ >= 0) ? model_->clips[(size_t)clip_].name.c_str() : "-";
}

void WeaponMesh::tick(float dt, std::vector<WeaponNotify>& fired) {
    if (!valid() || clip_ < 0) return;
    const assets::AnimClip& c = model_->clips[(size_t)clip_];
    // Notifies inside [t0, t0+dt) fire this step (a notify at 0 fires on the first step);
    // looping clips wrap around.
    if (dt > 0.0f) {
        float a = (loop_ && c.duration > 0.0f) ? std::fmod(time_, c.duration) : time_;
        float b = a + dt;
        for (const NotifyDef& d : kNotifies) {
            if (c.name != d.clip) continue;
            float nt = d.n.time;
            if ((nt >= a && nt < b) || (loop_ && b > c.duration && nt < b - c.duration))
                fired.push_back(d.n);
        }
    }
    time_ += dt;
    if (!loop_ && time_ >= c.duration) {          // event anim finished -> back to idle
        float hold = c.duration;
        int finished = clip_;
        clip_ = clipIdle_; time_ = 0.0f; loop_ = true;
        assets::bindPose(*model_, pose1_);
        assets::samplePose(*model_, finished, hold, false, pose1_);
        blendOut_ = kBlendOutTime;
    }

    assets::bindPose(*model_, pose0_);
    assets::samplePose(*model_, clip_, time_, loop_, pose0_);
    if (blendOut_ > 0.0f) {
        // Cross-fade from the held last frame of the event anim into idle.
        assets::blendPose(pose0_, pose1_, {}, blendOut_ / kBlendOutTime);
        blendOut_ -= dt;
    }
    assets::computeGlobals(*model_, pose0_, globals_);
    assets::skinMesh(*model_, globals_, pose_);
}

bool WeaponMesh::socketLocal(const std::string& socket, core::Mat4& out) const {
    for (const Socket& s : sockets_) {
        if (s.name != socket) continue;
        if (s.node < 0 || (size_t)s.node >= globals_.size()) return false;
        out = globals_[(size_t)s.node] * s.rel;
        return true;
    }
    return false;
}

} // namespace game
