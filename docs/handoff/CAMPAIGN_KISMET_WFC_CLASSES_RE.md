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
- **DialogEventCallback (C, script + its native twin 0x830E7030), by event value:**
  - 0 (started): PlayingCount++, **enable** the duck preset;
  - 1 (stopped): fire *Stopped*, then PlayingCount--;
  - 2 (ended): PostDelayTimer = PostDelayTime, FinishedCount++, then PlayingCount--;
  - after 1 / 2: if PlayingCount is still ≥ 0 → **disable** the duck once (one disable per ended / stopped line, pairing each
    enable); if it went negative → clamp to 0 (no disable);
  - 3 / 4 (interrupted / failed): fire *Stopped* **only**. No PlayingCount decrement and no duck disable.

**Native op — CONFIRMED (2026-10-18, Ghidra):**
- **Inputs (0x8276F538, called each tick through vfunc 0x170):**
  - **Play impulse** → gather the Target objects; if there are none, PlayingCount = 0. For each target call script
    `PlayDialog(target, SoundCue)`. The impulse is cleared.
  - **Stop impulse** → for each target call script `StopDialog(target, SoundCue)`, then PostDelayTimer = 0, FinishedCount = 0. The
    impulse is cleared.
- **Update (0x827644D8):**
  - while **PlayingCount > 0** → keep running;
  - else `PostDelayTimer −= dt`; while it is still > 0 → keep running;
  - otherwise the op **completes**, firing **Finished only if FinishedCount ≠ 0** (at least one line ended naturally), then
    FinishedCount = 0.
  - So **Finished = the last playing line ended + PostDelayTime**. A Play with no valid target completes on the next tick with
    no output. A Stop completes the op with Stopped (from the callback) and no Finished.
- **Ducking:** EnableDuckingMixerPreset 0x8274E120 / Disable 0x8274E198 → AudioDevice vfunc 0x130 / 0x134 with
  DuckingMixerPreset (+0xA8).
- **Edge case (C):** a line that ends with event 3 / 4 leaves PlayingCount > 0, so the op does not complete and its duck stays
  enabled until a later Stop (count → 0) or another line's end. A rebuild should treat 3 / 4 like 1 if it wants the op to finish.
  The original does not.

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

---

## 1a. SeqAct_Conversation native sequencer — CONFIRMED (2026-10-18, Ghidra)

Supersedes the H part of §1. **Natives** (table 0x83707500): execPlay 0x827AAD58 → **Play 0x8276EE00**; execStop → **Stop
0x82769E58**; execOnEnded → **OnEnded 0x82764048**; execOnDialogEnded → **0x82769F18**; execDialogEventCallback → **0x8276ED30**;
execOnDialogPlayed sets the "line started" flag.
**Op hooks:** Activated **0x827782F0** (input 0 Play → Play; input 1 Stop → Stop); **Update 0x82787588**; line start
**0x82784688 / 0x827845B8**.

**Fields:**

| Offset | Field |
|---|---|
| +0x90 / +0x94 | Lines data / count (stride 0xA0; +4 = PostDelayTime) |
| +0x9C | DuckingMixerPreset |
| +0xA4 | CurrentLineIndex |
| +0xA8 | flags: bit 31 = current line issued, bit 30 = speaker reported "started" |
| +0xAC | PostDelayTimer |

OutputLinks: 0 Played, 1 Stopped, 2 Finished, **3 + i = "line i ended"** (optional per-line outputs; fired only if the instance has
that many outputs).

**Play:**
- `Stop()` (ends any current line);
- CurrentLineIndex = 0, clear "issued";
- **enable DuckingMixerPreset** (AudioDevice vfunc 0x130);
- fire **Played**.
- The first line starts on the next Update.

**Update(dt), every tick while active:**
1. If PostDelayTimer ≥ 0: `timer −= dt`. While it's still ≥ 0, return (waiting). When it drops below 0: clear "issued",
   **CurrentLineIndex++**. The timer starts at −1, so there is no wait before line 0.
