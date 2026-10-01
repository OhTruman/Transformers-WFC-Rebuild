// Clean-room reconstruction — procedural weapon recoil (WFC HM_Engine.HmSkelControlRecoil).
//
// Robot_ANIMTREE (TR_Shared_ANIMTREE_p) SkelControlLists [CONF]:
//   R_Arm02_Shoulder_XB   -> HmSkelControlRecoil "RightHandRecoil"
//   C_Spine02_Lumbar02_XB -> HmSkelControlRecoil "SpineRecoil"
//   L_Arm01_Clav_XB       -> HmSkelControlRecoil "LeftHandRecoil" (no Ion Blaster def; unused)
// The firing weapon mesh (TnWeaponMesh) supplies the RecoilDef for each shot. HmSkelControlRecoil's
// RecoilDef layout is identical to UE3 GameSkelCtrl_Recoil, so the per-tick evaluation follows it:
// restart on every shot (TimeToGo = TimeDuration; ERS_Random axes get a random sine phase), then
// offset = smoothstep(TimeToGo/TimeDuration) * Amplitude * sin(SinOffset + TimeToGo * Frequency),
// rotation in UE rotator units (65536 = 360 deg) applied about the bone in aim space.
#pragma once
#include <cmath>
#include <cstdlib>
#include "core/Math.h"

namespace game {

struct RecoilDef {
    float duration = 0.33f;                 // HmSkelControlRecoil CDO default
    core::Vec3 rotAmp{0, 0, 0};             // X=pitch Y=yaw Z=roll (UE rotator units)
    core::Vec3 rotFreq{0, 0, 0};
    bool rotRandom[3] = {false, false, false};   // ERS_Random per axis
    core::Vec3 locAmp{0, 0, 0};             // X=forward Y=right Z=up (UU)
    core::Vec3 locFreq{0, 0, 0};
    bool locRandom[3] = {false, false, false};
};

// [CONF] Ion Blaster = TnWeaponMesh CDO (TransGame.xxx) merged with IonBlaster_WEPMESH overrides.
//   SpineRecoil:     TimeDuration 0.8, RotAmplitude (500,1000,0), RotFrequency (10,10,0) [CDO],
//                    RotParams Y=ERS_Zero (override of the CDO's ERS_Random).
//   RightHandRecoil: TimeDuration 0.5, RotAmplitude (2000,500,-2000), RotFrequency (15,10,10),
//                    RotParams Y=ERS_Random [CDO], LocAmplitude X=-8 @ LocFrequency X=10 [CDO].
inline RecoilDef ionBlasterSpineRecoil() {
    RecoilDef d; d.duration = 0.8f;
    d.rotAmp = {500, 1000, 0}; d.rotFreq = {10, 10, 0};
    return d;
}
inline RecoilDef ionBlasterRightHandRecoil() {
    RecoilDef d; d.duration = 0.5f;
    d.rotAmp = {2000, 500, -2000}; d.rotFreq = {15, 10, 10};
    d.rotRandom[1] = true;
    d.locAmp = {-8, 0, 0}; d.locFreq = {10, 0, 0};
    d.locRandom[2] = true;
    return d;
}

class RecoilControl {
public:
    void play(const RecoilDef& d) {
        def_ = d;
        timeToGo_ = d.duration;
        const float twoPi = 2.0f * core::PI;
        auto ph = [&](bool rnd) { return rnd ? (float)std::rand() / (float)RAND_MAX * twoPi : 0.0f; };
        for (int i = 0; i < 3; ++i) { rotSin_[i] = ph(def_.rotRandom[i]); locSin_[i] = ph(def_.locRandom[i]); }
    }

    void tick(float dt) {
        active_ = false;
        if (timeToGo_ > dt) {
            timeToGo_ -= dt;
            if (timeToGo_ > 0.0f && def_.duration > 0.0f) {
                active_ = true;
                float p = core::clampf(timeToGo_ / def_.duration, 0.0f, 1.0f);
                float alpha = p * p * (3.0f - 2.0f * p);
                const float* ra = &def_.rotAmp.x; const float* rf = &def_.rotFreq.x;
                const float* la = &def_.locAmp.x; const float* lf = &def_.locFreq.x;
                for (int i = 0; i < 3; ++i) {
                    rot_[i] = std::trunc(alpha * ra[i] * std::sin(rotSin_[i] + timeToGo_ * rf[i]));
                    loc_[i] = alpha * la[i] * std::sin(locSin_[i] + timeToGo_ * lf[i]);
                }
            }
        } else {
            timeToGo_ = 0.0f;
        }
    }

    bool active() const { return active_; }

    // Model-space rotation + translation for this control, in an aim frame given by unit
    // forward/right/up axes (model space). UE: pitch about right, yaw about up, roll about fwd.
    void aimSpaceOffset(const core::Vec3& fwd, const core::Vec3& right, const core::Vec3& up,
                        core::Mat4& outRot, core::Vec3& outLoc) const {
        const float k = 2.0f * core::PI / 65536.0f;
        // +pitch = muzzle up (+angle about right); +yaw = turn right (-angle about up, since the
        // frame is right-handed with -Z forward).
        outRot = axisAngle(up, -rot_[1] * k) * axisAngle(right, rot_[0] * k) * axisAngle(fwd, rot_[2] * k);
        const float uu = 0.01f;   // UU -> metres
        outLoc = fwd * (loc_[0] * uu) + right * (loc_[1] * uu) + up * (loc_[2] * uu);
    }

private:
    static core::Mat4 axisAngle(const core::Vec3& a, float ang) {
        float h = ang * 0.5f, s = std::sin(h);
        core::Quat q{a.x * s, a.y * s, a.z * s, std::cos(h)};
        return core::mat4FromQuat(q);
    }

    RecoilDef def_;
    float timeToGo_ = 0.0f;
    float rotSin_[3] = {0, 0, 0}, locSin_[3] = {0, 0, 0};
    float rot_[3] = {0, 0, 0}, loc_[3] = {0, 0, 0};
    bool active_ = false;
};

} // namespace game
