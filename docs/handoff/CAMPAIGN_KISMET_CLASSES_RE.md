# Campaign Kismet class census: sizing the campaign scripting step (RE, 2026-10-18)

Data: `notes/data/campaign_kismet_classes.json` (`tools/campaign_kismet_classes.py`). Per class: instance count, per-chapter
counts, status, implementation kind, the Rebuild source files that reference it, and the RE handoff docs that decode it.

**Scope:** all level packages listed in AssetTools' `manifests/campaign/<ch>_assets.json` (A1–A5, D1, D2, D3A, D3B, D5).
authored.db classes matching Seq* / TnSeq* / HmSeq* / Sequence* / InterpData. **3,197 `Sequence` objects** (Integration's 3,189 +
8 sub-sequences in companion levels), **176 Kismet classes, ~130k instances.**

**Status:**
- **(a)** the class name is referenced by `Rebuild/src`. This is a token match, so the scope can be partial: e.g. `SeqAct_Delay` /
  `Gate` are handled inside the LevelAudio script subset, and `SeqEvent_RemoteEvent` / `SeqAct_ActivateRemoteEvent` inside
  FrontendScene. They are not a general Kismet VM.
- **(b)** not in src, but decoded in a WFC RE handoff doc.
- **(c)** neither.
- `SequenceFrame*` are editor comment boxes (no runtime).

**Kind:**
- `script`: the class has a real UnrealScript body (Activated / Update…, decompiled in work/script); semantics are readable from
  bytecode.
- `native`: no or trivial script; the handler is C++. For stock UE3 classes the behaviour is the documented engine one.
- `data`: variables / matinee data.
- `event`: fired by the engine or other actors.

## 1. Totals

| Status | Classes | Instances | Breakdown |
|---|---|---|---|
| (a) referenced by Rebuild | 26 | 49,720 | stock 18 (2 data, 6 event, 7 native, 3 script) · WFC 8 (5 native, 3 script) |
| (b) decoded in RE docs only | 36 | 50,353 | stock 9 · WFC 27 (9 event, 8 native, 10 script) |
| (c) undecoded | 113 | 24,455 | stock 79 (51 native, 12 script, 11 data, 5 event) · WFC 34 (32 script, 2 data) |
| editor-only | 2 | 5,443 | SequenceFrame, SequenceFrameWrapped |

**Reading it:**
- Most **instances** are already in (a) or (b). The bulk is SeqVar_Object (40k, data), the remote-event pair (20k), Delay,
  Interp / InterpData, Touch, Toggle, Gate, SequenceActivated.
- The **(c) work is wide but shallow:**
  - **51 stock native classes:** variable math / compare / switch, damage, destroy, teleport, attach, physics / collision, mips
    hints, god mode, … These are standard UE3 behaviour; implement from the engine semantics, with no RE needed.
  - **32 WFC script classes:** dialog / conversation, markers, AI move / shoot / sentry, achievements, form forcing, turrets.
    Their bytecode is decompilable on request: one decode per class, mostly short.
  - **No WFC native-only class is undecoded.** Every Tn / Hm class without script is already in (a) or (b).

## 2. (a): referenced by the Rebuild runtime (verify scope per file)

| Class | Instances | Kind | Where (src) |
|---|---|---|---|
| SeqEvent_RemoteEvent / SeqAct_ActivateRemoteEvent | 10,314 / 10,204 | event / native | frontend/FrontendScene |
| SeqAct_Delay | 4,560 | native | game/LevelAudio* (audio subset) |
| SeqAct_Interp (+ InterpData 3,601) | 3,640 | native | core/Application, frontend/FrontendScene |
| Sequence | 3,197 | data | FrontendScene |
| SeqEvent_Touch | 3,113 | event | game/AmbientAudio |
| SeqAct_Gate | 2,736 | native | LevelAudio |
| SeqAct_PlaySound | 1,529 | native | LevelAudio |
| SeqAct_ToggleHidden | 1,473 | native | FrontendScene, game/MapState |
| SeqAct_TnPawnFactory | 1,263 | native | FrontendRuntime |
| SeqAct_PlayPlayerPositionalSound / PlayFlybySound | 915 / 133 | script / native | AmbientAudio, LevelAudio |
| SeqEvent_GameplayStarted | 777 | event | GameFlow, AmbientAudio |
| SeqAct_Reverb / AmbientAudioZone / Mixer | 399 / 392 / 280 | native | AmbientAudio |
| SeqAct_PlayMusic / StopMusic | 384 / 154 | script | audio / frontend |
| SeqEvent_Console | 264 | event | LevelAudio |
| SeqAct_TnSetCurrentObjective | 179 | script | frontend/CampaignObjectives |
| SeqAct_ToggleCinematicMode | 168 | native | FrontendRuntime |
| SeqAct_MoviePlayer, TnSeqAct_DisplayTutorialMessage, SeqEvent_PlayerSpawned, SeqEvent_PickupStatusChange | 16 / 14 / 13 / 2 | | frontend / LevelAudio / PickupFactory |

