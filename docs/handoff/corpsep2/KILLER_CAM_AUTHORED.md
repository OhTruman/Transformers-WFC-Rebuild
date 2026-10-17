# Killer cam (PlayerSpectating after MinRespawnDelay): authored data and open questions

Gameplay, 2026-10-17. Read only from authored.db. Known (RE DEATH_CAMERA_RE):
- `TnPC.Dead.BeginState` sets `SetTimer(MinRespawnDelay 3.0, 'StartSpectating')`.
- After exactly 3.0 s the controller enters PlayerSpectating: UI event 4, then `CameraStrategyManager.StartSpectating` with the initial
  target `_LastPawnWhoKilledMe`.
- Fire (unfrozen) → `ServerRestartPlayer` → RequestRespawn.

## SpectateOverTheShoulder_STRATEGY (TransitionTime 1.5)

| Behaviour | Values |
|---|---|
| TnFov | default 80 / SmoothTime 0.4; FineAim rows 30 (HeavyPistol, BurstRifle, SniperRifle…) |
| HmOffsetAnchorPointRelativeToActor | (0, 0, 200) |
| TnLocationOffset | DefaultOrbitDistance **1600**; FineAim rows 100 |
| TnOrbitRotation | default look 50 / 25 (ramp 2.5, pitch −75..75); FineAim rows slower |
| TnOrbitRotateToTarget | FocusingOnPointOfInterest: stick, speed 2; FineAim: snap, speed 10 |
| TnScreenSpaceOffsetByPitch | (150, 0, 150 / −35 / 150) over pitch −75..75, SmoothTime 0.3 (FineAim X 350) |
| TnShake | yaw 5 Hz × 3, pitch 4 Hz × 5 |
| TnSpectateCameraInitialView | (no properties) |
| HmOrbitSmoother, HmOrbitUpdateLocationRotation, TnThirdPersoncollision | defaults |

## SpectateActor_STRATEGY (TransitionTime 0.75)

| Behaviour | Values |
|---|---|
| TnFov | default 60 / 0.4; FineAim rows 35 |
| HmOffsetAnchorPointRelativeToActor | (0, 0, 300) |
| TnLocationOffset | DefaultOrbitDistance **800**; FineAim rows 100 |
| TnOrbitRotation | default 50 / 25; FineAim rows 12 / 6… |
| TnOrbitRotateToTarget | as above |
| HmConeCollision | HalfConeAngle 8°, BackupTime 0.5, bAllowImmediateMoveBack, SafeBaseLocationOffset (0, 0, 150), TraceExtentsScale (0, 0, 0.1) |
| TnShake | as above |
| HmOrbitSmoother, HmOrbitUpdateLocationRotation | defaults |

Both strategies read the look input (TnOrbitRotation), unlike Death_STRATEGY.

## Questions for the RE lane

1. Which strategy does `StartSpectating(_LastPawnWhoKilledMe)` select, SpectateOverTheShoulder or SpectateActor? What selects the
   other (a non-pawn target, the killer's form, a game mode)?
2. What does `TnSpectateCameraInitialViewCameraBehavior` set: the initial orbit rotation (the killer's view, or the direction from the
   corpse to the killer?) and the initial offset?
3. **The target over time:**
   - Can the dead player cycle targets (which buttons: NextWeapon / Fire / AltFire?), and in what order (team mates only, or
     everyone)?
   - What happens when the killer dies, leaves, or is the world (suicide / KillZ, `_LastPawnWhoKilledMe` none)?
4. **Fire to respawn:**
   - Is RequestRespawn gated by the wave timer (WaveRespawnTime) while spectating?
   - Does the camera cut or blend back to the new pawn's OverTheShoulder?
5. Is the PCS (FineAim rows) evaluated against the target pawn's state, e.g. the killer fine-aiming a sniper rifle → FOV 30 /
   distance 100?
