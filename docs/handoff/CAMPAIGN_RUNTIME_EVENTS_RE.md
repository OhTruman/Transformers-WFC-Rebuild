# Campaign runtime events: one contract for Gameplay and the Frontend (RE, 2026-10-23)

This consolidates the chapter-level flow with the original's class, event and function names.

**Labels:** C = script / data / native as cited; H = inference.

**Detail lives in:**
- CAMPAIGN_STREAMING_RE (streaming natives, restart);
- CHECKPOINT_DATA_RE / CHECKPOINT_BODY (save contents);
- CAMPAIGN_KISMET_HANDLERS_RE (MissionComplete);
- CAMPAIGN_FRONTEND_FLOW_RE (menus);
- CAMPAIGN_COOP_SYNC_RE (join / leave / game over);
- CAMPAIGN_DIFFICULTY_RE (buffs).

**New in this item:** `tools/campaign_streaming_export.py` → **`work/handoff/campaign_streaming_order.json`**. Per chapter it has:
- every LevelStreamingState with its level list;
- every Checkpoint **in DisplayOrder** (the progression order), with its StreamingState, aux spawn count and menu index;
- the `SeqAct_SetStreamingState` ops.

## 1. Game classes (C)

| Mode | Game | PlayerController | UI controller | Lobby |
|---|---|---|---|---|
| SOLO | `TnCampaignGameSolo` (bShouldSaveCheckpoints, presence id 2) | TnPlayerControllerCampaign | TnUIControllerCoop | TnGameLobbyGameSolo |
| CP / CCP | `TnCoopGame` (no pause, MissionCompleteCountdown 10) | TnPlayerControllerCampaign | TnUIControllerCoop | TnGameLobbyGameCoop |

- All derive from `TnCampaignGame` ← `TnCampaignGameBase`.
- `GetGameTeamStatus` = 4 (GTS_CampaignGame).
- `IsCampaignGame()` true.
- Default difficulty-buff lists: `DifficultyBuffsPlayer` / `DifficultyBuffsAi`.

## 2. Chapter start (C)
1. **Travel:** the lobby `StartLevel` → `<map>?MapId=<id>[?Checkpoint=<path>]`.
2. **Load-time streaming** (CAMPAIGN_STREAMING §3):
   - `?Checkpoint=` given (a menu pick) → `Activate(Checkpoint.StreamingState)`;
   - otherwise `WorldInfo.InitialStreamingState` → blocking flush.
   - H: InitialStreamingState isn't in authored.db. The lowest-DisplayOrder checkpoint's state is the likely chapter-start state:
     A1 LevelStart, A2 Start_State, A3 Initial, A4 LevelStart_NoCine, A5 Streets, D1 Level_Started, D2 Gorge_Intro, D3A Start,
     D3B IntroCine, D5 Arena. Its menu entry is index 0, which has no path.
3. **`PendingMatch`:**
   - `CanStartMatch` = the host has a character and `AllPlayersHaveControllers`;
   - `TrySpawnPlayer` sets `RespawnFromCheckpoint = true` and returns false.
4. **`InProgress.BeginState`:**
   - GameStatus 3;
   - **`SetupAiSquad`** (the unpicked trio → AI);
   - `AiSquad.BeginGame`;
   - the planner on;
   - **UI event 3** (OnBeginGame).
5. Players spawn at `FindCampaignStart` → `SpawnPointFinder.FindFromActiveCheckpoint()`.
6. **In-map character select:** TnUIControllerCoop starts in `WaitingOnGameStart` → `InGameLobbyUI` = CharacterSelect_GFX (iconic
   trio only), mixer COOP_CHARACTER_SELECT. On the pick → `NotifyPlayerReadyToSpawn` → `AiSquad.RemoveMember`.
7. **Difficulty buffs:**
   - every pawn at spawn: `TnPawn.ApplyDifficultyBuffs`, with level = 3·difficulty + humans − 1;
   - every pawn again on `OnSetDifficulty`, which also runs on every `PostLogin`.

