# Campaign AI layer: tactical volumes, buddies, AI sets, downed, spawning

WFC RE lane, 2026-10-18. This is the last of the campaign runtime series.

Sources:
- script: TnAiController, TnTacticalVolume, TnGoapGoalStayInTacticalVolume, TnTacticalPathNodePicker,
  TnAiActionMeleePlanningUtil, TnAiSquad, TnGoapGoalBuddyLead, TnCustomCostAwayFromBuddy, SeqAct_HmPawnFactory,
  SeqAct_TnPawnFactory;
- data: `notes/data/ai_definitions.json` (all 94 AI character definitions with their resolved sets), authored.db CDOs,
  Xe-TransGame.ini.

The planner (HmGoap plan runner and scheduler) is the same GOAP system the MP note already covers, so no new native code was
needed. All of this is CONFIRMED script / data unless marked.

## 1. Tactical volumes (`SeqAct_TnAssignTacticalVolume` → `TnAiController.AssignToVolume`)

**Assign:**
1. Leave the old volume (`RemoveFromVolume`) and reset the move-to-volume sensor.
2. Store the new volume and `TacticalVolume.AssignToVolume(C)`, which registers the AI as assigned.
3. **If `AlertAssignedAiToEnemiesInVolume`:** every enemy pawn already touching the volume is pushed into the AI's perception
   (`SeeEnemy`).
   - While assigned, an enemy entering the volume → `SeeEnemy`; leaving → `UnregisterEnemy`.
   - Same-team pawns are ignored.
4. If the AI is outside the volume: move inside (below).

**Planning inside:**
- **Position choice:** `TnTacticalPathNodePicker` sorts candidate tactical positions by distance with the volume as a filter
  (`TnTactical.SortByDistance(…, TacticalVolume)`).
- The current best position is **dropped if it is outside the volume** (`RescoreBestNode`). The AI only takes cover or positions
  at the volume's own `TacticalPathNodes`, which are collected at save time: PathNodes inside the brush that implement
  TnTacticalInterface.
- **Melee** (`TnAiActionMeleePlanningUtil`): an enemy is meleeable only if it is **inside the assigned volume**, plus the plane-form,
  sentry and size checks.
- The other goals (shoot, defend, react) run normally, but from positions inside the volume.

**"Walks back inside":**
- Goal `TnGoapGoalStayInTacticalVolume` owns a MoveToRange sensor of type `TnAiWorldStateMoveToTacticalVolume`.
- **Leaving the volume** (controller `UnTouch` of its assigned volume) → `MoveToPathNodeInTacticalVolume`:
  - the subject is `GetTacticalPathNodeInsideVolume`: **the first tactical path node, in stored order, that is not blocked and not
    claimed** (an air node first if the AI can fly);
  - range 1.
- The goal then demands `AtActor(that node)`. It doesn't fire if the AI's claimed position is already inside.
- **Re-entering** (`Touch`) resets the sensor and replans. Arriving (`MoveToTacticalVolumeCompleted`) clears the subject.
- No free node → logged error; the AI isn't moved.
- **UnAssign or Toggle-off** → `AssignToVolume(none)`, i.e. unconstrained.

## 2. Buddy (AI teammate) behaviour in single player

**Spawn** (`TnAiSquad`, members from `TnWorldInfo.TnCoopInfo.PlayableCharacters`, e.g. A1: Optimus / Bumblebee / Ratchet
Buddy AICDs):
- They spawn via `SpawnPendingMembers` (each squad tick, and from Togetherness volumes):
  - at the active checkpoint's spawn points when restarting from a checkpoint, else near any player;
  - team = the CoopInfo faction;
  - `InitializeFromTemplate(AICD)`.
- Set on spawn:
  - **`IsAiBuddy = true`**;
  - **`DownedEnabled = false`**: buddies are never downed, they die;
  - pending scripted interacts applied;
  - the squad's stored **objective** (actor + range) re-applied.
- **Death:** `TnAiSquad.Killed` keeps the objective and sets a respawn timer of **`AIRespawnDelay = 0`** (Xe-TransGame.ini), so the
  buddy respawns on the next squad tick, near a player.

**Goal priority** (`Buddy_AiGoalSet`, highest first; used by all Truck / Jet / Tank buddies):
1. Use Interact (high)
2. React
3. React to stimulus (high-priority buddy set)
4. **Scripted Move-To** (Kismet)
5. Defend Self
6. **Kill**
7. Use Interact (low)
8. **BuddyLead**
9. **Move To Objective**
10. **Move To Buddy** (`TnAiWorldStateCloseToFriend` via `TnAiMoveToClosestFriendRangeSensor`; **skipped while a Move-To-Objective is
    active**)
11. React to stimulus (low)
12. Idle

Engineer buddies (Ratchet, Soundwave, Jetfire, Thundercracker) use `Engineer_AiGoalSet`.

**Fight vs follow:**
- Kill (6) outranks every follow goal (8–10). So a buddy fights whenever it has a target, and follows only when idle of combat.
- While fighting, **`TnCustomCostAwayFromBuddy`** (on ShootAtTarget, in all buddy and NPC-healer action sets) adds **cost 10** to
  any plan that leaves it farther than **RANGE_Close (1500 UU)** from its `_Friend` (the player). That keeps it fighting near the
  player.
