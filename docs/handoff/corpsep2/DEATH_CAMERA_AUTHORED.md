# Death camera: authored data (patch 2, part 3)

Gameplay, 2026-10-17. Read only from `AssetTools/manifests/authored.db`. `TnPlayerController.Dead` →
`CameraStrategyManager.Died` switches to **Death_STRATEGY** (robot) or **Death_Vehicle_STRATEGY**.

## Death_STRATEGY (robot), TransitionTime 1.25 s

| Behaviour | Values |
|---|---|
| TnFovCameraBehavior | FOV **85**, SmoothTime 0.4 |
| HmOffsetAnchorPointRelativeToActorCameraBehavior | Offset **(X 0, Y −300, Z 150) UU**, OffsetUsesCameraRotation **true**, BoneName **C_Spine01_Lumbar01_XB** |
| HmOrbitUpdateLocationRotationCameraBehavior | class default (MaxRoll 360) |
| TnShakeCameraBehavior | yaw 5 Hz × 3, pitch 4 Hz × 5 (one wave each); Intensity 1 (class default) |
| TnWiggler3CameraBehavior | translation amplitude [_, 0.4], frequency [_, 1.0]; rotation amplitude (0.3, 0.2, 0.1), frequency (1, 1, 1); TransitionTime 1; DampeningLimitRollDeg 0.4; VelocityAdjustPercent 1 |
| TnThirdPersoncollisionCameraBehavior | class default |

**Not present**, unlike OverTheShoulder:
- no TnLocationOffsetCameraBehavior, so **no orbit distance** (OTS has 800);
- no TnOrbitRotationCameraBehavior, so **no look input**;
- no screen-space offset and no orbit smoother.

## Death_Vehicle_STRATEGY, TransitionTime 1.0 s

| Behaviour | Values |
|---|---|
| TnFovCameraBehavior | FOV 85, SmoothTime 0.4 |
| HmOffsetAnchorPointRelativeToActorCameraBehavior | Offset (X −100, Y 0, Z 50), OffsetUsesCameraRotation true, BoneName **C_Reference_XR** |
| TnScreenSpaceOffsetByPitchCameraBehavior | default SmoothTime 0.3, with rows only for FineAim / Jumping (offsets (150, 200, 100 / 0 / 50) and (150, 200, 150 / 65 / 50)) |
| TnOrbitRotateToTargetCameraBehavior | settings object empty |
| TnFixedOrbitRotationCameraBehavior | **PitchDeg −30, bRelativeToActor true** |
| TnDutchRollCameraBehavior | RollDeg −1 |
| TnRecoilCameraBehavior, TnShakeCameraBehavior (as robot), HmOrbitUpdateLocationRotation, TnThirdPersoncollisionCameraBehavior (_4599) | — |

## Reading and the open question (for the RE lane)

- **Taken literally, the robot strategy places the camera at bone + (0, −300, 150) in camera-rotation axes:** 3 m to the **left** of
  the lumbar bone and 1.5 m up (UE3: X forward, Y right, Z up), with no pull-back.
- **With a frozen view rotation the body would sit 90° to the right, out of view.** So either the dead controller rotates the
  camera toward the corpse in code, or the axes mean something else.
- **Classes to read** (TransGame, defaults empty in authored.db):
  - `TnDeathCameraStrategyType`, `TnVehicleDeathCameraStrategyType`;
  - `TnDeathCameraLocationInterface`;
  - `TnOffsetAnchorPointDeathCameraBehavior`;
  - `TnPlayerController.Dead` (BeginState / GetPlayerViewPoint / UpdateRotation);
  - `CameraStrategyManager.Died`.
- **Questions:**
  1. What sets the view **rotation** while dead: frozen at death, turned toward the corpse (and over what time), or look input
     still applied?
  2. Is the anchor's Y −300 left in camera axes?
  3. Is the Death strategy kept until respawn or spectate, and when does spectating start (the design's "3 s")?
- **Gameplay implements it once answered:**
  - a death camera mode in PlayerController (anchor from the corpse bone through the body's pose globals);
  - FOV 85 / 0.4 s, the 1.25 s transition from the live camera;
  - the shake and wiggle spectra above, from a presentation-only clock;
  - third-person collision with the existing camera trace.

  The interim framing stays until then.
