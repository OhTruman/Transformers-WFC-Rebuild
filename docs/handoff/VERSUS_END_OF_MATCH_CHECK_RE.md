# Versus end of match: the original's rules vs ours (hand-over check, 2026-10-23)

**Sources:**
- **Original:** `notes/MP_PROGRESSION_SCORING_AI_2026-10-06.md` §1–4, §8 and its addenda (C); TnMultiplayerGame.MatchOver
  (`work/script/decomp_tc/MP_ESC_BrokenHope_BASE_m.TransContent.txt` 480–570); EndGameStats_GFX AS (`work/q/gfx`);
  TnCharacterScriptBinding.
- **Ours:** Rebuild 3a2c822d:
  - `src/game/Match.cpp` (endGame 440–490, MatchOver countdown 169);
  - `src/frontend/FrontendRuntime.cpp` (updateProgression 908–930, the Customize bindings 467–470);
  - `src/frontend/UIController.cpp`, `src/core/Application_Frontend.cpp` 1660–1680.

**Result: everything matches except four items (§2). One of them, public-match XP, needs a fix before public lobbies ship.**

## 1. Sequence and contents (original, C) and our status

| Step | Original | Ours | |
|---|---|---|---|
| End trigger | score limit `EndGame(scorer, "Score")`; clock `EndGame(none, "")`; forfeit 1.5 s after a team empties; **no overtime** | Match.cpp: the same three; no overtime | match |
| Winner | versus PickWinningTeam (higher score / the other team empty; equal → "Tie game"); FFA GetWinningPRI | the same; FFA equal top = draw | match (FFA tie: see §2.4) |
| Completion per player (stats) | 0 win / 1 loss / 2 draw | e.completion, the same | match |
| **Win / lose XP** | versus only: **GameWin 300** to everyone on `Winner.Team`, where Winner is the PRI passed to EndGame (on a clock end, the **top individual scorer's team, even on a team tie**); everyone else **GameLose 150**; **DM none** | `e.xpWinTeam` with the same quirk; Application tests check GameWin / GameLose and "DM: none" | match |
| MatchOver.BeginState | AwardMVPAchievement; GameStatus 5; **UI event 9** (OnEndGame); voice-mute reset; stats StartWrite; timer **MatchOverCountdown 15 s** | state MatchOver, status 5, MatchEnded → UI event EndGame (9); matchOverCountdown 15 | match (MVP: §2.2) |
| UI | GameEnded state: HUD hidden → **EndGameStats_GFX** | UIController.endGameUI = EndGameStats_GFX_1 (Multiplayer class) | match for versus (Escalation: §2.3) |
| **EndGameStats contents** | Two views, toggled with **X**: **Earnings** (title `XpEarned`: per specialty `Customize.GetXpEarnedForSpecialtyLastMatch` animated up to `GetXpEarnedForSpecialty`, level-ups with `$UIText.EndGameStats.LevelUp`, `GetLevelForSpecialty` / `GetXpNeededForLevel` bars) and **Scoreboard** (title = `<CurrentGame:GameOverMessage>` when set, else `FinalScores`; columns Level · Name · Score · Kills · Deaths; sorted Score↓, Kills↓, Deaths↑, Name). `IconicMode` opens on the scoreboard; `xpOnly` shows only earnings. Mode icon from `<CurrentGame:GameModeTag>` | the original movie; the four Customize bindings are implemented from the profile progression (lastMatchXp / xp / levelForXp / xpNeededForLevel); GameOverMessage, GameTeamStatus, GameModeTag and _bIconicMode are published | match |
| Level-ups during the match | each award → CheckForLevelUp → `_global.NotifyLevelUp` + `TnPlayerLevelUpMessage` "`p is now a level `l `s"; unlocks collected in SkillsUnlockedThisGame | presentLevelUps: the same calls and text; unlockedThisGame_ | match |
| Commit at the end | `ClientWriteLeaderboardStats` → AddNewlyUnlockedSkills → **SaveProfileSettings** → customization file → TnStatsWrite classes | the profile is saved once when the match level ends, after adding newlyUnlocked | match (no leaderboard service: LIVE) |
| Return | 15 s → **ReturnToGameLobby**: ServerTravel to the game lobby; everyone, party members included, stays in the host's game lobby | ReturnToLobby → GameFlow::returnToGameLobby | match |

## 2. Differences

1. **XP gate in online public games (needs a fix when public lobbies ship).**
   - **Original:** `CanGainXp = !IsPrivateGame()`, so **public matchmade games earn XP, challenges and stats**; private games award
     0 (popups still show).
   - **Ours** (FrontendRuntime.cpp:920–921):
     - offline earns XP (a PC adaptation; `WFC_ORIGINAL_XP_RULE=1` restores the original);
     - **every online session is forced to no XP**, *including* `playlistId ≥ 0` public lobbies.
   - **Correct for today's private-only online (D8).** When public Quick Match lands, the line should keep XP for public
     sessions (`sessionRole != Offline && playlistId < 0` → no XP).
2. **MVP gate.**
   - **Original:** `CanAwardMVP` requires more than one PRI **and `CanGainXp`**, so private matches never award MVP
     (achievement 37).
   - **Ours** (Match.cpp:482–485): fills `e.mvp` without the XP condition. **It has no consumer today** (no achievements yet).
     Add the CanGainXp condition when achievements are wired.
3. **Escalation's EndGameStats.**
   - **Original:** TnUIControllerSurvival → TnUIControllerCoop, whose `EndGameUI` is also EndGameStats_GFX. The Escalation end of
     match shows it (header "You survived until wave N"), then 15 s → the game lobby (MP_ESCALATION_RE §8.4, corrected 6b08fb9).
   - **Ours:** `UIController::endGameUI` returns the movie only for the Multiplayer class. When Escalation is ported, the
     Coop / Survival class also needs EndGameStats. Escalation XP is none (`CanGainXp` false), so its view is effectively the
     scoreboard.
4. **FFA tie (known, noted earlier).**
   - **Original:** `GetWinningPRI` takes the first-found top scorer (stock), and the FFA game-over message is "".
   - **Ours:** an equal top score is a draw (from RE PLAYTEST §3, completion type 2).
   - It only affects a winner label; there's no XP effect, since DM has no win / lose XP.
5. **(PC, already listed)** Bots: the original had none, and `UsesPlayerDataProvider` excludes bBot PRIs from player lists and
   the scoreboard. Ours lists bots (ONLINE_PC_DEVIATIONS row 8). If kept, they also appear in EndGameStats' scoreboard. Whether
   bot kills give XP is D8.