## 3. Streaming during play (C)
- **The only streaming driver is Kismet `SeqAct_SetStreamingState(StreamingState)`:** Activate → **Started** at once; latent
  **Finished** when `IsCurrentState`; **Aborted** if another state replaces it.
  - 20–42 ops per chapter, listed in the export.
  - `SeqAct_FlushLevelStreaming` blocks.
  - The per-level streaming tick works in list order (loads and unloads interleaved per level).
- **Transitions are authored as named states** (A1: LevelStart → Courtyard → TransitionToInterior1 → Interior1 → BalconyDemo →
  AntiAirBalcony → Streets0 → HalfPipe → InsideHalfPipeTerminal → ThumperTransition → Thumper → Highway2 → …).
  - The export's checkpoint list in DisplayOrder gives the order the player passes through them.
  - The "Transition*" states keep both the outgoing and incoming areas loaded.
- `TnCoopGame.NotifyLevelStreamedOut` (≥ 2 players) teleports teammates out of a level that's unloading (`TeleportToPlayer`).

## 4. Checkpoints: when they're set, what's saved, menu indices (C)
1. **Set:** Kismet `SeqAct_SetActiveCheckpoint` → `CheckpointManager.SetActiveCheckpoint(CP)` → native `GenerateCheckpointData`.
   This builds the in-memory payload (version 1, map, package, checkpoint path, then the `TnCheckpointSaveData` body).
2. **The body (per TnPlayerController):**
   - PlayerName, CharacterName;
   - Health (max of segment health and health), AbilityResource;
   - the weapons with clip + reserve (the equipped weapon first; vehicle weapons separately; types 5 and 7 skipped).
   - **Nothing about the world:** world state comes back by reloading levels.
3. **Saved to storage:** `SeqAct_SaveActiveCheckpoint` (HmQueuedDataReaderWriter). Storage saves only matter where
   `bShouldSaveCheckpoints` is set (SOLO).
4. **Reached:** `TnCampaignGame.NotifyReachedCheckpoint(CP)`:
   - `CheckpointIndex = TnDataProvider_Checkpoint.GetCheckpointsForMap(MapId).Find(PathName(CP))`, so **only the checkpoints in the
     menu list have an index**. Others give −1 and don't advance the profile.
   - `PC.ClientReachedCheckpoint(CP, MapId, index)` → profile `ReachedCheckpoint` (the highest index, setting MapId + 16) +
     SaveProfileSettings; fires `SeqEvent_CheckpointActivated`.
   - The `TnCompetitiveScoreEventHitCheckpoint` score event.
   - `GameStatistics.RecordCheckpointReached`.
   - `NotifyCheckpointSet` → `GRI.ActiveCheckpoint` = the checkpoint's Tag.
   - **No HUD text.**
5. **Menu index:** the export gives each checkpoint's `menu_index`. Index 0 = the chapter start: the menu's first entry has no
   path. Nine menu checkpoints in the full chapters, one in A5 / D5.

## 5. Game over / mission failed (C)
- **Triggers** (`TnCampaignGame`):
  - `Killed`: a human killed by anything but `DmgType_Suicided` → `CampaignGameOver(PRI)`;
  - an active AI squad member bleeding out (`TnDamageTypeDownedDrain`) → the same;
  - `NotifyDownedPawn`: no same-team pawn left standing (AI buddies count) → the same.
- **Flow:**
  1. `InProgress.CampaignGameOver` → broadcast `TnCampaignGameOverMessage(WhoDied)` → state **InProgressOver**;
  2. after `_DelayBeforeShowingGameOverScreen` **1 s**, `SendGameOverEvent`:
     - `GRI._CampaignGameOver = true`;
     - every PC `OnCampaignGameOver()` (music stop 0.25 s, DeathSound, GameOverMusic, GameOverMixerPreset, dialog off);
     - AI planners suspended, scheduler off;
     - **UI event 14** → UI state `CampaignGameOver` → CampaignGameOver_GFX "MISSION FAILED".
  3. Damage is 0 (`ReduceDamage`); no spawns.
