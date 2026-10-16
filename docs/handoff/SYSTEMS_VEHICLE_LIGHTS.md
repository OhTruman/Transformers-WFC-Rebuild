# Vehicle headlights and dynamic emissive lights in the original (Rendering plan v3 items 3 and 7)

Source: AssetTools authored.db (read only; every exported object, class defaults and script types), via
`tools/systems/vehicle_lights.py` -> work output `vehicle_lights.json`. Systems, 2026-10-16.

## (3) Vehicle headlights: not authored

- No vehicle form owns a light component. There are 174 vehicle-form objects in 29 packages (car, truck, tank and jet
  forms, VEHDEFs, HmVehicle). Every Point / Spot / Directional / Sky light component outside the map lighting belongs to:
  - weapon meshes: TnWeaponMesh 78, TnWeaponMeshVehicle 8 (the muzzle lights, already shipped);
  - pawn classes: TnPawn and subclasses (CloakLight / DeathLight, TnRampLightManager);
  - the engine class defaults (4).
- The script types declare exactly five light-component properties: HmWeaponMesh.MuzzleFlashLight, TnPawn.CloakLight,
  TnPawn.DeathLight, TnRampLightManager.RampLightComponent and Engine.Light.LightComponent. No vehicle, form, pickup or
  interact type declares one.
- No object anywhere is named headlight / taillight. The only "light beam" objects are translucent cone meshes and their
  materials: map props (ENV_GLB_EnvEffects_p / ENV_IAC_Turret_p LightBeam_Cone_STAT, the KON Prison courtyard beams, the
  IAC Sentinel arena cones, PROP_EnergonSigns cones) and FX (Omega_LightBeam_MAT, FX_Pickups_p LightBeam_STAT).
- So the vehicles' lamps are emissive texture / material only; they light nothing. Real headlights would be a deliberate
  addition (Integration / user decision): no values to be faithful to.

## (7) Dynamic emissive objects with light components: none

- Pickups (TnWeaponPickupFactory 240, TnSeqAct_SpawnPickupRewards 870, the energon-colour particle parameters 508,
  dropped pickups), interact objects (TnInteract*, incl. TnInteractLightEnergon, which is an interact class, not a light),
  destructibles and screens own no light component. Their glow is material emission plus particles; pickups add the
  FX_Pickups LightBeam_STAT cone mesh.
- Map-placed dynamic light actors (Toggleable / Movable point, spot, directional and sky lights) exist in 71 campaign and
  cinematic packages. In the MP maps there is exactly one: MP_ESC_Remnant_ART_m PointLightToggleable_1918, already in the
  bake-lights audit (SYSTEMS_BAKE_LIGHTS_AUDIT.json, the Remnant extra light).
- The original's gameplay lights are therefore exactly what package 5 ships: weapon muzzle lights and death ramp lights. TnPawn.CloakLight exists but the original never turns it on (Gameplay RE, GAMEPLAY_CLOAKLIGHT_RE.md on agents/gameplay fb5e40c3: only the death-light service calls TriggerLight; the cloak manager stays at CurrentTime -1); not wired, by design.

Anything beyond these (headlights, pickup glow lights, screen lights) is an RT-era addition, not a restoration.
