# Campaign HUD anchors, movies and bridge calls (Frontend C6, RE, 2026-10-23)

**Labels:** C = script / AS / data; H = inference.

**Sources:**
- `tools/gfxtree.py` (new: the frame-1 display tree from PlaceObject2/3 through DefineSprite);
- the AS dumps in `work/q/gfx`;
- TnUIController / TnCampaignGame / TnHUD script;
- the CDOs in authored.db.

This complements CAMPAIGN_HUD_OBJECTIVES_RE.md, which has the per-element behaviour.

## 1. Movies (C, CDO data)

| UI | Movie | Opened by |
|---|---|---|
| In-game HUD (campaign, Escalation and versus) | `UI_GFxHud_p.Hud_GFX_1` | TnHUD |
| **Mission failed** (campaign game over) | **`UI_GFxGameOver_p.CampaignGameOver_GFX_1`** | `TnUIControllerCoop.CampaignGameOverUI`; state `CampaignGameOver` |
| Pause | `UI_GFxPause_p.PauseMenu_GFX_1` | TnUIControllerCoop.PauseUI |
| End-of-game stats | `UI_GFxEndGameStats_p.EndGameStats_GFX_1` | EndGameUI (not used by the campaign mission-complete path, see section 4) |
| Message box / text prompt | `UI_GFxShared_p.MessagePrompt_GFX_1` / `TextPrompt_GFX_1` | TnUIController |
| Saving indicator | `UI_GFxSavingContent_p.SavingContent_GFX` ("$UIText.MessagePrompts.SavingContent" + "…StorageDevice") | H: native profile-save indicator. Referenced by every campaign base map |

TnCampaignGame's UIControllerClass = **TnUIControllerCoop**.

## 2. Hud_GFX layout (C, the display tree)

- The stage is **1120 × 720**.
- `hud_mc` (sprite 654) holds nine anchor clips, which `updateStageScaling` re-positions from the safe margins.
- The frame-1 authored positions (px), with each campaign element's sprite and its offset inside the anchor:

| Anchor (authored pos) | Child sprite @ offset | Element / `_global` callbacks |
|---|---|---|
| `topLeft_mc` (84, 54) | 572 @ (20, 10) | health: NotifySegmentedHealthChanged, NotifyOverShieldChanged, IsDead |
| | 588 @ (24, 70) | ability 1: NotifyAbilityType1Changed / UpdateCooldown / Jammed / ShowAbilityType1 |
| `topRight_mc` (1036, 54) | 593 @ (−115, 65) | NotifyAmmoAdded |
| | 600 @ (−20, 10) | clip / reserve ammo |
| | 601 @ (10, 46) | grenades: NotifyGrenadeAmmoChanged, ShowGrenades |
| | 603 / 605 @ (−24, 70) | abilities 0 / 2 |
| `bottomLeft_mc` (84, 666) | 591 @ (0, −40) | GameMessage, GameMessageSpectatorMode |
| `bottomRight_mc` (1036, 666) | **607 @ (−340, −124)** | **campaign objective: `AddObjective`** (mc_objectiveNew) |
| | 608 @ (−100, −27) | NotifyKillstreakChanged |
| | 609 @ (−50, −97) | killstreak buffs |
| | 610 @ (−40, −50) | CurrentWave (Escalation) |
| `midCenter_mc` (560, 360) | 621 @ (0, 0) | crosshair / reticule container, which also holds (offsets relative to screen centre): **613 NotifyCantTransform @ (0, 140)**, **614 TransformDisrupted @ (0, 150)**, **615 tutorial message `NotifyTutorialMessage` @ (0, 100)** (then minimizes into the mid-right stack, per the objectives doc), 616 weaponFeedback_mc (NotifyWeaponMessageChanged), 617 reload prompt @ (40, −10), 618 active grenade count @ (−40, −10), 620 lock-on @ (0, −120) |
| | 622 @ (0, 0) | damage direction indicators |
| `topCenter_mc` (560, 54) | countdown_mc 632 @ (0, 50) | objective / round countdown |
| | 633 @ (0, 5) | point multiplier |
| | 638 @ (0, 111.7) | GameAnnouncement, RewardAnnouncement |
| | 640 @ (0, 175) | PointEvent |
| `midLeft_mc` (84, 360) | — | (only the shared 589 child) |
| `midRight_mc` (1036, 360) | 643 @ (−80, 0) | NotifyLevelUp |
| | 644 @ (−30, −90) | NotifyBadConnection |
| `bottomCenter_mc` (560, 666) | **notifyManager_mc 646 @ (0, −30)** | **contextual / interact prompts: `NotifyContextualCommand`** |
| | **returnFlagNotifyTime_mc 652 @ (0, −100)** | **progress bar: `NotifyProgressBarChanged`** (revive / hack / defuse / capture) |
| root (outside hud_mc) | **incapacitated_mc 559 @ (560, 360)** | **own downed state: NotifyNormalizedDownedHealthChanged, NotifyDownedWarningFade** |
| root | sprite 558 @ (0, 0) | **world-space markers: StartMarkerUpdate / UpdateMarker / FinishMarkerUpdate** (objective markers, buddy markers) |
| hud_mc | zoomScopeAttachPoint_mc @ (560, 360) | showScope / hideScope |

