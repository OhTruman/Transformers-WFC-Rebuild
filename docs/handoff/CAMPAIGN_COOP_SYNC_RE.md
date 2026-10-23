# Campaign co-op: joining, travel, AI trio slots, difficulty re-apply, revive / game over with two humans (RE, 2026-10-23)

For the campaign-online phase.

**Labels:** C = TransContent `TnCampaignGame` / `TnCoopGame` (`work/script/decomp_tc/A2_KON_BASE_m.TransContent.txt` 113–880),
TransGame `TnTeammateSpawner`, `TnPlayerRecord`, `TnAiSquad`, `TnPlayerController`, CDOs; H = inference / stock UE3.

**Related:** CAMPAIGN_FRONTEND_FLOW_RE (lobby, CampaignSelect, the in-map character select), CAMPAIGN_DIFFICULTY_RE,
CONTENT_DELTAS §3.1 (downed / revive), CHECKPOINT_*, CAMPAIGN_HUD_OBJECTIVES_RE.

## 1. Host travel and the start of a chapter (C)
- **Lobby start:** `TnGameLobbyGame.StartLevel` → URL = settings + `<map>?MapId=<id>[?Checkpoint=<path>]` →
  `ServerTravelToMap(URL, bAbsolute true, bShouldSkipGameNotify false)`.
  - Every connected member is a client of the host's game lobby (the listen server), so the **server travel carries them**.
    There's no separate join (ONLINE_PARTY_RE §1).
  - The loading screen comes from the URL (`StartLoadingMovie`: "Campaign", "Loading `m", 3 tips).
- **PendingMatch** (TnCampaignGame):
  - `CanStartMatch` = the **host's** local player has a selected character **and** `AllPlayersHaveControllers` (TnPlayerControllers
    of the desired class ≥ `GRI.NumPlayers`). So the match waits for every traveller to arrive and the host to pick.
  - `TrySpawnPlayer` sets `RespawnFromCheckpoint = true` and returns false: nobody spawns before the start.
  - On `AllClientsFinishedTravel && SpawnHelper.AllPlayersInGame`, `_PendingStartingFormOverride = 2` (H: the starting-form hand-off).
- **Start** (`InProgress.BeginState`):
  1. GameStatus 3, `ElapsedTime = 0`;
  2. **`SetupAiSquad`**: every playable character of the chapter (`CoopInfo.GetPlayableCharacters`) that no human selected becomes
     an **AI squad member**;
  3. `AiSquad.BeginGame`, the planner on, UI event 3.
- **Team:** `SetupCoopInfo` forces every TnPlayerController onto the CoopInfo faction team. `HandleSeamlessTravelPlayer` keeps
  the team.
- **Start position:** `FindInitialPlayerStart`. `RespawnFromCheckpoint` → `FindCampaignStart` =
  `SpawnPointFinder.FindFromActiveCheckpoint()`: the active checkpoint's start (the `?Checkpoint=` one at load, or the chapter's
  first). Otherwise the standard player start.
- **The return trip:**
  - mission complete → `ReturnToGameLobby` (CP at once; CCP after EndGameStats for 10 s);
  - the lobby then preselects the next chapter, and everyone's character is cleared (FRONTEND_FLOW §2).
  - The party is kept: members stay connected to the host's lobby.

## 2. A player joining a running chapter (C)
**Allowed:** `bAllowJoinInProgress` is the engine default true; CP / CCP don't override it.

**`TnCampaignGame.PostLogin`:**
1. **`OnSetDifficulty()`**: every TnPawn in the world runs `ApplyDifficultyBuffs` again, with the **new** level
   (3·difficulty + humans − 1). So **a join re-scales everyone at once** (§4).
2. **A cutscene in progress is synced:** if a movie is playing (`_MovieParameters`), the joiner gets
   `ClientPlayMovie(file, …, StartTimeInMilliseconds = the elapsed time)` and joins it mid-play.

**Character / trio slot:**
- `TnCoopGame.SetupPC`: a joiner **without** a selected character gets `RespawnFromCheckpoint = false`.
- They pick in the in-map character select (TnUIControllerCoop WaitingOnGameStart → CharacterSelect_GFX, the chapter's trio
  only).
