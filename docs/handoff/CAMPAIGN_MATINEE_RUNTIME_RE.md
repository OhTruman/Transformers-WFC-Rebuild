# Matinee runtime semantics in WFC (for CAMPAIGN_MATINEE_SCHEMA.md)

WFC RE lane, 2026-10-18. Integration copies this to `docs/handoff`.

Sources:
- Engine / Core enums and class property lists from the cooked script packages;
- script decompiles: PlayerController, HmPlayerController, TnPlayerController, TnPawn, HmPawn, SkeletalMeshActorMAT;
- headless Ghidra on the native SeqAct_Interp / LocalPlayer / director code. Queries are in `work/q/mt*.txt`.

**Labels:**
- **CONFIRMED** = read in WFC's own code or data.
- **HIGH** = WFC's class schema and enums are exactly stock UE3 for that feature, so stock behaviour applies, but the C++ was not
  traced.

## 0. Schema check (CONFIRMED)

**SeqAct_Interp:**
- inputs **Play, Reverse, Stop, Pause, Change Dir**;
- outputs **Completed, Aborted only**. There is **no "Reversed" output** in this build;
- variable `Data`;
- properties: PlayRate (1.0), Position, ForceStartPosition, bIsPlaying, bPaused, bReversePlayback, bLooping, bRewindOnPlay,
  bNoResetOnRewind, bRewindIfAlreadyPlaying, bInterpForPathBuilding, bForceStartPos, bClientSideOnly, bSkipUpdateIfNotVisible,
  bIsSkippable, bIsBeingEdited, bIsCanceled;
- the bools sit at +0xD4, masks from 0x80000000 down in that order: bIsSkippable 0x00100000, bClientSideOnly 0x00400000,
  bIsCanceled 0x00040000.

**Enums:**
- **`EInterpCurveMode` = {CIM_Linear, CIM_CurveAuto, CIM_Constant, CIM_CurveUser, CIM_CurveBreak}.** There is **no
  CIM_CurveAutoClamped** in WFC; the schema doc lists it. Mode bytes are 0–4 in that order.
- EInterpTrackMoveFrame {IMF_World, IMF_RelativeToInitial}.
- EInterpTrackMoveRotMode {IMR_Keyframed, IMR_LookAtGroup}.
- ETrackToggleAction {ETTA_Off, ETTA_On, ETTA_Toggle}. There is **no ETTA_Trigger**; the schema doc lists it.
- Camera EViewTargetBlendFunction {Linear, Cubic, EaseIn, EaseOut, EaseInOut}.

## 1. SeqAct_Interp play control

### 1.1 Termination and outputs (CONFIRMED native, `USeqAct_Interp::Stop`-style terminate, 0x82C1EF60)

When the interp terminates:
- **`Aborted`** fires if `Position < 0.0001` **or bIsCanceled**. This covers a reverse play that reaches the start, and any skip.
- **else `Completed`** fires if `Position > InterpLength − 0.0001`, i.e. it reached the end.
- **else no output** (stopped mid-way).

Each fires only if the link is not disabled. Then every group instance is terminated (TermTrackInst on each track, actors told
interpolation finished) and the group list is cleared.

- The `Stop()` script native (0x82C7D3A0) clears bIsPlaying and bPaused.
- `Cancel()` (0x82C7D418) sets bIsCanceled and sends an internal notify (0x33).

### 1.2 Inputs (HIGH stock UE3)

- **Play:** play forward from the current Position.
  - If `bRewindOnPlay`, rewind to 0 first. `bNoResetOnRewind` keeps the actors where they are instead of resetting them to the
    saved initial state.
  - If already playing: rewinds only with `bRewindIfAlreadyPlaying`.
  - `bForceStartPos`: start at ForceStartPosition.
- **Reverse:** play backwards. Reaching 0 terminates and fires **Aborted** (per 1.1).
- **Pause:** toggle pause.
- **Change Dir:** flip direction without stopping.
- **Stop:** terminate now; the output depends on Position (1.1).

