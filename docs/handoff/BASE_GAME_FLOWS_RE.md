# Base-game flows for the quality pass: Escalation lobby and character select, party lobby, Private Match, chapter select, brightness, pause menu (RE, 2026-10-23)

**Labels:** C = shipped GFx ActionScript / UnrealScript / authored data; H = inferred.

**Source:** answers USER_PLAYTEST_2026-10-23b rows 9–15, 19, 23–26, 29, 34–37, 17 / 30.

**Scratch disassembly** (as2dis): PartyLobby, GameLobby, CharacterSelect, FrontEnd, PlayerList, Pause, Settings, Brightness.

**Strings:** English is from `Coalesced_int/TransGame/Localization/INT/UIText.int`.

## 1. Escalation: host / join / lobby / character select

### 1.1 Entry and menus (C)
- **Main menu Escalation** (`escalationBtn_mc`, hint `MainMenu.EscalationDesc` "Survive waves of enemies with your friends.") →
  `Online.OpenPartyLobby('GTS_SingleTeamGame')`. Lobby label `Lobby.EscalationLobbyLabel` "Escalation".
- **Escalation party menu** (`mc_menuEscalation*`):
  - Find Game → playlist 6 SurvivalPlaylist (`TnOnlineGameSettingsSVPublic`);
  - Host Game;
  - Leaderboards;
  - **Escalation Info** (`Lobby.EscalationTut`);
  - Matchmaking (Worldwide / Regional);
  - Invite.
  - Find and Host are enabled for the party host only.
- **Host Game:** the only choice is privacy.
  - Invite Only → `PlayPrivateGame('SV')` = **TnOnlineGameSettingsSVPrivate** (4 slots);
  - Public → playlist 6 as host.
- **The mode identity is set by the SV settings class**, not by the versus path:

| Field | Value |
|---|---|
| LobbyMapName | UI_CampaignLobby_m |
| LobbyGameClass | TnGameLobbyGameSurvival |
| GameClass | TnSurvivalGame |
| TeamType | **GTS_SingleTeamGame** (sets iconic mode) |
| OnlyAllowIconicCharacters | true |
| NumRequiredPlayers | 2 |
| FriendlyName / Description | "Escalation" / "Survive waves of attacks" |
| MapSelectionMethod | Private 1 (host's choice), Public 0 (rotate) |

- **So a TDM lobby, TDM title or TDM settings in Escalation means the host path didn't use SVPrivate / SVPublic** (playtest 9 /
  14).

### 1.2 Escalation game lobby (`mc_menuMultiplayerIconic*`, C)

| Item | Key | Rule |
|---|---|---|
| Start Game | `Lobby.StartGame` | host + private + no countdown |
| Select Map (spinner) | `Lobby.SelectMap` | maps whose CompatibleGameTypes contain SV; host + MapSelectionMethod 1 + no countdown |
| Select Character | `Lobby.SelectCharacter`, hint `SelectIconicDesc` "Select an iconic character." | |
| Escalation Info, Leaderboards, Invite, (360: Party / Party Invite) | | |

- **There is no Game Settings item and no difficulty item.** Escalation has no match settings; skill is fixed (MP_ESCALATION §1).
- If Start and Select Map are both disabled (non-host), they're hidden and the menu moves up 50 px.
- **Public:** auto-start with ≥ 2 players.
- **Playtest 10** (Game Settings → global Settings): the original has no Game Settings here, so remove the row from the Escalation
  lobby.

### 1.3 Character select (C, the same CharacterSelect_GFX as campaign)
**Layout: a vertical 3D wheel that wraps around** (`BuildCharacterWheel` / `PositionElementsInWheel`).
- **Ring:** all N characters on a full 360° ring (step 360/N). WheelRadius 160, ZoomOut 100, PerspectiveSkew 400, X 50,
  AlphaFadeMultiplier 2.8.
- **Per item:**
  - r = 400 / (400 + cos(a + 180°)·160 + 100);
  - x = 50·(1 − r); y = sin(a)·160·r;
  - scale r·100, **the focused item fixed at 160**;
  - alpha = (2.8r − 1.8)·100, so items fade to 0 around ±140°.
