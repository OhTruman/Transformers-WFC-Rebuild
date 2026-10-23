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
| Matinee | slerp shortest-path for bUseQuatInterpolation | **C (0x82B82FA8)** | shortest path: second weight negated when dot < 0; plain lerp weights (1 − α, α) when \|dot\| ≥ 0.9999 (0x3F7FF972); no renormalize before the quat → rotator (0x82B8A0F0). A1 uses it on 311 move tracks | MATINEE_NATIVE §2 |
| Matinee | Lookup-key end tangents | **C (0x82BFFD80)** | first / last key → tangents zeroed (memset 12 bytes); interior keys Catmull-Rom with (1 − Tension) · 0.5 | MATINEE_NATIVE §3 |
| Matinee | groups bound to empty SeqVar_Objects (165) | C mechanism / H cause | actor tracks no-op; sound / event / fade tracks still run | MATINEE_NATIVE §7, PROVISIONALS_CLOSED §5 |
| Matinee | AnimNodeSlot "skip weights" flag (+0xC4 bit 31) meaning | H | ignore (always apply weights) | MATINEE_NATIVE §9 |
| Dialog | subtitle canvas placement / font | H | stock: highest-priority line, centred bottom title-safe, word-wrapped, white with shadow | DIALOG_TIMING §5 |
| Dialog | UseSubtitles = false lines hidden | HIGH (data pattern) | show only lines with UseSubtitles. The English wave cues have 1,648 true vs 8,697 false: true = story conversation lines, false = combat barks (reload / grenade / taunts). The native consumer (cue +0x10 bool in the subtitle queue) wasn't located; there's no name string in the exe | PROVISIONALS_CLOSED §6 |
| Dialog | conversation's second variable per line | **C (data) + H (use)** | VariableLinks come in pairs per line: `Speaker_N` (the speaking actor, required by CheckForErrors), then **`Face_N`**. Face is linked in only 69 links across the whole campaign, **0 in A1**. H: the speaker faces / looks at that actor. Implement as an optional look-at | DIALOG_TIMING |
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
| Kismet | latent ForceIntoForm finish; pseudo-god; IK look-at bones | **C / C / H** | **ForceIntoForm** (TnSeqAct_ForceIntoForm.Update, TnPawn.OnForceIntoForm): already in form → nothing; else MoveToSafeTransformationLocation → ForceIntoForm (PC.ForceToForm / AiController.Transform); a target that isn't in form and isn't transforming → **AbortFor** (Aborted); the latent finishes when every target is in the form. **Pseudo-god** (Controller.OnTogglePseudoGodMode inputs On / Off / Toggle; GameInfo.ReduceDamage): damage ≥ Health is clamped to **Health − 1** (never dies; that hit skips InvManager.ModifyDamage), and TnPawn refuses insta-gib while in (pseudo-)god. IK look-at bones: still H | KISMET_WFC_CLASSES |
| Kismet | ObjectiveType None matches all | **C: corrected** | an unset ObjectiveType is the **CDO default `TnObjectiveMarkerTypeObjective`**, not None. Add / Remove pass it through, and `TnObjectiveManager.RemoveObjectiveMarker` removes only entries with the **same MarkerBase and the exact same MarkerTypeClass** (replicated 16 slots + local list) | KISMET_WFC_CLASSES |
| HUD | which anchor clip each campaign HUD element sits under; pause-menu objective list | H | Frontend's layout | HUD_OBJECTIVES |
| FaceFX | Trypticon jaw correlation | PROVISIONAL (metric) | data is fine | FACEFX_RE |
| FaceFX | the data-derived bone mapping (needs a full-skeleton check) | PROVISIONAL | Gameplay to confirm | FACEFX_RE |
| Materials | RandomSeed (FMeshElement +0xBC): the reader is C (material PS SetMesh 0x82E982E8 uploads it), the **writer is UNKNOWN** | UNKNOWN | 0 = the first atlas variant | MATERIAL notes / matc.py |
| Materials | 4th-row / TEXCOORD1-2 lane notes; CameraWorldPosition fold | H | as MATERIAL_GAPS | MATERIAL_GAPS |
| Content | difficulty 0 / 1 / 2 = EASY / MEDIUM / HARD | **C** | the UIText.Difficulty strings, profile default 1 (CAMPAIGN_DIFFICULTY_RE). The MP iconic list: unchanged | CONTENT_DELTAS |
| Pipeline | reachFlags 2 = R_FLY airborne exemption | C (data) / was PROVISIONAL | keep | PROVISIONALS_CLOSED §1 |
| Pipeline | physmat simple-hull rule; default Metal fallback | H / C | keep | MP_COLLISION_PHYSMAT |

## Not RE (data or pipeline gaps, tracked elsewhere)
Pickup factories in design sublevels, the BSP default-face grids, the matc pass for the remaining campaign materials, the airborne
player starts: CAMPAIGN_BACKLOG (AssetTools / Rendering).

## Pass 2 (2026-10-23): A1 first

**Closed to C:**
- quat slerp;
- Lookup end tangents;
- the ForceIntoForm finish rule;
- pseudo-god;
- ObjectiveType (corrected: the default is the Objective marker type, and removal needs an exact match);
- the difficulty names;
- the conversation's second variable (data: `Face_N`, unused in A1).

**UseSubtitles:** raised to HIGH by the data pattern.

**What A1 actually uses** (authored.db, InterpTrackMove in A1 packages):
- 1,163 move tracks, of which 1,078 RelativeToInitial, 311 quaternion and 1,147 with lookup entries;
- FaceFX tracks 25.

So the remaining A1-relevant open item is **IMF_RelativeToInitial order** (H): VMX code that Ghidra can't decode; it affects only
groups whose actor has a Base.

**Missing evidence per remaining A1 row:**
- **RelativeToInitial order:** a VMX128-capable disassembly of the move-frame code (0x82C010B8 callee), or a runtime capture.
- **Streaming listeners 0x36–0x38 and the named-level step:** the listener objects are registered dynamically. Finding them needs
  the vtable of the object at the notify target (one more Ghidra pass: who calls the register at device +0x20 with ids 0x36–0x38).
- **The streaming time-slice budget:** the engine's per-frame budget constant in 0x82D5FB28's caller. Gameplay doesn't need it
  (stock is fine).
- **The checkpoint body binary / field order:** not needed (no byte compatibility).
