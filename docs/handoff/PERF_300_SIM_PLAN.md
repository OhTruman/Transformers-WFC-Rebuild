# Gameplay sim plan for the 300 FPS target (campaign + Escalation at original settings), 2026-10-24

Status: a plan, written during the a2e9fbd9 gate freeze from code reading only. Work starts after the package ships.

**Target:** worst sim tick **under 1.5 ms** on every campaign chapter and every Escalation map, with the per-step SIMHASH stream
identical to the pre-change build. Lossless only: no AI level of detail, no fewer decisions, no changed think rate, no budget that
moves a search to another step.

**Why 1.5 ms:**
- At 300 fps a frame is 3.33 ms and the 60 Hz step runs on about one frame in five.
- With the async step (tickPrefix on the main thread, the remainder on the sim thread overlapping the draw, joined at the next frame
  top), a step longer than the frame becomes a hitch.
- 1.5 ms worst tick leaves the prefix and the join margin inside one 300 fps frame.

## 1. Measurement (first, before any change)

**Per-step log `WFC_TICKLOG=<csv>` (new, diagnostic only).** One row per sim step:
- the step index;
- the participant / live-horde count;
- the total step ms (prefix + remainder);
- every TICKPROF slot's ms for that step (not the 300-step average WFC_TICKPROF prints today);
- the step's counters: path searches started / expanded nodes, bot thinks run, spawn tries, LOS rays.

**Split the "match" slot.** Today `tickSurvival` and `tickCampaign` hide inside slot 2. New slots:
- `sv.director`, `sv.kismet`, `sv.mech` (Escalation);
- `cp.vm`, `cp.matinee`, `cp.touches`, `cp.destructibles`, `cp.interacts`, `cp.sentries/mines`, `cp.factories` (campaign);
- `hazards`;
- `fillPresented` (already 29) split into `fill.markers` / `fill.hud`.

**Rows (headless WFC_NULLRENDER, a fixed seed, WFC_SIMHASH on, logs per map):**

| Mode | Maps | Run | What it shows |
|---|---|---|---|
| Escalation | Remnant, BrokenHope, Tranquillity, District, Pulse, Static | 10 min soak: the human bot-driven + seat 2, waves to about 10, no guard (real deaths) | horde size up to MaxCount, wave starts |
| Campaign | all 10 chapters | 120 s from the chapter start, bot-driven (the sweep harness) + D1 (2.1 ms today) and D5 (Omega) for 300 s | Kismet / matinee / factories |
| Control | Streets TDM 5 v 5 and 16 v 16 | 120 s | the versus baseline the async step was tuned on |

**Report per row:** mean / p99 / max step ms, the share of steps over 1.5 ms, and the top 3 slots of the worst 20 steps.

**Remnant first:** the user's capture shows about 8 % of intervals over 16.7 ms with the renderer under 9 ms, so a sim step or the
join is the suspect. The log tells whether those steps are long or whether two steps land in one frame (catch-up). Catch-up is
`tickPrefix` running a pending remainder inline: it shows as two step rows in one frame, which is an async-scheduling issue, not
step cost.

## 2. Spike sources to confirm (from the code)

**Escalation:**
1. **Bot decisions at 4 Hz** (`tickBots`): `botThink` + Smart `smartPerceive` / `smartDecide` run **sequentially** for every bot.
   Perception is mostly LOS rays to every enemy. The horde can be 25 to 113 slots, Pulse being the largest. If the think timers line
   up (horde spawned in the same step at a wave start), one step carries a large share of the thinks.
2. **Wave start:** many spawns in the fill loop (svSpawn: 5 tries each, groundHeight + capsule loop over all pawns), then each new
   bot's first think and repath request in the same window.
3. **Presented frame** (`fillPresented`, every step): a marker per pawn tag, each with an enemy-label LOS `segmentHit`, plus
   hudState's crosshair target ray. It scales with the horde.
4. **Path upkeep** (`botPathUpkeep`): already one time-sliced search at a time, so not an unbounded spike, but its per-step slice
   cost needs measuring on Pulse (7,056 cells).
5. **DLC mechanisms:** awake sentries do one LOS per candidate target per step (District, 10 sentries); laser / trap hazard toggles
   are cheap.

**Campaign:**
6. **Per-step scans:** cpTickTouches (every touch volume × every pawn), cpTickDestructibles, cpTickInteracts (every interact ×
   humans with the use cone), the factories' kill checks.
7. **The Kismet VM tick:** latent ops (Delay / Interp / Gate) walked every step.
8. **Hazards:** every hazard volume per pawn per step. 73 non-convex brushes now use ray parity behind an AABB test (A4 has 69).
   Cheap when outside the box; inside, up to 113 triangles.
9. **Squad / AI factories:** many AICD pawns with Smart off (Classic), the same think cost as above.

## 3. Lossless fixes (each one: SIMHASH stream identical over the full row, then the row re-measured)

