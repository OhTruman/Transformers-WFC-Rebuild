# Online multiplayer: the first run, screen by screen (Frontend, 2026-10-17; for the hand-over)

What a player sees the first time they open Multiplayer in an online build, from the main menu to their first lobby.
Everything below is phase 1 code already on integration (ONLINE_MP_FLOWS.md, implemented on ISession / FakeSession and
captured in the B2 / re-slot shots), except the one-time prompt in step 2, which is marked **NEW** (design; code with the
transport). Texts are the original's (`$File.Section.Key`) unless they are `$WfcPC.*` (data/frontend/pc_strings.int).

"Online build" = a build with Systems' session layer. Until it exists, step 2 never shows and steps 3-5 run against the
dev FakeSession only; offline play is unchanged throughout.

---

## 1. Walkthrough

### Step 1 - Main menu -> Multiplayer
- The original Multiplayer button opens the party lobby (`$UIText.Lobby.PartyLobbyTitle` "Party Lobby"). No sign-in step:
  the original's Live / Demonware sign-in is replaced by the local identity (ONLINE_MP_FLOWS.md 3.1).
- The original's first-time online message boxes (CheckOnlineWarningPart0..2: profile not ready, CanCommunicate,
  RecommendPlayingCampaignFirst) are not shown: the first two are console / Live checks with nothing behind them on PC;
  the third waits for the campaign.

### Step 2 - The one-time online prompt (NEW)
Shown once per profile, the first time the party lobby opens in an online build, over the party lobby
(MessagePrompt_GFX, the original popup form):

| case | title | message | buttons |
|---|---|---|---|
| a signed-in account exists | NEW $WfcPC.FirstRun.Title "Online Play" | NEW $WfcPC.FirstRun.Message "Other players will see you as `p. Voice chat: `v." (`p = the account name, D1; `v = "Push to Talk (V)" / "Open Mic" / "Off" from the Audio rows) | A NEW $WfcPC.FirstRun.Continue "Continue"; X NEW $WfcPC.FirstRun.ChangeName "Change Name"; Y NEW $WfcPC.FirstRun.VoiceSettings "Voice Settings" |
| no account yet | the same title | NEW $WfcPC.FirstRun.ChooseName "Choose the name other players will see." | A "Change Name" (label NEW $WfcPC.FirstRun.ChooseNameButton "Choose Name"); B $UIText.ButtonHints.Back |

- **Change / Choose Name** opens the original account-name prompt: TextPrompt_GFX with the Accounts screen's own texts
  (`Account.CreateAccount`, 15 characters) and the original result boxes `$UIText.MessagePrompts.CreateAccountTitle` +
  `CreateAccountSucceededMessage` / `CreateAccountInvalidUsernameMessage`. A new account is signed in at once and is the
  online name (D1). Then the prompt returns with the new name.
- **Voice Settings** opens Settings > Audio with the voice rows (Voice Chat, Voice Channel, Input Device, Push-to-Talk Key
  incl. "Custom...", Voice Volume, Proximity Volume, Mic Test); Back returns to the party lobby.
- **Continue** (or Back once a name exists) records it: profile `[Online] FirstRunShown=1`, written only when set, so
  offline profiles stay byte-identical. The defaults stay as they are (Push to Talk on V, Radio + Proximity).
- No name, Back: the prompt closes and Multiplayer stays usable offline (Find Match / Private Match with bots); it shows
  again the next time the party lobby opens online.

### Step 3 - Host or join (the party lobby)
The party lobby menu, as captured (r_party_joinrow):

| row | hint | what it does |
|---|---|---|
| Find Match | original | phase 1: the local playlist game with bots (no directory yet) |
| Private Match | `$UIText.Lobby.HostDesc` "Set up a custom game for others to join." | host (step 4a) |
| **Join Game** | `$WfcPC.Online.JoinGameDesc` "Join a friend's game with a code or an address." | join (step 4b) |
| Create a Character / Teletran I / Friends List | original | unchanged |