- `NotifyPlayerReadyToSpawn` → **`AiSquad.RemoveMember(name)`**, which **destroys the AI controller** playing that character. The
  human then spawns as that character.
- H: the select offers characters not held by humans; one held by the AI is free to take.
- Special Kismet spawn logic (`TnSeqAct_SpecialSpawnLogic.UseThisLogicForPlayer`) can claim the spawn. Otherwise UI event 5
  (OnRespawn).

**Where the joiner spawns:** `TnCoopGame.InProgress.FindPlayerStart` → **`TnTeammateSpawner`**:
1. if a Kismet spawn-point array is in force (`SetCoopSpawnPointSet`: `_UseSpawnPointArray`), the first of those points that
   fits the joiner's chassis capsule;
2. otherwise **near a teammate**. For each other player record, in record order:
   - skip a teammate who is dead or has no pawn (`AllowTeammateRespawn`) or is on another team;
   - try that teammate's **breadcrumb positions, newest first**. Each player has a ring of **kNumRecordsToKeep 5** positions, sampled
     every **TimeBetweenPlayerLocationRecords 0.25 s**. A sample is skipped when within the location tolerance of the previous one,
     or while dead or standing on a mover (base Physics 7, PHYS_Interpolating);
   - **the first safe location** (Validator `IsSafeSpawnLocation` for the teammate's capsule) becomes a dynamic player start
     with that rotation.
3. Nothing found → `NotifyPlayerSpawnPointNotFound` → `StartClientPlayerWaiting`, retried (H: on the next respawn attempt).

**What else the joiner has:** no checkpoint data is sent to a joiner. The joiner gets the live server state by normal replication:
- actor and destructible states;
- the GRI's **`CurrentObjective`** bank index;
- the replicated objective markers (16 GRI slots);
- `ActiveCheckpoint` (`NotifyCheckpointSet` → GRI.set_ActiveCheckpoint).
- Kismet runs only on the host.
- Transformer health-bar markers are built locally by each client (DoNotReplicate).
- H: the completed-objectives list isn't replicated.

**Joining after a game over:** a player who confirms a character while `GRI._CampaignGameOver` is set and has no pawn goes
straight to the game-over UI (`TnUIControllerCoop.OnCharacterSelected`).

## 3. Leaving (C)
- `TnCoopGame.InProgress.NotifyPlayerRemoved` (and in InProgressOver) → **`AiSquad.AddMember(name)`**. The departed player's
  character returns to the AI squad.
- **The AI squad's Tick spawns a member without a controller once its `CurrentRespawnTimer` runs out** (AIRespawnDelay; not
  authored → class default, H 0). So the AI takes the character back.
- **The leaver's pawn:** H, stock UE3: Logout makes the pawn suicide. `TnCampaignGame.Killed` **excludes `DmgType_Suicided`** from
  the game-over rule, which is why a leaving player doesn't end the game.
- **Difficulty is not re-applied on leave:** there's no Logout override. The buffs stay at the larger player count until the next
  `PostLogin` or the next pawn spawn (which applies the then-current level to that pawn only).
- **Restart rule:** `TnCoopGame.PlayerCanRestart` is true only while **another** player is alive with a pawn.
- **Host leaves:** the listen server goes away, so the session ends for everyone (ONLINE_PARTY_RE §5, D9).

## 4. Difficulty / buff index with join and leave (C)
- Level = `max(0, GameDifficulty × 3 + (GetNumPlayers() − 1))`. Each spawn applies the pawn's list (players:
  DamageCaused / DamageTaken / DamageTakenDowned / AbilityResource; AI: AiVsAi).
- **Re-applied to all pawns** on `OnSetDifficulty`: a difficulty change, **and every PostLogin** (a join).
- On leave, existing pawns keep the old level; newly spawned ones (including respawns and the AI squad member taking over) get
  the lower level.
- This corrects the H in CAMPAIGN_DIFFICULTY_RE §2.1: joins do re-scale immediately.
- H: AddBuff of the same class with a new level replaces or updates the existing one (BuffManager), rather than stacking.

