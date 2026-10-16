# Offline multiplayer bots: Gameplay API (Pass 25c, 2026-10-06)

The shipped versus game had **no bots** (RE: no MP bot controller; bBot PRIs hidden from the scoreboard; MaxPlayers 10).
Everything here is a **PC ADAPTATION** for offline Private Match, built on original rules and data where they exist.

## Launch (Frontend contract)
- StartLevel URL options, parsed by `MatchLaunch::fromURL` into `MatchLaunch::bots` (`game::BotLaunch {friendly, enemy, difficulty}`):
  - `?BotsFriendly=<n>`, `?BotsEnemy=<n>`, `?BotDifficulty=<0|1|2>`.
- Defaults are 0 / 0 / 1, so with no options there are no bots.
- Capacity is `MatchSettings::maxPlayers` (16) and `MatchSettings::maxPerTeam` (8). These are a PC ADAPTATION; the original
  was 10 / no per-team cap.
- `World::addBots` clamps the request to that capacity.
- The previous match's bots leave when the next match launches (`World::removeBots`).

## Participants
Bots are ordinary `game::MatchPlayer`s, `kind == ParticipantKind::Bot`. Fields:
- `name`: a generated handle, unique in the match;
- `team`;
- `selection`: a custom class preset, i.e. the specialty, the default faction bodies and the legal MP preset weapons. The
  four classes are spread per team before any repeats;
- `chassis` / `specialty`: resolved at spawn;
- `level`: the displayed level, 4–99 depending on difficulty;
- score / kills / deaths / assists.

They select a character, spawn, take damage, die, respawn in waves and score through the same Match code as players. Every
kill / spawn / objective is in `Match::gameplayEvents()` with `ParticipantKind::Bot` in the snapshots.

Kill scoring with bot victims is gated by `MatchSettings::botVictimsScore` (PC ADAPTATION; false = the original ShouldScoreKill).

Systems (audio preload at match load): `Match::players()` → `selection` / `resolveChassis(selection, match.faction(p))` gives each
participant's body and weapons right after `launchMatch`.

## Presentation hooks
- `World::participantShots()`: this step's non-local shots, as {player, weapon id, from, to, impact}. Use them for bot
  tracers, muzzle flashes and fire sounds.
- Each bot pawn's `weapon().shotSerial` / `reloadSerial` also increment as for the local pawn.
- **Not drawn yet [PARTIAL]:** bots' held robot weapon meshes and their muzzle / tracer FX. Only the local pawn draws a held
  weapon today.

## AI (src/game/BotBrain.*, BotNav.*, WorldBots.cpp)

### Navigation (`BotNav`)
- Loads `Maps/<map>/bot_nav.json` from AssetTools' bot_nav.py for any map. Nothing is map-specific.
- Cell lookup; A* over portals plus jump_up / drop_down links.
- Corridors are string-pulled (funnel) through radius-shrunk portals.
- Robot clearance is a cost, because `clearance_m` is a per-cell minimum.
- Vehicle form uses the `vehicle_capable` layer only.

### Decisions
Bots think every 0.25 s, staggered. Each decision covers:
- **Perception:** a sight cone and range, line of sight on the weapon collision, at most 3 ray checks per think.
- **Target choice:** closest visible enemy, with stickiness, retaliation and a low-health bias.
- **Goal**, from the shared objective layer (`BotGoalKind` Roam / Attack / Defend / Capture / Hold / Contest / Retrieve /
  Return / Support):
  - TDM: Attack the team's latest sighting, snapped to the nav;
  - otherwise "hunt" roam toward a random enemy's area with 25 m of fuzz (70 % of roams), or wander between anchors.
- **Weapon choice:** the weapon whose DesiredFiringRange band is nearest the target's band, after a 0.5 s hold.
- **Form:** travel in vehicle form on vehicle-capable cells when the goal is more than 45 m away; fight as a robot. Jets
  stay robots for now.

### Every step
- **Steering:**
  - corners with look-ahead;
  - jump links;
  - displacement-based stuck recovery: jump, then repath, then a new goal;
  - off-mesh recovery.
- **Combat movement:** hold the desired band, strafe with a walkability check.
- **Aim:** ease toward the target's TargetableLocation (projectiles lead) plus a tracking error.
- **Fire:** pace with the AI WEPDATA BurstRanges for the target's band [CONF RE], then fire the real Weapon through
  `fireHitscanAs` / `spawnProjectile`.
- **Reloads:** when empty, and opportunistically out of combat.

