# Escalation (co-op horde): gameplay design spec (Gameplay, 2026-10-17)

Status: DESIGN ONLY. The user approved Escalation for after online phase 1; no code until then.
Source of truth for behaviour: RE `RE-Workspace/work/handoff/MP_ESCALATION_RE.md` (401a49e; §5a BrokenHope complete) and the data export
`RE-Workspace/notes/data/escalation/{MP_ESC_BrokenHope,MP_ESC_Remnant,_class_defaults}.json` (map, packages, spawn_list, 35 generators,
30 zones, 43 Kismet events per map). Section numbers below in [RE §n] point there. Labels: C = confirmed by RE, H = inference, PROV =
our placeholder until a source exists.

## 1. What the original does (summary)

- Up to 4 players on the map's co-op faction (BrokenHope Autobots vs a Decepticon horde; Remnant the reverse) [RE §1].
- Loadout: primary + secondary removed, an Assault Rifle given; weapon data = SurvivalData; no difficulty setting [RE §1].
- States: PreGame (30 s BrokenHope / 10 s Remnant after the first spawn) → InWave → BetweenWaves (25 s) → InWave … endless.
  Loss when every player is dead at the same time (downed does not count). No win condition [RE §1].
- Waves spend a cost budget on enemies: fill while the alive cost ≤ the deficit threshold, one spawn per recheck (0.5 s / 3.0 s) until
  the alive cost ≥ BudgetAlive, until the wave budget is spent and the horde is dead. Past the table the last row repeats; late waves add
  horde buffs (damage taken ×0.9…0.5, damage dealt ×1.25…2.25) [RE §2, §5a].
- Enemy choice: eligible by wave window and MaxCount; any type below MinCount is forced; else uniform random [RE §2].
- Placement: zone scoring per spawn type (Standard / ShortRange / LongRange / Air / Boss), occupied zones −999, neighbour ZoneChanges,
  random among the top 3 weighted by |score|, capsule fit, 5 tries [RE §2, §5a zones].
- Economy: kills pay PowerReward to the killer (per player, 0..9999). Generators: hold Interact in the area, drain max(1, dt × DrainRate)
  per tick into a shared per-generator pool, one level per MP generator, then Kismet: unhide a pickup factory (items) or open doors /
  bridges, nav cells and spawn zones (doors) [RE §3, §5, §5a].
- Death: wait for the wave end (no timer); wave end respawns the dead and revives the living; downed + teammate revive (stock co-op)
  [RE §4].
- Map set pieces: "Wall_Crashers" zone gates after rounds 3 / 4 / 4 / 6 and crashable walls (200 damage on first pawn touch) [RE §5a].

## 2. Mapping onto our code (what exists, what is missing)

| Need | What we have | Gap |
|---|---|---|
| Game mode | MatchMode / MatchSettings::forMode, the shared Match framework (TDM / CTF / KOTH / EXT …) | a new `MatchMode::Escalation` with its own state machine (§3) and no score goal / time limit |
| Doors / bridges in Escalation state | `TransformingActors::resetForNewMatch(…, escalation = true)` keeps the authored closed start (transact 1b894dc7) | trigger by generator id instead of the versus rule; the bridge UnHide + collision on purchase |
| Nav through doorways | `BotNav::setBlocked` / `recomputePieces`, bot_nav_versus.json (AssetTools) | the closed-door nav (the Escalation start), and the per-door NavigationCell switch on purchase |
| Pickup factories | PickupFactory (health / overshield / ammo; now per seat online, 2ce68e91) | a hidden / disabled state until a generator unhides it; weapon pickup factories (none today) |
| Interact hold | Button::Interact in the seat command (held + pressed bits) | the generator area test + drain per tick, per seat, deterministic |
| Horde AI | the Classic / Smart bot brains, MatchOpponent, per-bot difficulty | the campaign enemy roster (§4); "sees every player at spawn"; no objective layer |
| Spawn finder | team starts + generated spawn points | the TnSurvivalZoneVolume set (30 per map, in the export) with scores, ZoneChanges, activation |
| Downed / revive | none in the sim (only presentation / progression strings) | the stock co-op downed state, revive interaction and timings (RE gap, §6) |
| Weapons | WeaponDef MP data (MultiplayerData) | SurvivalData weapon rows (DesiredWeaponDataType 4): AssetTools survival_weapon_data.json (escalation_player_data = the SV row, else PlayerData per RE §7.4; escalation_ai_data = AiData) |
| Destructible walls | Destructible (state, applyDamage) | the four Wall_Crashers walls + TnToggleDestructible 5491 as map actors |
| Online | the seat order, per-seat paths, lockstep (phase 1) | power per seat, generator pools, the director: all sim, all deterministic (§7) |