**Other flags:**
- **bLooping:** at the end, wrap to 0 and keep playing; never terminates by itself.
- **bClientSideOnly:** runs locally on each client; also one of the skip conditions below.
- **bSkipUpdateIfNotVisible:** skips the per-frame update while none of the bound actors was recently rendered.

### 1.3 Skip (CONFIRMED native: `ULocalPlayer::CancelMatinee(CancelTime)`, 0x82F95ED0; called by `TnPlayerController.SkipMatinee` with 0)

For every SeqAct_Interp that meets **all** of these:
- **bIsPlaying**;
- **bIsSkippable**;
- **bClientSideOnly, or a game condition** (H: not a dedicated server);
- one of its **group instances is driven by this player's controller** (the director / player group);
- `Position < InterpLength − 0.1` and `Position ≥ CancelTime`;

the game does:
1. **set bIsCanceled**;
2. **`SetPosition(InterpLength − 0.1, jump)`**, which evaluates every track at that time as a jump, updates the bound actors and
   ticks them;
3. then the remaining **0.1 s plays normally** and the interp terminates. **Because bIsCanceled is set, it fires `Aborted`, not
   `Completed`.**

**Actors end at the end-of-matinee pose**, i.e. the jump evaluation at Length − 0.1, then normal playback to the end.

**Per-track skip behaviour (HIGH stock):**
- Event-track keys inside the jumped span fire only if `bFireEventsWhenJumpingForwards`.
- Sound keys skipped over don't start. A sound already playing keeps playing if `bContinueSoundOnMatineeSkip` (CDO default
  **true**).
- **Director:** while canceled, cuts use **blend time 0** (CONFIRMED in the director update, §2).

## 2. Director track and the player's view

### 2.1 Cuts (CONFIRMED native: director update 0x82C2BEC8)

Only for a group-instance actor that is a **local** PlayerController. Each update:
1. Find the cut active at the current time: camera-group actor and TransitionTime.
2. If the cut's camera actor is none, or the PC's own pawn, go back to the player's own view.
3. Else, if it differs from the current view target, **`SetViewTarget(cameraActor, BlendTime = cut TransitionTime)`**.
4. **While bIsCanceled, the blend time is forced to 0.**
5. On director termination (0x82C02670), the PC's previous view target is restored. H: the call carries a 0.1 s parameter.

### 2.2 Camera (HIGH stock + CDO)

- The view uses the **CameraActor's location, rotation and FOVAngle**. FOV is usually animated by a FloatProp `FOVAngle` track on
  the camera group. Defaults: FOVAngle 90, AspectRatio 1.7778.
- **Letterbox** only if the CameraActor has `bConstrainAspectRatio`, which defaults to false (not authored on the CDO).
- `CamOverridePostProcess` applies when `bCamOverridePostProcess` is set.
- In WFC, `HmCamera.UpdateViewTarget` hands over to the stock camera when `IsMatineeCameraActive() && CameraActor(ViewTarget)`, so
  the High Moon camera strategies don't run during cuts.

### 2.3 What hides the HUD, the player and input (CONFIRMED script)

**Not the director track.** It is **`SeqAct_ToggleCinematicMode`** (inputs Enable / Disable / Toggle; CDO defaults all true:
bDisableMovement, bDisableTurning, bHidePlayer, bDisableInput, bHideHUD, bPauseable, bDisableIncidentalDialog).

`PlayerController.OnToggleCinematicMode` → `SetCinematicMode`:
- **bHidePlayer:** Pawn.SetHidden.
- **Movement / Turning:** IgnoreMoveInput / IgnoreLookInput.
- **TnPlayerController adds:**
  - on entering with bDisableInput: ClearInputs + StopMovement;
  - **if the pawn is downed, it is revived** (`GetRevive(none, false)`);
  - client side, the HUD is hidden via `bHudOffInCinematicMode` and UI event 13 is sent when the HUD returns.
- **HmPlayerController:** bDisableInput → `SetCinematicButtonsDisabled` + interactive state.
- Pauseable via `SetCinematicPauseable`.
- Incidental dialog is muted while in cinematic mode.

## 3. Movement track

