# Death lights (Rendering plan v2 item 4, part 2): data read, not wired

Source: AssetTools authored.db (read only), tools/systems/transient_lights.py -> SYSTEMS_TRANSIENT_LIGHTS.json (`ramp_lights`).

The original lights a death through a **TnDeathModifierTriggerLight** in the chosen death entry of the pawn form's
**TnDeathBlueprint** (ROBODEF / VehicleForm `DeathBlueprint`). The light is the pawn's DeathLight component driven by a
TnRampLightManager (RampLightProperties: RampInTime, LightTime, RampOutTime, Radius, Brightness, LightColor,
FalloffExponent, Translation).

| blueprint | used by | death types with the light | light |
|---|---|---|---|
| TR_Deaths_p.SharedRobotDeaths | 53 robot forms (every MP robot) | Poke, Hovering, Airborne, SingleShot, RapidShot, Explosive | R 2048 cm, B 10, (230,230,255), ramp in 2.0 s, LightTime 2.0 s, falloff 10 (default) |
| (same) | | none for RobotBooster, HeadShot, Martyrdom, TractorBeam, Energy, Melee, Forward / BackwardMomentum | - |
| TR_Deaths_p.SharedCarDeaths | 29 car / truck vehicle forms | MovingCar, StationaryCar | R 512 cm, B 25, (202,123,213), ramp in 0.2 s, LightTime 8.0 s, falloff 10 |
| TR_Deaths_p.SharedTankDeaths | 5 tank forms | MovingTank, StationaryTank | as cars (same modifier object) |
| SharedJetDeaths / SharedVehicleDeaths | jets, Laserbeak | none | - |

Blocked: the rebuild has no death-type selection (TnDeathPicker: which TnDeathType the damage / motion picks). The
robot light depends on that choice: a headshot, melee, energy or momentum death has no light. Wiring needs either:
- Gameplay's death-type picker (preferred: the same choice drives the death animation and gibs); or
- an approximation: vehicle-form car / tank death -> the car light; robot death not by melee / headshot / energy -> the
  robot light.

Open read: whether LightTime counts from the start or after the ramp-in (code-side, the script bytecode is not decompiled).
Suggested: ramp in over RampInTime, hold until LightTime from the start, then RampOutTime (0 = off).
