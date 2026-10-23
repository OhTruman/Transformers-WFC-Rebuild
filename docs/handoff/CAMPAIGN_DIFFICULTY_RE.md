# Campaign difficulty: levels, scalers, co-op scaling, persistence (RE, 2026-10-23)

For the campaign menu and Gameplay.
Labels: C = TransGame / HM_Engine / TransContent bytecode, authored CDO data or frontend localization; H = inference.
The buff tables were first decoded in CAMPAIGN_CONTENT_DELTAS_RE §3.1 / §3.3; they are repeated here so this doc stands alone.

## 1. The three difficulties (C)

| `GameDifficulty` | Menu string (`UIText.Difficulty.*`) | Description |
|---|---|---|
| 0 | **EASY** | "For the new recruit: Experience the events of the war for Cybertron without difficult opposition." |
| 1 | **MEDIUM** (FRA "NORMAL") | "For the experienced soldier: Step onto the front lines to face a more battle ready adversary." |
| 2 | **HARD** | "For the hardened veteran: Test your skills against the most lethal opponents." |

- These strings close the old H "0 / 1 / 2 = Easy / Normal / Hard": the order is C, and the English menu name is **Medium**.
- Other strings: the header "Difficulty Setting", the button hint "Difficulty", and the lobby text "Choose Difficulty" / "Select
  a difficulty setting for the current game."
- **Setting it** (`HmGameInfo`):
  - `set_GameDifficulty` clamps to [0, MaxDifficultyLevel = 2], writes the profile, then calls **`OnSetDifficulty`**, which
    re-applies the difficulty buffs to every pawn. A **mid-game change takes effect at once**.
  - The URL option `?Difficulty=` is read in InitGame.
  - `_GameDifficulty` defaults to −1, meaning "read it from the profile" (PostBeginPlay / get).
- **Profile** (`HmProfileSettings`): setting id **12**; **default 1 (Medium)** when unset.
- Kismet / script can call `TnGameActionScriptBinding.Set/GetGameDifficulty`; debug `DebugSetDifficulty`.

## 2. What difficulty actually changes (C)
Difficulty is applied **only through buffs, loot sub-tables and progression records**:
- **No enemy health scaling.** No buff or code path touches AI health or HealthMax by difficulty. Boss and enemy health are the
  authored chassis values on every difficulty.
- **Weapon damage, fire interval and spread are not difficulty-indexed.**
  - `TnWeapon.ModifyDamage` / `GetFireInterval` / `GetCurrentSpreadModifier` (and abilities, buffs, projectiles) index their
    `*Multipliers` arrays by **`GetCurrentSkillDataIndex()`**.
  - `TnGame.GetSkillDataIndex()` returns **1** and no campaign class overrides it, so that index is the game mode, not the
    difficulty.
- **AI behaviour, action sets and spawn counts don't read difficulty.** Kismet doesn't branch on it either:
  authored.db has no difficulty-reading sequence op.

### 2.1 The buff level: difficulty × co-op player count
`TnCampaignGame.GetDifficultyBuffLevel = max(0, GameDifficulty × (MaxDifficultyLevel + 1) + (GetNumPlayers() − 1))`

That is **level = 3·difficulty + (humans − 1)**: Easy 0–2, Medium 3–5, Hard 6–8, for 1–3 players.

- Every pawn gets its list at spawn (`TnPawn.ApplyDifficultyBuffs`) and again on `OnSetDifficulty`.
- **Players:** `DifficultyBuffsPlayer` = PlayerDamageCaused, PlayerDamageTaken, PlayerDamageTakenDowned,
  AbilityResourceModifier.
- **AI:** `DifficultyBuffsAi` = DamageCausedAiVsAi.
- These are permanent (BuffTime −1).
- H: the level is computed when the buff is applied. A player joining mid-game changes `GetNumPlayers`, but the buffs are only
  re-applied to newly spawned pawns or on the next `OnSetDifficulty`.

