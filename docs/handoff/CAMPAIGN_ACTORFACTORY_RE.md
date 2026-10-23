# SeqAct_ActorFactory / HmPawnFactory stepping (RE, 2026-10-23)

Closes CAMPAIGN_RE_INDEX "Stock ActorFactory stepping (SpawnCount / SpawnDelay / PointSelection)" (was HIGH; now C).
Natives (headless Ghidra; vtables from the classes' internal constructors):
- **SeqAct_ActorFactory:** vtable 0x820B7A90; Activated 0x82F18150, Update 0x82F181F8, SpawnStep 0x82F2C6E0, point ordering
  0x82F1E2A0, OnSpawn 0x82F25E00, Spawned 0x82F33118.
- **SeqAct_HmPawnFactory:** vtable 0x820B7C38; Update 0x8276F100, SpawnStep 0x8275C7A8, Spawned 0x8276F350.
- SeqAct_TnPawnFactory (all 1,180 campaign factories) is a script subclass of HmPawnFactory.

**Links:**
- Inputs: 0 Spawn Actor, 1 Enable, 2 Disable, 3 Toggle, (Hm) 4 Kill All.
- Outputs (Hm): 0 Spawned, 1 Aborted, 2 All Pawns Killed, 3 Single Pawn Killed, 4 All Pawns Spawned.
- **Defaults:** SpawnCount 1, SpawnDelay 0.5 s, bCheckSpawnCollision true, bEnabled true, PointSelection PS_Normal.

## Activated (C)
- **Spawn Actor** (with a Factory set): `bIsSpawning = true`, `SpawnedCount = 0`, **RemainingDelay = 0**. The first spawn happens
  on the next update.
- **Enable / Disable / Toggle** set or clear bEnabled.

## Update, every tick (C)
1. The same three inputs also re-evaluate bEnabled. Spawn Actor while enabled → Activated.
2. **If enabled and spawning** (needs a Factory and ≥ 1 spawn point):
   - while the factory is not busy:
     - `RemainingDelay > 0` → `RemainingDelay −= dt`;
     - else (at most one step per distinct world time) → **SpawnStep**;
   - (busy → a deferred / async path, vtable +0x1A4.)
3. **If not spawning** and `HaveAllPawnsDied()` (script) → output **2 "All Pawns Killed"**, and the op finishes.
4. (Hm) Inputs: Kill All → `KillAllPawns()`. HaveAllPawnsDied also fires output **3 "Single Pawn Killed"** for each
   controller found dead (and removes it).

## SpawnStep (C, 0x82F2C6E0; Hm override 0x8275C7A8)
- **Hm first:** if `HaveMaxPawnsBeenSpawned()` → **spawning stops** (bIsSpawning = false, no spawn).
  - That script counts **every AI-controlled pawn in the world**, not just this factory's, against MaxNumSpawnedInWorld
    (≤ 0 = no cap).
- **Point ordering, applied on every step** (0x82F1E2A0, only with ≥ 2 points):
  - **PS_Random (1):** shuffle the SpawnPoints array (each i swapped with a random index in [i, N)), and LastSpawnIdx = −1.
  - **PS_Reverse (2):** reverse the array in place.
  - PS_Normal (0): unchanged.
- **Pick:** starting from LastSpawnIdx, try idx = (idx + 1) mod N **for each point at most once, stopping at the first success**:
  - skip null points;
  - location = `AdjustSpawnLocation(point)` (script; default = point.Location); rotation = point.Rotation;
  - `Factory.CreateActor(location, rotation, this)`;
  - LastSpawnIdx = idx.
  - A failed spawn (e.g. collision) tries the next point in the same step.
- Then **RemainingDelay = SpawnDelay**.

## OnSpawn / Spawned (C)
- The spawned actor is flagged, `SpawnedCount += 1`.
- It's written to every "Spawned" object variable, and **output 0 fires on each spawn**.
- If the point is a navigation / start point and the spawn is a Pawn, the pawn records its spawn point (pawn +0x2D4).
- **`SpawnedCount ≥ SpawnCount` → bIsSpawning = false.**
  - SpawnCount 0 behaves as 1: the first spawn has already happened when the check runs.
- (Hm) The pawn's controller is appended to CurrentControllerList. When spawning ends, output **4 "All Pawns Spawned"** fires.

## Timeline example (defaults, PS_Normal, 3 points, SpawnCount 3, SpawnDelay 0.5)
t0 Spawn Actor → next tick point[0] → +0.5 s point[1] → +0.5 s point[2] → spawning ends, All Pawns Spawned → when all three are
dead, All Pawns Killed.

Authored campaign data: SpawnCount mostly 1 (default), some 2–4; SpawnDelay mostly 0.5 (default), many 0.1; 63 factories use
PS_Random.
