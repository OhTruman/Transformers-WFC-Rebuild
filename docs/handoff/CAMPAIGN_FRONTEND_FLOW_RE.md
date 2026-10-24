# Campaign frontend flow: mode, lobby, chapter / checkpoint / difficulty select, character select, loading, game over, mission complete, co-op join (RE, 2026-10-23)

For Frontend's campaign menu.

**Labels:** C = TransGame / TransContent bytecode (`work/script/decomp`, `work/script/decomp_tc/A2_KON_BASE_m.TransContent.txt`),
the Scaleform AS2 disassembly (`work/q/gfx/*.as.txt`), authored CDOs, or `TransLevels.ini` (ExtractedAssets
config); H = inference.

**Related:**
- CAMPAIGN_DIFFICULTY_RE (difficulty values, profile records);
- CAMPAIGN_HUD_OBJECTIVES_RE (objectives, checkpoint toast, mission-complete presentation);
- CAMPAIGN_STREAMING_RE / CHECKPOINT_* (restart mechanics);
- ONLINE_PARTY_RE (party / invites).

## 1. Modes (C, the TnOnlineGameSettings CDOs)

| Mode tag | Game class | Lobby class | Required players | Notes |
|---|---|---|---|---|
| **SOLO** (TnOnlineGameSettingsCPSolo) | TnCampaignGameSolo | TnGameLobbyGameSolo | 1 | no invites, no presence or join via presence; `bShouldSaveCheckpoints` true |
| **CP** "Campaign" / "Cooperative Adventure" | TnCoopGame | TnGameLobbyGameCoop | 2 | rules ScoreKills + TrackKills; stats TnStatsWriteCP |
| **CCP** "Campaign" / "Competitive Cooperative Adventure" | TnCoopGame | TnGameLobbyGameCoop | 2 | adds TnGameRules_CompetitiveScoring; stats TnStatsWriteCCP; EndGameStats after a mission (§6) |

- **All three:**
  - `OnlyAllowIconicCharacters` (`_bIconicMode`);
  - lobby map `UI_CampaignLobby_m`;
  - TeamType `GTS_CampaignGame` (CP / CCP);
  - `AllowPartyToFillPublicGame`.
- **Co-op specifics:**
  - TnCoopGame `bPauseable` = false: no pause in co-op, except after a campaign game over (`AllowPausing`);
  - **join in progress is allowed**: the engine default `bAllowJoinInProgress` true isn't overridden by CP / CCP.
- **Loading-screen tips** (`EngageText`):
  - CP: "Collect Energon shards from fallen enemies…", "The only way to kill a Brute is to destroy its backpack.", …;
  - CCP: the scoring tips.

## 2. Campaign lobby (GameLobby_GFX + TnGameLobbyGame / TnGameReplicationInfoGameLobby, C)
- **Menu clip:** `mc_menuCampaign` (platform variants 360 / PS3 / PC) when GameTeamStatus is `GTS_CampaignGame`.
- **Items:**

  | Item | Who | Action |
  |---|---|---|
  | **Start Game** | host | `Online.BeginLobbyExitCountdown` → `HostRequestsGameStart`. Enabled while `CanHostRequestGameStart` and no countdown is running. TnGameLobbyGameCampaign makes `CanHostRequestGameStart` **always true**, so the host can start alone even in public CP / CCP |
  | **Select Mission** | host | CampaignSelect, skipping character select |
  | **Select Difficulty** | host | DifficultySelect |
  | **Select Character** | | CharacterSelect |
  | Leaderboards | | |
  | Invite Friends | | |

- **On entering the lobby the host is sent straight into CampaignSelect** (`displayCampaignSelect(false)`).
  - In SOLO, CampaignSelect gets `backToFrontEnd`: backing out of it quits to the main menu (`Game.QuitToMainMenu`).
- **Countdown label:** `$UIText.Lobby.CampaignCountdownLabel`.
- **Host start:**
  1. `BeginShortCountdown` (LowCountdownTickThreshold) → FinalCountdown → `StartLevel`;
  2. URL = map file + `?MapId=<id>` + `?Checkpoint=<path>` if a checkpoint is selected (`ModifyURL`) + the settings URL;
  3. **ServerTravel**.
- **Public auto-start:** with ≥ NumRequiredPlayers (2) and balanced teams → `CountingDown` from LobbyIntermissionTime **45 s**.
- **Lobby status strings:**
  - "Finding `p more player(s)";
  - "Waiting for all players to choose a character";
  - "Waiting for host to start game";
  - "Waiting for balanced teams".
