# Campaign frontend plan (Frontend, 2026-10-23)

Source: RE CAMPAIGN_FRONTEND_FLOW_RE.md (4d25a41), with CAMPAIGN_DIFFICULTY_RE, CAMPAIGN_HUD_OBJECTIVES_RE, CAMPAIGN_STREAMING_RE.
The original movies already exist and run in the rebuild (FrontEnd_GFX's Solo / Cooperative menus, GameLobby_GFX
mc_menuCampaign, CampaignSelect_GFX, DifficultySelect_GFX, CharacterSelect_GFX, CampaignGameOver_GFX). The work is the bridge
calls and data stores they read, and the flow states behind them. Versus stays byte-identical (campaign() gates).

## Steps

| Step | What | Bridge / data (the movies' own calls) | Needs |
|---|---|---|---|
| C1 | Main menu entry: Solo -> Resume Autobot / Resume Decepticon / Mission Select; Cooperative -> the co-op party lobby | `Online.OpenCampaignGameLobby` (SOLO lobby, UI_CampaignLobby_m); `Game.GetCheckpointFileStatus` (the Resume rows); `Game.LoadSavedCheckpoint(faction)`; `Online.OpenPartyLobby('GTS_CampaignGame')` | profile checkpoint records |
| C2 | Campaign lobby: mc_menuCampaign; the host goes straight into CampaignSelect; Start always allowed (TnGameLobbyGameCampaign); SOLO Back from CampaignSelect quits to the main menu | `<CurrentGame:GameTeamStatus>` GTS_CampaignGame; `CanHostRequestGameStart` true; `Online.BeginLobbyExitCountdown`; `$UIText.Lobby.CampaignCountdownLabel` | - |
| C3 | CampaignSelect: faction -> chapter (unlock + best difficulty) -> checkpoint (locked above the highest reached; thumb 0 = start) | `<TnMenuItems:Maps>` Faction / CompatibleGameTypes; `<OnlinePlayerData:ProfileData.<A1..D5>Available / Difficulty / Checkpoint>`; `<TnMenuItems:Checkpoints>` (FriendlyName, ImagePath, CheckpointPath); `Online.SetSelectedMapID`, `Online.SetSelectedCheckpoint`; launch URL `?Checkpoint=` | TransLevels.ini checkpoint table (catalog) |
| C4 | Difficulty: EASY / MEDIUM / HARD, host only, saved to the profile (setting 12) | `Game.GetGameDifficulty`, `Game.SetGameDifficulty`, `Console.SaveProfileSettings` | - |
| C5 | Character select: the chapter's iconic trio (CampaignCharacters); picks outside it cleared on a chapter change | `Customize.GetPlayableCharacters` in campaign = the trio | - |
| C6 | In the map: loading screen (Campaign, "Loading <map>", 3 tips; checkpoint resume = ResumeAutobot / ResumeDecepticon title); in-map character select for anyone without one; game over (UI event 14 -> CampaignGameOver_GFX: MISSION FAILED, Restart From Last Checkpoint / Restart Chapter (host) / Quit); mission complete (CCP: EndGameStats then the lobby after 10 s; CP / SOLO: back to the lobby; next chapter preselected) | `Game.RestartPlayerFromLastCheckpoint`, `Game.RestartChapter`, UI event 14 handling in UIController | Gameplay's campaign runtime: map load, game over / mission complete events, checkpoint restart |

C1-C5 are frontend-only and FakeSession / headless testable now. C6 waits for Gameplay's campaign runtime events (names to agree,
as for Escalation: e.g. HudGameState / MatchEvent CampaignGameOver, MissionComplete, a checkpoint-restart request).
Co-op (CP / CCP) online: the session's lobby already carries GTS_CampaignGame; join in progress follows Systems' late join.

## Order
C1 -> C4 -> C3 -> C2 -> C5 (each with tests), then C6 with Gameplay.
