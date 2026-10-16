# Cross-play: profile, identity and stats (Systems, 2026-10-16; design only, no code)

Companion to CROSSPLAY_NETCODE_DESIGN.md. Constraints: non-commercial project, no accounts and no server we must run, the
player's data stays on the system they play on. Open questions at the end.

## 1. What the original kept (reference)

- **Profile** (TnProfileSettings / OnlinePlayerData.ProfileData; the rebuild's LocalProfile, wfc_profile.ini): FX /
  Dialogue / Music volume, subtitles, vibration, control scheme, invert Y per form, camera sensitivity, gamma; the PC SKU's
  display settings (resolution, fullscreen, texture quality, VSync). Saved on the console's storage device per gamer profile.
- **Progression** (TnXpManager, reconstructed in frontend/Progression.h): XP per specialty (Scout, Scientist, Soldier,
  Leader), level 0-25 each from Default__TnXpManager.LevelTable, XP capped at 355,000; the displayed level is the sum of the
  four; Prime mode once all four are maxed. Challenges (TransChallenges.ini: ChallengeId, StatPropertyId, GoalValues,
  XpRewardValues, PrimeChallenge) unlock tier by tier from lifetime stat counters (ReportGameStat: add / max-in-match /
  add-in-match); newly unlocked skills carry "new" badges (TnCharacterCustomizationData). Written at match end
  (ClientWriteLeaderboardStats -> SaveProfileSettings).
- **Leaderboards** (Teletraan I -> Leaderboards; Xbox Live / PSN stats service, now gone): views cycled left / right,
  columns Rank / Player Name (Gamertag on 360) / Score, "Player Records", Friend / Global / Top standings,
  TnLeaderboardRead ColumnMappings "Enemies Killed" and "Redeemer Rating"; "You just passed `p on the leaderboard"; Escalation
  (survival) runs had their own boards ("Your leaderboard progress will be lost"). The rebuild shows the screen with "the
  online stats archive is absent" (docs/FRONTEND.md: SERVICE DEPENDENT).
- **Identity:** the platform account (Gamertag / PSN ID); names filtered by UnsupportedUnicodeCharacters (Xe-TransEngine.ini).
- RE gap: the full list of leaderboard views / columns per mode (only two ColumnMappings are named in TransGame.int) - open
  question 1.

## 2. Local profile per platform (unchanged model, new storage backends)

One profile per player per system, the same fields everywhere; where it lives:

| platform | storage | notes |
|---|---|---|
| PC (phase 1) | wfc_profile.ini beside the exe / in the run folder (today) | add an explicit profile folder (e.g. %APPDATA%\WFC Rebuild) only if the user wants profiles to survive a package swap - open question 2 |
| Browser (phase 2) | IndexedDB per site origin | the browser can evict it; offer Export / Import of the profile file from the menu |
| PS3 / Xbox 360 (late) | the console's save storage (homebrew file on the HDD) | per AssetTools' feasibility study |

- **PC-only sections** ([PCSettings]: resolution, upscaling, HD, ray tracing, FPS counter ...) stay per system and never
  travel: a browser or console ignores them. Original fields (volumes, controls, invert Y, sensitivity) and progression are
  the portable part.
- **One file format** everywhere (the current ini text, UTF-8), so a profile exported on one platform imports on another.

## 3. Portable identity without accounts

- **Player id = a locally generated key pair** (Ed25519 or similar) created on first launch; the id shown to others is a short
  fingerprint of the public key (e.g. 8 base32 characters: "WFC-7QK2-M9TD"), plus the chosen **display name** (free text,
  filtered like the original: length cap, UnsupportedUnicodeCharacters, no control characters).
- **Moving between platforms:** Export Profile writes one file (identity key + portable profile + progression); Import on
  the other system makes it the same player. No cloud sync; the user carries the file (or a QR / copy code for the identity
  alone). Two systems with the same identity simply diverge until the next import (open question 3: merge or overwrite).
- **Matchmaking across platforms:** sessions are joined by the netcode's join codes / direct IP; the identity is what peers
  see in the lobby and remember as "friends" (a local friends list of fingerprints + names). No directory server needed; an
  optional self-hosted lobby list could index sessions by host fingerprint (open question 4).
- **Signing:** lobby messages and match results are signed with the player's key, so a remembered friend cannot be
  impersonated by someone choosing the same display name. Nothing more is enforced (no server to enforce it).

## 4. Stats and progression in a cross-play match

- In lockstep every peer simulates every player, so every peer computes the same scoreboard, awards and per-player game
  stats. **Each peer saves only its own player's** XP / challenge stats / unlocks into its local profile - exactly the
  original's ClientWriteLeaderboardStats order, run locally.
- **Loadouts and unlocks affect the sim** (abilities and weapons are level-gated): the lobby carries each human's chosen
  character, specialty, level-gated abilities and weapons; every peer must simulate them identically, so the lobby message
  is the authority for the match. A peer cannot verify another's unlocks (no server). Options (open question 5): (a) trust
  the lobby (friends-first), (b) an "everything unlocked" rule for cross-play sessions, (c) the host decides per session.
- **Leaderboards without a service:**
  - **Personal records** (Player Records): local, always available.
  - **Friend standings:** peers exchange signed stat summaries (the leaderboard columns) when they play together or by
    file; each player's board shows the friends they have met, from the last summary each signed. Works offline, no
    server.
  - **Global / Top standings:** only with a server someone runs (optional, self-hosted, opt-in; open question 6). Without
    it, the Global / Top views show "No Leaderboard Ranking" (the original's own string).
- **Escalation / survival boards:** the same model per mode.

## 5. Privacy and constraints

- No accounts, no e-mail, no telemetry, no analytics. The profile and identity key stay on the device unless the player
  exports them.
- What another player learns: display name, fingerprint, level (self-reported), and - through WebRTC - the IP address of
  the connection (the join code screen says so).
- Friend lists and received stat summaries are local and can be cleared from the menu.
- Display-name filtering is client-side only (each client filters names it shows); nothing is reported anywhere.
- Licences: any key / crypto library used must fit the non-commercial rule (open question 7).

## 6. Work items (after package 6, after the netcode's phase 1 basics)

1. Profile file: add the identity block and an Export / Import menu action (PC).
2. Lobby message: display name, fingerprint, level, specialty, loadout; signature.
3. Results: each peer writes its own player's progression from the shared match results (reuses the offline path).
4. Leaderboards screen: Player Records from the local profile; Friend standings from stored signed summaries.
5. Browser storage backend (IndexedDB) + Export / Import; console backends per the feasibility study.

## 7. Open questions

1. RE: the full leaderboard view / column list per mode (beyond "Enemies Killed" / "Redeemer Rating") - worth recovering
   from the TnLeaderboard* classes for the Player Records / Friend standings screens?
2. Profile location on PC: keep wfc_profile.ini beside the exe (a package swap starts fresh) or move to a per-user folder?
3. Same identity on two systems: overwrite on import, or merge progression (take the higher XP per specialty)?
4. An optional self-hosted lobby / session list - wanted, or join codes and direct IP only?
5. Unlocks in cross-play: trust the lobby, everything-unlocked for cross-play sessions, or a host setting?
6. Global leaderboards: skip entirely, or an opt-in self-hosted server the user could run?
7. Crypto library choice (signing) under the non-commercial licence rule.
8. Display-name rules: reuse the original's character filter only, or add a profanity filter (client-side)?
