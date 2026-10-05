#include "game/VehicleAudio.h"
#include "game/CharacterAudio.h"

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

// The vehicle sounds are the character profile's vehicle SoundEventSet (CharacterAudio; default: Optimus
// Veh_Optimus_Prime_SoundSet). Event -> slot [CONF HmVehicleAudioComponent / HmPlayerVehicleAudioComponent fields]:
//   Auto_Boost_Start BoostSound, Auto_Boost_Wheels BoostWheelsSound, Auto_Boost_End BoostStopSound, Auto_Boost_Loop
//   BoostSounds.BoostLoops, Auto_Engine_Gear_1_OnLoad / _OffLoad DriveSounds[*] (and ReverseSound), Auto_Jump_Loop
//   JumpRevLoops, Auto_Jump_Start AscendSound, Auto_Ram_Boost BoosterSound, Auto_Ram_Nitro NitroSound, Auto_Ram_Impact
//   RamSound, Auto_Tire_Squeal_Default DefaultTireSquealSound, Auto_Land_Hover_* HoverLandSound, Auto_Land_Wheels_*
//   BoostLandSound. Events mapped to None play nothing. [PARTIAL] the component tunables above are OptimusTruckForm's;
// other vehicles' HmPlayerVehicleAudioComponent values are not generated yet.
constexpr float kLandThreshold[2] = {0.15f, 2.0f};

} // namespace

void VehicleAudio::setProfile(const CharacterAudioProfile& p) {
    auto ev = [&](const char* e) { return p.vehicleCue(e); };
    n_.boostStart = ev("Auto_Boost_Start"); n_.boostWheels = ev("Auto_Boost_Wheels"); n_.boostEnd = ev("Auto_Boost_End");
    n_.boostLoop = ev("Auto_Boost_Loop"); n_.onLoad = ev("Auto_Engine_Gear_1_OnLoad"); n_.offLoad = ev("Auto_Engine_Gear_1_OffLoad");
    n_.jumpLoop = ev("Auto_Jump_Loop"); n_.ascend = ev("Auto_Jump_Start"); n_.booster = ev("Auto_Ram_Boost");
    n_.nitro = ev("Auto_Ram_Nitro"); n_.ram = ev("Auto_Ram_Impact"); n_.squeal = ev("Auto_Tire_Squeal_Default");
    n_.hoverLand[0] = ev("Auto_Land_Hover_Light"); n_.hoverLand[1] = ev("Auto_Land_Hover_Heavy");
    n_.wheelsLand[0] = ev("Auto_Land_Wheels_Light"); n_.wheelsLand[1] = ev("Auto_Land_Wheels_Heavy");
    named_ = true;
}

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
    if (!cue || !*cue) return false;                    // the set maps this event to None
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
    if (!cue || !*cue) return;
    cues.play(cue, at(), 0.0f, speed_);
}

void VehicleAudio::ram(SoundCues& cues, const EmitterFn& at) { ensureNames(); playEvent(n_.ram.c_str(), cues, at); }

// HmPlayerVehicleAudioComponentImpl states: EndState stops that state's loops, BeginState plays the next.
void VehicleAudio::gotoState(State s, SoundCues& cues, const EmitterFn& at) {
    if (s == state_) return;
    stopLooping(engine_, state_ == State::Boosting ? kBoostFadeOut : kEngineFadeOut, cues);
    state_ = s;
    switch (s) {
        case State::Boosting: playLooping(engine_, n_.boostLoop.c_str(), kBoostFadeIn, cues, at); break;
        case State::JumpReving: playLooping(engine_, n_.jumpLoop.c_str(), kEngineFadeIn, cues, at); break;
        case State::ForwardOnLoad: case State::ReverseOnLoad: playLooping(engine_, n_.onLoad.c_str(), kEngineFadeIn, cues, at); break;
        case State::ForwardOffLoad: case State::ReverseOffLoad: playLooping(engine_, n_.offLoad.c_str(), kEngineFadeIn, cues, at); break;
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

void VehicleAudio::ensureNames() { if (!named_) setProfile(CharacterAudio::defaultProfile()); }

void VehicleAudio::tick(float dt, const Input& in, SoundCues& cues, const EmitterFn& at) {
    ensureNames();
    if (in.entered && !entered_) attach();

    // Boost: Driving.BeginState -> PlayBoostSound; EndState (also on leaving the form) -> StopBoostSound.
    if (in.boosting && !boosting_ && in.entered) {
        playLooping(boost_, n_.boostStart.c_str(), kBoostFadeIn, cues, at);
        if (in.onGround) playLooping(boostWheels_, n_.boostWheels.c_str(), kBoostFadeIn, cues, at);
        boostWheelsTimer_ = 0.0f;
    } else if (boosting_ && (!in.boosting || !in.entered)) {
        stopLooping(boost_, kBoostFadeOut, cues);
        stopLooping(boostWheels_, kBoostFadeOut, cues);
        playEvent(n_.boostEnd.c_str(), cues, at);
    }
    boosting_ = in.boosting && in.entered;

    if (!in.entered) { if (entered_) detach(cues); return; }

    if (in.ascend) playEvent(n_.ascend.c_str(), cues, at);                          // PlayAscendSound
    if (in.booster) playLooping(booster_, n_.booster.c_str(), 0.0f, cues, at);     // PlayBoosterSound (no fade)
    if (in.nitro) playEvent(n_.nitro.c_str(), cues, at);                            // PlayNitroSound

    // HmVehicleAudioComponent.Tick: UpdateVehicleSpeed (15-sample mph average), tire squeal, boost wheels.
    speedHist_[histIdx_] = core::length(in.velocity) * kMph;
    histIdx_ = (histIdx_ + 1) % 15;
    speed_ = 0.0f;
    for (float h : speedHist_) speed_ += h;
    speed_ /= 15.0f;
    if (in.onGround && speed_ >= kSquealMinMph) {
        playLooping(squeal_, n_.squeal.c_str(), kSquealFade, cues, at);
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
            const std::string* table = state_ == State::Boosting ? n_.wheelsLand : n_.hoverLand;
            for (int i = 1; i >= 0; --i)
                if (landTimer_ >= kLandThreshold[i]) { playEvent(table[i].c_str(), cues, at); break; }
        }
        landTimer_ = 0.0f;
    } else {
        landTimer_ += dt;
    }
    onGroundPrev_ = in.onGround;
}

} // namespace game
