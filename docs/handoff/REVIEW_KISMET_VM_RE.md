# Review: the campaign Kismet VM (integration 309628f3, `src/game/Kismet.h / .cpp`) vs the original (RE, 2026-10-23)

**Against:** KISMET_VM_CORE_RE (ExecuteActiveOps 0x82F2B450, UpdateOp 0x82F2EEE0, the CDO defaults), the authored.db Sequence
objects, and `next_map/D5_OME_kismet.json`.

**Verdict:**
- The structure is sound (load, the Host split, events, the stock ops).
- **Six behavioural findings:** K1–K4 change which events fire at all; K5–K6 change order and timing. Plus two minor ones.

## K1 (event defaults): RemoteEvent is wrongly unlimited, Interact and Touch defaults are missing
- `fireEvent` treats RemoteEvent / Console / SequenceActivated as MaxTriggerCount 0 when it isn't exported. The original CDO chain
  (C) gives:

| class | MaxTriggerCount | ReTriggerDelay |
|---|---|---|
| SequenceEvent (base) and **SeqEvent_RemoteEvent** | **1** | 0 |
| SeqEvent_SequenceActivated, SeqEvent_Console, **TnSeqEvent_Interact** | **0** | 0 |
| **SeqEvent_Touch** | 1 | **0.1** |
| GameplayStarted / PreGameplaySetup / destructible notification | 1 | 0 |

- **Impact:** 7,079 of the 10,439 campaign RemoteEvents don't export MaxTriggerCount, so **they fire once in the original** and
  unlimited in ours. Interact events (244) would fire only once in ours.
- **Fix:** a per-class default table (above); the exported value wins.

## K2 (bEnabled): authored-disabled events fire
- `Op::enabled` starts true and `load` never reads `authored.bEnabled`.
- Authored `bEnabled false`: 265 Touch, 13 RemoteEvent, 14 destructible notifications, 10 Interact. These are turned on later by
  SeqAct_Toggle; until then they must not fire.
- **Fix:** `op.enabled = authored["bEnabled"].asBool(true)`, and have the Toggle op (host or builtin) flip it.

## K3 (startup): every SequenceActivated fires at begin
- `begin` fires every SeqEvent_SequenceActivated with `!hasIncoming`. **No SequenceActivated ever has incoming links** (they're entered
  through the parent Sequence's input), so this fires **all 2,843**, including every sub-sequence entry, at level start.
- Original (HIGH): **only the ones directly in a level's root sequence** (parent path ends `PersistentLevel.Main_Sequence`, 150
  events) fire at level begin. The rest fire only via their parent's input link.
- **Fix:** filter `endsWith(o.seq, ".PersistentLevel.Main_Sequence")`.

## K4 (sub-sequence I/O): input k picks by object order; outputs are lost
- **Input:** the `Sequence` builtin fires the k-th SequenceActivated in load order. Original (C): `Sequence.InputLinks[k].LinkedOp`
  names the event. That differs whenever a sequence has more than one input.
- **Output:** `SeqAct_FinishSequence` calls `output(seq, OutputLabel)`. **The kismet JSON has no outputs for 27 of D5's 28
  Sequences, and no InputLinks / OutputLinks at all.** So every FinishSequence chain dead-ends today.
- Original (C): `Sequence.OutputLinks[k]` has `LinkedOp` = the FinishSequence and `Links` = the parent-level targets (LinkedOp +
  InputLinkIdx).
- **Fix:** AssetTools should export each Sequence's `InputLinks[].LinkedOp` and `OutputLinks[]` (LinkedOp + Links) from authored.db
  (all present: 576 inputs, 414 outputs). The VM maps input k → that event and FinishSequence → the output whose LinkedOp is it. I'll
  message AssetTools. Until then the VM can read them from authored.db directly.

## K5 (drain order): FIFO breadth-first vs the original LIFO depth-first
- `run` pops from the front of a deque (FIFO), so an op's outputs interleave breadth-first with everything already queued.
- **Original (C):** pop the **last** op. A finished op's impulsed targets are pushed in reverse, so **they run in link order,
  depth-first**: the first target's whole chain completes before the second target starts.
- **Impact:** any graph whose branches share variables or race (SetInt on one branch, Compare on another; two remote events touching
  the same actor) can resolve differently.
- **Fix:** use a stack, pushing each op's targets in reverse (output link order, then link order).

## K6 (caps and latent timing)
- **Cap:** ours is 20,000 per `run`, then **clears the queue**. Original: **1,000 steps per ExecuteActiveOps call (per sequence per
  tick)**, and the remaining ops **stay queued for the next tick**. Nothing is dropped.
- **Per sequence:** the original keeps a queue per Sequence. UpdateOp drains the parent, **then each nested sequence in array order**.
  Ours has one global queue. That's order-only, and lower priority than K5.
- **Latent first update:** the original calls an op's UpdateOp **in the same step it is activated**. So a SeqAct_Delay's first
  `−= dt` happens in its activation tick, and it finishes **one tick earlier** than ours (ours decrements first on the next tick).
  That's 33 ms at 30 Hz per Delay; chains of Delays accumulate it.
  - Fix: decrement in the activation step, or start the timer at Duration − dt.
- **Latent re-queue:** the original re-queues still-active ops after the drain, for the next tick. Ours scans all ops each tick, which
  is equivalent except for the order within the tick (H: the original's push-top position relative to the next tick's new events
  isn't traced; ours updates latents between the two runs, which is acceptable).

## Minor
- **M1:** output links with `bDisabled` (input-link bit 30) are skipped in the original. The JSON doesn't carry the flag; zero
  observed in D5, so there's no impact now.
- **M2:** `begin(checkpoint)`: on an **in-place** checkpoint restart the original skips PreGameplaySetup (`GameInfo.ResetLevel`, C). A
  level load starting at a checkpoint still runs it. Make sure the restart path doesn't call `begin` with PreGameplaySetup.

## Correct (C)
- **Startup order** PreGameplaySetup → root SequenceActivated → GameplayStarted, with the Originator = Checkpoint filter (once K3
  is fixed).
- **ReTriggerDelay** comparison; the MaxTriggerCount check; exported values taking precedence.
- **Variable writes are immediate**, which equals the original's publish-on-finish for non-latent ops. SeqVar_Named resolves once at
  load by VarName.
- **MutualExclusion** per WFC §69; the Gate / Switch / Compare / Set* stock bodies (H, stock).
