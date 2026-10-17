# MP transforming actors: sounds and FX the original plays (RE, 2026-10-18)

Labels: C = authored data / script; H = native behaviour inferred (HmAnimNotify_Sound has no script; its handler is native).
Companion: MP_TRANSFORMING_ACTORS_RE.md (states, triggers, versus / Escalation), INTERPOLATED_LIGHTMAP_RE.md (lighting).

## 1. Where the presentation comes from (C)

- **`TnTransformingWorldActor` script plays no sound, FX, light or shake.** Its only side effect on a transition is the AI stimulus
  `TnStimulusType_WorldTransformation` (Transforming / Untransforming BeginState).
- The class has a `FaceAudioComponent` (FacialAudioComp), but nothing in its script uses it.
- **All audio comes from `HmAnimNotify_Sound` notifies on the transform AnimSequence.** The actor plays that sequence forward (rate 1)
  to transform and backward (rate −1) to untransform. **No particle, light, camera-shake or other notify exists** on any of the four
  AnimSets used in MP: they contain only AnimSequences and HmAnimNotify_Sound objects.
- **Replication (C + H):** `DesiredTransformState` replicates (bAlwaysRelevant, SimulatedProxy), and each machine plays the anim
  locally. So **every client fires the notifies itself**, and the sounds are local, not replicated sounds.

## 2. Notify semantics (H, native; field use C)

| Field | Meaning |
|---|---|
| `SoundCue` | the cue to play; an empty notify = silent |
| `SocketName` | the mesh socket to play at; empty = the actor / mesh location |
| `MinWeight` | the notify fires only if the anim node weight ≥ MinWeight (class default **0.25**; most door notifies set 0) |
| `FireWhenForwards` (default true) / `FireWhenBackwards` (default false) | the direction filter |

The authored pattern makes the direction filter clear: **opening cues fire forwards, closing cues are separate notifies marked
backwards-only**. So the same sequence gives distinct open and close sounds.

## 3. Per actor type (C)

**BrokenHope IAC_InteriorDoor ×13; Rust IAC_InteriorDoor ×1:** `TPRO_IAC_Doors_p.IAC_InteriorDoor_ANIM` /
`IAC_InteriorDoor_TransOut`, length 2.367 s.

| t (s) | Cue | Socket | Direction |
|---|---|---|---|
| 0.000 | `BL_TRANS_ENV_IAC.DOOR_INTERIOR` | AUDIO_DOOR_CENTER | forward (open start) |
| 1.402 | `BL_TRANS_ENV_IAC.DOOR_INTERIOR` | AUDIO_DOOR_CENTER | **backward only** (close start, reached ~0.96 s into the reverse play) |

**Rust IAC_TubeDoor ×1:** `TPRO_IAC_Doors_p.IAC_TubeDoor_ANIM` / `IAC_TubeDoor_TransOut`, length 1.70 s.

| t | Cue | Socket | Direction |
|---|---|---|---|
| 0.000 | `BL_TRANS_ENV_IAC.DOOR_TUBE` | (actor) | forward |
| 1.696 | `BL_TRANS_ENV_IAC.DOOR_TUBE` | (actor) | backward only |

**BrokenHope NEU_BridgeStraight ×5:** `TPRO_NEU_Bridges_p.NEU_BridgeStraight_ANIM` / `NEU_BridgeStraight_TransOut`, length 1.0 s.

| t | Cue | Socket | Direction |
|---|---|---|---|
| 0.000 | `BL_TRANS_ENV_NEU.TRANS_ENV_NEU_BRIDGE_STRAIGHT` | (actor) | forward only (default) |

The bridges never untransform in a match (MP_TRANSFORMING_ACTORS_RE §2). The PreGameplaySetup untransform snaps the position
(SetValuesForState), so it plays no anim and fires no notify.

**Remnant ORB_InteriorDoor ×12:** `TPRO_ORB_D1_Doors_p.ORB_InteriorDoor_ANIM_BIG_AUDIO` / `ORB_A4InteriorDoor_FastTransOpen`
(the **_BIG_AUDIO** set, not the plain ORB set), length 2.367 s. 14 notifies, no socket (actor location):

| t | Cue | Dir |
|---|---|---|
| 0.008 | INTERIOR_DOOR_MECH_01 | fwd (MinWeight 0) |
| 0.437 | INTERIOR_DOOR_SYNTH_VOX_01_PART_A | fwd (MinWeight 0) |
| 0.466 | INTERIOR_DOOR_MECH_03 | fwd |
| 0.521 | INTERIOR_DOOR_MECH_02 | fwd |
| 0.632 | INTERIOR_DOOR_SYNTH_VOX_02 | fwd |
| 1.208 | INTERIOR_DOOR_MECH_03 | back |
| 1.277 | INTERIOR_DOOR_MECH_01 | back (MinWeight 0) |
| 1.312 | INTERIOR_DOOR_SYNTH_VOX_01_PART_A | back (MinWeight 0) |
| 1.496 | INTERIOR_DOOR_MECH_02 | back |
| 1.511 | INTERIOR_DOOR_MECH_04_PART_A | fwd |
| 1.676 | INTERIOR_DOOR_MECH_02 | back |
| 1.983 | INTERIOR_DOOR_SYNTH_VOX_02 | back |
| 2.116 | INTERIOR_DOOR_MECH_04_PART_A | back |
| 2.277 | INTERIOR_DOOR_MECH_02 | back |

(All cues are in `BL_TRANS_ENV_ORB`. Notifies without a MinWeight use the 0.25 default; the door's single-node anim has weight 1, so all fire.)

**Opening a Remnant door plays 6 cues** (0.008–1.511 s). The backward-only ones form the closing set, which isn't used in a match
because Remnant doors never close.

## 4. Rust's level-start open (C)

`SeqEvent_SequenceActivated` → Transform on both Rust doors at level start, so **each client hears DOOR_INTERIOR (interior door)
and DOOR_TUBE (tube door) at its own level start**. The anim begins on the client when the replicated state arrives, or at its own
sequence activation.

## 5. FX / lights / shake

- **None authored for these actors in MP**: no notify, no script, no component.
- Any visual or sound extras around Escalation generator events would be Kismet (not surveyed here; H: none are linked to the door
  actions in the transforming-actor Kismet listed in MP_TRANSFORMING_ACTORS_RE).

## 6. For Systems / AssetTools

- **Implement:** play the sequence's sound notifies by direction and time as the anim advances (forward on Transform, backward on
  Untransform), at the socket if set (AUDIO_DOOR_CENTER on the IAC interior door) or the actor location. Locally on each client.
- **Cues needed:**
  - BL_TRANS_ENV_IAC.DOOR_INTERIOR, .DOOR_TUBE;
  - BL_TRANS_ENV_NEU.TRANS_ENV_NEU_BRIDGE_STRAIGHT;
  - BL_TRANS_ENV_ORB.INTERIOR_DOOR_MECH_01 / 02 / 03 / 04_PART_A, .INTERIOR_DOOR_SYNTH_VOX_01_PART_A, .INTERIOR_DOOR_SYNTH_VOX_02.
- No FX templates are required.
