# The original's party and invite semantics (RE answers to ONLINE_MP_PHASE2_FLOWS R8 / R9, plus R2) (RE, 2026-10-18)

This answers the RE questions in Rebuild `docs/handoff/ONLINE_MP_PHASE2_FLOWS.md` §1.3 / §7. This lane can't write into Rebuild,
so Frontend should paste or merge it into that doc's RE section.

Sources (bytecode, decompiled read-only):
- `TransContent.TnPartyLobbyGame`, decompiled this item from `UI_PartyLobby_m.xxx` →
  `work/script/decomp_ui/UI_PartyLobby_m.TransContent.txt`;
- `TransGame.TnPlayerController`, `TnPlayerControllerPartyLobby`, `TnOnlineGameSettingsBase`, `TnOnlineActionScriptBinding`;
- `Engine.OnlineDisconnector`, `OnlineSessionJoiner`, `OnlineRecentPlayersList`, `PlayerReplicationInfo`;
- `IpDrv.OnlineReservationInterfaceImpl` (all in `work/script/decomp`).

Labels: C = script / data; H = inference; **LIVE** = Xbox Live / Demonware-layer behaviour with no equivalent on our relay
(not applicable).

## 0. The model (C)

- **The party lobby is a real listen server hosted by the leader.**
  - `BuildPartyLobbyURL` = `UI_PartyLobby_m?game=TnPartyLobbyGame?listen?GameTeamStatus=n`.
  - Members are network clients of the leader's party lobby map. On the original they joined it through the 'Party' online
    session plus a party reservation beacon.
  - `TnPartyLobbyGame.PostLogin`: a remote member gets `ClientJoinSession('Party', …, option 1)`, which joins the Live
    presence session (LIVE).
- **Every player always has a local party lobby of their own.** On entering the party lobby, `WaitingForTasksToIdle.Tick` waits
  for the online layer to go idle, then:
  - **not a party client** → `CreatingPartySessions` → `Ready`: you are the leader of your own party (alone or with members);
  - **party client** (`IsPartyClient`) → **`AwaitingInstructionsFromPartyLeader`** (§2).

## 1. What the leader's start does to the members (C)

Both start paths first run **`CheckCanPlayGameType`** (§3). On failure the leader gets the "Party Too Large" popup, nothing
starts, and the members are untouched.

- **Leader hosts** (Private / Custom Match: `HostOnlineGameInternal` → `HostingGame`):
  1. `StartOnlineGame('Party')`; reservations of anyone not yet joined are cancelled.
  2. Once the game session is created: `TellPartyToJoinSession(Game, PostJoinSessionOption = 1)`. Every member's
     `OnJoinSessionRequestReceived` → `ClientJoinSession(…, 1)` → the session joiner **joins the Live game session only, no
     travel** (option 1 ends in Ready) (LIVE bookkeeping).
  3. The leader runs **`ServerTravelToMap(GameLobbyURL?listen, seamless = true, …)`**. The members are already connected
     clients of the leader's party-lobby server, so **they're carried along by the leader's server travel**: their
     party-lobby state is replaced by the game lobby (UI_Lobby_m) exactly as for any connected client.
  - **That is how a member's own lobby state is replaced:** it never ran its own lobby while in the party. It was showing the
    leader's server, and the server changed maps.
- **Leader joins a found / friend's game** (matchmaking `ReservingSpace` → `JoiningGame`):
  1. The leader reserves space for the whole party (the party list goes into the reservation), then joins.
  2. If the party has more than 1 member: `TellPartyToJoinSession(Game, option 0)` and, **1.0 s later**, the leader travels
     itself (`OnTravelToGameSession`).
  3. Option 0 makes **each member reserve, join and `ClientTravel` to the game host itself** (OnlineSessionJoiner Reserving →
     Joining → Travelling).
  - A member whose join fails gets the joiner's failure (NotifyInviteFailed path) and stays behind (H: in its own party
    lobby).
- **Joining a friend's game as a leader with members:**
  - the prompt is `JoinFriendGameAndDisbandMessage` (`IsPartyLeader && GetNumPartyMembers > 1`), otherwise
    `JoinFriendGameMessage`;
  - accepting goes through the invite path (`JoinSessionUsingGameInvite`, option 0), whose Disconnecting state **cancels the
    'Party' reservation and destroys the reservation host**. So the party really disbands (§4).

