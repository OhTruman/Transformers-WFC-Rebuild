# Death camera (robot form): what turns the view, offset axes, Dead duration

WFC RE lane, 2026-10-17, for Gameplay patch 2 (corpse / death).

Sources:
- script decompiles in `work/script/decomp`: HmCamera, HmCameraStrategy, HmOffsetAnchorPoint*, HmOrbitUpdateLocationRotation,
  TnLocationOffset, TnScreenSpaceOffsetByPitch, TnCameraStrategyManager, TnPlayerController and Engine.PlayerController;
- authored.db for CAM_Strategies_p.

Everything here is script plus data. No native code is involved.

## 1. What sets the view rotation while dead: nothing (CONFIRMED)

**The camera keeps the last gameplay orbit rotation.**

1. TnPlayerController.Dead.BeginState calls `_CameraStrategyManager.Died()`, which runs `GotoState('WatchingDeath')`.
   WatchingDeath.BeginState does `ClearPCS(); set_CameraStrategy(TnDeathCameraStrategyType)`.
   All 26 HmCameraStrategySets map that type to `CAM_Strategies_p.Death_STRATEGY`.
2. **Death_STRATEGY behaviours, in order:**
   1. TnFov: 85°, smooth 0.4 s.
   2. HmOffsetAnchorPointRelativeToActor: Offset (0, −300, 150), OffsetUsesCameraRotation true, **BoneName `C_Spine01_Lumbar01_XB`**
      (note the `_XB` suffix).
   3. HmOrbitUpdateLocationRotation.
   4. TnShake.
   5. TnWiggler3.
   6. TnThirdPersoncollision.

   No behaviour in this stack writes `OrbitRotation` or `OrbitLocationOffset`, and none reads stick input (no `CI.` reads in any of
   them). There is no look-at (HmOrbitUpdate), no orbit input (TnOrbitRotation) and no killer target.
3. **The state is shared across strategies.**
   - HmCamera has a single `CameraState`. `SetActiveCameraStrategy` hands the same `CameraState` to the new strategy's Activate.
   - `HmCameraStrategy.UpdateBehaviorsPass` starts each frame from `CameraState.CameraStrategyResult` and writes back
     Location, Rotation, OrbitRotation and OrbitLocationOffset.
   - So the death strategy **inherits OrbitRotation and OrbitLocationOffset from the strategy that was active at death**: normally
     OverTheShoulder_STRATEGY (all 26 sets), or Downed_Robot if the player was downed first. Both stay frozen from then on.
4. **HmOrbitUpdateLocationRotation.Update:**
   - `rot = OrbitRotation + OrbitRotationAdjustment`, with roll clamped to ±MaxRoll (default 360).
   - `Location = AnchorPoint + (OrbitLocationOffset >> rot)`, `Rotation = rot`.
   - The rotation is therefore the frozen orbit rotation from the moment of death.
   - On top of it come TnShake (only while a camera shake is playing, e.g. the GibbedCameraShake from Dying.Gib) and the Wiggler3
     idle noise (rotation amplitude 0.3 / 0.2 / 0.1, 1 Hz; translation Y 0.4, 1 Hz).
   - PlayerController.Dead.PlayerMove still applies aTurn / aLookUp to `PC.Rotation`, but no death behaviour reads `PC.Rotation`, so
     the stick has no visible effect.
5. **The offsets cancel, so the body is centred (CONFIRMED from data).**
   - OverTheShoulder writes the orbit offset each frame:
     - TnLocationOffset: `OrbitLocationOffset = (−DefaultOrbitDistance 800, HorizontalOffset 0, VerticalOffset 0)`.
     - Then TnScreenSpaceOffsetByPitch adds `+= (150, 300, z(pitch))`, where z = 150 / −35 / 150 at pitch −75 / 0 / +75 (curve over
       fraction of [−75, 75], smooth 0.3 s).
     - The stored value at death is therefore about **(−650, +300, z)**, assuming the player is not in FineAim / sniper PCS. ClearPCS
       runs at WatchingDeath, but the stored offset is not recomputed.
   - The death anchor is `GetBoneLocation(C_Spine01_Lumbar01_XB) + ((0, −300, 150) >> CamRotation)`. CamRotation = the incoming
     `Result.Rotation`, which is last frame's orbit rotation (adjustments are kept separately).
   - Sum: `camera = lumbar + rot · (−650, 0, 150 + z)`. At level pitch that is **650 UU behind the lumbar bone, about 115 UU above it,
     horizontally centred, looking along the frozen orbit yaw / pitch.**
   - **The −300 Y removes the over-the-shoulder +300 Y shoulder shift. It is not a free 3 m sideways placement.**
   - **Do not implement the offset by itself.** Read it as an anchor that the inherited orbit offset is added to. Without that offset
     you get the "90° off-view" result.
6. **Follow and blend.**
   - The anchor follows the lumbar bone every frame (death anim, then ragdoll), so the camera translates with the body but never
     rotates toward it.
   - If AnchorActor becomes none (the pawn is destroyed after Gib sets LifeSpan 1), UpdateAnchorPoint returns early and the anchor
     freezes in place.
   - TnThirdPersoncollision pulls the camera in against geometry (`_QuickMoveSpeed` 2000, `_SlowMoveSpeed` 500).
   - **Strategy switch blend:** SetActiveCameraStrategy stores the delta between the old and new POV (location, rotation, FOV).
     HmCamera.UpdateTransition lerps that delta to 0 over `TransitionTime` **1.25 s** on the camera transition curve. Death_STRATEGY
     has no TransitionOverrides.