### Original vs adaptation
- **CONFIRMED (RE addendum 7):**
  - range bands (TnAiController.RangeSet: Touch 0–175 UU … OutOfRange 7001+);
  - burst-band mapping (Striking / Close → Short, Medium → Medium, else → Long);
  - BurstRanges and DesiredFiringRange values;
  - AI aims at TargetableLocation; inaccuracy = weapon spread + bursts.
- **PC ADAPTATION:** difficulty (`BotSkill`: reaction, turn rate, aim error, FOV, sight, memory, strafe, burst / pause
  scale), reaction delay, team callouts, hunt roaming, form choice.

### Also implemented (25e-25g)
- **Objective modes:**
  - KOTH: hold the zone;
  - DOM: capture / contest / defend nodes;
  - CTF: retrieve / capture / support / return / defend;
  - EXT: retrieve / plant / defuse / defend.
  Missions keep their route through combat.
- **Melee and grenades:** both go through the shared participant paths (startMeleeFor / tickMeleeFor / releaseGrenade).
  - Melee rush toward an enemy within 20 m, holding fire, then the strike with the assist lunge.
  - Grenade tosses at 8-30 m.
- **Repair:** Scientist bots heal wounded teammates with the Repair Ray.
- **Pathfinding:** time-sliced A*, approach cells, and corridor validation.

### Not yet [PARTIAL]
- bot abilities (Warcry, Cloak, Hover, ...): the ability effects are local-player code paths;
- heal grenades;
- jet flight (the AssetTools air layer exists, unused);
- vehicle boost / vehicle-form combat;
- bots' held weapon meshes / FX.

## Tests
- `WFC_BOTTEST` (25/25 + phase-2 grenade / repair checks): human + 3 / 4 bots, then 7 v 8 HARD, 120 s each. Checks the roster, spawns, movement, combat,
  stuck time, environment deaths, AI cost, completion and MatchEnd.
  - **Known artefact:** run it with the default sim threads. Under `WFC_SIMTHREADS=2` (a cap used for CPU-only batch runs), the "AI cost per step" check FAILS for Classic AND Smart. 2026-10-13: avg 0.10-0.14 ms per bot, max 8-22 ms; the same build passes 39/39 for both AIs with default threads. That is the thread cap, not a code regression; do not chase it.
  - Other machine load also inflates the max; the 2026-10-13 regression sweep saw 13.85 ms on a busy machine and 4.59 ms quiet.
- `WFC_NAVQTEST` (measurement): bot pathing quality on the loaded map. Per bot-minute: slow %, heading reversals, off-mesh %, 5 s idles, stucks / repaths / failed searches. `WFC_NAVQ_SECS`, `WFC_NAVQTRACE=<player>`.
  - Run it under `WFC_NULLRENDER=1` (CPU only: no window, GPU or audio device); single matches mislead.
  - **Yardstick (Integration decision 2026-10-16):** 24 seeds per arm (`WFC_SEED` 1-24, about 5 s per run), gated per column with a z-score.
    - z = delta / sqrt(SE_ref² + SE_arm²) over the per-seed values (`work/pass29/navq_cmp.js` on agents/gameplay).
    - The reference for Smart on 7d9edf78 is `work/pass29/c09` (seeds 1-12 = the 069a0a8f baseline lines, seeds 13-24 new).
    - The older |mean(s7-12) - mean(s1-6)| noise reference is retired: it was implausibly small on some columns (Debris slow 0.07 %).
    - The same build differs between seed groups 1-12 and 13-24 by as much as the deltas it was used to call (Gorge stucks 1.44 vs 1.69, Streets off-mesh 4.91 vs 5.94 %).
  - Results 2026-10-13: docs/handoff/GAMEPLAY_PATHING_2026-10-13.md.
- **Renderer coupling (audit 2026-10-13):** bots and gameplay read nothing decision-relevant from the renderer.
  - The one render-time read is `drawOwnerRenderAge` in `participantShotFxHook`: the presentation-only impact-squib relevance gate, used in extended lobbies or with `WFC_IMPACTRELEVANCE=1`.
  - Participant culling is hard-wired off.
  - Bone and socket reads use the per-step animation pose.
  - The renderer's visibility query calls const collision only.
  - So a `WFC_NULLRENDER=1` boot simulates the same match as a GL boot (DETERMINISM digest identical, 2026-10-13). Any renderer-mode SIMHASH divergence comes from another sim input (RNG / step clock).
- `WFC_BOTNAVTEST` (7/7 on 9 maps; `WFC_MAP=<map>`): anchors on the nav, paths between anchors for radius 1.75 / 2.0, corridors on the mesh, A* cost.
- `WFC_BOTOBJTEST` (12/12): bots in KOTH / DOM / CTF / EXT (`WFC_BOTOBJTEST_MODES`, `_SECS`).
- Diagnostics: `WFC_BOTLOG=1|<player>` traces each bot once a second.

