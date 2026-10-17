# Campaign PROVISIONAL readings closed (RE, 2026-10-18)

Labels: **CONFIRMED (C)** = read in script bytecode, the binary, or cooked data; **HIGH (H)** = stock UE3 reading with
supporting WFC evidence, not traced end to end; **CORRECTED** = the provisional reading is wrong or incomplete, the correction is C/H as marked.

Scope: every PROVISIONAL in Rebuild `docs/handoff/CAMPAIGN_*.md` + `MP_COLLISION_PHYSMAT.md` (integration/milestone-09c) and
AssetTools `docs/handoff/chapter_notes/*.md`. Part 1 = the five that gate verdicts (Integration's order: 1, 4, 3, 2, 5).

## Part 1 (verdict-gating)

### 1. reachFlags 2 = R_FLY (airborne_by_design) — **CONFIRMED (data)**, enum value H

- **Data census** over all 185,660 cooked `TnReachSpec` objects (authored.db): reachFlags takes exactly the values
  **1 (76,035), 2 (106,873), 8 (2,191), 9 (560)**. These are the stock `EReachSpecFlags` bits R_WALK = 1, R_FLY = 2, R_JUMP = 8,
  and R_WALK | R_JUMP = 9; no other bit appears.
- **Specs leaving air nodes** (`AirPathNode`, `TnTacticalAirPathNode`) are flag 2 in **58,154 of 58,211** cases (99.9 %).
  Flag 2 is the air network's flag.
- The remaining 48,719 flag-2 specs start on ground node classes (TnTacticalPathNode, NavigationCell, PathNode, Checkpoint,
  pickup factories): the links from the ground graph up into the air graph. Ground-only specs are 1 / 8 / 9.
- The engine-side enum (`APawn::calcMoveFlags` / `UReachSpec::supports`) was not located in the binary. The bit meaning is
  stock (H), but the data pins flag 2 to fly-only reach independently.
- **Verdict:** airborne_by_design's reading holds; the A4_ORB / D2_COR exemptions stand.

### 4. Empty SetCurrentObjective Description = "clear" — **CONFIRMED (script)**, with a nuance

Chain: `SeqAct_TnSetCurrentObjective.Activated` → `TnGameReplicationInfoGameBase.SetCurrentObjective` →
`TnObjectiveManager.SetCurrentObjective` → `ClientSetCurrentObjective`.

1. **The action:** `Description != ""` → `Localize("Objectives", Description, "TransGame")`, else `""`; then `GRI.SetCurrentObjective(desc)`.
2. **The manager, empty desc:** `CurrentObjective = -1` → `ClientSetCurrentObjective`:
   - the previous objective (LastObjective) becomes **DeferredCompleteObjective**, i.e. it is shown as completed;
   - `DeferredAddObjective = ""`: no new objective is added;
   - `LastCompletedObjective = LastObjective`; `LastObjective = -1`.
   So "clear" = **complete the current objective and show none**, not a silent wipe.
3. **The manager, non-empty desc:** `GetObjectiveId(desc)`.
   - If found and different, it becomes current; the previous one completes the same way.
   - **Not found:** `LogInternal("Error: objective … not found in world info objective list")` and **no change at all**.
     The CAMPAIGN_DATA_INDEX note "D1 sets one key not in its bank" is therefore a runtime no-op (C), not a clear.

### 3. elevated_above_floor exemption (start 3–30 m above walkable floor) — **CONFIRMED (script)**, falling physics H

- **No floor snap at spawn (C):**
  - `TnGame.SpawnDefaultPawnFor` spawns at exactly `StartSpot.Location`, yaw only.
  - `GameInfo.RestartPlayer` then SetAnchor / Possess / PlayTeleportEffect (MakeNoise only).
  - The 5 m floor trace (`Trace … Location + (0, 0, −500)`, then `HitPosition + CylinderHeight + 1`) exists only in
    `TnPawn.OnTeleport`, not on the spawn path.
- **The pawn falls (H):** with no floor under it the pawn takes PHYS_Falling. The stock `Pawn.SetMovementPhysics` sets
  PHYS_Falling unless it is in water, and `TnPawn` robot / transforming states set `SetPhysics(2)` on teleport. The native
  walking-without-floor transition was not traced.
- **No damage from the drop (C):** `TnPlayerPawn.TakeDamage` returns immediately for `DmgType_Fell` and `DmgType_Crushed`.
  So although a 30 m drop exceeds `Pawn.MaxFallSpeed` 1200 uu/s (Engine CDO), a player spawned there lands unhurt.
- **Verdict:** "the spawn drops onto it" holds; the exemption is sound for player starts / checkpoints.

### 2. Hull physmat rule (BodySetup PhysMaterial, else the first section material's) — **CORRECTED** (C for the order; one link H)

**Located:** the hit → script `HitInfo` PhysMaterial resolver **`0x82F1B370`**, called from the trace natives
(`0x82F22140` / `0x82F21F68` / `0x82F1B488`). It fills (Material, PhysMaterial, Item, LevelIndex, BoneName, HitComponent) from the
check result. The PhysMaterial order, **C from the code**:

1. **the hit component's override** (component +0x118, H: PhysMaterialOverride);
2. else **the check result's own PhysMaterial** (+0x2C: what the collision query reported; H: for a simple-collision hit
   this is the BodySetup PhysMaterial);
