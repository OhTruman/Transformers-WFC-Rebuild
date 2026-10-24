# Kismet VM scheduling: active-op list vs. a full walk (RE, 2026-10-24; for Gameplay's fix F)

**Labels:** C = native (USequence::ExecuteActiveOps 0x82F2B450, UpdateOp 0x82F2EEE0; details in KISMET_VM_CORE_RE §4); H = stock
UE3.

## 1. The original keeps an active-op list; there is no per-tick walk of the graph (C)
- **Queue:** each USequence holds **ActiveSequenceOps**, a queue of the ops that received an impulse or are latent. Per tick, per
  sequence:
  1. the delayed-activation list (+0x94; H: ActivateDelay);
  2. if the sequence is enabled and its parent active, **ExecuteActiveOps**;
  3. then each nested sequence's UpdateOp **in array order**, so a parent drains before its children.
- **Ops that aren't queued cost nothing.**
- **Events aren't polled by the VM** (H, stock):
  - events are pushed in by their sources: actor Touch / Interact / Death / RemoteEvent / Console / Level and GameplayStarted
    notifications, through `CheckActivate`, which queues the event op;
  - polling-style events (e.g. proximity) are driven by their actor's tick, not by Kismet.

## 2. ExecuteActiveOps (C)
- **Pops the last op (LIFO).** For each:
  1. pull its linked variables;
  2. Activated, on its first activation or a re-impulse;
  3. **UpdateOp(dt)**;
  4. if finished: DeActivated + publish variables;
  5. queue its impulsed outputs **pushed in reverse**, so they run **in link order, depth-first**, within the same tick;
  6. skip targets whose input link is disabled.
- **Drain:** in the same tick until the queue is empty, **capped at 1000 steps per call** (and MaxSteps when non-zero). Anything left
  continues next tick.
- **Latent ops** (Delay, Interp, Teleport-with-travel, PawnFactory spawning, SetStreamingState, conversations…):
  - they stay active;
  - after the drain they're **re-queued for the next tick** and updated **once per tick**, also on their activation tick;
  - intermediate outputs (e.g. matinee events, Delay Finished) fire from inside their UpdateOp and are processed **that same
    tick**.
- **There is no time budget:** a step that does heavy work (spawning a pawn factory batch, a teleport, a matinee's first update)
  runs synchronously within its tick in the original too. The original avoids hitches only because the content (packages,
  animations, matinee data) is **already loaded with the streaming level** before the Kismet runs.

## 3. For an active-op list implementation to stay behaviour-identical
1. **Queue on impulse only.** Ops become active from an event or an upstream impulse; never scan the graph per tick.
2. **LIFO with reverse push** → depth-first, in link order, same tick. Respect disabled input links.
3. **Drain each tick** up to a 1000-step cap per sequence call, carrying the remainder over.
4. **Latent ops:** updated once per tick (after that tick's new activations), re-queued until finished. Their intermediate outputs
   are processed in the same tick.
5. **Order of sequences:** parent, then nested sequences in array order.
6. **Variables:** pulled at step start, published when the op finishes. Visible to later ops in the same tick.
7. **Spikes from loading inside a step** (a matinee clip load, the Omega factory's pawn assets) aren't original behaviour.
   **Preload with the streaming level** (or at chapter start for the persistent level) so the step only instantiates.
   - Spreading one op's work over frames would change timing (outputs would fire later), so **preloading is the lossless fix,
     not deferral**.
