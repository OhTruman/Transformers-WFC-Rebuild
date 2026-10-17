# Campaign WFC Kismet classes: handler decodes (RE, 2026-10-18)

The 32 WFC "script" classes from CAMPAIGN_KISMET_CLASSES_RE.md (c), in instance-count order.

Labels: CONFIRMED (C) = decompiled TransGame / HM_Engine bytecode (work/script/decomp) + class defaults; HIGH (H) = native part
inferred, not traced.

**Notation:**
- **IN / OUT / VAR** = the class-default links (VAR: `desc → property (type)`).
- "Targets → controller" means each Targets entry is used as a TnAiController, or as a Pawn whose Controller is a
  TnAiController.
- **Character-name resolution** (several AI actions): live TnAiController → `GetCurrentCharacter().CharacterName` (""
  if dead); else `TnSeqVar_Character.GetCharacterName()`; else `TnSeqVar_Buddy.GetCharacterName()`.
- `CampaignGame` = `TnCampaignGameBase(WorldInfo.Game)`; `AiSquad` = `CampaignGame.get_AiSquad()`.

---

## 1. SeqAct_Conversation (892, HM_Engine)

- **IN** Play, Stop · **OUT** Played, Stopped, Finished.
- **VAR (per instance): two links per line,** `Speaker_<i> <first words>` and `Face_<i>`. The variable-link index is `2i`
  (speaker) and `2i+1` (face target).
- **Props:**
  - `Lines[]`: { SoundCue, PostDelayTime } (1,566 lines; PostDelayTime is 0 in the sample);
  - `DuckingMixerPreset` (SoundMixerPresetName);
  - runtime `CurrentLineIndex`, `PostDelayTimer`.
- **Script (C):** only editor validation, and the per-line play / stop:
  - `PlayDialog(cue, speaker, type)` → `HmDialogInterface(speaker).PlayDialog(cue, true, type, false, DialogEventCallback)`;
  - `StopDialog` → `StopDialog(true, true)` if that cue is playing.
  - Speakers must implement HmDialogInterface / HmSoundEventInterface. Each line's cue must contain a SoundNodeWaveEvent for one
    of the speaker's DialogCharacters (the cue picks the variant per character).
- **Native (H: Play / Stop / OnDialogEnded / Update are native stubs):**
  - **Play** → CurrentLineIndex = 0; enable DuckingMixerPreset; play line 0 on Speaker_0 (Face_0 = look-at target); fire
    *Played*.
  - On each line's dialog-ended callback → wait PostDelayTime → next line.
  - After the last line → disable the duck, fire *Finished*.
  - **Stop** → stop the current line, disable the duck, fire *Stopped*.
  - `Reset()` (C) stops it.
- **Depends on:** the HmDialogInterface implementation on pawns (dialog component), HmDialogCharacter selection in cues, and
  the mixer presets (MP_REVERB_ROOMS_RE §1).

## 2. SeqAct_TnAiSetMoveToObjective (691)

- **IN** Enable, Disable · **OUT** none.
- **VAR** Target → Targets (Object), Objective Point → ObjectiveActor (Object).
- **Props:** `Range` (EObjectiveRange, default RANGE_Touch).
- **Handler (C):**
  - Enable → `SetWorldState(Targets, ObjectiveActor)`; Disable → `SetWorldState(Targets, none)`.
  - For each target: name = character-name resolution (stop the loop at the first unresolved).
  - `AiSquad.SetObjective(name, actor, Range)`: stores ObjectiveActor / ObjectiveRange on the squad member (so it survives
    respawns and character swaps) and calls `TnAiController.SetMoveToObjective(actor, Range)`.
  - If the squad has no such member → it calls the target's controller directly.
- **`TnAiController.SetMoveToObjective` (C):**
  `TacticalAnalyzer.GetMoveToRangeSensor(TnAiWorldStateMoveToObjective).SetSubjectAndRange(actor, Range)`, or `ClearSubject()`
  for none. The GOAP planner then satisfies the "be within Range of ObjectiveActor" world state (CAMPAIGN_AI_LAYER_RE).
- **Completion:** none. It is a persistent goal until Disable or a new objective.

## 3. SeqAct_TnRemoveMarker (389)