- Sprite 589 appears once in every anchor: a shared per-anchor child (H: the safe-area debug or alignment marker).
- `hud_mc` (sprite 654) itself handles NotifyHudScrambledChanged, NotifySpectating and ToggleSpectatorHudMode.

## 3. Campaign elements and their bridge calls (C)

| Element | Script → movie | Movie → script |
|---|---|---|
| **Objective (new)** | `TnHUD.UpdateObjectiveManager` → `_global.AddObjective(text)`, inside the StartMarkerUpdate / FinishMarkerUpdate bracket; sound HUD_OBJECTIVE_ADDED | — |
| Objective complete | `_global.CompleteObjective(text)` is invoked but **not defined** in Hud_GFX: no visual | — |
| **Objective marker** | `_global.StartMarkerUpdate()` / `UpdateMarker(...)` / `FinishMarkerUpdate()` (sprite 558, screen-space) | — |
| **Checkpoint reached** | **No HUD element.** `PC.ClientReachedCheckpoint` → profile ReachedCheckpoint + SaveProfileSettings + SeqEvent_CheckpointActivated. The only visible feedback is H, the **SavingContent** save indicator during the profile save, or authored Kismet | — |
| **Mission failed** | see the flow below | `HmExternalInterface.Game.RestartPlayerFromLastCheckpoint()` / `RestartChapter()` / `QuitToMainMenu()`; `HmExternalInterface.Online.IsHost()`; `DataStores.ReadValue('<CurrentGame:GameOverMessage>')`, `'<PlayerOwner:TeamID>'` |
| **Mission complete** | **No HUD or menu movie.** `TnSeqAct_MissionComplete` → profile + `EndGame("Complete")` → the outro Bink (FMV path, see CAMPAIGN_RE_ENDSTATE pass 3 / 6) → ReturnToGameLobby | — |
| Interact prompt | `_global.NotifyContextualCommand(0, action 0 add / 1 remove, text)` | — |
| Revive / progress | `_global.NotifyProgressBarChanged(desc, 0..1)` | — |
| Downed | `_global.NotifyNormalizedDownedHealthChanged(h)` | — |
| Tutorial | `_global.NotifyTutorialMessage(text)` (once per profile) | — |

**Mission failed flow:**
1. `TnCampaignGame.InProgress.CampaignGameOver(WhoDied)`:
   - `BroadcastLocalizedMessage(TnCampaignGameOverMessage, WhoDied)`. On each client this sets `GRI.GameOverMessage` to
     **"Offline"** (yourself, or the team is down) or **"`p was Destroyed"** (a teammate, `p is their name);
   - then state InProgressOver.
2. After **`_DelayBeforeShowingGameOverScreen` = 1.0 s**:
   - `GRI._CampaignGameOver = true`;
   - every PC gets `OnCampaignGameOver`: StopMusic(0.25), DeathSound, GameOverMusic, the GameOverMixerPreset, dialog playback disabled;
   - AI plan runners are suspended and the AI scheduler is deactivated;
   - **UI event 14** goes to every controller.
3. `TnUIController.OnUIEvent(14)` (only if the selected character has chassis types) → state **CampaignGameOver** →
   `OpenUI(CampaignGameOverUI)`, subtitles off, force feedback off.
   - EndState / OnCleanUp restore them. OnRespawn → InGame.
4. The movie:
   - title `$UIText.GameOver.MissionFailed` (upper-cased);
   - `killMessage_txt` = GameOverMessage upper-cased;
   - buttons restartLastCheckpoint_mc / restartChapter_mc / quitGame_mc;
   - **a non-host has the two restart buttons disabled**, with focus on Quit;
   - the hint is A = `$UIText.ButtonHints.Accept`.
