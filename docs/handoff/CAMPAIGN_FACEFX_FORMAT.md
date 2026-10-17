# FaceFX .fxa format notes (Systems, 2026-10-16; read-only RE, first pass)

Source: the 22 FaceFX actor files in ExtractedAssets content (read only) - one per character skeleton, e.g.
TR_Optimus_ROBO_p/RB_Optimus_FaceFX.fxa (374 KB), TR_Trypticon_ROBO_p/RB_Trypticon_FaceFX.fxa (48 KB). Tools:
tools/systems/fxa/fxa_strings.py (strings + offsets), fxa_survey.py (all files; output work/fxa_survey.json). Labels:
**ESTABLISHED** = seen identically in all 22 files; **HYPOTHESIS** = consistent with the bytes, not yet proven by a parser
round trip. Companion: CAMPAIGN_AUDIO_DIALOG.md section 6.

## 1. What the files are

- **ESTABLISHED:** FaceFX (OC3 Entertainment) binary archives as embedded by UE3: each is the raw actor blob of a UE3
  FaceFXAsset object, the actor of one skeletal mesh. Magic "FACB", archive version 1710 (FaceFX 1.7.x) in all 22, then
  the licensee string "Unreal Engine 3 Licensee" and the project string "Unreal Engine 3 Project".
- **ESTABLISHED:** the archive is big-endian (Xbox 360 cook): strings are a u32 BE length followed by the characters
  (no terminator); 32-bit integers are BE; floats are IEEE BE (e.g. 3E23D70A = 0.16).

Survey (fxa_survey.py, all 22 files):

| item | value |
|---|---|
| version | 1710 in all 22 |
| animation groups | "Default" and "Dialogue" in all 22 |
| face bones per actor | 14 for every robot (C_Head01_Face_XF2, C_Face01_Jaw_XF2, L / R Face02 EyelidUp, Face03 EyelidDn, Face04 Cheek01, Face05 LipInUp, Face06 LipCorner, Face07 LipInDn); 33 for the two KON Megatron wall / holo faces |
| animations | 3,580 in all, every one in group Dialogue, named DX_<CHAR>_LK<nnnnnn> (Megatron 621, Optimus 533, Bumblebee 215 ... Trypticon 46) |
| audio link | every animation (3,580 / 3,580) is preceded by its SoundCue path, all in BL_DX_SWITCHBOARD (e.g. BL_DX_SWITCHBOARD.SC004350 -> DX_TRPTCN_LK004350) |

## 2. Layout (Trypticon, 47,732 bytes; offsets from fxa_strings.py)

