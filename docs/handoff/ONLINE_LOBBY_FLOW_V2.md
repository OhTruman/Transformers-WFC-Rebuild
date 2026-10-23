# Online lobby flow v2: straight into the lobby, one Game Settings entry (Frontend, 2026-10-17)

User decision 15:40 (via Integration): hosting or joining takes the player straight into the lobby menu. There the player
creates a class and sees the member list and voice. The host also changes the map, opens ONE "Game Settings" entry holding
everything the old pre-match screens had, and starts the match. Joiners get Create a Character only, never the settings.
Two ways to a match: private (copy / enter a game code) and public (find a public match: a lobby list and Quick Match
through Systems' central lobby service). "Do it however you want to make that work and flesh it out."

This replaces the v1 host path in ONLINE_MP_FLOWS.md 3.2 (Private Match -> mode list -> Game Mode Options -> Create Game).
Everything else in v1 stays: the join prompt, the join panel, the start gate, the load gate, the leave / end texts, voice,
the launch record. Texts: the original's where one exists, **[PC text]** otherwise.

---

## 1. Screens and who sees what

### 1.1 Party lobby (Multiplayer) - the entry
| row | label (original text) | what it does now |
|---|---|---|
| Find Match | `$UIText.Lobby.FindMatch` | public matches (section 4): Quick Match into a voting lobby |
| Private Match | `$UIText.Lobby.HostPrivateMatch` | **hosts at once**: straight into the game lobby with the last-used Game Settings (2) |
| Join Game | `$UIText.OnlineFriends.JoinGame` (phase 1 row) | the code / address prompt -> the host's game lobby as a member |
| Create a Character / Teletran I / Friends List | original | unchanged |

Offline (no session) Private Match does the same: it opens the same lobby with bots and no join panel. So the offline
private match also loses its mode-list / options pre-screens; the settings live in the lobby (2.2). One flow, not two.

### 1.2 Game lobby - the host
- Menu: **Start Game**, **Select Map** (the original row = Change Map), **Game Settings** (new, 2.2), **Create a Character**,
  Teletran I, Friends List.
- The right column: the member list (original PlayerList rows: name + PC / Web tag, the voice icon, "Near", connecting
  spinner), the join panel (code, address, "C: copy the join code", the privacy line), the status line (the original gate:
  characters -> balance -> "Waiting for host to start game").
- Start Game: the original gate (D4) and countdown; the launch record goes to every member (ONLINE_MP_ARCHITECTURE 10.3).

### 1.3 Game lobby - a member (joined by code or Quick Match)
- Menu: **Create a Character**, Teletran I, Friends List. No Start Game, no Select Map, no Game Settings (`Online.IsHost`
  false: the same rows the member view already greys out / hides).
- The same member list, voice and status line; the map preview follows the host.
- The member's player-list A on another player = Mute / Unmute (phase 1).

---

## 2. The host's lobby settings

### 2.1 Entering: last-used settings
Private Match creates the lobby with the profile's last Game Settings (`[Lobby]` LastMode, and each mode's values as the
player left them; written only once changed). A new profile starts at Team Deathmatch with the mode's authored defaults
and Streets. "Hosting Game / Creating lobby..." shows online while the session opens (phase 1 popup).

### 2.2 Game Settings (one entry, host only)
One page in the original Settings sub-menu form (SettingsMenu_GFX `mc_subMenu`: lateral rows, written as they change).
The title is **[PC text]** "Game Settings"; the original also has `$UIText.Lobby.HostOptions` "Host Options" for this idea,
so the label can swap to it if the user prefers. Every change applies at once and reaches members through the published
lobby. The page is locked (read-only) while the start countdown runs.

| row | values | default | source |
|---|---|---|---|
| Game Mode | Team Deathmatch, Deathmatch, Code of Power, Power Struggle, Conquest, Countdown to Extinction | Team Deathmatch | the versus modes (Catalog modes, original names) |
| Team Balancing | Manual / Autobalanced | Autobalanced | the mode's authored row |
| Map Selection | the mode's choices (Host's Choice ...) | the authored default | the mode's authored row |
| Time Limit | the mode's choices | the authored default (TDM 15 minutes) | the mode's authored row |
| Points to Win / Rounds | the mode's choices | the authored default (TDM 40, Code of Power 2 rounds, ...) | the mode's authored row |
| Player Limit | Original (5 v 5) / Extended (32 v 32); FFA Original (10) / Extended | Original | PC row (D10 caps a browser lobby) |
| Autobot Bots / Decepticon Bots (team modes); Bots (Deathmatch) | 0 .. the limit | 0 | PC rows |
| Bot Difficulty | Easy / Medium / Hard / Expert | Medium | PC row |
| Iconic Characters (team modes) | Off / On | Off | PC extension |
| Kick Idle Players (online) | Off / On | Off (private), On (public) | D5 revised |

- **Game Mode** switches the page's rows to that mode's own (each mode keeps its last values). Between team modes the lobby
  stays as it is (the same lobby class); to or from Deathmatch the lobby is re-created (the team / FFA lobby class differs)
  and members follow it automatically, with their characters kept.
- A map that the new mode cannot use (CompatibleGameTypes) is swapped for that mode's first map.
- Public lobbies (section 4) lock Game Mode and the score / time rows to the playlist; the host can still pick the map
  where the playlist lets it (MapSelectionMethod).

### 2.3 Select Map (Change Map)
The original Select Map row and its map vote / host choice, unchanged; members see the preview follow.

---

## 3. Private matches: copy / enter a code
- Host: the join panel's code (C copies it); send it by any chat (D13: plus the relay mailbox invite when both are online).
- Joiner: Join Game -> the prompt -> "Joining game..." -> the host's game lobby as a member (1.3). Failures and texts as v1.