## 3. The director (new: `SurvivalDirector`, owned by World)

Sim state, ticked in the step (main-thread prefix, after the seat commands, by match player index where it iterates players):

- `state` PreGame / InWave / BetweenWaves / GameOver; `currentWave` (clamped to the last row) and `displayWave`; `preGameT`,
  `betweenT` (25 s); `usedThisWave`, `aliveCost`, `filling`, `fillRecheck`.
- PreGame → InWave when a human has spawned and the elapsed time ≥ PreGameTime.
- InWave each step: (1) if every human is dead (downed is alive) → GameOver("AllDead"); (2) if usedThisWave ≥ Budget and no horde pawn is
  alive → BetweenWaves; (3) else if aliveCost ≤ BudgetDeficitThreshold start filling; while filling, every FillWaveReCheckTime spawn one
  enemy until aliveCost ≥ BudgetAlive (stop filling there).
- Enter InWave: IncrementWave, "Round Begin" (level audio + Kismet zone counters), usedThisWave = 0, spawn the first enemy.
- Enter BetweenWaves: respawn every dead human (RespawnHelperRoundEnd), "Round End", revive every living human, then count down 25 s.
- Wave buffs: on each horde spawn apply the row's DecreaseDamageTaken / IncreaseDamage levels (multipliers in applyMatchDamage, like the
  existing warcry / hard-lock factors).
- RNG: the Match RNG (host-seeded online) for the enemy pick and the zone pick, so every peer draws alike.
- Game over: `TnSurvivalGameOverMessage(displayWave + 1)`; the HUD / Frontend show it; displayed wave 16 → achievement 45 (Progression).

## 4. The horde roster

Per map from `spawn_list` (BrokenHope 13 entries, Remnant 7 types): MinWave / MaxWave / MinCount / MaxCount / Cost / spawn type / loot
class / PowerReward [RE §2, §5a]. Each entry needs a body + weapon + AI template:

| Enemy | Asset status (ExtractedAssets/content) | Plan |
|---|---|---|
| Soldier MG / Rocket / Shotgun / CQC (Decepticon soldiers) | campaign soldier bodies (Minion1–3 under VerticalSlice/Characters are the nearest present) | AssetTools: confirm the SV soldier classes' meshes; weapon per entry |
| Soldier Jet | Jet chassis exist | Air zones only |
| Spider (DES_COR_EnergonSpider_p?) | to confirm | RE / AssetTools |
| Brute (TR_BruteDec_*, TR_BruteAut_*, TR_BruteNeu_ROBO_p) | present | body + its backpack rocket weapon (BruteRocketLauncher) |
| Titan | to confirm | RE / AssetTools |
| Soldier Tank (wave-15 boss, Boss zones) | Tank chassis exist; the SV boss class to confirm | RE |
| HoloBrute (Remnant) | Sentinel_Hologram_SpawnBrutes strings present | RE / AssetTools |

AI templates (RE §7, c0640fb: each TnAiCharacterDefinition = GOAP sets, chassis health / speed, weapons, pawn class):

| Enemy | Health | Notes |
|---|---|---|
| Spider | 20 | 2000 UU/s |
| Soldier MG / Rocket / Shotgun (SoldierCar) | 150 | AR / Burst / HeavyPistol, RocketLauncher, EmpShotgun; vehicle-form weapons too |
| CQC | 90 | |
| Jet | 250 | TnAiJetSoldierPawnContent |
| Titan | 1250 | GatlingGun |
| Brute | 100000, HeavyDamageThreshold 300 | HalberdAxe (Autobot Brute: WarHammer); killed through its weak point (RE H) |
| Boss Tank | 6000 | speed 720, TnTankAiPawn, Tank_AiActionSet |
| HoloBrute | | opacity 0.9, CeremonialStaffHolo |

AI behaviour: the GOAP sets are not ported. Until they are (PROV): the Classic bot brain with the entry's weapon and health, `SeeEnemy`
on spawn (every human known), no objective layer, Smart off. The horde never respawns; a dead enemy frees its cost.

Loot (RE §7): kills drop TnAbilityResourcePickup (+3 ability resource each): Soldier 4, Brute / Titan 7, Tank 12 + a 50 % rare
GatlingGun or HeavyRocketTurret; +1 per kill, +5 headshot, +1 melee, +1 explosion; Spider / Jet / HoloBrute drop nothing. Power comes
only from PowerReward.

## 5. Generators, doors, zones

- Data: the export's 35 generators per map (cost, DrainRate, area volume, target events) and 43 events (their Kismet targets: doors,
  bridges, factories, TriggerVolumes, DynamicBlockingVolumes, NavigationCells, zones).