**Interpolation** (HIGH stock FInterpCurve::Eval; the C++ was not traced):
- Before the first key → the first value. After the last → the last.
- Between keys i and i+1, by **key i's InterpMode**:
  - **Constant** → value of key i;
  - **Linear** → lerp;
  - **CurveAuto / CurveUser / CurveBreak** → cubic Hermite:
    `CubicInterp(P_i, LeaveTan_i·Δ, P_{i+1}, ArriveTan_{i+1}·Δ, α)`, with `Δ = t_{i+1} − t_i` and `α = (t − t_i)/Δ`.
- **Use the stored tangents.** The editor computes them for CurveAuto, and the cooked data carries them. Runtime evaluation does
  not recompute them.
- CurveUser has equal in/out tangents; CurveBreak has separate ones.
- Rotation is an Euler curve in degrees, interpolated per component, unless `bUseQuatInterpolation`, which slerps between the
  keys' quaternions.

**MoveFrame** (HIGH):
- `IMF_World`: keys are world transforms.
- `IMF_RelativeToInitial`: keys are relative to the actor's transform captured at InitTrackInst (`UInterpTrackInstMove`, native
  0x82CB6E18). `world = Initial ⊗ key`.

**LookupTrack** (HIGH): a key carrying a group name takes that group's actor position at that time instead of the stored value.

**RotMode** (HIGH): `IMR_LookAtGroup` overrides rotation to face the named group's actor.

**Restore:** at termination, unless the actor keeps its final pose, actors restore their saved transform (SavedActorTransforms)
when rewound with `!bNoResetOnRewind`.

## 4. AnimControl and FaceFX on pawns

### 4.1 AnimControl (CONFIRMED script for the pawn path; HIGH for the native slot code)

1. **BeginAnimControl(AnimSets):**
   - `TnPawn` zeroes Acceleration and Velocity, sets `_MatineeAnimationPlaying`, and hands the **vehicle mesh (vehicle form) or the
     robot mesh** to `_MatineeAnimator.BeginAnimControl`.
   - `HmPawn` appends any missing AnimSets to the mesh and removes them at FinishAnimControl.
   - **TnPlayerController** enters state **`AnimControl`**: ProcessMove, fire, weapon switch and abilities are blocked; StopFire;
     velocity zeroed.
2. **SetAnimPosition(SlotName, ChannelIndex, AnimSeqName, Position, bFireNotifies, bLooping):**
   - forwarded to the SkeletalMeshMatineeAnimator (native);
   - the named **AnimNodeSlot** channel plays `AnimSeqName` at `Position`;
   - the track computes `Position = startOffset + (t − keyTime)·rate`, wrapping if `looping` and mirrored if `reverse`.
3. **SetAnimWeights(SlotInfos):** the per-slot channel weights from the track's **weight curve**. This is the only blend in or
   out; keys have no per-key blend times in this build.
4. **FinishAnimControl:** clears the flag and returns the slot to the normal tree. The PC leaves AnimControl back to its form state.

### 4.2 FaceFX (HIGH)

- The InterpTrackFaceFX `FaceFXAnimSets` are mounted at InitTrackInst (native 0x82C97C40).
- At each key the matinee calls `Actor.PlayActorFaceFXAnim(AnimSet, GroupName, SeqName)` → `HmPawn` → `Mesh.PlayFaceFXAnim`.
  The bone blend is in FACEFX_BONE_BLEND_RE.md.
- Termination calls `StopActorFaceFXAnim`.

## 5. Corrections to CAMPAIGN_MATINEE_SCHEMA.md

1. **No `CIM_CurveAutoClamped`:** the interp mode bytes are 0 Linear, 1 CurveAuto, 2 Constant, 3 CurveUser, 4 CurveBreak.
2. **No `ETTA_Trigger`:** toggle actions are {Off, On, Toggle}.
3. **SeqAct_Interp outputs are Completed / Aborted only.** A skip fires **Aborted**. A reverse play reaching 0 fires Aborted. A
   stop mid-way fires nothing.
4. **HUD, player hiding and input** are controlled by SeqAct_ToggleCinematicMode, not by the matinee or director. Entering it also
   revives a downed player.
