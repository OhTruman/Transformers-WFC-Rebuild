// Clean-room reconstruction — which HmVehicleAudioComponent calls each vehicle FORM class makes, from Gameplay's
// vehicle state (Systems M08d). Gameplay owns the state (form, Hovering / Driving / Flying, boost, dash, roll, nitro,
// jump, ascend / descend); this only turns its per-step values into the component inputs (VehicleAudio::Input) and a
// per-step event set for presentation (Rendering's vehicle FX), keeping just the previous step's flags for edges.
// Per form [CONF decompiled TransGame form classes]:
//   car / truck (TnCarForm, TnTruckForm)  Hovering: UpdateSounds EngineLoadState from the stick, WheelSlipRatio 0,
//       IsOnGround = hover sim; DoDash -> PlayBoosterSound; UpdateFx -> set_BoosterAmount (largest thruster
//       contribution); UpdateJumping -> PlayAscendSound. Driving (boost): PlayBoostFx / StopBoostFx -> Play /
//       StopBoostSound; UpdateSounds IsOnGround + WheelSlipRatio = SlipAngle (EngineLoadState untouched); UpdateJumping
//       and (car) UpdateRolling -> PlayAscendSound. Truck: StartNitro -> PlayNitroSound, ram -> PlayRamSound.
//   tank (TnTankForm)  UpdateSounds: IsOnGround, EngineLoadState from the stick; tank-sim boost -> Play / StopBoostSound;
//       UpdateJumping -> PlayAscendSound; the 180 special move -> PlayOneEightySound.
//   jet (TnPlaneForm)  Hovering: UpdateSounds EngineLoadState; PlayHoverFx / StopHoverFx (state begin / end) -> Play /
//       StopBoosterSound; UpdateFx -> set_BoosterAmount; UpdateDashing: Ascending / Descending held -> Play / Stop
//       AscendSound, Play / Stop DescendSound; UpdateRolling -> PlayRollSound. Flying: PlayBoostFx / StopBoostFx ->
//       Play / StopBoostSound; UpdateRolling -> PlayRollSound.
// Leaving the form (transform, death, unload) ends the state: StopHoverFx / StopBoostFx run, then the component
// detaches (VehicleAudio stops every loop it owns).
#pragma once
#include <string>
#include "core/Math.h"
#include "game/VehicleAudio.h"

namespace game {

struct VehicleFormSignals {
    enum class Kind { Car, Truck, Tank, Jet };
    Kind kind = Kind::Truck;
    bool vehicle = false;          // in vehicle form with vehicle control (not mid-transform, alive)
    bool onGround = false;
    bool boostState = false;       // car / truck: Driving; jet: Flying; tank: tank-sim boosting
    float stickForward = 0.0f;     // StrafeForwardBack
    core::Vec3 velocity{0, 0, 0};  // m/s
    core::Vec3 forward{0, 0, 1};
    bool tookOff = false;          // a jump started this step (UpdateJumping)
    bool dashing = false;          // hover dash active (car / truck DoDash)
    bool rolling = false;          // car Driving roll / jet roll active
    bool nitroStarted = false;     // truck StartNitro this step
    bool ascendHeld = false, descendHeld = false;   // jet Hover Up / Down
    bool special180 = false;       // tank 180 started this step (Gameplay: not modelled yet)
    float wheelSlip = -1.0f;       // Driving SlipAngle if known (< 0: unknown -> 0)
    float thrusterAmount = -1.0f;  // Hovering UpdateFx largest thruster contribution if known (< 0: not sent)