- **Map / checkpoint replication:**
  - `HostRequestMapID` / `HostRequestCheckpoint` only apply when `MapSelectionMethod == 1` (host picks);
  - SetMapId also prestreams the map (`UpdateMapPrestreaming`) and **clears any character not in the chapter's trio**
    (`DeselectInvalidCharacters`);
  - a client missing the map's content gets `ShowContentMissingMessageBox`.
- **Returning to the lobby from a map** (`OnEnterLobbyFromMap`): in a campaign game it always chooses the **next available
  chapter** (`TnProfileSettings.GetNextAvailableCampaignMap`, using the host's local profile).
  `TnGameLobbyGameCampaign.HandleSeamlessTravelPlayer` clears every player's character, so everyone re-picks.

## 3. CampaignSelect (CampaignSelect_GFX, C)
Three stages: **faction → chapter → checkpoint**. The frame resizes per stage (Faction 330 px, Map 528 px, 0.5 s ease-out:
PRESENTATION_AUDIT_3).

1. **Faction** (`mc_factionMenu`: Autobot / Decepticon logos; title `$UIText.Shared.ChooseFaction`).
   - The preselected faction is `_global.SelectedFaction`, else the faction of the current `SelectedMapID`, else Autobot.
   - If a map is already selected, the faction stage is skipped.
   - **A:** `SelectedFaction = focus.Faction`; the others fade out (0.5 s) → the map stage.
2. **Chapter** (`mc_missionMenu`, buttons `mc_missionChapterButton` with "CHAPTER" + roman numeral, title and description).
   - **List source:** `<TnMenuItems:Maps>` rows whose `CompatibleGameTypes` contains CP / CCP, whose Faction isn't −1 (the test maps
     have no faction, so they're hidden). Each button carries `ChapterFaction`. H: the filter to the picked faction happens
     through that value; the compare isn't in the traced block.
   - **Per row:** MapId 96–105 → profile key A1…A5 / D1…D5:
     - `<OnlinePlayerData:ProfileData.<key>Available>`: unlocked;
     - `…<key>Difficulty>`: the best difficulty completed, shown on the button.
   - The **button is enabled only if Available and the map provider is enabled**.
   - **New-profile defaults** (C, TnProfileSettings DefaultSettings):
     - only **A1 (96) and D1 (101) Available**; A2–A5 and D2–D5 locked;
     - every `<key>Checkpoint` = 0 (start only) and every `<key>Difficulty` = −1.
     - CompleteMission unlocks the next map in the faction order: [96..100] / [101..105].
   - **Focus:** the current SelectedMapID if enabled, else the first enabled chapter. Left / right navigation.
   - **A** → `Online.SetSelectedMapID(id)`. If the chapter has checkpoint thumbs → the checkpoint stage, else `mapSelected()`.
   - **X** = Difficulty (opens DifficultySelect).
   - **B:** if no map was ever selected, it selects the focused one and its first checkpoint, then goes back.
   - Debug footer only (not retail): UnlockAllLevels / ClearCompletedLevels.
3. **Checkpoint** (`mc_checkpointMenu`, title `$UIText.CampaignSelect.CheckpointSelect`).
   - **Thumbs** are the `<TnMenuItems:Checkpoints>` rows with this MapId, in data order. Each has FriendlyName, ImagePath
     (`UI_CheckpointThumbnails_p.*`, an external texture) and CheckpointPath.
   - **Locking:** thumb *n* is enabled iff the profile's `…<key>Checkpoint` (the highest checkpoint reached, setting
     `MapId + 16`) ≥ *n*. The others show `lock_mc`.
   - **Thumb layout:** width = (800 − spacing·(N − 1)) / N, capped at SelectedWidth − 30.
   - **Focus:** the current SelectedCheckpoint, else thumb 0.
   - **A** → `SetSelectedCheckpoint(path)` → `mapSelected()`. **B** → back to the chapter stage.
   - **Thumb 0 of every chapter has an empty CheckpointPath** (the chapter start: the ini lines are commented out), so no
     `?Checkpoint=` is sent.
4. **`mapSelected(SkipCharacterSelect)`:** unless skipped (or when opened from "Select Mission"), it opens **CharacterSelect**
   (`_global.DisplayCharacterSelect`, back-target = the checkpoint menu). Otherwise it returns to the lobby menu.

**Chapters and checkpoints** (C, `TransLevels.ini` TnDataProvider_MapInfo / _Checkpoint):

