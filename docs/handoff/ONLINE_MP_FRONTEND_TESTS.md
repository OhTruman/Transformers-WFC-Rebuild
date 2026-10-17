# Online multiplayer: the frontend's session tests without a network (Frontend, 2026-10-17; design, no code)

Turns ONLINE_MP_FLOWS.md section 5 item 16 into a runnable design. The frontend flows of online play (host, join,
member lobby, load gate, leave, host loss, desync, return paths) are tested headless before any transport exists, by a
**fake session** that plays the session layer's part. The same boundary later takes Systems' real session layer
(ONLINE_MP_ARCHITECTURE.md). Names here are proposals; mismatches with Systems' model are listed after their doc lands.

## 1. The boundary: ISession

GameFlow talks to the session layer only through one interface, owned by the frontend:
- calls go out;
- events come back in order, polled once per frontend update.

GameFlow never blocks on it. Every timeout the frontend shows (InitialConnectTimeout 50 s, ConnectionTimeout 8 s, the
load gate) counts **frontend update time**, not the wall clock, so a test advances it by updating with a fixed dt.

Calls (frontend -> session):

| call | when |
|---|---|
| host(settings) | Create Game in Game Mode Options, online (the mode, options, Player Limit, bot counts, map method) |
| join(target) | Join Game: a join code or an address string, as typed |
| cancelJoin() | Escape / Back while joining |
| leave() | Quit Game / Quit Match / Back out of the game lobby |
| publishLobby(state) | host only: whenever LobbyState changes (mode, options, map, teams, participant table, countdown) |
| setMemberInfo(info) | every member: display name, character, specialty, loadout, team request (Change Team), platform |
| requestStart() | host only: Start Game (BeginLobbyExitCountdown) |
| reportLoaded(simId, loadHash) | after the map load, before the match countdown |
| kick(member, reason) | host only: the idle kick (D5) |

Events (session -> frontend), each carrying the session time it happened:

| event | payload | frontend reaction (ONLINE_MP_FLOWS.md) |
|---|---|---|
| HostOpened | join code, address | game lobby as Host; the join panel shows both (3.2) |
| HostFailed | reason | "Unable to Host" popup (D2), stay in Game Mode Options |
| JoinProgress | Connecting | "Joining game..." -> the lobby's connecting_mc (3.3) |
| Joined | the host's LobbyState | game lobby as Member (3.3) |
| JoinFailed | Full / Timeout / Version / MapMissing | the matching popup, back to the party lobby (3.3) |
| MatchInProgress | the host's LobbyState + "running" | game lobby as Member, status "Match in progress" (D6) |
| LobbyState | LobbyState | members: replace the lobby state (data stores answer from it) |
| MemberJoined / MemberLeft | id, name, platform / id, reason | participant table: IsConnecting, then a human row; on leave the slot returns to a bot |
| CountdownStarted | start time | the lobby exit countdown on every member |
| PeerLoaded | id | load gate count |
| AllLoaded | start tick | the match countdown (MatchAutoStartCountdown 10 s, in sim ticks) |
| PeerDropped | id, reason | in a match: "`k has left the game" + the bot swap request to Gameplay |
| HostLost | - | end of session (D9): party lobby + "The game has ended because the host has quit." |
| ConnectionLost | - | member side: party lobby + "Connection Error" / "Your connection to the host has been lost." |
| Desync | tick | end of match: game lobby + the out-of-sync popup (3.6) |
| Kicked | Idle | party lobby + "Kicked for idling." |

## 2. FakeSession

A test-only class implementing ISession, built into wfc_frontend_tests (no sockets, no threads):
- **Script:** a queue of (at frame N or at "after call X", event) entries. A test writes the session side of the
  conversation in order.
- **Recorder:** every call GameFlow makes, with its arguments and the frame, so a test can assert "the flow called
  publishLobby with team 1 holding two humans" or "requestStart was not called by a member".
- **Clock:** the frame counter of the test's runUntil loop. "Nothing arrives for 50 s" is 3000 updates of 1/60 s.

Construction: `FrontendRuntime rt; rt.flow().setSession(std::make_unique<FakeSession>(script))`. A runtime with no
session set keeps today's offline flow, so every existing test keeps passing unchanged.

## 3. Flows and assertions

Each flow is one test function in tests/FrontendTests.cpp, next to testFlow. All of them start from the party lobby,
reached as testFlow does today (WFC_FRONTEND_AUTOPLAY off, the script steps `call:Online.OpenPartyLobby,GTS_TeamGame;
wait:level=PartyLobby`).

The assertions read only what the player sees or what the flow traces:
- `flow.level()`, `flow.ui().state()`, `flow.openMovies()`;
- `flow.popup()` (title, message, buttons);
- DataStores reads (`<CurrentGame:LobbyStatus>`, `<CurrentGame:CanHostRequestGameStart>`, `<CurrentGame:Players>` rows,
  `<CurrentGame:NumOnlinePlayers>`, `<TnGameSettings:*>`);
- FlowTrace lines;
- the FakeSession recorder.

