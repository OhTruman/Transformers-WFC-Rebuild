// Clean-room reconstruction — the Ion Blaster's own animated mesh (WFC TnWeaponMesh /
// HmAnimatedMesh): a 34-joint skeletal mesh with its own AnimSet, event animations, sockets and
// AnimNotifies. All names/times below are [CONF] from WEP_IonBlaster_p (cooked in A1_IAC_Base_m):
//   WeaponEventAnims: WP_Fire -> IonBlaster_Fire, WP_Reload -> Shooting_Reload_IonBlaster_AP,
//                     WP_Equip -> IonBlaster_Equip, WP_PutDown -> IonBlaster_Drop
//   IdleAnimation IonBlaster_IdleGroup (IonBlaster_Idle), HmAnimatedMesh.BlendOutTime 0.2 s
//   Sockets: MuzzleFlash -> C_Robo04_XT, ShellSocket -> C_Robo15_XT (+offset), MagSocket -> C_Robo01_XT
#pragma once
#include <string>
#include <vector>
#include "assets/SkinnedModel.h"
#include "core/Math.h"
#include "render/Mesh.h"

namespace game {

// An authored AnimNotify on a weapon sequence (HmAnimNotify_Sound / HmAnimNotify_PlayEffect).
struct WeaponNotify {
    enum class Kind { Sound, Effect };
    Kind kind;
    float time;                 // seconds into the sequence
    std::string what;           // SoundCue or ParticleSystem object name
    std::string socket;         // effects only
};

class WeaponMesh {
public:
    enum class Event { Idle, Fire, Reload };

    void setModel(const assets::SkinnedModel* m);
    // Any other weapon: event anims and the MuzzleFlash socket from its WeaponDef (TnWeaponMesh.WeaponEventAnims,
    // IdleAnimation, SkeletalMeshSockets) [CONF data]. Authored AnimNotifies are reproduced for the Ion Blaster only.
    void setModelGeneric(const assets::SkinnedModel* m, const struct WeaponDef& d);
    bool valid() const { return model_ && model_->valid(); }

    void play(Event e);
    // Advance; notifies whose time was crossed this step are appended to `fired`.
    void tick(float dt, std::vector<WeaponNotify>& fired);

    const render::MeshData& pose() const;   // skins the current pose on demand (tick only poses the bones)
    const char* clipName() const;

    // Socket transform in weapon-mesh space (bone global * socket relative transform).
    bool socketLocal(const std::string& socket, core::Mat4& out) const;

private:
    struct Socket { std::string name; int node; core::Mat4 rel; };
    const assets::SkinnedModel* model_ = nullptr;
    int clipIdle_ = -1, clipFire_ = -1, clipReload_ = -1;
    int clip_ = -1;
    float time_ = 0.0f;
    bool loop_ = true;
    // Blend-out from the finished event anim back to idle (HmAnimatedMesh.BlendOutTime).
    float blendOut_ = 0.0f;
    assets::LocalPose pose0_, pose1_;
    std::vector<core::Mat4> globals_;
    mutable render::MeshData pose_;
    mutable bool skinDirty_ = false;
    mutable std::vector<core::Mat4> skinGlobals_;
    std::vector<Socket> sockets_;
};

} // namespace game
