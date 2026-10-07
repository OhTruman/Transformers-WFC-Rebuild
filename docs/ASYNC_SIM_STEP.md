# Async simulation step — snapshot boundary (design note, Milestone E)

Status: DESIGN for review by Rendering, Systems, Integration. No code yet. Owner: Gameplay (World / sim side); Integration owns the
glue side.

## Why

At 300 fps a 60 Hz step runs in one frame out of five. The in-game step at 32 v 32 costs ~1.6-1.9 ms (Integration 28aec2e:
world tick ~1.1-1.3 ms + glue ~0.4-0.6 ms). Averaged it is ~0.35 ms per frame (inside the 0.5 ms budget), but the frame that
contains a step pays all of it and misses 3.33 ms. Running the step on a worker while the main thread renders removes that spike
without lowering any quality.

## Today (one thread, per frame)

1. `handleInput(frame input, realDt)` — camera look per frame, buffered intent / latched edges.
2. `clock_.tick` → 0..n fixed steps: `world_.tick(1/60)` + `gameMode_.tick` (all simulation, plus the glue and audio blocks that
   live inside `World::tick`).
3. `setRenderAlpha(clock_.alpha())`, `updateCamera`.
4. `world_.draw(renderer)` + HUD (`hudState()`), frontend events, audio listener / mix.

Presentation already lags one step: frames between step k and k+1 draw the blend of step k-1 → k (`renderAlpha`, `prevPos_`,
previous palettes / vertices).

## Proposed timeline

At the frame where the accumulator crosses a step boundary, the main thread *launches* step k+1 on the worker instead of running
it, and keeps rendering the k-1 → k blend. The worker must finish before the next boundary (16.7 ms; the step is < 2 ms).
The main thread joins at the next launch (or earlier if the frame needs step k+1's snapshot).

Two variants (decision for the user / Integration):

- **A. Whole step async.** Simplest. Costs one extra step of presentation latency for the local player's own movement (the
  camera / look stays per frame, so aiming feel is unchanged; movement / firing results appear 16.7 ms later).
- **B. Split step.** The local player's part of the step (controller apply, local pawn movement / weapon, the camera) runs
  synchronously on the main thread at the boundary as today; the participant / bot / ability / projectile / glue part runs async.
  No added local latency, more ordering work (local vs participant interactions inside a step must keep today's order, see
  Determinism).

Recommendation: A first behind a switch (WFC_ASYNCSTEP=1), measure the latency with Experimental's input-latency probe; B only if
A's latency is noticeable.

## What is read during a frame (must come from the snapshot, not live state)

### World::draw (Gameplay) and the renderer (Rendering's requirements)
- Per pawn (local + participants): model matrix (pose position / yaw / form / partner / arm / weapon socket), `renderOffset`
  (prev → cur position), bone palette **and** previous palette **and** the palette serial **and** alpha, all from the SAME snapshot
  (a torn pair blends wrong poses). CPU-skin fallback: the skinned vertex buffers + previous vertices, likewise paired.
- Held weapons (local + participants): weapon pose palette, weapon socket world matrix, charge glow parameter.
- Projectiles (flight FX positions, grenade meshes), barriers / sentries / beacons / rollers (positions, meshes, alive state),
  pickups, destructibles, map movers, objective / mode actors visibility, hazards.
- FX: `fx_` (weapon FX particles), `vehicleFx_`, participant shot FX queue (`partShotFx_`), repair beams (`partBeams_`), map FX
  spawn / move events — consumed on the main thread **in snapshot order** (single-threaded GL).
- Debug / QA overlays (bot brains: path, target, labels).
- Static (no snapshot needed): bind meshes, joints / weights, materials; light envs and bounds are computed renderer-side from
  the inputs above.

### Integration glue and HUD / frontend (Integration)
- `hudState()` (health, ammo, weapon, kill feed, objective / score, spectating, ability cooldowns, prompts), HUD notifies.
- Match events, gameplay events, XP / stat awards (`drainXpAwards` / `drainStatAwards`), frontend events (match over, results).
- Participant shot FX hook, participant positions for audio emitters (`participantPositionHook`).

### Systems (per frame audio)
- Listener pose (camera, per frame — main thread, not sim state).
- Cue emitters attached to pawns (owner keys → positions), vehicle / body audio signals (nitro serial, slip angle, boost state),
  ability-actor audio state, voice requests queued by the step.
- The audio glue blocks inside `World::tick` (participant audio, character audio + cues update, level audio) are part of the step
  and run on the worker with it; they must only *queue* requests to the audio device (cues_ / mixer calls from the worker must
  be either thread-safe or deferred to the main thread). Systems to confirm which calls touch the device.

## The snapshot

`PresentedState` (double-buffered: `front` read by the main thread, `back` written at the end of a step on the worker, swapped at
join):
- per pawn: id, visible, form, transform data for `meshMatrix` (position, yaw, draw-yaw offset excluded — that is per frame on the
  main thread for the local pawn), partner / arm visibility + matrices, current + previous palettes (or CPU vertices), palette
  serial, weapon visibility / socket / weapon palette / charge;
- the actor lists above (projectiles, ability actors, pickups, destructibles, movers, objectives) as plain value arrays;
- event queues produced by the step, appended in step order: FX spawn / move, audio requests, HUD notifies, match / gameplay
  events, XP / stat awards, frontend events. The main thread consumes them in order after the join.
- HUD state values.

Palettes are already per step (`PartPalette` cur / prev, serial) — they move into the snapshot as is. Size at 64 participants:
~64 × 3 parts × ~100 joints × 64 B × 2 ≈ 2.5 MB per buffer (swap, not copy: built in the back buffer).

## Input hand-off

The main thread keeps per-frame input (camera look, latched edges). At launch it hands the worker a copy of the step input
(`MoveIntent` + latched edges + camera yaw / pitch at the boundary — exactly what `applyToPawn` consumes today) and clears the
latches, as `applyToPawn` does now. The worker never reads `PlayerController` frame state directly.

## Determinism

- The step's computation and order are unchanged; only *where* it runs changes. Inputs are captured at the same boundary
  (variant A: the same frame inputs as today; the step simply completes later).
- Nothing on the main thread mutates simulation state while the step runs: the main thread reads only `front`.
- Sim RNG, worker-pool tasks and event order are as today. DETERMINISMTEST (60 vs 240 fps) must stay 2/2 with WFC_ASYNCSTEP=1.
- WFC_SIMTHREADS=0 or WFC_ASYNCSTEP=0: today's synchronous path (the snapshot is filled and swapped inline), for A/B.

## Work split

- Gameplay: `PresentedState` fill at step end; `World::draw` from `front`; Character / WeaponMesh draw from snapshot data; input
  capture at launch; join / swap; WFC_ASYNCSTEP switch; DETERMINISMTEST + a new ASYNCSTEP test (sync vs async event logs identical).
- Rendering: confirm the read list (above) is complete for the renderer; nothing else reads sim state.
- Systems: audio calls from the step → thread-safe or deferred queue; listener stays main-thread.
- Integration: glue reads (HUD / frontend / awards / shot FX / positions) from the snapshot and event queues.

## Open questions

1. Variant A vs B (latency vs complexity).
2. Systems: which cue / mixer calls inside the step touch the audio device thread-unsafely?
3. Frontend: does any UI code read World directly during a frame outside `hudState()` / events?
