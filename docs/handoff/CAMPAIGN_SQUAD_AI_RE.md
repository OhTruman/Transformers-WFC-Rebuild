# Campaign squad AI (SetupAiSquad), downed / revive, and checkpoint restore (RE, 2026-10-23; for TnCampaignGameSolo)

**Labels:** C = TransGame / TransContent bytecode + CDO data (authored.db); H = inference.

**Sources:**
- `TnCampaignGame` (decomp_tc A2_KON_BASE_m);
- `TnAiSquad`, `TnSpawnPointFinder`, `TnPawn`, `TnPlayerController`, `HmButtonInputStateHandler`, `TnAiActionRevive`,
  `TnGoapGoalRevive`;
- the goal and action sets.

Builds on CAMPAIGN_AI_LAYER_RE §2 and CAMPAIGN_COOP_SYNC_RE. **⚠ marks corrections to COOP_SYNC §5.**

## 1. Squad setup and membership (C)
- `InProgress.BeginState` (not when returning from InProgressOver):
  1. `SetupAiSquad()`;
  2. `AiSquad.BeginGame()` (bGameInProgress, then RestartGame);
  3. the AI planning scheduler on;
  4. UI event 3.
- **SetupAiSquad:** every name in `CoopInfo.GetPlayableCharacters()` (the chapter's trio) **not selected by a human** →
  `AiSquad.AddMember(name)`. Solo → the other two trio members.
- **Membership changes:** a human picking a character → `RemoveMember` (destroys that AI controller). A human leaving → `AddMember`
  (COOP_SYNC §2–3).
- `EndState` (leaving InProgress, except to InProgressOver) → `AiSquad.EndGame`.

## 2. Spawning and respawning teammates (C)
**`AiSquad.Tick` → `SpawnPendingMembers`:** for each member without a controller, once its `CurrentRespawnTimer` ≤ 0:
1. **Spawn point:**
   - **first spawn after a (re)start** (`ShouldSpawnFromCheckpoint`): the **active checkpoint**'s own location if it's safe (capsule
     150 × 400), else its first safe `AuxilliarySpawnPoints` entry, else the checkpoint anyway (logged);
   - **otherwise (a respawn): near a player.** For each human pawn, its anchor service's **closest unoccupied navigation point in
     front of the player** (180° cone, anchors at most 3.0 s old, ground points unless the player is a plane), if safe.
2. `SpawnAiCharacter`:
   - TnAiController + TnAiPawn with the CoopInfo AICD's chassis; team = the CoopInfo faction;
   - `InitializeFromTemplate(AICD, PendingStartingFormOverride)`;
   - `CheckForSpecialSpawn`.
3. Then:
   - `IsAiBuddy = true`;
   - **`DownedEnabled = false`**;
   - `ExpandAnchorSearchIfFlying`;
   - pending scripted interacts applied;
   - **`SetMoveToObjective(member.ObjectiveActor, ObjectiveRange)`**, the stored Kismet objective.
4. After the pass, `PendingStartingFormOverride = TF_Invalid`. A checkpoint's `TnStartingFormOverrideCheckpointAction` sets it before
   the restart, so only the first spawn uses it.

**Death** (`TnCampaignGame.Killed` → `AiSquad.Killed`):
- the member's controller is cleared;
- `CurrentRespawnTimer = AIRespawnDelay` (**0**, Xe-TransGame.ini);
- its current MoveToObjective is saved.
- **So a dead buddy respawns on the next squad tick, in front of a player, and resumes its objective.**
- Buddies are never downed, so they just die and come back.

## 3. Behaviour: follow, engage, revive (C; summary of AI_LAYER §2 + new checks)
- **Goal order** (Buddy_AiGoalSet; trucks / jets / tanks): interact (high) > react > stimulus > **scripted Move-To** > defend self >
  **Kill** > interact (low) > **BuddyLead** > **Move-To-Objective** > **Move-To-Buddy** (close to the player) > idle.
  - Engineer buddies (Ratchet, Soundwave, Jetfire, Thundercracker) use Engineer_AiGoalSet.
- **Engage:**
  - Kill outranks all follow goals. Buddies fight whenever they have a target.
  - `TnCustomCostAwayFromBuddy` adds +10 cost to plans that leave them > 1500 UU from their player, so they fight near you.
- **Follow:** with no target and no objective → Move-To-Buddy, staying within the close range of the nearest friend. With an
  objective → BuddyLead (emote toward it, re-checked every 0.5 s) and Move-To-Objective.
- **⚠ Revive: buddies never revive anyone (C).**
  - The revive **action** exists in the Soldier_Jet_Buddy, Soldier_NoWeapon_Buddy and Soldier_Jet_Engineer_Buddy action sets.
  - **But the only goal that requests it** (`TnGoapGoalRevive`, the ReviveTarget world state) is in **Engineer_Boss_AiGoalSet**, which
    only `Soundwave_Boss_AICD` uses.
  - No buddy goal set (Buddy_AiGoalSet: 23 templates; Engineer_AiGoalSet: 4) contains it.
  - **COOP_SYNC §5's "AI squad members revive downed players" was wrong.**
- **Being revived:** buddies have DownedEnabled false, so there's nothing to revive.

## 4. Difficulty scaling for the squad (C, CAMPAIGN_DIFFICULTY §2)
- Level = 3·difficulty + humans − 1.
- **AI pawns (buddies and enemies alike) get only `DamageCausedAiVsAi`:** 0.45 / 0.35 / 0.25 (Easy 1–3p), 0.3 / 0.2 / 0.1 (Medium),
  0.25 / 0.15 / 0.05 (Hard).
  - So **buddies hurt enemies far less on harder settings and with more humans**, and enemies hurt buddies by the same factor.
  - Their own health is unscaled.
- Players get PlayerDamageCaused / Taken / TakenDowned / AbilityResource.
- Applied at spawn (so every buddy respawn uses the current level) and re-applied to all pawns on each join.

## 5. Human downed state (C)
- **⚠ Solo: the human is never downed.** `TnPawn.GlobalCanBeDowned` returns **false for a player pawn when
  `Game.GetNumPlayers() ≤ 1`**.
  - **So in solo a lethal hit kills outright → `TnCampaignGame.Killed` → CampaignGame over** (1 s → MISSION FAILED).
- **Co-op (≥ 2 humans):** downed when the state allows it (robot form; vehicle form forces robot first, except planes) and not on a
  turret. TnCampaignGame.AllowDowned = true.
  - **Bleed-out:** DownedHealth **400**, drain **40 /s** as TnDamageTypeDownedDrain → 10 s base, **× PlayerDamageTakenDowned**:

    | | 2 players | 3 players |
    |---|---|---|
    | Easy | 0.4 → **25 s** | 0.3 → **33 s** |
    | Medium | 0.4 → 25 s | 0.3 → 33 s |
    | Hard | 0.2 → **50 s** | 0.15 → **67 s** |

    H: whether PlayerDamageTaken also multiplies the drain. If it does, divide by it.
  - **DownedInvulnerableLength 3 s** at the start: only the drain hurts. Enemy fire afterwards shortens it.
  - **Downed suicide:** hold the button for **1.0 s** (DownedSuicideButtonInputHandler) → infinite drain damage → death → game over.
- **Revive (player → player): a hold.**
  - The target is the nearest downed teammate within **500 UU** (robot) / **1000 UU** (plane) that nobody else is reviving. The
    contextual "Revive" prompt is shown.
  - X / Interact goes through `HmButtonInputStateHandler`: press = 1, tap release = 2, **held ≥ 0.3 s = 3**, release after the hold = 4.
  - **Event 3 → TryRevive** (else TryPickup); event 4 (**let go**) → **AbortRevive**. A tap (2) does interact / reload instead.
  - **So: hold X for 0.3 s to start, keep holding for the Revive_Heal animation** (`ClientStartReviveCountdown` = the animation length;
    the HUD revive bar).
  - Releasing early, losing the target, or the anim being interrupted fails the revive.
  - While being revived the target doesn't drain.
  - Success → `GetRevive`: **115 health**, plus the OnReviveBuffs: damage taken × 0.15 / 0.25 / 0.35 for 10 / 7 / 5 s, and abilities
    jammed 5 / 7 / 10 s.

**⚠ Game over, corrected:**
- **Any human killed** (not DmgType_Suicided), including a bleed-out → game over.
- The `AiSquadMemberBledOut` clause in Killed is **dead code**, because buddies can't be downed.
- `NotifyDownedPawn` → game over at once **only if no same-team TnPawn is still up**. **AI buddies count as up**, so with buddies alive
  both humans can be down without an immediate game over. **Nobody can revive them then, so the first bleed-out ends it.**
- A two-human team with no living buddy (all three trio slots human, or the buddy dead and not yet respawned) → immediate game
  over.

## 6. Checkpoint restore and the squad (C)
1. **Restart from checkpoint** (in place, CAMPAIGN_STREAMING §4): `CleanupBeforeRestartFromLoadedCheckpoint` destroys **all AI
   controllers and pawns** (buddies included). Non-static levels reload, `ResetLevel` runs, then `StartMatchAfterRestartFromCheckpoint`.
2. `InProgress.StartMatch` → **`AiSquad.RestartGame()`**:
   - every member: controller = none, **ShouldSpawnFromCheckpoint = true**, **objective cleared**;
   - **pending AI interacts cleared**;
   - membership unchanged (the same names; humans keep theirs).
3. The next squad tick spawns every member **at the active checkpoint** (its location / AuxilliarySpawnPoints), with the checkpoint's
   starting-form override if it has one. Then the override resets.
4. **Nothing else of the squad is saved.** No health, ammo or objective is restored: objectives come back only when the level Kismet
   sets them again (TnSeqAct squad objective ops).
   - Player save data (health, weapons, ammo) is restored separately from the checkpoint save (STREAMING §4).
5. A **chapter restart** or a new level load goes through `BeginGame` (bGameInProgress + RestartGame), with the same spawn rule.

## For TnCampaignGameSolo (summary)
- Two AI squad members (the trio minus the player's pick):
  - they spawn at the checkpoint at start or restart, and respawn instantly in front of the player on death;
  - Kill first, stay near the player (cost +10 beyond 1500 UU), follow when idle, lead to Kismet objectives;
  - **never downed and never reviving**;
  - deal × DamageCausedAiVsAi to enemies.
- **The solo human isn't downed:** death → game over → checkpoint restart (squad respawned at the checkpoint, objectives cleared).