## Tipped bot vehicles (2026-10-14): Smart self-rights by default; the Gorge movement cost was noise (2026-10-16)

**User report (package 4):** "a car tipped over and it was still moving forward even though the car was tipped forward".

**Status:** fixed for **Smart** by default (Integration decision 2026-10-14, overriding the strict pooled gate).
- Smart bots get the AI-path righting with the contact-relative gate (mode 0 + REL below). Classic stays on the player path, byte-identical to 30b2fa74.
- The cost is a measured Smart movement regression on Gorge, which is the explicit target of the next Smart pathing round (after the playtest, with the follow reversals).
- Package-5 notes: "Smart bot vehicles self-right (original AI behaviour); Gorge movement slightly worse, next round".

### What the original does

From the decompiled scripts:
- There is no forced transform, auto-reset or roll clamp.
- **Bots** in hover form run the AI path, not the player simulation: TnCarForm / TnTankForm.Hovering.DoUpdateAi calls UpdateFromLocationAndRotation, which uses:
  - HoverPhysics.MoveToTargetLocation: world-horizontal, at AiMaxLinearSpeed / AiMaxLinearAcceleration;
  - HoverPhysics.RotateToTargetOrientation: toward DesiredRotation (the focus direction, Pitch 0, roll 0). Pitch and roll are only included while IsUnstable (car / truck: absolute pitch or roll over 30 deg) or !IsStable (tank).
- So a tipped AI hover vehicle is turned back to level at AiMaxAngularSpeed / Acceleration (authored, rad/s and rad/s^2: car 12 / 12, truck 20 / 20, tank 6 / 6).
- **Players:** UpdateTurn rights the body only with no spring contact or upside down (up.Z < 0.01).
- **Driving:** DoUpdateAi is a no-op in the Driving state.

### What the rebuild did (Classic still does)

- Bots run the player hover path.
- Pitch and roll are clamped to +-1.2 rad, so "upside down" is unreachable. A tipped body that keeps a spring contact is not righted, and its body-frame strafe keeps pushing.
- The real case (WFC_TIPTEST, from a Gorge match): Car2 with its nose against a 4.7 m step, held nose-down by the rear springs. It stays at -60..-67 deg on the player path, whichever way it steers.

### The fix

Per bot, MoveIntent.aiTip. **Smart default: mode 0 with the contact-relative gate.** Classic: -1 (the player path). `WFC_BOTTIP=<-1|0|1|2>` (+ `WFC_TIPREL=0/1`) overrides it for every bot, for measurement:

| Mode | Behaviour |
|---|---|
| 0 | RotateToTargetOrientation on pitch / roll while unstable |
| 1 | Mode 0, plus MoveToTargetLocation toward the move direction while unstable |
| 2 | Mode 0, plus MoveToTargetLocation toward the bot's next nav-mesh waypoint once unstable for over 1 s |

The contact-relative gate (REL) is [PROV], a deviation from the original's absolute IsUnstable. Tipped = body up vs the averaged spring contact normal past 30 deg; with no contact, the absolute rule applies. A hover vehicle climbing a ramp steeper than 30 deg with its springs on the ramp is not "tipped", so it is not pulled level against the slope. The absolute gate regressed Smart on Debris and Gorge (9 columns).

**What is not ported:** AI translation while stable, and AI yaw.

### Measurements

**WFC_TIPTEST** (real Gorge tip):