- **BuddyLead:** with an objective and a friend, it picks an interact emote (`_Finder.FindEmote(buddy, objective)`) to "lead" the
  player toward the objective, re-validated every 0.5 s.

**Revive:**
- **Buddies do not revive anyone.** `TnGoapGoalRevive` is only in `Engineer_Boss_AiGoalSet`.
- Player revives are player-to-player (co-op).
- Revive goal defaults: MaxSearchDistance 30000, TimeDownedBeforeRevive 1 s, DistanceForNoWaitingBeforeRevive 5000,
  RevivePlayersOnly true.

**Togetherness (AI side):**
- `OnActivateTogethernessVolume` spawns pending buddies immediately. Its teleport moves friendlies outside the volume to safe
  TeleportPoints, **AI first**, preferring points out of view (Kismet handlers doc).
- The buddy then re-plans normally from there.

## 3. Campaign AI sets (from the 94 definitions)

| CharacterType | GoalSet | ActionSet(s) | Characters |
|---|---|---|---|
| CType_Buddy | Buddy_AiGoalSet | Soldier_Truck_Buddy / Soldier_Jet_Buddy / Soldier_Tank_Buddy | 23 named (Bumblebee, Optimus-class trucks, jets, tanks) |
| CType_Buddy | Engineer_AiGoalSet | Engineer_Buddy, Soldier_Jet_Engineer_Buddy | Ratchet, Soundwave, Jetfire, Thundercracker |
| CType_SoldierCar | TR_AiGoalSet_Agent / Shotgun_AiGoalSet | Soldier / Soldier_Car / Soldier_Shotgun | soldiers |
| CType_SoldierJet | JetSoldier_AiGoalSet | Soldier_Jet / _Defender / _Robot | jet soldiers (the hover attack actions) |
| CType_SoldierTank | TR_AiGoalSet_Agent | Tank_AiActionSet | soldier tanks |
| CType_Sniper / CQCSpecialist / Rocketeer / Brute / Spider | Sniper / CQC / Agent / Brute, Titan / Spider | own sets | specialists |
| CType_Boss | TR_AiGoalSet_Boss, Engineer_Boss, TR_AiGoalSet_Agent, NPC_Healer | per-boss sets | Starscream, Soundwave, Frenzy, Laserbeak, Rumble, Ratchet (NPC healer) |
| (special) | Agent, OmegaSupreme, Trypticon, Customization | own | SecurityDrone, Omega, Trypticon, the menu pawn |

- **Turret path:** buddy action sets include EnterTurret, LeaveTurret, ShootFromTurret and TurretScanForEnemies. Kismet
  `SeqAct_TnAiUseTurret` swaps in `TR_AiGoalSet_UseTurret` / `TR_AiActionSet_UseTurret` and hands over the scripted turret list
  (handlers doc).
- **Downed (AI side):**
  - Enemies are downable only where `DownedEnabled` is set: off by default; Kismet `TnSeqAct_DownRevive` enables it.
  - AI pawns `_TakesDownedDamage`.
  - **AI shooting at a downed target:** spread ×2.5 (`DownedEnemySpreadModifier`), fire interval ×2 (`DownedEnemyFireIntervalModifier`).
  - **Player downed:** DownedHealth 400, drain 40/s (10 s to bleed out), anims Revive_Fall / Revive_Getup / Revive_Heal(_VEH),
    sounds DOWNED_PULSE / DOWNED_REVIVE.
- **Versus the MP bot AI we already run:** WFC shipped no MP bots. The only original AI is this campaign set. Our MP bots should
  reuse the closest campaign enemy sets: Agent / Soldier, JetSoldier, Tank. Buddies are campaign-only: Buddy goal set, the
  AwayFromBuddy cost, no downed, instant respawn.

## 4. Spawn and wave pacing

- **There is no wave or pacing code in script beyond the Kismet factory.**
- Campaign enemies come from Kismet **`SeqAct_TnPawnFactory`** (← `SeqAct_HmPawnFactory` ← stock `SeqAct_ActorFactory`):
  - inputs Spawn Actor / Enable / Disable / Toggle / Kill All;
  - outputs Spawned / Aborted / All Pawns Killed / Single Pawn Killed / All Pawns Spawned.
- **Pacing parameters are per-factory data,** using the stock ActorFactory fields: SpawnCount, SpawnDelay, PointSelection,
  bCheckSpawnCollision, SpawnPoints. They are stepped by the stock native update (HIGH).
- **High Moon additions:**
  - `MaxNumSpawnedInWorld`: a cap on all AIController pawns currently in the world; ≤ 0 means no cap;
  - "Single Pawn Killed" fires per death;
  - "All Pawns Killed" fires when the factory's spawned list empties;
  - vehicle-form spawns are lifted to the factory pawn's collision height (`AdjustSpawnLocation`).
- **Waves are therefore authored Kismet:** factories chained on "All Pawns Killed" or "Spawned". The only code-side pacing is
  MP's respawn helpers.