**For our relay:** "TellPartyToJoinSession" is our `PartyFollow` event.
- Host case: members go into the leader's new game lobby as its members (same as now).
- Join case: each member runs the phase 1 join by code, and the leader's own join is delayed **1.0 s** after it sends the
  follow.
- The Live session joins (option 1) are not applicable.

## 2. "Waiting for party leader..." and "Leave party" (C)

- **Trigger:** state `AwaitingInstructionsFromPartyLeader`, entered on **a party client that is in its *own* local party
  lobby**. That happens after `ClientReturnToParty` / `QuitToPartyLobby` brought it back from a game (§5), or on first entry
  as a party client.
- **BeginState:**
  - `ShowPopupUI("$UIText.MessagePrompts.PartyLobbyWaitingForPartyLeader")` with B button
    `PartyLobbyWaitingForPartyLeaderLeaveParty`;
  - then `RequestReturnToPartySession()`, which asks the leader's party beacon to take this member back (RequestReturnToSession,
    LIVE beacon).
- **Retry:** on failure it retries **every 1.0 s, up to 30 tries** (`NumReturnToSessionTries < 30`), then **LeaveParty**.
- **On success:** `ClientTravelToMap(<leader's party session connect string>?UniqueId=…)`. The member connects to the leader's
  party lobby, EndState closes the popup, and its local lobby is replaced by the leader's.
- **"Leave party" (B):** `LeaveParty` → `ClientPartyDisbanded()` (local) → cancel the 'Party' reservation, disconnect, then
  `TransitionToPartyLeader` → WaitingForTasksToIdle → (no longer a party client) `CreatingPartySessions`. **The member stays
  in the party lobby screen as the leader of a new party of one.**
- **Quit prompt on the party lobby** (`GetQuitGameMessage`):
  - a party client gets `LeaveParty` / `LeavePartyConfirm` ("Quit Party" / "Are you sure you want to leave the party?");
  - the leader or a solo player gets `LeavePartyLobby` / `LeavePartyLobbyConfirm`.

**For our relay:**
- Show the popup while a member's local party lobby is trying to reattach to the leader's party (after a match, or on
  reconnect).
- Retry once a second for up to 30 s, then fall back to "own party".
- Close it on reattach.

## 3. "Party Too Large" and the per-mode party limit (C; answers R2)

