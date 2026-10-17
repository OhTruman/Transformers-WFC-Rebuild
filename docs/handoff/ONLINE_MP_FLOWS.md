# Online multiplayer: player flows and the original's rules (Frontend, 2026-10-17; design only, no code)

Assignment from Integration (rebuild-07): the player-facing side of online play, built on the existing offline MP
frontend (boot menu -> Multiplayer -> party lobby -> game lobby -> match). It also covers the original's online rules,
taken from existing material only (no new decompilation). Companions:
- CROSSPLAY_NETCODE_DESIGN.md: lockstep on inputs, the WebRTC transport, join codes and the session flow (sections 5-7).
- CROSSPLAY_PROFILE_STATS.md: identity, display names, stats.
- CROSSPLAY_PLATFORM_FEASIBILITY.md: platform order PC-PC first, then PC-browser.
- Systems' ONLINE_MP_ARCHITECTURE.md (in progress).

**The bar** (Integration, 2026-10-17): make it work with what we have now. The original's rules are the starting point
and are modified wherever they do not fit lockstep and this frontend. Each flow in section 3 ends with
**Simplified vs the original**, saying what changed and why.

This document decides nothing about the wire. It says what the player sees and does, which original screen each step
uses, and what the frontend state machine needs.

Labels:
- CONFIRMED: read from the original's script, config or text.
- HIGH: stock UE3 behaviour, or strongly implied.
- PC ADAPTATION / PC EXTENSION: the project's rules.
- UNKNOWN: not established; such points are listed as open questions instead of guessed.

