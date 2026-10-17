# Online rules check: the original's versus rules vs our MatchLaunch / lobby defaults (RE, 2026-10-18)

This is a rules review only; no networking code is involved.

Sources:
- **Original:**
  - authored.db `TransGame.Default__TnOnlineGameSettings<tag>[Public|Private]`;
  - Coalesced ini `Xe-TransGame.ini`;
  - the TransGame / TransContent bytecode (`work/script/decomp`, `decomp_tc/MP_ORB_Debris_BASE_m.TransContent.txt`);
  - INT localization.
- **Ours (Rebuild df4d62aa):**
  - `data/frontend/game_settings.json` (exported from the same authored.db classes);
  - `src/frontend/Catalog.cpp`, `GameFlow.cpp` (buildMatchUrl, lobby);
  - `src/game/World.cpp` (MatchLaunch::fromURL);
  - `src/game/Match.{h,cpp}` (MatchSettings::forMode);
  - `docs/handoff/ONLINE_MP_FLOWS.md`.

Labels: C = confirmed from data / script; H = inference.

**Result: one real divergence (CTF Rounds), one minor text divergence (the singular lobby status) and two notes. Everything else matches.**

## 1. Per-mode values (original, C)

The "default" is the LocalizedSettings ValueIndex / Properties value of the base class. Public and Private classes keep the same
defaults, except Map Selection (Public = Rotate, Private = Host's Choice).

| Mode (tag, class) | Score setting (choices, **default**) | Time limit (choices, **default**) | Team balancing default | NumRequiredPlayers | Slots |
|---|---|---|---|---|---|
| Deathmatch (DM, TnFreeForAllGame) | PointsToWin 10 / **20** / 30 kills | 600 / **900** / 1200 s | none (FFA) | 4 | 10 |
| Team Deathmatch (TDM, TnVersusGame) | PointsToWin 20 / 30 / **40** / 50 team kills | 600 / **900** / 1200 | **Autobalanced** | 4 | 10 |
| Code of Power (CTF) | **Rounds** **2** / 4 / 6 (no PointsToWin; GoalScore 0 = no score limit) | per round 240 / **300** / 360 | Autobalanced | 4 | 10 |
| Power Struggle (KOTH) | PointsToWin 150 / 200 / 250 / 300 / **400** / 500 | 600 / **900** / 1200 | Autobalanced | 4 | 10 |
| Conquest (DOM) | PointsToWin 150 … **400** … 500 | TimeLimit is a property: 600 / **900** / 1200 | Autobalanced | 4 | 10 |
| Countdown to Extinction (EXT) | PointsToWin 1 / **3** / 5 plants | 600 / **900** / 1200 | Autobalanced | 4 | 10 |
| Escalation (SV, TnSurvivalGame) | none | none | (one team) | 2 | 4 |

Score per event (ini): ScoreFlags (CTF capture) 10, ScoreBombingRun (EXT) 10, ScoreKingOfTheHill 1 per tick, ScoreObjectives 0;
multi-kill announcements at 3 / 5 / 7.

Common rules (C):
- **Match start:** `TnMultiplayerGame.MatchAutoStartCountdown` **10 s** (TnSurvivalGame **15 s**). HUD label "Match starts in" (CTF
  rounds: "Round starts in"; campaign: "Mission starts in"; UIText).
- **Match end:** `MatchOverCountdown` **15 s**, then return to the game lobby. `ForfeitDelay` **1.5 s** (versus, when a team empties:
  "Enemy team forfeits").
- **Time-out:** GRI RemainingTime ≤ 0 → `EndGame(none, "")`.
  - Versus: `PickWinningTeam` = higher team score (or the only non-empty team), otherwise **"Tie game"**.
  - **There is no overtime or sudden death.** `GameInfo.EndGame` sets bOverTime only if a rules modifier vetoes `CheckEndGame`.
    No WFC rule vetoes it: the only override, ScoreKingOfTheHill, deactivates the hill zones and then passes the call on.
- **Score limit:** `CheckScore` → `ReachedGoalScore` (GoalScore > 0 and score ≥ GoalScore) → `EndGame(scorer, "Score")`.
- **CTF:** RoundsBase. TimeBetweenRounds **5 s**, the round timer replaces the match clock, the match ends after GRI.Rounds rounds
  (`CheckEndCondition`), plus the mercy rule.
- **Respawn (versus and FFA):**
  - `TnRespawnHelperWave`: per player, **WaveRespawnTime 5 s** after death (0 with a kill-streak instant respawn);
  - `TimeToAllowInstantInitialSpawns −1` = the first spawn is always instant;
  - **PlayerSpectating after MinRespawnDelay 3.0 s**;
  - UI texts "Waiting to Respawn" / "Waiting to Spawn".
  - Escalation uses `TnRespawnHelperRoundEnd` (respawn at wave end; instant initial spawns for the first 30 s; "Waiting for Next
    Wave to Respawn").
  - `TnPlayerController.PlayerRestartDelay 6.0` (ini) is not referenced by any script, so it's dead config (H).
- **Lobby:**
  - **Private:** the host starts → `BeginShortCountdown` = LowCountdownTickThreshold **10 s** → FinalCountdown.
  - **Public:** once NumRequiredPlayers are in, LobbyIntermissionTime **45 s**; FinalCountdown when the countdown is below 10;
    abort if the conditions fail before the rubicon.
  - Low-tick sound `BL_HUD_INTERFACE.CTF_ROUND_TIMER_01` under 10 s.
  - At FinalCountdown, Autobalanced → TeamHandler.AssignTeams; otherwise PickTeam (the smaller team, random on a tie).
- **Lobby status texts** (TransGame.int TnGameReplicationInfoGameLobby):
  - "Finding `p more players" / "Finding `p more player";
  - "Waiting for all players to choose a character";
  - "Waiting for host to start game";
  - "Waiting for balanced teams".

## 2. Ours vs the original

| Item | Original | Ours | Verdict |
|---|---|---|---|
| Score defaults DM 20, TDM 40, KOTH / DOM 400, EXT 3 | as above | game_settings.json (same authored classes) → Catalog `pointsToWin`; `forMode` the same | match |
| Score choices | as above | from the authored PropertyMappings | match |
| Time limit 900 (index 1); CTF 300 per round | as above | `timeLimitDefaultIndex` from the authored mapping; URL `TimeLimit=900.00`; forMode 900 / CTF 300 | match |
| **CTF Rounds 4 / 6** | URL `?Rounds=N` → `TnMultiplayerGame.InitGame` reads **"Rounds"** → GRI.Rounds → CheckEndCondition | GameFlow builds `?Rounds=N` (scoreOptionName "Rounds", Catalog.cpp:47 / 338). **`MatchLaunch::fromURL` (World.cpp:2689) takes the round count only from `PointsToWin`**, which a CTF URL never carries. | **DIVERGES:** a host choosing 4 or 6 still plays **2** rounds, while `<CurrentGame:Rounds>` (DataStores.cpp:143, from GameFlow `match_.rounds`) shows 4 / 6. Fix: read `Rounds` (keep PointsToWin as a fallback if wanted). |
| CTF GoalScore | 0 (no PointsToWin; GameInfo has no default) → ReachedGoalScore never true | 1000000 | same behaviour. Only GRI.GoalScore differs (0 in the original), if the HUD or scoreboard ever shows it. |
| Overtime / sudden death | none; time-out → winner by score or "Tie game" | Match.cpp: endGame(−1, "") on time; team tie → −1 | match |
| FFA time-out winner | `EndGame(none, "")`; `TnFreeForAllGame.CheckEndGame` sets GRI.Winner = none; the FFA game-over message is "" | top individual score, tie = draw (Match.cpp:447–467, from an earlier RE note) | **note (H):** the script names no winner on an FFA time-out, so any "winner" comes from the end-of-match stats ranking. Fine if only the results screen uses it; don't drive GameWin XP from it (DM gives no win / lose XP anyway). |
| Respawn 5 s per player, instant first spawn, spectate after 3 s | as above | Match.h `waveRespawnTime 5`, `kMinRespawnDelay 3`; flows doc "initial spawn immediate" | match |
| Match-start countdown 10 s; match over 15 s; forfeit 1.5 s | as above | Match.h 10 / 15 / 1.5; online: the countdown runs in sim ticks (ONLINE_MP_FLOWS) | match. Escalation needs **15** when it's ported (forMode has no SV entry yet). |
| Team balancing default Autobalanced (Public and Private) | as above | Catalog field defaultIndex from the same data; the online gate shows "Waiting for balanced teams" only for Manual | match |
| Private lobby short countdown 10 s; public 45 s, final at 10 | as above | `kShortCountdown 10`, `kLobbyIntermissionTime 45` | match (public autostart is phase 2) |
| Lobby status texts | the five texts above | DataStores.cpp:129 maps LobbyStatus 1 / 3 / 4 / 6 to the same INT keys | **minor divergence:** the original picks `WaitingForOnePlayerStatus` ("Finding `p more player") when exactly one is missing; DataStores.cpp:129 always uses the plural `WaitingForPlayersStatus` (the singular exists only in the new load-gate text, GameFlow_Online.cpp:454). Matters for public lobbies (phase 2). |
| "Waiting for players (`n)" on the load screen | not in the original (no load gate) | new text (ONLINE_MP_FLOWS §3.6) | expected addition, labelled new |
| NumRequiredPlayers / slots | 4 / 10 (SV 2 / 4) | Catalog: from the data, connections default 10 / 4 / 3 | match |

## 3. For Frontend / Gameplay

1. **Gameplay (World.cpp:2689):** parse `Rounds` for CTF. A URL with `?Rounds=4` must give 4 rounds. Add a test like the CTFTEST
   launch with `?Rounds=4`.
2. **Optional:** CTF GoalScore 0 instead of 1000000, if anything displays GRI.GoalScore.
3. **Frontend (DataStores.cpp:129):** use `WaitingForOnePlayerStatus` when exactly one player is missing.
4. When Escalation is ported: MatchAutoStartCountdown 15, NumRequiredPlayers 2, 4 slots, TnRespawnHelperRoundEnd (see
   MP_ESCALATION_RE).