2. If CurrentLineIndex ≥ count → **OnEnded(Finished)**: **release the duck** (vfunc 0x134), fire **Finished**. The op completes.
3. Else, if the line isn't issued yet, **start line i**:
   - **Speaker** = variable link **2i** (object var; a Controller resolves to its Pawn).
   - **DialogType = ConversationStart if this speaker has not spoken in an earlier line of this conversation, else
     Conversation** (0x82764208 walks back over lines i−1…0 comparing speakers).
   - Calls script `PlayDialog(speaker, Lines[i].SoundCue, type)` → `HmDialogInterface.PlayDialog(cue, true, type, false,
     DialogEventCallback)`.
   - **The line counts as started only if the speaker's "started" event (0) arrived synchronously.** Then the **Face** variable
     (2i+1) is resolved (no gameplay use in this path) and a debug on-screen line is printed when the global debug flag is set.
   - **If there is no speaker, no cue (script PlayDialog returns early), or the speaker didn't start → Stop() → the conversation
     ends as Stopped.**
4. If CurrentLineIndex < 0 (Stop happened) → **OnEnded(Stopped)**: release the duck, fire **Stopped**. The op completes.

**Speaker callbacks (DialogEventCallback):**

| Event | Effect |
|---|---|
| **0** started | set "started" |
| **2** ended | clear "started", **PostDelayTimer = Lines[i].PostDelayTime**, fire output **3+i** if present. The next line starts when the timer runs out (0 → the next tick). |
| **1 / 3 / 4** (stopped / interrupted / failed) | `OnDialogEnded(false)` → **Stop()** → the conversation ends as **Stopped** on the next Update |

**Stop (input 1, or any abort):** if a line is issued and started → `StopDialog(speaker)` for that cue; CurrentLineIndex = −1,
PostDelayTimer = −1 → the next Update fires **Stopped** and releases the duck.

**Not present:** no skip-on-button input in the conversation op (no input polling; any skip must come from elsewhere, e.g. cinematic
skip stopping the dialog → event 1 → Stopped). No per-line timeout: a line lasts as long as the speaker's dialog component says.

## 25a. TnSeqAct_SpecialSpawnLogic caller — CONFIRMED (script in TransContent, not native)

The caller is **TransContent `TnCoopGame`** (map-embedded script: work/script/decomp_tc), which my earlier search missed:

```
CheckSpecialSpawnLogic(C, CharacterName):
    WorldInfo.GetGameSequence().FindSeqObjectsByClass(TnSeqAct_SpecialSpawnLogic, recursive=true, Actions)
    for each action: if action.UseThisLogicForPlayer(CharacterName) { action.NotifySpawned(C); return true }   // first match wins
    return false
```

**Triggers:**
1. **Player join / respawn:** `TnCoopGame.InProgress.NotifyPlayerAdded(PC)`:
   - if **no** special logic claims the PC's selected character → `SendUIEventToControllers(5, PC)` immediately (the normal
     "player spawned" UI event);
   - otherwise the UI event waits until Kismet fires **Handled** → `TnSeqAct_SpecialSpawnLogic.OnSpawnHandled` →
     `TnGame.NotifySpecialSpawnHandled(PC)` → `TnCoopGame.InProgress.NotifySpecialSpawnHandled` → `SendUIEventToControllers(5, PC)`.
2. **AI buddy spawn:** `TnAiSquad.SpawnAiCharacter` → `Game.CheckForSpecialSpawn(AC, CharacterName)` → the same check. Here
   *SpawnIsAI* = true; Handled has no UI event (not a PlayerController).

The action must be **armed** (Start input) and its `Character` must match the spawning character's name.


---

# Batch 2: the next 32 classes by campaign instance count (RE, 2026-10-18)

**Progress: 64 / 176 classes decoded** (batch 1 = §1–32 above; batch 2 = §33–64 below). Ranking from
`notes/data/campaign_kismet_classes.json`: status (c), most instances first, editor-only frames skipped.

**Locator:** `tools/kismet_native.py <Class>` resolves the wide name "U<Class>" → class registration → InternalConstructor → vtable.
Slots, CONFIRMED by diffing the vtables of SetBool / SetInt / Switch / Log / CauseDamage / Conversation:

| Slot | Meaning |
|---|---|
| **+0x170** | **Activated** (per class) |
| +0x16C | Update (latent ops only; the base is 0x82F09458) |
| +0x17C | the input-impulse hook (base 0x82F09438; FinishSequence calls it on the parent's linked ops) |
| SeqVar +0x164 / +0x168 / +0x16C / +0x178 / +0x180 | GetIntRef / GetBoolRef / GetFloatRef / GetStringRef / **GetObjectRef** |
| SeqVar +0x184 | GetValueStr |

**Generic action dispatch (C, 0x82F4C7B8):** this is the shared SequenceAction Activated, used by every action without its own
native body.
- When bCallHandler is set, for each **Targets** entry, call the script function **"On" + class name minus "SeqAct_"** on the target
  (FindFunction + ProcessEvent).
- If the target lacks it: if the target is a Pawn, try its Controller; if a Controller, try its Pawn.
- Below, "handler" names the script that actually runs.

Labels: C = bytecode / native code read; H = stock UE3 behaviour, native body not read (address given). Classes with no "In / Out"
listed inherit the SequenceAction defaults (In → Out).

## 33. TnSeqVar_Character (2,863, data, WFC)
- Props: Character (TnCharacterData), ChassisTypePathName (resolved at load / edit), VarName.
- **GetObjectRef 0x829F8658** → 0x829F85F0 → script `FindMatchingPawn` (C): the **first TnPawn in AllPawns whose
  `_CurrentCharacter.ChassisType` path equals ChassisTypePathName**, re-resolved on every read (none → empty).
- `GetCharacterName` = Character.AiCharacterDataResolved.CharacterName (the AI actions use it; batch 1 notation).
- **Ours:** resolve by chassis path at read time, not cached; it can return a player pawn when the player plays that character.

## 34. SeqVar_Named (1,772, data)
- Props: FindVarName, ExpectedType.
- Resolved at sequence load to the same-named variable anywhere in the level's sequences (stock). The ref slots are stubs
  (0x827BF7C8), and GetValueStr is 0x82F25D60.
- **Ours:** link at load by VarName across all sequences of the level (persistent + streamed); unresolved = empty.

## 35. SeqVar_Int (1,563) · 38. SeqVar_Float (1,220) · 45. SeqVar_Bool (502) · 48. SeqVar_String (380)
- Plain value holders: GetIntRef / GetFloatRef / GetBoolRef / GetStringRef return the address of Value (0x82794B10 shared).
- **Ours:** a typed cell shared by every linked op.

## 36. SeqAct_CauseDamage (1,400)
- **VAR** Target → Targets, Amount → DamageAmount (Float), Instigator (Object). Props: DamageType (default DmgType_Default),
  Momentum.
- Native vtable 0x82312B08; **Activated = generic dispatch → `Actor.OnCauseDamage`** (C):
  - instigator controller = Action.Instigator as Controller, or its Pawn's Controller;
  - `TakeDamage(DamageAmount, InstigatorController, Location, −Momentum along the rotation, DamageType)`.
- `Controller.OnCauseDamage` forwards to its Pawn.
- **Ours:** use the normal damage path (death / gib / TakeDamage events fire). The campaign uses it for scripted kills and wall
  breaks (e.g. Escalation's 200-damage walls).

## 37. SeqVar_Player (1,273, data)
- Props: bAllPlayers (default true), bOnlyLocalPlayers, PlayerIdx.
- **GetObjectRef 0x82F25898** (C):
  - rebuilds the list on every read from the controller list: PlayerControllers, local only when bOnlyLocalPlayers;
  - bAllPlayers → all of them; else Players[PlayerIdx].
- **Ours:** co-op aware. All human players, or local only when flagged.

## 39. SeqVar_External (1,020, data)
- VariableLabel. It's the sub-sequence's input port: the parent Sequence's variable link of that label supplies the value
  (stock). Ref slots are stubs.
- **Ours:** alias to the parent sequence's linked variable.

## 40. SeqAct_Destroy (748; no native class)
- Handler **`Actor.OnDestroy`** (C): bNoDelete or a non-authority role → Shutdown(); else DestroyActor().
  - `Pawn.OnDestroy` first calls `Game.NotifyExplicitDestroy(self)`;
  - `PlayerController.OnDestroy` refuses ("Cannot use Destroy action on players").
- **Ours:** destroy targets, with those three rules.

## 41. TnSeqVar_Buddy (679, data, WFC)
- Prop: BuddyIndex 1–2.
- **GetObjectRef 0x829F79C0** → script `FindBuddy` (C): `CampaignGame.GetAiTeammates()[BuddyIndex − 1].Pawn` (none if absent).
- `GetCharacterName` = `AiSquad.GetMemberNames()[BuddyIndex − 1]`.
- **Ours:** AI teammate slot 1 / 2 of the campaign squad, resolved per read.

## 42. SeqAct_ForceFeedback (639; no native class)
- **IN** Start, Stop. Prop: FFWaveform.
- Handler **`PlayerController.OnForceFeedback`** (C): Start → ClientPlayForceFeedbackWaveform(FFWaveform); Stop → stop it.
- **Ours:** rumble on the targeted players (respect the PC rumble setting).

## 43. SeqAct_CameraShake (601; no native class; HM handler)
- **IN** Timed, Continuous, Stop. Props: CameraShake, GlobalScale, GlobalAmplitudeScale, GlobalFrequencyScale (all 1).
- Handler **`HmPlayerController.OnCameraShake`** (C):
  - shake = CameraShake with TimeDuration × GlobalScale; Rot / Loc / FOV Amplitude × (GlobalScale × GlobalAmplitudeScale);
    Frequency × (GlobalScale × GlobalFrequencyScale);
  - **Timed** → PlayCameraShake(shake);
  - **Continuous** → TimeDuration = 9,999,999;
  - **Stop** → TimeDuration = −1 (replaces the running shake).
- Shake model: WEAPON_CAMERA_SHAKE_RE.

## 44. SeqAct_Switch (509)
- **OUT** Link 1…LinkCount. **VAR** Index → Indices (Int). Props: LinkCount, IncrementAmount (1), bLooping, bAutoDisableLinks.
- **Activated 0x82F5A970** (C, asm), for each Indices value i (1-based):
  - if 1 ≤ i ≤ #outputs and that output isn't disabled → fire it, and if bAutoDisableLinks disable it;
  - then if IncrementAmount ≠ 0: i += IncrementAmount, and if bLooping, i = ((i − 1) mod #outputs) + 1;
  - written back to the variable.

## 46. SeqCond_CompareBool (432)
- **OUT** True, False. **VAR** Bool.
- **Activated 0x82F308F0** (C): result = AND of all linked bools (none → true); fire True if true else False, unless that output is
  disabled.

## 47. SeqAct_FinishSequence (390)
- Prop: OutputLabel.
- **Activated 0x82F183C0** (C): in the parent Sequence, find the output link bound to this op. For each op linked from it (not
  disabled): set its input impulse, call its input hook (+0x17C), and queue it on the parent sequence (+0x19C).
- **Ours:** a sub-sequence output port that fires the parent's outgoing links of that label.

## 49. SeqVar_RandomFloat (367, data)
- Props: Min (0), Max (1). **GetFloatRef 0x82F506D8** (C): a **new uniform random in [Min, Max] on every read**.

## 50. SeqAct_AttachToActor (342; no native class)
- **VAR** Target, Attachment. Props: bDetach, bHardAttach (true), BoneName, bUseRelativeOffset / Rotation, RelativeOffset /
  Rotation.
- Handler **`Actor.OnAttachToActor`** (C), for each Attachment (a Controller → its Pawn):
  - bDetach → SetBase(none);
  - else DoKismetAttachment(attachment, action) on the target (a Controller target → its Pawn): stock SetBase with bHardAttach,
    bone and relative offset.

## 51. SeqAct_Log (335)
- **VAR** String / Float / Bool / Object / Int / Target. Props: bOutputToScreen, bIncludeObjComment, TargetDuration.
- **Activated 0x82F30280** (C): concatenates every linked variable's GetValueStr into one line and logs it (screen output when
  enabled).
- **Ours:** debug log only. Don't show it on screen in normal play.

## 52. SeqAct_SetBool (303)
- **VAR** Target (Bool), Value (Bool). **Activated 0x82F309B8** (C): v = AND of the Value vars (none → the DefaultValue bit); write
  v into every Target bool.

## 53. SeqVar_ObjectList (277, data)
- An object array. GetObjectRef 0x82F18080 serves indexed entries (stock). The list ops (SeqAct_ModifyObjectList) are separate.

## 54. SeqAct_Teleport (230; script CheckForErrors only)
- **VAR** Target, Destination → DestinationActor. Prop: bUpdateRotation (true).
- Handler chain (C):
  - `Controller.OnTeleport` → Pawn.
  - `Pawn.OnTeleport`:
    - server only;
    - picks the Destination matching the target's index in the Target list (else the first);
    - a Controller destination → its Pawn;
    - SetLocation + PlayTeleportEffect;
    - with bUpdateRotation: pawn, controller and client rotation + DesiredRotation;
    - then Controller.OnTeleport(none).
  - **`TnPawn.OnTeleport`**:
    - ends any interaction;
    - anchors to the Destination NavigationPoint (or the best anchor);
    - after the move, traces 500 UU down: in vehicle form snaps to the ground + height + 1 and FindSpotAwayFromPawns;
    - ClientTeleport for remote pawns; SetBase on the hit actor.
  - `TnAiController.OnTeleport` clears scripted turret states.
  - `TnPlayerController` → Game.NotifyTeleported.
  - `HmPlayerController` → camera Reset.

## 55. SeqAct_ToggleNavGate (195; no native class)
- **IN** Open, Close, Toggle. Handler **`NavigationPoint.OnToggleNavGate`** (C): bGateOpen = true / false / not bGateOpen, then
  Game.NotifyNavigationChanged.
- **Ours:** gate a nav point for AI pathing.

## 56. SeqAct_SetInt (183) · 58. SeqAct_SetFloat (143)
- **VAR** Target, Value. Activated **0x82F5B4D0** / **0x82F5B3F0** (H, stock): Value (sum of the linked Value vars, or the Value
  prop) written to every Target.

## 57. SeqCond_Increment (161)
- **OUT** A<=B, A>B, A==B, A<B, A>=B. **VAR** A → ValueA, B → ValueB. Prop: IncrementAmount (1).
- **Activated 0x82F583E0** (H, stock): A += IncrementAmount (written back), then fire every output whose relation holds.
- Used by Escalation's round counters (MP_ESCALATION_RE §5a).

## 59. SeqAct_ChangeCollision (141; no native class)
- Prop: CollisionPreset.
- Handler **`Actor.OnChangeCollision`** (C):
  - SetCollisionPreset(Action.CollisionPreset);
  - re-apply BlockRigidBody on the collision component;
  - ForceNetRelevant;
  - replicate bCollideActors / bBlockActors.

## 60. SeqAct_RandomSwitch (132)
- **OUT** Link 1…N. **VAR** Active Link → Indices.
- **Activated 0x82F5AC00** (C): pick uniformly among the **non-disabled** outputs and fire it; if bAutoDisableLinks, disable it;
  write the chosen index + 1 to Active Link.

## 61. SeqEvent_TakeDamage (125; script)
- **VAR** Instigator, Damage Taken. Props: DamageThreshold (100), MinDamageAmount, DamageTypes / IgnoreDamageTypes, bPlayerOnly.
- Fired from `Actor.TakeDamage` / `KActor.TakeDamage` → `HandleDamage` (C):
  - accumulates hits ≥ MinDamageAmount of valid types (bPlayerOnly: player instigators);
  - when CurrentDamage ≥ DamageThreshold → activate; Damage Taken = CurrentDamage (added if it already fired this tick);
  - then CurrentDamage −= DamageThreshold (or 0 if the threshold is ≤ 0).
- Reset clears it.

## 62. SeqCond_CompareObject (124)
- **OUT** A == B, A != B. **Activated 0x82F33388** (C): equal if every A equals every B, **treating a Pawn and its Controller as
  equal** (either side's +0x1C8 link).

## 63. SeqCond_CompareInt (119)
- 5 relation outputs. **Activated 0x82F580C8** (H, stock): fire every output whose relation between ValueA and ValueB holds.

## 64. SeqAct_AttachToEvent (101)
- **VAR** Attachee. **EV** Event. **Activated 0x82F33C40** (C):
  - for each Attachee (a Controller → its Pawn when flagged): instantiate a copy of each linked event for that actor;
  - add it to the actor's GeneratedEvents with originator = the attachee.
- So events (Death, TakeDamage …) start firing for spawned / dynamic actors.

**Next (65+), by instances:** SeqAct_SetActorMatInstScalarParam 92, SeqCond_CompareFloat 92, SeqAct_AddInt 83, SeqAct_GetDistance
81, …
