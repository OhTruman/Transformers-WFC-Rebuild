# Streaming / checkpoint notify ids 0x31–0x38 and the in-place restart's named-level step (RE, 2026-10-23)

Closes CAMPAIGN_STREAMING_RE's "native notify ids 0x32–0x38" and the "optional named level made visible" step.
Labels: C = WFC binary (headless Ghidra); H = inference.

## The device
The ids are event types on a global callback device (pointer at 0x8376A3D4: UE3's GCallbackEvent). vtable slots used:
- **+0x1C Send(type)**;
- **+0x20 Register(type, listener)**;
- **+0x24 Unregister(type, listener)**;
- +0x0C a second Register / Send-with-object variant (used for 0x33).

Senders and listeners were found by scanning every `li r4,0x3N` and `cmp r4,0x3N` against the functions that load the device
pointer.

## The ids (C: who sends them; H: the names are ours)

| id | sent by | when | listener (found) |
|---|---|---|---|
| **0x31** | LoadMap 0x82CE8E08 (called from Browse 0x82CEAB38) | **start of a map load** (pre-load map) | – |
| **0x32** | LoadMap 0x82CE8E08 | **end of a map load** (post-load map) | the checkpoint manager, registered by RestartFromCheckpoint's "travel" path |
| **0x33** | SeqAct_Interp.Cancel native 0x82C7D418 | **a matinee is cancelled (skipped)** | registered via 0x82BF20B8 from ULocalPlayer.CancelMatinee 0x82F95ED0 |
| **0x34** | World tick 0x82F98198 | **every world tick**, after the streaming update | the checkpoint manager (registered in its constructor 0x8277F898) |
| **0x35** | engine tick 0x82C39AC0, right after the world tick | **end of the engine's world update each frame**: a safe point | the checkpoint manager, registered by RestartFromCheckpoint's in-place path; 0x82CC6CE0 (another engine object) |
| **0x36** | RestartFromCheckpoint 0x8277F448 (both paths) | **a checkpoint restart was requested** (after registering 0x32 / 0x35 and starting travel or in-place) | not found by register scan (H: loading-screen / UI) |
| **0x37** | checkpoint restore 0x8278B2F8, last line | **checkpoint restore finished**: players restored, the restarting flag cleared | not found (H: UI / loading screen dismiss) |
| **0x38** | streaming reset 0x8278A8B0 | **mid-restore:** after the old streaming state is pre-deactivated, GC'd and flushed, **before** the checkpoint's state is activated and blocking-flushed | not found |

**Checkpoint manager listener (0x8278B5E0, C):**
- **0x32:** unregister; then **only if the new URL has `LoadCheckpoint`**, run the restore 0x8278B2F8.
- **0x35:** unregister; run the restore.

## Restart flow, with the ids (C)
- **Travel path** (checkpoint not in this map):
  1. Register(0x32);
  2. URL += `?LoadCheckpoint?DontLoadInitialStreamingState`; Browse;
  3. Send(**0x36**);
  4. LoadMap sends **0x31**, loads, then sends **0x32** → restore.
- **In-place path:**
  1. Checkpoint.ProcessActions; Register(0x35); flag the manager as restarting; Send(**0x36**);
  2. at the **end of the next engine tick** (0x35) → restore.
- **Restore (0x8278B2F8):**
  1. co-op data send;
  2. streaming reset 0x8278A8B0, which sends **0x38** in the middle;
  3. players restored;
  4. Send(**0x37**).

## The named-level step (C, 0x8278A8B0)
The checkpoint data carries two level-name strings (data +0x04 and +0x10) and the checkpoint object path (+0x1C).
- Every non-fully-static level is set to target **Unloaded**, then flushed.
- **If the two names differ** (stricmp ≠ 0): the streaming level named by the second string (+0x10, found by package name,
  0x82DD0B50) gets **target state 2** (H: Visible), and a **blocking flush** runs.
  - H: +0x10 is the level that contains the checkpoint actor. Loading it first is what lets the next step
    (`FindObject(checkpoint path)`) resolve the checkpoint while every other level is unloaded.
- Then:
  1. FindObject(checkpoint) → its StreamingState (+0x278);
  2. **PreActivate** → GC → flush;
  3. **Send 0x38**;
  4. **Activate** → blocking flush.

## For Gameplay / Frontend
- A loading or "restarting" overlay can key off 0x36 (start) and 0x37 (done). Our equivalent events: restart requested /
  restore finished.
- The in-place restore runs at the end of the frame after the request. The travel restore runs right after the map load
  completes.
- Make the checkpoint's own level visible (blocking) before resolving the checkpoint actor; then apply the checkpoint streaming
  state.