- **Motion:** each frame the angle eases 0.3 toward the target. Up / down wrap around. The wheel sits at (420, 265).
- **Visible:** with 13 entries, about 9–11 slots.

**One slot `mc_charButton`:**
- `charName_txt`: the name (`LocalizeUniqueIconicId` = the chassis FriendlyIconicName), **right-aligned**;
- `info_txt` **on the line below**: when focused, Specialty + 3 spaces + status; otherwise the status alone.
- Status, upper-cased:
  - **AVAILABLE** (`CharacterSelect.Unclaimed` "Available"): name at 100 % in 0x50B5D5, info in orange 0xFF9333;
  - **SELECTED** (`CharacterSelect.Claimed` "Selected", taken by another player): name at alpha 10, info white at alpha 30.
- **No "UNAVAILABLE" text exists.** Name and status are on separate lines, so nothing overlaps (playtest 11 / 13).

**Rules:**
- **No duplicates across players:** claimed names come from the GRI (`ClaimedIconicCharacters`, your own pick excluded). A on a
  claimed slot does nothing. The server `ValidateCharacterSelection` rejects names off the list or held by another PRI.
- **Locked characters:** `LockedCharacter=true` chassis are removed. One placeholder is added at the end:
  - Autobot: `ArceeLocked`, "Complete Autobot Campaign to Unlock";
  - Decepticon: `SlipstreamLocked`.
  - **For the rebuild (DLC owned):** no locked placeholders.
- **Side panel:** `characterPortrait_mc` at (650, 350) shows **2D bitmaps** `<Name>_Robot` / `<Name>_Vehicle`, plus weapons and
  abilities when expanded. **There is no 3D model in the original's select.**

### 1.4 Roster: order and the two playtest bugs (C)
The list is **the map's `CampaignCharacters`** (TransLevels.ini; the in-map CoopInfo list is the same in names and order). It is
**never built from the chassis roster** on ESC maps.

| Map | Order |
|---|---|
| BrokenHope (Autobots) | AirRaid, Bumblebee, Ironhide, Jetfire, Optimus Prime, Ratchet, Sideswipe, **SilverBolt**, Warpath, Arcee, Jazz, Zeta Prime, Scattershot |
| Remnant (Decepticons) | Megatron, Soundwave, Breakdown, Brawl, Barricade, Starscream, Skywarp, Thundercracker, Shockwave, Slipstream, Deadend, Demolishor, Onslaught |
| DLC Tranquillity / Pulse | = BrokenHope list (DLC_RE §2) |
| DLC District / Static | = Remnant list |

- **Optimus twice:** the chassis roster has `Truck` "Optimus Prime" **and** `Truck7` "Optimus" (the same TR_Optimus_ROBODEF,
  locked). A list built from chassis or presets gets both. **Use the map list:** Optimus Prime appears once.
- **Silverbolt "no model":**
  - Its id is `SilverBolt` (capital B), but the portrait exports are `Silverbolt_Robot` / `Silverbolt_Vehicle`. **Match bitmap names
    case-insensitively.**
  - Its AICD StartingForm is **TF_Vehicle** (a jet). If our 3D preview or spawn uses the robot mesh path only, check that the
    vehicle form has a mesh (H).

### 1.5 When character select appears (C)
- **Lobby:** only via Select Character. Escalation doesn't open it automatically and doesn't auto-pick.
- **In the map:** `UseInGameLobby = !PRI.HasSelectedCharacter()`. Only a player **without** a character gets CharacterSelect
  (TnUIControllerSurvival). Then `OnCharacterConfirmed` → PreGameCountdown_GFX. **A player with a lobby pick goes straight to the
  countdown.**
- **The pick carries over:** `TnPlayerReplicationInfo.CopyPropertiesTo` keeps it because the Survival GRI has
  `RetainSelectedCharacterOnTransition = True`. The multiplayer GRI has False. The Escalation lobby doesn't clear picks on travel.
- **Playtest 15** (asked again after load) is therefore a bug: the pick must persist.

## 2. Party lobby (the multiplayer root): no teams (C)
- **The right panel is the shared PlayerList movie** at (580, 178): `ViewMode='PartyLobby'`, `DisableIconicDisplay`,
  `ShouldCycleSpecialtyLevels`.
