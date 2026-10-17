# Online multiplayer phase 2: parties, invites, PC-browser (Frontend, 2026-10-17; design only, no code)

Companion to ONLINE_MP_FLOWS.md (phase 1: PC to PC, implemented on ISession / FakeSession). This document expands its
stubs 3.10 (parties and invites) and 3.11 (PC to browser) into flows, screens, interface additions, tests and decisions.
Conventions as in phase 1: the original's rules are the starting point; every flow states where it simplifies and why.
Texts are the original's (`$File.Section.Key`, UIText / TransGame / Engine) unless marked **[PC text]**, which go in
data/frontend/pc_strings.int.

**Phase mapping.** "Phase 2" here is the frontend's second flow set. In ONLINE_MP_ARCHITECTURE.md section 11 it spans:
- parties and invites over direct codes: architecture phase 2 (LAN) and phase 3 (internet + relay);
- the browser peer: architecture phase 4 (WebRtcTransport).
Nothing here needs a service with accounts. A relay-hosted mailbox for invites is an option (decision D13).

Sources: CONFIRMED AS = the decompiled movies in RE-Workspace/work/q/gfx (PartyLobby_GFX, GameLobby_GFX, PauseMenu_GFX,
OnlineFriendsMenu_GFX); texts = ExtractedAssets config Coalesced_int (UIText.int, TransGame.int, Engine.int).

---

## 1. The original's parties and invites (what is known, with its source)

### 1.1 The party
- The party lobby (UI_PartyLobby_m, PartyLobby_GFX) is where players gather before a game. Its header is
  `$UIText.Lobby.PartyLobbyTitle` ("Party Lobby"); its member list is `$UIText.Lobby.PartyMembersHeader` ("Party Members")
  [CONFIRMED AS + texts].
