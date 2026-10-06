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
- `WFC_BOTNAVTEST` (7/7 on 9 maps; `WFC_MAP=<map>`): anchors on the nav, paths between anchors for radius 1.75 / 2.0, corridors on the mesh, A* cost.
- `WFC_BOTOBJTEST` (12/12): bots in KOTH / DOM / CTF / EXT (`WFC_BOTOBJTEST_MODES`, `_SECS`).
- Diagnostics: `WFC_BOTLOG=1|<player>` traces each bot once a second.