## 2. Is Y −300 left? Yes (CONFIRMED)

- UE3 camera axes are X forward, Y right, Z up. HmOffsetAnchorPointBase.UpdateAnchorPoint does
  `AnchorPoint = origin + ((Offset − CoM) >> CamRotation)`.
- `_CenterOfMassOffset` is 0 unless the pawn is in Physics 10 (PHYS_RigidBody); in that case it is the RB centre of mass relative to
  the actor.
- InvertY flips Y. It is false here.
- So −300 means 300 UU to the camera's left. This cancels the OTS shoulder shift of +300 to the right (see §1.5).

## 3. How long Dead holds (CONFIRMED)

1. TnPlayerController.Dead.BeginState calls super first: Engine Dead.BeginState sets bFrozen = true, runs FindGoodView (this only
   sets PC.Rotation, which is unused by the strategy camera), then `SetTimer(MinRespawnDelay)`.
2. Then, in MP (`GRI.IsMultiplayerGame()`), it sets `SetTimer(MinRespawnDelay, 'StartSpectating')`.
3. **MinRespawnDelay = 3.0 s** (TnPlayerController CDO; Engine default 1.0; no ini override).
4. **MP:** after 3.0 s, StartSpectating runs `GotoState('PlayerSpectating')`. That calls UIController.OnUIEvent(4) and
   CameraStrategyManager.StartSpectating.
   - **The spectate target starts as `_LastPawnWhoKilledMe`**, through SpectateTargetChooser, using the SpectatingCameraStrategy
     asset. **The killer appears only here, after the 3 s death camera.**
   - Respawn then follows the game's respawn flow (TnRespawnHelperWave; see MP note addendum 8).
5. **Fire input during Dead (Engine):**
   - StartFire, Use and Jump are ignored while bFrozen, and bFrozen clears after MinRespawnDelay.
   - Once bFrozen is clear, StartFire calls ServerRestartPlayer. TnPC.Dead.ServerRestartPlayer either restarts from the active
     checkpoint (SP, when PlayerCanRestart is false) or calls TnGame.RequestRespawn.
   - In MP the 'StartSpectating' timer fires at the same moment, so in practice Dead lasts exactly 3.0 s.

## Vehicle death (for reference)

TnCameraStrategyManager.VehicleForm.Died → WatchingVehicleDeath → `Death_Vehicle_STRATEGY` (transition 1.0 s).

- **Anchor:** `C_Reference_XR` + (−100, 0, 50) in camera axes.
- **TnFixedOrbitRotation:** bRelativeToActor, so `OrbitRotation = vehicle Actor.Rotation + (pitch −30°, yaw 0)`. This one does
  rotate with the wreck.
- **Other behaviours:** TnOrbitRotateToTarget, TnScreenSpaceOffsetByPitch (default curve empty; FineAim-only values), TnRecoil,
  TnShake, TnDutchRoll −1°.
- The orbit distance is again inherited from the driving strategy.

## Addendum: gibbed camera shake (CONFIRMED, script)

**Trigger** (`TnPawn.Dying.Gib`):
- For each local TnPlayerController whose **`PC.Pawn` is not none and VSize(PC.Pawn − gibbed pawn) < 1000**, the game calls
  `PC.StartCameraShake(GibbedCameraShake)`.
- **All-or-nothing:** there is no distance scaling.
- The victim's own PC is in `Dead` with Pawn = none, so it **does not feel its own gib**.

**Plumbing:**
1. `TnCamera.StartShake` is ignored if Intensity == 0. Otherwise it sets bShakeStarted for one frame (cleared in UpdateViewTarget).
2. `TnShakeCameraBehavior.Update`, on start, stores Intensity and calls `Shaker.Shake(Yaw/PitchSpectrum, ShakeFalloffDuration)` and
   `Shaker.Roll(Roll, RollFalloffDuration)`.
3. Every frame: `RotationAdjustment += Shaker.Update(dt) × Intensity`.

**`TnCameraShaker.UpdateRotation`:**
- `f = max(0, f − dt) / ShakeFalloffDuration`, a **linear** falloff over a duration in seconds.
- Yaw = `Σ A·sin(2π·F·t)` from the **YawSpectrum generator** × 182.044 × f. Pitch is the same with PitchSpectrum.
- **Amplitude is in degrees.**
- Roll = `Roll(deg) × 182.044 × r²`, with `r` = the linear roll falloff. **Roll is a one-sided offset with no oscillation**, fading
  **quadratically** over RollFalloffDuration.
- The whole rotator is then × Intensity.
- **Phase:** the generator's `t` accumulates from when it was created and is never reset by Shake, so the phase is arbitrary. There
  is no random phase and no per-wave phase.
- A new shake restarts the falloffs and replaces Intensity; shakes don't add up.
- A strategy change (`Activate` → `Shaker.Reset`) cuts a running shake.

**Gibbed values** (TnPawn CDO: Intensity 0.55, Roll 0.75, falloffs 0.89 s) with the spectra (Yaw 5 Hz × 3°, Pitch 4 Hz × 5°):
- **yaw ±1.65°** and **pitch ±2.75°** peak, fading linearly to 0 over 0.89 s;
- **roll +0.41°** (0.75 × 0.55), fading as (1 − t/0.89)².
