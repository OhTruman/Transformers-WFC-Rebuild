# Matinee native: curve evaluation, move / lookup tracks, director and sound termination, unresolved groups (RE, 2026-10-23)

Upgrades CAMPAIGN_MATINEE_RUNTIME_RE.md §2.1 / §3 from HIGH to CONFIRMED and **corrects two points** there (marked ⚠).
Labels: C = read in the WFC binary / data; H = inference.
Tool: `tools/wfcq.ps1`. New WfcQuery commands: `words`, `opscan`, `funcs`, `decomprange`, `findword`.

## 1. Position curve: InterpTrackMove.PosTrack (C, 0x82C00378)
The point layout is 0x2C bytes: InVal +0, OutVal +4, ArriveTangent +0x10, LeaveTangent +0x1C, InterpMode +0x28.
The curve's InterpMethod byte follows the array (track +0x70).

| time t | result |
|---|---|
| no keys | (0, 0, 0) |
| 1 key, or t ≤ t₀ | key 0 |
| t ≥ t_last | last key |
| otherwise | segment (i−1, i) with t < tᵢ (first such key) |

Inside the segment, Δ = tᵢ − tᵢ₋₁:
- **Δ ≤ 0, or key i−1's mode is Constant (2):** key i−1's value.
- **Mode Linear (0):** lerp(Pᵢ₋₁, Pᵢ, α), with α = (t − tᵢ₋₁) / Δ.
- **Modes CurveAuto / CurveUser / CurveBreak (1 / 3 / 4):** `CubicInterp(Pᵢ₋₁, Lᵢ₋₁, Pᵢ, Aᵢ, α)` (0x82B5B040), the standard
  Hermite:
  `P0·(2α³ − 3α² + 1) + T0·(α³ − 2α² + α) + T1·(α³ − α²) + P1·(−2α³ + 3α²)` (constants 2, 3, −2 read from the binary).

**⚠ Tangent scaling depends on the curve's `InterpMethod`:**
- **0 → tangents are used as stored, NOT multiplied by Δ.**
- 1 → tangents × Δ.
- WFC's Core enum is **`EInterpMethodType {IMT_UseBrokenTangentEval = 0, IMT_UseFixedTangentEval = 1}`**.
- **No InterpTrackMove in the campaign authors InterpMethod** (6,327 tracks), so **every move track uses the raw tangents with no
  Δ scaling.** The RUNTIME doc's `LeaveTan·Δ` form is wrong for WFC data.
- Tangents are the stored ones; nothing is recomputed at runtime, except lookup keys (§3).

## 2. Rotation: EulerTrack and quaternion mode (C, 0x82C00A08 / 0x82C006C0)
`GetKeyTransformAtTime` (vtable slot at 0x822B3294) evaluates position by §1, then rotation.

**`bUseQuatInterpolation` (track +0x9C bit 31):**
- the bracketing keys' Euler values are converted to quaternions;
- **slerp** with α = clamp((t − tᵢ₋₁) / Δ, 0, 1);
- **the interp mode is ignored**: Constant keys also slerp;
- outside the range → the first / last key;
- no keys → the default rotation.

**Otherwise:** the EulerTrack (+0x74, InterpMethod at +0x80) is evaluated exactly like §1 (same Constant / Linear / Hermite rules
and the same raw-tangent rule) as an Euler vector in degrees, then converted to a rotator.

## 3. LookupTrack keys (C, 0x82BFFD80 position; 0x82C00078 rotation)
A key whose LookupTrack entry (track +0x84, 8-byte entries) names a group takes **that group's actor's current Location**. If the
actor is a Controller, its Pawn is used.

The tangents of such a key are **recomputed**, Catmull-Rom style:
`T = ((P − P_prev) + (P_next − P)) · (1 − Tension) · 0.5`. Tension is the track field at +0x94 (rotation: +0x98).
- Interior keys use this.
- End keys: H, zero tangents (the branch was cut off in the decompile).

## 4. Move reference frame (0x82C010B8)
- **MoveFrame byte (+0xA0) = IMF_World (0):** the reference is the group actor's **Base** transform if it has a Base, else
  identity (C).
- **IMF_RelativeToInitial (1):** the code is VMX matrix math the decompiler can't render. H, stock: `InitialTM × BaseTM`, where
  InitialTM is captured at InitTrackInst.

## 5. Director termination: ⚠ correction (C)
The RUNTIME doc's "0x82C02670 restores the view target with 0.1 s" was a **misattribution**:
- **0x82C02670 is InterpTrackSound's TermTrackInst** (§6).
- **The director's TermTrackInst is 0x82C21030.** If the track instance holds a cut camera (+0x2C) and the group actor is a local
  PlayerController, it pops the camera (0x82C20FC8) with **BlendTime = the last cut's TransitionTime** (stored at +0x30 when the
  cut was applied).

**The PlayerController keeps a matinee view-target stack** (PC +0x520):
- **push 0x82C106B0:** if the stack is empty, the current view target is pushed first;
- **pop 0x82C19D00:** when one entry is left, `SetViewTarget(original, params)` and the stack is cleared.

**Transition params on every cut and on the restore (C, Engine enum order read):**
`{BlendTime = cut TransitionTime, BlendFunction = VTBlend_Cubic (1), BlendExp = 2.0, bLockOutgoing = false}`.
- A cut back to the player's own pawn uses the same pop.
- While the sequence is **canceled (skipped)**, BlendTime is 0 (as documented).

## 6. Sound track termination (C, 0x82C02670)
At matinee end, each sound track's audio component:
- **bUseExistingSound** (track flags +0x84 bit 0x20000000): left untouched;
- **ended normally** and the track has **bContinueSoundOnMatineeEnd** (0x80000000, authored true on 1,988 tracks): keeps playing;
- **skipped** (sequence bIsCanceled) and the track has **bContinueSoundOnMatineeSkip** (0x40000000; class default true, authored
  false on 754): keeps playing;