**Caveat:** several (a) classes are implemented for one subsystem only:
- Delay / Gate / PlaySound in level-audio sequences;
- RemoteEvent in the frontend scene.

The campaign needs them **generalised to all sequences**. Count them as "pattern exists", not "done".

## 3. (b): decoded in RE docs, not in the runtime (36)

Largest by instances:

| Class | Instances |
|---|---|
| SeqVar_Object | 40,345 |
| SeqAct_Toggle | 3,125 |
| SeqEvent_SequenceActivated | 2,574 |
| SeqEvent_HmDestructibleStateNotification | 1,319 |
| TnSeqAct_SpawnPickupRewards | 831 |
| SeqAct_TnAssignTacticalVolume | 433 |
| SeqAct_TnAiMoveToActor | 389 |
| TnSeqEvent_Interact | 244 |
| SeqAct_SetActiveCheckpoint | 209 |
| SeqAct_SetStreamingState | 200 |
| TnSeqAct_TransformWorldProp | 181 |
| TnSeqAct_ActivateTogethernessVolume | 134 |
| SeqEvent_PreGameplaySetup | 81 |
| SeqAct_TnToggleDestructible | 55 |
| SeqAct_TnAiUseTurret | 54 |
| SeqAct_FlushLevelStreaming | 46 |

Plus the set-piece / boss classes (Trypticon, Omega, MissionComplete, DownRevive, DetPack, TankStatus…).

Docs: CAMPAIGN_KISMET_HANDLERS_RE, CAMPAIGN_STREAMING_RE, CAMPAIGN_CONTENT_DELTAS_RE, CAMPAIGN_AI_LAYER_RE, CAMPAIGN_SETPIECES_RE,
CAMPAIGN_HUD_OBJECTIVES_RE, MP_TRANSFORMING_ACTORS_RE (the json lists the docs per class). These are implementation-ready.

## 4. (c): undecoded (113)

**WFC script classes (32), decode on demand, by instances:**

| Class | Instances |
|---|---|
| SeqAct_Conversation | 892 |
| SeqAct_TnAiSetMoveToObjective | 691 |
| SeqAct_TnRemoveMarker / AddMarker | 389 / 385 |
| SeqAct_TnAiUseInteract | 351 |
| SeqAct_PlayStinger | 250 |
| TnSeqAct_DefendActor | 151 |
| SeqAct_TnAiShootAtActor | 105 |
| TnSeqAct_AttackFromActor | 97 |
| SeqAct_PlayDialog / DialogGroup | 94 / 94 |
| TnSeqAct_ForceIntoForm | 93 |
| TnSeqAct_SetSniperSearchLocations | 89 |
| TnSeqAct_SetTargetableByAi | 50 |
| SeqAct_TnAiSentryForceTarget | 47 |
| TnSeqAct_AddCompetitiveScore | 43 |
| TnSeqAct_SetAiGodStatus | 35 |
| SeqAct_CrowdSpawner | 33 |
| SeqAct_TnAiSentrySetDormant | 29 |

Plus about 14 classes with fewer than 20 instances each (achievements, special spawn logic, prison walk, bombing run, …).

**Stock UE3 native classes (51): engine semantics, no RE needed**, by instances:

| Class | Instances |
|---|---|
| SeqAct_CauseDamage | 1,400 |
| SeqAct_Destroy | 748 |
| SeqAct_ForceFeedback | 639 |
| SeqAct_CameraShake | 601 (WFC camera-shake params, see WEAPON_CAMERA_SHAKE_RE) |
| SeqAct_Switch | 509 |
| SeqCond_CompareBool | 432 |
| SeqAct_FinishSequence | 390 |
| SeqAct_AttachToActor | 342 |
| SeqAct_Log | 335 |
| SeqAct_SetBool / Int / Float | 303 / 183 / 143 |
| SeqAct_Teleport | 230 |
| SeqAct_ToggleNavGate | 195 |
| SeqCond_Increment | 161 |
| SeqAct_ChangeCollision | 141 |
| SeqAct_RandomSwitch | 132 |
| SeqCond_Compare* | |
| SeqAct_AttachToEvent | 101 |
| … | |

**Stock script (12):** ObjectListLength, CompareString, RandomObject, AttachComponent, SetActorMatInst*Param, …
**Data (13):** SeqVar_* (Named 1,772, Int, Player, Float, External, Bool, String, RandomFloat, ObjectList…) and TnSeqVar_Character
(2,863) / Buddy (679).
**Events (5):** TakeDamage 125, Destroyed, LOS, Mover, Death.

## 5. Sizing

1. **Generalise the existing subset into a sequence VM:** links, variables, SequenceActivated, RemoteEvent, Delay / Gate /
   Switch / Toggle, Touch. That covers about 75 % of instances by itself.
2. **Implement (b) from the RE docs:** 36 classes, specs exist.
3. **Stock (c):** 51 native + 12 script + 13 data + 5 events, from engine semantics.
4. **WFC (c) script:** 32 classes. I can decode these in batches (each one is short bytecode). Highest value:
   Conversation / PlayDialog / DialogGroup, the AI move / shoot / defend set, markers.
