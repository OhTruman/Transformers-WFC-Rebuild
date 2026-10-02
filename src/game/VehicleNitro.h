// Clean-room reconstruction — Optimus truck NITRO / RAM state (TransGame.TnTruckForm, script in
// TransGame.xxx). In the integrated build Gameplay owns the nitro timer, cooldown, input and what
// the nitro does to the vehicle (speed / steering scale, handling); this class follows Gameplay's
// state for the presentation (RamFX, audio) and the ram-hit registry.
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
// [CONF, native RE] the 8 s cooldown starts on activation; the nitro ends immediately when normal
// Boost is released (leaving Driving); the Dash action (RB / abstract Dash) only starts a nitro while
// Driving with Boost already active — while Hovering the same action is the hover dash (Gameplay).
// No ram animation is authored: RamFX + the nitro/ram cues are the whole presentation.
// Ram collision is active only during the nitro and hits each target at most once per nitro.
#pragma once
#include <vector>

namespace game {

class VehicleNitro {
public:
    static constexpr float kDuration      = 3.0f;   // [CONF] NitroDuration
    static constexpr float kSpeedScale    = 1.5f;   // [CONF] NitroSpeedScale    (Gameplay applies)
    static constexpr float kSteeringScale = 0.3f;   // [CONF] NitroSteeringScale (Gameplay applies)
    static constexpr float kCooldown      = 8.0f;   // [CONF] TimeBetweenNitros
    static constexpr float kMaxRamMass    = 1000.0f;// [CONF] MaxRamMass

    // [CONF] ram damage / momentum, TR_Optimus_VEHDEF_p.OptimusTruckForm (TnTruckFormBlueprint) x the
    // TnTruckForm modifiers (all 1.0). Class defaults (Default__TnTruckFormBlueprint) in comments.
    // For Gameplay's ram collision; Systems does not apply damage or impulses.
    static constexpr float kRamDamageToPlayerRobots   = 175.0f;   // default 50
    static constexpr float kRamDamageToAiRobots       = 300.0f;   // default 100
    static constexpr float kRamDamageToPlayerVehicles = 175.0f;   // default 50
    static constexpr float kRamDamageToAiVehicles     = 300.0f;   // default 100
    static constexpr float kExtraRamZVelocityUU       = 7000.0f;  // default 1000 (UU/s upward on the rammed target)

    enum class Event { None, Started, Stopped };

    // [integration/milestone-02] The nitro timer, cooldown, Dash input and Driving gate are owned
    // by Gameplay's movement code (Character::VehicleState nitroRemain / nitroCooldown, same CONF
    // values). The presentation follows that state each step: `gameplayActive` =
    // vehicleState().nitroRemain > 0. Edges start/stop RamFX and the nitro cues.
    Event follow(bool gameplayActive) {
        if (gameplayActive && !active_) {
            active_ = true;
            rammed_.clear();                               // one hit per target per nitro
            return Event::Started;
        }
        if (!gameplayActive && active_) { active_ = false; return Event::Stopped; }
        return Event::None;
    }

    // ---- read-only state ----
    bool nitroActive() const { return active_; }
    bool ramActive() const { return active_; }   // ram (collision damage / RamFX) runs with the nitro
    // Authored multipliers for Gameplay to apply while nitroActive() (1.0 otherwise).
    float speedScale() const { return active_ ? kSpeedScale : 1.0f; }
    float steeringScale() const { return active_ ? kSteeringScale : 1.0f; }

    // Ram collision gate for Gameplay: true only during the nitro and only the first time `target`
    // is hit in this nitro (AttemptToRam's _RammedPawns).
    bool registerRamHit(const void* target) {
        if (!active_ || !target) return false;
        for (const void* t : rammed_) if (t == target) return false;
        rammed_.push_back(target);
        return true;
    }

private:
    bool active_ = false;
    std::vector<const void*> rammed_;
};

} // namespace game