| MapId | Map | Faction | CampaignCharacters (the iconic trio) | Checkpoint thumbs |
|---|---|---|---|---|
| 96 | A1_IAC_Base_m | 0 Autobot | Optimus, Bumblebee, Ratchet | 10 |
| 97 | A2_KON_Base_m | 0 | Optimus, Sideswipe, Bumblebee | 10 |
| 98 | A3_COR_Base_m | 0 | Optimus Prime, Ironhide, Warpath | 10 |
| 99 | A4_ORB_Base_m | 0 | AirRaid, Jetfire, SilverBolt | 10 |
| 100 | A5_TRY_Base_m | 0 | Bumblebee, Optimus Prime, Ironhide | 2 |
| 101 | D1_ORB_Base_m | 1 Decepticon | Brawl, Barricade, Megatron | 10 |
| 102 | D2_COR_Base_m | 1 | Starscream, Skywarp, Thundercracker | 10 |
| 103 | D3A_IAC_Base_m | 1 | Megatron, Soundwave, Breakdown | 10 |
| 104 | D3B_IAC_Base_m | 1 | Megatron, Soundwave, Breakdown | 10 |
| 105 | D5_OME_BASE_m | 1 | Megatron, Soundwave, Breakdown | 2 |

- All are SOLO / CP / CCP. `CoopCombat_m` (539) and the TEST maps have no faction and are hidden.
- **The menu's chapter numbering** (from the Extras strings):
  - Decepticon I–V (D1 Dark Energon, D2 Fuel of War, D3A Iacon Destroyed, D3B Death of Hope, D5);
  - Autobot VI–X (A1 Defend Iacon, A2 Kaon Prison Break, A3 To the Core, A4 Aerial Assault, A5).
  - Profile key D4 = D3B (MapId 104).

## 4. Difficulty select (DifficultySelect_GFX, C)
- **Reads:** `Game.GetGameDifficulty()` (returns 0 for non-hosts).
- **On accept:** `Game.SetGameDifficulty(d)` (**host only**; clamps and writes profile setting 12), then
  `Console.SaveProfileSettings()`.
- EASY / MEDIUM / HARD with the three descriptions: CAMPAIGN_DIFFICULTY_RE §1.
- H: the travel URL carries no difficulty option from the lobby. The map's HmGameInfo reads the host's profile in PostBeginPlay.

## 5. Character select (C)
- **Iconic only:** the selectable characters are `GetPlayableIconicCharacterNames` = the chapter's `CampaignCharacters`
  (all iconics if the list is empty). Picks outside the trio are cleared on a chapter change.
- `AllPlayersHaveCharacter` ignores bots.
- **Where it runs:**
  - in the lobby (Select Character, or after CampaignSelect);
  - **in the map**: TnUIControllerCoop starts in `WaitingOnGameStart` and opens `InGameLobbyUI` = **CharacterSelect_GFX**
    with mixer `COOP_CHARACTER_SELECT` (subtitles off).
  - Players whose character was cleared by the lobby, or who join mid-game, pick here.
- **A character the humans don't take is played by the AI squad**:
  - `NotifyPlayerReadyToSpawn` → `AiSquad.RemoveMember(name)`;
  - `NotifyPlayerRemoved` (leave) → `AddMember(name)`, so the AI takes the character back.

## 6. In-game flow (C)
- **Loading screens** (`TnMoviePlayer.StartLoadingMovie`, from the pending URL):
  - **chapter load:** title = the mode's FriendlyName ("Campaign"); text = `$UIText.LoadScreen.LoadingMap` with `m = the map's
    friendly name; plus **3 random EngageText tips** (`_global.SetLevelText`, `_global.AddEngageText`);
  - **lobby loads** use the generic screen;
  - **checkpoint restart / restart chapter:** `StartLoadFromCheckpointLoadingMovie(team)` → title
    `$UIText.LoadScreen.ResumeAutobot` / `ResumeDecepticon` + CP tips.
- **Spawning and joining** (TnCoopGame):
  - A player with no selected character at SetupPC gets `RespawnFromCheckpoint = false`. They spawn through
    **TeammateSpawner** near a living teammate (or the special Kismet `TnSeqAct_SpecialSpawnLogic` for that character).
  - If no spot is found → `StartClientPlayerWaiting`.
  - After a successful spawn → UI event 5 (OnRespawn: HUD in).
  - **Restart is only allowed while another human is alive with a pawn** (`PlayerCanRestart`).
  - A streamed-out level with ≥ 2 players → teammates are teleported (`TeleportToPlayer`, teleport effect).
