# Join in progress: the original's versus and Escalation late-join rules (RE, 2026-10-23)

For Systems (the JIP work), Gameplay, the Frontend and Integration.

**Labels:** C = script / data (TnMultiplayerGame / TnVersusGame / TnSurvivalGame in `work/script/decomp_tc`, TnGame,
TnPartyBeaconHost and the drafters, the respawn helpers, CDOs); H = stock UE3 / inference; LIVE = Live-layer behaviour.

Earlier detail: ONLINE_PUBLIC_MATCH_RE §4 (the drafter) and §2 (search).

## 1. Allowed, and where from (C)
- `bAllowJoinInProgress` is the OnlineGameSettings default **true**. No game-session class overrides it (versus, SV, CP / CCP);
  the party sessions set it false.
- A public quick-match search therefore returns in-progress games.
- The search filters only on mode, playlist and region. **There's no time-remaining or score cutoff anywhere in script.** A
  game can be joined until it ends.
- Private games are joined by invite / friend (LIVE). The same server rules apply.

## 2. Team pick and the player limit (C)
- **Reservation (the host's beacon):** once the lobby's final countdown has started or a game is in progress, the
  **JoinInProgress drafter** is used:
  - the current team counts come from the reservations;
  - parties are processed largest first, each onto the **smaller team** (tie → team 0);
  - **accepted only if that team stays ≤ MaxPlayersPerTeam (5) and the teams differ by ≤ 1**;
  - a multi-member party gets that team; **a solo joiner's team is left open** and decided on arrival by `PickTeam` (the smaller
    team, random on a tie).
  - Rejected → the searcher tries its next result.
- **Limits:** versus `MaxPlayersAllowed` **10** (5 per team); Escalation (TnSurvivalGame) `MaxPlayers` / `MaxPlayersAllowed` **4**.
  The party-size rule (a public team-mode party may fill only one side: 5) still applies.
- `TnGame.PostLogin`: `GRI.NumPlayersJoined++`; `GRI.MaxPlayers = max(that, GetNumPlayers())` (a high-water mark);
  `SelectPieCharacter`.

## 3. Spawning (C)
- **In a running match** (`TnMultiplayerGame.InProgress.TrySpawnPlayer`): `PC.ClientStartSpectating()`, then
  `RespawnHelper.InitialSpawn(PC)`.
  - **Versus** (TnRespawnHelperWave, `TimeToAllowInstantInitialSpawns = −1`): always inside the initial-spawn window, so the
    **joiner spawns at once** after character select, at a normal player start (the versus spawn rating; H: the same rating as
    respawns).
- **Before the start** (PendingMatch): `TrySpawnPlayer` is false; they wait for the match-start countdown like everyone else.
- **During the end of a match** (MatchOver, the 15 s results): there's no MatchOver `TrySpawnPlayer`, so it falls back to the
  base `TnGame.TrySpawnPlayer`, which **doesn't spawn**. The joiner waits through the results and is carried by
  `ReturnToGameLobby` into the game lobby for the next match. H: whether the beacon still accepted reservations then; the drafter
  is the JIP one.

## 4. What the joiner sees on arrival (C mechanism, H where noted)
- **Score and timer:**
  - the GRI replicates the team scores, `RemainingTime` / `ElapsedTime`, GameStatus and GoalScore;
  - every PRI replicates its score / kills / deaths. The scoreboard is complete at once.
  - CTF: the current round and Rounds.
- **Kill feed: no history.** Kill and announcer messages are `BroadcastLocalizedMessage` events, delivered live only (H: nothing
  stores past messages). The feed starts empty.
- **Objectives and world state:** carried by replication: flags / bomb / hill / node actors, transforming-actor state, markers.
- **The joiner's own PRI starts at zero** (score, kills, streaks). `PRI.StartTime` = the join time.

## 5. XP and stats for a partial match (C)
- **XP is per event.** Every XP event the joiner earns counts (when `CanGainXp`: public games only). There's no minimum time in
  the match.
- **The GameWin / GameLose XP at the end goes to everyone on the winning PRI's team**, late joiners included (the versus
  quirk, VERSUS_END_OF_MATCH_CHECK).
- **Completion stats:** `HandleGameCompletionStats` → `PRI.AddGameCompletion(GRI.ElapsedTime − PRI.StartTime, completion type)`.
  The time played is the joiner's own share.
- MVP: the top score at the end; a late joiner can win it.

## 6. Leavers (C, versus)
- **No bots in the original:** a leaver's slot just **becomes empty** and is open to the next joiner, through matchmaking /
  the JIP drafter under the balance rule above.
- `TnVersusGame.Logout` → after **ForfeitDelay 1.5 s**, `InProgress.CheckForfeit`: if a team has 0 players, that team's score is
  cleared and `EndGame(none, "Forfeit")`, so the other team wins ("Enemy team forfeits").
- H, stock UE3: the leaver's PRI is kept as an inactive PRI for a rejoin within the session (stock `GameInfo.Logout` behaviour).
  Not confirmed in WFC script.

## 7. Escalation (C)
- **Allowed:** the SV settings use the default `bAllowJoinInProgress` true; up to 4 players.
- The PreGame / InWave / BetweenWaves states **extend `TnMultiplayerGame.InProgress`**, so a joiner goes through
  `InProgress.TrySpawnPlayer` → `RespawnHelper.InitialSpawn` with **TnRespawnHelperRoundEnd**:
  - `TimeToAllowInstantInitialSpawns` **30 s**: instant spawn within the first 30 s of the match;
  - **after that the joiner waits for the current wave to end** (`OnRoundEnded` respawns everyone pending; "Waiting for Next
    Wave to Respawn").
- **What they receive:**
  - the wave number and between-waves countdown from the GRI (`CurrentWave`, `NextWaveTime`);
  - the generators' state (power level, CurrentPower, and the visible / hidden factories) by actor replication.
  - **Their own power starts at 0**: a new `TnPlayerReplicationInfoSurvival` (`_CurrentPower` 0; `Reset` also zeroes it). There's
    no catch-up grant.
  - The horde's buffs / wave scaling are per spawned enemy; nothing applies per player.
- **No XP in Escalation** (`CanGainXp` false), so nothing to prorate. Leaderboard stats come from the joiner's own events.
- **A joiner doesn't change the horde:** wave budgets are fixed per wave, independent of player count (MP_ESCALATION §9.4).

## 8. For the rebuild (Systems / Gameplay / Frontend)
1. **Team:** the smaller team (tie → random for solos; team 0 for parties in the reservation step); refuse if the result breaks
   ≤ 5 per team or a difference ≤ 1.
2. **Spawn:** versus at once after character select; Escalation at once only within 30 s of the start, else at the next wave
   end; MatchOver → wait for the lobby.
3. **State to send:** scores, clock, round, objective and actor states, PRIs. **No kill-feed history.** Escalation adds the wave,
   the countdown and the generator states, with the joiner's power at 0.
4. **XP / stats:** per event; the end-of-match win / lose XP to everyone on the winning team; time played = from the join.
5. **Leave:** the slot empties (our bots are a PC adaptation: D16); a team emptied for 1.5 s forfeits.