## 5. Downed, revive and game over with two humans (C)
- **Downed:** at 0 health → downed; health drains (TnDamageTypeDownedDrain, scaled by PlayerDamageTakenDowned).
  - A teammate within **500 UU** (robot) / **1000 UU** (plane) revives with `Revive_Heal` (the time is the anim length) →
    **115 health**.
  - The revived player gets the difficulty revive buffs (damage taken ×0.15 / 0.25 / 0.35 for 10 / 7 / 5 s; abilities jammed
    5 / 7 / 10 s).
  - **AI squad members revive downed players too** (the Revive goal).
- **Game over** (`TnCampaignGame`):
  - `Killed`: **any human player killed by anything except `DmgType_Suicided`** → `CampaignGameOver(that PRI)`. That includes
    **bleeding out** and being killed while downed.
  - Also: an **active AI squad member bleeding out** (TnDamageTypeDownedDrain).
  - `NotifyDownedPawn` (each time a pawn goes down): **CampaignGameOver if no same-team TnPawn is still up**. The loop returns
    early, without a game over, if any same-team pawn isn't downed (**AI buddies count**) or if an enemy-team TnPlayerPawn exists.
- **With two humans:**

  | Situation | Result |
  |---|---|
  | **Both downed, an AI trio member standing** | **No game over yet.** The AI can revive them. Either one **bleeding out = game over**. |
  | **Both downed, no AI up** (3 humans, or the AI also down) | **Immediate game over** (NotifyDownedPawn) |
  | **One downed, the other revives in time** | continues |
  | **One quits** | not a game over (suicide excluded). Their character goes back to the AI squad and respawns. The remaining human continues alone. **The difficulty level stays at 2 players until the next join or spawn.** |
  | **The host quits** | the session ends for everyone |

- **Game-over flow:**
  1. `TnCampaignGameOverMessage(WhoDied)` → InProgressOver;
  2. after `_DelayBeforeShowingGameOverScreen` **1 s**: `GRI._CampaignGameOver`, `PC.OnCampaignGameOver` (audio), AI planners
     suspended, UI event 14 → "MISSION FAILED";
  3. **all damage is zeroed** in InProgressOver; no spawns.
  - Only the host can restart (checkpoint or chapter: FRONTEND_FLOW §6). `StartMatch` from InProgressOver → InProgress, where
    BeginState skips the re-setup when coming from InProgressOver.
- **Checkpoint restart for everyone:**
  - host → `RestartFromActiveCheckpoint` (native);
  - clients get `ClientRestartFromCheckpoint(data pieces)` with the checkpoint loading screen;
  - `InProgress.StartMatch` → `AiSquad.RestartGame` (every member: no controller, `ShouldSpawnFromCheckpoint`, objective cleared) +
    `SpawnHelper.RestartingMatch`;
  - players spawn at the checkpoint (`RespawnFromCheckpoint`).

## 6. For the campaign-online build
1. **Travel:** the host's launch URL carries `?MapId` / `?Checkpoint`. Members load it and wait for "all players have
   controllers" plus the host's character before the start.
2. **Join:**
   - re-apply the difficulty buffs to all pawns with the new count;
   - sync a playing cutscene by elapsed ms;
   - the in-map trio select; the chosen character's AI is removed;
   - spawn at a teammate's newest safe breadcrumb (5 × 0.25 s), or the Kismet spawn set;
   - the world, objective and marker state come by replication (for us: the lockstep state transfer).
3. **Leave:** the character returns to the AI squad; no game over; no difficulty re-apply.
4. **Game over** = any human killed (not by leaving) or everyone on the team down. AI buddies standing prevent the "all down"
   end. 1 s delay; damage zeroed; host-only restarts.


**Correction (2026-10-23, CAMPAIGN_SQUAD_AI_RE):**
- AI squad members **never revive**: no buddy goal set has the Revive goal.
- The "AI squad member bled out" game-over clause is dead code, because buddies are never downed.
- With both humans downed and a buddy up there is no immediate game over, but nobody can revive them, so the first bleed-out ends the game.
- In **solo** the human is never downed (`GlobalCanBeDowned` requires more than 1 player).
