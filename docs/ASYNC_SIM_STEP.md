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

---

# v2 (2026-10-07) — reviews folded in, decision recorded, API

## Decision (Integration)
Variant **B** is the default: no added input latency. Variant A may land first behind `WFC_ASYNCSTEP=1` (off by default) as a
stepping stone. `WFC_ASYNCSTEP=0` = today's synchronous path. The sync-vs-async event-log equality test is required.

## Variant B: the split
A step today runs: pre (presentation snapshot of prev state) → abilities → match → **local controller `applyToPawn` (local
movement / weapon / firing)** → bots → participants' movement + animation → participant weapons → ram / separation → FX / audio /
hazards / projectiles / actors / glue.
- **Prefix (main thread, synchronous at the boundary):** everything up to and including the local controller step. The local pawn's
  presented state is captured right after it, so the local player's movement / firing shows with today's latency.
- **Remainder (worker, while the main thread renders):** bots onward. The next boundary joins first, then runs the next prefix:
  the operation order is exactly the synchronous one, so results are identical (DETERMINISM, the new ASYNCSTEP test, and
  Experimental's sim-determinism.ps1 lockstep).
- The remainder writes live state the local pawn shares (damage, knockback, rams, pickups): the main thread draws the local pawn
  from its prefix snapshot, never from live state during the remainder.
- Presentation: the local pawn is shown one step ahead of the participants (k+1 vs the k-1→k blend). Standard practice; the local
  player is never interpolated against data it does not have yet.

## Review results
- **Rendering:** list complete, plus:
  - the renderer calls back into game state: `setVisibilityQuery` → `segmentHit(a, b, t)`, the 3-argument overload, which is
    **static geometry only** (no moving / dynamic sets). Static triangles are immutable during a match, so concurrent reads from the
    main thread are safe. Requirement: keep that callback on the static-only overload (never the dynamic sets the step mutates).
  - per frame, also snapshot: map presentation (mover / Matinee transforms, material params), draw-owner / character colours /
    per-owner draw params (Defrag, Overheat), reticle, attached / looping FX transforms (beams, trails), projectile FX positions.
  - decals / impact marks and map FX: events only (no world queries). Loading (`setLoadYield`) is main-thread only: no loads
    during an async remainder.
- **Systems:** the audio device (Win32Audio) is thread-safe (device mutex). `SoundCues` / `SoundMixer` / `LevelAudioHost` /
  `AbilityAudio` / `MatchAudio` are not internally synchronized: they run inside the step (worker) and must not be touched by the
  main thread while a remainder runs. Fenced main-thread entry points (queued to the join point): `applyProfileVolumes`,
  `setGroupVolume`, `preloadSelectionAudio`, `preloadWeaponAudio`, `setPlayerVehicleWeaponAudio`; travel (`loadMapAudio` /
  `unloadMapAudio` / `resetSystemsForMatch`) happens outside matches. The device listener stays main-thread per frame; World's
  `listenerPos_` is in-step state.
- **Frontend / Integration glue (Application_Frontend.cpp):** reads move to the snapshot; writes become commands.

## API
```cpp
// Filled at the end of each remainder (worker) into the back buffer; swapped at the join. The main thread reads only front.
struct PresentedFrame {
    std::vector<PresentedPawn> pawns;              // transforms, form / partner / arm, palettes cur + prev + serial, weapon pose
    PresentedPawn localPawn;                       // from the prefix (variant B)
    std::vector<PlayerRow> players;                // name, team, score, kills, deaths, alive, kind, level, specialty, chassis, colours
    std::vector<std::pair<int, core::Vec3>> positions;   // by match player
    HudGameState hud; HudFrameBits hudFrame;       // health segment tops, mag / reserve, aim state
    MatchSnapshot match;                           // state, mode tag, team scores, elapsed / remaining time, GRI objective fields
    // step-ordered queues (this snapshot's steps only; consumed once on the main thread):
    std::vector<MatchEvent> matchEvents; std::vector<GameplayEvent> gameplayEvents;
    std::vector<XpAward> xpAwards; std::vector<StatAward> statAwards;
    std::vector<FxEvent> fxEvents;                 // map / weapon / shot FX spawn / move / stop, decals
    MapPresentation map;                           // movers, materials, draw owners / params, reticle, looping FX transforms
    // DEV / QA (filled only while the panel asks): bot labels, bot list rows
};
const PresentedFrame& World::presented() const;   // main thread, between joins

// Commands from the main thread, applied in submission order at the start of the next prefix (deterministic).
struct SimCommand { enum Kind { SelectCharacter, Qa, LookSettings, AudioVolumes, AudioPreload, TestDamage } kind; /* payload */ };
void World::submit(SimCommand c);
```
- Integration adapts `routeMatchToFrontend` and the QA panel to `presented()` + `submit()`; Systems' fenced calls go through
  `submit(AudioVolumes / AudioPreload)`.
- Gameplay: `World::draw` from `presented()`; prefix / remainder split; snapshot fill; the ASYNCSTEP test.

## Order of work
1. Gameplay: PresentedFrame + World::draw from it, still synchronous (no behaviour change; the snapshot is filled inline). Lands
   first so Integration / Frontend / Systems can move to `presented()` / `submit()` with nothing async yet.
2. Integration / Frontend / Systems: adopt `presented()` / `submit()`.
3. Gameplay: the async remainder behind WFC_ASYNCSTEP (B), tests, then default on after Experimental's fps / latency check.

## Step (1) as landed: `World::presented()` / `submit()` / `consumePresented()` — migration table

Filled at the end of every `World::tick` (synchronous for now). Call `world.consumePresented()` once per frame after reading the
queues (match / gameplay events); award drains clear their own queues.

| old call (Application_Frontend.cpp / glue) | new |
|---|---|
| `world.matchEvents()` (last step only) | `presented().matchEvents` (every step since the last consume, in order) |
| `world.match().gameplayEvents()` (whole record) | `presented().gameplayEvents` (new since the last consume) |
| `world.drainXpAwards()` / `drainStatAwards()` | unchanged names (now drain `presented().xpAwards` / `statAwards` + anything pending) |
| `match.players()[i]` (name / team / score / kills / deaths / alive / kind / level / specialty / chassis / selection) | `presented().players[i]` (a `MatchPlayer` copy) |
| `match.teamScore(t)`, `match.elapsedTime()`, `match.remainingTime()`, `match.state()`, `match.settings().modeTag` | `presented().teamScore[t]`, `.elapsedTime`, `.remainingTime`, `.matchState`, `.modeTag` |
| `player().pawn().position()`, `matchOpponents()[k]->position()` | `presented().positions[player]` when `presented().present[player]` |
| `world.hudState()` | `presented().hud` |
| `player().pawn().health()` / `segmentTop(i)` | `presented().localHealth`, `.localHealthMax`, `.localSegmentTops[i]` |
| `player().pawn().weapon()` (magSize / reserveMax / def->id) | `presented().localMag`, `.localReserveMax`, `.localWeaponId` |
| `player().controller().hudAimState()` | `presented().aim` |
| `localChassis()` | `presented().localChassis` |
| `match().selectCharacter(me, cs)` | `world.submit([=](World& w) { w.match().selectCharacter(me, cs); })` |
| QA actions (`qaRespawn`, `qaKillAllBots`, ...) | `world.submit([](World& w) { w.qaKillAllBots(); })` etc. |
| `applyLookSettings(world.player().controller(), profile)` | `world.submit([=](World& w) { applyLookSettings(w.player().controller(), profile); })` |
| Systems: `applyProfileVolumes` / `setGroupVolume` | DIRECT (device-global, also used in menus; the sound groups are thread-safe, agents/systems e36b5a3) |
| Systems: `preloadSelectionAudio` / `preloadWeaponAudio` / `setPlayerVehicleWeaponAudio` (mid-match) | inside `submit` (via Integration's `applyLoadout` migration; mid-match `queueSelectionAudio`) |
| TEST lifecycle `applyMatchDamage(...)` | inside `submit` |
| QA reads (`botBrains()`, `qaBotLabels()`) | unchanged for now (DEV only; snapshot in step 3) |
| `match.starts()[idx].pos` | unchanged (static per match) |

Load / unload (`launchMatch`, `loadMapAudio`, ...) stay direct calls: no step runs then.

## Step (3) as landed (2026-10-07, agents/gameplay): on by default since the default-on commit (`WFC_ASYNCSTEP=0` or `WFC_SIMTHREADS=0` = synchronous)

Validation before default-on: WFC_ASYNCSTEPTEST (sync == async), DETERMINISM, BOTTEST and mode suites with async (Integration 09c);
Systems audio cue counts / leaks PASS; Experimental 05db936 Streets: join wait avg 0.004-0.037 ms, max 0.2 ms at 32 v 32 uncapped,
p90 4.29 -> 3.84 ms (10 v 10), 9.22 -> 7.69 ms (32 v 32); WFC_LATENCYPROBE: the local action shows in the input frame in both modes.

Simpler than the snapshot plan above: the background part starts **after** `World::draw` and is joined at the start of the next
frame, so drawing never overlaps the step and needs no snapshot. The step hides behind `endFrame` / present / HUD / audio.

```
frame:  world.joinStep();                       // finish the background part launched last frame; publish presented()
        world.handleInput(input, realDt);
        for each due step: world.tickPrefix(dt)  // local part (async on) - else world.tick(dt)
        ... camera, world.draw(renderer) ...
        world.launchStep();                     // background part on the sim thread (or inline when async is off / no match)
        renderer.endFrame(); HUD / frontend (presented() only); present; audio
```

- **Local part** (`tickPrefix`, main thread): submitted commands, body / weapon preloads, map state, abilities, regen, match
  (spawns, deaths, score), awards, the local controller (`applyToPawn`: local movement / firing / hitscan). No added latency.
- **Background part** (`stepRemainder` + the `presented()` fill): bots, participants' movement / animation / weapons, camera
  collision, ram / separation, local animation / weapon presentation, FX, audio glue, hazards, projectiles, actors.
- Order of operations is exactly `tick()`'s: a second `tickPrefix` in one frame (catch-up) finishes the pending background part
  inline first. `World::tick` = `tickPrefix` + `joinStep`. WFC_ASYNCSTEPTEST: a seeded 32 v 32 TDM, sync vs async (1-4 frames per
  step, a real draw between the parts), identical event logs and per-step pawn states (60 s).
- `presented()` is double-buffered: the background part fills a back frame, `joinStep` publishes it (unconsumed queues of the front
  stay first). Reads of `presented()` / `consumePresented()` / `drain*Awards()` / `submit()` are safe at any time on the main thread.
- **While `world.stepRunning()` the main thread must not touch simulation state** (World / Match / pawns / controller / audio glue
  objects). Lifecycle entry points (`load`, `launchMatch`, `startLocalMatch`, `resetForNewLevel`) join first themselves.
- Hooks fired from the background part (`participantShotFxHook`, `participantAbilityHook`) are queued and replayed in order on the
  main thread at the join. `weaponFireHook` / `repairBeamHook` (local firing) run in the local part, on the main thread.
- GL / asset loads stay on the main thread: held weapons of every pawn are preloaded in the local part; a model first needed by the
  background part is loaded at the join and used from the next step (presentation only).
- Fixed on the way: the skeletal recoil phase used `std::rand` (per thread in the UCRT, shared with FX); grenades leave the hand
  bone, so it reached the simulation. Now a per-pawn deterministic seed.
- Presentation: on the frame that runs a step, participants / FX / projectiles show the previous step (exact, unblended) and the
  local pawn its new position; the next frame shows the completed step. At 300 fps the boundary frame's error is < 0.2 step.
- WFC_ASYNCLOG: background-part time and the main-thread join wait (the part not hidden by the frame), every 300 steps.
