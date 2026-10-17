# Campaign level-streaming runtime and checkpoint restart (RE)

WFC RE lane, 2026-10-18. Integration copies this to `docs/handoff/CAMPAIGN_STREAMING_RE.md`. It answers the open points of
**`docs/handoff/CAMPAIGN_STREAMING_RUNTIME.md`** (AssetTools); its corrections are listed in §6.

Sources:
- script decompiles: Engine.LevelStreamingState / Checkpoint / GameInfo, HM_Engine.CheckpointManager / HmGameInfo,
  TransGame.TnCheckpointManager / TnCheckpointSaveData / TnPlayerController, TransContent.TnCampaignGame;
- Engine.xxx enums (LevelStreamingState.ELevelTargetState, LevelStreaming.EStreamingState) and bool order;
- headless Ghidra. Queries are in `work/q/st*.txt` (not committed).

## 1. Data model the natives use (CONFIRMED)

**LevelStreaming** (High Moon fields):
- `PackageName` +0x28;
- bools at +0x3C, big-endian masks in declaration order: bShouldBeVisibleInEditor 0x80000000, bBoundingBoxVisible 0x40000000,
  bLocked 0x20000000, **bIsFullyStatic 0x10000000**, bForceIncrementalStream 0x08000000;
- **`LevelTargetState` +0x40** (ELevelTargetState: **0 LTS_Unloaded, 1 LTS_Loaded, 2 LTS_Visible**);
- **`CurrentStreamingState` +0x41** (EStreamingState).

**EStreamingState values:**
- 0 SS_NOT_LOADED
- **1 SS_LOADED_BUT_NOT_IN_WORLD**
- 2 SS_L_PACKAGE_ASYNC_LOADING
- 3 PENDING_ADDTOWORLD
- 4–14: APPLY_LEVEL_PATCHES, MOVE_ACTORS, UPDATE_COMPONENTS, CREATE_BSP_PHYS_MESH, INIT_ACTOR_RB_PHYS, INIT_ACTORS,
  ROUTE_ACTORS_PREBEGINPLAY, ROUTE_ACTORS_BEGINPLAY, FIXUP_CROSS_LEVEL_PATHS, ROUTE_SEQUENCE_BEGINPLAY, SORT_ACTOR_LIST
- **15 SS_L_FULLY_VISIBLE**
- 16–28, the unload stages: PENDING_UNLOAD, FIXUP_CROSSLEVEL_PATHS, CLEAR_OUT_KISMET_REFS, DESTROY_FACTORY_SPAWNED_ACTORS,
  GATHER_ACTOR_COMPONENTS, SHUT_DOWN_PHYSICS_COMPONENTS, TERM_LEVEL_RB_PHYS, REMOVE_PAWNS, REMOVE_PICKUPS, CLEAR_MODEL_COMPONENTS,
  CLEAR_ACTOR_COMPONENTS, FINAL_UNLOAD, WAITING_FOR_GC

**Other structures:**
- **LevelStreamingState:** `Levels` +0x28 / +0x2C, entries of 8 bytes {FName LevelName; flags, with bit 31 = LevelVisible}.
- **WorldInfo:** StreamingLevels +0x2C8 / +0x2CC, **current state +0x2E0**, **InitialStreamingState +0x2E4**.
- **Checkpoint:** **StreamingState +0x278**, Actions +0x288 / +0x28C.

## 2. Natives on LevelStreamingState (CONFIRMED)

| Native | Impl | Behaviour |
|---|---|---|
| `Activate(WI)` | 0x82DD6FE0 | For **every** StreamingLevel: `LevelTargetState = (in this state ? (LevelVisible ? Visible : Loaded) : Unloaded)`. If the state differs from `WI+0x2E0`: store it and notify remote clients (0x82DD0828, H: ClientUpdateLevelStreamingState). **It only sets targets; it does no I/O.** |
| `PreActivate(WI)` | 0x82DD6E40 | For every level: keep the target **only if bIsFullyStatic and in the new state**; otherwise target = Unloaded. Its only callers are the exec and the checkpoint restart (§4). |
| `IsCurrentState` | 0x82DD7178 | True when every level's `CurrentStreamingState` equals its target exactly: Unloaded ↔ SS_NOT_LOADED, Loaded ↔ SS_LOADED_BUT_NOT_IN_WORLD, Visible ↔ SS_L_FULLY_VISIBLE. |
| `IsTargetState` | 0x82DBE0B8 | `WI+0x2E0 == this`. |