- **VAR** Marker Actors → MarkerActors.
- **Handler (C):** for each actor: `GRI.RemoveObjectiveMarker(actor, ObjectiveType)` → `TnObjectiveManager.RemoveObjectiveMarker`.
  The class default ObjectiveType is unset (None), i.e. any type (H: None matches all).

## 4. SeqAct_TnAddMarker (385)

- **VAR** Marker Actors → MarkerActors.
- **Props:** `ObjectiveType` (default `TnObjectiveMarkerTypeObjective`).
- **Handler (C):** for each actor: `GRI.AddObjectiveMarker("", actor, ObjectiveType)` → `TnObjectiveManager.AddObjectiveMarker`
  (description "", replicated). The check warns if the actor isn't replicated to clients (RemoteRole None): the marker then
  wouldn't show for co-op clients.
- HUD side: CAMPAIGN_HUD_OBJECTIVES_RE.

## 5. SeqAct_TnAiUseInteract (351)

- **IN** Use Interact, Abort Interact, End Interact · **OUT** Out, AI Command Receive.
- **VAR** Target → Targets (exactly one; the check warns otherwise), Interact Object → Interact (TnInteractBase).
- **Activated (C):**
  - `_Aborted = _Ended = false`; fire *Out* immediately.
  - Abort → `_Aborted = true`; End → `_Ended = true`.
  - **Use** with one target → `AiSquad.AddPendingAiInteract(name, self)`. This only applies if the character is a playable
    co-op character; it replaces the character's previous pending action. Then, **if that character is currently AI-controlled**,
    fire *AI Command Receive*.
  - Abort / End with an Interact set → `AiSquad.ClearPendingAiInteract(Interact)`.