- One member leads. The leader picks the mode and hosts or joins a game; the others follow into it. Non-leaders see
  `$UIText.MessagePrompts.PartyLobbyWaitingForPartyLeader` ("Waiting for party leader...") with one button,
  `PartyLobbyWaitingForPartyLeaderLeaveParty` ("Leave party") [texts; the beacon "tell clients to join" is the phase 1
  doc's 3.10 source].
- Leaving: `LeaveParty` / `LeavePartyConfirm` ("Quit Party" / "Are you sure you want to leave the party?"); quitting the
  lobby itself: `LeavePartyLobby` / `LeavePartyLobbyConfirm` [texts].
- Size: `PartyTooLargeToPlayGameTypeTitle` / `...Message` ("Party Too Large" / "`g only supports a party size of `i. You
  have `n players in your party.") [texts]. The numbers behind `i per mode are unknown (phase 1 doc R2).
- Joining a friend's game from a party disbands it: `JoinFriendGameAndDisbandMessage` ("Joining this game will disband
  your party. Continue?") [texts].
- Errors: `$UIText.Errors.PartyLobbyError` ("An unrecoverable error occurred in the party lobby. Please try again
  later."); signed-out variants `MustBeLoggedInParty` / `MustBeLoggedInInvite` / `MustBeLoggedInOF` [texts].
- Console party chat (`Online.ShowPartyChatSessionsUI`, `Online.SendGameInviteToPartyChatMembers`, `IsInPartyChatSession`)
  is the Xbox Live / PSN system party [CONFIRMED AS calls]. PC: none (we already answer IsInPartyChatSession false).

### 1.2 Friends and invites
- The Friends List (`$UIText.Lobby.InviteFriends` "Friends List", `InviteFriendsDesc` "See online friends and manage game
  invites.") opens OnlineFriendsMenu_GFX from the party lobby, the game lobby and the pause menu [CONFIRMED AS].
- Its data [CONFIRMED AS]:
  - `<OnlinePlayerData:Friends>` rows: `NickName`, `PresenceInfo`, `bHaveInvited`;
  - `<OnlinePlayerData:FriendMessages>` rows: `SendingPlayerNick`, `Message`, `bIsFriendInvite`, `bWasAccepted`.
- Its actions (by NickName) [CONFIRMED AS]: `Online.SendGameInvite`, `Online.JoinFriendGame`,
  `Online.RespondToFriendInvite(nick, accept)`, `Online.ClearGameInvite`, `Online.SendFriendInvite`, `Online.RemoveFriend`.
- Texts: "Send Invite", "Invite Party Members", "Sent you a game invite!" (`OnlineFriends.Invited`), "No friends online",
  "Loading friends", Accept / Deny friend request; prompts `GameInviteSentMessage` ("Game invite sent to `p"),
  `GameInvitePreviouslySent` ("You have already invited `p to this game. Continue?"), `GameInviteReceivedMessage`
  ("`p wants to play"), `FriendInviteReceivedMessage` ("Friend invite from `p") [texts].
- A failed invite join: `JoinGameInviteFailedTitle` / `JoinGameInviteFailedMessage` ("Join Game Failed" / "Could not join
  the game. The game is full or no longer available."); service down: `...NotLoggedIntoOnlineServiceMessage` [texts].
- The system invite title / body: `TnOnlineActionScriptBinding.GameInviteTitle` / `GameInviteMessage`
  ("Transformers: WFC" / "Join my game") [texts]. UI events 16 GameInviteAccepted / 17 JoinInviteFailed (phase 1 doc).
- Privacy row: `$UIText.Lobby.PrivacySettingsInviteOnly` ("Invite Only") [texts].

### 1.3 Unknown (open questions; not guessed)
- **R8** Party leader loss: promotion to another member, or disband? (Beacon code; no text names it.)
- **R9** Whether party members returned to the party lobby with the leader after a match, or stayed in the game lobby.
- **R2** (phase 1) per-mode party size limits.

---

## 2. Constraints (what we have)

- No accounts, presence or friends service; the account name is the display name (D1). Join codes or `host:port` find
  a game (architecture section 6). NAT: port-forward / UPnP, else the relay (phase 3).
- The session layer is ISession (phase 1). A party is a second, small session of the same kind (section 5).
- The browser cannot open a UDP port and needs WebRTC with a signalling exchange (architecture section 4, feasibility
  section 3). It runs Off / Faithful at 5 v 5, no HD, no RT.

---

## 3. Flows

### 3.1 P1 - Make a party (leader)
1. Main menu -> Multiplayer -> party lobby, as today.
2. The party lobby gets one row under Join Game: **Invite to Party** **[PC text]**. It opens the party session (ISession
   `hostParty()`) and shows the same code panel the game lobby host shows (code, address, "C: copy the join code", the
   privacy line). The panel is the phase 1 join panel, retitled `$UIText.Lobby.PartySessions` ("Party").
3. Members appear in the party lobby's member list (the original's `Party Members` header), with their platform tag
   (D11) and HeadsetState (party voice: section 3.8).

**Simplified vs the original:** the original's party existed as soon as a friend accepted an invite through the Live /
PSN overlay. Ours opens when the leader asks for a code. Why: no overlay, no presence.

### 3.2 P2 - Join a party
- The Join Game prompt (phase 1) takes party codes too: a party code ("WFC-P-...") joins the party lobby instead of a
  game lobby. The joining popup and its failures are phase 1's (`JoinFriendGameTitle`, Full / Timeout / Version / Invalid).
- A member who is in a party and joins someone else's game by code gets `JoinFriendGameAndDisbandMessage` first (Yes:
  leave the party, then join).

**Simplified:** none beyond the code. Why: as P1.

### 3.3 P3 - Party member waits
- The party lobby on a member shows the leader's mode choice read-only (as the phase 1 member's game lobby does) and the
  popup `PartyLobbyWaitingForPartyLeader` with the button `PartyLobbyWaitingForPartyLeaderLeaveParty` while the leader is
  choosing. No mode / Private Match rows are actionable (`Online.IsHost` false for party members: same rule as phase 1).

### 3.4 P4 - The leader starts: the party follows
1. The leader hosts (Private Match -> Create Game) or joins a game by code.
2. Before that, the size check: party size > the mode's limit -> `PartyTooLargeToPlayGameTypeTitle` / `...Message`
   (`g = the mode's friendly name, `i = the limit, `n = the party size). Limits until R2: the mode's human capacity
   (5 v 5 team modes: 10; FFA 10; while a browser member is present D10 applies).
3. The party session sends `PartyFollow{code}` to every member; each member runs the phase 1 join with that code
   (the same popup, failures and texts). A member whose follow fails stays in the party lobby with the phase 1 failure
   popup and can rejoin by code.
4. The leader's game lobby treats the party members as ordinary members (phase 1 flows). The leader is the game host
   when it hosted, otherwise an ordinary member.

**Simplified:** the original's follow went through the beacon; ours is a session event. Same visible flow.

### 3.5 P5 - Leaving and leader loss
- Leave: `LeaveParty` / `LeavePartyConfirm`; the member returns to its own (offline) party lobby.
- Leader quits or drops: **disband** (until R8), with `$Engine.Errors.ConnectionFailedHostQuit` reworded for a party
  **[PC text]** ("The party has ended because the leader has left."). Members keep their current game if one is running.

**Simplified:** disband instead of promotion (if R8 says the original promoted). Why: promotion needs a second leader
election in the session layer; the visible cost is one re-invite.

### 3.6 P6 - After the match
- The game ends as in phase 1 (back to the game host's lobby). Leaving that lobby returns each party member to the party
  lobby, still in the party (until R9).

### 3.7 I1-I3 - Friends and invites without a service
- **I1 Friends List (OnlineFriendsMenu_GFX)** stays the original movie. `<OnlinePlayerData:Friends>` is filled with
  **recent players** **[PC adaptation]**: everyone met in an online lobby, newest first, kept in the profile
  (`[RecentPlayers]`, display name + fingerprint + last seen), `PresenceInfo` = "Last played <date>" **[PC text]**.
  `NickName` = the display name.
- **I2 Send an invite** (`Online.SendGameInvite(nick)`): without a mailbox (D13), the invite is the current game or party
  code copied to the clipboard and the prompt `GameInviteSentMessage` reworded **[PC text]**: "Invite code copied. Send
  it to `p." `GameInvitePreviouslySent` keeps its rule per lobby.
- **With a mailbox (D13 option b):** the relay keeps a per-fingerprint inbox; `SendGameInvite` posts the code, the
  receiver's FriendMessages row shows `OnlineFriends.Invited` ("Sent you a game invite!"), `GameInviteReceivedMessage`
  pops ("`p wants to play"), accepting runs `Online.JoinFriendGame` -> phase 1 join; failure -> `JoinGameInviteFailedTitle`
  / `JoinGameInviteFailedMessage`; mailbox unreachable -> `...NotLoggedIntoOnlineServiceMessage`.
- **I3** Friend requests / RemoveFriend: recent players can be pinned ("Add Friend" -> `AddFriendTitle` /
  `AddFriendMessage`) and removed locally; nothing is sent to the other player.

**Simplified:** recent players instead of a friends graph; invites by code (or mailbox). Why: no accounts or presence;
the movie, its rows and texts stay the original's.

### 3.8 Party voice
- While in a party lobby, the party session carries voice like a game lobby (phase 1 3.12: push-to-talk default, one
  mute switch, HeadsetState / "Near" not applicable outside a match). In the game the game session's voice takes over.

### 3.9 B1-B6 - PC to browser
- **B1 First run (browser):** before the main menu: "Select your game data folder" and "Click to start" **[PC texts;
  web]**; then the main menu as on PC. Settings: browser-only rows (no HD, no RT; Off / Faithful).
- **B2 Browser joins a PC host:** the Join Game prompt as on PC. A browser cannot reach `host:port`; a code must lead to
  a signalling point (D14): (a) the relay doubles as the WebRTC signalling server (recommended), or (b) copy-paste of
  offer / answer (two codes each way). The joining popup, failures and texts are phase 1's; a host without WebRTC
  support -> `JoinFailed{Version}` text **[PC text]** "This game does not accept browser players."
- **B3 Browser hosting:** not offered; Private Match -> Create Game shows D2's "Unable to Host" with a web text
  **[PC text]** "Hosting needs the PC version." Parties: a browser can join a party but not lead one (same reason).
- **B4 Mixed lobbies:** D10 (5 v 5 cap) and D11 (platform tags) are implemented in phase 1 code; the host's Player
  Limit row shows the cap's reason in its hint **[PC text]**.
- **B5 Voice in the browser:** the microphone permission prompt appears the first time voice is used (getUserMedia);
  refusal -> HeadsetState 0 and the Voice Chat row reads Off. Push-to-talk works under Pointer Lock; the key list
  excludes browser-reserved keys.
- **B6 Closing the tab** = a dropped peer (phase 1 PeerDropped): the original "has left the game" line and a bot swap.

**Simplified vs the original:** not applicable (no browser version); the flows reuse phase 1 unchanged.

---

## 4. Screens: exist vs must be made

| screen / element | status |
|---|---|
| party lobby, member list ("Party Members") | exists (PartyLobby_GFX); rows filled from the party session |
| "Invite to Party" row + code panel | new (duplicated row + the phase 1 panel) |
| waiting-for-leader popup | exists (MessagePrompt texts) |
| party too large / disband / leave prompts | exist (texts); one [PC text] for leader loss |
| Friends List | exists (OnlineFriendsMenu_GFX); data = recent players |
| invite received popup (mailbox) | exists (texts) |
| browser data / click-to-start pages | new (web shell, outside the movies) |

---

## 5. ISession additions (proposed, for Systems)

- `hostParty()` / `joinParty(code)` / `leaveParty()`: a party session beside the game session (one each at most).
- Events: `PartyOpened{code}`, `PartyMemberJoined/Left{member, info}`, `PartyFollow{code}` (leader -> members),
  `PartyDisbanded{why}`, `InviteReceived{from, code}` (mailbox only).
- `SessionMemberInfo` unchanged (it already carries platform, level, voice through MemberVoice, iconicCharacter).
- `sendInvite(fingerprint, code)` (mailbox only); the relay's mailbox API is Systems' (phase 3).
- WebRTC: `join(code)` accepts signalling codes; nothing else changes on the frontend side.

## 6. Tests (headless, FakeSession, as phase 1)

P1 party code shown; P2 join a party by code; P3 member waits (popup, rows read-only); P4 follow (all members join the
leader's game; one failing member keeps the party); P4b party too large (texts with `g `i `n); P5 leave / leader loss
(disband text); P6 back to the party lobby; I1 recent players rows; I2 invite = code copied (+ mailbox variant);
B2 browser join through a signalling code; B3 browser cannot host (text); B6 tab close = PeerDropped.

## 7. Decisions (D) and open RE questions (R)

- **D12** Party leader loss: disband (proposed, until R8) or promote.
- **D13** Invites: (a) code copied, nothing sent (proposed for phase 2 LAN); (b) a relay mailbox per fingerprint
  (phase 3, with the relay).
- **D14** Browser signalling: (a) the relay as WebRTC signalling server (recommended); (b) copy-paste offer / answer.
- **D15** Friends List content: recent players (proposed) vs an empty list with only "join by code".
- **R8** Party leader loss in the original (promotion or disband).
- **R9** Where party members went after a match (party lobby or game lobby).
- **R2** (phase 1) per-mode party size limits, used by P4's check.
