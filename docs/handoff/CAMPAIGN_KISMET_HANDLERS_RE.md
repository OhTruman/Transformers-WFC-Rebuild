# Campaign High Moon Kismet classes: exact handlers

WFC RE lane, 2026-10-18. Integration copies this to `docs/handoff/CAMPAIGN_KISMET_HANDLERS_RE.md`. It extends AssetTools'
`CAMPAIGN_KISMET_CLASSES.md`.

Sources:
- script: TransGame decompiles;
- **TransContent classes decompiled from the campaign map packages** that carry them, with `tools/ue3dis.py <map>
  TransContent.<Class>.<Func>`. TransContent is cooked per map, so campaign-only classes are absent from the MP-map decomps.
  Used here: A2_KON_BASE_m, A1_IAC_Elevator_Design_m and the Debris copy;
- CDO links from authored.db.

No native code was needed, except the movie playback, which is left PARTIAL below.

**Correction to CAMPAIGN_KISMET_CLASSES.md:** three of the "no decomp" classes have their own script:
- `TnSeqAct_MissionComplete`;
- `SeqAct_TnAiUseTurret`;
- `TnSeqAct_AiDisableShootingBehavior`.

They are in TransContent inside the map packages.

**Convention:**
- Actions without their own `Activated` dispatch to `On<ClassName minus "SeqAct_">(Action)` on each target (controller or
  actor).
- Event outputs are fired by `TriggerEventClass(EventClass, Instigator, OutputIndex)` / `ActivateEventClass(…, [indices])` on the
  actor that owns the event.

## Actions

### TnSeqAct_MissionComplete (← HM SeqAct_MoviePlayer, latent)

- **Links:** in `Play`; var `Player` (Targets); **no outputs** (the MoviePlayer outputs are overridden away).
- **Authored:** `MovieName`; `bIsSkippable` is reassigned every tick, see Update.
- **On Play:**
  1. `SeqAct_MoviePlayer` latches the play request, and `OnPlayMovie` hands `MovieParameters` to the game.
  2. **Then `TnCampaignGame.StartCompleteMission()`:**
     - for every TnPlayerController: `ClientCompleteMission(GRI.MapID, GameDifficulty)` →
       `TnProfileSettings.CompleteMission` + `CheckForLevelCompletionAchievements` + **`SaveProfileSettings()`**;
     - then `EndGame(none, "Complete")`.
- **`Update` (each tick):** `bIsSkippable = IsEndGameHandlingComplete()`, i.e. `bGameEnded && !IsTimerActive('PerformEndGameHandling')`.
  **The movie can't be skipped until end-game handling finishes.**
- **`StopMovie`** (skip or end): ignored until AllowedToEnd. Then it stops the movie and calls **`EndCompleteMission()` →
  `ReturnToGameLobby()`**.
- **PARTIAL:** the native FMV playback itself.

### SeqAct_TnAiMoveToActor (← Engine.SeqAct_AIMoveToActor, latent)

- **Links:** in `In` (inherited); out **`Finished`, `Aborted`, `Out`** (inherited); vars `Target`, `Destination`
  (DestinationActor), `Look At` (FocusActor).
- **Own data:** `Range` (default RANGE_Touch; authored Close / Medium / Striking).
- **Handler `TnAiController.OnTnAiMoveToActor`:**
  - `ClearLatentAction(SeqAct_TnAiMoveToActor, bAborted=true, except=this)` **aborts any earlier move on this AI** (it fires
    `Aborted`);
  - stores `ScriptedMoveToAction`.
- **Planning:** goal `TnGoapGoalScriptedMoveTo` adds `AtActor(Subject = destination, or its Pawn if the destination is a Controller)
  ≤ Range`. The planner then runs `TnAiActionNavigate`.
- **Completion:**
  - `TnAiActionNavigate.HasNavigationSucceeded`: not jumping, AtActor range ≤ expected, LOS if the action requires it, and the
    final transformer form set up. Then `ScriptedMoveCompleted` → `ClearLatentAction(…, false)` → **`Finished`**.
  - Navigation failure (no plan within the time window, head lost, subject changed, or finish state 4) → `ScriptedMoveFailed` →
    **`Aborted`**.
  - Both only act if the current AtActor subject is still the scripted destination.

### SeqAct_TnAssignTacticalVolume

- **Links:** in `Assign` / `UnAssign` / `Toggle`; vars `Target`, `Tactical Volume`.
- **`TnAiController.OnTnAssignTacticalVolume`:**
  - volume none → `AssignToVolume(none)`;
  - Assign → assign;
  - UnAssign → release only if it is the current volume;
  - Toggle → release if current, else assign.
- **Touch / UnTouch of the assigned volume:** entering resets the move-to-volume sensor and replans; leaving moves the AI back to
  a path node inside the volume.
- No outputs used.

### SeqAct_TnAiUseTurret (TransContent script)

- **Links:** in `Stay In Turret` (0) / `Can Leave Turret` (1); vars `Target`, `Turret Object` (Turrets[]).
- **Target resolution:** a TnAiController directly, or a Pawn's controller.
- **Input 0:** `C.SetScriptedTurrets(Turrets)`, then **fresh copies** of `_TurretGoalSet` (TR_AiGoalSet_UseTurret) and
  `_TurretActionSet` (TR_AiActionSet_UseTurret) via `SetGoalSet` / `SetActionSet`.
- **Input 1:** `ClearScriptedTurrets` + `ResetGoalSet` + `ResetActionSet` (back to the defaults) + **`CancelCurrentPlan`**.
- **Usable turret:** `GetUsableScriptedTurret` = the first turret no player is requesting and that `IsTurretUsableByAI`.
- `OnTeleport` clears the scripted turrets.