Sources:
- [NET] RE-Workspace notes/NETWORK_ARCHITECTURE_2026-10-01.md
- [MP] RE-Workspace notes/MP_PROGRESSION_SCORING_AI_2026-10-06.md (addenda 3 and 8)
- [INI] ExtractedAssets config Xe-TransGame.ini / Xe-TransEngine.ini
- [INT] ExtractedAssets UIText.int / TransGame.int / Engine.int (INT)
- [AS] RE-Workspace work/q/gfx/*.as.txt (the shipped movies' ActionScript listings)
- [CDO] AssetTools work/frontend_online_cdo_values.md (assettools/checkpoint e2a345e): effective class defaults from
  authored.db, 384 rows from 98 CDOs, with object paths
- [CODE] this repository (src/frontend)

---

## 1. The original's online rules (what is known, with its source)

### 1.1 Topology and sessions

| rule | value | label / source |
|---|---|---|
| Topology | player-hosted listen server; the party leader hosts the party lobby, the match host hosts the game lobby and the match | CONFIRMED [NET 1] |
| Host migration | none: no migration code in the online or game script; when the host leaves, the session ends | HIGH [NET 1] |
| Named sessions | 'Party' (party lobby) and 'Game' (game lobby + match) | CONFIRMED [NET 2] |
| Lobby -> match | seamless server travel into the map; after the match, seamless travel back to the game lobby (GameLobbyURL?MapId=<id>) | CONFIRMED [NET 2] |
| Join path | public: playlist search 'VERSUS' -> reservation request for the whole party -> join, or host the playlist when no result accepts; friends: Friends List -> Join Game (Online.JoinFriendGame(NickName)) | CONFIRMED [NET 2], [AS OnlineFriendsMenu_GFX] |
| Reservation | party beacon: request / response (accepted = 4), "tell clients to join", host ready / cancelled, heartbeat, return-to-session request (packet 9) | CONFIRMED [NET 4] |
| Party beacon defaults (stock socket transport) | HeartbeatTimeout 60 s, ReservationRequestTimeoutRange 4-10 s, ConnectionBacklog 9, port 8675 | CONFIRMED [NET 4] |
| LAN / system link | LiveSystemLinkPort 14000, LanAnnouncePort 14001, LanQueryTimeout 2 s, UdpBeacon 8777 / 9777 | CONFIRMED config, HIGH behaviour [NET 6] |
| Connection timeouts | ConnectionTimeout 8 s, InitialConnectTimeout 50 s, KeepAliveTime 0.1 s; server tick 30 Hz (LAN 35) | CONFIRMED [NET 5], [INI Xe-TransEngine] |
| Voice | Xbox Live voice carried inside the game packets (VDP: appended to a packet when >= 113 bytes remain under MaxPacket 510); MaxRemoteTalkers 30, MaxLocalTalkers 4 | CONFIRMED [NET 5], [INI Xe-TransEngine] |
| Voice in the movies | PlayerList_GFX column HeadsetState: 0 no headset (icon hidden), 1 headset (icon), 2 speaking (speaking_mc shown); Hud_GFX sprite 573 lists the names of players whose HeadsetState is 2, 24 px apart; "Mute" / "Unmute" ($UIText.PlayerList.Mute / UnMute) | CONFIRMED [AS PlayerList_GFX, Hud_GFX], [INT] |
| Hit registration | instant-hit weapons are client-authoritative ("favour the shooter"); vehicles are owner-simulated | CONFIRMED [NET 8] (not needed by lockstep; listed for the console fallback) |

### 1.2 Capacity, teams, starting a match

| rule | value | label / source |
|---|---|---|
| Players per match | MaxPlayers 10 ([Engine.GameInfo]); MaxPlayersAllowed 10 on TnMultiplayerGame / TnVersusGame / TnTeamGame / TnFreeForAllGame, 4 on TnSurvivalGame; 32 on the lobby games (TnPartyLobbyGame, TnGameLobbyGame*); NumPublicConnections / NumPrivateConnections 10 for the versus modes, 3 for CP / CCP, 4 for SV | CONFIRMED [INI], [MP add. 3 / 8], [CDO] |
| Join in progress | bAllowJoinInProgress **true** on every game-session settings class (TnOnlineGameSettings{TDM, DM, CTF, KOTH, DOM, EXT, CP, CCP, SV} and their Public / Private variants; Engine default true); **false** on the party sessions (TnOnlineGameSettingsPartyPersistent / PartyPresence) | CONFIRMED flag [CDO]; HIGH that versus sessions admitted joiners into a running match (the flag is what UE3's session search and join honour) |
| Invites / presence joins | bAllowInvites and bAllowJoinViaPresence true on the game sessions (public and private); PartyPersistent false / false, PartyPresence invites true, presence joins false | CONFIRMED [CDO] |
| Stats | bUsesStats false on the *Private settings, true on *Public (the private-match "no XP" rule) | CONFIRMED [CDO], [MP 3] |
| Spectators | MaxSpectators 2 | CONFIRMED [INI Engine.GameInfo] |
| Players required (public autostart) | NumRequiredPlayers 4 for TDM / DM / CTF / KOTH / DOM / EXT; 2 for co-op / Escalation; 1 for solo campaign | CONFIRMED [INI TnOnlineGameSettings*] |
| Team pick | TnTeamHandlerTwoTeams.PickTeam: the smaller team, random on a tie; parties are kept together by the reservation | CONFIRMED [MP add. 3] |
| Team cap | none in PickTeam (balance only); 5 v 5 at 10 players in practice | CONFIRMED / HIGH [MP add. 3] |
| Lobby status texts | "Finding `p more players" / "Finding `p more player" / "Waiting for all players to choose a character" / "Waiting for host to start game" / "Waiting for balanced teams" | CONFIRMED [INT TransGame.int TnGameReplicationInfoGameLobby] |
| Public lobby countdown | LobbyIntermissionTime 45 s, low-tick sound from 10 s | CONFIRMED [INI TnGameReplicationInfoGameLobby] |
| Private lobby start | host-started (CanHostRequestGameStart -> Online.BeginLobbyExitCountdown) | CONFIRMED [AS GameLobby_GFX], [CODE] |
| Map vote | MapSelectionMethod (Host's Choice / vote); veto quorum = NumRequiredPlayers / 2 + 1 (RequiredVetos, MapVetos) | CONFIRMED [MP add. 8], [AS GameLobby_GFX] |
| Match start | MatchAutoStartCountdown 10 s (Escalation 15) | CONFIRMED [MP add. 8] |
| Match end | MatchOverCountdown 15 s, then travel back to the game lobby | CONFIRMED [MP add. 8], [NET 2] |
| Respawn | per-player 5 s timer (TnRespawnHelperWave); the initial spawn is immediate | CONFIRMED [MP add. 8] |
| XP | public matches only (CanGainXp = !IsPrivateGame); private matches give no XP or challenge progress | CONFIRMED [MP 3] |
| Bots | none in the original MP (no bot menu; AssignTeams skips bBot) - the rebuild's bots are a PC ADAPTATION | CONFIRMED [MP add. 3] |

### 1.3 Idle, leave, disconnect: what the player saw

| event | original behaviour / text | label / source |
|---|---|---|
| Idle | MaxIdleTime 180 s, bKickLiveIdlers true (also on every lobby / mode game CDO): "Kicked for idling." | CONFIRMED config + text [INI Engine.GameInfo], [CDO], [INT Engine.int]; HIGH that the kick applied in versus |
| Service reconnect | DemonwareLobbyService MaxReconnectInterval 30 s (the online service connection, not a game-session rejoin) | CONFIRMED [CDO] |
| A player leaves | kill-feed line "`k has left the game" | CONFIRMED text [INT TransGame.int LeftGameString] |
| Client loses the host | "Connection Error" / "Your connection to the host has been lost." | CONFIRMED text [INT Engine.int]; HIGH: UE3 then returns to the front end |
| Host quits | "The game has ended because the host has quit." | CONFIRMED text [INT]; matches "no host migration" [NET 1] |
| Leaving a lobby or match | "Quit Game" / "Are you sure you want to leave the game?"; public: "All gained XP will be lost. Do you want to proceed?"; Escalation: "Your leaderboard progress will be lost." | CONFIRMED [INT UIText.int Lobby] |
| Leaving the party lobby | "Quit Lobby" / "Are you sure you want to leave the party lobby?"; "Quit Party" / "Are you sure you want to leave the party?" | CONFIRMED [INT] |
| Party member waiting | "Waiting for party leader..." (option "Leave party") | CONFIRMED [INT] |
| Joining | "Joining..." / "Join game?"; "Joining this game will disband your party. Continue?"; "Joining game..."; "Connecting to Host" | CONFIRMED [INT] |
| Join failed | "Join Game Failed" / "Could not join the game. The game is full or no longer available." | CONFIRMED [INT] |
| Hosting | "Hosting Game" / "Creating lobby..."; strict NAT: "Unable to Host" / "To allow hosting of a game, switch your NAT from strict to open or moderate." | CONFIRMED [INT] |
| Party too large | "`g only supports a party size of `i. You have `n players in your party." | CONFIRMED [INT] |
| Missing map | "Map Not Found" / "You do not have the selected map and will not be able to play in this match without it." | CONFIRMED [INT] |
| Pause | "Game Paused by Host" exists, but the MP world never pauses (TnGame.bPauseable false); the pause menu is local | CONFIRMED text [INT]; HIGH behaviour [CODE UIController comment] |
| Ready toggle | none in the movies: Online.SetPlayerReadyState exists only in the shared ExternalInterface wrapper that every movie carries, and no movie calls it. The lobby's gate is "every player has chosen a character" plus balanced teams plus the host's start | CONFIRMED [AS] (call count = wrapper copies only) |

### 1.4 Unknown (open questions; not guessed here)

- Join in progress: the flag is CONFIRMED true on every game session (1.2). Still unknown: how a joiner entered a running
  versus match (spawned at once, or waited for the respawn timer; which team; whether the public lobby kept advertising
  during the match). Open question R1.
- Party size limit for versus playlists (the "`g only supports a party size of `i" check): its numbers. No MaxPartySize /
  NumPlayersPerTeam / NumTeams property exists on any authored CDO [CDO], so the limit is in code or in the playlist
  file. R2.
- Team balancing: TnTeamHandlerTwoTeams / TnTeamHandler carry no authored properties [CDO]; the logic is code only
  (PickTeam, [MP add. 3]).
- How idle was measured (input only, or input + movement), and whether private matches kicked idlers. R3.
- What a client saw after "connection lost": the front end, or the party lobby with the party kept. HIGH is the front
  end (UE3 default travel), with the party gone. R4.
- Voice routing in versus: team only, or everyone (the Live default was the session's voice channel; the per-team rule is
  not in the notes). R7.
- Return-to-session (beacon packet 9, ReturnToSessionResponse): when the client sent it (after a match? after a
  disconnect?). R5.
- Whether the public lobby countdown restarted when a player left below NumRequiredPlayers. R6.

---

## 2. Where we start: the offline frontend today [CODE]

- **Levels** (GameFlow LevelKind): FrontEnd -> PartyLobby -> GameLobby -> Match, with the original's loading screens
  between them.
- **UI states** (UIController): NotInGame, FrontEnd, InLobby, WaitingOnGameStart, InGame, Spectating, Paused,
  PausedSpectating, WaitingOnNextRound, GameEnded. The events are the original's (0-17), including GameInviteAccepted 16
  and JoinInviteFailed 17, which nothing raises yet.
- **Party lobby** (PartyLobby_GFX):
  - Find Game (public playlists) and Host Game (Private Match -> mode list -> Game Mode Options with the bot rows ->
    Create Game).
  - Select Matchmaking (UseAllRegionMatchmaking).
  - Online.PlayPlaylist hosts the playlist game locally; the search is bypassed.
- **Game lobby** (GameLobby_GFX):
  - Start Game, Select Map, Create a Character, Teletran I, Friends List.
  - Player list (PlayerList_GFX) with team columns.
  - Lobby status text; host start (CanHostRequestGameStart).
  - Online.IsHost returns true; NumOnlinePlayers "0".
- **Match:** the HUD, the scoreboard (InGameStats_GFX), respawn (MultiplayerRespawn_GFX), pause (PauseMenu_GFX,
  Quit Match), end of match (EndGameStats_GFX) -> back to the game lobby.
- **Remote players:** none. Everyone else in a match is a bot (Smart AI by default). Lobby bot settings live in Game
  Mode Options (7da913ed).

---

## 3. Player flows (phase 1: PC to PC)

The aim is that a player who knows the original finds every step where it was. New steps reuse original screens and
original text wherever one exists. PC EXTENSION marks what has no original counterpart.

### 3.1 Entry

Main menu -> Multiplayer -> party lobby, as today. No sign-in step: the original's Live sign-in and Demonware login
([NET 2]) are replaced by the local identity (display name + fingerprint, CROSSPLAY_PROFILE_STATS.md section 3). The
Accounts screen's account name becomes the display name (decision D1).

Party lobby menu:
- **Find Game:** the original's public matchmaking. Phase 1 has no directory, so it stays a local playlist game with
  bots, as today (no XP rule change: open question D8). Phase 2+, with an optional session list (netcode open
  question 6 / profile open question 4), it searches.
- **Host Game:** Private Match, as today, now joinable by others (3.2).
- **Join Game** (PC EXTENSION): a new party-lobby row under Host Game, the same mc_panelButton form as the original rows.
  Hint: "Join a friend's game with a code or an address." It opens the code entry (3.3).
- **Friends List:** keeps the original's Join Game button (Online.JoinFriendGame). It joins remembered friends
  (fingerprints from earlier sessions) once a friend's session can be found without a directory, i.e. a stored last
  address / code. Phase 2.

**Simplified vs the original:**
- No sign-in, and no Live or Demonware services: a local identity instead. Why: those services are gone and the project
  runs no servers.
- Find Game stays a local game with bots until a session list exists. Why: matchmaking needs a directory nobody runs
  yet.

### 3.2 Host

1. Host Game -> mode list -> Game Mode Options (the mode's rows + Player Limit, bot counts, Bot Difficulty) -> Create Game.
2. "Hosting Game" / "Creating lobby..." (original popup texts) while the session opens. If no path can be opened (no
   UDP port bound, every STUN attempt failed), the original's "Unable to Host" popup appears with a PC message (D2).
3. The game lobby opens as today. Additions, PC EXTENSION:
   - a **join panel** in the lobby's right column under the team lists:
     - "Join code: WFC-7QK2-M9TD" (copy with one key);
     - "Address: 203.0.113.5:7777";
     - a privacy line, "Players who join can see your IP address." (netcode section 9);
     - shown only to the host;
   - player rows of remote humans next to the bots. Bots fill the slots humans leave free (3.4).
4. Game Mode Options stay host-only. The host can reopen them from the lobby menu (new row "Host Options", the original
   text $UIText.Lobby.HostOptions), as long as no countdown runs. Changes are broadcast to every lobby member.

**Simplified vs the original:**
- The host opens one session directly, with no party reservation beacon and no Live session advertising.
- A join code / address replaces the session search and the secure host address (XNADDR / XNKID).
- Why: the lockstep session layer (Systems) carries the lobby itself; codes work with no directory.

### 3.3 Join

1. Join Game -> TextPrompt_GFX (the original's text-entry movie; the Accounts screen already uses it for typed names). The
   field takes a join code (WFC-xxxx-xxxx) or an address (host:port / [v6]:port). Escape cancels.
2. "Joining game..." (MessagePrompt_GFX, original text), then "Connecting to Host" (the game lobby's own connecting
   animation, connecting_mc, already in GameLobby_GFX).
3. Checks on join, each with a popup and a return to the party lobby:
   - full: "Join Game Failed" / "Could not join the game. The game is full or no longer available." (original);
   - no answer within the original's InitialConnectTimeout (50 s): the same text;
   - a different simId or protocol version (PC EXTENSION; netcode section 3): "Join Game Failed" / "This game runs a
     different version of the rebuild." (superseded: the original $Engine.Errors.ClientOutdated text, ONLINE_MP_UI_STRINGS.md);
   - a missing map: the original's "Map Not Found" popup and text.
4. On success the client is in the host's game lobby as a member:
   - **no host controls:** CanHostRequestGameStart false (the Start Game row greys out, as the original's member
     view), Select Map follows the host (or offers the veto when the host chose a vote);
   - the status line shows the host-driven texts ("Waiting for host to start game", "Waiting for all players to choose
     a character").
5. A member can leave at any time: Back -> "Quit Game" / "Are you sure you want to leave the game?" (original) ->
   party lobby.

**Simplified vs the original:**
- Joining is by code or address only: no presence join and no matchmaking search.
- The original's reservation, which kept a whole party together, is not in phase 1.
- Why: there is no directory or friends service; parties come in phase 2 (3.10).

### 3.4 Lobby

- **Player list:** PlayerList_GFX rows for every human and bot (bots shown, as today: PC ADAPTATION, since the original
  hid bBot rows [MP 13]).
  - **IsConnecting** (an original column): true while a joiner's connection or data check runs; the original's own
    connecting icon and sort order (connecting rows last).
  - **HeadsetState** (an original column): 0 voice off or no device, 1 voice on, 2 speaking (3.12).
- **Teams:** the original rule, PickTeam = the smaller team, random on a tie [MP add. 3]. Online.SwitchTeam (Change Team,
  already wired offline) moves a human if the target team has room. Humans take bot slots: a joining human replaces the
  last bot of the team they join (PC ADAPTATION; bot counts in Game Mode Options are a maximum).
- **Character choice:** Create a Character, as today; each member's chosen character, specialty and loadout go to every
  peer in the lobby state (profile doc section 4: unlock trust is decision D3).
- **Readiness:** the original has no ready toggle (1.3). Phase 1 keeps the original gate:
  - every human has chosen a character ("Waiting for all players to choose a character");
  - teams are balanced if Team Balancing is on ("Waiting for balanced teams");
  - then the host starts.

  An explicit Ready toggle would be a PC EXTENSION, decision D4.
- **Public autostart** (phase 2+, with a session list): the original's LobbyIntermissionTime 45 s countdown once
  NumRequiredPlayers (4) humans are in; "Finding `p more players" until then.
- **Map:**
  - Host's Choice: the host's Select Map, as today.
  - Vote: the original veto (Veto Map, RequiredVetos = NumRequiredPlayers / 2 + 1), counted over humans only.

**Simplified vs the original:**
- Bots are listed and fill free slots; humans take bot slots (the original had no MP bots).
- Phase 1 has no public autostart, because there are no public lobbies yet.
- Why: offline bot play is how the rebuild fills matches today, and the user's offline settings carry over unchanged.

### 3.5 Start and loading

1. The host presses Start Game -> the original lobby exit countdown (BeginLobbyExitCountdown) on every member's screen.
2. Every peer loads the map behind the original loading screen (LoadScreen_GFX + tips). A new **load gate** (PC
   EXTENSION, needed by lockstep): the match does not start until every human reports "loaded" with matching simId and
   load hash (netcode section 6).
   - While waiting, the loading screen's status line shows "Waiting for players (`n)" (new text).
   - A peer that never loads within InitialConnectTimeout (50 s) is dropped. Its slot becomes a bot, and the others
     see "`k has left the game".
3. The original MatchAutoStartCountdown (10 s), run in sim ticks so it ends on the same tick everywhere. Then InGame,
   as today.

**Simplified vs the original:**
- A load gate is added: the match starts when every peer has loaded. The original listen server started and let
  clients arrive as they loaded.
- Why: lockstep needs every peer on the same tick from the first command.

### 3.6 In the match

- **No pause:** the original MP world never paused. The pause menu (PauseMenu_GFX) is local and the match keeps
  running, as offline today.
- **Idle:** the original MaxIdleTime 180 s kick, "Kicked for idling.". In lockstep the kick is the host's decision,
  applied at an agreed tick; the idler returns to the party lobby with the original text, and its slot becomes a bot.
  Whether idle bots count is decision D5.
- **Join in progress:** the original allowed it (bAllowJoinInProgress true on every game session, 1.2). Being faithful
  needs late join by state transfer, taking over a bot slot (netcode section 7), so:
  - Phase 1 (before state transfer exists): a joiner during a running match waits in the host's game lobby with the
    status "Match in progress" (new text) and enters with everyone else when the match returns to the lobby. The
    alternative is refusing with the original "game is full or no longer available" text (D6).
  - Phase 1b: faithful join in progress. The joiner loads the map, receives the state, and takes over a bot slot on the
    smaller team (PickTeam) at an agreed tick; it spawns through the normal respawn path (R1 for the original's exact
    entry).
- **A human leaves** (clean quit, or no packets for the original ConnectionTimeout 8 s):
  - the others see "`k has left the game" (original kill-feed text);
  - the slot becomes a bot at an agreed tick (netcode section 7);
  - the score row stays with the bot (netcode open question 8; the original dropped the row).
- **Desync** (SIMHASH mismatch; PC EXTENSION):
  - phase 1: the match ends for everyone with "Connection Error" / "The game has gone out of sync and cannot
    continue." (new text) and everyone returns to the game lobby;
  - phase 1b: state-transfer recovery behind a short "Resynchronising..." banner instead.

**Simplified vs the original:**
- Join in progress (allowed by the original) waits for phase 1b; phase 1 joiners wait in the lobby (D6).
- A leaving player's slot becomes a bot and keeps its row; the original removed the player.
- Desync handling is new.
- Why: late join and recovery need state transfer, which phase 1 does not have. A bot keeps the teams even, which a
  lockstep match cannot rebalance mid-tick.

### 3.7 Host loss

- **The original:** no migration; the session ends with "The game has ended because the host has quit." [NET 1], [INT].
- **The rebuild** (decision D9):
  - (a) **Faithful:** the match ends for every client with that popup. The client returns to the party lobby (not the
    main menu, so the player can host or join again at once; the original's exact return point is R4).
  - (b) **Host migration** (netcode section 7: every peer has the state and command history): the match continues; a
    short banner says "The host left. `k is now the host." (new text). Migration is PC EXTENSION.

  Recommendation: (a) for phase 1 (simpler, faithful), (b) in phase 1b, once state transfer exists.

**Simplified vs the original:** none in phase 1. It is the original's end of session, with its text.

### 3.8 Reconnect (phase 1b)

- A client that lost the connection gets "Connection Error" / "Your connection to the host has been lost." (original),
  with two choices (PC EXTENSION): **Rejoin** and OK.
- Rejoin uses the stored join code / address. The host recognises the player's fingerprint and gives the slot back
  from its bot (late join + takeover, netcode section 7).
- The window lasts until the match ends. After the match the player rejoins the lobby like any joiner.
- The original's return-to-session request (beacon packet 9) is the nearest counterpart; its timing is R5.

**Simplified vs the original:**
- New: the original had no rejoin into a running match (the return-to-session timing is R5).
- Why: lockstep keeps every peer's full state, so a rejoin is a late join with the player's old slot.

### 3.9 After the match

- EndGameStats_GFX, as today (MatchOverCountdown 15 s); every peer computes the same results; each saves only its own
  player's progression (profile doc section 4).
- Then everyone travels back to the **host's game lobby** together (the original's seamless return [NET 2]). Members
  keep their characters and teams; the host can change options and start again.
- A member who leaves the end screen early ("Quit Match") returns to the party lobby. The host leaving at this point is
  host loss (3.7).

**Simplified vs the original:** none. It is the original's seamless return to the game lobby.

### 3.10 Parties and invites (phase 2)

- The original's party: members follow the leader into the leader's game (beacon "tell clients to join"). Non-leaders
  see "Waiting for party leader..." / "Leave party".
- Phase 2 can rebuild it on the same session layer: a party is a small session; when the leader joins or hosts a game,
  the party's members get a join instruction.
- Invites (Friends List -> "Invite Friends" / "Sent you a game invite!") become a code the inviter sends out of band
  (copy / paste, chat) until a directory exists. The original's GameInviteAccepted (event 16) and JoinInviteFailed (17)
  then get raised.

**Simplified vs the original:**
- Deferred to phase 2.
- Invites become codes until a directory exists.
- Why: no friends or presence service.

### 3.11 Phase 2: PC to browser

- The same flows. Browser specifics (CROSSPLAY_PLATFORM_FEASIBILITY.md section 3):
  - **Data:** a data-loading step before the main menu ("Select your game data folder"), and the user-gesture
    "Click to start" that WebAudio needs.
  - **Hosting:** the browser joins only. Hosting needs a PC peer, or a signalling helper: a browser cannot bind a
    port; WebRTC with a signalling exchange works (join code carries the offer).
  - **Settings:** browser-only rows (no HD, no RT; Faithful / Off only).
  - **Mixed lobbies:** the host's Game Mode Options cap Player Limit to the browser's supported size (5 v 5) when a
    browser peer is in the lobby (D10).
- The join code is the same for both platforms. The lobby shows each member's platform as a small text tag
  ("PC" / "Web") after the name (D11).

---

**Simplified vs the original:** not applicable (there was no browser version).

### 3.12 Voice chat (decision D7: in scope)

The user's decision: push-to-talk by default, an open-mic option, per-player mute, team or all channel, and the
original's talker icons and HeadsetState column where the movies have them.

**Settings** (Settings > Audio, rows added after the original volumes, the technique of the Graphics rows):

| row | values | default |
|---|---|---|
| Voice Chat | Off / Push to Talk / Open Mic | Push to Talk |
| Voice Channel | Team Radio / All Radio / Proximity / Radio + Proximity | Radio + Proximity (FFA modes: All Radio + Proximity's positional part) |
| Proximity Volume | 0-100 in tens (only if Systems' mix wants a separate level) | 70 |
| Input Device | System Default, then the capture devices by name | System Default |
| Push-to-Talk Key | a short list: V, T, B, Caps Lock, Mouse 4, Mouse 5 (pad: hold the left d-pad) | V |
| Voice Volume | 0-100 in tens (the original volume rows' scale) | 70 |

- These are saved under [PCSettings] (VoiceMode, VoiceChannel, VoiceDevice, VoicePttKey, VoiceVolume).
- Open Mic gets a fixed voice-activity threshold in phase 1. A sensitivity row follows only if playtests ask for it.
- A Push-to-Talk binding conflicts with a game binding when it shares a key; the row then shows the hint "Also bound
  to `a" (NEW text). Nothing is unbound automatically.

**Lobby and scoreboard:**
- **Talking indicator:** PlayerList_GFX's HeadsetState per row (1 voice on, 2 speaking), the original icon and
  speaking_mc. Muted players show 1 even while they talk.
- **Mute:** selecting a player row (game lobby list, in-match scoreboard) offers the original "Mute" / "Unmute"
  ($UIText.PlayerList.Mute / UnMute). Mutes are local only (nobody is told) and are remembered per fingerprint in the
  profile, so a muted player stays muted in the next session.
- Bots have no voice. Their HeadsetState is 0.

**In the match:**
- The HUD's own talker list (Hud_GFX sprite 573) shows the names of the players speaking, including the local player
  while the key is held or the open mic is live.
- The channel follows Voice Channel (user addition 2026-10-17, proximity voice; Systems' architecture section 7):
  - **Team Radio:** the own team, anywhere on the map;
  - **All Radio:** everyone, anywhere;
  - **Proximity:** everyone within range (64 m), heard from the speaker's position with room reverb;
  - **Radio + Proximity** (default): team radio anywhere, plus positional voice for everyone in range.
- **Which channel a speaker is on:** the session reports it with each speaking state (MemberVoice.voiceVia).
  - Radio: the original headset / speaking icon (HeadsetState 2) and the HUD talker list as the original.
  - Proximity: the same speaking icon, and in the HUD talker list the name followed by the "Near" tag (NEW text).
  - A speaker heard both ways shows as radio.
  - The per-player mute is one switch for both kinds.
- **Spectators and the dead:** they hear and talk on their team channel as normal. The original's dead-chat rule is
  unknown (R7).

**Browser (phase 2):** the browser asks for microphone permission the first time voice is used; with no permission,
Voice Chat behaves as Off and the row's hint says so (NEW text).

**Simplified vs the original:**
- Push-to-talk by default (the user's decision); the original was open mic on a headset.
- Team / All is a player setting, not a rule of the session.
- Mutes persist per fingerprint, not per gamertag.
- Voice travels on the session layer's own channel (Systems' transport, e.g. WebRTC audio), not inside game packets.
- Why: lockstep packets carry only commands, and PC players have no headset convention.

## 4. Screens: exist vs must be made

| screen / element | movie | status |
|---|---|---|
| Party lobby menu (Find Game, Host Game, Select Matchmaking) | PartyLobby_GFX | exists |
| Join Game row (party lobby) | PartyLobby_GFX + a duplicated mc_panelButton row | **add** (PC EXTENSION; same technique as the Graphics menu's added rows) |
| Code / address entry | TextPrompt_GFX | exists (reuse; text and validation new) |
| Joining / hosting / failure popups | MessagePrompt_GFX | exists, with the original texts; new texts: version mismatch, out of sync, host migrated, waiting for players |
| Game lobby, member view | GameLobby_GFX | exists (host controls greyed by CanHostRequestGameStart) |
| Connecting animation | GameLobby_GFX connecting_mc ("CONNECTING TO HOST") | exists |
| Player list incl. IsConnecting / HeadsetState | PlayerList_GFX | exists (columns already read; values new) |
| Join panel (code, address, privacy line) | GameLobby_GFX | **add**: text fields created at run time in the right column, the way the FPS counter / DLSS credit create theirs (overlay text field, $NormalFont) |
| Host Options from the lobby | PartyLobby_GFX mc_menuMultiplayerGameModeSettings | exists in the party lobby; **add** a way to open it from the game lobby (or a GameLobby_GFX lateral copy) |
| Loading "waiting for players" status | LoadScreen_GFX | exists (status text field; new text) |
| Kill feed "has left the game" | Hud_GFX | exists (original string) |
| Rejoin choice on connection loss | MessagePrompt_GFX two-button form | exists (original two-button prompts, e.g. Join / cancel) |
| Platform tag ("PC" / "Web") | PlayerList_GFX name cell | **add** (appended text, D11) |
| Voice rows (Voice Chat, Voice Channel, Input Device, Push-to-Talk Key, Voice Volume) | SettingsMenu_GFX Audio page | **add** (rows inserted before the page's last row, as on the Graphics page) |
| Mute / Unmute on a player row | PlayerList_GFX (lobby, scoreboard) | exists (original texts); the row action is **added** |
| Talking indicator | PlayerList_GFX HeadsetState + speaking_mc | exists (values new) |
| Talker list | Hud_GFX sprite 573 | exists (reads HeadsetState 2) |
| Desync / migration banner | Hud_GFX game message (mc_gameMessage_default) | exists (HUD message line) |
| Session browser (phase 2+) | PartyLobby_GFX playlist list form | **add** later, built from the playlist list form |
| Data-folder step (browser) | none | **add** in phase 2 (outside GFx: a DOM page before the canvas) |

No new GFx movie has to be authored for phase 1: every addition is a row, text field or popup text in an existing
movie, using the techniques the frontend already uses (lateral rows in Game Mode Options, the Graphics menu rows, overlay
text fields).

---

## 5. Frontend state machine changes (list; no code yet)

GameFlow:
1. **Session role** on LobbyState: Offline (today), Host, Member. Online.IsHost answers from it (today always true);
   CanHostRequestGameStart, Game Mode Options and Select Map gate on Host.
2. **Remote lobby state:** members get the host's LobbyState (mode, map, options, teams, participant table, status,
   countdown) through the session layer. Data-store reads (<CurrentGame:*>, <TnGameSettings:*>) answer from it, so the
   movies need no change.
3. **New bridge entries** (frontend side; the session layer behind them belongs to Systems):
   - Online.JoinByCode(code) / Online.JoinByAddress(addr), PC EXTENSION;
   - Online.CancelJoin;
   - the original Online.JoinFriendGame(NickName) mapped onto a stored friend address (phase 2).
4. **Join states**, raised by the session layer: Joining -> Connecting -> InLobby (member), or JoinFailed(reason:
   full / timeout / version / map). JoinFailed raises the original UI event JoinInviteFailed (17) and its popup.
5. **Participant table:** humans (local + remote, by fingerprint) and bots per slot. The bot counts from Game Mode
   Options become maxima, reduced by the humans on each team. IsConnecting per row.
6. **Lobby gate:** start allowed when every human has a character (and teams are balanced if Team Balancing is on);
   the lobby status follows the original texts (WaitingForCharacterChoiceStatus, a new LobbyStatus value next to the
   existing 1 / 3 / 4 in DataStores; WaitingForBalancedTeamsStatus = 4;
   WaitingForHostToStartStatus = 3).
7. **Load gate state** between the lobby exit and the match start: LoadingMap -> WaitingForPeers(n) -> Countdown. The
   loading screen text follows it. A timeout drops the slow peer (it becomes a bot).
8. **Match-side session events into the flow:**
   - PeerLeft(k) -> kill-feed text + bot swap (Gameplay);
   - HostLost -> end-of-session (3.7 a) or migration (b);
   - Desync -> end-of-session (phase 1);
   - Kicked(idle) -> party lobby with "Kicked for idling."
9. **Return paths:**
   - session end, a member's quit, or a failed join -> the party lobby (UIState InLobby under the PartyLobby
     controller);
   - the match's end -> everyone to the host's game lobby (today's travel, run by the host for all);
   - host loss -> the party lobby with the original popup.

UIController (the original states stay; events added or raised):

10. Raise GameInviteAccepted (16) when a join starts from an invite or code, and JoinInviteFailed (17) on failure, the
    original events [CODE UIController.h].
11. Session end while InGame / Spectating / Paused / PausedSpectating / GameEnded -> close the HUD and menus and go to
    NotInGame, then follow the flow's return path. The same transition as GotoMainMenu (11), with the target set by the
    flow.
12. No new UI state is needed for "waiting for players": it lives under the loading screen (GameFlow LoadingScreen),
    not in UIController.

Profile / data stores:

13. <PlayerOwner:PlayerName> from the identity's display name. <CurrentGame:NumOnlinePlayers> = the humans in the
    session.
14. <CurrentGame:Players> rows for remote humans: name, team, level (self-reported), IsConnecting, platform tag.
15. A stored "last joined" code / address per friend (phase 2 Friends List).

Voice (3.12):

17. Profile: the [PCSettings] voice fields and a muted-fingerprint list. The Audio page rows read and write them through
    <PCSettings:Voice*> data-store keys, as the Graphics rows do.
18. Session boundary additions (Session.h, when voice code starts):
    - calls: setVoice(mode, channel, device, pttDown) and setMuted(member, bool);
    - an event: TalkerState(member, talking).
    The frontend turns them into HeadsetState per <CurrentGame:Players> row; the audio capture and playback belong to
    Systems.
19. Push-to-talk: the key is read from raw input as a held key, never through the sim command stream (UI-only input,
    netcode section 4).

Tests (frontend, all headless, before any network exists):

16. A fake session layer driving the flow through host, join (success / full / timeout / version / map), member
    lobby, load gate, peer leave, host loss, desync and the return paths. Assertions on UI states, open movies,
    LobbyStatus and the popup texts, the same way testFlow drives offline matches today.

---

## 6. Decisions (D) and open RE questions (R)

Decided 2026-10-17 by Integration (rebuild-07), faithful where the original defines a rule; D7 by the user.

- **D1 decided:** the Accounts screen's account name is the online display name (no separate field).
- **D2 decided:** "Unable to Host" keeps the original title; the message is a PC text ("No connection could be opened
  for others to join."). The original NAT wording is kept wherever it still applies.
- **D3 decided:** phase 1 (friends / private sessions) trusts each player's lobby loadout; a host-authoritative unlock
  check comes with public matches.
- **D4 decided:** the original gate - every player has a character, teams balanced (when Team Balancing is on), the
  host starts. No Ready toggle.
- **D5 decided:** the idle kick is on at the original 180 s in every session (public and private).
- **D6 decided:** in phase 1, a player joining during a match waits in the host's game lobby ("Match in progress") and
  enters with everyone for the next match; faithful join in progress (state transfer, bot slot) comes in phase 1b.
- **D7 decided (user):** voice chat is in scope - push-to-talk by default, an open-mic option, per-player mute, Team /
  All channel, the original's talker icons and HeadsetState column where the movies have them (3.12).
- **D8 decided:** faithful - no XP in private online games (the original's bUsesStats false / CanGainXp rule).
- **D9 decided:** host loss ends the session in phase 1 (the original's "The game has ended because the host has
  quit."); host migration comes in phase 1b.
- **D10 decided:** while a browser peer is in the lobby, Player Limit is capped to 5 v 5 automatically.
- **D11 decided:** the lobby list shows each member's platform ("PC" / "Web").
- **R1** How a joiner entered a running versus match in the original (spawn at once or on the respawn timer, team
  choice, advertising during the match). The flag itself is CONFIRMED true [CDO].
- **R2** Versus party size limit (the numbers behind "`g only supports a party size of `i").
- **R3** Idle measurement and whether private matches kicked idlers.
- **R4** The client's return point after "connection lost" / "host has quit": front end or party lobby.
- **R5** When the original sent the return-to-session request (beacon packet 9).
- **R6** Whether the public lobby countdown restarted below NumRequiredPlayers.
- **R7** The original's voice routing in versus (team only or everyone; whether dead players talked to the living).