3. else **the hit Material's `GetPhysicalMaterial()`** (vtable +0x160; MIC → parent chain → UMaterial.PhysMaterial);
4. else **`0x82C88168`, only when the hit actor `IsA('StaticMeshActor')`** (class looked up by name `L"StaticMeshActor"` in Engine):
   - it walks the StaticMeshComponent's material slots **in order** (`GetMaterial(i)`, vtable +0x208, i < the mesh's element count);
   - it takes **the first slot whose material has a PhysMaterial** (GetPhysicalMaterial, or the base material's +0x38).
   So "the first material" means the first **with** a physmat, not strictly slot 0;
5. else **GEngine.DefaultPhysMaterial** (GEngine +0x25C = `ENV_PHYSMAT_p.Metal_PHYSMAT`, Xe-TransEngine.ini).

**Class hierarchy (C, Engine.xxx export table):** `StaticMeshActor` and **`StaticMeshCollectionActor` are siblings** under
`StaticMeshActorBase`; SMCA is **not** a StaticMeshActor.

**Consequences for AssetTools' `collision_physmat.py`:**
- **StaticMeshCollectionActor members** (most cooked MP / campaign world meshes):
  - a simple-hull hit with no BodySetup PhysMaterial does **not** take the first section material's PhysMaterial;
  - it falls to the hit Material (H: a hull hit reports no material) and then to **Metal**.
  - The `first_material` hull rule should apply only where step 4 applies.
- **StaticMeshActor (+ subclasses) members:** the `first_material` rule holds, refined to "first slot with a PhysMaterial".
- **InterpActor / DynamicSMActor / KActor** derive from DynamicSMActor, not StaticMeshActor (stock hierarchy, H; not read
  here), so they too fall to Metal.
- **Recommendation:** split the 65,842 `first_material` triangles by owning actor class:
  - SMCA / other → `default_metal` (H);
  - StaticMeshActor → `first_material_with_physmat` (C).
  The step-2 link (what the simple-collision line check writes into +0x2C) stays H; its writer was not traced within the 1 h cap.

### 5. Matinee group linked to an empty SeqVar_Object = runtime no-op — **CORRECTED for 23 of 148 groups** (H)

- AssetTools' manifests (`manifests/next_map/*_matinee.json`): 148 groups are `unresolved` via `object_empty` (the doc's 149;
  one differs by the disabled-track filter).
- **Track census (enabled tracks only):**

  | Track classes | Groups |
  |---|---|
  | Move only | 44 |
  | AnimControl only | 38 |
  | AnimControl + Move | 19 |
  | ActorFloatMaterialParam only | 15 |
  | Toggle only | 5 |
  | FloatProp only | 4 |
  | **with an Event track** (Event + Move 8, Event + AnimControl 4, Event + MaterialParam 3, Event + Move + Sound 1, Event + AnimControl + Move 1) | **17** |
  | **with a Sound track** (Sound only 3, + MaterialParam 1, + Move 1, + Toggle 1, + Event + Move 1) | **7** (1 overlaps the Event row) |

- **Stock `USeqAct_Interp::InitInterp` (H; RE 27d1ee0 rated it HIGH for WFC):**
  - a group with no actor still gets a group instance;
  - **Event tracks fire their Kismet outputs regardless of the group actor;**
  - **Sound tracks without an actor play at the listener.**
- **So:** the 125 groups with only actor-targeted tracks (Move / AnimControl / Toggle / material / property) are no-ops (H).
  The **23 groups with an Event or Sound track are not**: their event outputs fire (e.g. A1_IAC `EndPiece` ×4, `DataCube`,
  `Elevator`) and their sounds play.
- The manifests should keep those 23 as **live** for the event / sound tracks.

## Part 2

Items 6–12 follow in the next commit.