- **otherwise: `FadeOut(0.1 s, 0.0)`** and the component is released.

## 7. The 165 unresolved groups (C for the mechanism)
- The bound SeqVar_Objects carry **no ObjValue at all** (e.g. A1_IAC_DecoInt_Design_m RobotArmActions.SeqVar_Object_15191: a
  40-byte export holding only ParentSequence).
- **WFC's level format has no cross-level actor reference table:** no CrossLevelActors / fixup names in the package; the Level
  export has no such list.
- Nothing in the exported levels writes these variables (RUNTIME / SCHEMA docs).
- So these groups **have no actor in the shipped game.** Their **actor-targeted tracks** (Move / AnimControl / Toggle / material /
  property) do nothing. Actor-free tracks in the same group (Sound at the listener, Event, Fade, and so on) still run, as in
  CAMPAIGN_PROVISIONALS_CLOSED_RE §5 (23 of 148 groups).
- Example: the 18 A1 DecoInt robot-arm groups (DataCube2 / BasePiece / MiddlePiece / EndPiece, RelativeToInitial moves). The likely
  cause is a cross-level actor reference cleared at save (H).
- **Rebuild:** treat them as no-ops. Don't try to bind them by name.

## 9. AnimControl native slot playback (C, added 2026-10-23)
Path: track → `Actor.SetAnimPosition / SetAnimWeights` (script) → `USkeletalMeshMatineeAnimator` → for **every AnimNodeSlot whose
NodeName == SlotName** in the animator's slot list (gathered at BeginAnimControl by IsA(AnimNodeSlot), 0x82D7D688) →
AnimNodeSlot vtable +0x1D8 / +0x1DC.
- The animator execs are 0x82C86B38 / 0x82CAD1F0 → 0x82D74860 / 0x82D74900.
- AnimNodeSlot's vtable is 0x820AA180, read from its constructor 0x827A3198.

**`MAT_SetAnimPosition(ChannelIndex, AnimSeqName, Position, bFireNotifies, bLooping)` (0x82E6F2F8):**
- target child = **Children[ChannelIndex + 1]** (child 0 is the normal anim-tree source); it must be an AnimNodeSequence, else
  nothing happens;
- if its AnimSeqName differs → `SetAnim(name)` (seq vtable +0x180), and the child's additive flag is copied from the new
  AnimSequence;
- `bLooping` is written onto the sequence node;
- **`SetPosition(Position, bFireNotifies)`** (vtable +0x184), then a slot-child refresh (0x82E7A5E0).
- There's **no play rate, blend time or end handling here.** The track supplies the absolute position every frame. Looping /
  clamping of that position is the track's job (§10), and the node only holds the bLooping flag.

**`MAT_SetAnimWeights(SlotInfo)` (0x82E3B658):**
- 1 child → source weight 1.
- Otherwise, **from the last channel down to channel 0**:
  `w_i = clamp(ChannelWeights[i], 0, 1)` (0 if absent); `child[i+1].Weight = (1 − Σ already assigned) · w_i`.
  The source child gets `1 − Σ`.
- **Higher channels take priority.** Weights apply instantly with no internal blending. The fade in / out is entirely the track's
  weight curve (FloatTrack), evaluated as in §1.
- Skipped for slots with flag +0xC4 bit 31 set (H: a "don't let matinee drive weights" flag).

## 10. Move RelativeToInitial (C for operands, H for order)
- In 0x82C010B8, the IMF_RelativeToInitial branch loads **InitialTM from the track instance +0x50** and the **BaseTM** (identity, or
  the Base actor's transform) as the 4 row vectors. The product is VMX128 code Ghidra can't decode.
- The register setup (BaseTM rows loaded whole, InitialTM rows splatted) is the shape of `VectorMatrixMultiply(Result, InitialTM,
  BaseTM)` = **InitialTM × BaseTM** (row-vector convention), which matches stock UE3.
- The final actor transform is then **KeyTM(t) × RefTM** (H stock).

## 8. Still HIGH / open
- AnimControl native slot playback (SkeletalMeshMatineeAnimator SetAnimPosition / SetAnimWeights): not traced.
  The script path is C (RUNTIME §4.1).
- IMF_RelativeToInitial matrix order: VMX, not decompiled (H stock).
- Slerp shortest-path behaviour (0x82B82FA8): H stock.

## 11. AnimControl track-side position (H: the native GetAnimForTime was not located)
I decompiled the whole Matinee code range (0x82C00000–0x82CC0000, about 3,200 functions) and searched for the key-array walk
(0x1C-byte keys: StartTime, AnimSeqName, AnimStartOffset, AnimEndOffset, AnimPlayRate, bLooping / bReverse bits at +0x18). That
found only the editor key add / set functions (0x82C24160 / 0x82C070A8), which confirm the key layout and the **bit order:
bLooping = bit 31, bReverse = bit 30**. The evaluator itself is likely inlined into UpdateTrack and wasn't isolated.

**Stock UE3 formula (H):** with key k = the last key whose StartTime ≤ t (key 0 if t is before it):
- L = sequence length; span = L − StartOffset − EndOffset;
- p = (t − k.StartTime) · AnimPlayRate;
- looping: p = fmod(p, span); otherwise **clamp p to span**, so it holds the last frame past the end;
- reverse: pos = (L − EndOffset) − p; otherwise pos = StartOffset + p;
- then `SetAnimPosition(slot, channel, k.AnimSeqName, pos, bFireNotifies = !jump, k.bLooping)` → §9.

Data: 1,177 of 6,609 keys loop and 202 are reversed.
