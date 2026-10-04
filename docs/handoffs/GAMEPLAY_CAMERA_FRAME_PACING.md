# Handoff to Gameplay: character "interlacing" in integration milestone 05 = camera frame pacing

**Human report:**
- the frame rate is smooth, but the character jitters and its pieces seem to separate;
- the animation looks out of phase;
- it affects both robot and vehicle;
- it is new in milestone 05.

## Cause (VISUALLY VERIFIED / measured)
`PlayerController::tickCameraCollision` runs inside the fixed 60 Hz `World::tick`. It writes `camLoc_` as a world
position, computed from the view yaw and pitch at that tick (Gameplay pass 20, the RE camera-collision rewrite).

`updateCamera()` then runs once per **rendered** frame:
- it takes the stored `camLoc_`;
- it takes the **current** `viewYaw_` / `viewPitch_`, which `handleInput` changes every rendered frame.

When the display runs faster than 60 Hz, most rendered frames have no simulation step. The camera orbit position then
advances in 60 Hz steps while its rotation advances every frame, so the pawn swims across the screen. At 60 Hz there is
exactly one step per frame and nothing is visible. That is why headless lockstep tests pass.

In milestone 04, `cameraPos()` was computed every frame, so the camera stayed consistent.

## Measurement
- Pawn projected screen x, second difference per frame, `WFC_CAMLOG=1`.
- Scripted walk and turn, `WFC_RENDERHZ=<display Hz>` (deterministic frame time).

| build | 60 Hz | 144 Hz | 240 Hz |
|---|---|---|---|
| integration/milestone-05 | 0.00000 | **0.0095 mean, 0.014 max** (≈ 7 px per frame) | — |
| agents/rendering (M04-style camera) | 0.00002 | 0.00000 | — |
| milestone-05 + this patch, default camera | 0.00000 | 0.00000 | 0.00009 |
| milestone-05 + this patch, `WFC_CAMRE=1` | 0.00001 | 0.00001 | 0.00000 |

## Fix (owner: Gameplay, `src/game/PlayerController.{h,cpp}`): `gameplay_camera_frame_pacing.patch`
The obstruction behaviour itself is unchanged. It still runs on the simulation tick and keeps Gameplay's smoothing state.
What changes is where its result is kept: **relative to the camera frame**, instead of as a world position.
- **Default model:** the pull-in fraction along focus → desired.
- **RE model:** the existing target-space offset `camOld_` (UE X forward, Y right, Z up). This matches the RE
  description of the behaviour: smoothing in target space.

`cameraPos()` then rebuilds the world position every rendered frame from:
- the current view rotation;
- the current pawn location.

`camLoc_` is still written for the existing diagnostics (`cameraObstructed`, `WFC_CAMTEST`).

The patch was verified on a `git archive` export of integration/milestone-05 (`work/m08/int05` in agents/rendering).
Diagnostics now on agents/rendering, to copy if useful:
- `WFC_RENDERHZ=<hz>`: deterministic display rate;
- `WFC_SHOTEVERY=<dir>,<from>,<to>`: per-frame captures;
- `WFC_CAMLOG=1`: per-frame pawn screen position.

## Not the cause (checked)
- **Renderer:** no skinning or pose change between M04 and M05. Dynamic meshes are not deferred by the M06
  translucency queue.
- **Pawn yaw and weapon attachment:** both are updated on the simulation tick and stay in phase.
- **Window / vsync:** no change.
- **Frame loop:** the fixed-step loop is identical to M04.
