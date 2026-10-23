# Online public match: the post-match flow, party across matches, voice mute / report (RE, 2026-10-23)

**Labels:** C = TransGame / TransContent bytecode, AS (`work/q/gfx`), CDOs; LIVE = handled by the Xbox Live guide, not the game; H =
inference.

This consolidates VERSUS_END_OF_MATCH_CHECK_RE, ONLINE_PARTY_RE §4–5, ONLINE_PUBLIC_MATCH_RE §3 and
MILESTONE05_FRONTEND_MATCH_BOOTSTRAP. The **new** items are marked ★.

## 1. Post-match flow (C)

| t | What happens | Source |
|---|---|---|
| 0 | `EndGame`: **★ voice muting reset** (§3); completion stats; winner (versus: PickWinningTeam; FFA: the first top scorer) | TnGame.EndGame |
| 0 | `MatchOver.BeginState`: MVP achievement (only when CanGainXp, i.e. public); GameStatus 5; **UI event 9** → TnUIController **GameEnded**: HUD hidden, **EndGameStats_GFX** opened | TnMultiplayerGame |
| 0 → 15 s | EndGameStats on screen (below) | |
| 15 s | **MatchOverCountdown 15 s → ReturnToGameLobby**: ServerTravel to the **same host's game lobby** (UI_Lobby_m), same game session; everyone comes along, party members included | VERSUS_END §1, PARTY §5 |

**EndGameStats (in game):**
- **Earnings** view (title XpEarned):
  - per specialty, XP animated from last-match to the total;
  - **level-up strip + `$UIText.EndGameStats.LevelUp`** on each level crossed;
  - XP bars use `GetXpNeededForLevel`.
- **Scoreboard** view: GameOverMessage or FinalScores; Level · Name · Score · Kills · Deaths; sorted Score↓ Kills↓ Deaths↑ Name.
- Iconic mode opens on the scoreboard.
- **X toggles** the two views (`ViewScores` / `ViewXp`; 0.5 s fades).
- **★ No "play again" or "return" choice exists.** The in-game copy has no parent menu, so its B "Back" hint is inert. The 15 s
  timer always returns everyone to the game lobby.
- `TnUIController.GameEnded.OnPlayAgain` → WaitingOnGameStart is the state the lobby's next start reuses. Nothing in the movie calls it.
- Rank-up during the match: each award → CheckForLevelUp → HUD `NotifyLevelUp` + the `TnPlayerLevelUpMessage` line. There's no
  separate rank-up screen.
- **XP only in public games** (`CanGainXp = !IsPrivateGame()`). Private games show 0 (VERSUS_END §2.1: ours must keep XP for
  public sessions when quick match ships).

**★ Back in the game lobby (GameLobby_GFX):**
- **Y "Game Stats"** (`$UIText.Lobby.GameStats`, hidden in iconic mode) re-opens EndGameStats **xpOnly, skipXpTabulation** (the
  earnings with no count-up animation) over the lobby. B closes it.
- The party lobby has the same Y, except for campaign or single-team games.
- **Public lobby rules** (ONLINE_PUBLIC_MATCH §3, BOOTSTRAP):
  - next map = **Rotate**: a uniformly random *different* compatible map;
  - **map veto** (RB, `Online.SubmitMapVeto`) passes at max(VetoQuarum = NumRequired/2 + 1, humans/2 + 1) votes;
  - **autostart 45 s** once NumRequiredPlayers (4) humans are present; final countdown below 10 s;
  - teams re-drafted (PublicSnakeDraft by score if anyone scored, else by level) at the final countdown.
- **Leaving:** B in the game lobby = QuitToPartyLobby (see §2).

## 2. Party across matches, and leader / host loss (C; ONLINE_PARTY_RE §4–5)
- **The party stays together:** the whole game returns to the host's game lobby, and the party isn't consulted. Members leave with the
  game (host quits → `TellClientsToReturnToPartyHost`: every client goes to its own party lobby, and party members reattach to their
  leader there) or alone (they keep their party reservation and reattach; "Waiting for party leader..." up to 30 s).
- **No party-leader migration:** the leader leaving the 'Party' session → **disband** (ClientPartyDisbanded, 1.0 s grace). Each member
  becomes the leader of its own new party. No message in the original.
- **★ No game-host migration:** the game is a listen server.
  - The host quitting **to the menu** sends everyone to their party lobbies (above).
  - The host **dropping** (crash / network) hits each client's stock `NotifyConnectionError` → `QuitWithError`: the error title and
    message are stored on the GameEngine (`SetCurrentErrorMessage`) and `ConnectionErrorQuitGame` leaves the game session. H: the
    frontend then shows that error and returns to the main menu / party lobby.

## 3. ★ Voice: automatic muting and mute / report (C / LIVE)
**Automatic (gameplay) muting, `TnVoiceMuteHandler`:**
- **In a match:**
  - `TnMultiplayerGame.RestartPlayer` sets **`PC.VoiceChannel = its team index`**, then `UpdatePlayerMuting`;
  - **every pair of players on different channels is gameplay-muted both ways** (`GameplayMutePlayer` on each side), and same-channel
    pairs unmuted;
  - **so in team modes you hear only your own team.**
  - A player with no team (FFA, H: no Team object) keeps channel −1, so everyone in FFA hears everyone.
- **`EndGame` → `ResetMuting`:** every channel −1, then everyone unmuted. So **after the match (EndGameStats and the lobby)
  everyone hears everyone again.**
- **A player leaving:** `UnMutePlayer`, channel −1, unmuted for all.
- `UpdateGameplayMuteList` also completes the stock voice handshake.

**Player-initiated mute / report: there is no in-game UI. It's LIVE.**
- No movie has a mute or report control. The only per-player action is **A / X "Gamer Card"**
  (`$UIText.PlayerList.Gamercard`) in PlayerList, InGameStats, GameLobby, PauseMenu and EndGameStats.
- That calls `Online.ShowGamerCardUI(name)` → `TnOnlineActionScriptBinding` → `PlayerInterfaceEx.ShowGamerCardUI(controller,
  UniqueId)`, which opens the **Xbox guide gamer card**. Mute, Send Feedback / Report and friend requests all live there (Live). The
  game itself only reads the platform mute list (stock).
- **For us:** a mute / report UI is a **PC addition**.
  - The original-faithful hook is the same per-player "Gamer Card" button in those five screens (A in lists, X in InGameStats).
  - Keep the gameplay team-channel muting above independent of user mutes. The original kept the two separate: gameplay mute vs
    the platform mute list.

## Summary for Frontend / Systems
- **Post-match:**
  - 15 s of EndGameStats (X toggles earnings and scoreboard; XP only in public) with no choices;
  - then everyone goes to the same host's game lobby: next map random-different, veto, 45 s autostart at 4 players, a re-draft, and
    Y re-opens the last match's XP.
- **Party:** stays together through the lobby. There is no migration of party leader or game host; a leader leaving disbands, a host
  dropping is a connection error for all.
- **Voice:**
  - team-only in team matches (cross-team gameplay mute), everyone in FFA, everyone again after EndGame;
  - mute / report was the Xbox gamer card, so ours is a PC addition behind the Gamer Card buttons.
