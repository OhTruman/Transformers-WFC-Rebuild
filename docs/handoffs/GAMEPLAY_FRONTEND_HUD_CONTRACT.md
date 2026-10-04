# Gameplay → Frontend / HUD / Systems contract (Pass 21e, 2026-10-04)

Gameplay exposes **authoritative state and events only**. The in-match HUD presentation is the recovered `Hud_GFX`
Scaleform movie: it belongs to Rendering / Frontend (Rendering M08 finding). Gameplay draws no HUD widgets.

## 1. Per-frame state: `World::hudState()` → `HudGameState` (`src/game/World.h`)

| HUD need | Field(s) | Original binding / source | Provenance |
|---|---|---|---|
| Health (segmented) | `health`, `healthMax`, `activeSegment`, `segmentCount` | NotifySegmentedHealthChanged; SharedHealth [175, 125, 125, 125] | CONF |
| Overshield | `overshield`, `normalizedOverShield` | NotifyOverShieldChanged | CONF |
| Ammo | `clipAmmo`, `reserveAmmo` | ammo notifies | CONF values |
| Current weapon | `weaponName` | only the Ion Blaster exists in the rebuild | PARTIAL |
| Damage direction | `damageTakenCount` (increments per hit), `lastDamageFrom` (world), `lastDamageBearing` (rad, 0 ahead, + right) | TakeDamage → HUD hit direction | PARTIAL: the Hud_GFX indicator call is not traced; the instigator location is authoritative |
| Form / transform | `vehicleForm`, `transforming`, `cantTransformCount` (increments per refusal: NotifyCantTransform `mc_cantTransform` + TransformFailedSound `BL_TRANS_POWER.TRANSFORM_DISABLED`) | TnPawn.MoveToSafeTransformationLocation | CONF |
| Death / respawn | `alive`, `timeToRespawn` (MultiplayerRespawn_GFX `<PlayerOwner:TimeToRespawn>`), `spectating` (≥ MinRespawnDelay 3 s) | TnPlayerSpawnHelper | CONF |
| Match clock | `remainingTime`, `elapsedTime`, `timeLimit`, `countdown` (pre-match) | GRI.RemainingTime / CurrentCountdown | CONF |
| Mode | `modeTag`, `matchState`, `gameStatus`, `goalScore` | GRI | CONF |
| Team score | `teamScore[2]`, `myTeam`, `faction` (DM = 1 Decepticon) | `<CurrentGame:Teams>` | CONF |
| Local score / K / D | `score`, `kills`, `deaths`, `assists` | PRI | CONF |
| Kill feed | `killFeed` (killer, victim, teams, damage type class, time; rows live 5 s + 1 s fade, max 5 shown by Hud_GFX) | TnDeathMessage switch 0 / 1 | CONF |
| Objective markers | `objectives[]`: markerType, pointNumber, ownerTeam (255 neutral / 254 contested), active, captureProgress, beingCaptured, timeLeft, pos | TnDominationPointBase / TnKingOfTheHillZoneBase | CONF |
| Player tags | `tags[]` | TnObjectiveMarkerTypeTransformerVersus | CONF |
| Match end / result | `winnerTeam`, `winnerPlayer` (FFA, −1 draw), `result` (team text; FFA empty: TnFreeForAllGameOverMessage), `endReason`, `matchOverTimeLeft` (15 s) | TnVersusGameOverMessage | CONF |
| Scoreboard | `scoreboard[]`: player, name, team, score, kills, deaths, assists, alive, local | InGameStats / EndGameStats | CONF columns (Level column needs profile data: Frontend) |

## 2. Events: `World::matchEvents()` (one-frame copy; `MatchEvent::Type`)
CountdownTick, MatchStarted, PlayerSpawned, PlayerKilled, GameNearlyComplete, TimeAnnouncement,
KillsLeftAnnouncement, PointsLeftAnnouncement, MatchEnded, ReturnToLobby. Consumers poll every frame. Counters such as
`cantTransformCount` and `damageTakenCount` are edge-detected by the consumer.

## 3. Character selection (`src/game/CharacterRoster.h`)
- `Match::selectCharacter(player, CharacterSelection)` → applied at the next spawn
  (TnPlayerController.SelectCharacter → PRI._SelectedCharacter) [CONF].
- `CharacterSelection`: type (0 custom / 1 iconic), specialty (Leader / Scientist / Scout / Soldier), chassisId (stable
  roster UniqueId such as "Truck", "Jet4"; never file names).
- The faction is the team at spawn. A custom selection resolves to the specialty default body for that faction:
  - Leader: Ironhide `Truck3` / Soundwave `Truck4`;
  - Scientist: Air Raid `Jet4` / Starscream `Jet`;
  - Scout: Sideswipe `Car2` / Barricade `Car4`;
  - Soldier: Warpath `Tank3` / Brawl `Tank2`.
  [CONF TR_MPPlayerCharacterData_p]
- Spawning waits for `hasSelectedCharacter` (CheckReadySpawn) [CONF]. The local player is pre-selected as iconic `Truck`
  (Optimus) until the selection screen exists. `MatchPlayer::chassis` reports the resolved body.
- **Gap:** only the Optimus pawn resources load. Other chassis need AssetTools ROBODEF / VEHDEF exports per UniqueId, which
  fill `PawnDefinition` (33 chassis in `notes/data/mp_chassis_roster.json`).

## 4. Settings / input contract
| Item | Owner | Value / rule | Provenance |
|---|---|---|---|
| Transform | Gameplay action | F / pad Y. Refused with NotifyCantTransform when the target form does not fit | CONF |
| Fine aim | Gameplay | RMB / LT, **robot only** | CONF |
| Boost | Gameplay | RMB / LT in **vehicle**, held. Accelerator fixed at 1 while Driving (no throttle) | CONF |
| Dash / Nitro | Gameplay | Shift: Dash while hovering, Nitro while boosting (steer × 0.3) | CONF |
| Jump / Reload / Fire | Gameplay | Space; R (tap < 0.3 s, on release); LMB held | CONF |
| ShowScores | Frontend | Tab toggles the scoreboard (reads `scoreboard[]`) | CONF binding |
| Pause | Frontend | Esc | CONF |
| Camera sensitivity | Frontend → Gameplay | default 30 (profile). Gameplay's `kMouseSens` 0.0022 rad / px is PROVISIONAL until the profile scale is mapped (MouseSensitivity 0.00114286 noted by RE) | PARTIAL |
| Invert look | Frontend → Gameplay | per form (robot / vehicle) profile flags; not yet plumbed | PARTIAL |
| FOV | Frontend / camera strategy | owned by the camera sets, not by a settings slider in Gameplay | HIGH |
| Minimap | — | none in the original | CONF |
