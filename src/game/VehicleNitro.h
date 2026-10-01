// Clean-room reconstruction — Optimus truck NITRO / RAM state (TransGame.TnTruckForm, script in
// TransGame.xxx). Systems owns the state, timer, cooldown, FX and audio; Gameplay owns what the
// nitro does to the vehicle (speed / steering scale, handling) and the final control mapping, and
// reads the authored scales below without Systems applying them.
//
// [CONF] compiled UnrealScript (float literals in the getters; modifiers = 1.0 on the CDO):
//   get_NitroDuration      = 3.0 s     * NitroDurationModifier
//   get_NitroSpeedScale    = 1.5       * NitroSpeedScaleModifier
//   get_NitroSteeringScale = 0.3       * NitroSteeringScaleModifier
//   get_TimeBetweenNitros  = 8.0 s     * TimeBetweenNitrosModifier
//   get_MaxRamMass         = 1000      * MaxRamMassModifier
//   state Driving (on wheels = while boosting): UpdateNitro starts the nitro on the DASH input
//   (_Dashing) when the cooldown allows; StartNitro plays RamFX + NitroForceFeedback (3 s) and sets
//   the nitro camera state; StopNitro stops RamFX and restores the scales to 1.0; leaving Driving
//   (Driving.EndState) calls StopNitro. In state Hovering, dash is a plain hover dash (DoDash).
// [MED] the 8 s cooldown is measured from nitro start (the bytecode compares it against the nitro
// timer; the exact reference point is not decoded).
#pragma once

namespace game {

class VehicleNitro {
public:
    static constexpr float kDuration      = 3.0f;   // [CONF] NitroDuration
    static constexpr float kSpeedScale    = 1.5f;   // [CONF] NitroSpeedScale    (Gameplay applies)
    static constexpr float kSteeringScale = 0.3f;   // [CONF] NitroSteeringScale (Gameplay applies)
    static constexpr float kCooldown      = 8.0f;   // [CONF] TimeBetweenNitros
    static constexpr float kMaxRamMass    = 1000.0f;// [CONF] MaxRamMass

    enum class Event { None, Started, Stopped };

    // `driving`: truck on its wheels (boost held, vehicle form, not transforming).
    // `dashPressed`: rising edge of the abstract Dash action this step.
    Event update(float dt, bool driving, bool dashPressed) {
        sinceStart_ += dt;
        if (active_) {
            remaining_ -= dt;
            if (!driving || remaining_ <= 0.0f) { active_ = false; remaining_ = 0.0f; return Event::Stopped; }
            return Event::None;
        }
        if (driving && dashPressed && sinceStart_ >= kCooldown) {
            active_ = true; remaining_ = kDuration; sinceStart_ = 0.0f;
            return Event::Started;
        }
        return Event::None;
    }

    // ---- read-only state for Gameplay ----
    bool nitroActive() const { return active_; }
    bool ramActive() const { return active_; }   // ram (collision damage / RamFX) runs with the nitro
    float timeRemaining() const { return remaining_; }
    float cooldownRemaining() const { return sinceStart_ >= kCooldown ? 0.0f : kCooldown - sinceStart_; }
    // Authored multipliers for Gameplay to apply while nitroActive() (1.0 otherwise).
    float speedScale() const { return active_ ? kSpeedScale : 1.0f; }
    float steeringScale() const { return active_ ? kSteeringScale : 1.0f; }

private:
    bool active_ = false;
    float remaining_ = 0.0f;
    float sinceStart_ = 1e9f;
};

} // namespace game