- **No team is assigned at the root:**
  - `TnPartyLobbyGame.TeamHandler` = **TnTeamHandlerNoTeams**, so every player has TeamID 255;
  - PlayerList builds team sections only if someone has TeamID < 255, so it draws **one list under a plain column header** (Level,
    Name). There are no AUTOBOTS / DECEPTICONS headers.
  - **Playtest 19 is right:** teams exist only in the game lobby.
- **Row contents:** level, awards, specialty level (cycling every 4 s), connecting / headset status, name. No score, kills or deaths.
- **Sort order:** connecting first, then score / kills / deaths, then name (case-insensitive).
- **Root menu:** Find Game, Host a Game, Create a Character, Teletran I, Invite / Friends.

## 3. Private Match (C)
**Game settings are chosen before hosting, not in the lobby.** The original lobby has **no Game Settings item**.
- Path: Host a Game → game-mode list (TDM, DM, DOM, CTF, EXT, KOTH) → **Host Options** (`Lobby.HostOptions`, one lateral selector
  per settings field) → **Create Game** → `PlayPrivateGame(tag)`.
- **TDM Private Host Options** (row order H, from the field order):

| Row | Values | Private default |
|---|---|---|
| Team Balancing | Manual / Autobalanced | **Autobalanced** |
| Map Selection | Rotate / Host's Choice | **Host's Choice** |
| Time Limit | 10 / 15 / 20 min | **15** |
| Points to Win | 20 / 30 / 40 / 50 | **40** |

- DM has no Team Balancing. **There is no bots option in the original.** Other modes: each settings class's
  LocalizedSettingsMappings.
- 10 private slots.

**The lobby menu:**
- Start Game (host, no countdown);
- **Select Map** spinner (host, Host's Choice);
- Create a Character, Teletran I, Invite / Friends;
- Y = Game Stats; B = quit to main menu; RB = Veto (Rotate only).
- **Playtest 24 / 26** (no Game Settings, stuck on TDM): the original fixes the mode in Host Options. The rebuild needs that
  pre-host screen (or an equivalent), not a lobby row.

**Team switch:**
- The button hint **X = `Lobby.SwitchTeams`** → `Online.SwitchTeam` → `ServerChangeTeam` (0 ↔ 1). It's shown **only when
  Team Balancing = Manual** and the mode is a team game.
- **No balance check** on a manual switch.
- With Autobalanced, everyone stays unassigned (255) in the lobby and **teams are assigned when the final countdown starts**.
- With Manual, joiners go to the smaller team (random on a tie).
- **Team layout:** one vertical list. Each team has a header row (TeamName, Score; sorted by score) followed by its players. Team
  colours: 0 = 0xF03C3C, 1 = 0xA06FFF, other = 0x8AF0FF.
- Team 0 = Autobots, team 1 = Decepticons.

**Start countdown:**
- **10 s** (LowCountdownTickThreshold, tick sound CTF_ROUND_TIMER_01), label `Lobby.MultiplayerCountdownLabel` "Match starts in".
  The number pops to 200 % under 10.
- **It cannot be cancelled in the original:** there's no cancel input or call, and Start is disabled while it runs. The only abort
  is a public-intermission logout case. Public intermission is 45 s.
- **Playtest 25 asks for a cancel**, which is a user-requested addition beyond the base game; that's Integration's call.

## 4. Campaign chapter select and fresh-profile unlocks (C; detail in CAMPAIGN_FRONTEND_FLOW_RE §3)
- **Layout:** three stages, **faction → chapter → checkpoint**.
  - Faction: Autobot / Decepticon logos, frame 330 px.
  - Chapter: `mc_missionChapterButton` rows "CHAPTER" + roman numeral, title, description and best difficulty, frame 528 px;
    left / right navigation.
  - Checkpoint: thumbnails, width (800 − spacing·(N − 1)) / N; locked thumbs show `lock_mc`.
  - Stages resize with a 0.5 s ease-out. X = Difficulty.
- **Numbering:** Decepticon I–V (D1 Dark Energon … D5), Autobot VI–X (A1 Defend Iacon … A5).
- **Fresh profile:** **only A1 (chapter VI, the Autobot start) and D1 (chapter I) are Available**. Everything else is locked, every
  checkpoint is start-only, and best difficulty is −1.
- **So VI being unlocked from the start is the original's design** (playtest 37). Chapters II–V and VII–X must be locked until
  CompleteMission unlocks the next one in that faction.
- **Highlight:** the focus goes to the current SelectedMapID if it's enabled, else the first enabled chapter (chapter I, in the
  Decepticon list).

