// Clean-room reconstruction — Optimus truck audio: a port of WFC's HmVehicleAudioComponent +
// HmPlayerVehicleAudioComponent(Impl) behaviour, as decompiled from HM_Engine script
// (RE-Workspace/work/script/decomp, read-only) with OptimusTruckForm.HmPlayerVehicleAudioComponent_6670
// values and the Veh_Optimus_Prime_SoundSet event -> cue map. [CONF] unless marked.
//
// Inputs mirror what TnCarForm feeds the component:
//   Hovering.UpdateSounds: IsOnGround = hover sim on ground, WheelSlipRatio = 0,
//                          EngineLoadState = 1 (stick forward > 0.01) / 2 (back < -0.01) / 0.
//   Driving.UpdateSounds:  IsOnGround = car on ground, WheelSlipRatio = CarSimulation.SlipAngle.
//   Driving.Begin/EndState (PlayBoostFx / StopBoostFx): PlayBoostSound / StopBoostSound.
//   Hovering/Driving.UpdateJumping: PlayAscendSound.  TnTruckForm.Hovering.DoDash: PlayBoosterSound.
//   StartNitro: PlayNitroSound.  AttemptToRam -> ClientPlayRammingSound: PlayRamSound.
// One-shot events go through HmPawn.PlaySoundEvent -> Actor.PlaySound(cue, bNotReplicated,,
// bStopWhenOwnerDestroyed=true, no SoundLocation): owner-attached [HIGH: UE3 engine default].
// Looping events use Owner.CreateAudioComponent (attached, SocketName, FadeIn / FadeOut).
#pragma once
#include <functional>
#include "core/Math.h"
#include "game/SoundCues.h"

namespace game {

class VehicleAudio {
public:
    struct Input {
        bool entered = false;        // the pawn is in vehicle form (component Attached)
        bool boosting = false;       // TnCarForm state Driving
        bool onGround = false;
        int loadState = 0;           // EngineLoadState 0 / 1 forward / 2 back
        float wheelSlip = 0.0f;      // WheelSlipRatio (rad); 0 while hovering
        core::Vec3 velocity{0, 0, 0};   // m/s
        core::Vec3 forward{0, 0, 1};    // pawn rotation as a vector
        bool ascend = false;         // a jump started this step
        bool booster = false;        // a hover dash started this step
        bool nitro = false;          // a nitro started this step
    };
    using EmitterFn = std::function<SoundCues::Emitter()>;

    void tick(float dt, const Input& in, SoundCues& cues, const EmitterFn& at);
    void ram(SoundCues& cues, const EmitterFn& at);       // PlayRamSound

    // Diagnostics.
    const char* engineState() const;
    float speedMph() const { return speed_; }

private:
    enum class State { None, Boosting, JumpReving, ForwardOnLoad, ForwardOffLoad, ReverseOnLoad, ReverseOffLoad };
    struct Loop { int id = -1; const char* cue = nullptr; };

    bool playLooping(Loop& l, const char* cue, float fadeIn, SoundCues& cues, const EmitterFn& at);
    void stopLooping(Loop& l, float fadeOut, SoundCues& cues);
    void playEvent(const char* cue, SoundCues& cues, const EmitterFn& at);
    void gotoState(State s, SoundCues& cues, const EmitterFn& at);
    void attach();
    void detach(SoundCues& cues);

    bool entered_ = false;
    bool boosting_ = false;
    float speedHist_[15] = {};       // VehicleSpeedHistoryLength 15 (class default)
    int histIdx_ = 0;
    float speed_ = 0.0f;             // averaged mph
    float boostWheelsTimer_ = 0.0f;
    bool onGroundPrev_ = false;
    float jumpRevTimer_ = 0.0f, landTimer_ = 0.0f;
    State state_ = State::None;
    Loop boost_, boostWheels_, booster_, squeal_, engine_;
};

} // namespace game
