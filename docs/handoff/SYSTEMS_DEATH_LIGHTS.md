# Death lights (Rendering plan v2 item 4, part 2)

Patch: `SYSTEMS_DEATH_LIGHTS.patch`, applied on top of `SYSTEMS_MUZZLE_LIGHTS.patch` (both against integration 2873a071, which carries Rendering's addTransientLight;
rebased to Rendering's rt2 item 4 head when Integration names it). Data: AssetTools authored.db (read only) via
`tools/systems/transient_lights.py` -> `src/game/DeathLights.inc` and `SYSTEMS_TRANSIENT_LIGHTS.json` (`ramp_lights`).

## The original

A death is lit by a **TnDeathModifierTriggerLight** in the chosen death entry of the pawn form's **TnDeathBlueprint**
(ROBODEF / VehicleForm `DeathBlueprint`). The light is the pawn's DeathLight component, driven by a TnRampLightManager
(RampLightProperties: RampInTime, LightTime, RampOutTime, Radius, Brightness, LightColor, FalloffExponent, Translation).

| blueprint | used by | death types with the light | light |
|---|---|---|---|
| TR_Deaths_p.SharedRobotDeaths | 53 robot forms (every MP robot) | Poke, Hovering, Airborne, SingleShot, RapidShot, Explosive | R 2048 cm, B 10, (230,230,255), ramp in 2.0 s, LightTime 2.0 s, falloff 10 |
| (same) | | none for RobotBooster, HeadShot, Martyrdom, TractorBeam, Energy, Melee, Forward / BackwardMomentum | - |
| TR_Deaths_p.SharedCarDeaths | 29 car / truck vehicle forms | MovingCar, StationaryCar | R 512 cm, B 25, (202,123,213), ramp in 0.2 s, LightTime 8.0 s, falloff 10 |
| TR_Deaths_p.SharedTankDeaths | 5 tank forms | MovingTank, StationaryTank | as cars (the same modifier object) |
| SharedJetDeaths / SharedVehicleDeaths | jets, Laserbeak | none | - |

The death type comes from TnDeathPicker. Each TnDamageType authors a `DamageDeathType` (e.g. IonBlaster -> RapidShot,
HeavyPistol -> SingleShot, rockets / grenades -> Explosive, Poke -> Poke). The picker can also choose from the victim's
motion (Hovering, Airborne, momentum), and a headshot gives HeadShot.

## What the rebuild does (APPROXIMATION, Integration decision 2026-10-15)

- **Vehicle form** death of a car / truck / tank: the vehicle light. Jets: none.
- **Robot form** death: the robot light when the killing damage type's authored DamageDeathType is a lit entry (34 damage
  types, listed in DeathLights.inc), and the kill is not a headshot. Melee, energy, flashbang, ragdoll types and damage
  types without a DamageDeathType (e.g. TnDamageTypePlaneRocket, TnDamageTypeTruckRocket, whose death type the picker
  takes from the victim's motion) have no light.
- Not picked: Hovering / Airborne from the victim's motion state.
- Ramp: brightness 0 -> 1 over RampInTime, held until LightTime from the start, then RampOutTime (0 = off). Whether the
  original's LightTime counts from the start or after the ramp-in is code-side (the script bytecode is not decompiled).
- The light sits at the victim's actor location at the death (Translation 0), keyed per victim (a new death restarts it).
- One hook: World::onPawnDeath (Gameplay ee43bef1, once per death on every path). The victim (light key), headshot and vehicle form type come from the newest death record whose victim pawn matches the chassis and position.

Presentation only: SIMHASH unaffected. WFC_MUZZLELOG=1 logs every death's choice ("DEATH LIGHT player N chassis form
(damage type): robot light / vehicle light / none").

Test (headless, null renderer, no window, muted; Release + PGO, WFC_SEED=7 FLOWSEED=7 AUTODETECT=0) on 2873a071 + both patches: WFC_BOTTEST 44/44 (89 deaths: 32 robot lights, 24 vehicle lights, the rest unlit), WFC_ENGAGETEST 3/3 (8 muzzle lights). Same counts on 24c89769. Robot deaths by TankShell / HomingRocket
light; vehicle tank deaths light; jets and PlaneRocket / TruckRocket robot deaths stay unlit.

## Backlog

A real death-type picker (TnDeathPicker: drives the death animation, gibs and lights together) is Gameplay's item in the
campaign phase after package 5: `CAMPAIGN_BACKLOG.md`.
