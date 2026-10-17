# The original's public-match flow: quick match, search, lobby, join-in-progress (RE, 2026-10-18)

For the Frontend's new lobby flow.

Sources (C, bytecode + data):
- `TnPartyLobbyGame` (UI_PartyLobby_m TransContent, `work/script/decomp_ui`);
- `TnPartyBeaconHost` + the team drafters, `TnMultiplayerGame`, `TnRespawnHelperDelayed` (TransGame / TransContent);
- authored defaults `TnOnlinePlaylistManager`, `TnOnlineGameSearchVersus`, `TnDataStore_GameSearch`, OnlineGameSettings.

**LIVE** marks the Xbox Live / Demonware matchmaking transport, which has no equivalent on our relay. Read those parts as rules to
reproduce with our own session list.

## 1. There is no server browser; matchmaking is by playlist (C)
- **Public play = pick a playlist** (quick match). The default quick-match playlist is **1**.
- Playlists (TransPlaylists.ini / TnOnlinePlaylistManager), each with one settings class:

| PlaylistId | Name | Settings class |
|---|---|---|
| 1 | Civil War | TDMPublic |
| 2 | Deathmatch | DMPublic |
| 3 | Code of Power | CTFPublic |
| 4 | Power Struggle | KOTHPublic |
| 5 | Countdown to Extinction | EXTPublic |
| 6 | Escalation | SV (VisibleInMenu false) |
| 7 | Conquest | DOMPublic |
| 100 / 101 | campaign CP / CCP | hidden |

- **There's no list of games for the player to choose from.** The game search runs invisibly; the UI shows only a progress popup.

## 2. Quick-match sequence (`TnPartyLobbyGame.PlayPlaylistInternal`, C)
1. **The party size check** (`CheckCanPlayGameType`): over the limit → "Party Too Large" (ONLINE_PARTY_RE §3; public team modes allow
   5).
2. `StartOnlineGame('Party')`; reservations of anyone not yet joined are cancelled.
3. **Search** (`TnOnlineGameSearchVersus`; LIVE query id 3):
   - filters: `GameMode` = the playlist's mode, `PlaylistId`, and **GamerRegionMin / Max** = your region (an "all regions" profile
     option widens it to 0..7; region codes 0 and 6 are treated as 1);
   - **max 50 results**.
   - Popup: SearchForGameTitle / "Searching..." with B = Cancel.
4. **ReservingSpace:** walk the results in order (null-settings results are skipped), and for each request a party reservation from
   its host. Text: "Reserving space in game `i of `n".
   - **Accepted** → JoiningGame: join that session, tell the party to follow (members travel themselves, the leader 1 s later;
     ONLINE_PARTY_RE §1), and travel to the host's game lobby.
   - **Rejected** (full / unbalanced) → try the next result.
   - **Cancel** → back to Ready ("Cancelling...", then releases any reservation it got).
5. **No result left:**
   - **NAT not strict** → **host it yourself**: create the session with the playlist's settings + region (`HostPlaylist` →
     HostingGame, "Creating lobby..."), then server-travel to the game lobby. You're now the public host, waiting for others to
     find you.
   - **Strict NAT** → you may not host; it **searches again** (the same playlist) in a loop. After **2 failed searches**
     (_StrictNATMessageThreshold) the Ready state shows StrictNATTitle / Message ("Unable to Host" / "To allow hosting of a game,
     switch your NAT from strict to open or moderate.") once.
6. A failed search → NumberOfFailedSearches++, back to Ready.

## 3. Public game lobby (C; see ONLINE_MP_FLOWS / ONLINE_RULES_CHECK_RE for the timings)
- **Map:** MapSelectionMethod Rotate (public): a random different compatible map; **map veto allowed** (AllowMapVeto = Rotate).
- **Autostart:** **LobbyIntermissionTime 45 s** once NumRequiredPlayers (4) humans are in; FinalCountdown under 10 s; aborts if the
  conditions break before the rubicon. Status "Finding `p more players / player".
- **Team building at the lobby** (`TnPartyBeaconHost.GetActiveDrafter`):
  - public lobby → **PublicSnakeDraft**; private → PrivateSnakeDraft; final countdown or game in progress → **JoinInProgress
    drafter**.
  - **Snake draft** (C): sort the parties (public) or players (private) by **total score** if anyone has scored, else by **player
    level** (sum per party), highest first. The first pick goes to a random team, then picks alternate snake-style, with a team
    cap of MaxPlayersPerTeam. The draft is valid only if both teams ≤ the cap.
  - Autobalanced (the default) applies it at the final countdown.

## 4. Join-in-progress (C; answers ONLINE_MP_FLOWS R1)
- **Allowed in public games:** OnlineGameSettings `bAllowJoinInProgress` defaults true, and no versus settings class overrides it.
  The party sessions (PartyPersistent / Presence) set it false. A search can therefore return games already in progress.
- **Reservation rule for in-progress games** (the JoinInProgress drafter):
  - the current team counts come from the reservations;
  - parties are processed largest first, each added to the smaller team (ties → team 0);
  - **accepted only if that team stays ≤ MaxPlayersPerTeam and the teams differ by ≤ 1**;
  - a multi-member party gets that team assigned; **a solo joiner's team is left open** (picked on arrival by PickTeam: the
    smaller team, random on a tie).
  - Rejection → the searcher tries the next result.
- `IsClientAllowedToRejoinSession`: refused when the session doesn't allow join-in-progress and isn't pending / ended.
- **What the joiner gets in the match** (`TnMultiplayerGame.InProgress.TrySpawnPlayer`):
  - `ClientStartSpectating()`, then `RespawnHelper.InitialSpawn`;
  - with the versus TnRespawnHelperWave, `TimeToAllowInstantInitialSpawns = −1` → **always inside the initial-spawn window → spawns
    immediately** (no respawn wait);
  - it joins mid-match on the smaller team with the current score and clock.
- In PendingMatch (before the start) TrySpawnPlayer is false: players wait for the match-start countdown.
- Escalation (TnRespawnHelperRoundEnd, TimeToAllowInstantInitialSpawns 30): instant only in the first 30 s, otherwise it waits for
  the wave's end.

## 5. For our relay (proposal)
- Implement "Quick Match <playlist>":
  1. query our session list for open games with the same mode / playlist (+ region if we have one);
  2. try them in order with a join request carrying the party size; the host applies the §4 balance rule (lobby: snake draft; in
     progress: smaller team, ≤ 1 difference, cap 5);
  3. if none accept, host one and list it.
- Keep the original texts: Searching / Reserving `i of `n / Creating lobby / Cancelling, and Party Too Large.
- The strict-NAT branch maps to "can't host here (relay unreachable)". With our relay every client can host, so the loop isn't
  needed.