## 5. Brightness calibration (C)
- **Where it opens:**
  - **The first time Campaign is entered from the main menu.** The Campaign submenu reads `ProfileData.HasAdjustedGamma` (profile
    id 34, default 0); if it's false it shows the brightness screen first.
  - Settings → Graphics → Brightness (the same Settings movie in the main menu and the pause menu).
  - **Never at first boot, press-start, multiplayer or level load.**
  - **Playtest 34** (first-launch setup) is a user-requested change from the base game.
- **Closing:**
  - A (Accept) sets HasAdjustedGamma = true and saves.
  - B (Cancel) restores the value from when the screen opened, but **also** sets HasAdjustedGamma = true, so it never re-appears.
- **Layout:**
  - title `Shared.BrightnessAdjust` (upper-cased);
  - hint "Adjust brightness so the left logo is barely visible.";
  - A Accept / B Cancel;
  - the background fades to 70 % over 1 s;
  - the reference image is the separate movie `CalibrationImage_GFX`: bitmap `CalibrationImage_CLR` 512 × 256, centred at
    about (560, 345). It's black with six grey bars (sRGB 30 / 75 / 120 / 165 / 210 / 255), a **Decepticon logo at sRGB 30 (left)**
    and an **Autobot logo at sRGB 165 (right)**.
- **The logos are static art:** the calibration movie has no script. **They respond only because the slider changes the real
  display gamma** applied to the whole frame, the UI included.
- **Playtest 35** (logos don't respond): our gamma isn't applied to the UI / calibration layer. Apply it as a final full-frame
  transfer, after the UI.
- **Slider:**
  - HmSlideBar on `ProfileData.GammaSetting` (profile id 35, int, **default 50**), continuous **0–100**;
  - d-pad hold accelerates (+0.5 % per frame); mouse drag on PC;
  - every move is applied live (`ApplyProfileSettings` → the Display data store "Gamma").
- **Gamma** = 2.2 + lerp(−0.95, 0.95, v/100), so **1.25 … 3.15**, with 50 = 2.2.

## 6. Pause menu per mode (C, PauseMenu_GFX `buildPauseMenu`)

| Mode | Items in order (`|` = separator) |
|---|---|
| **Versus** | Resume Game, [**Switch Teams** if Team Balancing = Manual and a team mode], \|, **Choose Character**, Settings, \|, Friends List, \|, Quit Game |
| **Escalation** | Resume Game, \|, Settings, \|, Friends List, \|, Quit Game. **No Choose Character** |
| **Campaign solo** | Resume Game, Restart from Last Checkpoint, Restart Chapter, \|, Settings, Tutorials, \|, Quit Game |
| **Campaign co-op** | as solo, then \|, Friends List, \|, Quit Game. The two restarts are **host-only** (disabled for others) |

- **Selection:** campaign = `GTS_CampaignGame` (SOLO tag → solo); iconic mode → the Escalation list; otherwise versus. The SOLO
  title is "PAUSED". The 360 party items aren't relevant on PC.
- **Choose Character (versus only):**
  - opens the **multiplayer** CharacterSelect in pick-only mode (`selectOnly`, `backout` → back closes it and returns to the pause
    menu);
  - a pick → `TnPlayerController.SelectCharacter`, plus the notification **"Selected character used on respawn."**;
  - the current pawn is kept until the next respawn.
  - **Playtests 17 / 29 / 30:** in versus it must be the multiplayer list (custom + iconic per the mode), never the Escalation
    one. Escalation has no in-match Change Character.
- **Settings:** the **same Settings movie** as the main menu, with no in-match branch:
  - Graphics: on PC, Brightness, Resolution, Fullscreen, Texture Quality, VSync, Commit;
  - Audio: FX / Dialog / Music 0–100 in steps of 10, Subtitles;
  - Controls: Vibration, Layout, Invert Y per form, Camera Sensitivity 0–100 in steps of 10.
  - Every change is applied immediately to the profile.