| Buff (who) | E·1p | E·2p | E·3p | M·1p | M·2p | M·3p | H·1p | H·2p | H·3p |
|---|---|---|---|---|---|---|---|---|---|
| **PlayerDamageCaused** (player's outgoing damage ×) | 1.5 | 1.25 | 1.0 | 1.0 | 0.9 | 0.8 | 0.7 | 0.6 | 0.5 |
| **PlayerDamageTaken** (incoming damage ×) | 0.5 | 0.75 | 1.0 | 1.0 | 1.25 | 1.5 | 1.75 | 2.0 | 2.25 |
| **PlayerDamageTakenDowned** (while downed, i.e. bleed-out damage ×) | 0.5 | 0.4 | 0.3 | 0.5 | 0.4 | 0.3 | 0.25 | 0.2 | 0.15 |
| **AbilityResourceModifier** (ability energy ×) | 0.75 | 0.65 | 0.55 | 1.0 | 0.9 | 0.8 | 1.25 | 1.0 | 0.9 |
| **DamageCausedAiVsAi** (AI hitting AI, e.g. buddies vs enemies ×) | 0.45 | 0.35 | 0.25 | 0.3 | 0.2 | 0.1 | 0.25 | 0.15 | 0.05 |

**Reading the table:**
- **Co-op scaling is folded into the same index.** Each extra human lowers outgoing damage and raises incoming damage.
- Easy with 3 players equals Medium solo for damage.
- So the "enemy is tougher" effect comes from the player's damage-caused scaler: on Hard solo enemies effectively have 1/0.7
  ≈ 1.43× health, and Hard with 3 players 2×. There is no health scaler.

### 2.2 Downed, revive and game-over rules (C)
**The same on all difficulties:**
- revive radius 500 (robot) / 1000 (plane);
- revive time = the `Revive_Heal` anim length;
- you come back at **115 health** (or full with Kismet FullRevive);
- a human death (other than suicide), or **everyone downed**, → campaign game over after 1 s → checkpoint restart.

**By difficulty:**
- the bleed-out speed through PlayerDamageTakenDowned (above);
- two **post-revive self-buffs**, keyed on the difficulty, not the player count (`BuffTimeByDifficulty` indexed by the level,
  giving the same value across a difficulty's three columns):

| On revive (`TnPlayerPawn.OnGetRevive`) | Easy | Medium | Hard |
|---|---|---|---|
| `TnBuffDifficultyPlayerDamageTakenRevived` (damage-taken modifier / duration) | 0.15 / 10 s | 0.25 / 7 s | 0.35 / 5 s |
| `TnBuffDifficultyJamAbilitiesRevived` (abilities jammed for) | 5 s | 7 s | 10 s |

- The first row is a **damage-taken multiplier**, the same FloatModifier form as PlayerDamageTaken (CDO: FloatModifier
  [0.15 ×3, 0.25 ×3, 0.35 ×3], BuffTimeByDifficulty [10 ×3, 7 ×3, 5 ×3]). So a just-revived player takes only 15 / 25 / 35 % of
  incoming damage for 10 / 7 / 5 s: a protective window that shrinks with difficulty (C).
- H: it stacks multiplicatively with PlayerDamageTaken (both are product modifiers).

### 2.3 Checkpoints (C)
- **Checkpoint rules don't depend on difficulty.**
- `TnProfileSettings.ReachedCheckpoint(MapId, Index)` stores the highest checkpoint per map (setting `MapId + 16`) and clears the
  faction's "resume" slot (55 Autobot / 56 Decepticon).
- Restart, respawn and checkpoint data are as in CHECKPOINT_DATA / CHECKPOINT_BODY.

### 2.4 Loot (C)
- `TnLootTable.GetLoot(class, modifiers, difficulty)` only considers sub-tables whose `GameDifficulty` is **−1 (any) or the
  current one** (CONTENT_DELTAS §1.3).
- In the shipped data **all 742 loot sub-tables have GameDifficulty −1**, so **drops are identical across difficulties** (data, C).

## 3. Persistence, unlocks and achievements (C, TnProfileSettings)
- **`CompleteMission(MapId, Difficulty)`** (from `TnCampaignGame` mission complete → `ClientCompleteMission`):
  1. marks the map complete (bool `MapId`);
  2. stores the **highest difficulty completed** (int `MapId + 32`, −1 = never), raising it only;
  3. unlocks the next map in the faction's order;
  4. sets the faction resume map (55 / 56).
- **Campaign-complete achievements** (`CheckForLevelCompletionAchievements`):
  - HighestDifficulty = the **minimum** of the per-map best difficulties over the faction's maps;
  - if every map is done, it unlocks the achievement ids from `11 + HighestDifficulty` **down to 11**: Autobot 11 Easy, 12
    Medium, 13 Hard;
  - **Decepticon 24 / 25 / 26** the same way;
  - **27** when both campaigns are complete;
  - the chassis unlocks `Car5` (Autobot) and `Jet8` (Decepticon).
  - So finishing every chapter on Hard also awards the Medium and Easy achievements. Mixed difficulties award the lowest one.
- **The Extras unlock text** ("Complete Chapter VI: Defend Iacon to unlock", …, "Complete the Autobot Campaign to unlock") is keyed
  per chapter. It needs completion only, not a particular difficulty: H, from the strings.
- **Statistics:** `Match.Dif = GameDifficulty`.

## 4. For the menu
1. Show EASY / MEDIUM / HARD with the three descriptions; preselect the profile value (default MEDIUM).
2. On select: `set_GameDifficulty` (clamped 0–2) → profile setting 12 → re-apply buffs.
3. The co-op host's difficulty is the game's difficulty. Scaling with the human count is automatic through the buff level.
4. Per chapter, show the best difficulty completed (`MapId + 32`, −1 = not completed).
