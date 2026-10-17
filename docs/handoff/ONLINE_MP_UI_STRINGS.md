# Online multiplayer: every added row, text field and popup text, per movie (Frontend, 2026-10-17; design, no code)

Companion to ONLINE_MP_FLOWS.md (flows, decisions D1-D11) and ONLINE_MP_FRONTEND_TESTS.md (tests S1-S24).

Rule: reuse the original's own text wherever one exists, cited by its INT key as the movies cite them
($File.Section.Key, from ExtractedAssets/config/Coalesced_int/TransGame/Localization/INT). New text is marked NEW, gets
a key in a rebuild-owned table, and follows the original's style:
- popup titles in title case, messages as one or two sentences with a full stop;
- menu labels in title case, hints as one sentence;
- the original's backtick placeholders (`p, `k, ...) where a value is inserted.

Layout limits measured on the current frontend:
- hint lines: one line, at most ~58 characters (the Graphics menu rule);
- lateral-selector values: at most ~16-18 characters;
- MessagePrompt_GFX messages wrap (the original's longest, ClientOutdated, is 128 characters).

## 0. Where NEW texts live

Today the rebuild's own texts (Graphics hints, the Game Mode Options bot rows) are literals in the presenter. For online
play, proposal:
- one rebuild-owned table, `data/frontend/pc_strings.int`, in the original INT format, section per screen;
- loaded by Catalog under the file name `WfcPC`, so movies and code use `$WfcPC.Online.Key` exactly like
  `$UIText.Lobby.Key`;
- the existing PC literals can move there later (not required for online).

A French table (`pc_strings_FRA.int`) can follow the original's FRA files.

## 1. PartyLobby_GFX (party lobby menu)

| element | kind | key | text | note |
|---|---|---|---|---|
| Find Game | row (original) | as authored in the movie (the row label is set by PartyLobby_GFX itself; nearest INT keys $UIText.Lobby.PublicMatch / FindPublicGame) | as shipped | phase 1 keeps today's local playlist game; its original hint ($UIText.Lobby.PublicDescMultiplayer, "Join an open online game and earn XP...") promises online play the phase-1 build does not have. Proposal: unchanged until a session list exists (it is the original's text), noted in the playtest guide |
| Host Game | row (original) | as shipped; hint $UIText.Lobby.HostDesc | "Set up a custom game for others to join." | the original hint now describes exactly what happens online |
| **Join Game** | row (NEW position, original label) | label $UIText.OnlineFriends.JoinGame | "Join Game" | PC EXTENSION row under Host Game, same form as the original rows (mc_panelButton, as the Graphics menu's added rows) |
| Join Game hint | hint | NEW $WfcPC.Online.JoinGameDesc | "Join a friend's game with a code or an address." | 48 characters |

## 2. TextPrompt_GFX (join code / address entry)

| element | kind | key | text | note |
|---|---|---|---|---|
| title | title | $UIText.OnlineFriends.JoinGame | "Join Game" | original |
| prompt | text | NEW $WfcPC.Online.JoinPrompt | "Enter a join code or an address." | |
| example line | text | NEW $WfcPC.Online.JoinExample | "Code: WFC-XXXX-XXXX   Address: 192.168.1.20:7777" | the address example uses a LAN-style address, not a real public one |
| invalid entry | popup message (title $UIText.MessagePrompts.GenericErrorTitle "Error") | NEW $WfcPC.Online.JoinInvalid | "That is not a join code or an address." | shown before any connection attempt |
| buttons | hints | $UIText.ButtonHints.Accept / $UIText.ButtonHints.Back | "Select" / "Back" | original |

## 3. MessagePrompt_GFX (popups)

| moment (flow ref) | title key / text | message key / text | buttons | note |
|---|---|---|---|---|
| joining (3.3, S3) | $UIText.MessagePrompts.JoinFriendGameTitle "Joining..." | $UIText.MessagePrompts.JoiningMessage "Joining game..." | $UIText.MessagePrompts.GenericCancelButton "Cancel" | original texts |
| hosting (3.2, S1) | $UIText.MessagePrompts.HostGameTitle "Hosting Game" | $UIText.MessagePrompts.HostingMessage "Creating lobby..." | none | original |
| cannot host (D2, S2) | $UIText.MessagePrompts.StrictNATTitle "Unable to Host" | NEW $WfcPC.Online.HostNoPath "No connection could be opened for others to join." | OK | D2: original title, PC message |
| cannot host, NAT known strict | $UIText.MessagePrompts.StrictNATTitle | $UIText.MessagePrompts.StrictNATMessage "To allow hosting of a game, switch your NAT from strict to open or moderate." | OK | D2: the original wording where it still applies (the session layer reports a strict NAT) |
| join failed: full / gone / timeout (S4, S5) | $UIText.MessagePrompts.JoinGameInviteFailedTitle "Join Game Failed" | $UIText.MessagePrompts.JoinGameInviteFailedMessage "Could not join the game. The game is full or no longer available." | OK | original |
| join failed: version (S6) | $UIText.MessagePrompts.JoinGameInviteFailedTitle | $Engine.Errors.ClientOutdated "The match you are trying to join is running an incompatible version of the game.  Please try upgrading your game version." | OK | **original text** (replaces the NEW version text proposed in ONLINE_MP_FLOWS.md 3.3) |
| join failed: map missing (S7) | $UIText.MessagePrompts.LobbyInvalidMapTitle "Map Not Found" | $UIText.MessagePrompts.LobbyInvalidMapMessage "You do not have the selected map and will not be able to play in this match without it." | OK | original |
| host quit (D9, S17) | $Engine.Errors.ConnectionFailed_Title "Connection Error" | $Engine.Errors.ConnectionFailedHostQuit "The game has ended because the host has quit." | OK | original |
| connection lost (S18) | $Engine.Errors.ConnectionFailed_Title | $Engine.Errors.ConnectionFailed "Your connection to the host has been lost." | OK (phase 1b adds Rejoin) | original |
| connection timed out (no packets for 8 s) | $Engine.Errors.ConnectionFailed_Title | $Engine.Errors.ConnectionTimeout "Your connection to the host timed out." | OK (phase 1b adds Rejoin) | original; the session layer reports which of the two applies |
| Rejoin button (3.8, phase 1b) | - | - | NEW $WfcPC.Online.Rejoin "Rejoin" | the original two-button prompt form |
| out of sync (3.6, S19) | $Engine.Errors.ConnectionFailed_Title | NEW $WfcPC.Online.Desync "The game has gone out of sync and cannot continue." | OK | |
| idle kick (D5, S20) | $UIText.MessagePrompts.GenericErrorTitle "Error" | $Engine.Errors.KickedForIdling "Kicked for idling." | OK | original message; the title is the original generic one |
| removed by the host (other reasons, later) | $UIText.MessagePrompts.GenericErrorTitle | $Engine.AccessControl.KickedMsg "You have been forcibly removed from the game." | OK | original |
| leave the game lobby / match (member) | $UIText.MessagePrompts.LeaveLobby "Quit Game" | $UIText.MessagePrompts.LeaveLobbyConfirm "Are you sure you want to leave the game?" | $UIText.ButtonHints.Yes / No | original |
| leave as the host (ends the session) | $UIText.MessagePrompts.LeaveLobby | NEW $WfcPC.Online.HostLeaveConfirm "Leaving will end the game for everyone. Are you sure you want to leave?" | Yes / No | PC EXTENSION: the original host also ended the session but said nothing |
| leave a public match (phase 2+) | $UIText.MessagePrompts.LeaveLobby | $UIText.MessagePrompts.LeaveLobbyXpConfirm "All gained XP will be lost.  Do you want to proceed?" | Yes / No | original; private sessions give no XP (D8), so not shown there |
| quitting | - | $UIText.MessagePrompts.LeaveLobbyInProgressMessage "Quitting..." | none | original |

## 4. GameLobby_GFX (game lobby)

| element | kind | key | text | note |
|---|---|---|---|---|
| join code | text field (NEW, host only) | NEW $WfcPC.Online.JoinCode | "Join code: `c" | right column under the team lists; overlay text field in $NormalFont, the technique of the FPS counter |
| address | text field (NEW, host only) | NEW $WfcPC.Online.JoinAddress | "Address: `a" | |
| copy hint | text field (NEW, host only) | NEW $WfcPC.Online.CopyCode | "C: copy the join code" | keyboard; on a pad the prompt glyph system adds the button |
| privacy line | text field (NEW, host only) | NEW $WfcPC.Online.IpPrivacy | "Players who join can see your IP address." | netcode doc section 9 |
| copied | transient text (NEW) | NEW $WfcPC.Online.CodeCopied | "Join code copied." | 2 s, replaces the copy hint |
| Host Options row | row (NEW position, original label) | $UIText.Lobby.HostOptions | "Host Options" | host only; reopens Game Mode Options (flows 3.2) |
| Host Options hint | hint | NEW $WfcPC.Online.HostOptionsDesc | "Change the game mode settings and bots." | 39 characters |
| status: match in progress | <CurrentGame:LobbyStatus> | NEW $WfcPC.Online.MatchInProgressStatus | "Match in progress" | D6; the other statuses are the original TnGameReplicationInfoGameLobby texts |
| status: players / character / teams / host | <CurrentGame:LobbyStatus> | $TransGame.TnGameReplicationInfoGameLobby.WaitingForPlayersStatus / WaitingForOnePlayerStatus / WaitingForCharacterChoiceStatus / WaitingForBalancedTeamsStatus / WaitingForHostToStartStatus | original | D4 |
| connecting | connecting_mc | $UIText.Lobby.ConnectingToHost | "Connecting to Host" | original (shown in caps by the movie) |
| browser cap notice (D10) | hint line under Player Limit | NEW $WfcPC.Online.BrowserCap | "A browser player is in the game: 5 v 5 at most." | 47 characters; phase 2 |

## 5. PlayerList_GFX (lobby / scoreboard rows)

| element | kind | key | text | note |
|---|---|---|---|---|
| platform tag (D11) | appended to the name cell | NEW $WfcPC.Online.PlatformPC / PlatformWeb | "PC" / "Web" | small text after the name, e.g. "Name1  PC" |
| connecting | IsConnecting column | (icon, original) | - | original column and sort |
| bot rows | name cell | the bot names as today | - | PC ADAPTATION (the original hid bBot rows) |

## 6. LoadScreen_GFX (map loading)

| element | kind | key | text | note |
|---|---|---|---|---|
| load gate | status text | NEW $WfcPC.Online.WaitingForPlayers | "Waiting for `n players" | S14; `n = humans not yet loaded; one player: NEW $WfcPC.Online.WaitingForOnePlayer "Waiting for `n player" (the original keeps singular / plural pairs, cf. WaitingForOnePlayerStatus) |

## 7. Hud_GFX (match)

| element | kind | key | text | note |
|---|---|---|---|---|
| a player left | kill-feed line | $TransGame.TnGameMessage.LeftGameString | "`k has left the game" | original |
| host migrated (phase 1b) | game message line | NEW $WfcPC.Online.HostMigrated | "The host left. `k is now the host." | |
| resynchronising (phase 1b) | game message line | NEW $WfcPC.Online.Resync | "Resynchronising..." | |

## 8. PauseMenu_GFX / EndGameStats_GFX

- **PauseMenu_GFX:** Quit Match uses the original confirmation of section 3 (member or host variant). The menu never
  pauses the world. "Game Paused by Host" ($UIText.PauseMenu.PausedByHost) is never shown: the MP world does not pause.
- **EndGameStats_GFX:** unchanged. The return to the host's game lobby uses the existing flow; no text.

## 9. Browser (phase 2; outside GFx)

The data-folder step and the WebAudio "click to start" are a DOM page before the canvas, not INT texts. They get
their own table when the browser build starts. Proposed wording, NEW:
- "Select the folder with your extracted game data."
- "Click to start."

## 10. Count

| | reused original | NEW |
|---|---|---|
| PartyLobby_GFX | 3 | 1 |
| TextPrompt_GFX | 3 | 3 |
| MessagePrompt_GFX | 22 | 4 |
| GameLobby_GFX | 7 | 8 |
| PlayerList_GFX | 0 | 2 |
| LoadScreen_GFX | 0 | 2 |
| Hud_GFX | 1 | 2 |
| total | 36 | 22 |

No new movie; every NEW text has a key in `$WfcPC.Online`.