### Step 4a - Host
1. Private Match -> the mode -> Game Mode Options (the mode's rows, Player Limit, the bot rows, Iconic Characters) ->
   Create Game.
2. `$UIText.MessagePrompts.HostGameTitle` "Hosting Game" / `HostingMessage` "Creating lobby..." (b2_hosting).
3. The game lobby with the join panel (b2_host_lobby): `$WfcPC.Online.JoinCode` "Join code: `c", `JoinAddress`
   "Address: `a", `CopyCode` "C: copy the join code" (-> `CodeCopied` "Join code copied."), `IpPrivacy` "Players who join can
   see your IP address."
4. The player sends the code to friends (any chat). As they join, rows appear with "PC" / "Web" tags; the status line
   follows the original gate (step 5).

### Step 4b - Join
1. Join Game -> TextPrompt_GFX (b2_join_prompt): title `$UIText.OnlineFriends.JoinGame` "Join Game", prompt
   `$WfcPC.Online.JoinPrompt` "Enter a join code or an address.", example `JoinExample`. Enter / Accept sends, Back cancels.
2. A malformed entry: `$UIText.MessagePrompts.GenericErrorTitle` "Error" / `$WfcPC.Online.JoinInvalid` "That is not a join
   code or an address." (before any connection).
3. `$UIText.MessagePrompts.JoinFriendGameTitle` / `JoiningMessage` "Joining game..." with Cancel (b2_joining).
4. Success: the host's game lobby as a member (r_member_lobby): no Start Game / Select Map, the host's map and mode, the
   status line from the host.

### Step 5 - The first lobby
- The original gate texts on the status line: `$TransGame.TnGameReplicationInfoGameLobby.WaitingForCharacterChoiceStatus`
  "Waiting for all players to choose a character" -> `WaitingForBalancedTeamsStatus` (Team Balancing Manual only) ->
  `WaitingForHostToStartStatus` "Waiting for host to start game" (D4).
- Voice: the player's own row shows the original voice icon (HeadsetState 1, or 2 while Push to Talk is held); speakers
  show the talking icon, proximity speakers the "Near" tag; the row's A is Mute / Unmute.
- Start -> the loading screen "Waiting for `n players" until everyone has loaded -> the match.

---

## 2. Every error a first-time player can meet (one table)

| moment | title | message | source |
|---|---|---|---|
| malformed code | $UIText.MessagePrompts.GenericErrorTitle | $WfcPC.Online.JoinInvalid | PC |
| cannot host | $UIText.MessagePrompts.StrictNATTitle "Unable to Host" | $WfcPC.Online.HostNoPath, or the original StrictNATMessage when the NAT is known strict | D2 |
| game full / gone / no answer in 50 s | $UIText.MessagePrompts.JoinGameInviteFailedTitle "Join Game Failed" | JoinGameInviteFailedMessage | original |
| another version | JoinGameInviteFailedTitle | $Engine.Errors.ClientOutdated | original |
| map missing | $UIText.MessagePrompts.LobbyInvalidMapTitle "Map Not Found" | LobbyInvalidMapMessage | original |
| host quit | $Engine.Errors.ConnectionFailed_Title "Connection Error" | $Engine.Errors.ConnectionFailedHostQuit | original |
| connection lost / timed out | ConnectionFailed_Title | ConnectionFailed / ConnectionTimeout | original |
| out of sync | ConnectionFailed_Title | $WfcPC.Online.Desync | PC |
| idle 180 s (public sessions only, D5; not in phase 1) | GenericErrorTitle | $Engine.Errors.KickedForIdling | original |
| account name refused (step 2) | CreateAccountTitle | CreateAccountInvalidUsernameMessage | original |

All of them return to the party lobby (the game lobby for out of sync); none loses the profile or offline progress.

---

## 3. NEW for the first run (to build with the transport)

- The prompt in step 2: GameFlow `firstRunPending()` (online build, party lobby opened, `[Online] FirstRunShown` not set),
  the popup with the X / Y buttons (the original MessagePrompt form takes four buttons), the hand-off to the Accounts prompt
  and to Settings > Audio, and the return.
- Strings ($WfcPC.FirstRun): Title, Message, Continue, ChangeName, ChooseName, ChooseNameButton, VoiceSettings (7 NEW).
- Profile: `[Online] FirstRunShown=1`, written only when set.
- Tests (FakeSession): FR1 the prompt once per profile and never offline; FR2 Change Name -> the account prompt -> the
  prompt again with the new name; FR3 Voice Settings -> the Audio page -> back to the party lobby; FR4 no name + Back ->
  shows again next time, Multiplayer usable offline.
- Capture: one shot each of the two prompt cases, in the gap after the transport lands.