- **Restart paths** (host only):
  - **Restart From Last Checkpoint:** `RestartPlayerFromLastCheckpoint` → confirm popup → `CheckpointManager.RestartFromActiveCheckpoint`:
    - same map → in place: non-static levels reloaded, `HmGameInfo.CleanupBeforeRestartFromLoadedCheckpoint` (AI destroyed,
      pawns destroyed, music stopped, `ResetLevel`), players restored from the body at `GetStartSpotForPlayer` (+ aux spawns);
    - otherwise `?LoadCheckpoint?DontLoadInitialStreamingState` travel.
    - Clients: `ClientRestartFromCheckpoint(data pieces)` + the ResumeAutobot / ResumeDecepticon loading screen.
    - Then `InProgressOver.StartMatch` → InProgress (no re-setup), `AiSquad.RestartGame`.
  - **Restart Chapter:** `RestartChapter` → confirm → `reloadmap` (with the checkpoint loading screen).
  - **Quit:** TnQuitMessageBox.

## 6. Mission complete → the next chapter (C)
1. Kismet **`TnSeqAct_MissionComplete`** (the outro movie) → `TnCampaignGame.StartCompleteMission()`:
   - every PC `ClientCompleteMission(MapId, GameDifficulty)` → `TnProfileSettings.CompleteMission`:
     - sets the map's own bool;
     - raises the best difficulty (MapId + 32);
     - **unlocks the next map in the faction order** ([96..100] / [101..105]);
     - sets the faction resume slot (55 / 56) to the next available map;
   - then `CheckForLevelCompletionAchievements`, `SaveProfileSettings`;
   - then `EndGame(none, "Complete")`.
2. The movie can't be skipped until end-game handling finishes.
3. `EndCompleteMission`:
   - **TnCoopGame.MissionComplete:** achievement 35 (+36 in CCP). CCP → UI event 9 (EndGameStats), then `ReturnToGameLobby`
     after **10 s**. CP → `ReturnToGameLobby` at once.
   - SOLO: `ReturnToGameLobby`.
4. **The lobby** (`OnEnterLobbyFromMap`) → `SelectCampaignMap` = `GetNextAvailableCampaignMap(prev)` from the host's profile.
   Everyone's character is cleared (`TnGameLobbyGameCampaign.HandleSeamlessTravelPlayer`).

## 7. The contract in one table

| Event (original name) | When | Who acts |
|---|---|---|
| UI 3 OnBeginGame | InProgress begins | Frontend: HUD in |
| UI 5 OnRespawn | a player spawned (co-op `NotifyPlayerAdded`) | Frontend: HUD in / close select |
| `SeqAct_SetStreamingState` Started / Finished / Aborted | Kismet | Gameplay: stream; Kismet waits on Finished |
| `NotifyReachedCheckpoint` → `ClientReachedCheckpoint(CP, MapId, index)` | Kismet SetActiveCheckpoint | Gameplay: save body; Frontend: profile `ReachedCheckpoint` |
| `CampaignGameOver(WhoDied)` → (1 s) UI 14 | a human death / all down / an AI bleed-out | Frontend: MISSION FAILED |
| `RestartFromActiveCheckpoint` / `ClientRestartFromCheckpoint` | the host's choice | both: the loading screen + restore |
| `TnSeqAct_MissionComplete` → `ClientCompleteMission(MapId, Difficulty)` → `EndGame("Complete")` | Kismet | Frontend: profile unlock + save; then the outro |
| UI 9 OnEndGame (CCP) / `ReturnToGameLobby` | after the outro | Frontend: stats (CCP); lobby with the next chapter |
| `OnSetDifficulty` / `ApplyDifficultyBuffs` | a difficulty change, PostLogin, each spawn | Gameplay |