---

## 4. Public matches: Quick Match into a voting lobby (user decision 2026-10-22; updated 2026-10-23)
The original (RE ONLINE_PUBLIC_MATCH_RE.md, fc12ceb) had **no server browser**: public play was Quick Match by playlist (TDM 1,
the default; DM 2, CTF 3, KOTH 4, EXT 5, DOM 7). It ran a party size check, then an invisible search (mode + playlist + region,
up to 50 results), then asked each host in order for a party reservation ("Reserving space in game `i of `n"). The first
acceptance joins; if none accepts, it hosts ("Creating lobby..."). A strict NAT can't host: it re-searches, and shows
"Unable to Host" after 2 failures. **The user decided: no server list on PC either** - Browse Games is dropped.

Find Match = **Quick Match** (`$UIText.MainMenu.QuickMatch`, the original playlist list):
- Choose a mode -> `SearchForGameTitle` / `SearchingMessage` "Finding Game / Searching..." -> the best-ping open public game for
  that playlist: a lobby still gathering, or a match in progress with an open seat ("Reserving space in game `i of `n" while
  trying several, the original `ReservingMessage`). None -> the player hosts a new public lobby for that playlist
  ("Creating lobby..."). Cancel -> `CancelSearchMessage` "Cancelling...". **Built: step 5a, 2656b9c7.**
- **Join in progress is allowed in public games** (bAllowJoinInProgress; answers ONLINE_MP_FLOWS R1). A reservation is
  accepted only while the smaller team stays within the cap and the teams differ by at most 1; a party gets that team, a solo
  joiner the smaller team on arrival. The joiner spawns at once, mid-match, with the current score and clock. Private games
  keep D6 (joiners wait in the lobby) until the user says otherwise.
- Without a lobby service (Systems): Quick Match searches the LAN, then hosts a public-on-LAN lobby; over the internet the
  Online settings say a lobby service is needed (Lobby Service row; blank = LAN + codes only).

### 4.1 The public lobby and its map vote (Halo Reach style, PC ADAPTATION of the original veto)
The original public lobby rotated the playlist's maps with a veto (Online.SubmitMapVeto). The user chose a Reach-style vote
instead:
- **Options:** when the public lobby opens, the host draws **3 options** from the playlist's map rotation (no repeats, not the
  map just played), plus **"None of these"** in the first round only.
- **Shown in the lobby menu** (all players, host and members alike): one row per option, the map's name and its live tally -
  "Streets  -  3", "Rust  -  1", "Gorge  -  0", "None of these  -  0"; the row of the player's own vote is marked (the
  original selected-row look). Accept on a row = vote / change the vote. The map preview follows the leading option.
- **Who votes:** every human in the lobby, one vote each, changeable until the vote closes; bots do not vote. Joiners vote on
  arrival.
- **"None of these":** if it has a strict majority of the votes cast when the vote closes in round 1 (or when every human has
  voted for it), the host draws 3 new options (excluding the shown ones), votes reset, round 2 has no "None of these".
- **Closing:** the vote closes when the public lobby's countdown (the original 45 s, started by itself at 4 players) reaches
  10 s; the winner = the most votes; a tie or no votes = a random pick among the tied (host's seed). The winner is announced
  ("Next map: Streets") and loads at the countdown's end. The playlist fixes the mode.
- **Private matches** keep the host's Select Map (2.3); no vote.

Session fields (Systems adds them; FakeSession scripts them in the tests):
- `SessionMemberInfo.mapVote` - the member's choice: option index 0..2, 3 = None of these, -1 = no vote;
- `SessionLobby.voteMaps` (map ids of the options), `voteCounts` (one per option + None), `voteRound` (1 / 2), `voteOpen`,
  `voteWinner` (map id once closed, else -1). The host tallies from the members' info and publishes them in LobbyState.

ISession additions in use (FakeSession / UdpSession):
- `SessionHostSettings.isPublic` + `playlist` (host a public lobby the service lists);
- `quickMatch(playlist)` -> `SearchProgress{tried, of}` ... then `Joined` (found) or `HostOpened{public}` (none: hosting);
  `cancelSearch()`; `findGames` stays for LAN discovery only (no list on screen).

---

## 5. Host leaves / ends
- Private and public lobbies alike (D9): the session ends; members return to their party lobby with the original
  "The game has ended because the host has quit." Host migration stays a later item.
- In a match the host's leave ends the match the same way; a member's leave = D16 (suicide, bot or empty seat, forfeit).
- The host's Back in the lobby asks `HostLeaveConfirm` "Leaving will end the game for everyone. Are you sure you want to
  leave?" (phase 1 text).

---

## 6. Build plan (against FakeSession now)
1. Private Match hosts at once with the last-used settings (offline and online); `[Lobby]` LastMode / per-mode values.
2. The host's Game Settings row in the game lobby (duplicated original row, host only) -> the Settings sub-menu with the
   rows of 2.2, live writes, locked during the countdown.
3. Game Mode changes in the lobby (in place between team modes; re-created to / from Deathmatch, members follow).
4. Member menu = Create a Character / Teletran I / Friends List only.
5. Find Match -> Quick Match on the new ISession calls (5a, built); the voting public lobby (5b, section 4.1).
6. Tests (FakeSession): LV1 Private Match -> lobby with last settings; LV2 Game Settings rows + live publish; LV3 mode change
   team -> team in place and team -> DM re-created with members following; LV4 member menu has no host rows; LV5 Quick Match
   found / none -> host public; LV6 the map vote (options, tallies, None of these re-roll, winner at the countdown); LV7 offline
   private match = the same lobby with bots.