| offset | content | status |
|---|---|---|
| 0x00 | "FACB", u32 1710, licensee string, project string | ESTABLISHED |
| ~0x3f | u32 1000 (an internal version), a small header, then the class table | HYPOTHESIS for the small fields |
| 0x51-0x14c | class table: FxBone, FxCompiledFaceGraph, FxObject, FxActor, FxAnim, FxMasterBoneList, FxNamedObject, FxName, FxAnimCurve, FxPhonemeMap, FxAnimGroup - each a length-prefixed name with a class version / id before it | names ESTABLISHED; the per-entry fields HYPOTHESIS |
| 0x150-~0xa60 | name table: the actor name (RB_Trypticon), the 14 bone names, the face-graph node names (Neutral, phoneme targets such as Church / Fave / Told, Jaw_Open / Jaw_Forward / Jaw_Back / Jaw_Left / Jaw_Right, SmileLeft / Right, FrownLeft / Right, UpperLip_Up / Down, LowerLip_Up / Down, Lips_Narrow, Eyelid*_Closed / Wide / Squint, Cheek*_Up / Down, Eyebrows_Raise, Exp_Pain, Exp_Anger, Blink, Gaze* / Emphasis* / Orientation* head and eye nodes), the group names (Default, Dialogue) and the animation names | ESTABLISHED (strings); index assignment HYPOTHESIS |
| ~0xa60-~0x39f0 | binary: the master bone list (each bone's rest pose) and the compiled face graph (nodes, links, per-node bone poses) | HYPOTHESIS (no strings; float-heavy) |
| ~0x39f0-~0x3f40 | the node names again (the face graph's own name list) | ESTABLISHED (strings) |
| ~0x4000-end | the Dialogue group's animations, one block each | see section 3 |

## 3. One animation block (DX_TRPTCN_LK004350 at ~0x4300)

```
3E23D70A 3E6147AE           floats 0.16, 0.22                -> HYPOTHESIS: blend-in / blend-out time (s)
00000000 00000000 0000001A  "BL_DX_SWITCHBOARD.SC004350"     -> ESTABLISHED: the SoundCue this animation lip-syncs
00000012                    "DX_TRPTCN_LK004350"             -> ESTABLISHED: the animation name (= InterpTrackFaceFX FaceFXSeqName)
00000000 00000000 0000004F  ...                              -> HYPOTHESIS: class / object ids
00000019                    25                               -> HYPOTHESIS: number of curves
25 x (u32 name index, u32 0)  0x11, 0x42, 0x46, 0x3C ...     -> HYPOTHESIS: each curve's target node (name-table index)
BF0F3F80 40872 8C1          -0.56, 4.22                      -> HYPOTHESIS: the animation's start / end time (s)
then per curve: key counts, then packed keys: increasing u16 (08F0 0B77 140D 1A9D 20AD 35E4 3EFB ...)
                                                             -> HYPOTHESIS: quantised key times across [start, end], then
                                                                quantised values (and, for Hermite keys, slopes)
```

The next block starts after the packed keys with the next pair of floats and SoundCue path; that repeats 46 times in
this file.

## 4. How UE3 uses it (and how InterpTrackFaceFX references it)

- **ESTABLISHED (campaign data):** InterpTrackFaceFX keys are {StartTime, FaceFXGroupName = "Dialogue", FaceFXSeqName =
  "DX_<CHAR>_LK<n>"}: at StartTime the actor plays that animation (and through the archive's SoundCue link the dialog
  line). 123 tracks campaign-wide (13 in D5_OME); scripted lines without a matinee play the same animation by name.
- **Runtime model (FaceFX 1.7, HYPOTHESIS for our port):**
  1. Evaluate each curve of the playing animation at t (Hermite between keys) -> a value per face-graph node.
  2. Run the compiled face graph: node values combine through its links (linear / clamped / inverse ... link functions)
     into the bone-pose target nodes.
  3. Each bone-pose node holds, per face bone, a pose delta (position / rotation / scale); the bones' final pose is the
     master-bone-list rest pose plus the weighted sum of the active node deltas.
  4. UE3 applies the 14 (33) bone transforms on top of the skeleton's animation (the FaceFX bones override the body anim
     on the face).

## 5. What a runtime needs (Gameplay skinning + Systems audio)

1. A parser for the archive (header, class table, name table, master bone list, compiled face graph, anim groups /
   animations / curves), validated by a round trip: every animation name and cue link found (3,580 / 3,580), curve counts
   and key counts summing exactly to each block's length.
2. Curve evaluation (FaceFX key interpolation), face-graph evaluation, bone-pose blending.
3. A hook in the skinned character pose (Gameplay): after the body animation, override the face bones.
4. Audio sync (Systems): the dialog line (the SoundCue in the block) starts with the animation; the curve time follows the
   playing voice (the dialog component's playback position), so lip-sync stays aligned when audio starts late.
5. Data: 22 files, 1.6 MB in all - small; ship as is (they are part of the user's own extracted game data).

## 6. Next RE steps (in order)

1. Parse the class table and name table exactly (field widths before each name), so name indices are proven.
2. Walk one animation block to its end: confirm the curve list (25 x 8 bytes), the time range floats, and the per-curve key
   encoding (count, u16 time, value, slopes) by landing exactly on the next block's float pair; then all 46 blocks in
   Trypticon, then all 3,580.
3. Decode the master bone list (14 rest transforms) and the compiled face graph (node count, link table, bone-pose nodes).
4. A visual proof: one line (e.g. DX_TRPTCN_LK004350) evaluated on Trypticon's face bones beside the voice.

## 7. Open questions

1. The jaw-flap fallback until this lands (CAMPAIGN_AUDIO_DIALOG.md question 4) - user decision.
2. FaceFX's key interpolation and link functions: recover them from the data alone (curve shapes vs node targets), or
   is there an RE source for the FaceFX 1.7 runtime in the original's executable (statically linked into the game)?
