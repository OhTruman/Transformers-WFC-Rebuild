# Weapon camera kick and shake (RE, 2026-10-18): WEAPON_PRESENTATION_AUDIT gap 1

Labels: CONFIRMED (C) = TransGame / HM_Engine script bytecode + authored data; HIGH (H) = inference, not traced.
There are **no CameraShake / CameraAnim assets** in this path. Both effects are WFC's own camera behaviours driven by two structs in
each weapon's **MultiplayerData** (TnWeaponData): `RecoilCameraParams` and `CameraShakeParams`.

## 1. When they fire (C)

`TnPlayerPawn.PlayWeaponFireEffects` → `TnPlayerController.OnWeaponFired(weapon)`:
```
if (IsLocalPlayerController() && weapon != none && TnCamera != none) {
    TnCamera.OnWeaponFired(weapon);                               // sets FiredWeapon for this frame → recoil behaviour
    StartCameraShake(weapon.GetWeaponFiredCameraShakeParams());   // → TnCamera.StartShake
}
```
- **Owner only:** local player controllers only. Other players' shots never move your camera.
- **Per shot:** PlayWeaponFireEffects runs per fired shot (H: per FireAmmunition, so per round in a burst).
- `TnCamera.UpdateViewTarget` clears `FiredWeapon` and `bShakeStarted` every frame. Several shots in one frame count once.
- `StartShake` ignores Intensity == 0.
- Other shake sources use the same entry point: victim damage (`VictimCameraShakeParams[segments]`), attacker
  (`AttackerCameraShakeParams`), gib, hit-wall, vehicle-form blueprints, and anim notifies (`TnAnimNotify_CameraShake`, radius
  falloff `1 − d/R`). **The last StartShake in a frame wins; there is no additive stacking** (one params slot).

## 2. Camera kick: `TnRecoilCameraBehavior` (C)

It is in every player strategy. Robot (`CAM_Strategies_p.OverTheShoulder_STRATEGY`) order:

`Fov → OffsetAnchor → LocationOffset → OrbitRotation (stick) → TargetStickiness → ScreenSpaceOffsetByPitch → RotateToTarget →
OrbitSmoother → OrbitUpdateLocationRotation → **Recoil** → Wiggler3 → **Shake** → ThirdPersonCollision`

The vehicle / turret / plane strategies have the same Shake + Recoil pair after UpdateLocationRotation. FirstPerson, Spectate,
GuidedMissile, Death and the plain Car / Truck drive strategies have **no** recoil behaviour; of those, Car / Truck / Death /
Spectate still have the Shake behaviour, while FirstPerson and GuidedMissile have neither.

```
Update(dt, Result):
  if (camera.GetFiredWeapon() != none) {                 // a shot this frame
      TimeSinceLastShot = 0
      Params = weapon.Data.RecoilCameraParams
      if (owner has TnBuffReduceRecoil) Params.{FovAdjustment, PitchAdjustmentDeg, RecoilTime} *= 0.35   // buff CDO
      Result.OrbitRotation.Pitch += PitchAdjustmentDeg * 182.044          // degrees → rotator units, ONCE per shot
  }
  TimeSinceLastShot += dt
  Result.Fov -= FovAdjustment * Lerp()
Lerp() = RecoilTime == 0 ? 0 : 1 − smoothstep(0, RecoilTime, clamp(TimeSinceLastShot − RecoilDelay, 0, RecoilTime))
```

**Pitch kick is permanent aim climb, not a recovering punch.**
- `OrbitRotation` persists in the strategy result from frame to frame. The stick behaviour only adds input and clamps to the pitch
  range; nothing pulls the kick back.
- So each shot raises the camera, and with it the aim, by PitchAdjustmentDeg. The player recovers it by stick.
- It is added after UpdateLocationRotation, so it shows from the next frame, through `HmOrbitSmoother` (eased, not a one-frame
  snap; smoother SmoothTime = its class default, not extracted).
- Positive = up.
- Aim follows the camera: the controller takes the camera view rotation (C for vehicle form `PlayerMove: SetRotation(CameraRotation)`;
  H for robot form).

**FOV kick is transient.**
- `TnFovCameraBehavior` rewrites `Result.Fov` (smoothed target FOV) at the start of every frame, so the recoil offset doesn't
  accumulate.
- Shape: **full offset from the shot until RecoilDelay, then a smoothstep return over RecoilTime.**
- `Fov −= FovAdjustment`: a **negative FovAdjustment widens the FOV** (punch-out); a positive one zooms in.
- A new shot restarts the timer (no stacking: the offset never exceeds one FovAdjustment).

## 3. Shake: `TnShakeCameraBehavior` + `TnCameraShaker` (C)

```
on StartShake this frame:  Intensity = P.Intensity
                           Shaker.Shake(YawSpectrum, PitchSpectrum, P.ShakeFalloffDuration)   // falloff timer = duration
                           Shaker.Roll(P.Roll, P.RollFalloffDuration)
every frame:  f = max(0, falloff − dt) / ShakeFalloffDuration                  // linear 1 → 0
              yaw   = Σ A·sin(2π·F·t) (Yaw waves)   · f       [degrees]
              pitch = Σ A·sin(2π·F·t) (Pitch waves) · f
              rf = RollFalloff / RollFalloffDuration;  roll = Roll · rf²        // quadratic fade, no oscillation
              Result.RotationAdjustment += (yaw, pitch, roll) · 182.044 · Intensity
```
- **Spectra (C, all 60 placed shake behaviours, every strategy):** Yaw 3.0° at 5 Hz, Pitch 5.0° at 4 Hz (one wave each).
- The wave clock `t` is the generator's running time. It is **not reset per shot**, so the phase continues; only the falloff
  restarts.