**`SeqAct_SetStreamingState`** (native class; outputs **Started / Finished / Aborted**):
- `Activated` (0x82FC37D8): `StreamingState.Activate(WI)` (no PreActivate), then fires **Started** at once.
- Latent update (0x82FC3828): fires **Finished** and ends when `IsCurrentState`. Fires **Aborted** and ends when this state is no
  longer the target, because another SetStreamingState replaced it. Otherwise it waits.

**The streaming tick** (0x82D5FB28, per frame):
- It walks StreamingLevels **in list order**, handling each level independently.
- Per level: should-be-loaded / should-be-visible from the target, async package load, then **AddToWorld across frames** through
  the SS_L_* stages, or the SS_U_* unload stages.
- Loads and unloads are interleaved per level. There is no global "load all, then unload all".
- Time-slicing is skipped while a flush forces blocking.

## 3. Initial state and level start (CONFIRMED)

At map load (`0x82CE8E08` → `0x82CCB718`):
- **If the game starts from a checkpoint belonging to this world:** `Activate(Checkpoint.StreamingState)`.
- **Otherwise** (`0x82CC6FF0`): `Activate(WorldInfo.InitialStreamingState)`, unless the URL has **`?DontLoadInitialStreamingState`**
  (or an engine flag is set). The option is then stripped from the URL.
- Then **`0x82CE8418` → blocking flush** (`0x82D60300`): the initial levels are fully loaded and added before play starts, behind
  the loading screen.

**Flush** (0x82D60420 / 0x82D601C0 / 0x82D60300; `SeqAct_FlushLevelStreaming` is H the same path):
- It runs the streaming tick repeatedly until nothing is pending.
- Between passes it ticks the loading screen (H: `0x82B50CF8(…, 5)`).
- It can force a GC.

## 4. Checkpoints

### 4.1 Setting a checkpoint (CONFIRMED script)

- `SeqAct_SetActiveCheckpoint` → `CheckpointManager.SetActiveCheckpoint(CP)`, which stores `ActiveCheckpointPathName` and calls
  native `GenerateCheckpointData`.
- Then `SeqAct_SaveActiveCheckpoint`, which writes the data to storage via HmQueuedDataReaderWriter.

**Saved data** (`TnCheckpointSaveData.OnPopulateSaveData`, per TnPlayerController):
- PlayerName, CharacterName;
- Health (max of segment health and health), AbilityResource;
- weapons with reserve + clip ammo (vehicle weapons separately; types 5 and 7 skipped);
- equipped weapon moved first.

PARTIAL: the native `GenerateCheckpointData` was not decoded. From the script hooks (`PreSaveGenerateCheckpointData`) it packages
the save data plus the checkpoint path and map. No Kismet variables, pickups or destructibles appear anywhere in the save path.

### 4.2 Restart from checkpoint (`SeqAct_RestartFromActiveCheckpoint` → native `RestartFromCheckpoint` 0x8277F448) (CONFIRMED native)

1. Deserialize the checkpoint data, then `FindObject(checkpoint path)`.
2. **Checkpoint actor not found** (another map, or loading a save from the menu): rebuild the current URL with **`?LoadCheckpoint`**
   and **`?DontLoadInitialStreamingState`** and travel (full map load).
   - The initial state is then skipped (§3).
   - After the load completes (notify 0x32 with LoadCheckpoint in the URL), the same restore as step 3b runs.
3. **Found (same map): an in-place restart with no map reload:**
   1. `Checkpoint.ProcessActions` (native mirror, 0x82A31D28). The actions are e.g. `TnStartingFormOverrideCheckpointAction`.
   2. Notify 0x35 → **restore** (`0x8278B2F8`):
      - send the checkpoint data to co-op clients (`ClientPrepareForCheckpointData` / `ClientSendCheckpointDataPiece`);
      - then **`0x8278A8B0`**:
        - set **target = Unloaded for every level without bIsFullyStatic**, then flush (`0x82D601C0`), so they are unloaded now;
        - optionally make one named level visible and flush blocking (H: the loading level);
        - `Checkpoint.StreamingState.PreActivate` → GC → flush;
        - **`Activate`** → **blocking flush** (`0x82D60300`).
      - The result: **every non-static level is reloaded fresh from its package**, so its actors and Kismet are in their authored
        state. Fully-static levels in the new state are kept.
   3. Then the manager's save-data hooks restore players (`TnCheckpointSaveData.OnDisseminateSaveData` → SelectCharacter +
      `_CheckpointOverrideCharacterData`: health, ability, weapons, ammo).
   4. Each PC gets `StartSpot = Checkpoint.GetStartSpotForPlayer`: the checkpoint for the local player, rotating through
      AuxilliarySpawnPoints for others. Then `TeleportPlayerToCheckpoint` (location, rotation, `ClientSetRotation(…, true)`, camera
      Reset), and the restarting flag is cleared.