**Fixes:**
- **A. Parallel perception, sequential decisions.**
  - Split the 4 Hz think into a parallel phase: every due bot's LOS rays and sightings into its own buffer, read-only on the World.
  - Then a sequential phase in today's order consumes them: decisions, RNG draws, goal / repath writes.
  - Identical results by construction, because rays are pure functions of the step-start state. This needs a write audit of
    smartPerceive / botThink (no shared RNG, no shared caches written during perception).
- **B. Faster A* with identical output.**
  - Flat open / closed arrays, a binary heap with today's exact tie-break (equal f: the same order as the current container), a
    stamp array instead of clears, precomputed neighbour spans.
  - Same expansions, so the same path.
  - Verify with a NAVQ-style path-dump diff over every map before trusting SIMHASH alone.
- **C. Spawn finder caching.** Each authored point's groundHeight against the static collision, computed once at match load (the
  static world does not change; movers and blockers are a separate set). The capsule loop keeps its live-pawn check.
- **D. Marker LOS in the worker pool.** The enemy-label rays are presentation (not hashed); run them in parallel inside the fill,
  results written per tag.
- **E. Campaign broadphase.** Touch volumes and interacts in a uniform grid (or a sorted AABB sweep), rebuilt when pawns move.
  - The same tests in the same order for the candidates, so identical events.
  - Destructibles: cached bounds, recomputed only when a piece changes state.
- **F. Kismet VM active list.** Tick only ops with latent state (an active list kept in insertion order) instead of walking every
  op. The same order of outputs.
- **G. Hazards:** the AABB prefilter for convex brushes too. A per-pawn "last volume" hint, checked first, as an order-preserving
  shortcut only when the result is provably the same: volumes are tested in index order, and the hint is used only to skip the
  test when the pawn is outside every earlier volume's box.
- **H. Allocation-free steps.** Reuse the per-step vectors: pending bots, eligible lists, the presented frame's players / markers
  copies; publishPresented already swaps. Measure with an allocation counter in the TICKLOG row.

- **I. Whole-step overlap (sim step k + 1 on a worker while frame N draws), SIMHASH identical.** Rendering's phase 1 decides
  whether the heavy chapters need it; designed now so it is ready.
  - **Today (variant B of docs/ASYNC_SIM_STEP.md):** `tickPrefix` runs on the main thread (commands, the local controller apply,
    abilities, match), and only the remainder runs on the sim thread, overlapping the draw. The prefix stays on the main thread
    because it reads the live `PlayerController`, which the main thread mutates every frame (camera look, latched edges).
  - **The design:** at the step boundary the main thread captures a **LocalStepInput** snapshot: everything `applyToPawn` /
    `tickPrefix` read from the controller for that step.
    - The MoveIntent, the latched presses (fire latch, dash / jump / transform / melee / grenade / pickup / reload / ability edges,
      the X-press interact flag), camYaw / camPitch, the fine-aim / guiding state, and the crosshair aim point.
    - **Exact floats, not the wire's quantized RemoteCommand** (rotator grid, 1 cm aim point), which would change SIMHASH.
    - The snapshot and the submitted `commands_` move into the step job; the worker runs `tickPrefix` + the remainder against them;
      the controller only feeds the camera on the main thread.
  - **Why identical:** the synchronous step reads exactly these values at exactly this boundary, so the sim consumes the same inputs
    at the same steps.
  - **Proof:** WFC_ASYNCSTEP=2 (whole step) against WFC_ASYNCSTEP=0 on the same seed, scripted input (WFC_AUTOWALK / AUTOFIRE /
    the player bot), with SIMHASH equal at every step. Plus WFC_ASYNCJITTER on top: no interleaving may change it.
  - **Cost:** one step of extra presentation latency for the local pawn's own movement / firing results (16.7 ms; the camera stays
    per frame). Measure it with Experimental's input-latency probe before choosing it as the default.
  - **Main-thread-only work the prefix does today, to audit and move or keep at the boundary:**
    - frontend / QA command lambdas: they enter as submitted commands;
    - the local guided missile's steering (from the controller): goes into the snapshot;
    - audio cue emission: already queued, played by the main thread;
    - campaign movie / cinematic flags read by the app: presented frame only.
  - **Join rule:** the main thread joins step k + 1 before it launches k + 2, or earlier only when a frame needs k + 1's snapshot
    (a step faster than the frame budget never stalls the draw). Catch-up steps keep today's inline order.

**Not allowed** (changes behaviour or SIMHASH): staggering think timers, think-rate changes, capping searches per step differently,
skipping LOS for distant bots, dropping horde slots.

## 4. Acceptance

- **Per row:** the worst step under 1.5 ms over the full run, and SIMHASH equal at every step to the baseline build of the same
  seed.
- **Escalation:** an ESCTEST pass on all six maps.
- **Campaign:** the chapter sweep (start + checkpoint, 0 errors) and CAMPAIGNTEST.
- **The user's numbers re-checked on the card** (Experimental's timed rows): Remnant intervals over 16.7 ms near 0 %; campaign and
  Escalation frames at 300 fps minimum at original settings.
- **Merge:** one patch per fix (A to H), each with its SIMHASH proof and before / after rows, and a FINAL_BASE_ITEMS entry for every
  sim-path change.
