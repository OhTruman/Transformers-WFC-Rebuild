# Cinematic mode, campaign objectives and campaign HUD elements

WFC RE lane, 2026-10-18. Integration copies this to `docs/handoff`.

Sources:
- script: Engine.PlayerController, HmPlayerController, TnPlayerController, TnHUD, TnObjectiveManager, TnHudDataObserver*,
  TnTutorialMessageDisplayer, TransContent.TnCampaignGame;
- authored.db CDOs;
- the **Hud_GFX** ActionScript: `tools/as2dis.py ExtractedAssets/content/UI_GFxHud_p/Hud_GFX.gfx`.

Campaign and MP use **the same HUD class (`TnHUD`, the game's HUDType) and movie (`UI_GFxHud_p.Hud_GFX_1`)**. The campaign
elements are extra callbacks and data observers on that movie.

## 1. SeqAct_ToggleCinematicMode (CONFIRMED script)

- **Inputs:** Enable / Disable / Toggle. Server only ("Not supported on client").
- **CDO:** all flags true: bDisableMovement, bDisableTurning, bHidePlayer, bDisableInput, bHideHUD, bPauseable,
  bDisableIncidentalDialog.

| Flag | On enable | On disable (restore) |
|---|---|---|
| bHidePlayer | `Pawn.SetHidden(true)` | **`Pawn.SetHidden(false)` always**, whatever the flag |
| bDisableMovement | `IgnoreMoveInput(true)`, remembered in `bCinemaDisableInputMove` | `IgnoreMoveInput(false)` only if this mode disabled it |
| bDisableTurning | `IgnoreLookInput(true)` (`bCinemaDisableInputLook`) | the same, symmetric |
| bDisableInput (buttons) | TnPC: `ClearInputs()` (stop deferred fire, boost off) + `StopMovement()` (velocity 0). HmPC: `PlayerInput.SetCinematicButtonsDisabled(true)` + `SetInteractiveState(true)` | buttons re-enabled, interactive state false |
| bHideHUD | client `bHudOffInCinematicMode = true` | false. When the HUD comes back (and no full-screen movie is playing), **UI event 13** is sent to the UIController |
| bDisableIncidentalDialog | `DisableIncidentalDialog(true)`, only if the mode actually changes | false |
| bPauseable | `SetCinematicPauseable(true)` | `SetCinematicPauseable(false)` |
| (WFC extra) | **a downed player is revived**: `GetRevive(none, false)` | — |

- **No letterbox.** Letterboxing comes only from a CameraActor with `bConstrainAspectRatio` (matinee doc §2.2).
- **Other HUD hiders:**
  - `SeqAct_ToggleHUD` (stock);
  - `TnInteractActionHideHud`: turrets and the hacking terminal hide the HUD in their BeginUse sequence and unhide it at end
    or ForceEndUse.

## 2. Campaign objectives (CONFIRMED script + AS)

**Data:**
- the persistent level's `TnWorldInfo._ObjectiveBank`: an ordered list of keys, each localized at startup with
  `Localize("Objectives", key, "TransGame")`;
- `TnGameReplicationInfoGameBase.SetupObjectiveManager` loads the bank into `TnObjectiveManager`.

**Setting an objective** (`SeqAct_TnSetCurrentObjective`, property `Description` = a key):
1. The key is localized, then `GRI.SetCurrentObjective(text)` → `ObjectiveManager.SetCurrentObjective`.
2. The manager stores the **bank index**, which replicates as `CurrentObjective`. An empty description means no objective. An
   unknown text is a logged error.
3. **Client** (`ClientSetCurrentObjective`):
   - the previous objective is queued as "complete";
   - the new one is queued as "add";
   - `LastCompletedObjective = Current − 1`. The bank is ordered, so all earlier entries count as done.
4. **HUD** (each frame in `UpdateObjectiveManager`, between `StartMarkerUpdate` and `FinishMarkerUpdate`): it calls
   `_global.AddObjective(text)` / `_global.CompleteObjective(text)`.

**AS (sprite 607): `AddObjective`:**
- sound **`HUD_OBJECTIVE_ADDED`**;
- removes the previous objective clip;
- `attachMovie('mc_objectiveNew', …, {Objective})`. The clip shows the header **`$UIText.Hud.NewObjective`** (upper-cased) and the
  objective text upper-cased, revealed line by line (`mc_objectiveLineObjective` + `mc_objectiveLineGlyph` +
  `mc_objectiveLineGlyphMask`; per-line interp 0.5 s easeout).
- **`_global.CompleteObjective` is not defined in Hud_GFX.** The invoke is a no-op: **no "objective complete" visual exists.**

**Other uses:**
- `GetActiveObjective` / `GetCompletedObjectives` ("|"-joined) feed other UI. H: the pause-menu objective list.
- An objective countdown, `<CurrentGame:CurrentObjectiveCountdown>` (`objectiveTime_mc`), exists in the HUD.

**Markers:** the same system as MP (`MILESTONE_E_OBJECTIVE_MARKERS.md`). Campaign marker types are e.g.
`TnObjectiveMarkerTypeObjective` and `TnObjectiveMarkerTypeTransformerCampaign`, added via `AddObjectiveMarker`; 16 replicated
plus local markers.

## 3. Checkpoint and mission complete presentation (CONFIRMED script)

- **Checkpoint reached** (`TnCampaignGame.NotifyReachedCheckpoint` → `PC.ClientReachedCheckpoint`):
  - profile `ReachedCheckpoint(map, index)` + `SaveProfileSettings`;
  - fires `SeqEvent_CheckpointActivated` on the checkpoint;
  - a score event.
  - **There is no HUD text.** Any on-screen "checkpoint" feedback is authored Kismet, or (H) the storage save indicator.
- **Mission complete:** see the Kismet handlers doc: `TnSeqAct_MissionComplete` → profile + `EndGame("Complete")` → outro FMV →
  `ReturnToGameLobby`. There is no HUD element of its own.

## 4. Campaign HUD elements (CONFIRMED script + AS)

| Element | Script source | Movie callback | AS / symbols |
|---|---|---|---|
| **Interact / contextual prompt** | `TnHudDataObserverContextualCommands` pops `PC.ContextualCommander` notifications; the text is the command's localized description with bindings translated to `{x}`-style tokens | `_global.NotifyContextualCommand(ButtonID 0, Action, Text)`: **Action 0 = add, 1 = remove**; an empty text clears all | `mc_notify` list, **spacing −27 px**, newest at the top. Button glyph substitution via `setImageSubstitutions` (`{lb} {lt} {rb} {rt} {back} {start} {a} {b} {x} {y} {dup} {dright} {ddown} {dleft} [power]`, bitmaps `GamepadButton*`, `GamepadFaceButton*`, `GamepadDpad*`, `PowerIcon`). Removal fades out and re-stacks (0.2 s) |
| **Reviving a buddy** | `TnHudDataObserverReviveBuddy` (a ProgressBar subclass): `Pawn._RevivePercentDone`, `_ReviveTarget`; label `"Reviving `p"` with the player's name, or `"Reviving"` for AI; 0 when done | `_global.NotifyProgressBarChanged(Description, NormProgress)` | sprite 652: upper-cased `description_txt`; progress clamped to 0..1. Shows by sliding to y −140 at alpha 100 (0.5 s), hides at 0 (to y −120, alpha 0); `bar_mc._width = barBG_mc._width × progress`. The same bar serves hacking, bomb defuse and domination capture |
| **Own downed state** | `TnHudDataObserverNormalizedDownedHealth` | `_global.NotifyNormalizedDownedHealthChanged(h)` | `IncapacitatedScreenClip` (`.Revived()` on revive) |
| **Tutorial messages** | `TnSeqAct_DisplayTutorialMessage(MessageType)` → every PC → `TnHUD.DisplayTutorialMessage`. **Shown once per profile** (`HasShownTutorialMessage` / `MarkTutorialMessageAsShown`). Text = `Localize("TutorialMessages", <enum>, "TransGame")` with command → button translation; sound `BL_LVL_HUD_INTERFACE.WHSH_TUTORIAL_BOX` | `_global.NotifyTutorialMessage(text)` | Queued; each `mc_tutorialMessage` shows for **3000 ms**, then **minimizes** into a mid-right stack (25 px apart, interp 0.5 s), with a 200 ms gap between items. `mc_tutorialMessageTextTutorialPause` (`$UIText.Hud.TutorialPause`), `$UIText.Hud.TutorialAvailable` |
| **Objective** | §2 | `_global.AddObjective` | `mc_objectiveNew` |
| Buddy status / health bars | `TnObjectiveMarkerTypeTransformerCampaign` marker on every pawn (CONFIRMED, see "Buddy markers" below) | `_global.UpdateMarker` | see the objective-marker doc |

**Subtitles:**
- They come from `Engine.SubtitleManager` (High Moon script: QueueSubtitles, priority, speaker).
- The Hud_GFX movie has **no subtitle handler**, and TnHUD never draws them.
- **PARTIAL:** they are rendered by the engine's canvas path. Placement wasn't traced here.
- On/off comes from the profile (`RestoreSubtitles` / `DisableSubtitles`).
- A "speaker pip" observer (`TnHudDataObserverPipCharacter`) exists but is **never attached** (no script or data reference), so
  it is unused.

**Anchors:** the HUD lays out nine anchor clips (`topLeft_mc` … `bottomRight_mc`) from the safe margins (sprite 654
`updateStageScaling`). Which anchor each campaign element sits under is in the movie's display list; that wasn't extracted here.

## Buddy markers and health bars — CONFIRMED (2026-10-18)

**Registration:**
- `TnPawn.Tick` → `SetupMarker()`, once per pawn, when `DisplayObjectiveMarker` (default true), it has a PRI and a blueprint, and
  the GRI exists.
- `GRI.AddObjectiveMarker(label, pawn, GameClass.default.TransformerMarkerClass, DoNotReplicate = true)`. Every client
  creates its own markers locally.
- The label is the PRI PlayerName, or the localized character name (`TnDataProvider_Chassis.LocalizeUniqueIconicId`).
- Marker class by game: **TnCampaignGame and TnSurvivalGame → `TnObjectiveMarkerTypeTransformerCampaign`**; TnMultiplayerGame →
  `…TransformerVersus`.

**`TnObjectiveMarkerTypeTransformerCampaign`:**
- **Visibility:**
  - never on the viewer itself;
  - **enemies:** only while the viewer is **fine-aiming**, the enemy is not AI-cloaked, and it is in line of sight;
  - **allies:** always, except AI pawns that are not buddies, not downed and have `_ShouldDisplayName` false.
- **Setup:**
  - ally → `AllyMarkerSetup` (MarkerAlly_MAT, TargetHealthBar_MAT, label drawn focused and unfocused); a downed human ally →
    `DownedAllyMarkerSetup`;
  - enemy → `EnemyMarkerSetup` (MarkerEnemy_MAT, label only when focused);
  - LabelZOffset = half the collision height.
- **Label:** a downed non-buddy shows `DownedEnemyLabel`; AI pawns without `_ShouldDisplayName` show ""; otherwise the name.
- **Health bar:** `DrawLabel` draws the name, then **a health bar for an ally only when the viewer's weapon is a repair weapon**
  (`W.get_IsRepairWeapon()`) and the label is non-empty.
  - The bar is `TargetHealthBar_MAT` with scalar params `Health` (normalized) and `Neutral` (1 when FillColor is 0) via
    DrawMaterialTile.
  - So buddies show health bars only to a player holding the repair ray. There is no always-on buddy health bar.
- **`TnObjectiveMarkerTypeTransformerHealthBar`** (red fill, shown while the pawn rendered within the last 0.25 s and its
  TnHealthDisplayService allows it) is used by sentry / ability markers, not buddies.
