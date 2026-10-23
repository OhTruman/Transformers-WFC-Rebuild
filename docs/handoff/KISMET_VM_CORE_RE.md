# Kismet VM core: answers for Gameplay's campaign VM (RE, 2026-10-23)

**Labels:**
- C = native code decoded / CDO data (authored.db) / bytecode;
- HIGH = strong data evidence, no code;
- H = stock UE3.

**Natives decoded:**
- **USequence::ExecuteActiveOps = 0x82F2B450**;
- **USequence::UpdateOp = 0x82F2EEE0**;
- USequence::UpdatePreGameplayOp = 0x82F4E2D8 (exec 0x82C57548);
- AWorldInfo::execPerformPreGameplaySetup = 0x82C4D770.

## 1. Event trigger defaults

The effective values come from the CDO chain (C, authored.db). A CDO stores only what differs from its parent. Property defaults where
nothing sets them: ReTriggerDelay 0, bEnabled true.

| class | parent | MaxTriggerCount | ReTriggerDelay | bPlayerOnly | other |
|---|---|---|---|---|---|
| SequenceEvent | - | **1** | 0 | **true** | bEnabled true |
| SeqEvent_RemoteEvent | SequenceEvent | **1** | 0 | false | EventName DefaultEvent |
| SeqEvent_SequenceActivated | SequenceEvent | **0** (unlimited) | 0 | false | InputLabel "Default Input" |
| SeqEvent_Console | SequenceEvent | **0** | 0 | true | |
| SeqEvent_GameplayStartupBase (HM) | SequenceEvent | 1 | 0 | false | |
| SeqEvent_GameplayStarted / SeqEvent_PreGameplaySetup | GameplayStartupBase | **1** | 0 | false | |
| SeqEvent_Touch | SequenceEvent | **1** | **0.1** | true | ClassProximityTypes [Pawn] |
| TnSeqEvent_Interact | SequenceEvent | **0** | 0 | true | outputs Touch / Untouch / Adjusting / Begin Use / End Use |
| SeqEvent_HmDestructibleStateNotification | SequenceEvent | **1** | 0 | true | vars Instigator / Piece Name / State Name |

**Instance overrides are common.** For example:
- 3,360 of 10,439 RemoteEvents set MaxTriggerCount 0;
- 401 of 794 GameplayStarted set 0;
- 917 Touches set ReTriggerDelay 0.0;
- 1,109 of 1,358 destructible notifications set bPlayerOnly false.

**Use the exported value when present, else the table above.**

**ActivateRemoteEvent → RemoteEvent:** H, stock. It goes through the event's CheckActivate, so **bEnabled, MaxTriggerCount and
ReTriggerDelay all apply**. Thirteen RemoteEvents use ReTriggerDelay 3 / 20, and 13 are authored disabled.

## 2. Sub-sequences (C, data)
- **Input k:** `Sequence.InputLinks[k].LinkedOp` **names the SeqEvent_SequenceActivated directly**. It's present on all 576 exported
  sequence input links, so resolve by that pointer, not by label or object order.
  - That order is the editor's sort by the events' ObjPosY, then X. It's consistent on all 791 multi-input sequences, but you don't
    need it.
- **Output k:** `Sequence.OutputLinks[k].LinkedOp` = **the SeqAct_FinishSequence** inside. `Sequence.OutputLinks[k].Links` = the
  **parent-level targets** (LinkedOp + InputLinkIdx), exactly like any op's output link.
  - All 414 exported output links carry LinkedOp; 233 have targets.
  - Read them from the Sequence object's own OutputLinks property. The `outputs` field of the kismet JSON is empty because
    AssetTools doesn't flatten them.
- **Firing:**
  - Activating input k of a Sequence activates the SequenceActivated named by InputLinks[k].LinkedOp (H stock; its own trigger
    count applies).
  - A FinishSequence firing fires the parent Sequence's output k, where OutputLinks[k].LinkedOp == that FinishSequence.

## 3. Startup order
- **PreGameplaySetup:**
  - fired by native `WorldInfo.PerformPreGameplaySetup(CP)`;
  - script calls it from `GameInfo.ResetLevel` **only when not restarting from a checkpoint** (C);
  - it runs a special pre-gameplay Kismet pass (UpdatePreGameplayOp 0x82F4E2D8). That pass executes the active ops with **only
    pre-gameplay-capable ops**: it temporarily removes the others from the queue, runs, then re-queues them. So PreGameplaySetup
    chains run **before** gameplay starts, and ops that can't run pre-gameplay wait for the first normal tick (C structure; which
    classes are allowed is a vtable predicate, not traced).
