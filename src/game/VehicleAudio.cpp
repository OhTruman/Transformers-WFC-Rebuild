#include "game/VehicleAudio.h"

namespace game {
namespace {

// [CONF] OptimusTruckForm.HmPlayerVehicleAudioComponent_6670 over the class defaults.
constexpr float kBoostFadeIn = 0.1f;      // Default__HmVehicleAudioComponent.BoostFadeInTime
constexpr float kBoostFadeOut = 0.15f;    // BoostFadeOutTime
constexpr float kBoostWheelsDelay = 0.27f;// BoostWheelsGroundCheckDelay
constexpr float kSquealMinMph = 20.0f;    // TireSquealSpeedMin
constexpr float kSquealFade = 0.5f;       // TireSquealCrossfadeTime
constexpr float kEngineFadeIn = 0.1f;     // Default__HmPlayerVehicleAudioComponent.EngineFadeInTime
constexpr float kEngineFadeOut = 0.2f;    // EngineFadeOutTime
constexpr float kJumpRevTime = 0.25f;     // JumpRevTime (JumpRevSounds.UseJumpRev = true)
constexpr float kMph = 2.23694f;          // ComputeSpeedMPH: |Velocity| (UU/s) x 0.0223694

// Veh_Optimus_Prime_SoundSet (events mapped to None are omitted: Auto_Speed, Auto_Tire_Tread_Default,
// the gear one-shots).
constexpr const char* kBoostStart = "VEH_OPTIMUS_BOOST_START";   // BoostSound
constexpr const char* kBoostWheels = "VEH_OPTIMUS_BOOST_WHEELS"; // BoostWheelsSound
constexpr const char* kBoostEnd = "VEH_OPTIMUS_BOOST_END";       // BoostStopSound
constexpr const char* kBoostLoop = "VEH_OPTIMUS_BOOST_LOOP";     // BoostSounds.BoostLoops
constexpr const char* kOnLoad = "VEH_OPTIMUS_DRIVE_ONLOAD";      // DriveSounds[*].OnLoadLoops, ReverseSound.OnLoadLoops
constexpr const char* kOffLoad = "VEH_OPTIMUS_DRIVE_OFFLOAD";    // ... OffLoadLoops
constexpr const char* kJumpLoop = "VEH_OPTIMUS_DRIVE_JUMP_LOOP"; // JumpRevSounds.JumpRevLoops
constexpr const char* kAscend = "VEH_OPTIMUS_DRIVE_JUMP_START";  // AscendSound
constexpr const char* kBooster = "VEH_OPTIMUS_RAM_BOOST_START";  // BoosterSound (Auto_Ram_Boost)
constexpr const char* kNitro = "VEH_OPTIMUS_RAM_NITRO_START";    // NitroSound
constexpr const char* kRam = "VEH_TRUCK_RAM_IMPACT";             // RamSound
constexpr const char* kSqueal = "VEH_OPTIMUS_TIRE_SQUEAL";       // DefaultTireSquealSound
// HoverLandSound / BoostLandSound {TimeInAirThreshold 0.15 light, 2.0 heavy}.
constexpr const char* kHoverLand[2] = {"VEH_OPTIMUS_HOVER_LAND_LIGHT", "VEH_OPTIMUS_HOVER_LAND_HEAVY"};
constexpr const char* kWheelsLand[2] = {"VEH_OPTIMUS_WHEELS_LAND_LIGHT", "VEH_OPTIMUS_WHEELS_LAND_HEAVY"};
constexpr float kLandThreshold[2] = {0.15f, 2.0f};

} // namespace

const char* VehicleAudio::engineState() const {
    switch (state_) {
        case State::Boosting: return "Boosting";
        case State::JumpReving: return "JumpReving";
        case State::ForwardOnLoad: return "ForwardOnLoad";
        case State::ForwardOffLoad: return "ForwardOffLoad";
        case State::ReverseOnLoad: return "ReverseOnLoad";
        case State::ReverseOffLoad: return "ReverseOffLoad";
        default: return "None";
    }
}

// HmVehicleAudioComponent.PlayLoopingSound: keep a component already playing the same cue; otherwise
// stop it (fading over FadeInTime) and create a new attached component that fades in.
bool VehicleAudio::playLooping(Loop& l, const char* cue, float fadeIn, SoundCues& cues, const EmitterFn& at) {
    if (l.id >= 0 && cues.playing(l.id) && l.cue == cue) return true;
    stopLooping(l, fadeIn, cues);
    l.id = cues.play(cue, at(), 0.0f, speed_);
    l.cue = cue;
    if (l.id >= 0 && fadeIn > 0.0f) cues.fadeIn(l.id, fadeIn);
    return l.id >= 0;
}

// StopLoopingSound: bAutoDestroy + FadeOut(FadeOutTime).
void VehicleAudio::stopLooping(Loop& l, float fadeOut, SoundCues& cues) {
    if (l.id >= 0) cues.stop(l.id, fadeOut);
    l.id = -1; l.cue = nullptr;
}

void VehicleAudio::playEvent(const char* cue, SoundCues& cues, const EmitterFn& at) {
    cues.play(cue, at(), 0.0f, speed_);
}

void VehicleAudio::ram(SoundCues& cues, const EmitterFn& at) { playEvent(kRam, cues, at); }

// HmPlayerVehicleAudioComponentImpl states: EndState stops that state's loops, BeginState plays the next.
void VehicleAudio::gotoState(State s, SoundCues& cues, const EmitterFn& at) {
    if (s == state_) return;
    stopLooping(engine_, state_ == State::Boosting ? kBoostFadeOut : kEngineFadeOut, cues);
    state_ = s;
    switch (s) {
        case State::Boosting: playLooping(engine_, kBoostLoop, kBoostFadeIn, cues, at); break;
        case State::JumpReving: playLooping(engine_, kJumpLoop, kEngineFadeIn, cues, at); break;
        case State::ForwardOnLoad: case State::ReverseOnLoad: playLooping(engine_, kOnLoad, kEngineFadeIn, cues, at); break;
        case State::ForwardOffLoad: case State::ReverseOffLoad: playLooping(engine_, kOffLoad, kEngineFadeIn, cues, at); break;
        default: break;
    }
    // The gear / boost / jump-rev / reverse one-shot lists map to None for Optimus (no EngineOneshotSpaz).
}

void VehicleAudio::attach() {
    entered_ = true;
    for (float& h : speedHist_) h = 0.0f;
    histIdx_ = 0; speed_ = 0.0f;
    onGroundPrev_ = false; jumpRevTimer_ = 0.0f; landTimer_ = 0.0f;   // Impl.Attached
    state_ = State::None;
}

void VehicleAudio::detach(SoundCues& cues) {
    gotoState(State::None, cues, nullptr);                 // Impl.Detached: StopEngineSounds
    stopLooping(squeal_, kSquealFade, cues);
    stopLooping(boost_, kBoostFadeOut, cues);
    stopLooping(boostWheels_, kBoostFadeOut, cues);
    entered_ = false;
}

void VehicleAudio::tick(float dt, const Input& in, SoundCues& cues, const EmitterFn& at) {
    if (in.entered && !entered_) attach();

    // Boost: Driving.BeginState -> PlayBoostSound; EndState (also on leaving the form) -> StopBoostSound.
    if (in.boosting && !boosting_ && in.entered) {
        playLooping(boost_, kBoostStart, kBoostFadeIn, cues, at);
        if (in.onGround) playLooping(boostWheels_, kBoostWheels, kBoostFadeIn, cues, at);
        boostWheelsTimer_ = 0.0f;
    } else if (boosting_ && (!in.boosting || !in.entered)) {
        stopLooping(boost_, kBoostFadeOut, cues);
        stopLooping(boostWheels_, kBoostFadeOut, cues);
        playEvent(kBoostEnd, cues, at);
    }
    boosting_ = in.boosting && in.entered;

    if (!in.entered) { if (entered_) detach(cues); return; }

    if (in.ascend) playEvent(kAscend, cues, at);                          // PlayAscendSound
    if (in.booster) playLooping(booster_, kBooster, 0.0f, cues, at);     // PlayBoosterSound (no fade)
    if (in.nitro) playEvent(kNitro, cues, at);                            // PlayNitroSound

    // HmVehicleAudioComponent.Tick: UpdateVehicleSpeed (15-sample mph average), tire squeal, boost wheels.
    speedHist_[histIdx_] = core::length(in.velocity) * kMph;
    histIdx_ = (histIdx_ + 1) % 15;
    speed_ = 0.0f;
    for (float h : speedHist_) speed_ += h;
    speed_ /= 15.0f;
    if (in.onGround && speed_ >= kSquealMinMph) {
        playLooping(squeal_, kSqueal, kSquealFade, cues, at);
        cues.update(squeal_.id, {0, 0, 0}, in.wheelSlip);
    } else {
        stopLooping(squeal_, kSquealFade, cues);
    }
    if (boosting_) {
        boostWheelsTimer_ += dt;
        if (boostWheelsTimer_ >= kBoostWheelsDelay && !in.onGround) stopLooping(boostWheels_, kBoostFadeOut, cues);
    }

    // Impl.UpdateEngineSound.
    int dir = 0;                                                      // GetMovementDirection
    if (speed_ > 1.0f) dir = core::dot(in.velocity, in.forward) >= 0.0f ? 1 : 2;
    bool jumpRev = false;                                             // GetIsJumpReving
    if (in.onGround) jumpRevTimer_ = 0.0f;
    else { jumpRevTimer_ += dt; jumpRev = jumpRevTimer_ >= kJumpRevTime; }
    State s;
    if (boosting_) s = State::Boosting;
    else if (jumpRev) s = State::JumpReving;
    else if (dir == 2) s = in.loadState == 2 ? State::ReverseOnLoad : State::ReverseOffLoad;
    else if (dir == 1) s = in.loadState == 1 ? State::ForwardOnLoad : State::ForwardOffLoad;
    else s = State::ForwardOffLoad;
    gotoState(s, cues, at);
    if (engine_.id >= 0) cues.update(engine_.id, {0, 0, 0}, speed_);   // UpdateEngineSoundParameters
    if (boost_.id >= 0) cues.update(boost_.id, {0, 0, 0}, speed_);

    // Impl.UpdateLandSound: on touchdown the highest TimeInAirThreshold reached (Boosting: BoostLandSound).
    if (in.onGround) {
        if (!onGroundPrev_) {
            const char* const* table = state_ == State::Boosting ? kWheelsLand : kHoverLand;
            for (int i = 1; i >= 0; --i)
                if (landTimer_ >= kLandThreshold[i]) { playEvent(table[i], cues, at); break; }
        }
        landTimer_ = 0.0f;
    } else {
        landTimer_ += dt;
    }
    onGroundPrev_ = in.onGround;
}

} // namespace game