- Drain: per seat with the Interact bit held while inside the area, `max(1, dt × DrainRate)` capped at the remaining cost, from that
  seat's power into the pool; up to 4 seats feed one pool; "fail" sound when a seat runs dry; one level, then the event fires once.
- Effects we implement as data, not a Kismet VM: `UnhideFactory(id)`, `TransformDoor(id)` (TransformingActors), `ShowBridge(id)` (UnHide +
  collision + transform), `EnableNavCells(ids)`, `DisableVolumes(ids)` (TriggerVolume / DBV collision off), `EnableZone(id)`.
  Pairs replay harmlessly (doors already open). Keep RE's asymmetries (zone 16285 from 7839 only, 10523 from 4199 only) and skip the
  orphan event 3551.
- Wall_Crashers: the round counter enables the four touch volumes after rounds 3 / 4 / 4 / 6; a zone is on while any pawn touches its
  volume (H: Pawn filter = players and AI); crashable walls take 200 on the first pawn touch.
- AssetTools needs: the TriggerVolume / DBV / NavigationCell geometry and the generator areas per map (the export has ids and targets;
  geometry comes from the map packages), plus a bot_nav variant with every door closed.

## 6. Players: loadout, death, downed, revive

- Spawn: Assault Rifle only (SurvivalData values), class abilities as in versus (H: ApplySpecialtyBuffs is off in survival games,
  ChassisDef.h note).
- Death: no respawn timer; respawn at the wave end at a team start (RE: every TnTeamPlayerStart is attack/defend).
- Downed / revive (RE §7): DownedHealth 540 draining 18 / s (30 s to bleed out, faster under fire), 3 s downed invulnerability; a
  teammate revives for the length of the Revive_Heal (or _VEH) animation (our anim data); the revived player gets 115 HP and 2
  post-revive difficulty debuffs; the wave end revives everyone. Loss counts only dead humans.
- Power: per seat, clamped 0..9999, reset at match start; shown on the HUD.

## 7. Online (after phase 1)

- Seats: up to 4 humans on the co-op team, the host's seat order (the phase 1 work: match player i = seat i on every peer).
- Everything above is sim and runs on every peer: the director, spawns, power, generator pools, door / zone / nav state. Inputs reach
  it only through the seat commands (Interact held / pressed). The Match RNG is host-seeded.
- Identity gates: the SESSIONTEST cross-perspective identity (local at seat 0 vs seat 1, same seat streams) extended with an Escalation
  launch (generators bought, a wave won, a death respawned at the wave end), plus Systems' two-process NET.identity row.

## 8. HUD / Frontend needs (for their specs)

Wave number (displayWave + 1), the next-wave countdown, per-seat power, the generator prompt (name + cost) and fill progress, the
game-over screen with the reached wave; the game-type entry for Escalation in the lobby (2 maps, 1–4 players, no bot / score / time
options).

## 9. Phasing (proposal)

1. E1 director + budget + spawn finder on BrokenHope with a placeholder roster (existing chassis + Classic brain), loss / respawn rules;
   headless ESCTEST (wave advance, budget sums, forced MinCount, zone scoring, all-dead loss).
2. E2 economy: PowerReward, generators (drain, pools, events), hidden factories, doors / bridges / nav / zones, Wall_Crashers.
3. E3 the real roster (assets + weapons + SurvivalData) as RE / AssetTools deliver; wave buffs.
4. E4 downed / revive once RE has the co-op values.
5. E5 online (seat paths for Interact drain; cross-perspective + NET.identity rows).
6. E6 polish: announcer lines per round, AI-dialog mute groups, loot table drops, achievement 45, Remnant.

## 10. Open questions

- RE (delivered 2026-10-17, §7: AI templates, downed / revive, loot). Still open: the GOAP behaviour port, the SV soldier / Spider / Titan /
  boss classes and meshes, the first-round "transform an object variable" target in Setup.
- AssetTools (delivered 2026-10-17, manifests/maps/MP_ESC_*/ + manifests/escalation/): escalation_navmesh.json = the ORIGINAL NavigationCell mesh (actor ids, blocked_at_start, enabled_by_generators; tactical / path nodes) = the primary Escalation nav; escalation_geometry.json (generator volumes, resolved actions, zone polygons); bot_nav_escalation_closed.json (start-state cross-check only); survival_weapon_data.json. Open: BrokenHope 30 of 32 generator-enabled cells are not cooked blocked (AssetTools / RE question).
- User: Escalation with bots filling empty co-op seats (not in the original: 1–4 humans only) — default no, original behaviour.
