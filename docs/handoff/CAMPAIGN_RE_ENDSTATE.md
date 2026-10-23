# Campaign RE end state: what the campaign build still assumes (2026-10-23)

This is every item across the campaign RE docs that is still **H** (stock UE3 or inference, not read in WFC code) or **UNKNOWN**,
after the 2026-10-23 pass. Everything not listed here is CONFIRMED in its doc.
The status column says what the build should do today.

## Closed in the 2026-10-23 pass (now C)
- Matinee:
  - curve evaluation (broken-tangent default: no Δ scaling);
  - quaternion / Euler rotation;
  - Lookup keys;
  - IMF_World frame;
  - director cut / restore params;
  - sound termination;
  - AnimNodeSlot MAT_SetAnimPosition / Weights;
  - AnimControl GetAnimForTime (MATINEE_NATIVE §1–7, 9, 11).
- Togetherness volume / events and the BuddyLead emote finder (TOGETHERNESS_BUDDY).
- Conversation native timing (DIALOG_TIMING).
- Callback ids 0x31–0x38 and the named-level step (STREAMING_NOTIFIES).
- Checkpoint body serializer (CHECKPOINT_BODY).
- ActorFactory / PawnFactory stepping (ACTORFACTORY).
- FaceFX M quaternion and face-graph clamping (FACEFX_GRAPH).
- PostProcessVolumeHeightFog (PPV_HEIGHTFOG).

## Still open

| Area | Item | Label | Build assumption | Doc |
|---|---|---|---|---|
| Matinee | IMF_RelativeToInitial matrix order (VMX code) | H (operands C) | InitialTM × BaseTM, then KeyTM × RefTM. It matters only for **307 of 4,197** RelativeToInitial groups (actors with a Base). With stock InitTrackInst (InitialTM = ActorTM × BaseTM⁻¹), only this order reproduces the actor's start pose. | MATINEE_NATIVE §10 |
| Matinee | slerp shortest-path for bUseQuatInterpolation | H | stock FQuat Slerp (sign flip on negative dot) | MATINEE_NATIVE §2 |
| Matinee | Lookup-key end tangents | H | zero at the first / last key | MATINEE_NATIVE §3 |
| Matinee | groups bound to empty SeqVar_Objects (165) | C mechanism / H cause | actor tracks no-op; sound / event / fade tracks still run | MATINEE_NATIVE §7, PROVISIONALS_CLOSED §5 |
| Matinee | AnimNodeSlot "skip weights" flag (+0xC4 bit 31) meaning | H | ignore (always apply weights) | MATINEE_NATIVE §9 |
| Dialog | subtitle canvas placement / font | H | stock: highest-priority line, centred bottom title-safe, word-wrapped, white with shadow | DIALOG_TIMING §5 |
| Dialog | UseSubtitles = false lines hidden | H (data) | show only lines with UseSubtitles | PROVISIONALS_CLOSED §6 |
| Dialog | conversation's second variable per line | H | look-at / listener target | DIALOG_TIMING |
| FMV | Bink audio-track → speaker / language mapping; held last-frame timing | UNKNOWN (middleware) | track 0 = mix; hold the last frame until Stop | FMV_PLAYBACK |
| FMV | subtitle cue times in 1/100 s; rect order X1, Y1, X2, Y2 | H | as stated | FMV_PLAYBACK |
| Streaming | listeners of callbacks 0x36 / 0x37 / 0x38 | not found | use as "restart requested / restore done / mid-restore" UI hooks | STREAMING_NOTIFIES |
| Streaming | named-level step strings: +0x10 = the checkpoint actor's level | H | load that level visible (blocking) first | STREAMING_NOTIFIES |
| Streaming | per-frame streaming time-slice budget | H | stock AddToWorld time-slicing | STREAMING_RE |
| Checkpoint | binary form of struct array items in the body | H | any stable encoding (no byte compatibility needed) | CHECKPOINT_BODY |
| Checkpoint | TnCheckpointPlayerData field order | H | declaration order | CHECKPOINT_DATA §2 |
| AI | hit-physmat fallbacks (override, simple-collision hit) | H | as PROVISIONALS_CLOSED §3 | PROVISIONALS_CLOSED |
| AI | pawn falls when spawned without a floor | H | PHYS_Falling | PROVISIONALS_CLOSED |
| Kismet | stock-native actions: math / compare / counter / set-material / interp-float shape (CurveExponent ease) | H (stock) | stock UE3 semantics | KISMET_WFC_CLASSES |
| Kismet | latent "ForcedForm" Finished output; pseudo-god; IK look-at bones | H | as described | KISMET_WFC_CLASSES |
| Kismet | ObjectiveType None matches all | H | any type | KISMET_WFC_CLASSES |
| HUD | which anchor clip each campaign HUD element sits under; pause-menu objective list | H | Frontend's layout | HUD_OBJECTIVES |
| FaceFX | Trypticon jaw correlation | PROVISIONAL (metric) | data is fine | FACEFX_RE |
| FaceFX | the data-derived bone mapping (needs a full-skeleton check) | PROVISIONAL | Gameplay to confirm | FACEFX_RE |
| Materials | RandomSeed (FMeshElement +0xBC): the reader is C (material PS SetMesh 0x82E982E8 uploads it), the **writer is UNKNOWN** | UNKNOWN | 0 = the first atlas variant | MATERIAL notes / matc.py |
| Materials | 4th-row / TEXCOORD1-2 lane notes; CameraWorldPosition fold | H | as MATERIAL_GAPS | MATERIAL_GAPS |
| Content | difficulty 0 / 1 / 2 = Easy / Normal / Hard; MP iconic list shared | H | as stated | CONTENT_DELTAS |
| Pipeline | reachFlags 2 = R_FLY airborne exemption | C (data) / was PROVISIONAL | keep | PROVISIONALS_CLOSED §1 |
| Pipeline | physmat simple-hull rule; default Metal fallback | H / C | keep | MP_COLLISION_PHYSMAT |

## Not RE (data or pipeline gaps, tracked elsewhere)
Pickup factories in design sublevels, the BSP default-face grids, the matc pass for the remaining campaign materials, the airborne
player starts: CAMPAIGN_BACKLOG (AssetTools / Rendering).