- **Then GameplayStarted** (H order: pre-gameplay setup → gameplay started, as the names and the HM base class imply).
- **Originator = Checkpoint_X: HIGH, fires only when starting from that checkpoint.**
  - 330 of 764 campaign GameplayStarted and 9 PreGameplaySetup carry a Checkpoint originator; the other 434 / 70 have none.
  - Checkpoint-tagged events exist in up to 3 copies per checkpoint, which only makes sense as filters.
  - The native filter itself isn't traced: the native-table function slots are filled at runtime.
  - **Events with no Originator fire on every start.**
- **SequenceActivated with no incoming link:** the **150 SequenceActivated directly in a level's Main_Sequence** (147 have outputs:
  loot tables, AttachToEvent, destructible toggles, boss staging) are level-start setup. **HIGH: they fire when the level's Kismet
  begins** (streamed-in or level load).
  - Sub-sequence SequenceActivated events (2,694) fire **only** through their parent's input link.
  - `GameInfo.ResetLevel` (C) re-fires SequenceActivated events whose Outer is a Level after `GameSeq.Reset()`.
  - Read literally, that cast matches none of the exported events: they're all outered to a Sequence. So the root-level re-fire on
    an in-place checkpoint restart is H. Restarts reload the streamed levels anyway (CAMPAIGN_STREAMING section 4), which begins their
    Kismet fresh.

## 4. The per-tick loop (C, ExecuteActiveOps 0x82F2B450)

**Queue order:**
- **LIFO:** each step pops the **last** op of the sequence's ActiveSequenceOps.
- **Same-tick drain:** the loop runs until the queue is empty, capped at MaxSteps (when non-zero) **and a hard 1000 steps per call**.

**Per op:**
1. vtable +0x168 (H: PopulateLinkedVariableValues).
2. **If not already active** (or re-activation is flagged): set bActive, ActivateCount++, then **Activated** (+0x170), plus the
   script event when one exists.
3. **UpdateOp(DeltaTime)** (+0x16c) → finished?
   - **Not finished:** the op is collected as **latent**. After the queue drains, the latent ops are re-queued (QueueSequenceOp,
     push-top) for **the next tick**. So **a latent op is updated once per tick, after that tick's new activations**. It's also
     updated in the same tick it was activated.
   - **Finished:** DeActivated (+0x174), then +0x164 (H: PublishLinkedVariableValues), then +0x178 after the outputs are queued.
4. **Outputs:**
   - every output link with bHasImpulse contributes its (op, input index) targets, in link order then target order;
   - then **all of this op's input and output impulses are cleared**;
   - the targets are pushed in **reverse**, so with LIFO popping **they run in link order, depth-first**: the first target's whole
     chain runs before the second target starts;
   - a target whose input link is **bDisabled** (bit 30) is skipped;
   - otherwise its input impulse is set, +0x17c (H: OnReceivedImpulse) is called, and it's queued.
   - Outputs also fire from a **still-active** latent op when it has set an impulse (the bit-30 path), e.g. Delay / Interp
     intermediate outputs.

**UpdateOp (0x82F2EEE0), per sequence per tick:**
1. Process the +0x94 list (H: delayed activations, ActivateDelay).
2. If the sequence is enabled and its parent is active: ExecuteActiveOps.
3. Then **each nested sequence's UpdateOp, in array order**. So a parent's queue drains before its children's.

## 5. Variables
- **The write timing is C structure, the names H:** an op pulls its linked variables at the start of each step and publishes them back
  when it finishes (DeActivated → publish).
  - SeqVar objects are shared cells, so **a SetInt / Increment result is visible to any op that runs later, in the same tick**
    (including its own output chain, which runs right after, depth-first).
  - A latent op publishes only when it finishes.
- **SeqVar_Named:** H, stock. It resolves to the matching SeqVar (by VarName, searched from the root sequence) **once, when the
  sequence initializes** (level Kismet begin), not on every read. Treat it as an alias to that cell.

## Still open
- The native Originator filter for startup events (HIGH by data).
- The +0x94 delayed-activation list's exact semantics (H: ActivateDelay).
- The pre-gameplay-capable op predicate.