- **`CheckCanPlayGameType(settings)`:**
  - `NumPlayersInParty = GetNumPlayers()` of the party lobby; `MaxNumber = settings.GetNumPlayersAllowedInParty()`;
  - if `NumPlayersInParty > MaxNumber`: popup title `PartyTooLargeToPlayGameTypeTitle`, message `PartyTooLargeToPlayGameTypeMessage`
    with `` `g `` = FriendlyName, `` `i `` = MaxNumber, `` `n `` = the party size; the start is refused.
  - It runs on both host and playlist starts.
- **`GetNumPlayersAllowedInParty`:**
  - `NumPrivateConnections > 0` → NumPrivateConnections;
  - else `NumPublicConnections / (AllowPartyToFillPublicGame ? 1 : 2)`.

| Settings class | Limit |
|---|---|
| any **Private** class (versus) | 10 |
| SVPrivate | 4 |
| CP / CCPPrivate | 3 |
| **Public** DM (AllowPartyToFillPublicGame true) | 10 |
| **Public** TDM / CTF / KOTH / DOM / EXT (false) | **5** (10 / 2: a party may fill only half a public game, one side) |
| SVPublic (true) | 4 |
| CP / CCP Public (true) | 3 |

## 4. R8: party leader loss (C)

**It disbands; there is no promotion.**

- **When:** the leader leaves (or quits) the 'Party' session. `OnlineDisconnector.DisconnectFromSessions` with 'Party' →
  state **`DisbandingParty`**: every remote PlayerController on the leader's machine gets **`ClientPartyDisbanded()`**, then
  there's a **1.0 s grace** timer before the leader disconnects.
- **Members drop too:** members also see the party host cancel through the reservation beacon (`OnHostHasCancelledReservation
  ('Party')`) → `TnPlayerControllerPartyLobby.ClientPartyDisbanded`.
- **A member in the party lobby:**
  - cancels the 'Party' reservation and disconnects;
  - `TransitionToPartyLeader` → becomes the **leader of its own new party** (CreatingPartySessions → Ready);
  - **no message box** is shown by the script (H: only a connection-lost error if the engine raises one).
- **A member in a game** (`TnPlayerController.ClientPartyDisbanded` = **`QuitToPartyLobby`**): when the disband RPC reaches
  it, it leaves the game for its own party lobby. That happens when the leader was the game host and quits the game to
  the menu; every connected client is a remote controller there. If the leader was only a client of someone else's game,
  the RPC can't reach the members (H), and the beacon cancel only destroys their 'Party' session (`OnHostHasCancelledReservation
  Internal`: a party client with a game reservation keeps the game). They stay in the game and come back to their own party
  lobby, no longer in a party.

**Our D12 "disband" matches the original.** The PC text ("The party has ended because the leader has left.") is an addition:
the original showed none.

## 5. R9: after a match (C)

- **Match end:**
  - `MatchOver` → `MatchOverCountdown` 15 s → **`ReturnToGameLobby`**;
  - **the whole game, party members included, returns to the game host's game lobby** (UI_Lobby_m), still in the same game
    session. The party isn't consulted.
- **Leaving the game lobby or game** (`QuitToPartyLobby`):
  - **On the game host** (listen server): `TellClientsToReturnToPartyHost` → **every** primary client gets
    `ClientReturnToParty()`, not only party members: each disconnects from the game and goes to **its own** party lobby.
    - A client that is a party leader `ServerTravel`s its party lobby (?listen).
    - Others `ClientTravel` to their own party lobby URL. There they're either party clients (→ "Waiting for party
      leader..." until reattached, §2) or solo leaders.
    - The host then disconnects and goes to its own party lobby.
  - **On a client:** it cancels its game reservation, disconnects alone and travels to its own party lobby. It keeps its
    party reservation (in game, `IsInPartyLobby` is false, so 'Party' isn't cancelled). If its leader is back in the party
    lobby it reattaches; otherwise it waits up to 30 s (§2).
- **Answer:** members stay in the game lobby after a match. They return to the party lobby when they, or the game host, leave
  that lobby, and they **stay in the party** (they reattach to the leader).
- **Our P6 matches.** Add the "Waiting for party leader..." reattach popup for the window before the leader's party lobby is
  back.

## 6. Friends List and recent players (C / LIVE)

- **The Friends List screen is the platform friends list only.** `UIDataProvider_OnlineFriends` fills `<OnlinePlayerData:Friends>`
  from `PlayerInterface.GetFriendsList` (Live / Demonware friends). Actions use the friend's UniqueId from that list; join
  requires `bIsJoinable || bHasInvitedYou` and `bIsPlayingThisGame`. **LIVE: not applicable.** Our relay has no friends
  service.
- **Recent players were collected but never shown by WFC's UI:**
  - `PlayerReplicationInfo.RegisterPlayerWithSession`: every **remote** player registered with a session (game or party lobby)
    is added to `OnlineRecentPlayersList.RecentPlayers`, a **ring buffer of MaxRecentPlayers 100** (unique by Uid; parties:
    MaxRecentParties 5);
  - the only display paths are `ShowRecentPlayerList` / `ShowRecentPartiesPlayerList` / `ShowLastPartyPlayerList`, which open
    the **Xbox guide's "custom players" UI** (`PlayerInterfaceEx.ShowCustomPlayersUI`, LIVE);
  - **no TransGame / TransContent script calls any of them**, and nothing feeds them into the Friends List.
- **For us:** a "Recent players" list (last 100 remote players met in sessions, unique) would be a PC addition, not an original
  screen. If wanted, populate it at the same point: when a remote player registers in a session.

## 7. Invites (C, for completeness)

- Accepting an invite (`OnGameInviteAccepted`: UI event 16) uses `JoinSessionUsingGameInvite`, always option 0 (reserve → join
  → travel). Its Disconnecting state leaves **both** 'Party' and 'Game' and destroys the reservation host, so **accepting an
  invite takes you out of your party** (and disbands it if you led it).
- Failures:
  - `NotifyInviteFailed` → JoinGameInviteFailed;
  - `NotifyNotEnoughSpaceInInvite` → no room for your party;
  - `NotifyNotAllPlayersCanJoinInvite`;
  - the not-logged-in variants.
- `RequestReservation` refuses an invite to a game you're already in or already joining (log only, no popup).
