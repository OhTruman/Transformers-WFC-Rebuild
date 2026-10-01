// Clean-room reconstruction: weapon-fire skeletal recoil (UE3 GameSkelCtrl_Recoil, used by WFC as
// HM_Engine.HmSkelControlRecoil). Each shot restarts the controller; the bone is offset by a
// sine wave per axis that fades out with a smoothstep over TimeDuration.
#pragma once
#include <cmath>
#include <cstdlib>
#include "core/Math.h"

namespace game {

// Authored recoil definition (UE units: rotator units 65536 = 360 deg, location in UU).
struct RecoilDef {
    float duration = 0.33f;                 // TimeDuration (struct default 0.33)
    core::Vec3 rotAmp{0, 0, 0};             // X=pitch, Y=yaw, Z=roll amplitude (rotator units)
    core::Vec3 rotFreq{0, 0, 0};            // sine frequency (rad/s) per axis
    bool rotRandom[3] = {false, false, false};  // ERS_Random start phase per axis
    core::Vec3 locAmp{0, 0, 0};             // X=forward, Y=right, Z=up amplitude (UU)
    core::Vec3 locFreq{0, 0, 0};
    bool locRandom[3] = {false, false, false};
};

// Ion Blaster recoil [CONF]: TransGame.Default__TnWeaponMesh archetype overlaid with the
// WEP_IonBlaster_p.IonBlaster_WEPMESH overrides (weapon.json SpineRecoil / RightHandRecoil).
inline RecoilDef ionBlasterSpineRecoil() {
    RecoilDef d;
    d.duration = 0.8f;                      // IonBlaster override
    d.rotAmp = {500.0f, 1000.0f, 0.0f};     // IonBlaster override
    d.rotFreq = {10.0f, 10.0f, 0.0f};       // archetype
    // RotParams: archetype Y=ERS_Random, IonBlaster overrides Y=ERS_Zero -> all zero-phase.
    return d;
}
inline RecoilDef ionBlasterRightHandRecoil() {
    RecoilDef d;
    d.duration = 0.5f;                          // IonBlaster override
    d.rotAmp = {2000.0f, 500.0f, -2000.0f};     // IonBlaster override
    d.rotFreq = {15.0f, 10.0f, 10.0f};          // IonBlaster override
    d.rotRandom[1] = true;                      // archetype RotParams Y=ERS_Random
    d.locAmp = {-8.0f, 0.0f, 0.0f};             // archetype: 8 UU kick back along the aim
    d.locFreq = {10.0f, 0.0f, 0.0f};            // archetype
    d.locRandom[2] = true;                      // archetype LocParams Z=ERS_Random (amp 0)
    return d;
}

// Runtime state of one recoil skel-control. [MED] update law reconstructed from UE3
// GameSkelCtrl_Recoil: restart -> TimeToGo=TimeDuration, sin offsets 0 or random*2pi; each tick
// SinOffset += Frequency*dt, Offset = Alpha*Amplitude*sin(SinOffset), Alpha = smoothstep(TimeToGo/TimeDuration).
struct RecoilControl {
    RecoilDef def;
    float toGo = 0.0f;
    core::Vec3 rotSin{0, 0, 0}, locSin{0, 0, 0};
    core::Vec3 rotOffset{0, 0, 0};          // rotator units (pitch, yaw, roll)
    core::Vec3 locOffset{0, 0, 0};          // UU (forward, right, up)
    bool active = false;

    void start() {
        auto phase = [](bool rnd) { return rnd ? (float)std::rand() / (float)RAND_MAX * 6.2831853f : 0.0f; };
        toGo = def.duration;
        rotSin = {phase(def.rotRandom[0]), phase(def.rotRandom[1]), phase(def.rotRandom[2])};
        locSin = {phase(def.locRandom[0]), phase(def.locRandom[1]), phase(def.locRandom[2])};
        rotOffset = {0, 0, 0};
        locOffset = {0, 0, 0};
    }
    void tick(float dt) {
        active = false;
        if (toGo <= dt) { toGo = 0.0f; return; }
        toGo -= dt;
        float pct = def.duration > 0.0f ? toGo / def.duration : 0.0f;
        pct = pct < 0.0f ? 0.0f : (pct > 1.0f ? 1.0f : pct);
        float alpha = pct * pct * (3.0f - 2.0f * pct);
        rotSin += def.rotFreq * dt;
        locSin += def.locFreq * dt;
        rotOffset = {alpha * def.rotAmp.x * std::sin(rotSin.x), alpha * def.rotAmp.y * std::sin(rotSin.y),
                     alpha * def.rotAmp.z * std::sin(rotSin.z)};
        locOffset = {alpha * def.locAmp.x * std::sin(locSin.x), alpha * def.locAmp.y * std::sin(locSin.y),
                     alpha * def.locAmp.z * std::sin(locSin.z)};
        active = true;
    }
};

} // namespace game
