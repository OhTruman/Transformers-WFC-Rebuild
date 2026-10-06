// Clean-room reconstruction — per-chassis vehicle particle effects through Rendering's generic particle runtime (Systems
// M08e). Gameplay owns the vehicle state; Rendering draws; this decides, per the form classes' script, which authored
// effect plays when, where and with which parameters [CONF decompiled TransGame]:
//   TnVehicleFxPlayer.Play: every entry {ParticleSystemTemplate, Socket} of the set is played attached to that socket of
//       the vehicle mesh, then SetColorParameter('Color', Pawn.EnergonColor). Stop: every entry of the set stops.
//   car / truck (TnCarForm): Hovering.UpdateFx - HoverFX while FxAllowed; per entry vector 'Size' =
//       min(1, linear + angular thruster contribution) x socket RelativeScale (CalculateBoosterFxAmount, HoverPhysics:
//       linear = clamp(((a - g) . dir) / 3000, 0, 1), angular = clamp(((normal(p) x dir) . alpha) / 6, 0, 1), dir = the
//       socket's -X, a / alpha the rigid body's local accelerations VLerp-smoothed by 0.1); the largest |Size| is the
//       audio BoosterAmount. Driving.UpdateFx - BoostFx while FxAllowed, UpdateBoostFx each tick: 'Color' =
//       lerp(EnergonColor, Yellow, NormalizedJumpTimeRemaining), alpha lerp(100, 255, same). JumpFX on a jump and on the
//       Driving roll; truck RamFX from StartNitro to StopNitro. OnEndPlay stops JumpFX.
//   tank (TnTankForm.UpdateFx): HoverFX whenever FxAllowed (no Size), BoostFx while the tank sim boosts; JumpFX on jump.
//   jet (TnPlaneForm): Hovering - HoverFX + Size as the car but smoothed by 0.3, limits 3000 / 6; Flying - BoostFx
//       (no colour update).
//   FxAllowed = TnVehicleForm._FxAllowed (TnAnimNotify_ToggleVehicleFx in the transform clips: Transform_ToRobot_VEH
//       disables at 0, Transform_ToVehicle_VEH enables at its notify) and not cloaking.
// The thruster maths is done in world space (glTF): dot and triple products are frame-invariant, the angular acceleration
// is taken from the body's world rotation, so the UE -> glTF reflection cancels [HIGH].
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core/Math.h"
#include "game/CharacterAudio.h"
#include "game/VehicleFormAudio.h"

namespace game {

class VehicleFxDriver {
public:
    // Rendering's runtime (IRenderer::spawnParticleEffect / setParticleEffectTransform / setParticleEffectParam /
    // stopParticleEffect), bound by the host. Unbound: nothing is driven (the host keeps its own fallback).
    struct Runtime {
        std::function<int(const std::string& tpl, const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& up)> spawnAt;
        std::function<bool(int, const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& up)> setTransform;
        std::function<bool(int, const std::string& name, const float rgba[4])> setParam;
        std::function<void(int)> stop;
    };
    struct Inputs {
        VehicleFormSignals::Kind kind = VehicleFormSignals::Kind::Truck;
        bool fxAllowed = false;        // vehicle form shown, FxAllowed (transform notifies), not cloaking
        bool hovering = false;         // car / truck / jet Hovering state
        bool boostState = false;       // Driving / Flying / tank boosting
        bool jumpStart = false;        // a jump (or the car's Driving roll) this step
        bool nitroActive = false;      // truck nitro running
        bool formEnded = false;        // the vehicle form ended this step (OnEndPlay)
        float normJumpRemaining = 0.0f;   // car Driving: NormalizedJumpTimeRemaining (0..1)
        float energon[3] = {1, 1, 1};  // Pawn.EnergonColor (linear)
        core::Mat4 body;               // vehicle rigid body / mesh world matrix
        core::Vec3 velocity{0, 0, 0};  // m/s
        core::Vec3 gravity{0, -9.81f, 0};   // m/s^2
        std::function<bool(const std::string& bone, core::Mat4& out)> boneWorld;
    };

    void setRuntime(Runtime r) { rt_ = std::move(r); }
    bool bound() const { return (bool)rt_.spawnAt; }
    void setData(const VehicleFxData* d);              // the chassis's effects (class change: stops the old set)
    // One step. Returns the audio BoosterAmount (largest hover |Size|), or -1 when no hover set is sized.
    float tick(float dt, const Inputs& in);
    void stopAll();
    int live() const;                                   // diagnostics: live effect handles

private:
    struct Live { int handle; const VehicleFxData::Entry* e; float age; };
    bool socketWorld(const Inputs& in, const std::string& socket, core::Mat4& out) const;
    void startSet(const Inputs& in, const std::vector<VehicleFxData::Entry>& set, std::vector<Live>& out);
    void stopSet(std::vector<Live>& v);
    void follow(const Inputs& in, std::vector<Live>& v);
    const VehicleFxData* d_ = nullptr;
    Runtime rt_;
    std::vector<Live> hover_, boost_, jump_, ram_;
    bool havePrev_ = false;
    core::Vec3 prevVel_{0, 0, 0}, prevOmega_{0, 0, 0}, smoothA_{0, 0, 0}, smoothAlpha_{0, 0, 0};
    core::Mat4 prevBody_;
};

} // namespace game