The expected texts are the INT strings (ONLINE_MP_UI_STRINGS.md); a test compares against the catalog lookup, never a
copied literal.

| id | flow | script (session side) | assertions |
|---|---|---|---|
| S1 | host succeeds | HostOpened(code, addr) one frame after host() | level GameLobby, role Host; IsHost true; CanHostRequestGameStart true; join panel texts contain the code and the address; publishLobby recorded once with the Game Mode Options values |
| S2 | host fails | HostFailed(NoPath) | popup "Unable to Host" + the PC text (D2); level stays PartyLobby; Game Mode Options still open; no publishLobby |
| S3 | join succeeds | JoinProgress, then Joined(lobby: TDM, map 508, 1 host + 4 bots) | popup "Joining game..." while connecting; then level GameLobby, role Member; IsHost false; CanHostRequestGameStart false; LobbyStatus = "Waiting for host to start game"; Players rows = host + 4 bots + me, with my row IsConnecting until the next LobbyState |
| S4 | join: full | JoinFailed(Full) | popup "Join Game Failed" / "Could not join the game. The game is full or no longer available."; level PartyLobby; UI event 17 traced |
| S5 | join: timeout | no event for 3000 frames | same popup as S4 after exactly 50 s of update time (not earlier, frame 2999 still connecting); cancelJoin recorded |
| S6 | join: version | JoinFailed(Version) | popup "Join Game Failed" + the version text; level PartyLobby |
| S7 | join: map missing | JoinFailed(MapMissing) | the original "Map Not Found" popup and text |
| S8 | join during a match | MatchInProgress(lobby) | level GameLobby, role Member; LobbyStatus "Match in progress"; Start Game greyed; when the next LobbyState says "lobby", the status returns to the normal texts (D6) |
| S9 | member cannot drive the lobby | (S3 state) the test calls Online.SetSelectedMapID, Online.BeginLobbyExitCountdown and writes <TnGameSettings:TimeLimit> as the member | none of them changes LobbyState or reaches the session (no requestStart / publishLobby recorded); Change Team -> setMemberInfo with the team request only |
| S10 | the lobby gate | host lobby; MemberJoined(A) without a character, then A's LobbyState with a character; Team Balancing on with 3 v 1 humans | LobbyStatus: "Waiting for all players to choose a character" -> "Waiting for balanced teams" -> "Waiting for host to start game"; CanHostRequestGameStart false until the last (D4) |
| S11 | humans take bot slots | host lobby with 4 bots a team; MemberJoined x2 (team 0, team 1) | Players rows: each team has one bot fewer; bot counts in Game Mode Options unchanged (maxima); publishLobby recorded with the new table |
| S12 | browser cap | host lobby, Player Limit Extended; MemberJoined(platform Web) | Player Limit becomes Original (5 v 5) with the counts clamped; the trace says why; MemberLeft(Web) does not raise it back by itself (the host decides) (D10) |
| S13 | platform tag | S11 with one PC and one Web member | Players rows carry "PC" / "Web" (D11) |
| S14 | start + load gate | host: requestStart; CountdownStarted; after the map load, PeerLoaded for 1 of 2 members, then AllLoaded | loading screen status "Waiting for players (1)" until AllLoaded; reportLoaded recorded once with the flow's simId; the match countdown starts only after AllLoaded |
| S15 | load gate timeout | as S14, the second member never loads | after 50 s of update time PeerDropped(timeout); the match starts; the dropped member's slot is a bot in the participant table sent to Gameplay |
| S16 | peer leaves in a match | in Match: PeerDropped(A, Left) | kill-feed request "`k has left the game" with A's name; a bot-swap request for A's slot (to Gameplay; the frontend asserts the request, Gameplay's harness the swap itself) |
| S17 | host lost, every UI state | HostLost injected in turn in: game lobby, loading, InGame, Spectating, Paused, PausedSpectating, GameEnded | each time: level PartyLobby, UI InLobby under the PartyLobby controller, no HUD, no pause / respawn movie left open, popup "The game has ended because the host has quit."; leave() not recorded (the session is already gone) |
| S18 | connection lost (member) | ConnectionLost in InGame | as S17 with "Connection Error" / "Your connection to the host has been lost." |
| S19 | desync | Desync(tick) in InGame | level GameLobby (the host's), the out-of-sync popup; no XP written (private, D8) |
| S20 | idle kick | Kicked(Idle) in InGame | level PartyLobby, popup "Kicked for idling." |
| S21 | match end | the match ends (scripted EndGame); then LobbyState("lobby") | members: EndGameStats, then the host's game lobby with their characters and teams kept; profile XP unchanged in a private session (D8) |
| S22 | member quits | in Match: Quit Match -> confirm | leave() recorded once; level PartyLobby; no popup |
| S23 | display name | profile account "Name1" | setMemberInfo recorded with display name "Name1" (D1); <PlayerOwner:PlayerName> = "Name1" |
| S24 | offline unchanged | no session set | today's testFlow assertions, unchanged (the RE 3.1 URL, bots, map) |

## 4. The match side, and how it fits WFC_EVENTTEST

The frontend tests stop at the requests the flow sends to Gameplay (S15, S16). What the world does with them belongs in
an Application-level harness, run the same way as WFC_EVENTTEST (Application.cpp runEventTest: direct boot, a scripted
TDM match, check() lines "EVENTTEST PASS / FAIL", a summary).

Proposed: **WFC_SESSIONTEST** (Gameplay's to write, Frontend's to review; added to the direct-boot list in
Application_Frontend.cpp next to WFC_EVENTTEST). It uses the record that already exists for this:
- GameplayEvents.h has `ParticipantKind { Local, Remote, Bot }`;
- a fake remote human is a participant of kind Remote, fed scripted commands (no network).

Checks:
1. A Remote participant spawns, scores and dies through the same records as a bot. Kill / Assist / Spawn events carry
   kind Remote, and kills on it score (the original's TnPlayerController-victim rule treats it as a player).
2. The swap: at an agreed tick the Remote participant becomes a Bot in the same slot.
   - Its score row, team and match player index stay.
   - The event serials stay unique and increasing across the swap.
   - The kill feed gets the "has left the game" line once.
3. The reverse (phase 1b, late join): a Bot slot becomes Remote at an agreed tick, with the same continuity checks.
4. Idle: a Remote participant with no commands for 180 s of sim time is kicked by the host rule; its slot becomes a
   Bot.

The counting style is EVENTTEST's: one record per occurrence, counts equal to the participant stats.

## 5. Full-exe runs (captures)

For screenshots of the new popups and lobby texts without a network, the frontend script runner
(WFC_FRONTEND_SCRIPT) gets one step: `session:<event>[,args]` (e.g. `session:HostOpened,WFC-7QK2-M9TD,203.0.113.5:7777`,
`session:HostLost`). It injects into a FakeSession that the exe creates when WFC_FAKESESSION=1, a DEV TOOL that is never
compiled into a package (the WFC_DEV_TOOLS gate). Capture runs follow the lane rules: windowed, WFC_MUTE=1,
WFC_NOINTERACT=1.

## 6. Order of work (when code starts)

1. ISession + FakeSession + S24: offline unchanged, the boundary in place.
2. S1-S9: host / join / member.
3. S10-S13: lobby gate, slots, browser cap, tags.
4. S14-S16 + WFC_SESSIONTEST 1-2: start, load gate, leave.
5. S17-S23: session end and return paths.
6. The real session layer (Systems) replaces FakeSession behind the same interface; S1-S24 keep running against the
   fake.

## 7. Phase 2: parties, invites, browser (ONLINE_MP_PHASE2_FLOWS.md, D12-D15)

Same rig (OnlineRig + FakeSession, the party session scripted like the game session); expected texts from the catalog.

| id | flow | script (session side) | assertions |
|---|---|---|---|
| P1 | make a party | Invite to Party -> PartyOpened(code) | the code panel titled "Party" with the code; Party Members lists the leader |
| P2 | join a party | Join Game prompt with a party code -> joinParty; PartyMemberJoined for the others | level PartyLobby, party role Member; the member list shows every member with platform tags |
| P3 | member waits | (P2 state) | popup "Waiting for party leader..." with "Leave party"; mode / Private Match rows not actionable; IsHost false |
| P4 | follow | leader hosts -> PartyFollow(code) to members | each member runs the phase 1 join with that code; one scripted JoinFailed leaves that member in the party lobby with the phase 1 failure popup, the others in the game lobby |
| P4b | party too large | party of 11, team mode | "Party Too Large" with `g / `i / `n filled; no host() recorded |
| P5 | leave | Leave party -> confirm | leaveParty() recorded; back to the offline party lobby; no popup |
| P5b | leader loss (D12) | PartyLeaderChanged(B, code) after the leader drops; a browser member older than B is skipped | B's lobby actionable, the others keep the waiting popup; the line "`k is now the party leader." once; with a direct code the panel shows the new code; the last member leaving closes the party |
| P6 | after the match | the game ends; members leave the game lobby | each member returns to the party lobby, still in the party |
| I1 | recent players (D15) | a lobby with A and B, then a new profile | Friends rows A, B newest first with "Last played"; at most 50 kept; [RecentPlayers] written only when not empty; the fresh profile shows "No friends online" |
| I2 | invite (D13) | SendGameInvite(A) with and without the mailbox | always the code on the clipboard + "Invite code copied..."; with the mailbox also sendInvite(A, code) and "Game invite sent to A"; a second invite asks "already invited" |
| I2b | invite received (mailbox) | InviteReceived(A, code) | row "Sent you a game invite!", popup "A wants to play"; accept -> the phase 1 join with that code; JoinFailed -> "Join Game Failed" |
| B2 | browser joins (D14) | a relay room code; JoinFailed(Version) for a code without a room | the phase 1 join flow; the "does not accept browser players" text |
| B3 | browser cannot host | Create Game in the browser build | "Unable to Host" + "Hosting needs the PC version."; no host() / hostParty() as leader |
| B6 | tab close | PeerDropped(Web member) in a match | the phase 1 S16 line and bot swap |