- **Game over** (all humans downed / dead: CONTENT_DELTAS §3.1):
  - UI event 14 → **CampaignGameOver_GFX**: title "MISSION FAILED"; subtitles and force feedback off;
    `PC.OnCampaignGameOver` audio (music stop 0.25 s, death sound, game-over music and mixer).
  - **Buttons:**

    | Button | Action |
    |---|---|
    | **Restart From Last Checkpoint** | popup "Load Checkpoint" / "All unsaved progress will be lost, proceed?" when `ShouldShowProgressLossMessageFor` → `RestartFromActiveCheckpoint` with the checkpoint loading screen |
    | **Restart Chapter** | popup "Restart Chapter" / "All unsaved progress will be lost. Do you want to proceed?" → clears the checkpoint character override → **`reloadmap`** |
    | **Quit Game** | TnQuitMessageBox |

  - **Non-hosts:** both restarts are disabled and focus is on Quit. The restart bindings return early when the caller isn't the
    host.
  - A player who finishes character select after the game is already over (no pawn) goes straight to the game-over UI.
- **Mission complete** (`TnSeqAct_MissionComplete` → profile `CompleteMission`, KISMET_HANDLERS / HUD_OBJECTIVES §3) →
  `TnCoopGame.MissionComplete`:
  - achievement **35** for every player (+ **36** in CCP);
  - **CCP:** UI event 9 → **EndGameStats** (scores), then `ReturnToGameLobby` after **MissionCompleteCountdown 10 s**;
  - **CP:** `ReturnToGameLobby` immediately.
  - The lobby then preselects the next chapter (§2).
- **Pause:** co-op isn't pausable (`bPauseable` false, unless the game is over). SOLO pauses normally (PauseMenu_GFX).

## 7. Checklist for the menu
1. Mode pick SOLO / CP / CCP → the campaign lobby (`UI_CampaignLobby_m`); the host goes straight into CampaignSelect.
2. Faction → chapter (unlock and best-difficulty from the profile) → checkpoint (locked above the highest reached; thumb 0 =
   start) → character select (the chapter's trio).
3. Host-only: difficulty, mission, Start (short countdown). Public CP / CCP auto-start at ≥ 2 players after 45 s.
4. Travel URL `?MapId=…[?Checkpoint=…]`; loading screen with the map name + 3 tips.
5. In-map character select for anyone without a character; AI fills untaken trio members and takes over leavers.
6. Game over: MISSION FAILED with Restart Checkpoint / Restart Chapter (host) / Quit.
7. Mission complete: CCP stats (10 s) or straight back to the lobby; next chapter preselected.

**H / not traced:**
- the exact LowCountdownTickThreshold;
- how the in-map character select closes into the spawn (OnCharacterConfirmed → GameStartUI is Survival's; Coop uses the base
  WaitingOnGameStart);
- SOLO's checkpoint saving vs co-op's: only Solo sets `bShouldSaveCheckpoints`. H: co-op records progress through the
  profile's ReachedCheckpoint, but doesn't write checkpoint saves.

## 8. Addendum 2026-10-24: the lobby pick carries into the map (C; QC #38 / 40)
- **The campaign map's GRI:** TnCampaignGame / Solo / Coop use **TnGameReplicationInfoGameBase** (the TnCampaignGameBase
  GameReplicationInfoClass).
  - It inherits `RetainSelectedCharacterOnTransition = True` from TnGameReplicationInfo.
  - Only TnGameReplicationInfoMultiplayer sets it False; the Survival GRI sets it back to True.
- **Lobby → map is seamless:** TnGame `bUseSeamlessTravel` True, and `ServerTravelToMap(URL, true, …)`. So
  `TnPlayerReplicationInfo.CopyPropertiesTo` copies `_SelectedCharacter`, the string, the ReplicatedCharacterData and the iconic
  name, provided `ValidateCharacterSelection` passes (the name is in the chapter's trio and not held by another PRI).
- **Characters are cleared only when entering the lobby:** `TnGameLobbyGameCampaign.HandleSeamlessTravelPlayer` →
  `PRI.ClearCharacter`. `TnCampaignGame.HandleSeamlessTravelPlayer` only re-sets the team.
- **So:**
  - A Solo / CP / CCP lobby pick spawns as that character, with **no in-map select**. In-map select is only for players with no
    valid pick (cleared, invalid, or mid-game joiners).
  - Returning to the lobby after a chapter clears it, so everyone re-picks.