| Path | Back (the bots' stuck reversal) | Left | Right | Forward into the wall |
|---|---|---|---|---|
| Player (the default) | tipped 6 s | tipped 6 s | tipped 6 s | tipped 6 s |
| Modes 0 / 2, either gate | level at 0.35 s | level at 0.45 s | level at 0.82 s | unrecoverable on any path |
| Mode 1 | | | 4.72 s | |

**Tipped time** (Smart, hover episodes over 1 s, seeds 1-3, summed; "old" is the default):

| Map | Old | Mode 1 | Mode 0 | Mode 2 | Mode 0 + REL | Mode 2 + REL |
|---|---|---|---|---|---|---|
| Streets | 69.8 s | 43.1 | 30.7 | 32.2 | 23.0 | 23.0 |
| Molten | 41.9 s | 10.6 | 23.0 | 18.5 | 12.7 | 13.0 |
| Debris | 20.3 s | 14.3 | 12.9 | 12.9 | 9.5 | 9.5 |
| Gorge | 98.2 s | 36.5 | 65.9 | 65.3 | 31.3 | 64.1 |
| **Total** | **230 s** | **105** | **132** | **129** | **76.5** | **109.6** |

**Pooled 12-seed NAVQ gate:** off s1-12 vs mode s1-12, both AIs. Noise reference per column = |off s7-12 - off s1-6|; a column regresses when its delta exceeds that.

| Variant | Smart regressing columns | Classic regressing columns |
|---|---|---|
| Mode 0 | 9 | 4 |
| Mode 2 | 9 | 4 |
| Mode 0 + REL | 3 | 11 |
| Mode 2 + REL | 6 | 11 |

- Smart's Gorge regression appears in every variant. For example, mode 2 + REL: stucks 1.26 -> 1.59 / bot-min, off-mesh 4.50 -> 5.64 %, reversals 4.24 -> 5.10.
- Caveat: the noise reference is one 6-vs-6 difference per column, and some are near zero, so individual calls are weak. The Gorge pattern is consistent across variants.

**Shipped Smart default, pooled 12 seeds** (mode 0 + REL; off -> on, with the noise |off s7-12 - off s1-6| in brackets; **bold** = beyond noise):

| Map | slow % | reversals / travel-min | off-mesh % | idle5 / bot-min | stucks / bot-min |
|---|---|---|---|---|---|
| Streets | 5.14 -> 5.82 (1.48) | 4.80 -> 4.70 (1.07) | **3.85 -> 4.91 (0.25)** | 0.16 -> 0.16 (0.03) | 1.41 -> 1.56 (0.30) |
| Molten | 1.92 -> 1.60 (0.17) | 3.32 -> 3.22 (0.23) | 1.90 -> 1.75 (0.26) | 0.24 -> 0.21 (0.05) | 0.68 -> 0.53 (0.01) |
| Debris | 4.04 -> 3.68 (1.38) | 2.94 -> 2.90 (0.08) | 2.08 -> 1.98 (0.33) | **0.10 -> 0.12 (0.00)** | 0.98 -> 1.02 (0.20) |
| Gorge | 4.10 -> 5.11 (1.03) | 4.24 -> 4.84 (0.71) | 4.50 -> 5.20 (1.00) | 0.14 -> 0.18 (0.06) | **1.26 -> 1.44 (0.06)** |

- **Gorge** is worse on every column; only stucks clears its (small) noise reference. This is the next round's target.
- Streets off-mesh is also beyond noise. Molten improves.

**Shipped default** (integration 56290b7d + this patch, run on 30b2fa74's identical sources):
- Classic NAVQ: 24/24 byte-exact vs the 30b2fa74 baseline.
- Smart NAVQ: reproduces the measured mode 0 + REL runs exactly (8/8 checked). The pooled table above (mode 0 + REL: Smart 3 regressing columns) is the shipped behaviour.
- BOTTEST: Classic 39/39; Smart 39/39 on two seeds (grenades 7 and 4).
- DETERMINISM: 2/2 for both AIs.
- 31v32 AI cost: 0.23 ms.
- TIPTEST: 3/3.

When the righting is forced onto Classic (WFC_BOTTIP), the SIMHASH divergence starts one step after the first unstable bot hover step (Gorge s2 713 -> 714, Molten s3 770 -> 771, Streets s1 1127 -> 1128).

### Resolved 2026-10-16: no movement cost (`docs/handoff/GAMEPLAY_SMART_ROUND_7d9edf78.md`)

Self-righting on (the shipped default) vs `WFC_BOTTIP=-1`, Smart, 24 seeds, on integration 7d9edf78. No column is significant:

| Map | Change | z |
|---|---|---|
| Gorge | stucks 1.54 -> 1.57 | 0.1 |
| Gorge | off-mesh 5.11 -> 5.81 % | 0.9 |
| Gorge | slow 4.55 -> 5.52 % | 1.45 (largest) |
| Streets | off-mesh 5.02 -> 5.42 % | 0.4 |

- Molten and Debris lean better with righting on.
- Integration decision: self-righting stays as shipped.
- The 12-seed regressions in the tables above were within the seed-group spread.
- Squad follow reversals (the other item of that round) are closed as a design item: `docs/handoff/GAMEPLAY_SQUAD_SLOT_REDESIGN.md` (after package 6).
- Still open, not a regression: porting the full AI translation as one unit.

### Diagnostics

| Variable | Use |
|---|---|
| WFC_TIPLOG | Tip episodes and the first unstable bot hover step |
| WFC_TIPTEST | The real Gorge tip case (with WFC_MAP=MP_UND_Gorge) |
| WFC_BOTTIP / WFC_TIPREL | Override the per-bot mode / gate (measurement) |