### TnSeqAct_AiDisableShootingBehavior (TransContent script)

- **Links:** in `Disable Shooting Behavior` (0) / `Enable Default Behavior` (1).
- **For each target AI:** **`Pawn.InvManager.DestroyInventory()`**, which runs **on both inputs**.
- Then: input 0 → copies of Soldier_NoWeapon_Buddy action / goal sets; input 1 → copies of the AI template definition's own
  ActionSet / GoalSet.
- If either set is none, the swap is skipped.
- **Note:** "Enable Default Behavior" restores the plans but **not the weapons**. The inventory was destroyed.

**Re-verified 2026-10-18 (Integration request):** both handlers re-decompiled; identical to the above.
- `SeqAct_TnAiUseTurret`'s script is cooked into every design sublevel that places it: A1_IAC_Strts0_Design_m,
  A2_KON_WallCrawler_Design_m, A4_ORB_Flying2_DSN_m (among the 11 sublevels with instances).
- Its CDO sets: `_TurretGoalSet` = TR_AIGoalSet_p.TR_AiGoalSet_UseTurret, `_TurretActionSet` = TR_AiActionSet_p.TR_AiActionSet_UseTurret.
- `TnSeqAct_AiDisableShootingBehavior`'s script is in A2_KON_BASE_m (6 instances, all A2_KON). Its CDO sets:
  `ShootingDisabledActionSet` = TR_AiActionSet_p.Soldier_NoWeapon_Buddy_AiActionSet, `ShootingDisabledGoalSet` =
  TR_AIGoalSet_p.NoWeapon_Buddy_AiGoalSet.
- `TnAiController.SetScriptedTurrets` / `ClearScriptedTurrets` have no TransGame caller because their only caller is this
  TransContent action.

### TnSeqAct_DownRevive

- **Links:** in `Down` / `Revive`; props DrainHealthWhenDown (true), CanHealWhenDown (true), FullRevive.
- **AI handler (`TnAiController`):**
  - Down → `DownedEnabled = true`, copies the two flags to the pawn, `DownTransformer()`;
  - Revive → `DownedEnabled = false`, `GetRevive(none, FullRevive)`.
- **Player handler (`TnPlayerController`):** the same, **without touching DownedEnabled**.

### TnSeqAct_ActivateTogethernessVolume

- **Links:** vars `Volume` (Targets), `Instigator` (InstigatingPawn); HideTeleports.
- **`TnTogethernessVolume.OnActivateTogethernessVolume`:**
  - skipped on clients;
  - `AiSquad.SpawnPendingMembers(0, true)`;
  - `GI.NotifyPlayerWithinTogethernessVolume(PC)`;
  - `TeleportFriendlies`, only if the instigator is human-controlled and not re-entrant.
- **TeleportFriendlies:**
  - friendlies not already touching the volume are teleported: **AI first, then other humans** (two passes);
  - more than 3 friendlies is logged as an error;
  - targets come from `TeleportPoints` via a view-sorted index: from the first index if AI out of view, else from the last;
  - each point must pass `IsSafeSpawnLocation(radius, height)`; it stops when no point is left.
- The volume also fires TnSeqEvent_Together (touch, untouch, all-in, all-out, plus timers). Not traced further.

### SeqAct_TnAiJump

- **Links:** in `Jump`.
- If the pawn is not downed, input 0 had the impulse, and the pawn is in robot form: world state `Reaction = 2`, then
  `RequestReplan(1)`.

## Events (who fires which output)

| Event | Outputs (index) | Fired by |
|---|---|---|
| **TnSeqEvent_Interact** | Touch 0, Untouch 1, Adjusting 2, Begin Use 3, End Use 4 | `TnInteractBase.ProcessInteractEvent(EventType)` → `TriggerKismetInteractEvent` → `ActivateEventClass(…, [EventType.default.SequenceEventMapType])`. **The index = EInteractEventType enum order** {IET_Touch, IET_Untouch, IET_BeginAdjust, IET_BeginUse, IET_EndUse, IET_Null}. Types with IET_Null (InUse, Disabled, Enabled, ForceEndUse, RemoveTurret) fire no output. Instigator = the current user. `TnInteractActionActivateKismetEvent` fires an authored type explicitly. ProcessInteractEvent also replicates the event and runs the InteractActions, waiting on latent ones. |
| **TnSeqEvent_Downed** | Downed 0, Revived 1 | `TnPawn.TakeDamageHealthBelowZero` (instigator = InstigatedBy) and `DownTransformer` (instigator none) → Downed; `TnPawn.Revive(Reviver)` → Revived. Fired on the pawn's **Controller**. |
| **TnSeqEvent_DetPack** | BaBoom 0 | `TnInteractActionFireDetpackEvent` on the interact actor |
| **TnSeqEvent_TankStatus** | FuelCellDamaged 0, FuelCellDestroyed 1 | `TnTankAiPawn` → BoundController, index = status |
| **TnSeqEvent_HealthStatus** | Healthy 0, Wounded 1 | `TnAiHealthMonitor.SetHealthy` / `SetWounded`, only if the blueprint's `TriggerHealthStatusSeqEvent`. SetWounded also sets Reaction 3 and replans. TnTankAiPawn fires Wounded too. |
| TnSeqEvent_OmegaStatus / TrypticonEvents | as listed by AssetTools | the boss pawns (unchanged) |

## Timing summary

- **Instant (same Kismet tick):** AssignTacticalVolume, UseTurret, DisableShootingBehavior, DownRevive, Jump (replan requested),
  ActivateTogethernessVolume (spawn + teleport immediately).
- **Latent:**
  - AiMoveToActor: until the navigate action succeeds or fails;
  - MissionComplete: until the movie stops, and the stop is only accepted after end-game handling.
