#include "game/VehicleAudio.h"
#include "game/CharacterAudio.h"

namespace game {
namespace {

constexpr float kMph = 2.23694f;          // ComputeSpeedMPH: |Velocity| (UU/s) x 0.0223694

// Every sound slot, list and tunable comes from the chassis's own HmPlayerVehicleAudioComponent (CharacterAudio
// vehicleComponent: the object over its archetype chain and the class defaults) [CONF data]; the event -> cue map is
// the chassis's vehicle SoundEventSet; an unset slot or an event the set maps to None plays nothing [CONF script].
// Optimus (OptimusTruckForm.HmPlayerVehicleAudioComponent_6670) gives the previous hand-entered values exactly:
// BoostFadeOut 0.15, BoostWheelsGroundCheckDelay 0.27, TireSquealSpeedMin 20, TireSquealCrossfadeTime 0.5,
// EngineFadeOut 0.2, UseJumpRev, land thresholds 0.15 / 2.0; his gear one-shot event is unmapped in his set.

std::string cueOf(const CharacterAudioProfile& p, const std::string& ev) { return ev.empty() ? std::string() : p.vehicleCue(ev); }

void resolve(const CharacterAudioProfile& p, std::vector<std::string>& v) {
    for (std::string& e : v) e = cueOf(p, e);
}

} // namespace

void VehicleAudio::setProfile(const CharacterAudioProfile& p) {
    c_ = p.vehicleComponent;
    if (!c_.valid) c_ = CharacterAudio::defaultProfile().vehicleComponent;   // (every MP chassis has one)
    // Events -> this chassis's cues, once.
    for (auto* g : {&c_.reverse}) { resolve(p, g->onLoops); resolve(p, g->onOneshots); resolve(p, g->offLoops); resolve(p, g->offOneshots); }
    for (auto& g : c_.drive) { resolve(p, g.onLoops); resolve(p, g.onOneshots); resolve(p, g.offLoops); resolve(p, g.offOneshots); }
    resolve(p, c_.boostLoops); resolve(p, c_.boostOneshots); resolve(p, c_.jumpLoops); resolve(p, c_.jumpOneshots);
    for (auto& l : c_.hoverLand) l.event = cueOf(p, l.event);
    for (auto& l : c_.boostLand) l.event = cueOf(p, l.event);
    for (std::string* s : {&c_.boost, &c_.boostWheels, &c_.boostStop, &c_.ascend, &c_.ram, &c_.booster, &c_.nitro, &c_.squeal,
                           &c_.speed, &c_.ascendStop, &c_.descend, &c_.descendStop, &c_.roll, &c_.oneEighty, &c_.enter, &c_.exit,
                           &c_.tread})
        *s = cueOf(p, *s);
    speedHist_.assign((size_t)std::max(1, c_.speedHistory), 0.0f);
    histIdx_ = 0;
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

void VehicleAudio::playEvent(const std::string& cue, SoundCues& cues, const EmitterFn& at) {
    if (cue.empty()) return;
    cues.play(cue.c_str(), at(), 0.0f, speed_);
}

void VehicleAudio::ram(SoundCues& cues, const EmitterFn& at) { ensureNames(); playEvent(c_.ram, cues, at); }

// Impl.ComputeGear: the first DriveSounds entry whose MaxSpeed >= the vehicle speed, else the last; false if none.
int VehicleAudio::computeGear() const {
    if (c_.drive.empty()) return -1;
    for (size_t i = 0; i < c_.drive.size(); ++i)
        if (speed_ <= c_.drive[i].maxSpeed) return (int)i;
    return (int)c_.drive.size() - 1;
}

// HmPlayerVehicleAudioComponentImpl states. EndState stops that state's loops (one-shots run out); BeginState plays
// the new state's loops (Boosting: BoostFadeInTime, others EngineFadeInTime), then - if EngineOneshotSpazTimer <= 0 -
// its one-shots (PlayLoopingSound, fade 0), each resetting the timer to EngineOneshotSpazTime. Drive / reverse
// one-shots are skipped when coming from Boosting or JumpReving. The drive gear is chosen at BeginState only
// (GotoState to the current state does not re-enter it). [CONF script]
void VehicleAudio::gotoState(State s, SoundCues& cues, const EmitterFn& at) {
    if (s == state_) return;
    const float fadeOut = state_ == State::Boosting ? c_.boostFadeOut : c_.engineFadeOut;
    for (Loop& l : engine_) stopLooping(l, fadeOut, cues);
    const State prev = state_;
    state_ = s;
    const std::vector<std::string>* loops = nullptr;
    const std::vector<std::string>* shots = nullptr;
    float fadeIn = c_.engineFadeIn;
    const bool fromBoostOrJump = prev == State::Boosting || prev == State::JumpReving;
    switch (s) {
        case State::Boosting: loops = &c_.boostLoops; shots = &c_.boostOneshots; fadeIn = c_.boostFadeIn; break;
        case State::JumpReving: loops = &c_.jumpLoops; shots = &c_.jumpOneshots; break;
        case State::ReverseOnLoad: loops = &c_.reverse.onLoops; shots = fromBoostOrJump ? nullptr : &c_.reverse.onOneshots; break;
        case State::ReverseOffLoad: loops = &c_.reverse.offLoops; shots = fromBoostOrJump ? nullptr : &c_.reverse.offOneshots; break;
        case State::ForwardOnLoad: case State::ForwardOffLoad: {
            const int g = computeGear();
            if (g < 0) break;
            const VehicleAudioComponentData::Gear& gear = c_.drive[(size_t)g];
            const bool on = s == State::ForwardOnLoad;
            loops = on ? &gear.onLoops : &gear.offLoops;
            shots = fromBoostOrJump ? nullptr : (on ? &gear.onOneshots : &gear.offOneshots);
            break;
        }
        default: break;
    }
    if (loops) {
        if (engine_.size() < loops->size()) engine_.resize(loops->size());
        for (size_t i = 0; i < loops->size(); ++i) playLooping(engine_[i], (*loops)[i].c_str(), fadeIn, cues, at);
    }
    if (shots && spazTimer_ <= 0.0f) {
        if (oneshots_.size() < shots->size()) oneshots_.resize(shots->size());
        for (size_t i = 0; i < shots->size(); ++i) {
            spazTimer_ = c_.oneshotSpazTime;
            playLooping(oneshots_[i], (*shots)[i].c_str(), 0.0f, cues, at);
        }
    }
}

// HmVehicleAudioComponent.Attached: speed history cleared, the SpeedSound loop starts (fade 0); the form's
// OnBeginPlay plays EnterSound. Impl.Attached resets the land / jump-rev timers.
void VehicleAudio::attach(SoundCues& cues, const EmitterFn& at) {
    entered_ = true;
    for (float& h : speedHist_) h = 0.0f;
    histIdx_ = 0; speed_ = 0.0f;
    onGroundPrev_ = false; jumpRevTimer_ = 0.0f; landTimer_ = 0.0f;   // Impl.Attached
    state_ = State::None;
    playLooping(speedLoop_, c_.speed.c_str(), 0.0f, cues, at);
    playEvent(c_.enter, cues, at);
}

void VehicleAudio::detach(SoundCues& cues) {
    gotoState(State::None, cues, nullptr);                 // Impl.Detached: StopEngineSounds
    stopLooping(speedLoop_, 0.0f, cues);                   // Detached: speed / tread / squeal loops (fade 0)
    stopLooping(tread_, 0.0f, cues);
    stopLooping(squeal_, 0.0f, cues);
    stopLooping(boost_, c_.boostFadeOut, cues);
    stopLooping(boostWheels_, c_.boostFadeOut, cues);
    // The booster component is the pawn's (Owner.CreateAudioComponent): the form's state end stops it (StopHoverFx); a
    // dash booster one-shot runs out. Nothing the form owned may outlive it.
    stopLooping(booster_, 0.0f, cues);
    entered_ = false;
}

void VehicleAudio::stopAll(SoundCues& cues) {
    if (entered_) detach(cues);
    stopLooping(booster_, 0.0f, cues);
    for (Loop& l : engine_) stopLooping(l, 0.0f, cues);
    for (Loop& l : oneshots_) stopLooping(l, 0.0f, cues);
    stopLooping(boost_, 0.0f, cues); stopLooping(boostWheels_, 0.0f, cues);
    stopLooping(speedLoop_, 0.0f, cues); stopLooping(tread_, 0.0f, cues); stopLooping(squeal_, 0.0f, cues);
    entered_ = false; boosting_ = false; state_ = State::None;
}

int VehicleAudio::liveLoops() const {
    int n = 0;
    for (const Loop* l : {&boost_, &boostWheels_, &booster_, &squeal_, &speedLoop_, &tread_}) n += l->id >= 0 ? 1 : 0;
    for (const Loop& l : engine_) n += l.id >= 0 ? 1 : 0;
    return n;
}

void VehicleAudio::ensureNames() { if (!named_) setProfile(CharacterAudio::defaultProfile()); }

void VehicleAudio::tick(float dt, const Input& in, SoundCues& cues, const EmitterFn& at) {
    ensureNames();
    if (in.entered && !entered_) attach(cues, at);

    // Boost: Driving.BeginState -> PlayBoostSound; EndState (also on leaving the form) -> StopBoostSound.
    if (in.boosting && !boosting_ && in.entered) {
        playLooping(boost_, c_.boost.c_str(), c_.boostFadeIn, cues, at);
        if (in.onGround) playLooping(boostWheels_, c_.boostWheels.c_str(), c_.boostFadeIn, cues, at);
        boostWheelsTimer_ = 0.0f;
    } else if (boosting_ && (!in.boosting || !in.entered)) {
        stopLooping(boost_, c_.boostFadeOut, cues);
        stopLooping(boostWheels_, c_.boostFadeOut, cues);
        playEvent(c_.boostStop, cues, at);
    }
    boosting_ = in.boosting && in.entered;

    if (!in.entered) {
        if (entered_) { playEvent(c_.exit, cues, at); detach(cues); }   // the form's OnEndPlay: PlayExitSound
        return;
    }

    if (in.ascend) playEvent(c_.ascend, cues, at);                                  // PlayAscendSound
    if (in.booster) playLooping(booster_, c_.booster.c_str(), 0.0f, cues, at);     // PlayBoosterSound (fade 0)
    if (in.boosterStop) stopLooping(booster_, 0.0f, cues);                          // StopBoosterSound (fade 0)
    if (in.boosterAmount >= 0.0f && booster_.id >= 0) cues.update(booster_.id, {0, 0, 0}, in.boosterAmount);   // BoosterParameter
    if (in.ascendStop) playEvent(c_.ascendStop, cues, at);
    if (in.descend) playEvent(c_.descend, cues, at);
    if (in.descendStop) playEvent(c_.descendStop, cues, at);
    if (in.roll) playEvent(c_.roll, cues, at);
    if (in.oneEighty) playEvent(c_.oneEighty, cues, at);
    if (in.nitro) playEvent(c_.nitro, cues, at);                                    // PlayNitroSound

    // HmVehicleAudioComponent.Tick: UpdateVehicleSpeed (VehicleSpeedHistoryLength-sample mph average), tire squeal,
    // boost wheels.
    speedHist_[(size_t)histIdx_] = core::length(in.velocity) * kMph;
    histIdx_ = (histIdx_ + 1) % (int)speedHist_.size();
    speed_ = 0.0f;
    for (float h : speedHist_) speed_ += h;
    speed_ /= (float)speedHist_.size();
    // UpdateTireTreadSound: on the ground the tread loop (TireTreadCrossfadeTime) with SpeedSoundParameter = speed.
    if (in.onGround) {
        playLooping(tread_, c_.tread.c_str(), c_.treadFade, cues, at);
        if (tread_.id >= 0) cues.update(tread_.id, {0, 0, 0}, speed_);
    } else {
        stopLooping(tread_, c_.treadFade, cues);
    }
    if (speedLoop_.id >= 0) cues.update(speedLoop_.id, {0, 0, 0}, speed_);          // UpdateSpeedSound
    if (in.onGround && speed_ >= c_.squealMinMph) {
        playLooping(squeal_, c_.squeal.c_str(), c_.squealFade, cues, at);
        cues.update(squeal_.id, {0, 0, 0}, in.wheelSlip);
    } else {
        stopLooping(squeal_, c_.squealFade, cues);
    }
    if (boosting_) {
        boostWheelsTimer_ += dt;
        if (boostWheelsTimer_ >= c_.boostWheelsDelay && !in.onGround) stopLooping(boostWheels_, c_.boostFadeOut, cues);
    }

    // Impl.UpdateEngineSound.
    int dir = 0;                                                      // GetMovementDirection
    if (speed_ > 1.0f) dir = core::dot(in.velocity, in.forward) >= 0.0f ? 1 : 2;
    spazTimer_ = std::max(0.0f, spazTimer_ - dt);                     // UpdateEngineOneshotSpazTimer
    bool jumpRev = false;                                             // GetIsJumpReving (only with UseJumpRev)
    if (c_.useJumpRev) {
        if (in.onGround) jumpRevTimer_ = 0.0f;
        else { jumpRevTimer_ += dt; jumpRev = jumpRevTimer_ >= c_.jumpRevTime; }
    }
    State s;
    if (boosting_) s = State::Boosting;
    else if (jumpRev) s = State::JumpReving;
    else if (dir == 2) s = in.loadState == 2 ? State::ReverseOnLoad : State::ReverseOffLoad;
    else if (dir == 1) s = in.loadState == 1 ? State::ForwardOnLoad : State::ForwardOffLoad;
    else s = State::ForwardOffLoad;
    gotoState(s, cues, at);
    for (const Loop& l : engine_)                                     // UpdateEngineSoundParameters
        if (l.id >= 0) cues.update(l.id, {0, 0, 0}, speed_);
    if (boost_.id >= 0) cues.update(boost_.id, {0, 0, 0}, speed_);

    // Impl.UpdateLandSound: on touchdown the highest TimeInAirThreshold reached (Boosting: BoostLandSound).
    if (in.onGround) {
        if (!onGroundPrev_) {
            const auto& table = state_ == State::Boosting ? c_.boostLand : c_.hoverLand;
            for (int i = (int)table.size() - 1; i >= 0; --i)
                if (landTimer_ >= table[(size_t)i].t) { playEvent(table[(size_t)i].event, cues, at); break; }
        }
        landTimer_ = 0.0f;
    } else {
        landTimer_ += dt;
    }
    onGroundPrev_ = in.onGround;
}

} // namespace game