    static Kind kindFromForm(const std::string& f) {
        return f == "car" ? Kind::Car : f == "tank" ? Kind::Tank : f == "jet" || f == "plane" ? Kind::Jet : Kind::Truck;
    }
};

// What happened this step, for presentation (vehicle FX) and diagnostics.
struct VehicleFormEvents {
    bool enter = false, exit = false;          // OnBeginPlay / OnEndPlay of the form
    bool boostOn = false, boostOff = false;    // Driving / Flying / tank boost begin / end (BoostFx)
    bool hoverOn = false, hoverOff = false;    // Hovering begin / end (HoverFX)
    bool jump = false, roll = false, dash = false, nitro = false, special180 = false;
    bool ascendOn = false, ascendOff = false, descendOn = false, descendOff = false;
    bool hovering = false, boosting = false;   // current state (for thruster / afterburner FX)
};

class VehicleFormAudio {
public:
    VehicleAudio::Input translate(const VehicleFormSignals& s, VehicleFormEvents* evOut = nullptr) {
        using K = VehicleFormSignals::Kind;
        VehicleFormEvents ev;
        VehicleAudio::Input in;
        in.entered = s.vehicle;
        in.onGround = s.onGround;
        in.velocity = s.velocity;
        in.forward = s.forward;
        const bool boost = s.vehicle && s.boostState;
        const bool hover = s.vehicle && !s.boostState;
        ev.enter = s.vehicle && !prevVehicle_;
        ev.exit = !s.vehicle && prevVehicle_;
        ev.boostOn = boost && !prevBoost_;
        ev.boostOff = !boost && prevBoost_;
        ev.hoverOn = hover && !prevHover_;
        ev.hoverOff = !hover && prevHover_;
        ev.hovering = hover; ev.boosting = boost;
        in.boosting = boost;
        // EngineLoadState: Hovering.UpdateSounds (all forms) and the tank's UpdateSounds set it from the stick; Driving /
        // Flying leave it as it was.
        if (hover || (s.vehicle && s.kind == K::Tank)) loadState_ = s.stickForward > 0.01f ? 1 : (s.stickForward < -0.01f ? 2 : 0);
        if (!s.vehicle) loadState_ = 0;
        in.loadState = loadState_;
        in.wheelSlip = (boost && (s.kind == K::Car || s.kind == K::Truck) && s.wheelSlip > 0.0f) ? s.wheelSlip : 0.0f;
        if (s.vehicle) {
            ev.jump = s.tookOff && s.kind != K::Jet;
            const bool rollStart = s.rolling && !prevRolling_;
            const bool dashStart = s.dashing && !prevDashing_;
            switch (s.kind) {
                case K::Car: case K::Truck:
                    in.ascend = ev.jump || (boost && s.kind == K::Car && rollStart);   // UpdateJumping / Driving.UpdateRolling
                    in.booster = hover && dashStart;                                    // Hovering.DoDash
                    in.nitro = s.kind == K::Truck && s.nitroStarted;
                    if (hover && s.thrusterAmount >= 0.0f) in.boosterAmount = s.thrusterAmount;
                    ev.roll = boost && rollStart; ev.dash = hover && dashStart; ev.nitro = in.nitro;
                    break;
                case K::Tank:
                    in.ascend = ev.jump;
                    in.oneEighty = s.special180;
                    ev.special180 = s.special180;
                    break;
                case K::Jet:
                    in.booster = ev.hoverOn;                                            // Hovering: PlayHoverFx
                    in.boosterStop = ev.hoverOff;                                       //           StopHoverFx
                    if (hover && s.thrusterAmount >= 0.0f) in.boosterAmount = s.thrusterAmount;
                    in.roll = rollStart;
                    ev.roll = rollStart;
                    if (hover) {                                                        // Hovering.UpdateDashing
                        in.ascend = s.ascendHeld && !prevAscend_;
                        in.ascendStop = !s.ascendHeld && prevAscend_;
                        in.descend = s.descendHeld && !prevDescend_;
                        in.descendStop = !s.descendHeld && prevDescend_;
                    }
                    break;
            }
        }
        // Leaving Hovering (boost, transform, death): its EndState runs StopHoverFx; ascend / descend end with it.
        if (s.kind == K::Jet && ev.hoverOff) in.boosterStop = true;
        if (s.kind == K::Jet && !hover) {
            if (prevAscend_) in.ascendStop = true;
            if (prevDescend_) in.descendStop = true;
        }
        ev.ascendOn = in.ascend && s.kind == K::Jet; ev.ascendOff = in.ascendStop;
        ev.descendOn = in.descend; ev.descendOff = in.descendStop;
        prevVehicle_ = s.vehicle; prevBoost_ = boost; prevHover_ = hover;
        prevRolling_ = s.vehicle && s.rolling; prevDashing_ = s.vehicle && s.dashing;
        prevAscend_ = hover && s.kind == K::Jet && s.ascendHeld;
        prevDescend_ = hover && s.kind == K::Jet && s.descendHeld;
        if (evOut) *evOut = ev;
        return in;
    }
    void reset() { *this = VehicleFormAudio{}; }

private:
    bool prevVehicle_ = false, prevBoost_ = false, prevHover_ = false, prevRolling_ = false, prevDashing_ = false;
    bool prevAscend_ = false, prevDescend_ = false;
    int loadState_ = 0;
};

} // namespace game