- **The AI side (C):**
  - `TnAiSquad.SetAiUseInteract(controller)`: finds the pending entry by the pawn's character → `C._ScriptedUseInteract = action;
    C.RequestReplan(1)`.
  - GOAP action `TnAiActionUseInteract`:
    - precondition: be at `Interact.HotSpotPathNode`, in the blueprint's RequiredForm;
    - invalid if the interact is AI-disabled, in use, a mountable turret, or its hotspot is occupied;
    - OnStart: checks `IsUseableBy` (angle / distance conditions suspended), removes the AI cloak buff, stops, calls
      `InteractCommand(pawn, 0)` (start), sets finished / failed callbacks, anchors at the hotspot;
    - OnUpdate: when the sequence action reports Ended or Aborted → `InteractCommand(pawn, 1)` (release); fails if the interact
      stops being in use;
    - Succeeds on the interact's finished callback.
- **Completion:** the interact's own finish (e.g. hold-to-use complete), or End / Abort from Kismet. A human-controlled
  character just gets the pending entry. *AI Command Receive* only fires when the AI is driving.

## 6. SeqAct_PlayStinger (250, HM_Engine)

- **IN** Play · **Props** `Stinger` (SoundCue, per instance).
- **Handler (C):** `HmWorldInfo._MusicPlayer.PlayStinger(Stinger, Replicate = true)`, then output 0.
  - `PlayStinger`: replicates the stinger, then plays it only if no stinger is playing, or if the new cue's root
    **Priority > the current one's**. If it plays, the old stinger fades out over 0.1 s and the new AudioComponent is created and
    played. It is solo-tracked to the music player's current `_SoloTrackIndex` when set.
- **Completion:** immediate.

## 7. TnSeqAct_DefendActor (151)

- **IN** Enable, Disable, Toggle.
- **VAR** Target → Targets, Actor to Defend → DefendActor.
- **Handler (C):** for each target controller: Enable → `C._DefendActor = DefendActor`; Disable → none; Toggle → flip. The AI's
  defend behaviour reads `_DefendActor` (GOAP, CAMPAIGN_AI_LAYER_RE).
- **Completion:** immediate; the goal persists.

## 8. SeqAct_TnAiShootAtActor (105)

- **IN** Enable, Disable, Toggle.
- **VAR** Target → Targets, Shoot At Actor → ShootAtActor.
- **Handler (C):** for each target controller: Enable → `C.set_ShootAtActor(actor)`; Disable → none; Toggle → flip. Persistent
  forced fire target, read by the AI targeting (CAMPAIGN_AI_LAYER_RE).

## 9. TnSeqAct_AttackFromActor (97)

- **IN** Enable, Disable, Toggle.
- **VAR** Target → Targets, Attack From Actor → AttackFromActor.
- **Handler (C):** `C._AttackFromActor` = actor / none / flip, the same pattern as DefendActor. It is the preferred firing
  position for the AI.

## 10. SeqAct_PlayDialog (94, HM_Engine)

- **IN** Play, Stop · **OUT** Stopped, Finished.
- **VAR** Target → Targets (the speakers).
- **Props:** `SoundCue`, `DialogType` (None 65 / kDialogTypeConversationStart 29 / kDialogTypeConversation 12), `PostDelayTime`
  (0, 0.15, 1.0 seen), `DuckingMixerPreset`.
- **Per target (C):** `HmDialogInterface(target).PlayDialog(SoundCue, true, DialogType, false, DialogEventCallback)`. Stop →
  `StopDialog()` if the cue is playing.
- **DialogEventCallback (C), by event value:**
  - 0 (started): PlayingCount++, enable the duck preset;
  - 1: fire *Stopped*;
  - 2 (ended): reset the post-delay timer to PostDelayTime, FinishedCount++;
  - for 1 and 2: PlayingCount--, and when it reaches 0, disable the duck;
  - 3 / 4: fire *Stopped* (H: interrupted / failed).
- **Native (H):** the Update ticks PostDelayTimer and fires *Finished* when every started line has finished and the post-delay has
  elapsed. EnableDuckingMixerPreset / Disable are native → `AudioDevice.Enable / DisableMixerPreset(DuckingMixerPreset)`.

## 11. SeqAct_DialogGroup (94, HM_Engine)

- **IN** Enable, Disable · **Props** `DialogGroup` (SoundEvents_Dialog.Muting_Setup_Groups.<letter> / DefaultDialogGroup).
- **Handler (C):** `HmWorldInfo._DialogEventManager.EnableGroup / DisableGroup(DialogGroup)`. It gates which ambient / barks
  dialog groups may play (the manager is native / HM; groups are data in SoundEvents_Dialog).

## 12. TnSeqAct_ForceIntoForm (93, latent)

- **VAR** Target → Targets (TnSeqVar_Character / pawns).
- **Props:** `ForcedForm` (TF_Robot default in 70 instances, TF_Vehicle in 24).
- **Start (C, `TnPawn.OnForceIntoForm`, the latent handler):** if already in the form → done. Else
  `MoveToSafeTransformationLocation(form)` → `ForceIntoForm(form)`; if not transforming afterwards → `AbortFor(self)`.
- **Update (C, latent):**
  - per target: if not in the form and not transforming → `AbortFor(P)` and keep running;
  - if transforming → keep running;
  - when in the form → `ClearLatentAction`.
  - The action **completes when every target is in ForcedForm**: out "Finished" (H: the stock latent-action outputs
    Finished / Aborted).

## 13. TnSeqAct_SetSniperSearchLocations (89)

- **IN** Set · **VAR** Target → Targets, Sniping Search Locations → SnipeSearchLocations (object list).
- **Handler (C):** `C._SnipeSearchLocations = list` on each target controller (the sniper AI's search points).

## 14. TnSeqAct_SetTargetableByAi (50)

- **VAR** Target → Targets.
- **Props:** `IsTargetableByAi` (False in 36 instances, True in 15).
- **Handler (C):** sets `_TargetableByAi` on each target's TnAiPawn (direct or via controller), else on a TnSentryPawn; logs other
  targets. AI target selection skips pawns with `_TargetableByAi = false`.

## 15. SeqAct_TnAiSentryForceTarget (47)

- **IN** Set, Clear · **VAR** Sentries → Targets, Forced Target → ForcedTargets (exactly 1).
- **Handler (C):**
  - Set → each sentry's `TnAiSentryController._ForcedTarget = ForcedTargets[0]`; it errors if the count ≠ 1.
  - Clear → none.
  - Non-sentries are logged and skipped.

## 16. TnSeqAct_AddCompetitiveScore (43)

- **IN** Score · **VAR** Targets (players).
- **Props:** `ScoreAmount` (10 / 25 / 50 / 75 / 100), `MultiplierAmount` (−1 = default).
- **Handler (C):** for each target (TnPlayerPawn → controller, or a TnPlayerController):
  `PRI._ScoreTracker.ScoreEvent(TnCompetitiveScoreEventKismet, , , ScoreAmount, MultiplierAmount)`. This is co-op competitive
  scoring.

## 17. TnSeqAct_SetAiGodStatus (35)

- **VAR** Targets.
- **Props:** `GodStatus` (GOD_Off default 12, GOD_On 11, GOD_PseudoOn 13).
- **Handler (C):** per target AI controller:
  - `bGodMode = (GodStatus == On)`, `bPseudoGodMode = (GodStatus == PseudoOn)`;
  - `SetWorldStateInt(TnAiWorldStateGodStatus, GodStatus)`, so the planner knows.
  - Pseudo-god = takes damage but can't die (H: stock UE3 meaning).

## 18. SeqAct_TnAiSentrySetDormant (29)

- **IN** Dormant, Not Dormant · **VAR** Targets (sentries).
- **Handler (C):** `TnAiSentryController.SetDormant(bool)`. On a change to dormant: clear CurrentTarget and `Pawn.StopFire(0)`.

## 19. SeqAct_CrowdSpawner (33, Engine)

This is stock UE3 `SeqAct_CrowdSpawner` (crowd agents along paths). It has script but is not WFC-specific. Implement from the
stock UE3 crowd system; deferred unless a level needs it.

## 20. TnSeqAct_SetSkeletalMeshLookAtActorTarget (20)

- **IN** Start, Stop, Lock · **VAR** Skeletal Mesh LookAt Actor → Targets, LookAt Target → TargetActor (a Controller is replaced
  by its Pawn).
- **Handler (C):** on each target implementing `TnIKLookAtTargetInterface` (errors otherwise):
  - Start → `StartLookAtTracking(TargetActor)`;
  - Stop → `StopLookAtTracking()`;
  - Lock → `LockLookAtTracking(TargetActor)`.
  - The IK look-at is on set-piece skeletal actors (H: head / turret aim bones).

## 21. TnSeqAct_AddImpulseToPawnInVolume (19)

- **IN** Add Impulse · **VAR** Target → Targets, Volume → TriggerVolume.
- **Props:** `ImpulseDirection` (0, 0, 1), `RobotImpulse` 4000, `VehicleImpulse` 2000.
- **Handler (C):** for each target pawn **inside TriggerVolume**:
  - robot form → `SetPhysics(PHYS_Falling)`, `Velocity = Dir × 4000` (replaces velocity);
  - vehicle form → `Mesh.AddLinearVelocity(Dir × 2000)` (additive, rigid body).
  - Jump pads / vents.

## 22. SeqAct_TnUnlockAchievement (18)

- **VAR** Targets (players) · **Props** `Achievement` (enum; the check requires ≠ 0).
- **Handler (C):** `TnPlayerController.ClientUnlockAchievement(Achievement)` for each player target (pawn → controller).

## 23. TnSeqAct_ShootFromShieldedLocation (15)

- **IN** Enable · **VAR** Target → Targets, Shielded Location → ShieldInteract (must be a TnInteractShieldedPositionBase).
- **Handler (C):** `C.set_ShieldInteract(interact)` per AI controller: the AI uses that shielded firing position (cover
  interact).

## 24. TnSeqAct_AchievementEventCounter (13)

- **IN** Hit · **VAR** Targets (players) · **Props** `Achievement`, `CounterNumber`.
- **Handler (C):** `PC.ClientSetBitOnAchievement(Achievement, CounterNumber)`: sets one bit of a multi-part achievement's
  progress mask.

## 25. TnSeqAct_SpecialSpawnLogic (6)

- **IN** Start, Stop, Handled · **OUT** Spawned.
- **VAR** SpawnIsAI → IsAiSpawn (Bool, written).
- **Props:** `Character` (whose spawn this handles); latent, `bAllowActivationWhileActive`.
- **State machine (C):** ActionState 0 idle / 1 armed / 2 handling.
  - **Start:** 0 → 1.
  - **Stop:** → 0. If it was handling, it logs an error and completes the spawn.
  - **Handled:** completes. If not handling it logs, and does nothing while idle.
  - `UseThisLogicForPlayer(name)`: armed AND `Character.AiCharacterDataResolved.CharacterName == name`.
  - `NotifySpawned(C)` (from the spawn code): when armed → `IsAiSpawn = (C is not a PlayerController)`, publish the variable,
    `SpawningDude = C`, state 2, fire *Spawned*.
  - `OnSpawnHandled`: if a player → `TnGame.NotifySpecialSpawnHandled(PC)` (script stub; native continues the spawn, H); clear;
    back to armed.
- So level script can intercept a specific character's (re)spawn: e.g. play a matinee, then signal *Handled*.
- **The caller is native (H):** no script calls UseThisLogicForPlayer / NotifySpawned.

## 26. TnSeqAct_SetCoopSpawnPointSet (5)

- **IN** Set, Clear · **VAR** Navigation Points → NavPoints.
- **Handler (C):** on `HmSpawnPointSetInterface(WorldInfo.Game)`:
  - Set → `SpawnPointArray = NavPoints (NavigationPoints only)`, `UseSpawnPointArray = true`;
  - Clear → empty, false.
  - It restricts co-op (re)spawns to those points.

## 27. SeqAct_HmSetAiGoalSet (4, HM_Engine)

- **VAR** Targets · **Props** `GoalSet` / `PackageGoalSet`, `bUsePackageGoalSet`.
- **Handler (C):** per target implementing HmGoapWorldStateInterface with a plan runner:
  `GetGoapPlanRunner().ReplaceGoalSet(Clone(goal set))`. It swaps the AI's whole GOAP goal set.

## 28. TnSeqAct_TrypticonSentry (4)

- **IN** Display Healthbar, Damage Sentry, Kill Sentry · **VAR** Targets[0] = the TnTrypticonPawn (or its controller).
- **Props:** `Sentry`, `DamageSentryAmount`.
- **Handler (C):**
  - Display → `Trypticon.AddAttachedSentryObjectiveMarker(Sentry)`;
  - Damage → `DamageAttachedSentry(Sentry, amount)`;
  - Kill → `KillAttachedSentry(Sentry)`.
  - See CAMPAIGN_SETPIECES_RE (Trypticon).

## 29. TnSeqAct_StartBombingRun (2)

- **VAR** Targets (AI) · **Props** `BombingRunInfos[]`.
- **Handler (C):** per target AI controller: pick a **random** BombingRunInfo →
  `SetWorldStateInt(TnAiWorldStateBombingRun, 1, info)`; `RequestReplan(1)`. The planner runs the bombing-run action toward it.

## 30. TnSeqAct_BeginPrisonWalk (2)

- **IN** Begin · **VAR** Targets.
- **Props:** `WeaponTypesToDestroy` [WT_Primary, WT_Secondary, WT_Grenades].
- **Handler (C):** per target pawn: destroy those weapons, give `TnWeaponEmpty`, then weapon type 6 → `ConsumeAllAmmo()` +
  `HideAmmoHudUntilFilled()`. The unarmed "prison walk" state.

## 31. TnSeqAct_ToggleHealthDisplay (2)

- **IN** (per instance) Enable / Disable / Toggle (input index 0 / 1 / 2) · **VAR** Targets (TnServiceProviders).
- **Handler (C):** the target's `TnHealthDisplayService.IsEnabled` = true / false / flip. It errors if the target has no service.
  Boss / NPC health bars.

## 32. TnSeqAct_UnlockAchievementInVolume (1)

- **VAR** Targets (players) · **Props** `Achievement`, `TriggerVolume`.
- **Handler (C):** `ClientUnlockAchievement(Achievement)` only for players whose pawn **is inside TriggerVolume**.

---

## Native dependencies to provide (summary)

- **Dialog:** HmDialogInterface on speakers (PlayDialog / StopDialog / GetIsDialogPlaying + event callback),
  HmDialogEventManager groups, AudioDevice mixer presets for ducking.
- **Conversation** (§1) and **PlayDialog Finished** (§10) sequencing are native (H, described).
- **AI:**
  - TnAiController fields / world states: `_DefendActor`, `ShootAtActor`, `_AttackFromActor`, `_SnipeSearchLocations`,
    `ShieldInteract`, god flags, `TnAiWorldState{MoveToObjective, GodStatus, BombingRun, InteractedWith}`, `RequestReplan`;
  - the squad (`TnAiSquad`) objective / pending-interact tables;
  - sentry `_ForcedTarget` / dormant.
- **Game:** objective markers (TnObjectiveManager), score tracker events, achievements (client unlock / bit), co-op spawn-point
  set, special-spawn interception (native caller), music stingers (HmMusicPlayer), IK look-at interface, latent-action
  framework (ForceIntoForm).