4. **Script side of the in-place restart** (CONFIRMED script; native call site H): `HmGameInfo.CleanupBeforeRestartFromLoadedCheckpoint`:
   - destroy all AIControllers and their pawns;
   - per player: stop force feedback, leave cinematic mode, `PlayerCleanupBeforeCheckpointOrTransition`, destroy the pawn;
   - stop all music;
   - **`ResetLevel()`**:
     - `Reset()` on every controller and every actor with ShouldReset;
     - **the game Kismet sequence `Reset()`** (PerformPreGameplaySetup is skipped while restarting);
     - every `SeqEvent_SequenceActivated` in each level re-fires.
   - `TnCampaignGame.ResetLevel` also reactivates the AI planning scheduler.
   - `StartMatchAfterRestartFromCheckpoint` = ClearPause + StartMatch.
5. **Loading screens:** `TnPlayerController.ClientRestartFromCheckpoint` starts `TnMoviePlayer.StartLoadFromCheckpointLoadingMovie(team)`.
   A restart from a saved file shows a "Loading checkpoint" popup until the file is read. PARTIAL: the native notify ids
   0x32 / 0x35 / 0x36 / 0x37 / 0x38 sent to a listener are H loading-screen and travel phases.

**What a restart restores, in short:**
- streamed levels: reloaded fresh, so actors, pickups, destructibles and Kismet are back to authored;
- persistent level: `ResetLevel` (actor `Reset()` and Kismet sequence reset);
- players: from the save data at the checkpoint spot.
- Nothing else is serialized.

## 5. Visibility semantics (CONFIRMED from the enum + IsCurrentState)

- **LTS_Loaded = SS_LOADED_BUT_NOT_IN_WORLD:** the package is in memory, but the level is not added to the world. No actors are
  spawned in the world, so there is **no rendering, collision, ticking or Kismet** (no SequenceBeginPlay until AddToWorld).
- **LTS_Visible = SS_L_FULLY_VISIBLE:** added, with actors initialised, BeginPlay routed and the level's Kismet begun.
- Unloading runs the SS_U_* stages: Kismet refs cleared, factory-spawned actors destroyed, pawns and pickups removed, then the
  final unload / GC.

## 6. Corrections to CAMPAIGN_STREAMING_RUNTIME.md

1. **§2.2:**
   - Initial state CONFIRMED.
   - Start-from-checkpoint uses `Checkpoint.StreamingState` (native, not inferred).
   - Restarts and save loads add `?DontLoadInitialStreamingState`.
   - Map start does a **blocking flush** before play.
2. **§2.3 is wrong in two places:**
   - (a) Activation does not "load targets, then hide/unload the rest". It just sets per-level targets, and the streaming tick then
     processes **each level in StreamingLevels order** (async load, AddToWorld over frames). **Levels present in both the old and
     new state are not touched.**
   - (b) **`Finished` waits for full convergence:** every level exactly at target, including unloads reaching SS_NOT_LOADED and
     visibility. There is also an **`Aborted`** output when another state takes over.
3. **§2.4 Flush:** CONFIRMED blocking, with the loading screen ticked and an optional GC.
4. **§2.5 Restart is native, with two paths:** in place on the same map, or URL travel. The in-place path **unloads every
   non-static level and reloads it** (PreActivate is used only here), runs `ResetLevel` + a Kismet sequence reset, destroys AI and
   player pawns, then respawns at the checkpoint with the saved player data. Gameplay does not continue during it; it is
   blocking.
5. **§2.6 is wrong:** a loaded-hidden level is **not in the world**, so its **Kismet does not run** and its actors don't exist until
   it becomes Visible.
6. **§4.3:** saved data = **player data only** (character, health, ability resource, weapons, ammo) plus the checkpoint and map.
   Kismet, pickups and destructibles are restored by reload / ResetLevel, not by serialization.

**For the rebuild's merged-chapter model:**
- "Visible" = enable the level's nodes, collision, nav, actors and Kismet.
- "Loaded" = treat as absent.
- On a state change, only levels whose target changed switch.
- `Finished` fires once all have switched.
- On checkpoint restart: reset every non-static sublevel to its authored state, reset the persistent level's actors and Kismet,
  then apply the checkpoint's state and respawn players.
