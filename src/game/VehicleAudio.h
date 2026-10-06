// Clean-room reconstruction — vehicle-form audio: a port of WFC's HmVehicleAudioComponent +
// HmPlayerVehicleAudioComponent(Impl) behaviour, as decompiled from HM_Engine script
// (RE-Workspace/work/script/decomp, read-only), driven by each chassis's own HmPlayerVehicleAudioComponent data
// (gears, reverse / boost / jump-rev loops and one-shots, land tables, slots, tunables) and vehicle SoundEventSet
// (CharacterAudio). [CONF] unless marked.
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
#include <string>
#include <functional>
#include "core/Math.h"
#include "game/SoundCues.h"
#include "game/CharacterAudio.h"
#include <algorithm>
#include <vector>

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
        bool booster = false;        // PlayBoosterSound this step: Car / Truck Hovering.DoDash, Plane Hovering.PlayHoverFx
        bool boosterStop = false;    // StopBoosterSound this step: Plane Hovering.StopHoverFx
        float boosterAmount = -1.0f; // set_BoosterAmount (Car / Plane Hovering.UpdateFx: the largest thruster
                                     // contribution) -> BoosterParameter on the booster sound; < 0: not set this step
        bool nitro = false;          // a nitro started this step
        // Plane Hovering.UpdateDashing: PlayAscendSound / StopAscendSound / PlayDescendSound / StopDescendSound;
        // Car Driving.UpdateRolling and Plane UpdateRolling / OnUpdateAi: PlayRollSound (Car: PlayAscendSound);
        // Tank ClientPlaySpecialMoveSound: PlayOneEightySound.
        bool ascendStop = false, descend = false, descendStop = false, roll = false, oneEighty = false;
    };
    using EmitterFn = std::function<SoundCues::Emitter()>;

    // The character's vehicle sounds (CharacterAudioProfile::vehicle); until set, the default profile's.
    void setProfile(const CharacterAudioProfile& p);
    void tick(float dt, const Input& in, SoundCues& cues, const EmitterFn& at);
    void ram(SoundCues& cues, const EmitterFn& at);       // PlayRamSound
    // Stop every sound this component owns at once (class change, match restart, map unload, ownership change).
    void stopAll(SoundCues& cues);
    int liveLoops() const;                                // diagnostics: loop components with an instance
    bool entered() const { return entered_; }

    // Diagnostics.
    const char* engineState() const;
    float speedMph() const { return speed_; }

private:
    enum class State { None, Boosting, JumpReving, ForwardOnLoad, ForwardOffLoad, ReverseOnLoad, ReverseOffLoad };
    VehicleAudioComponentData c_;    // the chassis's component, sounds resolved to cues
    bool named_ = false;
    void ensureNames();
    struct Loop { int id = -1; const char* cue = nullptr; };

    bool playLooping(Loop& l, const char* cue, float fadeIn, SoundCues& cues, const EmitterFn& at);
    void stopLooping(Loop& l, float fadeOut, SoundCues& cues);
    void playEvent(const std::string& cue, SoundCues& cues, const EmitterFn& at);
    void gotoState(State s, SoundCues& cues, const EmitterFn& at);
    int computeGear() const;
    void attach(SoundCues& cues, const EmitterFn& at);
    void detach(SoundCues& cues);

    bool entered_ = false;
    bool boosting_ = false;
    std::vector<float> speedHist_ = std::vector<float>(15, 0.0f);   // VehicleSpeedHistoryLength
    int histIdx_ = 0;
    float speed_ = 0.0f;             // averaged mph
    float boostWheelsTimer_ = 0.0f;
    bool onGroundPrev_ = false;
    float jumpRevTimer_ = 0.0f, landTimer_ = 0.0f;
    float spazTimer_ = 0.0f;         // Impl.EngineOneshotSpazTimer
    State state_ = State::None;
    Loop boost_, boostWheels_, booster_, squeal_, speedLoop_, tread_;
    std::vector<Loop> engine_, oneshots_;   // the state's engine loops; one-shot components
};

} // namespace game