- **View only:** `RotationAdjustment` is zeroed at the start of every behaviour pass, so it never affects aim and never
  accumulates.
- Roll is a one-sided tilt of `Roll` degrees fading quadratically (sign as authored, all positive).
- A new shake **replaces** the running one: Intensity and falloff are reset to the new values.

## 4. Per-weapon values (C, MultiplayerData; absent field = 0)

| Weapon | PitchAdj ° | FovAdj | RecoilDelay s | RecoilTime s | Shake Intensity | Shake falloff s | Roll ° | Roll falloff s |
|---|---|---|---|---|---|---|---|---|
| PlasmaCannon | 0 | −1.0 | 0.15 | 0.65 | 0.8 | 0.4 | 0.8 | 0.4 |
| SniperRifle | 1.0 | 0.5 | 0.05 | 0.7 | 0.1 | 0.2 | 0.3 | 0.2 |
| AssaultRifle | 0.05 | 0 | 0.25 | 0.05 | 0.17 | 0.06 | 0.15 | 0.1 |
| IonBlaster | 0.1 | 0.085 | 0.25 | 0.05 | 0.17 | 0.06 | 0.15 | 0.1 |
| HeavyPistol | 1.5 | −1.0 | 0.05 | 0.7 | 0.6 | 0.2 | 0.3 | 0.2 |
| BurstRifle | 0 | 0 | 0.25 | 0.05 | 0.15 | 0.15 | 0.15 | 0.15 |
| HeavyMG | 0.2 | 0 | 0.25 | 0.05 | 0.15 | 0.1 | 0.1 | 0.1 |
| GrenadeLauncher | 0.5 | −1.0 | 0.25 | 0.6 | 0.5 | 0.3 | 0.3 | 0.3 |
| Bazooka | 1.5 | −2.0 | 0.25 | 0.6 | 0.45 | 0.4 | 0.45 | 0.4 |
| RepairRay | 0 | 0.4 | 0.05 | 1.0 | 0.025 | 0.1 | 0.5 | 0.05 |
| EMPShotgun | 4.0 | −1.0 | 0.25 | 0.4 | 0.2 | 0.5 | 0.5 | 0.4 |
| Shotgun | 2.0 | −1.0 | 0.25 | 0.4 | 0.2 | 0.5 | 0.5 | 0.4 |
| RocketLauncher | 0.25 | 1.0 | 0.25 | 0.6 | 0.6 | 0.5 | 0.4 | 0.4 |
| RocketVehicle | 0 | 0 | 0.05 | 0.35 | 0.2 | 0.3 | 0.2 | 0.2 |
| AssaultRifleVehicle | 0 | 0 | 0.25 | 0.05 | 0.1 | 0.1 | 0.1 | 0.1 |
| TankCannon | 0 | −4.0 | 0.2 | 0.8 | 0.38 | 0.79 | 0.65 | 0.86 |
| HomingRocketVehicle | 0 | 1.0 | 0.25 | 0.6 | 0.3 | 0.2 | 0.2 | 0.15 |
| AssaultRiflePlane / PlaneMachineGun | 0 | 0 | 0.25 | 0.05 | 0.1 | 0.1 | 0.1 | 0.1 |
| RocketPlane | 0 | 0 | 0.05 | 0.35 | 0.15 | 0.25 | 0.2 | 0.15 |
| Grenades (Flak / Kamikaze / Heal / FlashBang), melee | — | — | — | — | — | — | — | — |

- Source: notes/data/weapon_presentation_audit.json (`camera_recoil`, `camera_shake`), tool tools/weapon_presentation_audit.py.
- Campaign PlayerData differs only for HeavyPistol pitch (SP 2.0) and Bazooka pitch (SP 0.75): see the audit.
- Example peak shake, Bazooka: 0.45 × 5° = 2.25° pitch and 1.35° yaw, fading over 0.4 s, plus a 0.45° roll.
- Example climb, IonBlaster: 0.1° up per shot.

## 5. Implementation notes for Gameplay

1. Hook on the **local owner's** fire event (per shot). Skip other pawns' shots.
2. **Aim climb:** add PitchAdjustmentDeg to the player's look pitch (respecting the pitch clamp), applied through the same
   smoothing as stick look. Never auto-recover.
3. **FOV:** after the base FOV is computed each frame, subtract FovAdjustment × (1 − smoothstep over RecoilTime after
   RecoilDelay).
4. **Shake:** one active shake slot; on each shot restart falloff with the new Intensity / durations and keep the wave clock
   running. Add `(3° sin 2π·5t, 5° sin 2π·4t) × falloff × Intensity` (yaw, pitch) and `Roll × rollFalloff²` to the view
   rotation only, never to aim.
5. **ReduceRecoil buff:** scale FovAdjustment, PitchAdjustmentDeg and RecoilTime by 0.35. Delay and shake are unaffected.
6. **Strategies without recoil** (first person, spectate, guided missile, death, plain drive): no kick. Shake still plays in drive /
   death / spectate, but not in first person or guided missile.
