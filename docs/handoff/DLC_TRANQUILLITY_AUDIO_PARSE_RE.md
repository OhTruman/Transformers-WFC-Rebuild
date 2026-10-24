# MP_ESC_Tranquillity_AUDIO_m: why 125 exports fail to parse (RE, 2026-10-23)

**Labels:** C = measured on the cooked file.

**Package:** AssetTools/work/dlc/cooked/MP_ESC_Tranquillity_AUDIO_m.xxx (LZO, 6 chunks).

**One cause, two effects (C):**
- **Chunk 0, block 1** (uncompressed 131,181 … 262,253; header says 131,072 bytes) decodes to **104,644 bytes**.
  - lzallright and an independent pure-python LZO1X decoder (`tools/lzo1x_ref.py`) agree byte for byte.
  - The input is consumed exactly, ending in a valid end-of-stream marker.
  - It's every block in all 6 chunks that's checked; this is the only short one.

**Effect 1, a loader bug that recovers 82 of the 125:**
- `ue3pkg.decompress` advances the write position by `len(dec)` instead of the block's declared uncompressed size.
- So everything after the short block, through the end of chunk 0, is shifted 26,428 bytes early. That's exports 222 … 344
  (SeqEvent_Touch, SeqVar_Object, SeqAct_PlaySound / PlayerPositionalSound, Reverb, SequenceFrame…).
- **Fix:** `w += bus` (place each block at its declared offset), and log when `len(dec) != bus`.
- With that change, TypedReader parses everything except 43 exports.

**Effect 2, damaged shipped data (43 exports, unrecoverable from this file):**
- The short block is valid only up to block offset 57,569 (absolute **188,750**, the end of Model export 208). From there to the
  end of its output it holds no object headers.
- The corruption point is at file offset 46,696, which isn't on a 4 KB STFS block boundary. Every block also matches its SHA-1. So
  the damage is in the cooked package as shipped, not in the extraction.
- The lost range is 188,750 … 262,900: 43 exports, listed in `tranquillity_audio_unrecoverable.json`:

| Class | Count |
|---|---|
| Model (TriggerVolume brushes) | 14 |
| Package | 15 (12-byte group objects) |
| SeqAct_AmbientAudioZone | 6 |
| SeqAct_Delay | 3 |
| SeqAct_Gate | 2 |
| SeqAct_PlayFlybySound | 2 |
| SeqAct_Mixer | 1 |

- H: the original read the same damaged bytes, so these objects were broken or unused in the shipped game as well.

**For Systems:**
- Every SeqEvent_Touch, SeqVar_Object, SeqAct_PlaySound, PlayerPositionalSound and Reverb object parses after the fix.
- Only the 6 AmbientAudioZone, 3 Delay, 2 Gate, 2 FlybySound and 1 Mixer ops are lost.
- Treat those as defaults or skip them.
