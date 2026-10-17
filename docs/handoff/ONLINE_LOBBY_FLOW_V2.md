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
| Find Match | `$UIText.Lobby.FindMatch` | public matches (section 4): Quick Match / Browse Games |
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

### 1.3 Game lobby - a member (joined by code, Browse or Quick Match)
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

## 4. Public matches (Systems' central lobby service)
Find Match opens a small menu:
- **Quick Match** (`$UIText.MainMenu.QuickMatch`): choose a mode (the original playlist list), then `SearchForGameTitle` /
  `SearchingMessage` "Finding Game / Searching..."; the service returns the best open public lobby for that mode
  ("Reserving space in game `i of `n" while trying several, the original `ReservingMessage`); if none, the player hosts a new
  public lobby for that playlist ("Creating lobby..."). Cancel -> `CancelSearchMessage` "Cancelling...".
- **Browse Games** **[PC text]**: a list of open public lobbies - host name, mode, map, players n / max, ping, PC / Web -
  newest / best first, Refresh, Join. Joining = the member lobby (1.3). An empty list: **[PC text]** "No public games
  found." with Quick Match offered.
- Public lobby rules (the original's): the 45 s lobby countdown starts by itself when enough players are in
  (NumRequiredPlayers), the host may start earlier; Kick Idle Players defaults On; the playlist fixes the mode and limits.

ISession additions (for Systems' service; FakeSession scripts them now):
- `SessionHostSettings.isPublic` + `playlist` (host a public lobby the service lists);
- `findGames(query)` -> `GameList{entries: host, mode, map, players, maxPlayers, ping, platform, code}`;
- `quickMatch(playlist)` -> `SearchProgress{tried, of}` ... then `Joined` (found) or `HostOpened{public}` (none: hosting);
  `cancelSearch()`.

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
5. Find Match -> Quick Match / Browse Games on the new ISession calls (FakeSession results); the search / reserve popups.
6. Tests (FakeSession): LV1 Private Match -> lobby with last settings; LV2 Game Settings rows + live publish; LV3 mode change
   team -> team in place and team -> DM re-created with members following; LV4 member menu has no host rows; LV5 Quick Match
   found / none -> host public; LV6 Browse list + join; LV7 offline private match = the same lobby with bots.
